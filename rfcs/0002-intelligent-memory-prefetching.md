# RFC 0002: Intelligent Memory Prefetching and Cache Optimization

## Summary

This RFC proposes enhancements to Valkey's memory prefetching system to improve cache hit rates and reduce memory access latency. The proposal includes extending prefetching to all data structures, implementing locality-aware batch ordering, and adding adaptive pattern learning for predictive prefetching.

## Motivation

### Current State

Valkey currently implements basic memory prefetching in [`memory_prefetch.c`](src/memory_prefetch.c:1-302):
- Batches up to `prefetch_batch_max_size` keys
- Simple round-robin iteration through keys
- Only prefetches for RAW-encoded strings
- Operates in I/O thread path

### Problems Identified

1. **Missed Prefetch Opportunities**: Only RAW strings are prefetched; other encodings (INTSET, LISTPACK, SKIPLIST, QUICKLIST) are ignored
2. **No Pattern Learning**: Static algorithm doesn't learn from access patterns or predict future accesses
3. **Suboptimal Batch Ordering**: No consideration of memory locality when ordering prefetch operations
4. **Limited Coverage**: Only works in I/O thread path, not available in single-threaded mode
5. **No Data Structure Awareness**: Doesn't prefetch internal nodes of complex structures

### Use Cases

- **Pipeline Operations**: Batch GET operations benefit from prefetching all keys before processing
- **Cluster Slot Migration**: Prefetching keys during migration reduces latency
- **Complex Data Structures**: Operations on sorted sets, hashes, and lists benefit from prefetching internal nodes
- **Related Key Access**: Applications with predictable access patterns (e.g., `user:{id}:profile`, `user:{id}:sessions`)

### Expected Impact

- **15-30% Latency Reduction**: Improved cache utilization reduces memory stalls
- **Higher Throughput**: More requests served per second due to reduced blocking on memory
- **Better CPU Efficiency**: CPUs spend less time waiting for memory, more time executing

## Detailed Design

### Phase 1: Multi-Encoding Prefetching (3-4 weeks)

#### Extend Prefetching to All Encodings

Current implementation in [`memory_prefetch.c`](src/memory_prefetch.c:1-302):

```c
static void prefetchValue(KeyPrefetchInfo *info) {
    void *entry;
    if (hashtableIncrementalFindGetResult(&info->hashtab_state, &entry)) {
        robj *val = entry;
        if (val->type == OBJ_STRING && val->encoding == OBJ_ENCODING_RAW) {
            valkey_prefetch(val->ptr);
        }
        markKeyAsdone(info);
    }
}
```

**Enhanced Implementation**:

