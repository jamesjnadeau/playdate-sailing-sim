#include "t.h"
#include <math.h>
#include <string.h>
#include "seasound.h"

static Game g;
static BoatCfg bc;
static WeatherCfg wcfg;
static WorldCfg wc;
static SeaSound ss;
static int16_t L[4096], R[4096];

static void fresh(int preset, uint32_t seed) {
    boatcfg_default(&bc);
    weather_default(&wcfg);
    world_default(&wc);
    game_init(&g, &bc, &wcfg, &wc, preset, seed);
    seasound_init(&ss);
}

/* rms of a rendered block, and the largest sample */
static float rms_of(const int16_t *x, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += (double)x[i] * x[i];
    return (float)(sqrt(s / n) / 32768.0);
}
static int peak_of(const int16_t *x, int n) {
    int m = 0;
    for (int i = 0; i < n; i++) if (abs(x[i]) > m) m = abs(x[i]);
    return m;
}

/* Settles the smoothers on fixed params, then measures. */
static float level_for(const SoundParams *p, int16_t *l, int16_t *r) {
    seasound_init(&ss);
    ss.p = *p;
    for (int i = 0; i < 20; i++) seasound_render(&ss, l, r, 4096);
    seasound_render(&ss, l, r, 4096);
    return rms_of(l, 4096);
}

static void test_silent_when_params_zero(void) {
    seasound_init(&ss);
    for (int i = 0; i < 4; i++) seasound_render(&ss, L, R, 4096);
    T_ASSERT(peak_of(L, 4096) < 40 && peak_of(R, 4096) < 40);
}

static void test_each_voice_is_audible_but_not_clipping(void) {
    SoundParams p;
    float *fields[] = { &p.wash, &p.swell, &p.gurgle, &p.wind, &p.flutter };
    for (int v = 0; v < 5; v++) {
        memset(&p, 0, sizeof p);
        p.bright = 0.6f; p.whistle_hz = 700.0f; p.pan = 0.0f;
        *fields[v] = 1.0f;
        float r = level_for(&p, L, R);
        printf("voice %d rms %.4f peak %d\n", v, r, peak_of(L, 4096));
        T_ASSERT(r > 0.01f && r < 0.35f);
        T_ASSERT(peak_of(L, 4096) < 30000);
    }
}

static void test_wash_gets_brighter_with_bright(void) {
    SoundParams p;
    memset(&p, 0, sizeof p);
    p.wash = 1.0f;
    p.bright = 0.1f;
    level_for(&p, L, R);
    long lo = 0;
    for (int i = 1; i < 4096; i++) lo += abs(L[i] - L[i - 1]);
    p.bright = 1.0f;
    level_for(&p, L, R);
    long hi = 0;
    for (int i = 1; i < 4096; i++) hi += abs(L[i] - L[i - 1]);
    T_ASSERT(hi > 2 * lo);   /* more sample-to-sample movement means more treble */
}

static void test_gurgle_sits_on_the_heeled_side(void) {
    SoundParams p;
    memset(&p, 0, sizeof p);
    p.gurgle = 1.0f;
    p.pan = 0.7f;
    level_for(&p, L, R);
    T_ASSERT(rms_of(R, 4096) > 3 * rms_of(L, 4096));
    p.pan = -0.7f;
    level_for(&p, L, R);
    T_ASSERT(rms_of(L, 4096) > 3 * rms_of(R, 4096));
}

static void test_slap_is_a_decaying_burst(void) {
    SoundParams p;
    memset(&p, 0, sizeof p);
    seasound_init(&ss);
    ss.p = p;
    seasound_render(&ss, L, R, 4096);
    ss.slap_ev = 1.0f;
    seasound_render(&ss, L, R, 4096);
    float first = rms_of(L, 2048);
    for (int i = 0; i < 10; i++) seasound_render(&ss, L, R, 4096);
    float later = rms_of(L, 4096);
    T_ASSERT(first > 0.03f);
    T_ASSERT(later < 0.1f * first);
    T_ASSERT(ss.slap_ev == 0.0f);
}

static void test_level_zero_silences_and_returns_zero(void) {
    seasound_init(&ss);
    ss.p.wash = 1.0f;
    seasound_set_level(&ss, 0);
    int got = 1;
    for (int i = 0; i < 40 && got; i++) got = seasound_render(&ss, L, R, 4096);
    T_ASSERT(got == 0);
    seasound_set_level(&ss, 3);
    T_ASSERT(seasound_render(&ss, L, R, 4096) == 1);
}

static void test_levels_are_ordered(void) {
    SoundParams p;
    memset(&p, 0, sizeof p);
    p.wash = 1.0f; p.bright = 0.5f;
    float prev = 0;
    for (int lv = 1; lv < SEASOUND_LEVELS; lv++) {
        seasound_init(&ss);
        seasound_set_level(&ss, lv);
        ss.p = p;
        for (int i = 0; i < 20; i++) seasound_render(&ss, L, R, 4096);
        float r = rms_of(L, 4096);
        T_ASSERT(r > prev);
        prev = r;
    }
}

static void test_pause_fades_out_and_back(void) {
    seasound_init(&ss);
    ss.p.wash = 1.0f;
    seasound_set_active(&ss, 0);
    int got = 1;
    for (int i = 0; i < 40 && got; i++) got = seasound_render(&ss, L, R, 4096);
    T_ASSERT(got == 0);
    seasound_set_active(&ss, 1);
    T_ASSERT(seasound_render(&ss, L, R, 4096) == 1);
}

