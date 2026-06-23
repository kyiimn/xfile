#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "ASSERT_EQ FAILED: %s != %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "ASSERT_STR_EQ FAILED: '%s' != '%s' (%s:%d)\n", (a), (b), __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define ASSERT_NOT_NULL(p) do { \
    if ((p) == NULL) { \
        fprintf(stderr, "ASSERT_NOT_NULL FAILED: %s (%s:%d)\n", #p, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define TEST_BEGIN() fprintf(stdout, "Running %s...\n", __func__)
#define TEST_PASS() fprintf(stdout, "PASS: %s\n", __func__); return 0
#define TEST_FAIL(msg) do { fprintf(stderr, "FAIL: %s: %s\n", __func__, msg); return 1; } while(0)

#endif /* TEST_FRAMEWORK_H */