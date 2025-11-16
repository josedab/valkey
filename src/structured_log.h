/* Structured logging framework for Valkey
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

#ifndef __STRUCTURED_LOG_H
#define __STRUCTURED_LOG_H

#include "server.h"

/* Log field types for structured logging */
typedef enum {
    LOG_FIELD_STRING,
    LOG_FIELD_INTEGER,
    LOG_FIELD_UNSIGNED,
    LOG_FIELD_DOUBLE,
    LOG_FIELD_BOOL,
    LOG_FIELD_MSTIME,
    LOG_FIELD_CLIENT_ID,
    LOG_FIELD_NODE_ID,
    LOG_FIELD_TRACE_ID,
    LOG_FIELD_SPAN_ID
} log_field_type;

/* Named log field for structured logging */
typedef struct log_field {
    const char *name;         /* Field name (e.g., "client_id") */
    log_field_type type;      /* Field value type */
    union {
        const char *str_value;
        long long int_value;
        unsigned long long uint_value;
        double double_value;
        int bool_value;
        mstime_t time_value;
    } value;
} log_field;

/* Initialization */
void structuredLogInit(void);
void structuredLogCleanup(void);

/* Core structured logging function */
void logStructured(int level, const char *message,
                   const log_field *fields, size_t field_count);

/* Format-specific renderers */
sds renderLogTraditional(int level, const char *message,
                        const log_field *fields, size_t field_count);
sds renderLogFmt(int level, const char *message,
                const log_field *fields, size_t field_count);
sds renderLogJson(int level, const char *message,
                 const log_field *fields, size_t field_count);

/* Convenience macros for common logging patterns */

/* Log with client context */
#define LOG_WITH_CLIENT(level, client, msg, ...) \
    do { \
        log_field _fields[] = { \
            {.name = "client_id", .type = LOG_FIELD_UNSIGNED, \
             .value.uint_value = (client)->id}, \
            {.name = "client_addr", .type = LOG_FIELD_STRING, \
             .value.str_value = getClientPeerId(client)}, \
            __VA_ARGS__ \
        }; \
        logStructured(level, msg, _fields, \
                     sizeof(_fields) / sizeof(log_field)); \
    } while(0)

/* Log cluster events with slot and node context */
#define LOG_CLUSTER_EVENT(level, slot, msg, ...) \
    do { \
        log_field _fields[] = { \
            {.name = "slot", .type = LOG_FIELD_INTEGER, \
             .value.int_value = slot}, \
            {.name = "node_id", .type = LOG_FIELD_STRING, \
             .value.str_value = server.cluster->myself->name}, \
            __VA_ARGS__ \
        }; \
        logStructured(level, msg, _fields, \
                     sizeof(_fields) / sizeof(log_field)); \
    } while(0)

/* Log command execution with timing */
#define LOG_COMMAND_EXECUTION(level, client, cmd, duration_us, msg, ...) \
    do { \
        log_field _fields[] = { \
            {.name = "client_id", .type = LOG_FIELD_UNSIGNED, \
             .value.uint_value = (client)->id}, \
            {.name = "command", .type = LOG_FIELD_STRING, \
             .value.str_value = (cmd)->fullname}, \
            {.name = "duration_us", .type = LOG_FIELD_UNSIGNED, \
             .value.uint_value = duration_us}, \
            __VA_ARGS__ \
        }; \
        logStructured(level, msg, _fields, \
                     sizeof(_fields) / sizeof(log_field)); \
    } while(0)

#endif /* __STRUCTURED_LOG_H */
