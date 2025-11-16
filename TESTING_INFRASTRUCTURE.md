# Advanced Testing Infrastructure

This document describes Valkey's advanced testing infrastructure as defined in [RFC 0004](rfcs/0004-advanced-testing-infrastructure.md).

## Overview

The advanced testing infrastructure adds three major components to Valkey's existing test suite:

1. **Property-Based Testing** - Verify invariants across randomly-generated scenarios
2. **Performance Regression Detection** - Automated benchmarking in CI/CD
3. **Fuzz Testing** - Find crashes and memory corruption bugs

## Components

### 1. Property-Based Testing Framework

**Location:** `tests/property/`

Property-based testing verifies that invariants hold true across many random test cases, catching edge cases that traditional unit tests might miss.

**Key Features:**
- Test framework for defining properties and test suites
- Random data generation with reproducible seeds
- Example tests for replication and cluster operations

**Quick Start:**
```bash
cd tests/property
gcc -I../../src -o test_replication test_replication.c property_test.c -DCLUSTER_SLOTS=16384
./test_replication
```

**Documentation:** [tests/property/README.md](tests/property/README.md)

### 2. Performance Regression Detection

**Location:** `utils/performance-benchmark.sh`, `.github/workflows/performance-regression.yml`

Automated performance benchmarking that runs on every pull request to detect performance regressions before they reach production.

**Key Features:**
- Comprehensive benchmark suite covering all major operations
- Automated comparison between PR and base branch
- GitHub Actions integration with PR comments
- Configurable regression thresholds

**Quick Start:**
```bash
# Start Valkey server
valkey-server --daemonize yes --port 6379

# Run benchmarks
./utils/performance-benchmark.sh > results.json

# Compare results
./utils/compare-benchmarks.py baseline.json pr.json --threshold 5.0
```

**Workflow:** `.github/workflows/performance-regression.yml`

### 3. Fuzz Testing Targets

**Location:** `tests/fuzz/`

Fuzz testing finds crashes, hangs, and memory corruption by feeding random/malformed inputs to parsers and protocols.

**Available Targets:**
- `fuzz_resp_parser.c` - RESP protocol parser
- `fuzz_cluster_message.c` - Cluster bus messages
- `fuzz_rdb_loader.c` - RDB file loading
- `fuzz_aof_parser.c` - AOF file parsing

**Quick Start:**
```bash
cd tests/fuzz

# Build standalone executables
./build_fuzz.sh standalone

# Test with a file
echo '*1\r\n$4\r\nPING\r\n' > test.resp
./bin/fuzz_resp_parser test.resp

# Or build for continuous fuzzing
./build_fuzz.sh ossfuzz
./out/fuzz_resp_parser -max_total_time=60
```

**Documentation:** [tests/fuzz/README.md](tests/fuzz/README.md)

## CI/CD Integration

### Performance Regression Detection

The performance regression workflow runs automatically on pull requests:

```yaml
# .github/workflows/performance-regression.yml
- Builds both PR and base versions
- Runs comprehensive benchmarks
- Compares results with configurable threshold
- Comments results on PR
- Fails if regressions detected
```

**Threshold:** 5% by default (configurable)

### Future CI Integration

Property tests and fuzz tests can be integrated:

```yaml
# Example integration
- name: Run property tests
  run: make property-tests

- name: Run fuzz tests (short)
  run: |
    cd tests/fuzz
    ./build_fuzz.sh ossfuzz
    for fuzzer in out/fuzz_*; do
      $fuzzer -max_total_time=60
    done
```

## Testing Best Practices

### Property-Based Testing

1. **Focus on invariants** - What should always be true?
2. **Use sufficient iterations** - 100-1000 iterations for good coverage
3. **Make tests reproducible** - Use fixed seeds for debugging
4. **Test edge cases** - Empty data, maximum sizes, boundary conditions

Example:
```c
// Property: After sync, replica has same keys as primary
property_test_result test_sync_completeness(void *data) {
    // Generate random data
    int key_count = 1000 + (rand() % 9000);

    // Perform sync
    trigger_full_sync();

    // Verify invariant
    PROPERTY_ASSERT_EQ(
        primary_key_count(),
        replica_key_count(),
        "Replica should have all keys"
    );

    return PROPERTY_TEST_PASS;
}
```

### Performance Testing

1. **Baseline early** - Establish performance baselines for main branch
2. **Test realistic workloads** - Use production-like data and patterns
3. **Run multiple times** - Account for variance with multiple runs
4. **Monitor trends** - Track performance over time, not just regressions

### Fuzz Testing

