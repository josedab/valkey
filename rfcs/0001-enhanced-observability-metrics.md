# RFC 0001: Enhanced Observability and Distributed Tracing

## Summary

This RFC proposes a comprehensive observability framework for Valkey that includes OpenTelemetry integration for distributed tracing, Prometheus-compatible metrics export, and enhanced per-slot metrics for cluster deployments. This will significantly improve production debugging, performance analysis, and capacity planning capabilities.

## Motivation

### Current State

Valkey currently has:
- Basic latency monitoring via [`latency.h`](src/latency.h:1-116)
- Limited LTTNG support in [`trace.c`](src/trace/trace.c:1-35)
- Per-command histograms (underutilized)
- Minimal per-slot metrics in clustering via [`cluster_slot_stats.c`](src/cluster_slot_stats.c:1-340)

### Problems Identified

1. **Production Debugging Challenges**: Difficult to trace request flow across cluster nodes, especially during migrations and failovers
2. **Insufficient Performance Metrics**: Lack of granular metrics prevents bottleneck identification and optimization
3. **Limited Capacity Planning**: No historical metrics or trend analysis capabilities
4. **No SLA Monitoring**: Missing percentile tracking (P50, P95, P99) for operation latencies
5. **Cluster Visibility Gaps**: Insufficient per-slot metrics for understanding hotspots and imbalances

### Use Cases

- **Production Incident Response**: Trace a slow query across multiple cluster nodes to identify the root cause
- **Performance Optimization**: Identify hot keys, slow commands, and resource bottlenecks through detailed metrics
- **Capacity Planning**: Use historical metrics to predict resource needs and plan scaling
- **SLA Compliance**: Monitor and report on P99 latency for business-critical operations
- **Cluster Rebalancing**: Use per-slot metrics to identify imbalanced shards and optimize slot distribution

## Detailed Design

### Phase 1: OpenTelemetry Integration (4-6 weeks)

#### Core Tracing Infrastructure

```c
// New file: src/otel_tracer.h

#ifndef __OTEL_TRACER_H
#define __OTEL_TRACER_H

#include "server.h"
#include "hashtable.h"

/* Trace span structure compatible with OpenTelemetry format */
typedef struct trace_span {
    uint64_t trace_id_high;      /* 128-bit trace ID (high 64 bits) */
    uint64_t trace_id_low;       /* 128-bit trace ID (low 64 bits) */
    uint64_t span_id;            /* 64-bit span ID */
    uint64_t parent_span_id;     /* Parent span ID (0 if root) */
    mstime_t start_time;         /* Span start timestamp */
    mstime_t end_time;           /* Span end timestamp (0 if active) */
    const char *operation_name;  /* Operation being traced */
    hashtable *tags;             /* Key-value tags for context */
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
```

#### Command Execution Tracing

Integration points in [`networking.c`](src/networking.c):

```c
/* In processCommandAndResetClient() after command lookup */
void processCommandAndResetClient(client *c) {
    /* ... existing code ... */
    
    /* Start tracing span for command execution */
    if (server_tracer && server_tracer->enabled) {
        traceCommandStart(c);
    }
    
    /* Execute command */
    call(c, CMD_CALL_FULL);
    
    /* End tracing span */
    if (server_tracer && server_tracer->enabled) {
        traceCommandEnd(c, c->lastcmd->proc == c->cmd->proc ? 0 : 1);
    }
    
    /* ... existing code ... */
}
```

#### Cluster Migration Tracing

Integration in [`cluster_migrateslots.c`](src/cluster_migrateslots.c):

```c
/* Trace slot migration lifecycle */
void slotMigrationStart(slotMigrationJob *job) {
    if (server_tracer && server_tracer->enabled) {
        char target_node[CLUSTER_NAMELEN];
        memcpy(target_node, job->target_node->name, CLUSTER_NAMELEN);
        traceSlotMigrationStart(job->slot, target_node);
    }
    /* ... existing migration code ... */
}

void slotMigrationExportKeyCallback(void *privdata, const void *key) {
    slotMigrationJob *job = privdata;
    job->keys_migrated++;
    
    /* Periodic progress updates to trace */
    if (server_tracer && server_tracer->enabled && 
        job->keys_migrated % 1000 == 0) {
        traceSlotMigrationProgress(job->slot, job->keys_migrated, 
                                   job->total_keys);
    }
    /* ... existing export code ... */
}
```

