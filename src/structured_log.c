/* Structured logging implementation for Valkey
 *
 * Copyright (c) 2025, Valkey contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Valkey nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "structured_log.h"
#include "server.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

/* Initialize structured logging with default configuration */
void structuredLogInit(void) {
    /* Initialization now handled by server config defaults */
}

/* Cleanup structured logging resources */
void structuredLogCleanup(void) {
    /* Cleanup now handled by server shutdown */
}

/* Get ISO8601 timestamp string */
static sds getISO8601Timestamp(void) {
    struct timeval tv;
    struct tm tm;
    char buf[64];

    gettimeofday(&tv, NULL);
    gmtime_r(&tv.tv_sec, &tm);

    size_t len = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    snprintf(buf + len, sizeof(buf) - len, ".%03dZ", (int)(tv.tv_usec / 1000));

    return sdsnew(buf);
}

/* Get log level string */
static const char *getLevelString(int level) {
    switch (level) {
        case LL_DEBUG: return "debug";
        case LL_VERBOSE: return "verbose";
        case LL_NOTICE: return "notice";
        case LL_WARNING: return "warning";
        default: return "unknown";
    }
}

/* Escape string for JSON output */
static sds escapeJsonString(const char *str) {
    sds escaped = sdsempty();
    if (!str) return escaped;

    while (*str) {
        switch (*str) {
            case '"':  escaped = sdscatlen(escaped, "\\\"", 2); break;
            case '\\': escaped = sdscatlen(escaped, "\\\\", 2); break;
            case '\b': escaped = sdscatlen(escaped, "\\b", 2); break;
            case '\f': escaped = sdscatlen(escaped, "\\f", 2); break;
            case '\n': escaped = sdscatlen(escaped, "\\n", 2); break;
            case '\r': escaped = sdscatlen(escaped, "\\r", 2); break;
            case '\t': escaped = sdscatlen(escaped, "\\t", 2); break;
            default:
                if ((unsigned char)*str < 0x20) {
                    char hex[7];
                    snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)*str);
                    escaped = sdscat(escaped, hex);
                } else {
                    escaped = sdscatlen(escaped, str, 1);
                }
                break;
        }
        str++;
    }
    return escaped;
}

/* Escape string for LogFmt output */
static sds escapeLogFmtString(const char *str) {
    if (!str) return sdsnew("\"\"");

    /* Check if string needs quoting */
    int needs_quote = 0;
    const char *p = str;
    while (*p) {
        if (*p == ' ' || *p == '=' || *p == '"' || *p == '\\' || *p < 0x20) {
            needs_quote = 1;
            break;
        }
        p++;
    }

    if (!needs_quote) {
        return sdsnew(str);
    }

    /* Quote and escape the string */
    sds escaped = sdsnew("\"");
    while (*str) {
        if (*str == '"' || *str == '\\') {
            escaped = sdscatlen(escaped, "\\", 1);
        }
        escaped = sdscatlen(escaped, str, 1);
        str++;
    }
    escaped = sdscatlen(escaped, "\"", 1);
    return escaped;
}

/* Render log in traditional syslog format */
sds renderLogTraditional(int level, const char *message,
                        const log_field *fields, size_t field_count) {
    sds output = sdsempty();

    /* Add timestamp (always included in traditional format) */
    sds ts = getISO8601Timestamp();
    output = sdscatsds(output, ts);
    output = sdscat(output, " ");
    sdsfree(ts);

    /* Add level if configured */
    if (server.log_include_level) {
        output = sdscatprintf(output, "[%s] ", getLevelString(level));
    }

    /* Add process ID if configured */
    if (server.log_include_process_id) {
        output = sdscatprintf(output, "[%d] ", (int)getpid());
    }

    /* Add message */
    output = sdscat(output, message);

    /* Add fields in a readable format */
    for (size_t i = 0; i < field_count; i++) {
        output = sdscatprintf(output, " %s=", fields[i].name);

        switch (fields[i].type) {
            case LOG_FIELD_STRING:
                output = sdscatprintf(output, "%s",
                    fields[i].value.str_value ? fields[i].value.str_value : "null");
                break;
            case LOG_FIELD_INTEGER:
                output = sdscatprintf(output, "%lld", fields[i].value.int_value);
                break;
            case LOG_FIELD_UNSIGNED:
            case LOG_FIELD_CLIENT_ID:
                output = sdscatprintf(output, "%llu", fields[i].value.uint_value);
                break;
            case LOG_FIELD_DOUBLE:
                output = sdscatprintf(output, "%.2f", fields[i].value.double_value);
                break;
            case LOG_FIELD_BOOL:
                output = sdscat(output, fields[i].value.bool_value ? "true" : "false");
                break;
            case LOG_FIELD_MSTIME:
                output = sdscatprintf(output, "%lld", (long long)fields[i].value.time_value);
                break;
            case LOG_FIELD_NODE_ID:
            case LOG_FIELD_TRACE_ID:
            case LOG_FIELD_SPAN_ID:
                output = sdscatprintf(output, "%s",
                    fields[i].value.str_value ? fields[i].value.str_value : "null");
                break;
        }
    }

    return output;
}

