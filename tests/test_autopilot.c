#include "t.h"
#include <string.h>
#include "autopilot.h"

#define PI_F 3.14159265f
#define DT 0.1f

static BoatCfg cfg;
static Autopilot ap;
static const ControlsCfg *K;

static void fresh(void) {
    boatcfg_default(&cfg);
    K = &cfg.controls;
    autopilot_init(&ap);
}
static void frame(uint32_t pressed, uint32_t held, float heading, float wind_from) {
    autopilot_buttons(&ap, K, pressed, held, DT, heading, wind_from);
}
static void hold(int frames, float heading, float wind_from) {
    frame(BTN_A, BTN_A, heading, wind_from);
    for (int i = 1; i < frames; i++) frame(0, BTN_A, heading, wind_from);
    frame(0, 0, heading, wind_from);
}
static void tap(float heading, float wind_from) {
    frame(BTN_A, BTN_A, heading, wind_from);
    frame(0, 0, heading, wind_from);
}

static void test_next_heading_steps_clockwise(void) {
    T_NEAR(autopilot_next_heading(0, PI_F, deg2rad(45), deg2rad(45)), deg2rad(45), 1e-5);
}
static void test_next_heading_skips_no_go(void) {
    T_NEAR(autopilot_next_heading(0, deg2rad(45), deg2rad(45), deg2rad(45)), deg2rad(90), 1e-5);
}
static void test_next_heading_wraps(void) {
    float r = autopilot_next_heading(deg2rad(170), PI_F / 2 + PI_F, deg2rad(45), deg2rad(45));
    T_ASSERT(r > -PI_F && r <= PI_F);
    T_NEAR(r, deg2rad(-145), 1e-5);
}
static void test_next_heading_all_blocked_returns_from(void) {
    T_NEAR(autopilot_next_heading(1.0f, 0, deg2rad(45), PI_F), 1.0f, 1e-6);
}
static void test_hold_toggles_on_and_sets_target_to_heading(void) {
    fresh();
    frame(BTN_A, BTN_A, 1.0f, 3.0f);
    for (int i = 0; i < 6; i++) frame(0, BTN_A, 1.0f, 3.0f);
    T_ASSERT(ap.on);
    T_NEAR(ap.target, 1.0f, 1e-6);
}
static void test_hold_toggles_once_then_off_on_second_hold(void) {
    fresh();
    hold(30, 1.0f, 3.0f);
    T_ASSERT(ap.on);
    hold(30, 1.0f, 3.0f);
    T_ASSERT(!ap.on);
}
static void test_tap_when_on_steps_target(void) {
    fresh();
    hold(10, 0.0f, PI_F);
    T_ASSERT(ap.on);
    T_NEAR(ap.target, 0.0f, 1e-6);       /* the release after a hold is not a tap */
    tap(0.0f, PI_F);
    T_NEAR(ap.target, deg2rad(K->autopilot_step_deg), 1e-5);
    tap(0.0f, PI_F);
    T_NEAR(ap.target, 2 * deg2rad(K->autopilot_step_deg), 1e-5);
}
static void test_tap_when_off_does_nothing(void) {
    fresh();
    tap(0.0f, PI_F);
    T_ASSERT(!ap.on);
}
static void test_held_without_press_is_ignored(void) {
    fresh();
    for (int i = 0; i < 30; i++) frame(0, BTN_A, 0.0f, PI_F);
    T_ASSERT(!ap.on);
}
static void test_wheel_sign_and_clamp(void) {
    fresh();
    ap.target = 0.5f;  T_ASSERT(autopilot_wheel(&ap, K, 0, 0, PI_F) > 0);
    ap.target = -0.5f; T_ASSERT(autopilot_wheel(&ap, K, 0, 0, PI_F) < 0);
    ap.target = 3.0f;  T_ASSERT(autopilot_wheel(&ap, K, 0, 0, PI_F) == 1.0f);
    ap.target = PI_F - 0.1f;
    T_ASSERT(autopilot_wheel(&ap, K, -PI_F + 0.1f, 0, PI_F) < 0);   /* short way round */
}
static void test_wheel_never_turns_through_no_go(void) {
    fresh();
    /* wind from 0; close-hauled at -60 deg, target +75 deg: short way is through the wind */
    ap.target = deg2rad(75);
    T_ASSERT(autopilot_wheel(&ap, K, deg2rad(-60), 0, 0.0f) < 0);   /* long way round (gybe) */
    ap.target = deg2rad(-75);
    T_ASSERT(autopilot_wheel(&ap, K, deg2rad(60), 0, 0.0f) > 0);
    /* short way clear of the wind is unchanged */
    ap.target = deg2rad(120);
    T_ASSERT(autopilot_wheel(&ap, K, deg2rad(75), 0, 0.0f) > 0);
}
static void test_wheel_inside_no_go_takes_short_way(void) {
    fresh();
    ap.target = deg2rad(75);
    T_ASSERT(autopilot_wheel(&ap, K, deg2rad(-20), 0, 0.0f) > 0);   /* already in irons: just get out */
}
static void test_wheel_damping(void) {
    fresh();
    ap.target = 0;
    T_ASSERT(autopilot_wheel(&ap, K, 0, 0.5f, PI_F) < 0);
}
static void test_wheel_nonfinite_is_zero(void) {
    fresh();
    ap.target = 0.5f;
    T_ASSERT(autopilot_wheel(&ap, K, NAN, 0, PI_F) == 0.0f);
    T_ASSERT(autopilot_wheel(&ap, K, 0, NAN, PI_F) == 0.0f);
    ap.target = NAN;
    T_ASSERT(autopilot_wheel(&ap, K, 0, 0, PI_F) == 0.0f);
}
static void test_buttons_nonfinite_dt_and_heading_safe(void) {
    fresh();
    autopilot_buttons(&ap, K, BTN_A, BTN_A, NAN, NAN, NAN);
    for (int i = 0; i < 20; i++) autopilot_buttons(&ap, K, 0, BTN_A, NAN, NAN, NAN);
    autopilot_buttons(&ap, K, 0, 0, NAN, NAN, NAN);
    T_ASSERT(isfinite(ap.target) && isfinite(ap.a_time));
}

int main(void) {
    T_RUN(test_next_heading_steps_clockwise);
    T_RUN(test_next_heading_skips_no_go);
    T_RUN(test_next_heading_wraps);
    T_RUN(test_next_heading_all_blocked_returns_from);
    T_RUN(test_hold_toggles_on_and_sets_target_to_heading);
    T_RUN(test_hold_toggles_once_then_off_on_second_hold);
    T_RUN(test_tap_when_on_steps_target);
    T_RUN(test_tap_when_off_does_nothing);
    T_RUN(test_held_without_press_is_ignored);
    T_RUN(test_wheel_sign_and_clamp);
    T_RUN(test_wheel_never_turns_through_no_go);
    T_RUN(test_wheel_inside_no_go_takes_short_way);
    T_RUN(test_wheel_damping);
    T_RUN(test_wheel_nonfinite_is_zero);
    T_RUN(test_buttons_nonfinite_dt_and_heading_safe);
    return T_DONE();
}
