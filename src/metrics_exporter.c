/*
 * Copyright (c) 2025, Valkey contributors
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "server.h"
#include "metrics_exporter.h"
#include "dict.h"
#include <math.h>

/* Global metrics registry */
metrics_registry *server_metrics = NULL;

/* Storage for standard metrics */
static metric *metrics_commands_duration = NULL;
static metric *metrics_ops_per_sec = NULL;
static metric *metrics_connected_clients = NULL;
static metric *metrics_memory_used_bytes = NULL;
static metric *metrics_memory_fragmentation_ratio = NULL;
static metric *metrics_replication_lag_seconds = NULL;
static metric *metrics_io_thread_utilization = NULL;
static metric *metrics_slot_migration_duration = NULL;
static metric *metrics_cluster_slot_keys = NULL;

/* Initialize metrics exporter */
void metricsExporterInit(void) {
    server_metrics = zmalloc(sizeof(metrics_registry));
    server_metrics->metrics = NULL;
    server_metrics->metric_count = 0;
}

/* Cleanup metrics exporter */
void metricsExporterCleanup(void) {
    if (!server_metrics) return;

    metric *current = server_metrics->metrics;
    while (current) {
        metric *next = current->next;

        /* Free histogram buckets if present */
        if (current->type == METRIC_TYPE_HISTOGRAM && current->value.histogram.buckets) {
            zfree(current->value.histogram.buckets);
        }

        /* Free labels dict */
        if (current->labels) {
            dictRelease(current->labels);
        }

        zfree(current);
        current = next;
    }

    zfree(server_metrics);
    server_metrics = NULL;
}

/* Register a counter metric */
metric *metricsRegisterCounter(const char *name, const char *help,
                               const char **label_names, size_t label_count) {
    metric *m = zmalloc(sizeof(metric));
    m->name = name;
    m->help = help;
    m->type = METRIC_TYPE_COUNTER;
    m->value.counter_value = 0;
    m->labels = NULL;

    if (label_count > 0) {
        m->labels = dictCreate(&sdsHashDictType);
        for (size_t i = 0; i < label_count; i++) {
            dictAdd(m->labels, sdsnew(label_names[i]), sdsnew(""));
        }
    }

    /* Add to registry */
    m->next = server_metrics->metrics;
    server_metrics->metrics = m;
    server_metrics->metric_count++;

    return m;
}

/* Register a gauge metric */
metric *metricsRegisterGauge(const char *name, const char *help,
                             const char **label_names, size_t label_count) {
    metric *m = zmalloc(sizeof(metric));
    m->name = name;
    m->help = help;
    m->type = METRIC_TYPE_GAUGE;
    m->value.gauge_value = 0.0;
    m->labels = NULL;

    if (label_count > 0) {
        m->labels = dictCreate(&sdsHashDictType);
        for (size_t i = 0; i < label_count; i++) {
            dictAdd(m->labels, sdsnew(label_names[i]), sdsnew(""));
        }
    }

    /* Add to registry */
    m->next = server_metrics->metrics;
    server_metrics->metrics = m;
    server_metrics->metric_count++;

    return m;
}

/* Register a histogram metric */
metric *metricsRegisterHistogram(const char *name, const char *help,
                                 double *bucket_bounds, size_t bucket_count) {
    metric *m = zmalloc(sizeof(metric));
    m->name = name;
    m->help = help;
    m->type = METRIC_TYPE_HISTOGRAM;
    m->labels = NULL;

    /* Initialize histogram buckets */
    m->value.histogram.buckets = zmalloc(sizeof(histogram_bucket) * bucket_count);
    m->value.histogram.bucket_count = bucket_count;
    m->value.histogram.total_count = 0;
    m->value.histogram.sum = 0.0;

    for (size_t i = 0; i < bucket_count; i++) {
        m->value.histogram.buckets[i].upper_bound = bucket_bounds[i];
        m->value.histogram.buckets[i].count = 0;
    }

    /* Add to registry */
    m->next = server_metrics->metrics;
    server_metrics->metrics = m;
    server_metrics->metric_count++;

    return m;
}

/* Increment a counter metric */
void metricsIncrementCounter(metric *m, const char **label_values) {
    if (!m || m->type != METRIC_TYPE_COUNTER) return;

    /* TODO: Handle label values */
    m->value.counter_value++;
}

/* Set a gauge metric value */
void metricsSetGauge(metric *m, double value, const char **label_values) {
    if (!m || m->type != METRIC_TYPE_GAUGE) return;

    /* TODO: Handle label values */
    m->value.gauge_value = value;
}

/* Observe a value in a histogram */
void metricsObserveHistogram(metric *m, double value, const char **label_values) {
    if (!m || m->type != METRIC_TYPE_HISTOGRAM) return;

    /* Update histogram statistics */
    m->value.histogram.total_count++;
    m->value.histogram.sum += value;

    /* Find appropriate bucket */
    for (size_t i = 0; i < m->value.histogram.bucket_count; i++) {
        if (value <= m->value.histogram.buckets[i].upper_bound) {
            m->value.histogram.buckets[i].count++;
        }
    }
}

