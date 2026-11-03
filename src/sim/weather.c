#include "weather.h"
#include "json.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *const WEATHER_NAMES[WEATHER_PRESETS] = { "mist", "sunny", "cloudy", "choppy", "rollers", "storm", "hurricane" };

#define PI_F 3.14159265358979f

typedef struct { const char *key; size_t off; float mn, mx; } Fld;
#define F(f, lo, hi) { #f, offsetof(WeatherPreset, f), lo, hi }

static const Fld FLDS[] = {
    F(wind_base, 0, 40),
    F(wind_var, 0, 20),
    F(shift_rate_deg, 0, 30),
    F(gust_speed, 0, 30),
    F(gust_interval, 1, 600),
    F(wave_height, 0, 10),
    F(wave_period, 1, 20),
    F(wave_steepness, 0, 1),
    F(visibility, 20, 20000),
    F(sky_top, 0, 15),
    F(sky_horizon, 0, 15),
    F(cloud_cover, 0, 1),
    F(cloud_shade, 0, 15),
    F(cloud_size, 0.1f, 5),
    F(cloud_speed, 0, 20),
    F(rain, 0, 1),
    F(lightning_interval, 0, 600),
    F(sun, 0, 1),
};
#define NFLD ((int)(sizeof FLDS / sizeof FLDS[0]))

/* Wind from the Beaufort scale; gusts give a ~1.3-1.5 open-water gust factor.
   Wave heights are the WMO probable height for that force, periods for a fetch-limited sea. */
static const WeatherPreset DEFAULTS[WEATHER_PRESETS] = {
    /* name        base var   shift gust  ivl  wh    wp    steep  vis    sky: top hz  cover shade size spd  rain ltng sun */
    { "mist",      2,   0.5f, 0.5f, 0.8f, 30, 0.2f, 3,    0.07f, 150,   3,  1,  0.3f,  3,  1.5f, 0.3f, 0,   0,  0 },   /* F2, fog needs light air, ~4 kt */
    { "sunny",     5,   1,    0.5f, 2,    25, 0.6f, 4,    0.05f, 2000,  0,  0,  0.3f,  2,  0.8f, 1,    0,   0,  1 },   /* F3 sea breeze, ~10 kt */
    { "cloudy",    7,   1.5f, 1,    3,    20, 1.0f, 5,    0.1f,  1200,  4,  1,  0.85f, 6,  1.3f, 1,    0,   0,  0 },   /* F4, ~14 kt */
    { "choppy",    10,  1.5f, 0.5f, 3,    25, 2.0f, 5.5f, 0.45f, 2000,  1,  0,  0.5f,  5,  1,    2,    0,   0,  1 },   /* F5, ~19 kt, short steep sea */
    { "rollers",   14,  2,    1,    5,    15, 5.5f, 9,    0.25f, 1200,  5,  2,  0.75f, 8,  1.2f, 2.5f, 0,   0,  0 },   /* F6, ~27 kt wind over a storm-sized swell */
    { "storm",     20,  3,    2,    10,   10, 5.5f, 9,    0.25f, 400,   13, 8,  1,     9,  2.5f, 4,    0.5f, 20, 0 },   /* F8 gale, ~39 kt, squalls to ~58 kt */
    { "hurricane", 36,  5,    2,    12,   10, 9,    12,   0.45f, 400,   14, 10, 1,     11, 3,    6,    1,   8,  0 },   /* Cat 1, ~70 kt, gusts to ~93 kt */
};

void weather_default(WeatherCfg *out) {
    memset(out, 0, sizeof *out);
    memcpy(out->preset, DEFAULTS, sizeof DEFAULTS);
}

static void wadd(char *buf, size_t cap, size_t *n, bool *any, const char *fmt, ...) {
    *any = true;
    if (!buf || cap < 2) return;
    if (*n) {
        if (*n + 3 >= cap) return;
        buf[(*n)++] = ';';
        buf[(*n)++] = ' ';
    }
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(buf + *n, cap - *n, fmt, ap);
    va_end(ap);
    if (k < 0) { buf[*n] = 0; return; }
    *n += (size_t)k < cap - *n ? (size_t)k : cap - *n - 1;
}

