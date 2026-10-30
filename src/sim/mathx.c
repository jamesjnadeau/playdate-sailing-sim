#include "mathx.h"
#include <math.h>

#define PI_F 3.14159265358979f

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float wrap_pi(float a) {
    if (!isfinite(a)) return 0.0f;
    float r = fmodf(a, 2.0f * PI_F);
    if (r <= -PI_F) r += 2.0f * PI_F;
    else if (r > PI_F) r -= 2.0f * PI_F;
    return r;
}

float deg2rad(float d) {
    return d * (PI_F / 180.0f);
}

float table_eval(const Pt *t, int n, float x) {
    if (n <= 0) return 0.0f;
    if (x <= t[0].x) return t[0].y;
    if (x >= t[n - 1].x) return t[n - 1].y;
    for (int i = 1; i < n; i++) {
        if (x <= t[i].x) {
            float dx = t[i].x - t[i - 1].x;
            if (dx <= 0.0f) return t[i].y;
            float f = (x - t[i - 1].x) / dx;
            return t[i - 1].y + f * (t[i].y - t[i - 1].y);
        }
    }
    return t[n - 1].y;
}

uint32_t hash2(int32_t a, int32_t b, uint32_t seed) {
    uint32_t h = seed ^ 0x9e3779b9u;
    h ^= (uint32_t)a * 0x85ebca6bu;
    h = (h << 13) | (h >> 19);
    h *= 0xc2b2ae35u;
    h ^= (uint32_t)b * 0x27d4eb2fu;
    h ^= h >> 15;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

float hashf(uint32_t h) {
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}
