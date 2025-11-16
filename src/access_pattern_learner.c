/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "access_pattern_learner.h"
#include "server.h"
#include <math.h>
#include <ctype.h>

/* Global pattern learner instance */
pattern_learner *server_pattern_learner = NULL;

/* Initialize the pattern learner */
void patternLearnerInit(void) {
    if (server_pattern_learner) return;

    server_pattern_learner = zmalloc(sizeof(pattern_learner));
    server_pattern_learner->patterns = NULL; /* TODO: Initialize dict when pattern learning is fully implemented */
    server_pattern_learner->max_patterns = server.prefetch_pattern_max_patterns;
    server_pattern_learner->enabled = server.prefetch_pattern_learning_enabled;
    server_pattern_learner->min_confidence = server.prefetch_pattern_min_confidence;
    server_pattern_learner->pattern_ttl_ms = server.prefetch_pattern_ttl_ms;
}

/* Cleanup the pattern learner */
void patternLearnerCleanup(void) {
    if (!server_pattern_learner) return;

    /* TODO: Release dict when fully implemented */
    if (server_pattern_learner->patterns) {
        /* hashtableRelease(server_pattern_learner->patterns); */
    }
    zfree(server_pattern_learner);
    server_pattern_learner = NULL;
}

/* Extract pattern from concrete key by replacing numeric sequences with '*' */
sds extractKeyPattern(sds key) {
    sds pattern = sdsempty();
    int in_numeric = 0;

    for (size_t i = 0; i < sdslen(key); i++) {
        char c = key[i];

        if (isdigit(c)) {
            if (!in_numeric) {
                pattern = sdscat(pattern, "*");
                in_numeric = 1;
            }
            /* Skip subsequent digits */
        } else {
            pattern = sdscatlen(pattern, &c, 1);
            in_numeric = 0;
        }
    }

    return pattern;
}

/* Instantiate a pattern with numeric values from a concrete key */
sds instantiatePattern(sds pattern, sds concrete_key) {
    /* TODO: Full implementation when pattern learning is enabled */
    UNUSED(pattern);
    return sdsdup(concrete_key);
}

/* Calculate confidence score based on access frequency and recency */
void updatePatternConfidence(access_pattern *pattern) {
    if (!pattern) return;

    /* Frequency component: log scale for diminishing returns */
    double frequency_score = log((double)pattern->access_count + 1.0) / 10.0;
    if (frequency_score > 1.0) frequency_score = 1.0;

    /* Recency component: exponential decay */
    mstime_t age_ms = server.mstime - pattern->last_access;
    double recency_score = exp(-(double)age_ms / 3600000.0);  /* Half-life of 1 hour */

    /* Combined confidence score */
    pattern->confidence_score = (frequency_score * 0.6) + (recency_score * 0.4);
}

/* Learn which keys are frequently accessed together */
void learnAccessPattern(robj **keys, int key_count) {
    /* TODO: Full implementation when pattern learning is enabled */
    UNUSED(keys);
    UNUSED(key_count);

    if (!server_pattern_learner || !server_pattern_learner->enabled) return;
    /* Pattern learning functionality will be fully implemented in a future update */
}

/* Prune stale patterns that haven't been accessed recently */
void pruneStalePatterns(void) {
    /* TODO: Full implementation when pattern learning is enabled */
    if (!server_pattern_learner || !server_pattern_learner->enabled) return;
    /* Pattern pruning functionality will be fully implemented in a future update */
}

/* Predict and return related keys based on learned patterns */
list *predictRelatedKeys(robj *key) {
    /* TODO: Full implementation when pattern learning is enabled */
    UNUSED(key);

    if (!server_pattern_learner || !server_pattern_learner->enabled) return NULL;
    /* Predictive prefetching functionality will be fully implemented in a future update */
    return NULL;
}

/* Check if we should prefetch related keys for this key */
int shouldPrefetchRelated(robj *key) {
    /* TODO: Full implementation when pattern learning is enabled */
    UNUSED(key);

    if (!server_pattern_learner || !server_pattern_learner->enabled) return 0;
    /* Predictive prefetching functionality will be fully implemented in a future update */
    return 0;
}
