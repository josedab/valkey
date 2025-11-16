/*
 * Copyright (c) Valkey Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __ACCESS_PATTERN_LEARNER_H
#define __ACCESS_PATTERN_LEARNER_H

#include "server.h"

/* Access pattern for related keys */
typedef struct access_pattern {
    sds key_pattern;              /* Pattern like "user:*:profile" */
    list *commonly_accessed_with; /* Keys accessed in same request */
    uint64_t access_count;        /* Times this pattern was seen */
    mstime_t last_access;         /* Last access timestamp */
    double confidence_score;      /* 0.0-1.0 confidence in pattern */
} access_pattern;

/* Pattern learner configuration */
typedef struct pattern_learner {
    hashtable *patterns;          /* key -> access_pattern */
    size_t max_patterns;          /* Maximum patterns to track */
    int enabled;                  /* Enable/disable learning */
    int min_confidence;           /* Minimum confidence to use pattern (0-100) */
    mstime_t pattern_ttl_ms;      /* How long to keep patterns */
} pattern_learner;

/* Global pattern learner */
extern pattern_learner *server_pattern_learner;

/* Initialization */
void patternLearnerInit(void);
void patternLearnerCleanup(void);

/* Learning operations */
void learnAccessPattern(robj **keys, int key_count);
void updatePatternConfidence(access_pattern *pattern);
void pruneStalePatterns(void);

/* Prediction operations */
list *predictRelatedKeys(robj *key);
int shouldPrefetchRelated(robj *key);

/* Utility functions */
sds extractKeyPattern(sds key);
sds instantiatePattern(sds pattern, sds concrete_key);

#endif /* __ACCESS_PATTERN_LEARNER_H */
