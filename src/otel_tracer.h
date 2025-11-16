/*
 * Copyright (c) 2025, Valkey contributors
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __OTEL_TRACER_H
#define __OTEL_TRACER_H

#include "server.h"
#include "dict.h"

/* Trace span structure compatible with OpenTelemetry format */
typedef struct trace_span {
    uint64_t trace_id_high;      /* 128-bit trace ID (high 64 bits) */
    uint64_t trace_id_low;       /* 128-bit trace ID (low 64 bits) */
    uint64_t span_id;            /* 64-bit span ID */
    uint64_t parent_span_id;     /* Parent span ID (0 if root) */
    mstime_t start_time;         /* Span start timestamp */
    mstime_t end_time;           /* Span end timestamp (0 if active) */
    const char *operation_name;  /* Operation being traced */
    dict *tags;                  /* Key-value tags for context */
    int status_code;             /* 0=OK, 1=ERROR */
    sds error_message;           /* Error description if failed */
} trace_span;

/* Tracer configuration */
typedef struct otel_config {
    int enabled;                 /* Enable/disable tracing */
    int sampling_rate;           /* Sampling percentage (1-100) */
    sds exporter_endpoint;       /* OTLP endpoint (e.g., localhost:4317) */
    sds service_name;            /* Service name for traces */
    int batch_size;              /* Spans to batch before export */
    mstime_t export_interval_ms; /* Export interval */
    list *span_buffer;           /* Buffered spans waiting for export */
    trace_span *current_span;    /* Current active span (thread-local) */
} otel_config;

/* Global tracer instance */
extern otel_config *server_tracer;

/* Initialization and cleanup */
void otelTracerInit(void);
void otelTracerCleanup(void);

/* Span lifecycle management */
trace_span *otelSpanStart(const char *operation_name, uint64_t parent_span_id);
void otelSpanEnd(trace_span *span, int status_code);
void otelSpanAddTag(trace_span *span, const char *key, const char *value);
void otelSpanSetError(trace_span *span, const char *error_message);

/* Command tracing */
void traceCommandStart(client *c);
void traceCommandEnd(client *c, int status);

/* Cluster operation tracing */
void traceSlotMigrationStart(int slot, const char *target_node_id);
void traceSlotMigrationProgress(int slot, size_t keys_migrated, size_t total_keys);
void traceSlotMigrationEnd(int slot, int status);

/* Replication tracing */
void traceReplicationSyncStart(client *replica, const char *sync_type);
void traceReplicationSyncEnd(client *replica, int status, size_t bytes_transferred);

/* Span export */
void otelExportSpans(void);
sds otelFormatSpanOTLP(trace_span *span);

#endif /* __OTEL_TRACER_H */