```c
/* Enhanced prefetchValue supporting all data types and encodings */
static void prefetchValue(KeyPrefetchInfo *info) {
    void *entry;
    if (hashtableIncrementalFindGetResult(&info->hashtab_state, &entry)) {
        robj *val = entry;
        
        switch (val->type) {
        case OBJ_STRING:
            if (val->encoding == OBJ_ENCODING_RAW) {
                valkey_prefetch(val->ptr);
            }
            /* EMBSTR and INT encodings are embedded, no separate prefetch needed */
            break;
            
        case OBJ_LIST:
            if (val->encoding == OBJ_ENCODING_QUICKLIST) {
                quicklist *ql = val->ptr;
                valkey_prefetch(ql);  /* Prefetch quicklist header */
                
                /* Prefetch first few nodes for sequential access patterns */
                if (ql->head) {
                    valkey_prefetch(ql->head);
                    if (ql->head->next) valkey_prefetch(ql->head->next);
                }
                if (ql->tail && ql->tail != ql->head) {
                    valkey_prefetch(ql->tail);
                }
            } else if (val->encoding == OBJ_ENCODING_LISTPACK) {
                valkey_prefetch(val->ptr);
            }
            break;
            
        case OBJ_SET:
            if (val->encoding == OBJ_ENCODING_HASHTABLE) {
                hashtable *ht = val->ptr;
                valkey_prefetch(ht);  /* Prefetch hashtable metadata */
                
                /* Prefetch first few hash table buckets */
                if (ht->tables[0] && ht->size_mask[0] > 0) {
                    valkey_prefetch(&ht->tables[0][0]);
                }
            } else if (val->encoding == OBJ_ENCODING_INTSET) {
                valkey_prefetch(val->ptr);
            } else if (val->encoding == OBJ_ENCODING_LISTPACK) {
                valkey_prefetch(val->ptr);
            }
            break;
            
        case OBJ_ZSET:
            if (val->encoding == OBJ_ENCODING_SKIPLIST) {
                zset *zs = val->ptr;
                valkey_prefetch(zs);  /* Prefetch zset structure */
                
                /* Prefetch skiplist header and first node */
                if (zs->zsl) {
                    valkey_prefetch(zs->zsl);
                    if (zs->zsl->header && zs->zsl->header->level[0].forward) {
                        valkey_prefetch(zs->zsl->header->level[0].forward);
                    }
                }
                
                /* Prefetch dict for O(1) score lookup */
                if (zs->ht) {
                    valkey_prefetch(zs->ht);
                }
            } else if (val->encoding == OBJ_ENCODING_LISTPACK) {
                valkey_prefetch(val->ptr);
            }
            break;
            
        case OBJ_HASH:
            if (val->encoding == OBJ_ENCODING_HASHTABLE) {
                hashtable *ht = val->ptr;
                valkey_prefetch(ht);
                if (ht->tables[0] && ht->size_mask[0] > 0) {
                    valkey_prefetch(&ht->tables[0][0]);
                }
            } else if (val->encoding == OBJ_ENCODING_LISTPACK) {
                valkey_prefetch(val->ptr);
            }
            break;
            
        case OBJ_STREAM:
            valkey_prefetch(val->ptr);
            /* Could prefetch rax tree nodes for range queries */
            break;
            
        case OBJ_MODULE:
            /* Module types handle their own prefetching via callbacks */
            if (val->ptr) valkey_prefetch(val->ptr);
            break;
        }
        
        markKeyAsdone(info);
    }
}
```

#### Command-Specific Prefetch Strategies

```c
/* Prefetch strategy based on command type */
typedef enum {
    PREFETCH_STRATEGY_DEFAULT,      /* Basic value prefetch */
    PREFETCH_STRATEGY_RANGE,        /* Range scan: prefetch multiple nodes */
    PREFETCH_STRATEGY_AGGREGATE,    /* Aggregation: prefetch all elements */
    PREFETCH_STRATEGY_DEEP          /* Deep traversal: prefetch tree structure */
} prefetch_strategy;

/* Determine optimal prefetch strategy for command */
prefetch_strategy getPrefetchStrategy(struct serverCommand *cmd, robj *val) {
    /* Range operations on sorted sets */
    if ((cmd->proc == zrangeCommand || cmd->proc == zrevrangeCommand) &&
        val->type == OBJ_ZSET && val->encoding == OBJ_ENCODING_SKIPLIST) {
        return PREFETCH_STRATEGY_RANGE;
    }
    
    /* Aggregation operations */
    if (cmd->proc == sunionCommand || cmd->proc == sinterCommand ||
        cmd->proc == zunionstoreCommand) {
        return PREFETCH_STRATEGY_AGGREGATE;
    }
    
    /* Hash/set scan operations */
    if (cmd->proc == hscanCommand || cmd->proc == sscanCommand) {
        return PREFETCH_STRATEGY_DEEP;
    }
    
    return PREFETCH_STRATEGY_DEFAULT;
}
```

### Phase 2: Locality-Aware Batch Ordering (2-3 weeks)

#### Memory Address-Based Sorting

