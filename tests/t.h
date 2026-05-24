/*
 * Tiny assertion macros for OpenLords2 unit tests — mirrors the
 * style established in openrecet/tests/t.h, the sibling project's
 * canonical test harness.
 *
 * Each test function has signature `int name(void)` and returns:
 *   0 on pass
 *   1 on fail (T_FAIL has already emitted a message)
 *   2 on skip (e.g. vendor data missing)
 *
 * The test driver in test_main.c iterates over a list of these
 * functions, runs them, and prints a summary.
 */

#ifndef OPENLORDS2_TESTS_T_H
#define OPENLORDS2_TESTS_T_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OPENLORDS2_ROOT
#define OPENLORDS2_ROOT "."
#endif

#define T_FAIL(...) do { \
    fprintf(stderr, "  fail %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); \
    return 1; \
} while (0)

#define T_SKIP(...) do { \
    fprintf(stderr, "  skip: "); \
    fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); \
    return 2; \
} while (0)

#define T_ASSERT(cond) do { \
    if (!(cond)) T_FAIL("assertion failed: %s", #cond); \
} while (0)

#define T_ASSERT_EQ_U(a, b) do { \
    unsigned long long _a = (unsigned long long)(a); \
    unsigned long long _b = (unsigned long long)(b); \
    if (_a != _b) T_FAIL("expected %s == %s (got %llu, want %llu)", \
                         #a, #b, _a, _b); \
} while (0)

#define T_ASSERT_EQ_I(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a != _b) T_FAIL("expected %s == %s (got %lld, want %lld)", \
                         #a, #b, _a, _b); \
} while (0)

#define T_ASSERT_EQ_S(a, b) do { \
    const char *_a = (a); \
    const char *_b = (b); \
    if (strcmp(_a, _b) != 0) \
        T_FAIL("expected %s == %s (got \"%s\", want \"%s\")", \
               #a, #b, _a, _b); \
} while (0)

#define T_ASSERT_MEM_EQ(a, b, n) do { \
    if (memcmp((a), (b), (n)) != 0) \
        T_FAIL("memory differs over %zu bytes", (size_t)(n)); \
} while (0)

#endif /* OPENLORDS2_TESTS_T_H */
