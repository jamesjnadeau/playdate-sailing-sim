#include "t.h"
#include <math.h>
#include <string.h>
#include "sea.h"

static WeatherCfg wc;

static void setup(void) { weather_default(&wc); }

static WeatherPreset flat_preset(void) {
    WeatherPreset p = wc.preset[WEATHER_SUNNY];
    p.wave_height = 0.0f;
    return p;
}

/* One-component sea: wave travelling toward +x, wavelength 8 m. */
static Sea single(float amp) {
    Sea s;
    sea_init(&s, &wc.preset[WEATHER_STORM], 7);
    s.n = 1;
    s.c[0].m = 128; s.c[0].n = 0;               /* 128 * 2pi / 1024 -> 8 m */
    s.c[0].amp = amp;
    s.c[0].omega = sqrtf(9.81f * 2.0f * (float)M_PI / 8.0f);
    s.c[0].phase = 0.0f;
    return s;
}

static BoatState boat_at(double x, double y, float heading, float u) {
    BoatState b; memset(&b, 0, sizeof b);
    b.x = x; b.y = y; b.heading = heading; b.u = u;
    return b;
}

static void test_height_amplitude_matches_preset(void) {
    for (int p = 0; p < WEATHER_PRESETS; p++) {
        Sea s; sea_init(&s, &wc.preset[p], 3);
        T_ASSERT(s.n >= 1 && s.n <= SEA_MAX_COMPONENTS);
        float lo = 1e9f, hi = -1e9f;
        for (float x = 0; x < 300; x += 1.0f)
            for (float y = 0; y < 300; y += 1.0f)
                for (float t = 0; t < 12; t += 4.0f) {
                    float h = sea_height(&s, x, y, t);
                    lo = fminf(lo, h); hi = fmaxf(hi, h);
                }
        float H = wc.preset[p].wave_height;
        T_ASSERT((hi - lo) > 0.6f * H && (hi - lo) < 1.05f * H);
    }
}

static void test_view_matches_height(void) {
    for (int p = 0; p < WEATHER_PRESETS; p++) {
        Sea s; sea_init(&s, &wc.preset[p], 9);
        double ox = 123456.7, oy = -98765.4;
        float t = 41.3f;
        SeaView v; sea_view_init(&v, &s, ox, oy, t);
        for (int i = 0; i < 200; i++) {
            float dx = -150.0f + 1.5f * i, dy = 120.0f - 1.1f * i;
            double wx = fmod(ox + dx, SEA_WRAP), wy = fmod(oy + dy, SEA_WRAP);
            T_NEAR(sea_view_height(&v, dx, dy), sea_height(&s, (float)wx, (float)wy, t), 5e-3f);
        }
    }
}

static void test_periodic_in_time(void) {
    Sea s; sea_init(&s, &wc.preset[WEATHER_STORM], 5);
    float T = wc.preset[WEATHER_STORM].wave_period;
    for (int i = 0; i < 20; i++) {
        float x = 13.0f * i, y = -7.0f * i, t = 0.37f * i;
        T_NEAR(sea_height(&s, x, y, t), sea_height(&s, x, y, t + T), 1e-3f);
        T_NEAR(sea_height(&s, x, y, t), sea_height(&s, x, y, t + 3 * T), 1e-3f);
    }
}

static void test_flat_sea_gives_zero_forces(void) {
    WeatherPreset p = flat_preset();
    Sea s; sea_init(&s, &p, 1);
    BoatCfg cfg; boatcfg_default(&cfg);
    BoatState b = boat_at(10, 20, 0.7f, 2.5f);
    WaveForces f = sea_hull_forces(&s, &b, &cfg, 3.3f);
    T_ASSERT(f.drag_mult == 0 && f.surge == 0 && f.yaw == 0 && f.heel == 0 &&
             f.pitch == 0 && f.heave == 0);
    T_NEAR(sea_height(&s, 5, 5, 5), 0.0f, 1e-6f);
}