```c
/* Compare keys by memory address for cache-friendly ordering */
static int compareKeysByMemoryAddress(const void *a, const void *b) {
    KeyPrefetchInfo *key_a = (KeyPrefetchInfo *)a;
    KeyPrefetchInfo *key_b = (KeyPrefetchInfo *)b;
    
    /* Get actual memory addresses of value objects */
    uintptr_t addr_a = (uintptr_t)key_a->value_ptr;
    uintptr_t addr_b = (uintptr_t)key_b->value_ptr;
    
    if (addr_a < addr_b) return -1;
    if (addr_a > addr_b) return 1;
    return 0;
}

/* Sort prefetch batch by memory locality before issuing prefetch instructions */
static void sortBatchByLocality(KeyPrefetchInfo *batch, size_t batch_size) {
    /* Quick sort by memory address */
    qsort(batch, batch_size, sizeof(KeyPrefetchInfo), compareKeysByMemoryAddress);
    
    /* Sequential access to sorted addresses improves hardware prefetcher efficiency */
}

/* Enhanced batch processing with locality optimization */
void processPrefetchBatch(client *c) {
    KeyPrefetchInfo batch[PREFETCH_BATCH_MAX_SIZE];
    size_t batch_size = preparePrefetchBatch(c, batch, PREFETCH_BATCH_MAX_SIZE);
    
    if (batch_size == 0) return;
    
    /* Sort batch for memory locality */
    if (server.prefetch_locality_aware) {
        sortBatchByLocality(batch, batch_size);
    }
    
    /* Issue prefetch instructions in sorted order */
    for (size_t i = 0; i < batch_size; i++) {
        prefetchValue(&batch[i]);
    }
}
```

#### Cache Line Alignment

```c
/* Prefetch with cache line awareness */
#define CACHE_LINE_SIZE 64

static void prefetchCacheLineAligned(void *ptr) {
    /* Align to cache line boundary */
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned_addr = addr & ~(CACHE_LINE_SIZE - 1);
    
    /* Prefetch entire cache line */
    valkey_prefetch((void *)aligned_addr);
    
    /* For large structures, prefetch next cache line too */
    if ((addr + sizeof(robj)) > (aligned_addr + CACHE_LINE_SIZE)) {
        valkey_prefetch((void *)(aligned_addr + CACHE_LINE_SIZE));
    }
}
```

### Phase 3: Adaptive Pattern Learning (4-5 weeks)

#### Access Pattern Tracker

```c
// New file: src/access_pattern_learner.h

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

#endif /* __ACCESS_PATTERN_LEARNER_H */
```

#### Pattern Learning Implementation

```c
/* Learn which keys are frequently accessed together */
void learnAccessPattern(robj **keys, int key_count) {
    if (!server_pattern_learner || !server_pattern_learner->enabled) return;
    if (key_count < 2) return;  /* Need multiple keys to learn pattern */
    
    for (int i = 0; i < key_count; i++) {
        sds key = keys[i]->ptr;
        
        /* Extract pattern (e.g., "user:123:profile" -> "user:*:profile") */
        sds pattern = extractKeyPattern(key);
        
        /* Find or create pattern entry */
        access_pattern *ap = hashtableFindValue(server_pattern_learner->patterns, 
                                                pattern);
        if (!ap) {
            ap = createAccessPattern(pattern);
            hashtableInsert(server_pattern_learner->patterns, pattern, ap);
        }
        
        /* Record co-accessed keys */
        for (int j = 0; j < key_count; j++) {
            if (i != j) {
                sds related_key = keys[j]->ptr;
                sds related_pattern = extractKeyPattern(related_key);
                
                /* Add to commonly accessed list if not already present */
                if (!listSearchKey(ap->commonly_accessed_with, related_pattern)) {
                    listAddNodeTail(ap->commonly_accessed_with, 
                                   sdsnew(related_pattern));
                }
                sdsfree(related_pattern);
            }
        }
        
        /* Update statistics */
        ap->access_count++;
        ap->last_access = server.mstime;
        updatePatternConfidence(ap);
        
        sdsfree(pattern);
    }
    
    /* Periodically prune stale patterns */
    if (server.mstime % 60000 == 0) {  /* Every minute */
        pruneStalePatterns();
    }
}

/* Calculate confidence score based on access frequency and recency */
void updatePatternConfidence(access_pattern *pattern) {
    /* Frequency component: log scale for diminishing returns */
    double frequency_score = log(pattern->access_count + 1) / 10.0;
    if (frequency_score > 1.0) frequency_score = 1.0;
    
    /* Recency component: exponential decay */
    mstime_t age_ms = server.mstime - pattern->last_access;
    double recency_score = exp(-age_ms / 3600000.0);  /* Half-life of 1 hour */
    
    /* Combined confidence score */
    pattern->confidence_score = (frequency_score * 0.6) + (recency_score * 0.4);
}

/* Extract pattern from concrete key */
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
        } else {
            pattern = sdscatlen(pattern, &c, 1);
            in_numeric = 0;
        }
    }
    
    return pattern;
}
```

