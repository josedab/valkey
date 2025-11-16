/*
 * Copyright (c) 2025, Valkey contributors
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __METRICS_EXPORTER_H
#define __METRICS_EXPORTER_H

#include "server.h"
#include "dict.h"

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
    dict *labels;           /* Label dimensions */
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

/* Initialize standard Valkey metrics */
void initStandardMetrics(void);

#endif /* __METRICS_EXPORTER_H */