1. **Use sanitizers** - Enable AddressSanitizer and UndefinedBehaviorSanitizer
2. **Validate inputs early** - Skip invalid inputs to focus fuzzing effort
3. **Limit resources** - Prevent timeouts and OOM with size limits
4. **Continuous fuzzing** - Run fuzzers continuously, not just in CI

Example:
```c
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Validate input early
    if (size == 0 || size > MAX_SIZE) return 0;

    // Parse input (should not crash)
    parse_protocol(data, size);

    return 0;
}
```

## Metrics and Goals

As defined in RFC 0004, the success metrics are:

| Metric | Goal | Status |
|--------|------|--------|
| Code Coverage | >85% | 🔄 In Progress |
| Performance Regressions | 0 per release | ✅ Workflow Active |
| Bugs Found via Fuzzing | 10+ before release | 🔄 Framework Ready |
| Flaky Test Rate | <1% | 🔄 To Be Measured |

## Implementation Status

### ✅ Completed

- [x] Property-based testing framework
- [x] Example property tests for replication/cluster
- [x] Performance benchmark script
- [x] Benchmark comparison tool
- [x] Performance regression GitHub workflow
- [x] RESP parser fuzz target
- [x] Cluster message fuzz target
- [x] RDB loader fuzz target
- [x] AOF parser fuzz target
- [x] Fuzz build scripts
- [x] Documentation

### 🔄 In Progress

- [ ] Integration with Valkey build system
- [ ] Full property test implementations (currently mocks)
- [ ] OSS-Fuzz integration
- [ ] Continuous fuzzing infrastructure

### 📋 Planned

- [ ] Additional property tests for data structures
- [ ] Distributed system property tests
- [ ] Performance dashboard
- [ ] Coverage reporting integration
- [ ] Additional fuzz targets (module API, Lua scripts)

## Directory Structure

```
valkey/
├── tests/
│   ├── property/              # Property-based testing
│   │   ├── property_test.h    # Framework header
│   │   ├── property_test.c    # Framework implementation
│   │   ├── test_replication.c # Example tests
│   │   └── README.md          # Documentation
│   └── fuzz/                  # Fuzz testing
│       ├── fuzz_resp_parser.c
│       ├── fuzz_cluster_message.c
│       ├── fuzz_rdb_loader.c
│       ├── fuzz_aof_parser.c
│       ├── build_fuzz.sh      # Build script
│       └── README.md          # Documentation
├── utils/
│   ├── performance-benchmark.sh    # Benchmark runner
│   └── compare-benchmarks.py       # Result comparison
├── .github/
│   └── workflows/
│       └── performance-regression.yml  # CI workflow
└── rfcs/
    └── 0004-advanced-testing-infrastructure.md
```

## Contributing

### Adding Property Tests

1. Create test functions that verify invariants
2. Register tests in a test suite
3. Add to build system
4. Document properties being tested

### Adding Fuzz Targets

1. Create fuzz target in `tests/fuzz/fuzz_*.c`
2. Implement `LLVMFuzzerTestOneInput()`
3. Add to `build_fuzz.sh`
4. Create seed corpus files
5. Document target purpose

### Improving Benchmarks

1. Add new benchmarks to `performance-benchmark.sh`
2. Update JSON output format
3. Adjust comparison thresholds if needed
4. Test with real workloads

## Troubleshooting

### Property Tests Failing

- Check random seed for reproducibility
- Verify test data generation is correct
- Ensure cleanup between iterations
- Check for race conditions

### Benchmarks Showing False Regressions

- Run multiple times to account for variance
- Check for system load during tests
- Verify fair comparison (same hardware, settings)
- Adjust threshold if needed

### Fuzz Tests Timing Out

- Add input size limits
- Optimize parsing code paths
- Increase timeout threshold
- Use faster sanitizers (ASan instead of MSan)

## Resources

- **RFC 0004:** [rfcs/0004-advanced-testing-infrastructure.md](rfcs/0004-advanced-testing-infrastructure.md)
- **Property Testing:** [tests/property/README.md](tests/property/README.md)
- **Fuzz Testing:** [tests/fuzz/README.md](tests/fuzz/README.md)
- **LibFuzzer:** https://llvm.org/docs/LibFuzzer.html
- **OSS-Fuzz:** https://google.github.io/oss-fuzz/
- **QuickCheck:** https://en.wikipedia.org/wiki/QuickCheck

## Questions?

For questions or issues related to the testing infrastructure:

1. Check the documentation in `tests/property/README.md` and `tests/fuzz/README.md`
2. Review RFC 0004 for design rationale
3. Open an issue with the `testing` label
4. Contact the testing infrastructure team

---

**Last Updated:** 2025-11-16
**RFC:** 0004
**Status:** Implementation Complete