### Phase 2: Prometheus Metrics Export (2-3 weeks)

#### Metrics Infrastructure

```c
// New file: src/metrics_exporter.h

#ifndef __METRICS_EXPORTER_H
#define __METRICS_EXPORTER_H

#include "server.h"

/* Metric types following Prometheus conventions */
typedef enum {
    METRIC_TYPE_COUNTER,    /* Monotonically increasing counter */
    METRIC_TYPE_GAUGE,      /* Value that can go up or down */
    METRIC_TYPE_HISTOGRAM,  /* Distribution with buckets */
    METRIC_TYPE_SUMMARY     /* Statistical distribution */
} metric_type;

/* Histogram bucket definition */
typedef struct histogram_bucket {
    double upper_bound;     /* Bucket upper bound (inclusive) */
    uint64_t count;         /* Observations in this bucket */
} histogram_bucket;

/* Metric value union */
typedef union metric_value {
    uint64_t counter_value;
    double gauge_value;
    struct {
        histogram_bucket *buckets;
        size_t bucket_count;
        uint64_t total_count;
        double sum;
    } histogram;
} metric_value;

/* Metric definition */
typedef struct metric {
    const char *name;       /* Metric name (snake_case) */
    const char *help;       /* Help text describing metric */
    metric_type type;       /* Metric type */
    hashtable *labels;      /* Label dimensions */
    metric_value value;     /* Current value */
    struct metric *next;    /* Next metric in registry */
} metric;

/* Metrics registry */
typedef struct metrics_registry {
    metric *metrics;        /* Linked list of metrics */
    int metric_count;       /* Total registered metrics */
} metrics_registry;

/* Global metrics registry */
extern metrics_registry *server_metrics;

/* Initialization */
void metricsExporterInit(void);
void metricsExporterCleanup(void);

/* Metric registration */
metric *metricsRegisterCounter(const char *name, const char *help, 
                               const char **label_names, size_t label_count);
metric *metricsRegisterGauge(const char *name, const char *help,
                             const char **label_names, size_t label_count);
metric *metricsRegisterHistogram(const char *name, const char *help,
                                 double *bucket_bounds, size_t bucket_count);

/* Metric updates */
void metricsIncrementCounter(metric *m, const char **label_values);
void metricsSetGauge(metric *m, double value, const char **label_values);
void metricsObserveHistogram(metric *m, double value, const char **label_values);

/* Export to Prometheus text format */
sds metricsExporterRender(void);

/* HTTP endpoint handler for /metrics */
void metricsHttpHandler(client *c);

#endif /* __METRICS_EXPORTER_H */
```

#### Standard Metrics to Export

```c
/* Core metrics initialization */
void initStandardMetrics(void) {
    /* Command latency histogram */
    double latency_buckets[] = {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 
                                0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
    server.metrics.commands_duration = metricsRegisterHistogram(
        "valkey_commands_duration_seconds",
        "Command execution latency distribution",
        latency_buckets,
        sizeof(latency_buckets) / sizeof(double)
    );
    
    /* Operations per second gauge */
    server.metrics.ops_per_sec = metricsRegisterGauge(
        "valkey_ops_per_sec",
        "Operations executed per second",
        NULL, 0
    );
    
    /* Connected clients gauge */
    server.metrics.connected_clients = metricsRegisterGauge(
        "valkey_connected_clients",
        "Number of connected clients",
        NULL, 0
    );
    
    /* Memory usage gauge */
    server.metrics.memory_used_bytes = metricsRegisterGauge(
        "valkey_memory_used_bytes",
        "Total memory used by Valkey in bytes",
        NULL, 0
    );
    
    /* Memory fragmentation ratio */
    server.metrics.memory_fragmentation_ratio = metricsRegisterGauge(
        "valkey_memory_fragmentation_ratio",
        "Memory fragmentation ratio",
        NULL, 0
    );
    
    /* Replication lag */
    const char *replica_labels[] = {"replica_id"};
    server.metrics.replication_lag_seconds = metricsRegisterGauge(
        "valkey_replication_lag_seconds",
        "Replication lag in seconds per replica",
        replica_labels, 1
    );
    
    /* I/O thread utilization */
    const char *thread_labels[] = {"thread_id"};
    server.metrics.io_thread_utilization = metricsRegisterGauge(
        "valkey_io_thread_utilization",
        "I/O thread busy percentage",
        thread_labels, 1
    );
    
    /* Cluster slot migration duration */
    double migration_buckets[] = {1.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0};
    server.metrics.slot_migration_duration = metricsRegisterHistogram(
        "valkey_cluster_slot_migration_duration_seconds",
        "Slot migration duration distribution",
        migration_buckets,
        sizeof(migration_buckets) / sizeof(double)
    );
    
    /* Cluster slot keys count */
    const char *slot_labels[] = {"slot"};
    server.metrics.cluster_slot_keys = metricsRegisterGauge(
        "valkey_cluster_slot_keys_count",
        "Number of keys per slot",
        slot_labels, 1
    );
}
```

