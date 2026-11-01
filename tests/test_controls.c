#include "t.h"
#include <string.h>
#include "controls.h"
#include "camera.h"

typedef struct { BoatCfg cfg; Controls c; BoatInputs in; } Rig;

static void rig_init(Rig *r) {
    memset(r, 0, sizeof *r);
    boatcfg_default(&r->cfg);
    controls_init(&r->c);
    BoatState b; boat_init(&b, &r->cfg);
    boat_inputs_init(&r->in, &r->cfg);
}
static void tick(Rig *r, uint32_t pressed, float crank) {
    controls_update(&r->c, &r->cfg, pressed, pressed, crank, 1.0f / 30, &r->in);
}

static void test_right_latches_trim(void) {
    Rig r; rig_init(&r);
    float t0 = r.in.trim_target[0];
    tick(&r, BTN_RIGHT, 0);
    T_ASSERT(r.c.mode == MODE_TRIM && r.c.sail == 0);
    tick(&r, 0, 30);                      /* no button held */
    T_ASSERT(r.in.trim_target[0] > t0);
}

static void test_repress_advances_sail(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_RIGHT, 0); T_ASSERT(r.c.sail == 0);
    tick(&r, BTN_RIGHT, 0); T_ASSERT(r.c.sail == 1);
    tick(&r, BTN_RIGHT, 0); T_ASSERT(r.c.sail == 0);   /* wraps */
    float j0 = r.in.trim_target[1];
    tick(&r, BTN_RIGHT, 0);
    tick(&r, 0, 20);
    T_ASSERT(r.in.trim_target[1] > j0);
}

static void test_left_hoist_shares_selection(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_RIGHT, 0); tick(&r, BTN_RIGHT, 0);      /* jib selected */
    tick(&r, BTN_LEFT, 0);                                /* switch to hoist, no advance */
    T_ASSERT(r.c.mode == MODE_HOIST && r.c.sail == 1);
    tick(&r, 0, -100);
    T_ASSERT(r.in.hoist_target[1] < 1.0f);
    T_ASSERT(r.in.hoist_target[0] == 1.0f);
    tick(&r, BTN_LEFT, 0);
    T_ASSERT(r.c.sail == 0);
}

static void test_up_crank_moves_wheel_not_trim(void) {
    Rig r; rig_init(&r);
    float t0 = r.in.trim_target[0];
    tick(&r, BTN_UP, 0);
    tick(&r, 0, 90);
    T_ASSERT(r.in.wheel_target > 0.1f);
    T_ASSERT(r.in.trim_target[0] == t0);
    for (int i = 0; i < 100; i++) tick(&r, 0, 90);
    T_ASSERT(r.in.wheel_target == 1.0f);
}

static void tick_h(Rig *r, uint32_t pressed, uint32_t held, float crank) {
    controls_update(&r->c, &r->cfg, pressed, held, crank, 1.0f / 30, &r->in);
}
static void tap_down(Rig *r) { tick_h(r, BTN_DOWN, BTN_DOWN, 0); tick_h(r, 0, 0, 0); }
/* press DOWN and keep it held for `secs`, then release */
static void hold_down(Rig *r, float secs) {
    tick_h(r, BTN_DOWN, BTN_DOWN, 0);
    for (int i = 1; i < (int)(secs * 30); i++) tick_h(r, 0, BTN_DOWN, 0);
    tick_h(r, 0, 0, 0);
}
static void idle(Rig *r, int n) { for (int i = 0; i < n; i++) tick_h(r, 0, 0, 0); }

static void test_camera_yaw_unlimited_and_hold_returns(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_DOWN, 0);
    int passed_pi = 0;
    float prev = 0;
    for (int i = 0; i < 200; i++) {
        tick(&r, 0, 90);
        T_ASSERT(r.c.camera_yaw > -(float)M_PI && r.c.camera_yaw <= (float)M_PI);
        if (r.c.camera_yaw < prev - 3.0f) passed_pi = 1;
        prev = r.c.camera_yaw;
    }
    T_ASSERT(passed_pi);
    passed_pi = 0;
    for (int i = 0; i < 400; i++) {
        tick(&r, 0, -90);
        T_ASSERT(r.c.camera_yaw > -(float)M_PI && r.c.camera_yaw <= (float)M_PI);
        if (r.c.camera_yaw > prev + 3.0f) passed_pi = 1;
        prev = r.c.camera_yaw;
    }
    T_ASSERT(passed_pi);
    for (int i = 0; i < 20; i++) tick(&r, 0, 30);   /* whole turns so far: end off-centre */
    T_ASSERT(fabsf(r.c.camera_yaw) > 0.5f);
    tick(&r, BTN_UP, 0);                  /* leaving camera mode keeps the look */
    float y = r.c.camera_yaw;
    idle(&r, 100);
    T_NEAR(r.c.camera_yaw, y, 1e-6);
    tick_h(&r, BTN_DOWN, BTN_DOWN, 0);    /* entering does not recentre */
    T_NEAR(r.c.camera_yaw, y, 1e-6);
    float hold = r.cfg.controls.camera_hold_s;
    for (int i = 0; i < (int)(hold * 30) + 2; i++) tick_h(&r, 0, BTN_DOWN, 0);   /* hold fires */
    float y0 = r.c.camera_yaw;
    tick_h(&r, 0, BTN_DOWN, 0);
    float step = deg2rad(r.cfg.camera.return_rate) / 30;
    T_NEAR(fabsf(r.c.camera_yaw - y0), step, 1e-4);
    T_ASSERT(fabsf(r.c.camera_yaw) < fabsf(y0));
    for (int i = 0; i < 200; i++) tick_h(&r, 0, 0, 0);
    T_ASSERT(r.c.camera_yaw == 0.0f);
}

