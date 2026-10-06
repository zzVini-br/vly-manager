#ifndef VLY_TEST_H
#define VLY_TEST_H

#include <stdio.h>

/* Minimal test helpers: each test is an executable that returns non-zero
 * if any CHECK failed. */

static int test_failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            test_failures++;                                                                       \
        }                                                                                          \
    } while (0)

#define TEST_RESULT() (test_failures == 0 ? 0 : 1)

#endif /* VLY_TEST_H */
