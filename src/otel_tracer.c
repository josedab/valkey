/*
 * Copyright (c) 2025, Valkey contributors
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "server.h"
#include "otel_tracer.h"
#include "dict.h"
#include "monotonic.h"
#include <stdint.h>
#include <stdlib.h>

/* Global tracer instance */
otel_config *server_tracer = NULL;

/* Generate random 64-bit ID for span/trace IDs */
static uint64_t generateRandomId(void) {
    uint64_t id = 0;
    for (int i = 0; i < 8; i++) {
        id = (id << 8) | (rand() % 256);
    }
    return id;
}

/* Initialize the OpenTelemetry tracer */
void otelTracerInit(void) {
    server_tracer = zmalloc(sizeof(otel_config));
    server_tracer->enabled = 0;  /* Disabled by default */
    server_tracer->sampling_rate = 1;  /* 1% default sampling */
    server_tracer->exporter_endpoint = sdsnew("localhost:4317");
    server_tracer->service_name = sdsnew("valkey");
    server_tracer->batch_size = 100;
    server_tracer->export_interval_ms = 5000;  /* 5 seconds */
    server_tracer->span_buffer = listCreate();
    server_tracer->current_span = NULL;
}

/* Cleanup tracer resources */
void otelTracerCleanup(void) {
    if (!server_tracer) return;

    /* Free buffered spans */
    if (server_tracer->span_buffer) {
        listIter li;
        listNode *ln;
        listRewind(server_tracer->span_buffer, &li);
        while ((ln = listNext(&li)) != NULL) {
            trace_span *span = listNodeValue(ln);
            if (span->tags) dictRelease(span->tags);
            if (span->error_message) sdsfree(span->error_message);
            zfree(span);
        }
        listRelease(server_tracer->span_buffer);
    }

    sdsfree(server_tracer->exporter_endpoint);
    sdsfree(server_tracer->service_name);
    zfree(server_tracer);
    server_tracer = NULL;
}

/* Check if we should sample this trace based on sampling rate */
static int shouldSample(void) {
    if (!server_tracer || !server_tracer->enabled) return 0;
    return (rand() % 100) < server_tracer->sampling_rate;
}

/* Start a new trace span */
trace_span *otelSpanStart(const char *operation_name, uint64_t parent_span_id) {
    if (!server_tracer || !server_tracer->enabled || !shouldSample()) {
        return NULL;
    }

    trace_span *span = zmalloc(sizeof(trace_span));

    /* Generate trace and span IDs */
    if (parent_span_id == 0) {
        /* Root span - generate new trace ID */
        span->trace_id_high = generateRandomId();
        span->trace_id_low = generateRandomId();
    } else {
        /* Child span - inherit trace ID from parent */
        if (server_tracer->current_span) {
            span->trace_id_high = server_tracer->current_span->trace_id_high;
            span->trace_id_low = server_tracer->current_span->trace_id_low;
        } else {
            span->trace_id_high = generateRandomId();
            span->trace_id_low = generateRandomId();
        }
    }

    span->span_id = generateRandomId();
    span->parent_span_id = parent_span_id;
    span->start_time = mstime();
    span->end_time = 0;
    span->operation_name = operation_name;
    span->tags = dictCreate(&sdsHashDictType);
    span->status_code = 0;
    span->error_message = NULL;

    return span;
}

/* End a trace span */
void otelSpanEnd(trace_span *span, int status_code) {
    if (!span) return;

    span->end_time = mstime();
    span->status_code = status_code;

    /* Add to buffer for export */
    if (server_tracer && server_tracer->span_buffer) {
        listAddNodeTail(server_tracer->span_buffer, span);

        /* Export if buffer is full */
        if (listLength(server_tracer->span_buffer) >= (unsigned long)server_tracer->batch_size) {
            otelExportSpans();
        }
    }
}