static void test_down_tap_in_camera_mode_toggles_axis(void) {
    Rig r; rig_init(&r);
    tap_down(&r);
    T_ASSERT(r.c.mode == MODE_CAMERA && !r.c.camera_vertical);
    tap_down(&r); T_ASSERT(r.c.camera_vertical);
    for (int i = 0; i < 200; i++) tick(&r, 0, 90);
    T_NEAR(r.c.camera_pitch, deg2rad(r.cfg.camera.pitch_range_deg), 1e-4);
    T_ASSERT(r.c.camera_yaw == 0.0f);
    tap_down(&r); T_ASSERT(!r.c.camera_vertical);
}

static void test_entering_tap_does_not_toggle_axis(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_UP, 0);
    tap_down(&r);
    T_ASSERT(r.c.mode == MODE_CAMERA && !r.c.camera_vertical);
}

static void test_hold_recentres_both_axes_and_does_not_toggle(void) {
    Rig r; rig_init(&r);
    tap_down(&r);
    for (int i = 0; i < 40; i++) tick(&r, 0, 30);          /* yaw */
    tap_down(&r);
    for (int i = 0; i < 40; i++) tick(&r, 0, 30);          /* pitch */
    T_ASSERT(r.c.camera_yaw != 0.0f && r.c.camera_pitch != 0.0f && r.c.camera_vertical);
    hold_down(&r, r.cfg.controls.camera_hold_s + 0.1f);
    idle(&r, 300);
    T_ASSERT(r.c.camera_yaw == 0.0f && r.c.camera_pitch == 0.0f);
    T_ASSERT(r.c.camera_vertical);
}

static void test_hold_shorter_than_threshold_toggles_not_recentres(void) {
    Rig r; rig_init(&r);
    tap_down(&r);
    for (int i = 0; i < 40; i++) tick(&r, 0, 30);
    float y = r.c.camera_yaw;
    hold_down(&r, r.cfg.controls.camera_hold_s - 0.1f);
    T_ASSERT(r.c.camera_vertical);
    idle(&r, 100);
    T_NEAR(r.c.camera_yaw, y, 1e-6);
}

static void test_crank_cancels_hold_recentre(void) {
    Rig r; rig_init(&r);
    tap_down(&r);
    for (int i = 0; i < 60; i++) tick(&r, 0, 30);
    for (int i = 0; i < 30; i++) tick_h(&r, i ? 0 : BTN_DOWN, BTN_DOWN, 0);   /* hold fires */
    T_ASSERT(r.c.camera_recentre);
    tick_h(&r, 0, 0, 10);
    T_ASSERT(!r.c.camera_recentre);
    float y = r.c.camera_yaw;
    idle(&r, 30);
    T_NEAR(r.c.camera_yaw, y, 1e-6);
}

static void test_crank_after_cancel_moves_one_step_from_the_live_view(void) {
    Rig r; rig_init(&r);
    tap_down(&r);
    for (int i = 0; i < 60; i++) tick(&r, 0, 30);
    for (int i = 0; i < 30; i++) tick_h(&r, i ? 0 : BTN_DOWN, BTN_DOWN, 0);   /* hold fires */
    for (int i = 0; i < 5; i++) tick_h(&r, 0, 0, 0);                          /* eases part-way */
    float live = r.c.camera_yaw;
    T_ASSERT(live != 0 && r.c.camera_target_yaw != live);
    tick_h(&r, 0, 0, 10);
    float step = deg2rad(10.0f * r.cfg.controls.crank_gain_camera);
    T_NEAR(wrap_pi(r.c.camera_yaw - live), step, 1e-4);
}

static void test_vertical_crank_has_no_dead_travel_past_the_floor(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_DOWN, 0);
    tap_down(&r);                                   /* vertical */
    for (int i = 0; i < 400; i++) tick(&r, 0, -10);
    float lo, hi;
    camera_look_limits(&r.cfg, &lo, &hi);
    T_NEAR(r.c.camera_pitch, lo, 1e-5);
    float before = r.c.camera_pitch;
    tick(&r, 0, 10);
    T_NEAR(r.c.camera_pitch - before, deg2rad(10.0f * r.cfg.controls.crank_gain_camera), 1e-4);
}

