/* Property-based testing framework for Valkey
 *
 * This framework enables property-based testing where properties (invariants)
 * are verified across many randomly-generated test cases.
 */

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
