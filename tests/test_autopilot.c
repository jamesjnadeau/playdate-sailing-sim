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
static void press_a(float heading) { autopilot_buttons(&ap, BTN_A, heading); }
static void test_press_toggles_on_and_sets_target_to_heading(void) {
    fresh();
    press_a(1.0f);
    T_ASSERT(ap.on);
    T_NEAR(ap.target, 1.0f, 1e-6);
}
static void test_second_press_turns_off(void) {
    fresh();
    press_a(1.0f);
    press_a(2.0f);
    T_ASSERT(!ap.on);
}
static void test_other_buttons_and_no_press_do_nothing(void) {
    fresh();
    autopilot_buttons(&ap, BTN_B, 1.0f);
    autopilot_buttons(&ap, 0, 1.0f);
    T_ASSERT(!ap.on);
}
static void test_press_nonfinite_heading_safe(void) {
    fresh();
    press_a(NAN);
    T_ASSERT(ap.on && isfinite(ap.target));
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
static void test_crank_adjusts_target(void) {
    fresh();
    ap.on = true; ap.target = 1.0f;
    autopilot_crank(&ap, K, 360.0f);
    T_NEAR(ap.target, 1.0f + deg2rad(10), 1e-5);
    autopilot_crank(&ap, K, -180.0f);
    T_NEAR(ap.target, 1.0f + deg2rad(5), 1e-5);
}
static void test_crank_wraps_and_ignores_nonfinite(void) {
    fresh();
    ap.on = true; ap.target = PI_F - 0.01f;
    autopilot_crank(&ap, K, 360.0f);
    T_ASSERT(ap.target > -PI_F && ap.target <= PI_F);
    T_NEAR(ap.target, PI_F - 0.01f + deg2rad(10) - 2 * PI_F, 1e-5);
    float t = ap.target;
    autopilot_crank(&ap, K, NAN);
    T_NEAR(ap.target, t, 1e-9);
}

int main(void) {
    T_RUN(test_press_toggles_on_and_sets_target_to_heading);
    T_RUN(test_second_press_turns_off);
    T_RUN(test_other_buttons_and_no_press_do_nothing);
    T_RUN(test_press_nonfinite_heading_safe);
    T_RUN(test_wheel_sign_and_clamp);
    T_RUN(test_wheel_never_turns_through_no_go);
    T_RUN(test_wheel_inside_no_go_takes_short_way);
    T_RUN(test_wheel_damping);
    T_RUN(test_wheel_nonfinite_is_zero);
    T_RUN(test_crank_adjusts_target);
    T_RUN(test_crank_wraps_and_ignores_nonfinite);
    return T_DONE();
}