static void test_speed_drives_wash_and_swell_follows_sea(void) {
    fresh(WEATHER_CHOPPY, 3);
    g.b.u = 0.0f; g.b.v = 0.0f;
    seasound_update(&ss, &g, 1.0f / 30);
    float still = ss.p.wash;
    g.b.u = g.boat.hull.hull_speed;
    seasound_update(&ss, &g, 1.0f / 30);
    T_ASSERT(still < 0.01f);
    T_ASSERT(ss.p.wash > 0.9f);

    fresh(WEATHER_SUNNY, 3);
    for (int i = 0; i < 4; i++) { game_update(&g, 0, 0, 0, 1.0f / 30); seasound_update(&ss, &g, 1.0f / 30); }
    float calm = ss.p.swell;
    fresh(WEATHER_HURRICANE, 3);
    for (int i = 0; i < 4; i++) { game_update(&g, 0, 0, 0, 1.0f / 30); seasound_update(&ss, &g, 1.0f / 30); }
    T_ASSERT(ss.p.swell > calm);
}

static void test_heel_and_wind_and_luff_map(void) {
    fresh(WEATHER_CHOPPY, 3);
    g.b.heel = 0.5f; g.b.u = 3.0f; g.b.aws = 12.0f;
    g.b.luff[0] = 1.0f; g.b.hoists[0] = 1.0f;
    seasound_update(&ss, &g, 1.0f / 30);
    T_ASSERT(ss.p.gurgle > 0.3f && ss.p.pan > 0.0f);
    T_ASSERT(ss.p.wind > 0.3f && ss.p.whistle_hz > 600.0f);
    T_ASSERT(ss.p.flutter > 0.5f);
    g.b.heel = -0.5f;
    seasound_update(&ss, &g, 1.0f / 30);
    T_ASSERT(ss.p.pan < 0.0f);
    g.b.heel = 0.0f; g.b.luff[0] = 1.0f; g.b.hoists[0] = 0.0f;   /* a lowered sail does not flutter */
    seasound_update(&ss, &g, 1.0f / 30);
    T_ASSERT(ss.p.flutter == 0.0f && ss.p.gurgle == 0.0f);
}

static void test_rock_strike_thumps_once(void) {
    fresh(WEATHER_SUNNY, 3);
    g.last_hit = HIT_STRIKE;
    seasound_update(&ss, &g, 1.0f / 30);
    T_ASSERT(ss.thump_ev > 0.7f);
    ss.thump_ev = 0.0f;
    seasound_update(&ss, &g, 1.0f / 30);   /* still touching: not a second thump */
    T_ASSERT(ss.thump_ev == 0.0f);
}

static void test_bad_state_stays_finite(void) {
    fresh(WEATHER_CHOPPY, 3);
    g.b.u = NAN; g.b.v = INFINITY; g.b.heel = NAN; g.b.aws = NAN; g.b.luff[0] = NAN;
    g.wf.heave = NAN; g.wf.pitch = INFINITY;
    seasound_update(&ss, &g, NAN);
    seasound_update(&ss, &g, -1.0f);
    const float *f = (const float *)&ss.p;
    for (int i = 0; i < (int)(sizeof ss.p / sizeof *f); i++) T_ASSERT(isfinite(f[i]));
    T_ASSERT(seasound_render(&ss, L, R, 100) == 1);   /* n not a multiple of the chunk */
}

/* Sails through a rough sea under way: bow slaps fire at a sensible, not constant, rate. */
static void test_slaps_in_a_seaway(void) {
    fresh(WEATHER_STORM, 11);
    int slaps = 0, frames = 30 * 60;
    for (int i = 0; i < frames; i++) {
        g.b.u = 3.0f; g.b.v = 0.0f;
        game_update(&g, 0, 0, 0, 1.0f / 30);
        seasound_update(&ss, &g, 1.0f / 30);
        if (ss.slap_ev > 0.0f) { slaps++; ss.slap_ev = 0.0f; }
    }
    printf("slaps in 60 s of storm: %d\n", slaps);
    T_ASSERT(slaps >= 3 && slaps <= 60 * 2);
}

static void test_no_slaps_when_stopped_or_flat(void) {
    fresh(WEATHER_STORM, 11);
    int slaps = 0;
    for (int i = 0; i < 30 * 60; i++) {
        g.b.u = 0.0f; g.b.v = 0.0f;
        game_update(&g, 0, 0, 0, 1.0f / 30);
        seasound_update(&ss, &g, 1.0f / 30);
        if (ss.slap_ev > 0.0f) { slaps++; ss.slap_ev = 0.0f; }
    }
    T_ASSERT(slaps == 0);
}

int main(void) {
    T_RUN(test_silent_when_params_zero);
    T_RUN(test_each_voice_is_audible_but_not_clipping);
    T_RUN(test_wash_gets_brighter_with_bright);
    T_RUN(test_gurgle_sits_on_the_heeled_side);
    T_RUN(test_slap_is_a_decaying_burst);
    T_RUN(test_level_zero_silences_and_returns_zero);
    T_RUN(test_levels_are_ordered);
    T_RUN(test_pause_fades_out_and_back);
    T_RUN(test_speed_drives_wash_and_swell_follows_sea);
    T_RUN(test_heel_and_wind_and_luff_map);
    T_RUN(test_rock_strike_thumps_once);
    T_RUN(test_bad_state_stays_finite);
    T_RUN(test_slaps_in_a_seaway);
    T_RUN(test_no_slaps_when_stopped_or_flat);
    return T_DONE();
}
