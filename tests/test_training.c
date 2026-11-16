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

static void goto_step(TrainStep s) { t.step = s; t.held = 0; t.seen = 0; t.visiting = -1; t.miss = 0; }

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
    g.ap.on = true;
    g.ctl.mode = MODE_TRIM;
    hold(25);
    T_ASSERT(t.step == TRAIN_BEAM_MANUAL);
}

static void test_downwind_manual_needs_autopilot_trim_mode_and_no_engine(void) {
    fresh(170);
    goto_step(TRAIN_DOWNWIND_MANUAL);
    g.at.on = false;
    g.ap.on = false;                                /* steering by hand */
    g.ctl.mode = MODE_TRIM;
    hold(40);
    T_ASSERT(t.step == TRAIN_DOWNWIND_MANUAL);
    g.ap.on = true;
    g.ctl.mode = MODE_WHEEL;                        /* the crank is not on the sail */
    hold(40);
    T_ASSERT(t.step == TRAIN_DOWNWIND_MANUAL);
    g.ctl.mode = MODE_TRIM;
    g.en.on = true;                                 /* motoring is not sailing */
    hold(40);
    T_ASSERT(t.step == TRAIN_DOWNWIND_MANUAL);
    g.en.on = false;
    hold(25);
    T_ASSERT(t.step == TRAIN_BEAM_MANUAL);
}

static void test_manual_steps_need_engine_off(void) {
    fresh(90);
    goto_step(TRAIN_BEAM_MANUAL);
    g.at.on = false;
    g.en.on = true;
    hold(40);
    T_ASSERT(t.step == TRAIN_BEAM_MANUAL);
    g.en.on = false;
    hold(25);
    T_ASSERT(t.step == TRAIN_POINTS_MANUAL);
}

static void test_points_auto_needs_the_helm(void) {
    fresh(60);
    goto_step(TRAIN_POINTS_AUTO);
    g.ctl.mode = MODE_CAMERA;
    hold(40);
    T_ASSERT(t.seen == 0u);
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


static void test_custom_goal_checks_autopilot_and_mode(void) {
    static TrainGoal goals[TRAIN_STEPS];
    for (int s = 0; s < TRAIN_STEPS; s++) training_default_goal((TrainStep)s, &goals[s]);
    training_goal_any(&goals[TRAIN_DOWNWIND_AUTO]);
    goals[TRAIN_DOWNWIND_AUTO].autopilot = 1;
    goals[TRAIN_DOWNWIND_AUTO].mode = MODE_TRIM;
    goals[TRAIN_DOWNWIND_AUTO].hold_s = 1.0f;
    fresh(170);
    t.goals = goals;
    g.ap.on = false;
    g.ctl.mode = MODE_TRIM;
    hold(30);
    T_ASSERT(t.step == TRAIN_DOWNWIND_AUTO);
    g.ap.on = true;
    g.ctl.mode = MODE_WHEEL;
    hold(30);
    T_ASSERT(t.step == TRAIN_DOWNWIND_AUTO);
    g.ctl.mode = MODE_TRIM;
    hold(12);
    T_ASSERT(t.step == TRAIN_BEAM_AUTO);
}

static void test_miss_counts_time_off_task_and_resets(void) {
    fresh(170);
    g.at.on = false;
    hold(50);
    T_NEAR(t.miss, 5.0f, 1e-4);
    g.at.on = true;
    hold(1);
    T_NEAR(t.miss, 0.0f, 1e-6);
    T_ASSERT(training_goal_met(&t, &g));
}

static void test_visit_already_done_counts_as_off_task(void) {
    fresh(60);
    goto_step(TRAIN_POINTS_AUTO);
    hold(25);
    T_ASSERT(t.seen == 1u);
    const float before = t.miss;
    hold(10);                                       /* still on the close reach, already seen */
    T_ASSERT(!training_goal_met(&t, &g));
    T_NEAR(t.miss - before, 1.0f, 1e-4);
}

static void test_adjacent_visits_hold_separately(void) {
    static TrainGoal goals[TRAIN_STEPS];
    for (int s = 0; s < TRAIN_STEPS; s++) training_default_goal((TrainStep)s, &goals[s]);
    TrainGoal *q = &goals[TRAIN_DOWNWIND_AUTO];
    training_goal_any(q);
    q->nvisit = 2;
    q->visit_lo[0] = 90; q->visit_hi[0] = 120;
    q->visit_lo[1] = 120; q->visit_hi[1] = 150;
    fresh(110);
    t.goals = goals;
    hold(15);                                       /* 1.5 s in the first range */
    g.b.heading = wrap_pi((float)M_PI + 130.0f * (float)M_PI / 180.0f);
    hold(10);                                       /* 1 s in the second: not 2.5 s carried over */
    T_ASSERT(t.seen == 0u);
    hold(15);
    T_ASSERT(t.seen == 2u);
    g.b.heading = wrap_pi((float)M_PI + 100.0f * (float)M_PI / 180.0f);
    hold(25);
    T_ASSERT(t.step == TRAIN_BEAM_AUTO);
}

int main(void) {
    T_RUN(test_downwind_auto_advances_after_hold);
    T_RUN(test_passing_through_angle_does_not_advance);
    T_RUN(test_auto_step_needs_autotrim_on);
    T_RUN(test_manual_step_needs_autotrim_off);
    T_RUN(test_downwind_manual_needs_autopilot_trim_mode_and_no_engine);
    T_RUN(test_manual_steps_need_engine_off);
    T_RUN(test_points_auto_needs_the_helm);
    T_RUN(test_points_needs_both_close_and_broad_reach);
    T_RUN(test_irons_needs_low_speed_and_pointing_into_wind);
    T_RUN(test_engine_step_needs_engine_on_and_moving_free);
    T_RUN(test_engine_locked_until_engine_step);
    T_RUN(test_no_advance_when_capsized);
    T_RUN(test_done_is_terminal);
    T_RUN(test_nan_inputs_do_not_advance);
    T_RUN(test_custom_goal_checks_autopilot_and_mode);
    T_RUN(test_miss_counts_time_off_task_and_resets);
    T_RUN(test_visit_already_done_counts_as_off_task);
    T_RUN(test_adjacent_visits_hold_separately);
    return T_DONE();
}