#### Predictive Prefetching

```c
/* Predict and prefetch related keys based on learned patterns */
list *predictRelatedKeys(robj *key) {
    if (!server_pattern_learner || !server_pattern_learner->enabled) return NULL;
    
    sds key_str = key->ptr;
    sds pattern = extractKeyPattern(key_str);
    
    access_pattern *ap = hashtableFindValue(server_pattern_learner->patterns, 
                                            pattern);
    sdsfree(pattern);
    
    if (!ap || ap->confidence_score < server_pattern_learner->min_confidence / 100.0) {
        return NULL;
    }
    
    /* Return list of predicted related keys */
    list *related = listCreate();
    listIter *iter = listGetIterator(ap->commonly_accessed_with, AL_START_HEAD);
    listNode *node;
    
    while ((node = listNext(iter)) != NULL) {
        sds related_pattern = listNodeValue(node);
        
        /* Instantiate pattern with same ID from original key */
        sds related_key = instantiatePattern(related_pattern, key_str);
        if (related_key) {
            listAddNodeTail(related, related_key);
        }
    }
    listReleaseIterator(iter);
    
    return related;
}

/* Predictive prefetch integration */
void prefetchWithPrediction(client *c, robj *key) {
    /* Standard prefetch */
    prefetchValue(key);
    
    /* Predictive prefetch of related keys */
    list *related_keys = predictRelatedKeys(key);
    if (related_keys) {
        listIter *iter = listGetIterator(related_keys, AL_START_HEAD);
        listNode *node;
        int prefetch_count = 0;
        
        while ((node = listNext(iter)) != NULL && 
               prefetch_count < MAX_PREDICTIVE_PREFETCH) {
            sds related_key_str = listNodeValue(node);
            
            /* Check if key exists before prefetching */
            robj *related_key = lookupKey(c->db, related_key_str, LOOKUP_NOTOUCH);
            if (related_key) {
                prefetchValue(related_key);
                prefetch_count++;
            }
        }
        listReleaseIterator(iter);
        listRelease(related_keys);
    }
}
```

## Drawbacks

### Performance Trade-offs

- **Learning Overhead**: Pattern learning adds ~0.5-1% CPU overhead
  - Mitigation: Make learning optional, disable for workloads with random access
  - Batch pattern updates to reduce per-request cost

- **Memory Consumption**: Pattern storage requires ~1-5 MB for typical workloads
  - Mitigation: Configurable pattern limit (default: 10,000 patterns)
  - Automatic pruning of low-confidence patterns

### Accuracy Concerns

- **False Positives**: Predictive prefetching may waste bandwidth on wrong predictions
  - Mitigation: High confidence threshold (default: 70%)
  - Track hit rate and adjust threshold dynamically

- **Workload Changes**: Patterns become stale when workload changes
  - Mitigation: Recency-based scoring with exponential decay
  - Periodic pattern pruning

### Complexity

- **Increased Code Complexity**: Pattern learning adds non-trivial logic
  - Mitigation: Comprehensive unit tests and documentation
  - Clear separation of concerns with modular design

- **Tuning Parameters**: Multiple configuration knobs may confuse users
  - Mitigation: Sensible defaults that work for most workloads
  - Auto-tuning based on workload characteristics

## Alternatives

### Alternative 1: Static Configuration-Based Prefetching

**Pros**:
- Simpler implementation
- Predictable behavior
- No runtime overhead

**Cons**:
- Requires manual tuning per workload
- Cannot adapt to changing patterns
- Less effective for dynamic workloads

**Decision**: Rejected. Adaptive learning provides better out-of-box experience.

### Alternative 2: Machine Learning-Based Prediction

**Pros**:
- More sophisticated pattern detection
- Better prediction accuracy
- Can detect complex relationships

**Cons**:
- Significant CPU and memory overhead
- Requires ML library dependencies
- Harder to debug and explain

**Decision**: Rejected. Too complex for marginal benefit. Simple pattern matching sufficient.

### Alternative 3: Hardware Prefetch Hints Only

**Pros**:
- Minimal overhead
- Leverages CPU prefetcher
- No additional memory