static void test_yaw_crosses_behind_without_clamp_or_jump(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_DOWN, 0);
    float step = deg2rad(10.0f * r.cfg.controls.crank_gain_camera), prev = 0;
    for (int i = 0; i < 100; i++) {
        tick(&r, 0, 10);
        float d = wrap_pi(r.c.camera_yaw - prev);
        T_NEAR(d, step, 1e-4);
        prev = r.c.camera_yaw;
    }
}

static void test_recentre_from_near_pi_goes_short_way(void) {
    Rig r; rig_init(&r);
    tap_down(&r);
    r.c.camera_yaw = r.c.camera_target_yaw = 3.0f;
    for (int i = 0; i < 30; i++) tick_h(&r, i ? 0 : BTN_DOWN, BTN_DOWN, 0);
    float prev = r.c.camera_yaw;
    for (int i = 0; i < 100; i++) {
        tick_h(&r, 0, 0, 0);
        T_ASSERT(r.c.camera_yaw <= prev);
        prev = r.c.camera_yaw;
    }
    T_ASSERT(r.c.camera_yaw == 0.0f);
}

static void test_camera_hold_default_is_0_75(void) {
    BoatCfg c; boatcfg_default(&c);
    T_NEAR(c.controls.camera_hold_s, 0.75, 1e-6);
}

static void test_priority_up_over_right_over_left_over_down(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_UP | BTN_RIGHT | BTN_LEFT | BTN_DOWN, 0); T_ASSERT(r.c.mode == MODE_WHEEL);
    tick(&r, BTN_RIGHT | BTN_LEFT | BTN_DOWN, 0);          T_ASSERT(r.c.mode == MODE_TRIM);
    Rig q; rig_init(&q);
    tick(&q, BTN_LEFT | BTN_DOWN, 0);                      T_ASSERT(q.c.mode == MODE_HOIST);
}

static void test_crank_clamps_ranges(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_RIGHT, 0);
    for (int i = 0; i < 500; i++) tick(&r, 0, 90);
    T_NEAR(r.in.trim_target[0], deg2rad(r.cfg.sails[0].trim_max_deg), 1e-5);
    for (int i = 0; i < 500; i++) tick(&r, 0, -90);
    T_NEAR(r.in.trim_target[0], deg2rad(r.cfg.sails[0].trim_min_deg), 1e-5);
    tick(&r, BTN_LEFT, 0);
    for (int i = 0; i < 500; i++) tick(&r, 0, 90);
    T_ASSERT(r.in.hoist_target[0] == 1.0f);
    for (int i = 0; i < 500; i++) tick(&r, 0, -90);
    T_ASSERT(r.in.hoist_target[0] == 0.0f);
}

static void test_single_sail_ignores_repress(void) {
    Rig r; rig_init(&r);
    r.cfg.sail_count = 1;
    tick(&r, BTN_RIGHT, 0); tick(&r, BTN_RIGHT, 0); tick(&r, BTN_RIGHT, 0);
    T_ASSERT(r.c.sail == 0);
}

static void test_bad_inputs_stay_finite(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_UP, NAN);
    T_ASSERT(isfinite(r.in.wheel_target));
    controls_update(&r.c, &r.cfg, 0, 0, 90, 0, &r.in);
    controls_update(&r.c, &r.cfg, 0, 0, 90, NAN, &r.in);
    T_ASSERT(isfinite(r.c.camera_yaw));
    r.c.sail = 99;                         /* corrupted selection */
    tick(&r, BTN_RIGHT, 0);
    T_ASSERT(r.c.sail >= 0 && r.c.sail < r.cfg.sail_count);
}

static void test_b_is_left_to_game(void) {   /* the anchor tap lives with the autotrim hold, see test_game */
    Rig r; rig_init(&r);
    tick(&r, BTN_B, 0);
    T_ASSERT(!r.in.anchor);
}

int main(void) {
    T_RUN(test_right_latches_trim);
    T_RUN(test_repress_advances_sail);
    T_RUN(test_left_hoist_shares_selection);
    T_RUN(test_up_crank_moves_wheel_not_trim);
    T_RUN(test_camera_yaw_unlimited_and_hold_returns);
    T_RUN(test_down_tap_in_camera_mode_toggles_axis);
    T_RUN(test_entering_tap_does_not_toggle_axis);
    T_RUN(test_hold_recentres_both_axes_and_does_not_toggle);
    T_RUN(test_hold_shorter_than_threshold_toggles_not_recentres);
    T_RUN(test_crank_cancels_hold_recentre);
    T_RUN(test_crank_after_cancel_moves_one_step_from_the_live_view);
    T_RUN(test_vertical_crank_has_no_dead_travel_past_the_floor);
    T_RUN(test_yaw_crosses_behind_without_clamp_or_jump);
    T_RUN(test_recentre_from_near_pi_goes_short_way);
    T_RUN(test_camera_hold_default_is_0_75);
    T_RUN(test_priority_up_over_right_over_left_over_down);
    T_RUN(test_crank_clamps_ranges);
    T_RUN(test_single_sail_ignores_repress);
    T_RUN(test_bad_inputs_stay_finite);
    T_RUN(test_b_is_left_to_game);
    return T_DONE();
}
