#include "t.h"
#include <math.h>
#include "engine.h"

#define DT 0.1f

static BoatCfg cfg;
static Engine e;

static void fresh(void) {
    boatcfg_default(&cfg);
    engine_init(&e);
}
static bool frame(uint32_t pressed, uint32_t held) {
    return engine_buttons(&e, &cfg.controls, pressed, held, DT);
}
static bool hold_b(int frames) {
    bool tap = frame(BTN_B, BTN_B);
    for (int i = 1; i < frames; i++) tap |= frame(0, BTN_B);
    return tap | frame(0, 0);
}

static void test_hold_starts_engine_and_banner(void) {
    fresh();
    T_ASSERT(!hold_b(15));
    T_ASSERT(e.on);
}
static void test_default_hold_is_one_second(void) {
    fresh();
    T_NEAR(cfg.controls.engine_hold_s, 1.0, 1e-6);
    hold_b(8);
    T_ASSERT(!e.on);
}
static void test_quick_press_is_reported_and_does_not_start(void) {
    fresh();
    T_ASSERT(frame(BTN_B, BTN_B) == false);
    T_ASSERT(frame(0, 0) == true);
    T_ASSERT(!e.on);
}
static void test_long_hold_fires_once_and_release_is_not_a_tap(void) {
    fresh();
    T_ASSERT(!hold_b(40));
    T_ASSERT(e.on);
}
static void test_held_without_press_is_ignored(void) {
    fresh();
    for (int i = 0; i < 40; i++) frame(0, BTN_B);
    T_ASSERT(!e.on);
    T_ASSERT(!frame(0, 0));
}
static void test_thrust_shape(void) {
    T_ASSERT(engine_thrust(1000, 0) > 0);
    T_ASSERT(engine_thrust(1000, 0.47f) < engine_thrust(1000, 0));
    T_NEAR(engine_thrust(1000, ENGINE_SPEED), 0.0, 1e-6);
    T_NEAR(engine_thrust(1000, 3.0f), 0.0, 1e-6);       /* never brakes */
    T_ASSERT(engine_thrust(1000, -1.0f) > 0);
}
static void test_nonfinite_is_safe(void) {
    fresh();
    frame(BTN_B, BTN_B);
    engine_buttons(&e, &cfg.controls, 0, BTN_B, NAN);
    T_ASSERT(isfinite(e.b_time));
    T_ASSERT(isfinite(engine_thrust(NAN, 0)) && isfinite(engine_thrust(1000, NAN)));
}

int main(void) {
    T_RUN(test_hold_starts_engine_and_banner);
    T_RUN(test_default_hold_is_one_second);
    T_RUN(test_quick_press_is_reported_and_does_not_start);
    T_RUN(test_long_hold_fires_once_and_release_is_not_a_tap);
    T_RUN(test_held_without_press_is_ignored);
    T_RUN(test_thrust_shape);
    T_RUN(test_nonfinite_is_safe);
    return T_DONE();
}
