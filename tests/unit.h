/* Minimal unit-test harness: no dependencies, one binary, non-zero exit on failure. */
#ifndef TESTS_UNIT_H
#define TESTS_UNIT_H

#include <stdio.h>
#include <string.h>

extern int g_checks;
extern int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_failures++;                                                        \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        long long va_ = (long long)(a), vb_ = (long long)(b);                    \
        g_checks++;                                                              \
        if (va_ != vb_) {                                                        \
            g_failures++;                                                        \
            printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, \
                   #a, #b, va_, vb_);                                            \
        }                                                                        \
    } while (0)

#define CHECK_MEM(a, b, n) CHECK(memcmp((a), (b), (n)) == 0)
#define CHECK_STR(a, b)    CHECK(strcmp((a), (b)) == 0)

#define RUN(fn)                                                                  \
    do {                                                                         \
        int before_ = g_failures;                                                \
        fn();                                                                    \
        printf("%s %s\n", (g_failures == before_) ? "ok  " : "FAIL", #fn);       \
    } while (0)

#endif
