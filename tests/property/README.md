# Property-Based Testing Framework

This directory contains the property-based testing framework for Valkey, as defined in RFC 0004.

## Overview

Property-based testing verifies that certain invariants (properties) hold true across many randomly-generated test cases. Unlike traditional unit tests that check specific inputs, property tests verify that properties hold for entire classes of inputs.

## Framework Structure

### Core Components

- **`property_test.h`**: Framework header defining types and macros
- **`property_test.c`**: Framework implementation with test runner
- **`test_replication.c`**: Example property tests for replication and clustering

### Key Concepts

**Property Test**: A test function that verifies an invariant across multiple iterations with random data.

**Test Suite**: A collection of related property tests.

**Iterations**: Number of times a property is tested with different random inputs.

**Seed**: Random seed for reproducibility - same seed produces same random data.

## Writing Property Tests

### Basic Structure

```c
#include "property_test.h"

property_test_result my_property_test(void *test_data) {
    // 1. Generate random test data
    int value = rand() % 1000;

    // 2. Perform operations
    int result = my_function(value);

    // 3. Verify properties/invariants
    PROPERTY_ASSERT(result >= 0, "Result should be non-negative");
    PROPERTY_ASSERT_EQ(result % 2, 0, "Result should be even");

    return PROPERTY_TEST_PASS;
}
```

### Creating a Test Suite

```c
// Define your tests
static property_test my_tests[] = {
    {
        .name = "test_name",
        .description = "What this test verifies",
        .test_fn = my_property_test,
        .iterations = 100,
        .seed = 12345
    }
};

// Create suite
static property_test_suite my_suite = {
    .suite_name = "my_suite",
    .tests = my_tests,
    .test_count = sizeof(my_tests) / sizeof(my_tests[0])
};

// Register and run
int main(int argc, char **argv) {
    registerPropertyTestSuite(&my_suite);
    return runAllPropertyTests();
}
```

## Running Tests

### Build

Currently, property tests are demonstration code and require integration with the Valkey build system:

```bash
# Example compilation (requires full Valkey build)
gcc -I../../src -o test_replication test_replication.c property_test.c -DCLUSTER_SLOTS=16384
```

### Execute

```bash
# Run all tests
./test_replication

# Expected output:
# Running property test suite: replication_and_cluster
# Running property test: full_sync_completeness
#   Results: 10 passed, 0 failed, 0 errors
#   [PASS] full_sync_completeness
```

## Example Properties

### Replication Properties

1. **Full Sync Completeness**: After full sync, replica has exact same keys as primary
2. **Slot Migration Completeness**: Slot migration preserves all keys

### Data Structure Properties

1. **Type Consistency**: Operations maintain type invariants
2. **Rehashing Completeness**: Hash table rehashing preserves all keys
3. **Expiration Reliability**: Keys expire as expected

## Best Practices

### Property Selection

- Focus on **invariants** that must always hold
- Test **edge cases** with random data generation
- Verify **consistency** across operations
- Check **idempotence** where applicable

### Random Data Generation

```c
// Generate random keys that hash to a specific slot
do {
    snprintf(key, sizeof(key), "key_%d_%lu", i, (unsigned long)rand());
} while (keyHashSlot(key, strlen(key)) != target_slot);
```

### Assertions

```c
// Simple boolean assertion
PROPERTY_ASSERT(condition, "Descriptive error message");

// Equality assertion
PROPERTY_ASSERT_EQ(actual, expected, "Values should match");

// Custom assertion
if (complex_condition) {
    fprintf(stderr, "Detailed failure info: %d\n", debug_value);
    return PROPERTY_TEST_FAIL;
}
```

### Iteration Counts

- **Quick tests**: 10-50 iterations for expensive operations
- **Standard tests**: 100-500 iterations for normal cases
- **Thorough tests**: 1000+ iterations for critical invariants

## Integration with CI

Property tests can be integrated into CI pipelines:

```yaml
- name: Run property tests
  run: |
    make property-tests
    ./tests/property/test_replication
```

## Future Enhancements

- [ ] Integration with Valkey build system
- [ ] Real client/server setup for integration tests
- [ ] Parallel test execution
- [ ] Property test discovery and auto-registration
- [ ] Coverage reporting
- [ ] Shrinking failed test cases to minimal examples

## References

- [RFC 0004: Advanced Testing Infrastructure](../../rfcs/0004-advanced-testing-infrastructure.md)
- [QuickCheck: Property-Based Testing](https://en.wikipedia.org/wiki/QuickCheck)
- [Hypothesis: Python Property Testing](https://hypothesis.readthedocs.io/)
