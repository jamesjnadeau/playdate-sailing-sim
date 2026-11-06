#include "t.h"
#include <math.h>
#include <string.h>
#include "game.h"
#include "autopilot.h"
#include "race.h"

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

__attribute__((unused)) static void test_wreck_sets_state(void) {
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

__attribute__((unused)) static void test_same_seed_after_restart_same_rocks(void) {
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

static void test_origin_recentres(void) {
    fresh(4);
    g.b.x = 12345.0; g.b.y = -9999.0;
    game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(fabs(g.b.x) <= 5.0 * SEA_WRAP && fabs(g.b.y) <= 5.0 * SEA_WRAP);
    T_NEAR(remainder(g.b.x - 12345.0, 5.0 * SEA_WRAP), 0.0, 5.0);
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
    step(BTN_A, BTN_A, 1);        /* a quick press toggles */
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

static void test_autopilot_crank_turns_target_in_wheel_mode(void) {
    fresh(1);
    engage();
    float t0 = g.ap.target;
    game_update(&g, BTN_UP, 0, 360.0f, 1.0f / 30);
    T_NEAR(wrap_pi(g.ap.target - t0), deg2rad(10), 1e-4);
}
static void test_autopilot_crank_in_trim_mode_leaves_target(void) {
    fresh(1);
    engage();
    float t0 = g.ap.target;
    game_update(&g, BTN_RIGHT, 0, 360.0f, 1.0f / 30);
    T_NEAR(g.ap.target, t0, 1e-6);
}
static void test_autopilot_overrides_crank_wheel(void) {
    fresh(1);
    engage();
    game_update(&g, BTN_UP, 0, 90.0f, 1.0f / 30);
    T_NEAR(g.in.wheel_target, autopilot_wheel(&g.ap, &g.boat.controls, g.b.heading, g.b.r, wrap_pi(g.wind.dir + (float)M_PI)), 0.2);
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

static void idle(float s) {
    for (int i = 0; i < (int)(s * 30.0f + 0.5f); i++) game_update(&g, 0, 0, 0, 1.0f / 30);
}

static void tap_b(void) {
    game_update(&g, BTN_B, BTN_B, 0, 1.0f / 30);
    game_update(&g, 0, 0, 0, 1.0f / 30);
}

static void hold_b(float s) {
    game_update(&g, BTN_B, BTN_B, 0, 1.0f / 30);
    for (int i = 1; i < (int)(s * 30.0f + 0.5f); i++) game_update(&g, 0, BTN_B, 0, 1.0f / 30);
    game_update(&g, 0, 0, 0, 1.0f / 30);
}

static void test_b_tap_toggles_autotrim(void) {
    fresh(1);
    tap_b();
    T_ASSERT(g.at.on && !g.en.on && !g.in.anchor);
    tap_b();
    T_ASSERT(!g.at.on);
    tap_b();
    game_restart(&g, 2);
    T_ASSERT(!g.at.on);
}

static void test_a_tap_toggles_autopilot_off_again(void) {
    fresh(1);
    engage();
    T_ASSERT(g.ap.on);
    engage();
    T_ASSERT(!g.ap.on);
}

static void test_b_hold_starts_engine_with_banner(void) {
    fresh(1);
    for (int i = 0; i < 20; i++) game_update(&g, i == 0 ? BTN_B : 0, BTN_B, 0, 1.0f / 30);
    T_ASSERT(!g.en.on);                    /* 0.67 s: not yet */
    for (int i = 0; i < 20; i++) game_update(&g, 0, BTN_B, 0, 1.0f / 30);
    T_ASSERT(g.en.on && g.in.engine);
    game_update(&g, 0, 0, 0, 1.0f / 30);   /* releasing after the hold is not a tap */
    T_ASSERT(g.en.on && !g.at.on);
}

static void test_b_tap_stops_engine_and_keeps_autotrim(void) {
    fresh(1);
    tap_b();                               /* autotrim on */
    T_ASSERT(g.at.on);
    hold_b(1.5f);
    T_ASSERT(g.en.on && g.at.on);
    tap_b();
    T_ASSERT(!g.en.on && !g.in.engine);
    T_ASSERT(g.at.on);                     /* left as it was */
    tap_b();
    T_ASSERT(!g.at.on);                    /* the next tap is autotrim again */
}

static void test_engine_is_slow_and_never_brakes(void) {
    fresh(1);
    wind_set_speed(&g.wind, &g.weather.preset[WEATHER_SUNNY], 0.5f);
    g.b.u = 0.0f;
    hold_b(1.5f);
    T_ASSERT(g.en.on);
    idle(20.0f);
    T_ASSERT(g.b.u > 0.3f && g.b.u < 0.7f);          /* about a knot (0.514 m/s) */
    g.b.u = 3.0f;
    float before = g.b.u;
    idle(0.2f);
    T_ASSERT(g.b.u <= before + 0.05f);
}

static void test_engine_pushes_along_the_rudder(void) {
    fresh(1);
    wind_set_speed(&g.wind, &g.weather.preset[WEATHER_SUNNY], 0.5f);
    hold_b(1.5f);
    g.in.wheel_target = 0.8f;
    float h0 = g.b.heading;
    for (int i = 0; i < 90; i++) game_update(&g, 0, 0, 0, 1.0f / 30);
    float turn1 = wrap_pi(g.b.heading - h0);
    fresh(1);
    wind_set_speed(&g.wind, &g.weather.preset[WEATHER_SUNNY], 0.5f);
    hold_b(1.5f);
    g.in.wheel_target = -0.8f;
    h0 = g.b.heading;
    for (int i = 0; i < 90; i++) game_update(&g, 0, 0, 0, 1.0f / 30);
    float turn2 = wrap_pi(g.b.heading - h0);
    T_ASSERT(turn1 * turn2 < 0.0f);                  /* opposite wheel, opposite turn */
}

static void test_engine_cleared_by_restart(void) {
    fresh(1);
    hold_b(1.5f);
    T_ASSERT(g.en.on);
    game_restart(&g, 2);
    T_ASSERT(!g.en.on && !g.in.engine);
}

static Track tr;

static void race_fresh(void) {
    fresh(1);
    track_default(&tr);
    game_set_track(&g, &tr);
}

static void test_set_track_places_boat_and_wind(void) {
    race_fresh();
    T_ASSERT(g.mode == GAME_RACE);
    T_NEAR(g.b.x, -60.0, 1e-4);
    T_NEAR(g.b.y, 30.0, 1e-4);
    T_NEAR(g.b.heading, deg2rad(-20), 1e-5);
    T_NEAR(wrap_pi(g.wind.dir0 + (float)M_PI), deg2rad(45), 1e-5);
    T_ASSERT(g.race.next == 0);
    T_ASSERT(g.race.t == 0.0);
}

static void test_race_advances_when_boat_rounds_b(void) {
    race_fresh();
    g.b.x = 410.0;
    g.b.y = 5.0;
    g.b.heading = deg2rad(-90);
    g.b.u = 5.0f;
    for (int i = 0; i < 60 && g.race.next == 0; i++) game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(g.race.next == 1);
}

/* Drops the boat `out` m along the first mark's gate ray and 1 m to one side of it, heading straight
   across it, and runs a few frames. side = +1/-1 picks which side of the ray it starts on. */
static bool crosses_first_gate(float out, int side) {
    race_fresh();
    const Mark *m = &g.track.marks[0];
    double gx = cos(m->gate), gy = sin(m->gate);
    g.b.x = m->x + gx * out - gy * side;
    g.b.y = m->y + gy * out + gx * side;
    g.b.heading = (float)wrap_pi(m->gate + side * -(float)M_PI / 2);
    g.b.u = 5.0f;
    for (int i = 0; i < 30 && g.race.next == 0; i++) game_update(&g, 0, 0, 0, 1.0f / 30);
    return g.race.next == 1;
}

static void test_gate_registers_through_game_update(void) {
    T_ASSERT(crosses_first_gate(10.0f, 1));     /* port rounding: +y side -> -y side for gate 0 */
    T_ASSERT(!crosses_first_gate(10.0f, -1));   /* the opposite way does not count */
    T_ASSERT(!crosses_first_gate(-10.0f, 1));   /* nor does the back extension of the ray */
}

static void test_restart_resets_race(void) {
    race_fresh();
    g.race.next = 1;
    g.race.t = 50.0;
    game_restart(&g, 9);
    T_ASSERT(g.mode == GAME_RACE);
    T_NEAR(g.b.x, -60.0, 1e-4);
    T_NEAR(g.b.y, 30.0, 1e-4);
    T_ASSERT(g.race.next == 0);
    T_ASSERT(g.race.t == 0.0);
}

static void test_track_wind_speed_overrides_preset(void) {
    fresh(1);
    track_default(&tr);
    tr.wind_speed = 9.0f;
    game_set_track(&g, &tr);
    T_NEAR(g.wind.base, 9.0, 1e-5);
    game_set_preset(&g, WEATHER_STORM);                  /* the track's speed survives a preset change */
    T_NEAR(g.wind.base, 9.0, 1e-5);
    track_default(&tr);                                  /* unset: the preset's own mean applies */
    game_set_track(&g, &tr);
    T_NEAR(g.wind.base, g.weather.preset[WEATHER_STORM].wind_base, 1e-5);
}

static void test_preset_change_keeps_track_wind(void) {
    race_fresh();
    game_set_preset(&g, WEATHER_STORM);
    T_NEAR(wrap_pi(g.wind.dir0 + (float)M_PI), deg2rad(45), 1e-5);
}

static void test_recentre_moves_marks_with_boat(void) {
    race_fresh();
    g.b.x = 5200.0;
    game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(g.b.x < 200.0);
    double mx, my;
    race_mark_local(&g.race, &g.track, 0, &mx, &my);
    T_NEAR(mx, 400.0 - 5120.0, 1e-3);
    T_ASSERT(g.race.next == 0);
}

static void test_free_range_unchanged(void) {
    fresh(1);
    T_ASSERT(g.mode == GAME_FREE);
    T_NEAR(g.b.x, 0.0, 1e-9);
    T_NEAR(g.b.y, 0.0, 1e-9);
    track_default(&tr);
    game_set_track(&g, &tr);
    game_set_track(&g, NULL);
    T_ASSERT(g.mode == GAME_FREE);
    T_NEAR(g.b.x, 0.0, 1e-9);
    T_NEAR(g.b.y, 0.0, 1e-9);
}

/* Follow-camera tests: hold DOWN long enough to toggle buoy-follow, then run frames. */
static void toggle_follow_cam(void) {
    game_update(&g, BTN_DOWN, BTN_DOWN, 0, 1.0f / 30);
    for (int i = 1; i < 30; i++) game_update(&g, 0, BTN_DOWN, 0, 1.0f / 30);
    game_update(&g, 0, 0, 0, 1.0f / 30);
}

static float bearing_rel(double mx, double my) {
    return wrap_pi((float)atan2(my - g.b.y, mx - g.b.x) - g.b.heading);
}

static float race_mark_rel(int i) {
    double mx, my;
    race_mark_local(&g.race, &g.track, i, &mx, &my);
    return bearing_rel(mx, my);
}

static void test_follow_camera_faces_next_mark_in_race(void) {
    race_fresh();
    toggle_follow_cam();
    T_ASSERT(g.ctl.camera_follow);
    idle(3.0f);
    T_NEAR(g.ctl.camera_yaw, race_mark_rel(0), 0.05);
    T_ASSERT(fabsf(g.ctl.camera_yaw) > 0.05f);
}

static void test_follow_camera_swings_to_next_mark_when_one_is_rounded(void) {
    race_fresh();
    toggle_follow_cam();
    idle(3.0f);
    float before = g.ctl.camera_yaw;
    g.race.next = 1;
    T_ASSERT(fabsf(wrap_pi(race_mark_rel(1) - before)) > 0.2f);
    game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(fabsf(wrap_pi(g.ctl.camera_yaw - before)) < 0.1f);
    idle(3.0f);
    T_NEAR(g.ctl.camera_yaw, race_mark_rel(1), 0.05);
}

static void test_follow_camera_in_journey_uses_next_buoy(void) {
    fresh(1);
    game_set_journey(&g);
    const JourneyBuoy *nb = journey_next(&g.journey);
    T_ASSERT(nb != NULL);
    toggle_follow_cam();
    idle(3.0f);
    nb = journey_next(&g.journey);
    T_ASSERT(nb != NULL);
    T_NEAR(g.ctl.camera_yaw, bearing_rel(nb->x, nb->y), 0.05);
}

static void test_follow_camera_free_mode_holds_yaw(void) {
    fresh(1);
    T_ASSERT(g.mode == GAME_FREE);
    toggle_follow_cam();
    T_ASSERT(g.ctl.camera_follow);
    T_ASSERT(!isfinite(g.ctl.buoy_rel));
    float y = g.ctl.camera_yaw;
    idle(2.0f);
    T_NEAR(g.ctl.camera_yaw, y, 1e-6);
    T_ASSERT(g.ctl.camera_follow);
}

static void test_follow_resets_into_a_new_race_then_swings_to_first_mark(void) {
    race_fresh();
    toggle_follow_cam();
    idle(3.0f);
    track_default(&tr);
    game_set_track(&g, &tr);                  /* a new race re-initialises the controls */
    T_ASSERT(!g.ctl.camera_follow);
    T_NEAR(g.ctl.camera_yaw, 0.0, 1e-6);
    toggle_follow_cam();
    float prev = g.ctl.camera_yaw;
    for (int i = 0; i < 90; i++) {
        game_update(&g, 0, 0, 0, 1.0f / 30);
        T_ASSERT(fabsf(wrap_pi(g.ctl.camera_yaw - prev)) <= 0.1f);
        prev = g.ctl.camera_yaw;
    }
    T_NEAR(g.ctl.camera_yaw, race_mark_rel(0), 0.05);
}

/* ---- journey weather shifts ---- */

static void jfresh(uint32_t mask, uint32_t seed) {
    boatcfg_default(&bc);
    weather_default(&wcfg);
    world_default(&wc);
    wc.journey_shift_min_s = wc.journey_shift_max_s = 5.0f;
    wc.weather_blend_s = 2.0f;
    game_init(&g, &bc, &wcfg, &wc, WEATHER_SUNNY, seed);
    game_set_journey_weathers(&g, mask);
    game_set_journey(&g);
}

static int wx_is(const Game *x, int preset) {
    return memcmp(game_weather(x), &x->weather.preset[preset], sizeof(WeatherPreset)) == 0;
}

static void step_until_turning(void) {
    for (int i = 0; i < 30 * 30 && game_weather_turning(&g) < 0; i++) game_update(&g, 0, 0, 0, 1.0f / 30);
}

static void test_free_range_shifts_from_picked_weather(void) {
    jfresh(0, 1);
    game_set_free_weathers(&g, (1u << WEATHER_CLOUDY) | (1u << WEATHER_CHOPPY));
    game_set_preset(&g, WEATHER_CLOUDY);
    game_set_track(&g, NULL);
    T_ASSERT(g.mode == GAME_FREE);
    T_ASSERT(g.preset == WEATHER_CLOUDY);
    T_ASSERT(wx_is(&g, WEATHER_CLOUDY));
    step_until_turning();
    T_ASSERT(game_weather_turning(&g) == WEATHER_CHOPPY);
}

static void test_free_range_without_ticks_holds(void) {
    jfresh(0, 1);
    game_set_preset(&g, WEATHER_CLOUDY);
    game_set_track(&g, NULL);
    step_until_turning();
    T_ASSERT(game_weather_turning(&g) == -1);
    T_ASSERT(wx_is(&g, WEATHER_CLOUDY));
}

static void test_journey_start_weather_from_ticks(void) {
    const uint32_t mask = (1u << WEATHER_SUNNY) | (1u << WEATHER_STORM);
    int seen_sunny = 0, seen_storm = 0;
    for (uint32_t seed = 1; seed <= 30; seed++) {
        jfresh(mask, seed);
        T_ASSERT(g.preset == WEATHER_SUNNY || g.preset == WEATHER_STORM);
        seen_sunny |= g.preset == WEATHER_SUNNY;
        seen_storm |= g.preset == WEATHER_STORM;
        T_ASSERT(game_weather_turning(&g) == -1);
        T_ASSERT(wx_is(&g, g.preset));
    }
    T_ASSERT(seen_sunny && seen_storm);
}

static void test_journey_mask_zero_keeps_preset(void) {
    fresh(1);
    game_init(&g, &bc, &wcfg, &wc, WEATHER_CHOPPY, 1);
    game_set_journey(&g);
    T_ASSERT(g.preset == WEATHER_CHOPPY);
    T_ASSERT(wx_is(&g, WEATHER_CHOPPY));
    T_ASSERT(game_weather(&g) == &g.wx);
}

static void test_journey_shift_blends_and_settles(void) {
    const uint32_t mask = (1u << WEATHER_CLOUDY) | (1u << WEATHER_CHOPPY);
    jfresh(mask, 1);
    const int start = g.preset;
    const int other = start == WEATHER_CLOUDY ? WEATHER_CHOPPY : WEATHER_CLOUDY;
    const float lo = fminf(g.weather.preset[start].wind_base, g.weather.preset[other].wind_base);
    const float hi = fmaxf(g.weather.preset[start].wind_base, g.weather.preset[other].wind_base);
    const float dir0 = g.wind.dir0;
    game_update(&g, 0, 0, 0, 1.0f / 30);   /* the first frame lifts heave from 0 to the sea: not measured */
    float prev = g.wf.heave, worst = 0.0f;
    int mid_checked = 0;
    for (int i = 1; i < 216; i++) {   /* to 7.2 s */
        double t0 = g.t;
        game_update(&g, 0, 0, 0, 1.0f / 30);
        if (t0 < 4.95) T_ASSERT(game_weather_turning(&g) == -1);
        if (t0 > 5.05 && t0 < 6.95) {
            T_ASSERT(game_weather_turning(&g) == other);
            T_ASSERT(g.sea.anchored);
        }
        if (!mid_checked && t0 > 5.9) {
            T_ASSERT(game_weather(&g)->wind_base > lo && game_weather(&g)->wind_base < hi);
            mid_checked = 1;
        }
        T_ASSERT(g.wind.dir0 == dir0);
        float d = fabsf(g.wf.heave - prev);
        if (d > worst) worst = d;
        prev = g.wf.heave;
    }
    T_ASSERT(mid_checked);
    T_ASSERT(game_weather_turning(&g) == -1);
    T_ASSERT(g.preset == other);
    T_ASSERT(wx_is(&g, other));
    T_ASSERT(!g.sea.anchored);
    T_ASSERT(g.wind.base == g.weather.preset[other].wind_base);
    T_ASSERT(worst <= 0.3f);
}

static void test_restart_mid_blend_is_clean(void) {   /* RF1 */
    const uint32_t mask = (1u << WEATHER_CLOUDY) | (1u << WEATHER_CHOPPY);
    jfresh(mask, 1);
    step_until_turning();
    for (int i = 0; i < 20; i++) game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(game_weather_turning(&g) >= 0 && g.sea.anchored);
    game_restart(&g, 77);
    T_ASSERT(game_weather_turning(&g) == -1);
    T_ASSERT(!g.sea.anchored);
    T_ASSERT(wx_is(&g, g.preset));
    T_ASSERT(mask & (1u << g.preset));
}

static void test_leaving_journey_mid_blend(void) {   /* RF2 */
    const uint32_t mask = (1u << WEATHER_CLOUDY) | (1u << WEATHER_CHOPPY);
    jfresh(mask, 1);
    step_until_turning();
    for (int i = 0; i < 20; i++) game_update(&g, 0, 0, 0, 1.0f / 30);
    T_ASSERT(game_weather_turning(&g) >= 0 && g.sea.anchored);
    game_set_preset(&g, WEATHER_SUNNY);
    game_set_track(&g, NULL);
    T_ASSERT(g.mode == GAME_FREE);
    T_ASSERT(!g.sea.anchored);
    T_ASSERT(wx_is(&g, WEATHER_SUNNY));
    for (int i = 0; i < 600; i++) {
        game_update(&g, 0, 0, 0, 1.0f / 30);
        T_ASSERT(game_weather_turning(&g) == -1);
        T_ASSERT(!g.sea.anchored);
    }
    T_ASSERT(g.preset == WEATHER_SUNNY && wx_is(&g, WEATHER_SUNNY));
}

static void test_recentre_during_blend(void) {   /* RF3 */
    const uint32_t mask = (1u << WEATHER_CLOUDY) | (1u << WEATHER_CHOPPY);
    jfresh(mask, 1);
    step_until_turning();
    for (int i = 0; i < 27; i++) game_update(&g, 0, 0, 0, 1.0f / 30);   /* near mid-blend, where k is furthest from both seas */
    g.b.x = 5119.0;
    g.b.y = 0.0;
    g.b.u = 5.0f;
    g.b.v = g.b.r = 0.0f;
    g.b.heading = 0.0f;
    game_update(&g, 0, 0, 0, 1.0f / 30);   /* the teleport itself moves the boat to new water: not measured */
    T_ASSERT(g.sea.anchored && g.b.x > 5119.0);
    float prev = g.wf.heave, worst = 0.0f;
    int recentred = 0;
    for (int i = 0; i < 30 && game_weather_turning(&g) >= 0; i++) {
        game_update(&g, 0, 0, 0, 1.0f / 30);
        if (g.b.x < 100.0 && g.sea.anchored) recentred = 1;
        if (g.sea.anchored) T_ASSERT(fabs(g.sea.ax - g.b.x) < 1.0);   /* the anchor moved with the origin */
        float d = fabsf(g.wf.heave - prev);
        if (d > worst) worst = d;
        prev = g.wf.heave;
    }
    T_ASSERT(recentred);
    T_ASSERT(worst <= 0.3f);
}

static void test_race_weather_untouched(void) {
    fresh(1);
    game_set_journey_weathers(&g, WEATHER_JOURNEY_DEFAULT);
    track_default(&tr);
    game_set_track(&g, &tr);
    for (int i = 0; i < 400 * 30; i++) {
        game_update(&g, 0, 0, 0, 1.0f / 30);
        T_ASSERT(wx_is(&g, g.preset));
        T_ASSERT(game_weather_turning(&g) == -1);
    }
}

int main(void) {
    T_RUN(test_huge_dt_is_bounded_and_finite);
    T_RUN(test_first_frame_zero_dt_is_safe);
    T_RUN(test_capsize_sets_state_and_freezes_controls);
    /* T_RUN(test_wreck_sets_state);   rocks disabled in game_update for now */
    T_RUN(test_restart_restores_start_in_one_call);
    /* T_RUN(test_same_seed_after_restart_same_rocks); */
    T_RUN(test_origin_recentres);
    T_RUN(test_set_preset_changes_weather);
    T_RUN(test_calm_start_sails_on);
    T_RUN(test_autopilot_holds_heading);
    T_RUN(test_autopilot_crank_turns_target_in_wheel_mode);
    T_RUN(test_autopilot_crank_in_trim_mode_leaves_target);
    T_RUN(test_autopilot_overrides_crank_wheel);
    T_RUN(test_held_a_without_press_does_not_engage);
    T_RUN(test_autopilot_cleared_on_restart);
    T_RUN(test_autopilot_frozen_when_capsized);
    T_RUN(test_b_tap_toggles_autotrim);
    T_RUN(test_a_tap_toggles_autopilot_off_again);
    T_RUN(test_b_hold_starts_engine_with_banner);
    T_RUN(test_b_tap_stops_engine_and_keeps_autotrim);
    T_RUN(test_engine_is_slow_and_never_brakes);
    T_RUN(test_engine_pushes_along_the_rudder);
    T_RUN(test_engine_cleared_by_restart);
    T_RUN(test_set_track_places_boat_and_wind);
    T_RUN(test_track_wind_speed_overrides_preset);
    T_RUN(test_race_advances_when_boat_rounds_b);
    T_RUN(test_gate_registers_through_game_update);
    T_RUN(test_restart_resets_race);
    T_RUN(test_preset_change_keeps_track_wind);
    T_RUN(test_recentre_moves_marks_with_boat);
    T_RUN(test_free_range_unchanged);
    T_RUN(test_follow_camera_faces_next_mark_in_race);
    T_RUN(test_follow_camera_swings_to_next_mark_when_one_is_rounded);
    T_RUN(test_follow_camera_in_journey_uses_next_buoy);
    T_RUN(test_follow_camera_free_mode_holds_yaw);
    T_RUN(test_follow_resets_into_a_new_race_then_swings_to_first_mark);
    T_RUN(test_free_range_shifts_from_picked_weather);
    T_RUN(test_free_range_without_ticks_holds);
    T_RUN(test_journey_start_weather_from_ticks);
    T_RUN(test_journey_mask_zero_keeps_preset);
    T_RUN(test_journey_shift_blends_and_settles);
    T_RUN(test_restart_mid_blend_is_clean);
    T_RUN(test_leaving_journey_mid_blend);
    T_RUN(test_recentre_during_blend);
    T_RUN(test_race_weather_untouched);
    T_DONE();
}