static void test_storm_drags_more_than_sunny(void) {
    Sea sun, storm;
    sea_init(&sun, &wc.preset[WEATHER_SUNNY], 1);
    sea_init(&storm, &wc.preset[WEATHER_STORM], 1);
    BoatCfg cfg; boatcfg_default(&cfg);
    BoatState b = boat_at(0, 0, 0, 2.5f);
    float ds = 0, dt_ = 0;
    for (int i = 0; i < 50; i++) {
        ds += sea_hull_forces(&sun, &b, &cfg, 0.2f * i).drag_mult;
        dt_ += sea_hull_forces(&storm, &b, &cfg, 0.2f * i).drag_mult;
    }
    T_ASSERT(dt_ > 1.5f * ds);
    T_ASSERT(dt_ > 0);
}

static void test_slam_only_on_reentry_above_speed(void) {
    BoatCfg cfg; boatcfg_default(&cfg);
    Sea s = single(0.6f);
    Sea noslam = s; noslam.slam_gain = 0.0f;

    /* head sea: waves go +x, boat points -x */
    int fast_hits = 0, slow_hits = 0, n = 240;
    for (int i = 0; i < n; i++) {
        float t = 0.05f * i;
        BoatState fast = boat_at(0, 0, (float)M_PI, s.slam_speed + 1.5f);
        BoatState slow = boat_at(0, 0, (float)M_PI, 0.5f * s.slam_speed);
        float a = sea_hull_forces(&s, &fast, &cfg, t).surge - sea_hull_forces(&noslam, &fast, &cfg, t).surge;
        float c = sea_hull_forces(&s, &slow, &cfg, t).surge - sea_hull_forces(&noslam, &slow, &cfg, t).surge;
        T_ASSERT(a <= 0.0f);                    /* slam only ever decelerates */
        if (a < -1e-3f) fast_hits++;
        if (fabsf(c) > 1e-6f) slow_hits++;
    }
    T_ASSERT(slow_hits == 0);                   /* below threshold */
    T_ASSERT(fast_hits > 0 && fast_hits < n);   /* some of the cycle, not all of it */
}

static void test_surf_down_face_positive_beam_on_zero(void) {
    BoatCfg cfg; boatcfg_default(&cfg);
    Sea s = single(0.6f);
    s.slam_gain = 0.0f;
    /* h = a cos(kx - wt); slope along +x is -a k sin(kx - wt). At t = 0 a face with
       negative slope ahead of the boat has sin(kx) > 0, e.g. kx = pi/2 -> x = 2 m. */
    BoatState dn = boat_at(2.0, 0, 0.0f, 1.0f);
    WaveForces f = sea_hull_forces(&s, &dn, &cfg, 0.0f);
    T_ASSERT(f.surge > 50.0f);
    /* going up the back of the same wave: kx = 3pi/2 -> x = 6 m */
    BoatState up = boat_at(6.0, 0, 0.0f, 1.0f);
    T_ASSERT(sea_hull_forces(&s, &up, &cfg, 0.0f).surge < -50.0f);
    /* beam-on: heading +y, slope along heading is zero */
    BoatState beam = boat_at(2.0, 0, (float)M_PI / 2, 1.0f);
    WaveForces g = sea_hull_forces(&s, &beam, &cfg, 0.0f);
    T_NEAR(g.surge, 0.0f, 1.0f);
}

static void test_far_from_origin_is_finite_and_equivalent(void) {
    Sea s; sea_init(&s, &wc.preset[WEATHER_STORM], 9);
    BoatCfg cfg; boatcfg_default(&cfg);
    double far = 1.0e6;                          /* a multiple of SEA_WRAP away from origin */
    far = SEA_WRAP * floor(far / SEA_WRAP);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 400; i++) {
        float dx = 0.7f * i, dy = 0.3f * i, t = 0.1f * i;
        float a = sea_height(&s, dx, dy, t);
        float b = sea_height(&s, (float)(far + dx), (float)(far + dy), t);
        T_ASSERT(isfinite(b));
        T_NEAR(a, b, 2e-2f);
        BoatState bs = boat_at(far + dx, far + dy, 0.4f, 2.0f);
        WaveForces f = sea_hull_forces(&s, &bs, &cfg, t);
        T_ASSERT(isfinite(f.drag_mult) && isfinite(f.surge) && isfinite(f.yaw) &&
                 isfinite(f.heel) && isfinite(f.pitch) && isfinite(f.heave));
        lo = fminf(lo, b); hi = fmaxf(hi, b);
    }
    T_ASSERT((hi - lo) > 0.5f);
    /* a genuinely non-aligned far point (x ~ 1e6, not a wrap multiple) stays finite and bounded */
    float h = sea_height(&s, 1.0e6f + 0.37f, -1.0e6f, 2.0f);
    T_ASSERT(isfinite(h) && fabsf(h) < wc.preset[WEATHER_STORM].wave_height);
}