/* Add a tag to a span */
void otelSpanAddTag(trace_span *span, const char *key, const char *value) {
    if (!span || !span->tags) return;

    sds key_copy = sdsnew(key);
    sds value_copy = sdsnew(value);
    dictAdd(span->tags, key_copy, value_copy);
}

/* Set error on a span */
void otelSpanSetError(trace_span *span, const char *error_message) {
    if (!span) return;

    span->status_code = 1;
    if (span->error_message) sdsfree(span->error_message);
    span->error_message = sdsnew(error_message);
}

/* Trace command execution start */
void traceCommandStart(client *c) {
    if (!server_tracer || !server_tracer->enabled || !c->cmd) return;

    /* Create span for command execution */
    trace_span *span = otelSpanStart(c->cmd->fullname, 0);
    if (!span) return;

    /* Add command metadata as tags */
    otelSpanAddTag(span, "command", c->cmd->fullname);

    char argc_str[32];
    snprintf(argc_str, sizeof(argc_str), "%d", c->argc);
    otelSpanAddTag(span, "argc", argc_str);

    if (c->id) {
        char client_id[32];
        snprintf(client_id, sizeof(client_id), "%llu", (unsigned long long)c->id);
        otelSpanAddTag(span, "client_id", client_id);
    }

    /* Store span in client for later retrieval */
    c->trace_span = span;
}

/* Trace command execution end */
void traceCommandEnd(client *c, int status) {
    if (!server_tracer || !server_tracer->enabled || !c->trace_span) return;

    trace_span *span = c->trace_span;

    /* Add execution time */
    mstime_t duration = mstime() - span->start_time;
    char duration_str[32];
    snprintf(duration_str, sizeof(duration_str), "%lld", (long long)duration);
    otelSpanAddTag(span, "duration_ms", duration_str);

    otelSpanEnd(span, status);
    c->trace_span = NULL;
}

/* Trace slot migration start */
void traceSlotMigrationStart(int slot, const char *target_node_id) {
    if (!server_tracer || !server_tracer->enabled) return;

    char operation[128];
    snprintf(operation, sizeof(operation), "slot_migration_%d", slot);

    trace_span *span = otelSpanStart(operation, 0);
    if (!span) return;

    char slot_str[16];
    snprintf(slot_str, sizeof(slot_str), "%d", slot);
    otelSpanAddTag(span, "slot", slot_str);
    otelSpanAddTag(span, "target_node", target_node_id);
    otelSpanAddTag(span, "operation", "slot_migration");

    /* Store span for later updates */
    server_tracer->current_span = span;
}

/* Trace slot migration progress */
void traceSlotMigrationProgress(int slot, size_t keys_migrated, size_t total_keys) {
    if (!server_tracer || !server_tracer->enabled || !server_tracer->current_span) return;

    trace_span *span = server_tracer->current_span;

    char keys_migrated_str[32];
    char total_keys_str[32];
    snprintf(keys_migrated_str, sizeof(keys_migrated_str), "%zu", keys_migrated);
    snprintf(total_keys_str, sizeof(total_keys_str), "%zu", total_keys);

    otelSpanAddTag(span, "keys_migrated", keys_migrated_str);
    otelSpanAddTag(span, "total_keys", total_keys_str);
}

/* Trace slot migration end */
void traceSlotMigrationEnd(int slot, int status) {
    if (!server_tracer || !server_tracer->enabled || !server_tracer->current_span) return;

    trace_span *span = server_tracer->current_span;
    otelSpanEnd(span, status);
    server_tracer->current_span = NULL;
}

/* Trace replication sync start */
void traceReplicationSyncStart(client *replica, const char *sync_type) {
    if (!server_tracer || !server_tracer->enabled) return;

    trace_span *span = otelSpanStart("replication_sync", 0);
    if (!span) return;

    otelSpanAddTag(span, "sync_type", sync_type);

    if (replica->id) {
        char replica_id[32];
        snprintf(replica_id, sizeof(replica_id), "%llu", (unsigned long long)replica->id);
        otelSpanAddTag(span, "replica_id", replica_id);
    }

    replica->trace_span = span;
}

