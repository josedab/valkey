/* Property-based testing framework implementation */

#include "property_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TEST_SUITES 100

static property_test_suite *registered_suites[MAX_TEST_SUITES];
static size_t suite_count = 0;

/* Register a property test suite */
void registerPropertyTestSuite(property_test_suite *suite) {
    if (suite_count >= MAX_TEST_SUITES) {
        fprintf(stderr, "Error: Maximum number of test suites reached\n");
        return;
    }
    registered_suites[suite_count++] = suite;
}

/* Run a single property test */
static int runSinglePropertyTest(property_test *test) {
    printf("Running property test: %s\n", test->name);
    printf("  Description: %s\n", test->description);
    printf("  Iterations: %d, Seed: %u\n", test->iterations, test->seed);

    /* Set random seed for reproducibility */
    srand(test->seed);

    int passed = 0;
    int failed = 0;
    int errors = 0;

    for (int i = 0; i < test->iterations; i++) {
        property_test_result result = test->test_fn(NULL);

        switch (result) {
            case PROPERTY_TEST_PASS:
                passed++;
                break;
            case PROPERTY_TEST_FAIL:
                failed++;
                printf("  [FAIL] Iteration %d failed\n", i + 1);
                break;
            case PROPERTY_TEST_ERROR:
                errors++;
                printf("  [ERROR] Iteration %d had an error\n", i + 1);
                break;
        }

        /* Fail fast on first failure for debugging */
        if (result != PROPERTY_TEST_PASS) {
            break;
        }
    }

    printf("  Results: %d passed, %d failed, %d errors\n", passed, failed, errors);

    if (failed > 0 || errors > 0) {
        printf("  [FAIL] %s\n", test->name);
        return 0;
    } else {
        printf("  [PASS] %s\n", test->name);
        return 1;
    }
}

/* Run all tests in a specific suite */
int runPropertyTests(const char *suite_name) {
    property_test_suite *suite = NULL;

    /* Find the suite */
    for (size_t i = 0; i < suite_count; i++) {
        if (strcmp(registered_suites[i]->suite_name, suite_name) == 0) {
            suite = registered_suites[i];
            break;
        }
    }

    if (!suite) {
        fprintf(stderr, "Error: Test suite '%s' not found\n", suite_name);
        return -1;
    }

    printf("\n========================================\n");
    printf("Running property test suite: %s\n", suite->suite_name);
    printf("========================================\n");

    int total_passed = 0;
    int total_failed = 0;

    for (size_t i = 0; i < suite->test_count; i++) {
        if (runSinglePropertyTest(&suite->tests[i])) {
            total_passed++;
        } else {
            total_failed++;
        }
        printf("\n");
    }

    printf("========================================\n");
    printf("Suite '%s' Results: %d passed, %d failed\n",
           suite->suite_name, total_passed, total_failed);
    printf("========================================\n\n");

    return total_failed > 0 ? 1 : 0;
}

/* Run all registered property tests */
int runAllPropertyTests(void) {
    printf("\n");
    printf("========================================\n");
    printf("Running all property test suites\n");
    printf("Total suites: %zu\n", suite_count);
    printf("========================================\n\n");

    int total_failed_suites = 0;

    for (size_t i = 0; i < suite_count; i++) {
        if (runPropertyTests(registered_suites[i]->suite_name) != 0) {
            total_failed_suites++;
        }
    }

    printf("\n");
    printf("========================================\n");
    printf("All suites completed\n");
    printf("Passed: %zu, Failed: %d\n", suite_count - total_failed_suites, total_failed_suites);
    printf("========================================\n");

    return total_failed_suites > 0 ? 1 : 0;
}
