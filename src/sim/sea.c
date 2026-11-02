#include "sea.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "mathx.h"

#define G 9.81
#define TWO_PI 6.283185307179586
#define HULL_SAMPLES 7
#define SLOPE_MAX 0.6f
#define DRAG_K 6.0f         /* drag_mult per m^2 of wave variance, at hull speed */
#define BROACH_K 0.35f
#define HEEL_ARM 0.25f      /* m, lever of the cross-slope heeling moment */
#define RATE_EPS 0.02       /* s, finite-difference step for the bow's vertical rate */

static float fin(float x) { return isfinite(x) ? x : 0.0f; }
static double find(double x) { return isfinite(x) ? x : 0.0; }

static double height(const Sea *s, double x, double y, double t) {
    double xw = fmod(x, SEA_WRAP), yw = fmod(y, SEA_WRAP);
    double h = 0;
    for (int i = 0; i < s->n; i++) {
        const SeaComp *c = &s->c[i];
        double ph = TWO_PI * fmod(c->m * xw + c->n * yw, SEA_WRAP) / SEA_WRAP
                  - fmod((double)c->omega * t, TWO_PI) + c->phase;
        h += c->amp * cos(ph);
    }
    return h;
}

void sea_init(Sea *s, const WeatherPreset *p, uint32_t seed) {
    static const int harmonic[SEA_MAX_COMPONENTS] = { 1, 1, 1, 2, 2, 3 };
    static const float weight[SEA_MAX_COMPONENTS] = { 0.30f, 0.25f, 0.22f, 0.10f, 0.08f, 0.05f };
    /* the main swells cross at steep angles so the surface is a field of humps, not parallel ridges */
    static const float spread_deg[SEA_MAX_COMPONENTS] = { 0, 70, -70, 35, -35, 90 };
    memset(s, 0, sizeof *s);
    s->n = SEA_MAX_COMPONENTS;
    s->period = clampf(fin(p->wave_period), 0.5f, 30.0f);
    s->slam_speed = 1.0f;
    s->slam_gain = 1.5f;
    s->surf_gain = 0.6f;

    float H = clampf(fin(p->wave_height), 0.0f, 10.0f);
    float chop = clampf(fin(p->wave_steepness) / 0.1f, 0.25f, 2.5f);
    float w[SEA_MAX_COMPONENTS], wsum = 0;
    for (int i = 0; i < s->n; i++) {
        w[i] = weight[i] * (harmonic[i] > 1 ? chop : 1.0f);
        wsum += w[i];
    }
    float dir0 = hashf(hash2(1, 0, seed)) * (float)TWO_PI;
    for (int i = 0; i < s->n; i++) {
        SeaComp *c = &s->c[i];
        float dir = dir0 + deg2rad(spread_deg[i] + (hashf(hash2(2, i, seed)) - 0.5f) * 20.0f);
        c->omega = (float)(TWO_PI * harmonic[i] / s->period);
        float k = c->omega * c->omega / (float)G;
        float km = k * (float)SEA_WRAP / (float)TWO_PI;
        c->m = (int)lroundf(km * cosf(dir));
        c->n = (int)lroundf(km * sinf(dir));
        if (c->m == 0 && c->n == 0) c->m = 1;
        c->amp = 0.5f * H * w[i] / wsum;
        c->phase = hashf(hash2(3, i, seed)) * (float)TWO_PI;
    }
}

float sea_height(const Sea *s, float x, float y, float t) {
    return (float)height(s, find(x), find(y), find(t));
}

/* One period of cos, sampled COS_N times (+1 so interpolation never wraps), read by sea_view_height. */
#define COS_N 1024
static float cos_tab[COS_N + 1];
static bool cos_ready;

static void cos_tab_init(void) {
    for (int i = 0; i <= COS_N; i++) cos_tab[i] = (float)cos(TWO_PI * i / COS_N);
    cos_ready = true;
}

void sea_view_init(SeaView *v, const Sea *s, double ox, double oy, float t) {
    double xw = fmod(find(ox), SEA_WRAP), yw = fmod(find(oy), SEA_WRAP), td = find(t);
    if (!cos_ready) cos_tab_init();
    v->n = s->n;
    for (int i = 0; i < s->n; i++) {
        const SeaComp *c = &s->c[i];
        v->kx[i] = (float)(TWO_PI * c->m / SEA_WRAP);
        v->ky[i] = (float)(TWO_PI * c->n / SEA_WRAP);
        v->ph[i] = (float)(TWO_PI * fmod(c->m * xw + c->n * yw, SEA_WRAP) / SEA_WRAP
                         - fmod((double)c->omega * td, TWO_PI) + c->phase);
        v->amp[i] = c->amp;
    }
}

