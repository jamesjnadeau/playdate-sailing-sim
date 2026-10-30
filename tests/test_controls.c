#include "t.h"
#include <string.h>
#include "controls.h"

typedef struct { BoatCfg cfg; Controls c; BoatInputs in; } Rig;

static void rig_init(Rig *r) {
    memset(r, 0, sizeof *r);
    boatcfg_default(&r->cfg);
    controls_init(&r->c);
    BoatState b; boat_init(&b, &r->cfg);
    boat_inputs_init(&r->in, &r->cfg);
}
static void tick(Rig *r, uint32_t pressed, float crank) {
    controls_update(&r->c, &r->cfg, pressed, crank, 1.0f / 30, &r->in);
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

static void test_camera_yaw_clamped_and_returns(void) {
    Rig r; rig_init(&r);
    tick(&r, BTN_DOWN, 0);
    for (int i = 0; i < 200; i++) tick(&r, 0, 90);
    float lim = deg2rad(r.cfg.camera.yaw_range_deg);
    T_NEAR(r.c.camera_yaw, lim, 1e-4);
    for (int i = 0; i < 200; i++) tick(&r, 0, -90);
    T_NEAR(r.c.camera_yaw, -lim, 1e-4);
    tick(&r, BTN_UP, 0);                  /* leaving camera mode keeps the look */
    for (int i = 0; i < 100; i++) tick(&r, 0, 0);
    T_NEAR(r.c.camera_yaw, -lim, 1e-4);
    tick(&r, BTN_DOWN, 0);                /* entering does not recentre either */
    T_NEAR(r.c.camera_yaw, -lim, 1e-4);
    tick(&r, BTN_DOWN, 0);                /* a second press in camera mode does */
    float y0 = r.c.camera_yaw;
    tick(&r, 0, 0);
    float step = deg2rad(r.cfg.camera.return_rate) / 30;
    T_NEAR(r.c.camera_yaw - y0, step, 1e-4);   /* from -lim, moving toward 0 */
    for (int i = 0; i < 100; i++) tick(&r, 0, 0);
    T_ASSERT(r.c.camera_yaw == 0.0f);
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
    controls_update(&r.c, &r.cfg, 0, 90, 0, &r.in);
    controls_update(&r.c, &r.cfg, 0, 90, NAN, &r.in);
    T_ASSERT(isfinite(r.c.camera_yaw));
    r.c.sail = 99;                         /* corrupted selection */
    tick(&r, BTN_RIGHT, 0);
    T_ASSERT(r.c.sail >= 0 && r.c.sail < r.cfg.sail_count);
}

static void test_a_toggles_anchor(void) {
    Rig r; rig_init(&r);
    T_ASSERT(!r.in.anchor);
    tick(&r, BTN_A, 0); T_ASSERT(r.in.anchor);
    tick(&r, 0, 0);     T_ASSERT(r.in.anchor);      /* held state, not a pulse */
    tick(&r, BTN_A, 0); T_ASSERT(!r.in.anchor);
    tick(&r, BTN_A | BTN_RIGHT, 0);                 /* A alongside a mode button still toggles */
    T_ASSERT(r.in.anchor && r.c.mode == MODE_TRIM);
}

int main(void) {
    T_RUN(test_right_latches_trim);
    T_RUN(test_repress_advances_sail);
    T_RUN(test_left_hoist_shares_selection);
    T_RUN(test_up_crank_moves_wheel_not_trim);
    T_RUN(test_camera_yaw_clamped_and_returns);
    T_RUN(test_priority_up_over_right_over_left_over_down);
    T_RUN(test_crank_clamps_ranges);
    T_RUN(test_single_sail_ignores_repress);
    T_RUN(test_bad_inputs_stay_finite);
    T_RUN(test_a_toggles_anchor);
    return T_DONE();
}