/* Trace replication sync end */
void traceReplicationSyncEnd(client *replica, int status, size_t bytes_transferred) {
    if (!server_tracer || !server_tracer->enabled || !replica->trace_span) return;

    trace_span *span = replica->trace_span;

    char bytes_str[32];
    snprintf(bytes_str, sizeof(bytes_str), "%zu", bytes_transferred);
    otelSpanAddTag(span, "bytes_transferred", bytes_str);

    otelSpanEnd(span, status);
    replica->trace_span = NULL;
}

/* Format span in OTLP JSON format */
sds otelFormatSpanOTLP(trace_span *span) {
    if (!span) return sdsempty();

    sds json = sdsempty();

    /* Build JSON representation of span */
    json = sdscatprintf(json, "{"
        "\"traceId\":\"%016llx%016llx\","
        "\"spanId\":\"%016llx\","
        "\"parentSpanId\":\"%016llx\","
        "\"name\":\"%s\","
        "\"startTimeUnixNano\":%lld,"
        "\"endTimeUnixNano\":%lld,"
        "\"status\":{\"code\":%d}",
        (unsigned long long)span->trace_id_high,
        (unsigned long long)span->trace_id_low,
        (unsigned long long)span->span_id,
        (unsigned long long)span->parent_span_id,
        span->operation_name,
        (long long)(span->start_time * 1000000LL),  /* Convert to nanoseconds */
        (long long)(span->end_time * 1000000LL),
        span->status_code);

    /* Add tags as attributes */
    if (span->tags && dictSize(span->tags) > 0) {
        json = sdscat(json, ",\"attributes\":[");

        dictIterator *di = dictGetIterator(span->tags);
        dictEntry *de;
        int first = 1;

        while ((de = dictNext(di)) != NULL) {
            if (!first) json = sdscat(json, ",");
            first = 0;

            sds key = dictGetKey(de);
            sds value = dictGetVal(de);
            json = sdscatprintf(json, "{\"key\":\"%s\",\"value\":{\"stringValue\":\"%s\"}}",
                               key, value);
        }
        dictReleaseIterator(di);

        json = sdscat(json, "]");
    }

    /* Add error message if present */
    if (span->error_message) {
        json = sdscatprintf(json, ",\"status\":{\"message\":\"%s\"}", span->error_message);
    }

    json = sdscat(json, "}");

    return json;
}

/* Export buffered spans to OTLP collector */
void otelExportSpans(void) {
    if (!server_tracer || !server_tracer->span_buffer) return;

    if (listLength(server_tracer->span_buffer) == 0) return;

    /* Build OTLP export payload */
    sds payload = sdsnew("[");

    listIter li;
    listNode *ln;
    listRewind(server_tracer->span_buffer, &li);

    int first = 1;
    while ((ln = listNext(&li)) != NULL) {
        trace_span *span = listNodeValue(ln);

        if (!first) payload = sdscat(payload, ",");
        first = 0;

        sds span_json = otelFormatSpanOTLP(span);
        payload = sdscatsds(payload, span_json);
        sdsfree(span_json);

        /* Free span resources */
        if (span->tags) dictRelease(span->tags);
        if (span->error_message) sdsfree(span->error_message);
        zfree(span);
    }

    payload = sdscat(payload, "]");

    /* TODO: Send payload to OTLP endpoint via HTTP/gRPC */
    /* For now, just log that we would export */
    serverLog(LL_DEBUG, "OTLP: Would export %lu spans to %s",
             listLength(server_tracer->span_buffer),
             server_tracer->exporter_endpoint);

    sdsfree(payload);

    /* Clear buffer */
    listEmpty(server_tracer->span_buffer);
}
