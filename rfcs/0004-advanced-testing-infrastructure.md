# RFC 0004: Advanced Testing Infrastructure and Coverage

## Summary

This RFC proposes comprehensive enhancements to Valkey's testing infrastructure, including property-based testing, continuous performance benchmarking, systematic fuzz testing, and improved integration test coverage. These improvements will significantly increase code quality, catch bugs before production, and prevent performance regressions.

## Motivation

### Current State

Valkey has solid testing infrastructure:
- Comprehensive Tcl integration tests
- Unit tests in [`src/unit/`](src/unit/)
- Code coverage tracking via codecov
- Module API tests
- Cluster and sentinel tests

### Problems Identified

1. **Test Coverage Gaps**: Critical paths lack coverage (identified via codecov analysis)
2. **Flaky Tests**: Race conditions in cluster tests cause intermittent failures
3. **No Performance Regression Detection**: Manual benchmarking doesn't catch gradual slowdowns
4. **Limited Fuzzing**: No systematic fuzz testing for protocol parsing and edge cases
5. **Missing Property Tests**: Complex invariants not verified systematically

### Use Cases

- **Pre-merge Validation**: Catch regressions before code reaches main branch
- **Performance Monitoring**: Detect gradual performance degradation
- **Security Hardening**: Find crash and memory corruption bugs through fuzzing
- **Cluster Testing**: Verify distributed system invariants hold under all conditions
- **Replication Testing**: Ensure data consistency across primary-replica topologies

## Detailed Design

### 1. Property-Based Testing Framework

```c
// New file: tests/property/property_test.h

#ifndef __PROPERTY_TEST_H
#define __PROPERTY_TEST_H

#include "server.h"

/* Property test result */
typedef enum {
    PROPERTY_TEST_PASS,
    PROPERTY_TEST_FAIL,
    PROPERTY_TEST_ERROR
} property_test_result;

/* Property test function signature */
typedef property_test_result (*property_test_fn)(void *test_data);

/* Test case definition */
typedef struct property_test {
    const char *name;
    const char *description;
    property_test_fn test_fn;
    int iterations;           /* Number of random iterations */
    unsigned int seed;        /* Random seed for reproducibility */
} property_test;

/* Test suite */
typedef struct property_test_suite {
    const char *suite_name;
    property_test *tests;
    size_t test_count;
} property_test_suite;

/* Registration and execution */
void registerPropertyTestSuite(property_test_suite *suite);
int runPropertyTests(const char *suite_name);
int runAllPropertyTests(void);

/* Assertion helpers */
#define PROPERTY_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "Property assertion failed: %s\n", message); \
            return PROPERTY_TEST_FAIL; \
        } \
    } while(0)

#define PROPERTY_ASSERT_EQ(actual, expected, message) \
    PROPERTY_ASSERT((actual) == (expected), message)

#endif /* __PROPERTY_TEST_H */
```

#### Example: Replication Consistency Property

