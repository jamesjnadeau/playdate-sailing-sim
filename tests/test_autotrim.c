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
static void engage(void) { autotrim_toggle(&at); }

static void test_toggle_flips_on_and_off(void) {
    fresh();
    autotrim_toggle(&at);
    T_ASSERT(at.on);
    autotrim_toggle(&at);
    T_ASSERT(!at.on);
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

static void make_cat(void) {
    fresh();
    cfg.hull.hulls = 2;
    cfg.hull.hull_spacing = 0.8f * cfg.hull.beam;
    for (int i = 0; i < cfg.sail_count; i++) cfg.sails[i].reef_levels = 0;
    boat_init(&b, &cfg);
    boat_inputs_init(&in, &cfg);
    autotrim_init(&at);
    engage();
    b.awa = deg2rad(60); b.aws = 12;
}
static void test_cat_eases_instead_of_reefing(void) {
    make_cat();
    b.heel = 2 * deg2rad(cfg.controls.autotrim_heel_deg);
    for (int i = 0; i < 30; i++) autotrim_update(&at, &cfg, &b, &in, DT);   /* 3 s */
    T_ASSERT(at.ease > 0);
    for (int i = 0; i < cfg.sail_count; i++) {
        if (in.hoist_target[i] <= 0.01f) continue;
        T_NEAR(in.hoist_target[i], 1.0f, 1e-6);
        float best = autotrim_best_trim(&cfg.sails[i], b.awa, b.aws, at.heel);
        float mx = deg2rad(cfg.sails[i].trim_max_deg);
        T_NEAR(in.trim_target[i], fminf(best + at.ease, mx), 1e-5);
        /* ease after 3 s = 60 deg/s * 0.5 * 3 s = 90 deg, so it is > 5 deg past best unless trim_max clamps it */
        T_ASSERT(in.trim_target[i] > best + deg2rad(5) || in.trim_target[i] >= mx - 1e-5f);
    }
}
static void test_cat_sheets_back_in(void) {
    make_cat();
    b.heel = 2 * deg2rad(cfg.controls.autotrim_heel_deg);
    for (int i = 0; i < 30; i++) autotrim_update(&at, &cfg, &b, &in, DT);
    b.heel = 0;
    /* the heel filter (4 s) keeps filtered heel over the limit ~2.8 s and above the release band ~4.2 s,
       so ease keeps growing then unwinds at 30 deg/s: 5 s is not enough, 15 s is */
    for (int i = 0; i < 150; i++) autotrim_update(&at, &cfg, &b, &in, DT);
    T_NEAR(at.ease, 0, 1e-6);
    for (int i = 0; i < cfg.sail_count; i++) {
        if (in.hoist_target[i] <= 0.01f) continue;
        float best = autotrim_best_trim(&cfg.sails[i], b.awa, b.aws, at.heel);
        T_NEAR(in.trim_target[i], best, 1e-3);
    }
}
static void test_monohull_without_reefs_still_reefs(void) {
    fresh();
    for (int i = 0; i < cfg.sail_count; i++) cfg.sails[i].reef_levels = 0;
    engage();
    b.awa = deg2rad(60); b.aws = 12; b.heel = 2 * deg2rad(cfg.controls.autotrim_heel_deg);
    for (int i = 0; i < 30; i++) autotrim_update(&at, &cfg, &b, &in, DT);
    T_ASSERT(in.hoist_target[0] < 1.0f);
    T_NEAR(at.ease, 0, 1e-9);
}
static void test_cat_ease_leaves_stowed_sail(void) {
    make_cat();
    int sp = cfg.sail_count - 1;
    in.hoist_target[sp] = 0;
    float t = in.trim_target[sp];
    b.heel = 2 * deg2rad(cfg.controls.autotrim_heel_deg);
    for (int i = 0; i < 30; i++) autotrim_update(&at, &cfg, &b, &in, DT);
    T_ASSERT(in.hoist_target[sp] == 0);
    T_ASSERT(in.trim_target[sp] == t);
}

static void test_longship_squares_up_on_a_run(void) {
    static char buf[16384];
    FILE *f = fopen("../boats/disabled/longship.json", "rb");
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
    T_RUN(test_toggle_flips_on_and_off);
    T_RUN(test_best_trim_is_sensible);
    T_RUN(test_update_sets_trim_for_wind);
    T_RUN(test_off_changes_nothing);
    T_RUN(test_gust_is_smoothed);
    T_RUN(test_stowed_sail_stays_stowed);
    T_RUN(test_heel_reefs_then_recovers);
    T_RUN(test_bad_inputs_ignored);
    T_RUN(test_longship_squares_up_on_a_run);
    T_RUN(test_cat_ease_leaves_stowed_sail);
    T_RUN(test_monohull_without_reefs_still_reefs);
    T_RUN(test_cat_sheets_back_in);
    T_RUN(test_cat_eases_instead_of_reefing);
    return T_DONE();
}
