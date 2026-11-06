#include "weathershift.h"
#include "mathx.h"
#include "weather.h"

enum { SALT_START = 0x5710, SALT_GAP = 0x5711, SALT_COIN = 0x5712 };

static float draw(WeatherShift *s, int salt) {
    return hashf(hash2(salt, (int32_t)s->n++, s->seed));
}

static void roll_gap(WeatherShift *s, float gap_min, float gap_max) {
    s->gap = gap_min + (gap_max - gap_min) * draw(s, SALT_GAP);
}

int wshift_neighbour(uint32_t mask, int cur, float coin) {
    int down = -1, up = -1;
    for (int p = cur - 1; p >= 0; p--)
        if (mask & (1u << p)) { down = p; break; }
    for (int p = cur + 1; p < WEATHER_PRESETS; p++)
        if (mask & (1u << p)) { up = p; break; }
    if (down < 0) return up;
    if (up < 0) return down;
    return coin < 0.5f ? down : up;
}

int wshift_init(WeatherShift *s, uint32_t mask, int fallback, uint32_t seed, float gap_min, float gap_max) {
    mask &= (1u << WEATHER_PRESETS) - 1u;
    if (!mask) mask = 1u << fallback;
    s->mask = mask;
    s->seed = seed;
    s->n = 0;
    int count = 0;
    for (int p = 0; p < WEATHER_PRESETS; p++) count += (mask >> p) & 1u;
    int pick = 0;
    if (count > 1) {
        pick = (int)(draw(s, SALT_START) * (float)count);
        if (pick >= count) pick = count - 1;
    }
    int start = fallback;
    for (int p = 0; p < WEATHER_PRESETS; p++) {
        if (!(mask & (1u << p))) continue;
        start = p;
        if (pick-- == 0) break;
    }
    s->from = s->to = start;
    s->progress = 1.0f;
    roll_gap(s, gap_min, gap_max);
    return start;
}

WShiftEvent wshift_step(WeatherShift *s, float dt, float gap_min, float gap_max, float blend_s) {
    if (s->from != s->to) {
        s->progress += dt / blend_s;
        if (s->progress < 1.0f) return WSHIFT_NONE;
        s->progress = 1.0f;
        s->from = s->to;
        roll_gap(s, gap_min, gap_max);
        return WSHIFT_END;
    }
    s->gap -= dt;
    if (s->gap > 0.0f) return WSHIFT_NONE;
    int nb = wshift_neighbour(s->mask, s->to, draw(s, SALT_COIN));
    if (nb < 0) {
        roll_gap(s, gap_min, gap_max);
        return WSHIFT_NONE;
    }
    s->from = s->to;
    s->to = nb;
    s->progress = 0.0f;
    return WSHIFT_START;
}

float wshift_weight(const WeatherShift *s) {
    float t = s->progress < 0.0f ? 0.0f : s->progress > 1.0f ? 1.0f : s->progress;
    return t * t * (3.0f - 2.0f * t);
}