### Phase 3: Enhanced Per-Slot Metrics (2 weeks)

#### Extended Slot Statistics

Enhance [`cluster_slot_stats.c`](src/cluster_slot_stats.c:1-340):

```c
/* Enhanced slot statistics structure */
typedef struct enhanced_slot_stats {
    /* Existing network bytes tracking */
    unsigned long long network_bytes_in;
    unsigned long long network_bytes_out;
    
    /* NEW: Key and memory tracking */
    unsigned long long key_count;
    unsigned long long memory_usage_bytes;
    
    /* NEW: Operation counters */
    unsigned long long ops_per_sec;
    unsigned long long read_ops;
    unsigned long long write_ops;
    
    /* NEW: Latency tracking */
    mstime_t total_command_duration_us;
    mstime_t command_count;
    mstime_t p50_latency_us;
    mstime_t p95_latency_us;
    mstime_t p99_latency_us;
    
    /* NEW: Latency histogram for percentile calculation */
    uint64_t latency_histogram[64];  /* Logarithmic buckets */
} enhanced_slot_stats;

/* Update slot statistics on command execution */
void updateSlotStats(int slot, mstime_t duration_us, int is_write) {
    enhanced_slot_stats *stats = &server.cluster->slots_stats[slot];
    
    stats->command_count++;
    stats->total_command_duration_us += duration_us;
    
    if (is_write) {
        stats->write_ops++;
    } else {
        stats->read_ops++;
    }
    
    /* Update latency histogram for percentile calculation */
    int bucket = logBucket(duration_us);
    stats->latency_histogram[bucket]++;
    
    /* Periodically calculate percentiles */
    if (stats->command_count % 1000 == 0) {
        calculateSlotLatencyPercentiles(slot);
    }
}
```

## Drawbacks

### Performance Overhead

- **Tracing Impact**: OpenTelemetry tracing adds ~1-5% CPU overhead when enabled
  - Mitigation: Configurable sampling rates (default: 1% of requests)
  - Fast path: Minimal overhead when disabled (single branch check)

- **Metrics Collection**: Incrementing counters and histograms adds ~0.1-0.5% overhead
  - Mitigation: Use atomic operations for lock-free updates
  - Batch metric exports to reduce network overhead

### Memory Overhead

- **Trace Spans**: Each active span requires ~200 bytes
  - Mitigation: Configurable buffer limits with LRU eviction
  - Batch export to external collector, don't store indefinitely

- **Metrics Storage**: Each metric with labels requires ~100-500 bytes
  - Mitigation: Limited label cardinality (e.g., max 16384 slots)
  - Pre-allocated metric structures

### Implementation Complexity

- **External Dependencies**: OpenTelemetry libraries may increase binary size
  - Mitigation: Make observability features optional at compile time
  - Use lightweight C implementations of OTLP protocol

- **Backward Compatibility**: New metrics/tracing shouldn't break existing monitoring
  - Mitigation: Additive changes only, preserve existing `INFO` command output
  - Separate `/metrics` endpoint for Prometheus format

## Alternatives

### Alternative 1: StatsD Integration Instead of Prometheus

**Pros**:
- Simpler protocol
- Lower memory overhead
- Widely supported

**Cons**:
- No built-in pull model
- Less expressive metric types
- Requires additional aggregation service

**Decision**: Prometheus chosen due to wider adoption in cloud-native ecosystem and superior querying capabilities.

### Alternative 2: Custom Binary Tracing Format

**Pros**:
- Smaller overhead
- More control over format
- No external dependencies

**Cons**:
- Limited ecosystem integration
- Requires custom tooling
- Higher maintenance burden

**Decision**: OpenTelemetry chosen for industry standardization and existing tool support.