static void test_bad_input_stays_finite(void) {
    Sea s; sea_init(&s, &wc.preset[WEATHER_STORM], 2);
    BoatCfg cfg; boatcfg_default(&cfg);
    BoatState b = boat_at(0, 0, NAN, INFINITY);
    WaveForces f = sea_hull_forces(&s, &b, &cfg, NAN);
    T_ASSERT(isfinite(f.surge) && isfinite(f.drag_mult) && isfinite(f.yaw) && isfinite(f.heel));
    T_ASSERT(isfinite(sea_height(&s, NAN, 0, 0)));
}

/* A diagonal sea so the surface tilts across a boat heading along +x. */
static Sea tilted(float amp) {
    Sea s = single(amp);
    s.c[0].n = 64;                              /* wavelength 16 m across the boat */
    s.slam_gain = 0.0f;
    return s;
}

/* Sea height at the starboard (+1) or port (-1) hull centre; heading 0 puts starboard on +y. */
static float sea_height_at_offset(const Sea *s, double x, double y, float t, float off) {
    return sea_height(s, (float)x, (float)(y + off), t);
}

/* A cat samples under its hulls, not at the overall beam. */
static void test_cat_sea_heights_under_each_hull(void) {
    Sea s = tilted(0.6f);
    BoatCfg cfg; boatcfg_default(&cfg);
    cfg.hull.hulls = 2; cfg.hull.beam = 2.41f; cfg.hull.hull_spacing = 1.95f;
    BoatState b = boat_at(2.0, 1.0, 0.0f, 1.0f);
    float t = 0.7f;
    WaveForces f = sea_hull_forces(&s, &b, &cfg, t);
    T_ASSERT(isfinite(f.sea_port) && isfinite(f.sea_stbd));
    T_ASSERT(f.sea_stbd != f.sea_port);
    /* within one sample of the true surface at y = +-0.975 */
    T_NEAR(f.sea_stbd, sea_height_at_offset(&s, 2.0, 1.0, t, +0.975f) - f.heave, 0.02f);
    T_NEAR(f.sea_port, sea_height_at_offset(&s, 2.0, 1.0, t, -0.975f) - f.heave, 0.02f);
}

/* The default dinghy's wave forces are bit-identical to those before hulls were added. */
static void test_monohull_wave_forces_unchanged(void) {
    Sea s = tilted(0.6f);
    s.slam_gain = 1.0f;
    BoatCfg cfg; boatcfg_default(&cfg);
    BoatState b = boat_at(1.0, -2.0, 0.4f, 2.5f);
    WaveForces f = sea_hull_forces(&s, &b, &cfg, 1.3f);
    /* captured with %a from sea_hull_forces before the catamaran change */
    T_ASSERT(f.drag_mult == 0x1.b00002p-1f);
    T_ASSERT(f.surge == 0x1.6bf196p+7f);
    T_ASSERT(f.yaw == -0x1.99c172p+2f);
    T_ASSERT(f.heel == 0x1.d959c8p+2f);
    T_ASSERT(f.pitch == -0x1.3a1822p-3f);
    T_ASSERT(f.heave == -0x1.e4578p-3f);
    
}

/* ---- boat-anchored sea ---- */

#define AX 5123.7
#define AY (-4096.2)