/* Render log in LogFmt format */
sds renderLogFmt(int level, const char *message,
                const log_field *fields, size_t field_count) {
    sds output = sdsempty();
    int first = 1;

    /* Add timestamp (always included) */
    sds ts = getISO8601Timestamp();
    output = sdscatprintf(output, "ts=%s", ts);
    sdsfree(ts);
    first = 0;

    /* Add level */
    if (server.log_include_level) {
        if (!first) output = sdscat(output, server.logfmt_field_separator);
        output = sdscatprintf(output, "level=%s", getLevelString(level));
        first = 0;
    }

    /* Add process ID */
    if (server.log_include_process_id) {
        if (!first) output = sdscat(output, server.logfmt_field_separator);
        output = sdscatprintf(output, "pid=%d", (int)getpid());
        first = 0;
    }

    /* Add message */
    if (!first) output = sdscat(output, server.logfmt_field_separator);
    sds escaped_msg = escapeLogFmtString(message);
    output = sdscatprintf(output, "msg=%s", escaped_msg);
    sdsfree(escaped_msg);

    /* Add fields */
    for (size_t i = 0; i < field_count; i++) {
        output = sdscat(output, server.logfmt_field_separator);
        output = sdscatprintf(output, "%s=", fields[i].name);

        switch (fields[i].type) {
            case LOG_FIELD_STRING:
            case LOG_FIELD_NODE_ID:
            case LOG_FIELD_TRACE_ID:
            case LOG_FIELD_SPAN_ID: {
                sds escaped = escapeLogFmtString(fields[i].value.str_value);
                output = sdscatsds(output, escaped);
                sdsfree(escaped);
                break;
            }
            case LOG_FIELD_INTEGER:
                output = sdscatprintf(output, "%lld", fields[i].value.int_value);
                break;
            case LOG_FIELD_UNSIGNED:
            case LOG_FIELD_CLIENT_ID:
                output = sdscatprintf(output, "%llu", fields[i].value.uint_value);
                break;
            case LOG_FIELD_DOUBLE:
                output = sdscatprintf(output, "%.2f", fields[i].value.double_value);
                break;
            case LOG_FIELD_BOOL:
                output = sdscat(output, fields[i].value.bool_value ? "true" : "false");
                break;
            case LOG_FIELD_MSTIME:
                output = sdscatprintf(output, "%lld", (long long)fields[i].value.time_value);
                break;
        }
    }

    return output;
}

/* Render log in JSON format */
sds renderLogJson(int level, const char *message,
                 const log_field *fields, size_t field_count) {
    sds output = sdsnew("{");
    int first = 1;

    /* Add timestamp (always included) */
    sds ts = getISO8601Timestamp();
    output = sdscatprintf(output, "\"timestamp\":\"%s\"", ts);
    sdsfree(ts);
    first = 0;

    /* Add level */
    if (server.log_include_level) {
        if (!first) output = sdscat(output, ",");
        output = sdscatprintf(output, "\"level\":\"%s\"", getLevelString(level));
        first = 0;
    }

    /* Add process ID */
    if (server.log_include_process_id) {
        if (!first) output = sdscat(output, ",");
        output = sdscatprintf(output, "\"pid\":%d", (int)getpid());
        first = 0;
    }

    /* Add message */
    if (!first) output = sdscat(output, ",");
    sds escaped_msg = escapeJsonString(message);
    output = sdscatprintf(output, "\"message\":\"%s\"", escaped_msg);
    sdsfree(escaped_msg);

    /* Add fields */
    for (size_t i = 0; i < field_count; i++) {
        output = sdscat(output, ",");
        output = sdscatprintf(output, "\"%s\":", fields[i].name);

        switch (fields[i].type) {
            case LOG_FIELD_STRING:
            case LOG_FIELD_NODE_ID:
            case LOG_FIELD_TRACE_ID:
            case LOG_FIELD_SPAN_ID: {
                sds escaped = escapeJsonString(fields[i].value.str_value);
                output = sdscatprintf(output, "\"%s\"", escaped);
                sdsfree(escaped);
                break;
            }
            case LOG_FIELD_INTEGER:
                output = sdscatprintf(output, "%lld", fields[i].value.int_value);
                break;
            case LOG_FIELD_UNSIGNED:
            case LOG_FIELD_CLIENT_ID:
                output = sdscatprintf(output, "%llu", fields[i].value.uint_value);
                break;
            case LOG_FIELD_DOUBLE:
                output = sdscatprintf(output, "%.2f", fields[i].value.double_value);
                break;
            case LOG_FIELD_BOOL:
                output = sdscat(output, fields[i].value.bool_value ? "true" : "false");
                break;
            case LOG_FIELD_MSTIME:
                output = sdscatprintf(output, "%lld", (long long)fields[i].value.time_value);
                break;
        }
    }

    output = sdscat(output, "}");
    return output;
}

/* Core structured logging function */
void logStructured(int level, const char *message,
                   const log_field *fields, size_t field_count) {
    /* Fast path: check if this log level is enabled */
    if ((level & 0xff) < server.verbosity) return;

    sds rendered;

    /* Render according to configured format */
    switch (server.log_format) {
        case LOG_FORMAT_LOGFMT:
            rendered = renderLogFmt(level, message, fields, field_count);
            break;
        case LOG_FORMAT_JSON:
            rendered = renderLogJson(level, message, fields, field_count);
            break;
        case LOG_FORMAT_LEGACY:
        default:
            rendered = renderLogTraditional(level, message, fields, field_count);
            break;
    }

    /* Use the existing serverLog infrastructure to actually write the log */
    serverLog(level, "%s", rendered);

    sdsfree(rendered);
}