### Alternative 3: Sampling in Application vs. Collector

**Pros (Application-side)**:
- Reduced network traffic
- Lower collector load

**Cons**:
- May miss important traces
- Less flexible sampling strategies

**Decision**: Implement application-side sampling with configurable rate, allow collector-side sampling override.

## Unresolved Questions

1. **Metrics Cardinality Management**: How to handle high-cardinality labels (e.g., client IPs) without exploding memory?
   - **Proposed**: Limit label cardinality, aggregate rare labels into "other" category
   - **Need**: Community feedback on acceptable cardinality limits

2. **Trace Context Propagation**: How should trace context be propagated in Redis protocol?
   - **Option A**: Custom `TRACEPARENT` command to set context
   - **Option B**: Use client metadata mechanism
   - **Need**: TSC decision on protocol extension

3. **Storage Backend for Traces**: Should Valkey include embedded trace storage or only export?
   - **Proposed**: Export-only initially, avoid embedding complex storage
   - **Future**: Consider optional embedded storage for development/debugging

4. **Metrics Retention**: How long to retain in-memory metrics before aggregation?
   - **Proposed**: 1-minute rolling window for rate calculations
   - **Need**: Validate memory impact with realistic workloads

5. **Multi-Shard Tracing**: How to correlate traces across cluster resharding operations?
   - **Proposed**: Include shard epoch in trace metadata
   - **Need**: Design review for complex migration scenarios

## Implementation Plan

### Milestone 1: Core Infrastructure (Weeks 1-2)

- [ ] Implement `otel_tracer.h` with span lifecycle management
- [ ] Add trace context to `client` structure
- [ ] Implement OTLP exporter with batching
- [ ] Add configuration options (`trace-enabled`, `trace-sampling-rate`, etc.)
- [ ] Unit tests for tracing infrastructure

### Milestone 2: Command Tracing (Weeks 3-4)

- [ ] Integrate tracing into command execution path
- [ ] Add span tags for command type, keys, execution time
- [ ] Implement trace sampling logic
- [ ] Integration tests for command tracing

### Milestone 3: Cluster Tracing (Weeks 5-6)

- [ ] Add tracing to slot migration operations
- [ ] Trace cluster message propagation
- [ ] Implement cross-node trace correlation
- [ ] Integration tests for cluster tracing

### Milestone 4: Prometheus Metrics (Weeks 7-8)

- [ ] Implement `metrics_exporter.h` with registry
- [ ] Add standard metrics (ops/sec, latency, memory, etc.)
- [ ] Implement `/metrics` HTTP endpoint
- [ ] Add Prometheus text format renderer

### Milestone 5: Enhanced Slot Metrics (Weeks 9-10)

- [ ] Extend slot stats with key count, memory, latency
- [ ] Implement percentile calculation for slot latency
- [ ] Expose slot metrics via Prometheus
- [ ] Performance testing and optimization

### Milestone 6: Documentation and Testing (Weeks 11-12)

- [ ] Write user documentation for observability features
- [ ] Create example Grafana dashboards
- [ ] Write operator guide for production deployment
- [ ] Comprehensive integration testing
- [ ] Performance benchmarking

## Success Metrics

### Quantitative

- **Adoption**: >50% of production deployments enable metrics export within 6 months
- **Performance**: <2% CPU overhead with 1% trace sampling
- **Debugging Time**: 50% reduction in mean time to diagnose production issues
- **Coverage**: 100% of critical code paths instrumented with tracing

### Qualitative

- **Developer Experience**: Positive feedback from operators on debugging capabilities
- **Integration**: Successful integration with popular observability stacks (Grafana, Datadog, New Relic)
- **Community**: Active contributions of custom dashboards and alert rules

## References

- [OpenTelemetry Specification](https://opentelemetry.io/docs/specs/otel/)
- [Prometheus Exposition Formats](https://prometheus.io/docs/instrumenting/exposition_formats/)
- [Distributed Tracing in Practice (book)](https://www.oreilly.com/library/view/distributed-tracing-in/9781492056621/)
- [Google Dapper Paper](https://research.google/pubs/pub36356/)
- [Existing Valkey Latency Monitoring](src/latency.h:1-116)
- [Current Cluster Slot Stats](src/cluster_slot_stats.c:1-340)

## Changelog

- **2025-11-16**: Initial RFC draft based on comprehensive codebase analysis