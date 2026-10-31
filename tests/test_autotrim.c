#include "t.h"
#include <math.h>
#include "autotrim.h"

#define DT 0.1f

static BoatCfg cfg;
static BoatState b;
static BoatInputs in;
static Autotrim at;

static void fresh(void) {
    boatcfg_default(&cfg);
    boat_init(&b, &cfg);
    boat_inputs_init(&in, &cfg);
    autotrim_init(&at);
}
static bool frame(uint32_t pressed, uint32_t held) {
    return autotrim_buttons(&at, &cfg.controls, pressed, held, DT);
}
static bool hold_b(int frames) {
    bool tap = frame(BTN_B, BTN_B);
    for (int i = 1; i < frames; i++) tap |= frame(0, BTN_B);
    return tap | frame(0, 0);
}
static void engage(void) { hold_b(10); }

static void test_hold_toggles(void) {
    fresh();
    T_ASSERT(!hold_b(10));
    T_ASSERT(at.on);
    hold_b(10);
    T_ASSERT(!at.on);
}
static void test_tap_is_reported_and_does_not_toggle(void) {
    fresh();
    T_ASSERT(frame(BTN_B, BTN_B) == false);
    T_ASSERT(frame(0, 0) == true);
    T_ASSERT(!at.on);
}
static void test_long_hold_fires_once_and_no_tap(void) {
    fresh();
    T_ASSERT(!hold_b(40));
    T_ASSERT(at.on);
}
static void test_best_trim_is_sensible(void) {
    fresh();
    const SailCfg *main_s = &cfg.sails[0];
    float close = autotrim_best_trim(main_s, deg2rad(40), 8, 0);
    float beam = autotrim_best_trim(main_s, deg2rad(90), 8, 0);
    float run = autotrim_best_trim(main_s, deg2rad(150), 8, 0);
    T_ASSERT(close > 0 && beam > close && run > beam);
    /* mirrored wind gives the same trim */
    T_NEAR(autotrim_best_trim(main_s, deg2rad(-90), 8, 0), beam, 1e-5);
}
static void test_update_sets_trim_for_wind(void) {
    fresh();
    engage();
    b.awa = deg2rad(60); b.aws = 8;
    autotrim_update(&at, &cfg, &b, &in, DT);
    T_NEAR(in.trim_target[0], autotrim_best_trim(&cfg.sails[0], deg2rad(60), 8, 0), 1e-5);
}
static void test_off_changes_nothing(void) {
    fresh();
    float t = in.trim_target[0];
    b.awa = deg2rad(60); b.aws = 8;
    autotrim_update(&at, &cfg, &b, &in, DT);
    T_ASSERT(in.trim_target[0] == t);
}
static void test_gust_is_smoothed(void) {
    fresh();
    engage();
    b.awa = deg2rad(60); b.aws = 8;
    autotrim_update(&at, &cfg, &b, &in, DT);
    float before = in.trim_target[0];
    b.awa = deg2rad(110);                 /* a sudden shift */
    autotrim_update(&at, &cfg, &b, &in, DT);
    float after = in.trim_target[0];
    float full = autotrim_best_trim(&cfg.sails[0], deg2rad(110), 8, 0);
    T_ASSERT(fabsf(after - before) < 0.2f * fabsf(full - before));
    for (int i = 0; i < 300; i++) autotrim_update(&at, &cfg, &b, &in, DT);   /* then it settles there */
    T_NEAR(in.trim_target[0], full, 0.03f);
}
static void test_stowed_sail_stays_stowed(void) {
    fresh();
    engage();
    int sp = cfg.sail_count - 1;
    T_ASSERT(cfg.sails[sp].shape == 2 || cfg.sail_count > 0);
    in.hoist_target[sp] = 0;
    float t = in.trim_target[sp];
    b.awa = deg2rad(150); b.aws = 8;
    for (int i = 0; i < 50; i++) autotrim_update(&at, &cfg, &b, &in, DT);
    if (cfg.sails[sp].shape == 2) {
        T_ASSERT(in.hoist_target[sp] == 0);
        T_ASSERT(in.trim_target[sp] == t);
    }
}
static void test_heel_reefs_then_recovers(void) {
    fresh();
    engage();
    b.awa = deg2rad(60); b.aws = 12; b.heel = deg2rad(45);
    for (int i = 0; i < 400; i++) autotrim_update(&at, &cfg, &b, &in, DT);
    T_NEAR(in.hoist_target[0], cfg.controls.autotrim_hoist_min, 1e-5);
    b.heel = 0;
    for (int i = 0; i < 1000; i++) autotrim_update(&at, &cfg, &b, &in, DT);
    T_NEAR(in.hoist_target[0], 1.0f, 1e-5);
}
static void test_bad_inputs_ignored(void) {
    fresh();
    engage();
    b.awa = NAN; b.aws = 8;
    autotrim_update(&at, &cfg, &b, &in, DT);
    b.awa = 1; b.aws = 8;
    autotrim_update(&at, &cfg, &b, &in, NAN);
    T_ASSERT(isfinite(in.trim_target[0]) && isfinite(at.aws));
}

static void test_longship_squares_up_on_a_run(void) {
    static char buf[16384];
    FILE *f = fopen("../boats/longship.json", "rb");
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    T_ASSERT(n > 0 && boatcfg_load(&cfg, buf, n, NULL, 0));
    const SailCfg *sq = &cfg.sails[0];
    T_NEAR(autotrim_best_trim(sq, deg2rad(180), 6, 0), deg2rad(90), deg2rad(1));
    float reach = autotrim_best_trim(sq, deg2rad(100), 6, 0);
    T_ASSERT(reach >= 0);
    T_ASSERT(reach <= deg2rad(90));
}

int main(void) {
    T_RUN(test_hold_toggles);
    T_RUN(test_tap_is_reported_and_does_not_toggle);
    T_RUN(test_long_hold_fires_once_and_no_tap);
    T_RUN(test_best_trim_is_sensible);
    T_RUN(test_update_sets_trim_for_wind);
    T_RUN(test_off_changes_nothing);
    T_RUN(test_gust_is_smoothed);
    T_RUN(test_stowed_sail_stays_stowed);
    T_RUN(test_heel_reefs_then_recovers);
    T_RUN(test_bad_inputs_ignored);
    T_RUN(test_longship_squares_up_on_a_run);
    return T_DONE();
}