static inline float table_cosf(float x) {
    float u = x * 0.15915494f;            /* turns */
    u -= (float)(int)u;
    if (u < 0.0f) u += 1.0f;
    float f = u * (float)COS_N;
    int i = (int)f;
    if (i >= COS_N) i = COS_N - 1;
    float a = cos_tab[i];
    return a + (f - (float)i) * (cos_tab[i + 1] - a);
}

float sea_view_height(const SeaView *v, float dx, float dy) {
    float h = 0.0f;
    for (int i = 0; i < v->n; i++) h += v->amp[i] * table_cosf(v->kx[i] * dx + v->ky[i] * dy + v->ph[i]);
    return h;
}

typedef struct { double h[HULL_SAMPLES + 2]; } Samples;

/* Sample 0..6 run stern to bow along the keel; 7 is port, 8 is starboard at midships. */
static void sample(const Sea *s, double x, double y, double heading, float L, float beam,
                   double t, Samples *out) {
    double fx = cos(heading), fy = sin(heading);
    double sx = -fy, sy = fx;
    for (int i = 0; i < HULL_SAMPLES; i++) {
        double d = L * ((double)i / (HULL_SAMPLES - 1) - 0.5);
        out->h[i] = height(s, x + d * fx, y + d * fy, t);
    }
    out->h[7] = height(s, x - 0.5 * beam * sx, y - 0.5 * beam * sy, t);
    out->h[8] = height(s, x + 0.5 * beam * sx, y + 0.5 * beam * sy, t);
}

/* Least-squares plane along the keel: mean elevation and bow-up slope. */
static void fit(const Samples *a, float L, float *mean, float *slope) {
    double m = 0, num = 0, den = 0;
    for (int i = 0; i < HULL_SAMPLES; i++) m += a->h[i];
    m /= HULL_SAMPLES;
    for (int i = 0; i < HULL_SAMPLES; i++) {
        double d = L * ((double)i / (HULL_SAMPLES - 1) - 0.5);
        num += d * (a->h[i] - m);
        den += d * d;
    }
    *mean = (float)m;
    *slope = (float)(num / den);
}

WaveForces sea_hull_forces(const Sea *s, const BoatState *b, const BoatCfg *cfg, float t) {
    WaveForces f;
    memset(&f, 0, sizeof f);
    float var = 0;
    for (int i = 0; i < s->n; i++) var += 0.5f * s->c[i].amp * s->c[i].amp;
    if (!(var > 0.0f)) return f;

    double x = find(b->x), y = find(b->y), hd = find(b->heading), td = find(t);
    float u = fin(b->u), v = fin(b->v);
    float L = fmaxf(cfg->hull.length, 0.5f), beam = fmaxf(cfg->hull.beam, 0.2f);
    float m = cfg->hull.mass + cfg->hull.crew_mass;
    double vx = u * cos(hd) - v * sin(hd), vy = u * sin(hd) + v * cos(hd);
    float speed = hypotf(u, v);

    Samples a, a1;
    sample(s, x, y, hd, L, beam, td, &a);
    sample(s, x + vx * RATE_EPS, y + vy * RATE_EPS, hd, L, beam, td + RATE_EPS, &a1);
    float mean, slope, mean1, slope1;
    fit(&a, L, &mean, &slope);
    fit(&a1, L, &mean1, &slope1);
    slope = clampf(slope, -SLOPE_MAX, SLOPE_MAX);
    float cross = clampf((float)(a.h[8] - a.h[7]) / beam, -SLOPE_MAX, SLOPE_MAX);

    f.heave = mean;
    f.pitch = atanf(slope);
    f.drag_mult = DRAG_K * var * clampf(speed / fmaxf(cfg->hull.hull_speed, 0.1f), 0.0f, 1.2f);

    /* surf: the downslope component of weight along the keel */
    f.surge = -s->surf_gain * m * (float)G * slope;
    /* slam: the bow meets rising water faster than the hull's own motion carries it */
    if (s->slam_gain > 0.0f && speed > s->slam_speed) {
        double bow = L * 0.5;
        double rel0 = a.h[HULL_SAMPLES - 1] - mean - slope * bow;
        double rel1 = a1.h[HULL_SAMPLES - 1] - mean1 - slope1 * bow;
        float rb = (float)((rel1 - rel0) / RATE_EPS);
        if (rb > 0.0f) f.surge -= s->slam_gain * m * rb * rb;
    }
    f.heel = -m * (float)G * HEEL_ARM * cross;
    /* broach: running down a face that also tilts across the hull swings the bow round */
    f.yaw = BROACH_K * m * (float)G * L * fmaxf(-slope, 0.0f) * cross;

    f.drag_mult = fin(f.drag_mult); f.surge = fin(f.surge); f.yaw = fin(f.yaw);
    f.heel = fin(f.heel); f.pitch = fin(f.pitch); f.heave = fin(f.heave);
    return f;
}
