#ifndef T_H
#define T_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int t_run_count, t_fail_count;

#define T_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } } while (0)

#define T_NEAR(a, b, eps) do { \
    double t_a = (a), t_b = (b), t_e = (eps); \
    if (!(fabs(t_a - t_b) <= t_e)) { \
        fprintf(stderr, "%s:%d: %s = %g, expected %s = %g (+/- %g)\n", \
                __FILE__, __LINE__, #a, t_a, #b, t_b, t_e); \
        exit(1); \
    } } while (0)

#define T_RUN(fn) do { t_run_count++; fn(); } while (0)

static int T_DONE(void) {
    printf("%d test%s passed\n", t_run_count, t_run_count == 1 ? "" : "s");
    return t_fail_count ? 1 : 0;
}
#endif
