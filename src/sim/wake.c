#include "wake.h"
#include "mathx.h"

float wake_length(float k) { return 3.0f + 6.0f * clampf(k, 0.0f, 1.0f); }

float wake_fade_start(float fade_m) {
    float s = fade_m / wake_length(1.0f);
    return s < 0.0f ? 0.0f : s;
}

bool wake_segment(float f, float fade_start, float half, float *lo, float *hi) {
    float t = 0.0f;
    if (f > fade_start) t = clampf((f - fade_start) / (1.0f - fade_start), 0.0f, 1.0f);
    *lo = 0.5f * t * half;
    *hi = half * (1.0f - 0.5f * t);
    return t < 0.98f;
}