```c
// tests/property/test_replication.c

/* Property: After full sync, replica has exact same keys as primary */
property_test_result property_full_sync_completeness(void *test_data) {
    serverDb *primary_db = test_data;
    
    /* Create replica connection */
    client *replica = createFakeReplicaClient();
    
    /* Generate random operations on primary */
    int key_count = 1000 + (rand() % 9000);  /* 1K-10K keys */
    for (int i = 0; i < key_count; i++) {
        char key[64], value[256];
        snprintf(key, sizeof(key), "test_key_%d", i);
        snprintf(value, sizeof(value), "value_%d_%lu", i, (unsigned long)rand());
        
        robj *key_obj = createStringObject(key, strlen(key));
        robj *val_obj = createStringObject(value, strlen(value));
        dbAdd(primary_db, key_obj, val_obj);
    }
    
    /* Trigger full sync */
    replicationSetupReplicaForFullResync(replica, 0);
    
    /* Wait for sync completion */
    waitForReplicationSyncComplete(replica, 60000);  /* 60 second timeout */
    
    /* Verify replica has identical key set */
    serverDb *replica_db = replica->db;
    
    PROPERTY_ASSERT_EQ(
        kvstoreSize(primary_db->keys, -1),
        kvstoreSize(replica_db->keys, -1),
        "Replica should have same number of keys as primary"
    );
    
    /* Verify each key exists and has same value */
    kvstoreIterator *iter = kvstoreIteratorInit(primary_db->keys);
    kvstoreEntry *entry;
    while ((entry = kvstoreIteratorNext(iter)) != NULL) {
        robj *key = kvstoreEntryGetKey(entry);
        robj *primary_val = kvstoreEntryGetValue(entry);
        
        robj *replica_val = lookupKey(replica_db, key, LOOKUP_NOTOUCH);
        PROPERTY_ASSERT(replica_val != NULL, "Key should exist on replica");
        
        /* Compare values */
        if (compareStringObjects(primary_val, replica_val) != 0) {
            fprintf(stderr, "Value mismatch for key: %s\n", (char*)key->ptr);
            return PROPERTY_TEST_FAIL;
        }
    }
    kvstoreIteratorRelease(iter);
    
    /* Cleanup */
    freeClient(replica);
    
    return PROPERTY_TEST_PASS;
}

/* Property: Slot migration preserves all keys */
property_test_result property_slot_migration_completeness(void *test_data) {
    int slot = rand() % CLUSTER_SLOTS;
    
    /* Insert random keys in slot */
    int key_count = 500 + (rand() % 1500);  /* 500-2000 keys */
    list *inserted_keys = listCreate();
    
    for (int i = 0; i < key_count; i++) {
        char key[64];
        
        /* Generate key that hashes to target slot */
        do {
            snprintf(key, sizeof(key), "migrate_key_%d_%lu", i, (unsigned long)rand());
        } while (keyHashSlot(key, strlen(key)) != slot);
        
        robj *key_obj = createStringObject(key, strlen(key));
        robj *val_obj = createStringObject("test_value", 10);
        dbAdd(server.db, key_obj, val_obj);
        listAddNodeTail(inserted_keys, sdsnew(key));
    }
    
    size_t keys_before = countKeysInSlot(slot);
    
    /* Trigger migration to target node */
    clusterNode *target = getRandomClusterNode();
    slotMigrationJob *job = createSlotMigrationJob(slot, target);
    executeSlotMigration(job);
    
    /* Wait for migration completion */
    waitForSlotMigrationComplete(job, 120000);  /* 2 minute timeout */
    
    /* Verify all keys migrated */
    size_t keys_after_source = countKeysInSlot(slot);
    PROPERTY_ASSERT_EQ(keys_after_source, 0, 
                      "Source should have no keys in migrated slot");
    
    /* Verify all keys present on target */
    size_t keys_on_target = countKeysOnNode(target, slot);
    PROPERTY_ASSERT_EQ(keys_on_target, keys_before,
                      "Target should have all migrated keys");
    
    /* Verify data integrity - check sample of keys */
    listIter *iter = listGetIterator(inserted_keys, AL_START_HEAD);
    listNode *node;
    int samples_checked = 0;
    while ((node = listNext(iter)) != NULL && samples_checked < 50) {
        sds key = listNodeValue(node);
        robj *val = lookupKeyOnNode(target, key);
        PROPERTY_ASSERT(val != NULL, "Migrated key should exist on target");
        samples_checked++;
    }
    listReleaseIterator(iter);
    
    /* Cleanup */
    listRelease(inserted_keys);
    freeSlotMigrationJob(job);
    
    return PROPERTY_TEST_PASS;
}
```

### 2. Continuous Performance Benchmarking

```yaml
# .github/workflows/performance-regression.yml
name: Performance Regression Detection

on:
  pull_request:
    paths:
      - 'src/**'
      - 'tests/**'

jobs:
  benchmark:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout PR code
        uses: actions/checkout@v4
        with:
          ref: ${{ github.event.pull_request.head.sha }}
      
      - name: Build PR version
        run: |
          make clean && make -j$(nproc)
          cp src/valkey-server /tmp/valkey-server-pr
          cp src/valkey-benchmark /tmp/valkey-benchmark-pr
      
      - name: Checkout base branch
        uses: actions/checkout@v4
        with:
          ref: ${{ github.event.pull_request.base.sha }}
      
      - name: Build base version
        run: |
          make clean && make -j$(nproc)
          cp src/valkey-server /tmp/valkey-server-base
          cp src/valkey-benchmark /tmp/valkey-benchmark-base
      
      - name: Run PR benchmarks
        run: |
          /tmp/valkey-server-pr --daemonize yes --port 6379
          sleep 2
          ./utils/performance-benchmark.sh > /tmp/pr_results.json
          pkill valkey-server
      
      - name: Run base benchmarks
        run: |
          /tmp/valkey-server-base --daemonize yes --port 6379
          sleep 2
          ./utils/performance-benchmark.sh > /tmp/base_results.json
          pkill valkey-server
      
      - name: Compare results
        run: |
          python utils/compare-benchmarks.py \
            /tmp/base_results.json \
            /tmp/pr_results.json \
            --threshold 5.0
      
      - name: Upload benchmark results
        uses: actions/upload-artifact@v3
        with:
          name: benchmark-results
          path: /tmp/*.json
```

#### Benchmark Script

