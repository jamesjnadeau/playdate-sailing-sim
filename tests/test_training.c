#include "t.h"
#include "training.h"
#include "game.h"
#include "mathx.h"

static Game g;
static Training t;

/* Wind blows toward wind.dir; its source is dir + pi. Heading off_deg away from the source. */
static void fresh(float off_deg) {
    BoatCfg bc; WeatherCfg wcfg; WorldCfg wc;
    boatcfg_default(&bc);
    weather_default(&wcfg);
    world_default(&wc);
    game_init(&g, &bc, &wcfg, &wc, WEATHER_SUNNY, 1);
    g.wind.dir = 0.0f;                                   /* source is at angle pi */
    g.b.heading = wrap_pi((float)M_PI + off_deg * (float)M_PI / 180.0f);
    g.b.u = 2.0f;
    g.at.on = true;
    g.en.on = false;
    training_init(&t);
}

static void hold(int frames) {
    for (int i = 0; i < frames; i++) training_update(&t, &g, 0.1f);
}

static void goto_step(TrainStep s) { t.step = s; t.held = 0; t.seen = 0; }

static void test_downwind_auto_advances_after_hold(void) {
    fresh(170);
    T_ASSERT(t.step == TRAIN_DOWNWIND_AUTO);
    hold((int)(TRAIN_HOLD_S * 10) - 3);
    T_ASSERT(t.step == TRAIN_DOWNWIND_AUTO);
    hold(6);
    T_ASSERT(t.step == TRAIN_BEAM_AUTO);
}

static void test_passing_through_angle_does_not_advance(void) {
    fresh(170);
    hold(10);
    g.b.heading = wrap_pi((float)M_PI + 90.0f * (float)M_PI / 180.0f);
    hold(1);
    T_NEAR(t.held, 0.0f, 1e-6);
    g.b.heading = wrap_pi((float)M_PI + 170.0f * (float)M_PI / 180.0f);
    hold(10);
    T_ASSERT(t.step == TRAIN_DOWNWIND_AUTO);
}

static void test_auto_step_needs_autotrim_on(void) {
    fresh(170);
    g.at.on = false;
    hold(40);
    T_ASSERT(t.step == TRAIN_DOWNWIND_AUTO);
}

static void test_manual_step_needs_autotrim_off(void) {
    fresh(170);
    goto_step(TRAIN_DOWNWIND_MANUAL);
    g.at.on = true;
    hold(40);
    T_ASSERT(t.step == TRAIN_DOWNWIND_MANUAL);
    g.at.on = false;
    hold(25);
    T_ASSERT(t.step == TRAIN_BEAM_MANUAL);
}

static void test_points_needs_both_close_and_broad_reach(void) {
    fresh(60);
    goto_step(TRAIN_POINTS_AUTO);
    hold(40);
    T_ASSERT(t.step == TRAIN_POINTS_AUTO);        /* close reach alone is not enough */
    g.b.heading = wrap_pi((float)M_PI + 130.0f * (float)M_PI / 180.0f);
    hold(25);
    T_ASSERT(t.step == TRAIN_CLOSE_HAULED);
}

static void test_irons_needs_low_speed_and_pointing_into_wind(void) {
    fresh(10);
    goto_step(TRAIN_IRONS);
    g.b.u = 2.0f;                                  /* pointing into the wind but still moving */
    hold(40);
    T_ASSERT(t.step == TRAIN_IRONS);
    g.b.u = 0.1f;
    g.b.heading = wrap_pi((float)M_PI + 90.0f * (float)M_PI / 180.0f);   /* slow but not pointing up */
    hold(40);
    T_ASSERT(t.step == TRAIN_IRONS);
    g.b.heading = wrap_pi((float)M_PI + 10.0f * (float)M_PI / 180.0f);
    hold(25);
    T_ASSERT(t.step == TRAIN_ENGINE_FREE);
}

static void test_engine_step_needs_engine_on_and_moving_free(void) {
    fresh(100);
    goto_step(TRAIN_ENGINE_FREE);
    g.en.on = false;
    hold(40);
    T_ASSERT(t.step == TRAIN_ENGINE_FREE);
    g.en.on = true;
    g.b.u = 0.2f;                                   /* engine on but not yet moving */
    hold(40);
    T_ASSERT(t.step == TRAIN_ENGINE_FREE);
    g.b.u = 0.6f;
    hold(25);
    T_ASSERT(t.step == TRAIN_DOWNWIND_MANUAL);
}

static void test_engine_locked_until_engine_step(void) {
    Training x;
    training_init(&x);
    for (int s = 0; s < TRAIN_STEPS; s++) {
        x.step = (TrainStep)s;
        T_ASSERT(training_engine_locked(&x) == (s < TRAIN_ENGINE_FREE));
    }
}

static void test_no_advance_when_capsized(void) {
    fresh(170);
    g.run = RUN_CAPSIZED;
    hold(40);
    T_ASSERT(t.step == TRAIN_DOWNWIND_AUTO);
    T_NEAR(t.held, 0.0f, 1e-6);
}

static void test_done_is_terminal(void) {
    fresh(170);
    goto_step(TRAIN_DONE);
    hold(100);
    T_ASSERT(t.step == TRAIN_DONE);
}

static void test_nan_inputs_do_not_advance(void) {
    fresh(170);
    g.b.heading = NAN;
    hold(40);
    T_ASSERT(t.step == TRAIN_DOWNWIND_AUTO);
    fresh(10);
    goto_step(TRAIN_IRONS);
    g.b.u = NAN;
    hold(40);
    T_ASSERT(t.step == TRAIN_IRONS);
    fresh(170);
    training_update(&t, &g, NAN);
    T_NEAR(t.held, 0.0f, 1e-6);
}

int main(void) {
    T_RUN(test_downwind_auto_advances_after_hold);
    T_RUN(test_passing_through_angle_does_not_advance);
    T_RUN(test_auto_step_needs_autotrim_on);
    T_RUN(test_manual_step_needs_autotrim_off);
    T_RUN(test_points_needs_both_close_and_broad_reach);
    T_RUN(test_irons_needs_low_speed_and_pointing_into_wind);
    T_RUN(test_engine_step_needs_engine_on_and_moving_free);
    T_RUN(test_engine_locked_until_engine_step);
    T_RUN(test_no_advance_when_capsized);
    T_RUN(test_done_is_terminal);
    T_RUN(test_nan_inputs_do_not_advance);
    return T_DONE();
}