/* Render metrics in Prometheus text format */
sds metricsExporterRender(void) {
    sds output = sdsempty();

    if (!server_metrics) return output;

    metric *m = server_metrics->metrics;
    while (m) {
        /* HELP line */
        output = sdscatprintf(output, "# HELP %s %s\n", m->name, m->help);

        /* TYPE line */
        const char *type_str = "untyped";
        switch (m->type) {
            case METRIC_TYPE_COUNTER:
                type_str = "counter";
                break;
            case METRIC_TYPE_GAUGE:
                type_str = "gauge";
                break;
            case METRIC_TYPE_HISTOGRAM:
                type_str = "histogram";
                break;
            case METRIC_TYPE_SUMMARY:
                type_str = "summary";
                break;
        }
        output = sdscatprintf(output, "# TYPE %s %s\n", m->name, type_str);

        /* Metric value */
        if (m->type == METRIC_TYPE_COUNTER) {
            output = sdscatprintf(output, "%s %llu\n",
                                 m->name, (unsigned long long)m->value.counter_value);
        } else if (m->type == METRIC_TYPE_GAUGE) {
            output = sdscatprintf(output, "%s %.6f\n",
                                 m->name, m->value.gauge_value);
        } else if (m->type == METRIC_TYPE_HISTOGRAM) {
            /* Histogram buckets */
            for (size_t i = 0; i < m->value.histogram.bucket_count; i++) {
                output = sdscatprintf(output, "%s_bucket{le=\"%.3f\"} %llu\n",
                                     m->name,
                                     m->value.histogram.buckets[i].upper_bound,
                                     (unsigned long long)m->value.histogram.buckets[i].count);
            }

            /* +Inf bucket */
            output = sdscatprintf(output, "%s_bucket{le=\"+Inf\"} %llu\n",
                                 m->name,
                                 (unsigned long long)m->value.histogram.total_count);

            /* Sum and count */
            output = sdscatprintf(output, "%s_sum %.6f\n", m->name, m->value.histogram.sum);
            output = sdscatprintf(output, "%s_count %llu\n",
                                 m->name, (unsigned long long)m->value.histogram.total_count);
        }

        m = m->next;
    }

    return output;
}

/* HTTP handler for /metrics endpoint */
void metricsHttpHandler(client *c) {
    /* Update current metric values before export */
    if (metrics_ops_per_sec) {
        metricsSetGauge(metrics_ops_per_sec, getInstantaneousMetric(STATS_METRIC_COMMAND), NULL);
    }

    if (metrics_connected_clients) {
        metricsSetGauge(metrics_connected_clients, listLength(server.clients), NULL);
    }

    if (metrics_memory_used_bytes) {
        metricsSetGauge(metrics_memory_used_bytes, zmalloc_used_memory(), NULL);
    }

    if (metrics_memory_fragmentation_ratio) {
        /* TODO: Calculate actual fragmentation ratio */
        metricsSetGauge(metrics_memory_fragmentation_ratio, 1.0, NULL);
    }

    /* Render metrics */
    sds metrics_output = metricsExporterRender();

    /* Send HTTP response */
    sds response = sdsnew("HTTP/1.1 200 OK\r\n");
    response = sdscat(response, "Content-Type: text/plain; version=0.0.4\r\n");
    response = sdscatprintf(response, "Content-Length: %zu\r\n", sdslen(metrics_output));
    response = sdscat(response, "Connection: close\r\n\r\n");
    response = sdscatsds(response, metrics_output);

    addReplyProto(c, response, sdslen(response));

    sdsfree(response);
    sdsfree(metrics_output);
}

/* Initialize standard Valkey metrics */
void initStandardMetrics(void) {
    /* Command latency histogram */
    double latency_buckets[] = {0.001, 0.005, 0.01, 0.025, 0.05, 0.1,
                                0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
    metrics_commands_duration = metricsRegisterHistogram(
        "valkey_commands_duration_seconds",
        "Command execution latency distribution",
        latency_buckets,
        sizeof(latency_buckets) / sizeof(double)
    );

    /* Operations per second gauge */
    metrics_ops_per_sec = metricsRegisterGauge(
        "valkey_ops_per_sec",
        "Operations executed per second",
        NULL, 0
    );

    /* Connected clients gauge */
    metrics_connected_clients = metricsRegisterGauge(
        "valkey_connected_clients",
        "Number of connected clients",
        NULL, 0
    );

    /* Memory usage gauge */
    metrics_memory_used_bytes = metricsRegisterGauge(
        "valkey_memory_used_bytes",
        "Total memory used by Valkey in bytes",
        NULL, 0
    );

    /* Memory fragmentation ratio */
    metrics_memory_fragmentation_ratio = metricsRegisterGauge(
        "valkey_memory_fragmentation_ratio",
        "Memory fragmentation ratio",
        NULL, 0
    );

    /* Replication lag */
    const char *replica_labels[] = {"replica_id"};
    metrics_replication_lag_seconds = metricsRegisterGauge(
        "valkey_replication_lag_seconds",
        "Replication lag in seconds per replica",
        replica_labels, 1
    );

    /* I/O thread utilization */
    const char *thread_labels[] = {"thread_id"};
    metrics_io_thread_utilization = metricsRegisterGauge(
        "valkey_io_thread_utilization",
        "I/O thread busy percentage",
        thread_labels, 1
    );

    /* Cluster slot migration duration */
    double migration_buckets[] = {1.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0};
    metrics_slot_migration_duration = metricsRegisterHistogram(
        "valkey_cluster_slot_migration_duration_seconds",
        "Slot migration duration distribution",
        migration_buckets,
        sizeof(migration_buckets) / sizeof(double)
    );

    /* Cluster slot keys count */
    const char *slot_labels[] = {"slot"};
    metrics_cluster_slot_keys = metricsRegisterGauge(
        "valkey_cluster_slot_keys_count",
        "Number of keys per slot",
        slot_labels, 1
    );
}