/* Heights at offsets up to 50 m around (x, y): sea_height, a SeaView and the hull heave. */
#define N_PROBE 11
static void probe(const Sea *s, double x, double y, float t, float out[3][N_PROBE]) {
    BoatCfg cfg; boatcfg_default(&cfg);
    SeaView v; sea_view_init(&v, s, x, y, t);
    for (int i = 0; i < N_PROBE; i++) {
        float dx = -50.0f + 10.0f * i, dy = 50.0f - 7.3f * i;
        out[0][i] = sea_height(s, (float)(x + dx), (float)(y + dy), t);
        out[1][i] = sea_view_height(&v, dx, dy);
        BoatState b = boat_at(x + dx, y + dy, 0.4f, 2.0f);
        out[2][i] = sea_hull_forces(s, &b, &cfg, t).heave;
    }
}

static void expect_same(float a[3][N_PROBE], float b[3][N_PROBE], float eps) {
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < N_PROBE; i++) T_NEAR(a[k][i], b[k][i], eps);
}

static void test_anchor_preserves_heights_far_from_origin(void) {
    Sea s; sea_init(&s, &wc.preset[WEATHER_CHOPPY], 9);
    float t = 37.3f;
    float before[3][N_PROBE], after[3][N_PROBE];
    probe(&s, AX, AY, t, before);
    sea_anchor(&s, AX, AY, t);
    T_ASSERT(s.anchored);
    probe(&s, AX, AY, t, after);
    expect_same(before, after, 2e-3f);
}

static void test_move_anchor_preserves_heights(void) {
    Sea s; sea_init(&s, &wc.preset[WEATHER_CHOPPY], 9);
    float t = 37.3f;
    float before[3][N_PROBE], after[3][N_PROBE];
    probe(&s, AX, AY, t, before);
    sea_anchor(&s, AX, AY, t);
    sea_move_anchor(&s, AX + 37.0, AY - 12.0);
    T_NEAR(s.ax, AX + 37.0, 1e-9);
    probe(&s, AX, AY, t, after);
    expect_same(before, after, 2e-3f);
}

static void test_advance_matches_world_time(void) {
    Sea orig; sea_init(&orig, &wc.preset[WEATHER_CHOPPY], 9);
    Sea s = orig;
    float t = 37.3f;
    sea_anchor(&s, AX, AY, t);
    for (int i = 0; i < 300; i++) sea_advance(&s, 1.0f / 30.0f);
    float want[3][N_PROBE], got[3][N_PROBE];
    probe(&orig, AX, AY, t + 10.0f, want);
    probe(&s, AX, AY, 0.0f, got);                /* t is ignored while anchored */
    expect_same(want, got, 2e-3f);
}

static void test_unanchor_roundtrip(void) {
    Sea orig; sea_init(&orig, &wc.preset[WEATHER_CHOPPY], 9);
    Sea s = orig;
    float t = 37.3f;
    sea_anchor(&s, AX, AY, t);
    for (int i = 0; i < 300; i++) sea_advance(&s, 1.0f / 30.0f);
    sea_unanchor(&s, &orig, t + 10.0f);
    T_ASSERT(!s.anchored);
    T_ASSERT(s.n == orig.n);
    for (int i = 0; i < s.n; i++) {
        T_ASSERT(s.c[i].m == orig.c[i].m && s.c[i].n == orig.c[i].n);
        T_ASSERT(s.c[i].omega == orig.c[i].omega && s.c[i].amp == orig.c[i].amp);
    }
    float want[3][N_PROBE], got[3][N_PROBE];
    probe(&orig, AX, AY, t + 10.0f, want);
    probe(&s, AX, AY, t + 10.0f, got);
    expect_same(want, got, 2e-3f);
}

static float smooth(float x) { return x * x * (3.0f - 2.0f * x); }

