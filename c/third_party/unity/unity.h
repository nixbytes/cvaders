/*
 * unity.h -- A tiny, dependency-free C test harness.
 *
 * We use this instead of the full Unity framework because the build
 * environment has no internet access to fetch it. The API below mirrors
 * the common Unity C testing API (unity.github.io/unity) closely enough
 * for this project.
 *
 * NEW-COMER GUIDE TO THE MACROS BELOW
 *
 *  A C "macro" is text that the preprocessor copies in verbatim before the
 *  compiler runs. We use them as one-line CHECKS. Each one:
 *    - evaluates a condition
 *    - on FAILURE: prints where it happened, bumps a failure counter, and
 *      `return`s out of the calling test
 *    - on SUCCESS: does nothing
 *
 *  Because a macro is just copy-paste text, the tricky part is making sure
 *  a one-statement-looking macro cannot "leak" statements into a following
 *  `if`/`else`. The `do { ... } while (0)` wrapper is the standard trick:
 *  it glues the whole block together so the macro behaves like exactly one
 *  statement no matter where you drop it.
 */
#ifndef UNITY_H
#define UNITY_H

#include <stdio.h>

/* The global scoreboard. A struct holds two integers: how many tests we
 * have run so far and how many failed. `extern` here just DECLARES it so
 * this header can mention it; the real definition is in unity.c. */
typedef struct {
    int tests_run;
    int tests_failed;
} UnityState;

extern UnityState unity_state;

/*
 * Call this ONCE at the very end of main(). It prints a summary line and
 * returns 0 (all passed) or 1 (something failed). C programs tell the OS
 * "did I succeed?" by the value main() returns: 0 means success, anything
 * else means failure.
 */
int UNITY_END(void);

/*
 * Raw "assert this is true" -- the workhorse. We wrap it in do/while(0) so
 * it is safe to call as a single statement.
 */
#define TEST_ASSERT(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        unity_state.tests_failed++; \
        return; \
    } \
} while (0)

/* TEST_ASSERT_TRUE / TEST_ASSERT_FALSE are just TEST_ASSERT on a bool. */
#define TEST_ASSERT_TRUE(cond)               TEST_ASSERT(cond)
#define TEST_ASSERT_FALSE(cond)              TEST_ASSERT(!(cond))

/*
 * Compare two integers. We cast to `long` so the comparison is well-defined
 * for 64-bit platforms.
 */
#define TEST_ASSERT_EQUAL_INT(expected, actual) do { \
    long _e = (long)(expected), _a = (long)(actual); \
    if (_e != _a) { \
        fprintf(stderr, "FAIL: %s:%d: expected %ld got %ld\n", \
                __FILE__, __LINE__, _e, _a); \
        unity_state.tests_failed++; \
        return; \
    } \
} while (0)

/*
 * Compare two floating-point numbers. NEVER test floats for exact
 * equality: binary floating point has rounding error, so numbers that look
 * equal can differ in the last bit. Instead we check the difference is
 * smaller than a tiny tolerance (here 1e-9). This is a rule every C
 * programmer should internalise.
 */
#define TEST_ASSERT_EQUAL_DOUBLE(expected, actual) do { \
    double _e = (double)(expected), _a = (double)(actual); \
    double _d = _e - _a; if (_d < 0) _d = -_d; \
    if (_d > 1e-9) { \
        fprintf(stderr, "FAIL: %s:%d: expected %.9f got %.9f\n", \
                __FILE__, __LINE__, _e, _a); \
        unity_state.tests_failed++; \
        return; \
    } \
} while (0)

/*
 * Same idea, but with a caller-supplied tolerance -- useful when a value
 * has legitimately accumulated rounding error over many operations.
 */
#define TEST_ASSERT_DOUBLE_WITHIN(tolerance, expected, actual) do { \
    double _e = (double)(expected), _a = (double)(actual); \
    double _d = _e - _a; if (_d < 0) _d = -_d; \
    if (_d > (tolerance)) { \
        fprintf(stderr, "FAIL: %s:%d: expected within %.6f of %.9f got %.9f\n", \
                __FILE__, __LINE__, tolerance, _e, _a); \
        unity_state.tests_failed++; \
        return; \
    } \
} while (0)

#endif /* UNITY_H */
