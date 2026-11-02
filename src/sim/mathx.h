#ifndef MATHX_H
#define MATHX_H
#include <stdint.h>
#include <math.h>

typedef struct { float x, y; } Pt;

float clampf(float v, float lo, float hi);
float wrap_pi(float a);                         /* -> (-pi, pi]; non-finite -> 0 */
float deg2rad(float d);
float table_eval(const Pt *t, int n, float x);  /* linear, clamped at ends; n<=0 -> 0 */
uint32_t hash2(int32_t a, int32_t b, uint32_t seed);
float hashf(uint32_t h);                        /* -> [0,1) */

/* cosf with a float-only range reduction and an even polynomial (|error| < 1e-6). libm's cosf takes
   a slow soft-double argument reduction whenever |x| > pi/4, which is nearly every sample here. */
static inline float fast_cosf(float x) {
    const float inv_two_pi = 0.15915494f, two_pi = 6.2831855f, half_pi = 1.5707964f, pi = 3.1415927f;
    x -= two_pi * (float)(int)(x * inv_two_pi + (x < 0.0f ? -0.5f : 0.5f));   /* -> [-pi, pi] */
    x = fabsf(x);
    float sign = 1.0f;
    if (x > half_pi) { x = pi - x; sign = -1.0f; }                            /* cos(pi - x) = -cos(x) */
    float x2 = x * x;
    return sign * (1.0f + x2 * (-0.5f + x2 * (4.1666668e-2f + x2 * (-1.3888889e-3f
                 + x2 * (2.4801587e-5f + x2 * (-2.7557319e-7f + x2 * 2.0876757e-9f))))));
}

/* lroundf without the libm call: half away from zero, for values well inside int range. */
static inline int iroundf(float x) { return (int)(x + (x < 0.0f ? -0.5f : 0.5f)); }

static inline float fast_sinf(float x) { return fast_cosf(x - 1.5707964f); }

#endif