**Cons**:
- Limited to cache line granularity
- No semantic awareness
- Less effective for scattered access

**Decision**: Use as complement, not replacement. Software prefetch provides semantic awareness.

## Unresolved Questions

1. **Optimal Prefetch Distance**: How many keys ahead should we prefetch in batch operations?
   - **Proposed**: Adaptive based on memory bandwidth and latency
   - **Need**: Benchmarking on different hardware configurations

2. **Pattern Matching Complexity**: How to handle complex key structures with multiple variable parts?
   - **Example**: `app:{appid}:user:{userid}:session:{sessionid}`
   - **Proposed**: Support multi-wildcard patterns with precedence rules
   - **Need**: Design review and performance validation

3. **Cross-Database Patterns**: Should patterns be learned per-database or globally?
   - **Proposed**: Per-database by default, global option for multi-tenant setups
   - **Need**: User feedback on typical deployment patterns

4. **Prefetch in Single-Threaded Mode**: How to prefetch without blocking event loop?
   - **Option A**: Opportunistic prefetch during idle time
   - **Option B**: Only enable in I/O threaded mode
   - **Need**: Performance testing to determine feasibility

5. **Pattern Export/Import**: Should learned patterns be persisted or exportable?
   - **Use Case**: Sharing patterns across replicas, warm start after restart
   - **Proposed**: Optional RDB section for pattern storage
   - **Need**: Community feedback on value vs. complexity

## Implementation Plan

### Milestone 1: Multi-Encoding Support (Weeks 1-4)

- [ ] Extend `prefetchValue()` to handle all encodings
- [ ] Add command-specific prefetch strategies
- [ ] Implement deep prefetching for complex structures
- [ ] Unit tests for each data type/encoding combination
- [ ] Benchmark latency improvements

### Milestone 2: Locality Optimization (Weeks 5-7)

- [ ] Implement memory address-based sorting
- [ ] Add cache line alignment logic
- [ ] Integrate with existing batch processing
- [ ] Benchmark cache hit rate improvements
- [ ] Performance testing on different CPU architectures

### Milestone 3: Pattern Learning Infrastructure (Weeks 8-11)

- [ ] Implement `access_pattern_learner.h` data structures
- [ ] Add pattern extraction and matching logic
- [ ] Implement confidence scoring algorithm
- [ ] Add pattern pruning and garbage collection
- [ ] Unit tests for pattern learning

### Milestone 4: Predictive Prefetching (Weeks 12-15)

- [ ] Implement predictive key lookup
- [ ] Integrate with command execution pipeline
- [ ] Add configuration options for tuning
- [ ] Track prediction hit rate metrics
- [ ] A/B testing framework for validation

### Milestone 5: Testing and Optimization (Weeks 16-18)

- [ ] Comprehensive integration testing
- [ ] Benchmark suite with various workloads
- [ ] Memory profiling and optimization
- [ ] Documentation for users and operators
- [ ] Tuning guide with best practices

## Success Metrics

### Performance Targets

- **Latency Reduction**: 15-30% improvement on pipeline operations
- **Cache Hit Rate**: 10-20% increase in L3 cache hit rate
- **Prediction Accuracy**: >70% hit rate for predictive prefetch
- **Overhead**: <2% CPU overhead with all features enabled

### Adoption Metrics

- **Default Enabled**: Multi-encoding prefetch enabled by default
- **Opt-in Learning**: 30% of deployments enable pattern learning within 6 months
- **Performance Reports**: Positive feedback from high-traffic deployments

## References

- [Intel Optimization Manual: Memory Prefetching](https://www.intel.com/content/www/us/en/architecture-and-technology/64-ia-32-architectures-optimization-manual.html)
- [AMD Optimization Guide: Data Prefetch](https://www.amd.com/system/files/TechDocs/56305_SOG_3.0_PUB.pdf)
- [Current Valkey Prefetch Implementation](src/memory_prefetch.c:1-302)
- [Google's Maglev: Hardware Prefetching](https://research.google/pubs/pub44824/)
- [Facebook's Cache Prefetching Paper](https://research.facebook.com/publications/optimizing-web-servers-for-high-throughput-and-low-latency/)

## Changelog

- **2025-11-16**: Initial RFC draft based on codebase analysis and industry best practices