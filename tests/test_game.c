#include "t.h"
#include <math.h>
#include <string.h>
#include "game.h"
#include "autopilot.h"

static Game g;
static BoatCfg bc;
static WeatherCfg wcfg;
static WorldCfg wc;

static void fresh(uint32_t seed) {
    boatcfg_default(&bc);
    weather_default(&wcfg);
    world_default(&wc);
    game_init(&g, &bc, &wcfg, &wc, WEATHER_SUNNY, seed);
}

static int finite_state(const Game *x) {
    return isfinite(x->b.x) && isfinite(x->b.y) && isfinite(x->b.u) && isfinite(x->b.v) &&
           isfinite(x->b.r) && isfinite(x->b.heel) && isfinite(x->b.heading);
}

static void test_huge_dt_is_bounded_and_finite(void) {
    fresh(1);
    game_update(&g, 0, 0, 0, 5.0f);
    T_ASSERT(g.substeps >= 1 && g.substeps <= 12);
    T_ASSERT(finite_state(&g));
    game_update(&g, 0, 0, 1e30f, NAN);
    T_ASSERT(g.substeps >= 1 && g.substeps <= 12);
    T_ASSERT(finite_state(&g));
    game_update(&g, 0, 0, NAN, -3.0f);
    T_ASSERT(finite_state(&g));
}

static void test_first_frame_zero_dt_is_safe(void) {
    fresh(1);
    game_update(&g, 0, 0, 0, 0.0f);
    T_ASSERT(finite_state(&g));
    T_ASSERT(g.run == RUN_SAILING);
}

static void test_capsize_sets_state_and_freezes_controls(void) {
    fresh(1);
    g.b.heel = 100.0f * (float)M_PI / 180.0f;
    game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(g.run == RUN_CAPSIZED);
    Mode m = g.ctl.mode;
    float wheel_t = g.in.wheel_target;
    game_update(&g, BTN_RIGHT | BTN_UP, 0, 90.0f, 1.0f / 30);
    T_ASSERT(g.run == RUN_CAPSIZED);
    T_ASSERT(g.ctl.mode == m);
    T_NEAR(g.in.wheel_target, wheel_t, 1e-9f);
}

static void test_wreck_sets_state(void) {
    fresh(3);
    wc.rock_density = 1.0f;
    game_init(&g, &bc, &wcfg, &wc, WEATHER_SUNNY, 3);
    Rock r[8];
    int n = rocks_near(&wc, 3, 0, 0, 200, r, 8);
    T_ASSERT(n > 0);
    /* just inside the contact ring on the rock's -x side, heading +x at wreck speed */
    g.b.x = r[0].x - r[0].r - 0.5 * bc.hull.beam + 0.05;
    g.b.y = r[0].y;
    g.b.heading = 0.0f;
    g.b.u = 3.2f;
    g.b.v = 0.0f;
    game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(g.run == RUN_WRECKED);
    T_ASSERT(g.last_hit == HIT_WRECK);
    float wheel_t = g.in.wheel_target;
    game_update(&g, BTN_UP, 0, 90.0f, 1.0f / 30);
    T_ASSERT(g.run == RUN_WRECKED);
    T_NEAR(g.in.wheel_target, wheel_t, 1e-9f);
    T_ASSERT(finite_state(&g));
}

static void test_restart_restores_start_in_one_call(void) {
    fresh(5);
    Game start = g;
    for (int i = 0; i < 90; i++) game_update(&g, i == 3 ? BTN_UP : 0, 0, 5.0f, 1.0f / 30);
    g.run = RUN_CAPSIZED;
    g.b.heel = 2.0f;
    game_restart(&g, 5);
    T_ASSERT(g.run == RUN_SAILING);
    T_ASSERT(g.b.x == start.b.x && g.b.y == start.b.y);
    T_ASSERT(g.b.u == start.b.u && g.b.heel == start.b.heel);
    T_ASSERT(g.t == 0.0);
    T_ASSERT(g.ctl.mode == start.ctl.mode);
    T_ASSERT(g.last_hit == HIT_NONE);
}