bool weather_load(WeatherCfg *out, const char *buf, size_t len, char *warn, size_t warnlen) {
    size_t wn = 0;
    bool any = false;
    if (warn && warnlen) warn[0] = 0;
    weather_default(out);
    if (!buf || len == 0) { wadd(warn, warnlen, &wn, &any, "no weather data"); return false; }
    char err[96];
    JsonDoc *doc = json_parse(buf, len, err, sizeof err);
    if (!doc) { wadd(warn, warnlen, &wn, &any, "parse error: %s", err); return false; }
    const JsonNode *root = json_root(doc);
    if (!json_is_object(root)) {
        wadd(warn, warnlen, &wn, &any, "root: expected object");
        json_free(doc);
        return false;
    }
    for (int p = 0; p < WEATHER_PRESETS; p++) {
        const char *name = DEFAULTS[p].name;
        const JsonNode *g = json_get(root, name);
        if (!g) continue;
        if (!json_is_object(g)) { wadd(warn, warnlen, &wn, &any, "%s: expected object", name); continue; }
        for (int i = 0; i < NFLD; i++) {
            const JsonNode *nd = json_get(g, FLDS[i].key);
            if (!nd) continue;
            float *dst = (float *)((char *)&out->preset[p] + FLDS[i].off);
            if (!json_is_number(nd)) {
                wadd(warn, warnlen, &wn, &any, "%s.%s: expected number", name, FLDS[i].key);
                continue;
            }
            float v = json_num(nd, *dst);
            if (!isfinite(v)) {
                wadd(warn, warnlen, &wn, &any, "%s.%s: not a finite number", name, FLDS[i].key);
                continue;
            }
            if (v < FLDS[i].mn || v > FLDS[i].mx) {
                wadd(warn, warnlen, &wn, &any, "%s.%s: %g outside [%g, %g], clamped",
                     name, FLDS[i].key, v, FLDS[i].mn, FLDS[i].mx);
                v = clampf(v, FLDS[i].mn, FLDS[i].mx);
            }
            *dst = v;
        }
    }
    json_free(doc);
    return !any;
}

/* ---- wind: sum of incommensurate sines (slow noise) + sin^2 gust events ---- */

static const float W_FREQ[3] = { 0.05f, 0.13f, 0.31f };   /* rad/s, speed noise */
static const float W_AMP[3]  = { 0.5f, 0.3f, 0.2f };      /* sums to 1 */
static const float D_FREQ[3] = { 0.04f, 0.11f, 0.27f };   /* rad/s, direction */
static const float D_K[3]    = { 0.5f, 0.3f, 0.2f };      /* share of the rate cap */

static float speed_noise(const Wind *w) {
    float n = 0;
    for (int i = 0; i < 3; i++) n += W_AMP[i] * sinf((float)fmod(W_FREQ[i] * w->t, 2 * PI_F) + w->phase[i]);
    return n;
}

/* direction offset; |d/dt| <= shift_rate by construction (amp_i * freq_i = K_i * rate) */
static float dir_offset(const Wind *w, const WeatherPreset *p) {
    float rate = deg2rad(p->shift_rate_deg), off = 0;
    for (int i = 0; i < 3; i++)
        off += D_K[i] * rate / D_FREQ[i] * sinf((float)fmod(D_FREQ[i] * w->t, 2 * PI_F) + w->phase[3 + i]);
    return off;
}

static void schedule_gust(Wind *w, const WeatherPreset *p) {
    uint32_t h = hash2((int32_t)w->gust_n++, 77, w->seed);
    w->next_gust = w->t + p->gust_interval * (0.5f + hashf(h));
    w->gust_len = 4.0f + 6.0f * hashf(hash2(1, (int32_t)w->gust_n, w->seed));
    w->gust_amp = p->gust_speed * (0.5f + 0.5f * hashf(hash2(2, (int32_t)w->gust_n, w->seed)));
    w->gust_t = -1;
}

static void evaluate(Wind *w, const WeatherPreset *p) {
    float g = 0;
    if (w->gust_t >= 0 && w->gust_t < w->gust_len) {
        float s = sinf(PI_F * (float)(w->gust_t / w->gust_len));
        g = w->gust_amp * s * s;
    }
    float v = w->base + p->wind_var * speed_noise(w) + g;
    w->speed = v < 0 ? 0 : v;
    w->dir = wrap_pi(w->dir0 + dir_offset(w, p));
}

void wind_init(Wind *w, const WeatherPreset *p, uint32_t seed) {
    memset(w, 0, sizeof *w);
    w->seed = seed;
    w->base = p->wind_base;
    w->dir0 = (hashf(hash2(9, 9, seed)) * 2 - 1) * PI_F;
    for (int i = 0; i < 6; i++) w->phase[i] = hashf(hash2(i, 5, seed)) * 2 * PI_F;
    schedule_gust(w, p);
    evaluate(w, p);
}

void wind_step(Wind *w, const WeatherPreset *p, float dt) {
    if (!(dt > 0)) dt = 0;
    if (dt > 1.0f) dt = 1.0f;
    w->t += dt;
    if (w->gust_t >= 0) {
        w->gust_t += dt;
        if (w->gust_t >= w->gust_len) schedule_gust(w, p);
    } else if (w->t >= w->next_gust) {
        w->gust_t = 0;
    }
    evaluate(w, p);
}

void wind_set_base(Wind *w, float dir0) {
    w->dir = wrap_pi(w->dir + (dir0 - w->dir0));
    w->dir0 = dir0;
}

void wind_set_speed(Wind *w, const WeatherPreset *p, float mps) {
    w->base = mps;
    evaluate(w, p);
}

void wind_vec(const Wind *w, float *wx, float *wy) {
    *wx = w->speed * cosf(w->dir);
    *wy = w->speed * sinf(w->dir);
}