```bash
#!/bin/bash
# utils/performance-benchmark.sh

set -e

RESULTS_FILE="${1:-benchmark_results.json}"

echo "{"
echo "  \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
echo "  \"benchmarks\": ["

# Basic operations benchmark
echo "    {"
echo "      \"name\": \"basic_ops\","
valkey-benchmark -q -n 100000 -c 50 -P 16 \
  -t GET,SET,INCR,LPUSH,RPUSH,LPOP,RPOP,SADD,HSET,ZADD | \
  python -c "import sys, json; print(json.dumps({'output': sys.stdin.read()}))"
echo "    },"

# Latency percentiles
echo "    {"
echo "      \"name\": \"latency_percentiles\","
valkey-benchmark -q -n 100000 -c 50 --latency-dist | \
  python -c "import sys, json; print(json.dumps({'output': sys.stdin.read()}))"
echo "    },"

# Pipeline efficiency
echo "    {"
echo "      \"name\": \"pipeline_efficiency\","
valkey-benchmark -q -n 100000 -c 50 -P 64 -t GET,SET | \
  python -c "import sys, json; print(json.dumps({'output': sys.stdin.read()}))"
echo "    }"

echo "  ]"
echo "}"
```

### 3. Systematic Fuzz Testing

```c
// tests/fuzz/fuzz_resp_parser.c
// Build with: clang -fsanitize=fuzzer,address -o fuzz_resp_parser fuzz_resp_parser.c

#include "server.h"
#include <stdint.h>
#include <stddef.h>

/* Fuzz target for RESP protocol parser */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 1024 * 1024) return 0;  /* Skip empty or huge inputs */
    
    /* Create fake client */
    client *c = createClient(NULL);
    c->querybuf = sdsnewlen(data, size);
    c->qb_pos = 0;
    
    /* Try to parse input */
    processInputBuffer(c);
    
    /* Cleanup - should not crash, leak, or corrupt memory */
    freeClient(c);
    
    return 0;
}
```

Additional fuzz targets:
- `fuzz_cluster_message.c`: Cluster message parsing
- `fuzz_rdb_loader.c`: RDB file loading
- `fuzz_aof_parser.c`: AOF command parsing
- `fuzz_slot_migration.c`: Slot migration protocol

### 4. OSS-Fuzz Integration

```bash
# tests/fuzz/build_fuzz.sh
#!/bin/bash

# Build all fuzz targets for OSS-Fuzz

set -e

FUZZ_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$FUZZ_DIR/../.."

cd "$SRC_DIR"

# Build Valkey with fuzzer instrumentation
export CC=clang
export CFLAGS="-fsanitize=fuzzer-no-link,address -g"
export LIB_FUZZING_ENGINE="-fsanitize=fuzzer"

make clean
make -j$(nproc)

# Build each fuzz target
for fuzz_target in "$FUZZ_DIR"/fuzz_*.c; do
    target_name=$(basename "$fuzz_target" .c)
    $CC $CFLAGS $LIB_FUZZING_ENGINE \
        -I"$SRC_DIR/src" \
        "$fuzz_target" \
        src/*.o deps/*/*.o \
        -o "$OUT/$target_name"
done
```

## Implementation Plan

### Milestone 1: Property-Based Testing (Weeks 1-4)
- [ ] Implement property test framework
- [ ] Add replication consistency properties
- [ ] Add cluster migration properties
- [ ] Add data structure invariant properties
- [ ] Integration with CI

### Milestone 2: Performance Benchmarking (Weeks 5-7)
- [ ] Create benchmark suite script
- [ ] Add CI workflow for regression detection
- [ ] Set up benchmark result storage
- [ ] Create performance dashboard

### Milestone 3: Fuzz Testing (Weeks 8-11)
- [ ] Implement fuzz targets for all parsers
- [ ] Set up OSS-Fuzz integration
- [ ] Configure continuous fuzzing
- [ ] Triage and fix found issues

### Milestone 4: Coverage Improvements (Weeks 12-14)
- [ ] Identify coverage gaps via codecov
- [ ] Add missing unit tests
- [ ] Improve cluster test stability
- [ ] Document testing best practices

## Success Metrics

- **Coverage**: Increase code coverage from current to >85%
- **Regression Prevention**: Zero performance regressions in releases
- **Bug Detection**: Find and fix 10+ bugs through fuzzing before release
- **Test Reliability**: Reduce flaky test rate to <1%

## References

- [QuickCheck: Property-Based Testing](https://en.wikipedia.org/wiki/QuickCheck)
- [OSS-Fuzz](https://github.com/google/oss-fuzz)
- [AFL++ Fuzzer](https://github.com/AFLplusplus/AFLplusplus)
- [Current Valkey Tests](tests/)

## Changelog

- **2025-11-16**: Initial RFC draft