static void test_morph_continuous_far_from_origin(void) {
    Sea a, b, s;
    sea_init(&a, &wc.preset[WEATHER_CHOPPY], 9);
    sea_init(&b, &wc.preset[WEATHER_ROLLERS], 9);
    s = a;
    float t = 12.0f, dt = 1.0f / 30.0f;
    double x = 5000.0, y = 5000.0, hx = 0.6, hy = 0.8;   /* heading, unit */
    sea_anchor(&s, x, y, t);
    float h0 = sea_height(&s, (float)x, (float)y, 0), f0 = sea_height(&s, (float)(x + 40 * hx), (float)(y + 40 * hy), 0);
    float worst = 0.0f;
    for (int k = 1; k <= 300; k++) {
        sea_morph(&s, &a, &b, smooth(k / 300.0f));
        x += 2.0 * dt * hx; y += 2.0 * dt * hy;
        sea_move_anchor(&s, x, y);
        sea_advance(&s, dt);
        float h1 = sea_height(&s, (float)x, (float)y, 0);
        float f1 = sea_height(&s, (float)(x + 40 * hx), (float)(y + 40 * hy), 0);
        worst = fmaxf(worst, fmaxf(fabsf(h1 - h0), fabsf(f1 - f0)));
        h0 = h1; f0 = f1;
    }
    T_ASSERT(worst <= 0.15f);
    float tw = t + 300 * dt;
    sea_unanchor(&s, &b, tw);
    T_ASSERT(!s.anchored);
    T_NEAR(sea_height(&s, (float)x, (float)y, tw), h0, 2e-3f);
    T_NEAR(sea_height(&s, (float)(x + 40 * hx), (float)(y + 40 * hy), tw), f0, 2e-3f);
}

static void test_morph_same_length_scales_height(void) {
    Sea a, b, s;
    sea_init(&a, &wc.preset[WEATHER_CHOPPY], 9);
    b = a;
    for (int i = 0; i < b.n; i++) b.c[i].amp *= 2.0f;
    float t = 37.3f;
    Sea ref = a;
    sea_anchor(&ref, AX, AY, t);
    s = ref;
    sea_morph(&s, &a, &b, 0.5f);
    for (int i = 0; i < N_PROBE; i++) {
        float dx = -50.0f + 10.0f * i, dy = 50.0f - 7.3f * i;
        float x = (float)(AX + dx), y = (float)(AY + dy);
        T_NEAR(sea_height(&s, x, y, t), 1.5f * sea_height(&ref, x, y, t), 2e-3f);
    }
}

static void test_dominant_anchored_matches_world(void) {
    Sea s; sea_init(&s, &wc.preset[WEATHER_CHOPPY], 9);
    float nx0, ny0, wl0, nx1, ny1, wl1;
    T_ASSERT(sea_dominant(&s, &nx0, &ny0, &wl0));
    sea_anchor(&s, AX, AY, 37.3f);
    T_ASSERT(sea_dominant(&s, &nx1, &ny1, &wl1));
    T_NEAR(nx1, nx0, 1e-5f);
    T_NEAR(ny1, ny0, 1e-5f);
    T_NEAR(wl1, wl0, 1e-3f);
    Sea e; memset(&e, 0, sizeof e);
    T_ASSERT(!sea_dominant(&e, &nx0, &ny0, &wl0));
}

int main(void) {
    setup();
    T_RUN(test_height_amplitude_matches_preset);
    T_RUN(test_view_matches_height);
    T_RUN(test_periodic_in_time);
    T_RUN(test_flat_sea_gives_zero_forces);
    T_RUN(test_storm_drags_more_than_sunny);
    T_RUN(test_slam_only_on_reentry_above_speed);
    T_RUN(test_surf_down_face_positive_beam_on_zero);
    T_RUN(test_far_from_origin_is_finite_and_equivalent);
    T_RUN(test_bad_input_stays_finite);
    T_RUN(test_cat_sea_heights_under_each_hull);
    T_RUN(test_monohull_wave_forces_unchanged);
    T_RUN(test_anchor_preserves_heights_far_from_origin);
    T_RUN(test_move_anchor_preserves_heights);
    T_RUN(test_advance_matches_world_time);
    T_RUN(test_unanchor_roundtrip);
    T_RUN(test_morph_continuous_far_from_origin);
    T_RUN(test_morph_same_length_scales_height);
    T_RUN(test_dominant_anchored_matches_world);
    T_DONE();
}
