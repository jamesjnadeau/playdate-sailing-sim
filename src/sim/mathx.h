#ifndef MATHX_H
#define MATHX_H
#include <stdint.h>

typedef struct { float x, y; } Pt;

float clampf(float v, float lo, float hi);
float wrap_pi(float a);                         /* -> (-pi, pi]; non-finite -> 0 */
float deg2rad(float d);
float table_eval(const Pt *t, int n, float x);  /* linear, clamped at ends; n<=0 -> 0 */
uint32_t hash2(int32_t a, int32_t b, uint32_t seed);
float hashf(uint32_t h);                        /* -> [0,1) */

#endif