static void test_same_seed_after_restart_same_rocks(void) {
    fresh(9);
    game_update(&g, 0, 0, 0, 1.0f / 30);
    Rock a[ROCKS_MAX];
    int na = g.nrocks;
    memcpy(a, g.rocks, sizeof a);
    game_restart(&g, 9);
    game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(g.nrocks == na);
    T_ASSERT(memcmp(a, g.rocks, sizeof(Rock) * (size_t)na) == 0);
    game_restart(&g, 10);
    game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(g.seed == 10);
}

static void test_set_preset_changes_weather(void) {
    fresh(1);
    game_set_preset(&g, WEATHER_STORM);
    T_ASSERT(g.preset == WEATHER_STORM);
    game_set_preset(&g, 99);
    T_ASSERT(g.preset >= 0 && g.preset < WEATHER_PRESETS);
    game_set_preset(&g, -4);
    T_ASSERT(g.preset >= 0 && g.preset < WEATHER_PRESETS);
    for (int i = 0; i < 30; i++) game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(finite_state(&g));
}

static void test_calm_start_sails_on(void) {
    fresh(2);
    for (int i = 0; i < 300; i++) game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(finite_state(&g));
    T_ASSERT(g.t > 9.9 && g.t < 10.1);
}

static void step(uint32_t pressed, uint32_t held, int frames) {
    for (int i = 0; i < frames; i++) {
        game_update(&g, i == 0 ? pressed : 0, held, 0, 1.0f / 30);
    }
}
static void engage(void) {
    step(BTN_A, BTN_A, 25);       /* > 0.6 s */
    step(0, 0, 1);
}

static void test_autopilot_holds_heading(void) {
    fresh(1);
    engage();
    T_ASSERT(g.ap.on);
    g.ap.target = g.b.heading + deg2rad(60);
    step(0, 0, 600);
    T_ASSERT(g.run == RUN_SAILING);
    T_ASSERT(fabsf(wrap_pi(g.b.heading - g.ap.target)) < deg2rad(5));
}

static void test_autopilot_overrides_crank_wheel(void) {
    fresh(1);
    engage();
    game_update(&g, BTN_UP, 0, 90.0f, 1.0f / 30);
    T_NEAR(g.in.wheel_target, autopilot_wheel(&g.ap, &g.boat.controls, g.b.heading, g.b.r), 0.2);
    T_ASSERT(fabsf(g.in.wheel_target) < 0.9f);
}

static void test_held_a_without_press_does_not_engage(void) {
    fresh(1);
    for (int i = 0; i < 90; i++) game_update(&g, 0, BTN_A, 0, 1.0f / 30);
    T_ASSERT(!g.ap.on);
}

static void test_autopilot_cleared_on_restart(void) {
    fresh(1);
    engage();
    T_ASSERT(g.ap.on);
    game_restart(&g, 2);
    T_ASSERT(!g.ap.on);
}

static void test_autopilot_frozen_when_capsized(void) {
    fresh(1);
    engage();
    g.run = RUN_CAPSIZED;
    float w = g.in.wheel_target;
    for (int i = 0; i < 30; i++) game_update(&g, i == 0 ? BTN_A : 0, BTN_A, 0, 1.0f / 30);
    T_ASSERT(g.in.wheel_target == w);
    T_ASSERT(g.ap.on);
}

int main(void) {
    T_RUN(test_huge_dt_is_bounded_and_finite);
    T_RUN(test_first_frame_zero_dt_is_safe);
    T_RUN(test_capsize_sets_state_and_freezes_controls);
    T_RUN(test_wreck_sets_state);
    T_RUN(test_restart_restores_start_in_one_call);
    T_RUN(test_same_seed_after_restart_same_rocks);
    T_RUN(test_set_preset_changes_weather);
    T_RUN(test_calm_start_sails_on);
    T_RUN(test_autopilot_holds_heading);
    T_RUN(test_autopilot_overrides_crank_wheel);
    T_RUN(test_held_a_without_press_does_not_engage);
    T_RUN(test_autopilot_cleared_on_restart);
    T_RUN(test_autopilot_frozen_when_capsized);
    T_DONE();
}
