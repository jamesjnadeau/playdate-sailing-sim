#include <string.h>
#include "t.h"
#include <math.h>
#include "trainingtext.h"

#define NOTICE "config/training.md is missing text for this step"

static void test_parse_reads_one_block_per_step(void) {
    TrainingText tt;
    const char *md = "# downwind_auto\nfirst\n# beam_auto\nsecond line\nmore\n# done\nlast\n";
    T_ASSERT(trainingtext_parse(md, &tt) == 3);
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_DOWNWIND_AUTO), "first") == 0);
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_BEAM_AUTO), "second line\nmore") == 0);
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_DONE), "last") == 0);
}

static void test_pages_advance_by_the_configured_time(void) {
    TrainingText tt;
    char pg[64];
    trainingtext_parse("seconds: 3\n# beam_auto\none\ntwo\nthree\n# irons\nseconds: 10\nA\nB\n", &tt);
    T_ASSERT(trainingtext_page(&tt, TRAIN_BEAM_AUTO, 0.0f, pg, sizeof pg) == 0 && strcmp(pg, "one") == 0);
    T_ASSERT(trainingtext_page(&tt, TRAIN_BEAM_AUTO, 3.5f, pg, sizeof pg) == 1 && strcmp(pg, "two") == 0);
    T_ASSERT(trainingtext_page(&tt, TRAIN_BEAM_AUTO, 6.1f, pg, sizeof pg) == 2 && strcmp(pg, "three") == 0);
    T_ASSERT(trainingtext_page(&tt, TRAIN_BEAM_AUTO, 99.0f, pg, sizeof pg) == 2 && strcmp(pg, "three") == 0);   /* last stays */
    T_ASSERT(trainingtext_page(&tt, TRAIN_IRONS, 9.0f, pg, sizeof pg) == 0 && strcmp(pg, "A") == 0);   /* per-step override */
    T_ASSERT(trainingtext_page(&tt, TRAIN_IRONS, 10.5f, pg, sizeof pg) == 1 && strcmp(pg, "B") == 0);
    T_ASSERT(trainingtext_page(&tt, TRAIN_DONE, 0.0f, pg, sizeof pg) == 0 && strcmp(pg, NOTICE) == 0);
    trainingtext_parse("# beam_auto\none\ntwo\n", &tt);   /* no setting: the default */
    T_ASSERT(trainingtext_page(&tt, TRAIN_BEAM_AUTO, TRAINTEXT_DEFAULT_S + 0.1f, pg, sizeof pg) == 1);
}

static void test_missing_blocks_fall_back_to_notice(void) {
    TrainingText tt;
    trainingtext_parse("# beam_auto\nx\n# irons\n\n", &tt);
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_POINTS_AUTO), NOTICE) == 0);
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_IRONS), NOTICE) == 0);   /* empty body */
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_BEAM_AUTO), "x") == 0);
}

static void test_long_body_is_truncated_not_overflowed(void) {
    TrainingText tt;
    char md[1024] = "# downwind_auto\n";
    size_t n = strlen(md);
    memset(md + n, 'a', 600);
    md[n + 600] = 0;
    trainingtext_parse(md, &tt);
    T_ASSERT(strlen(trainingtext_get(&tt, TRAIN_DOWNWIND_AUTO)) == TRAINTEXT_LEN - 1);
    T_ASSERT(trainingtext_get(&tt, (TrainStep)99) != NULL);   /* out of range is still safe */
}

static void test_null_or_empty_input_gives_all_notices(void) {
    TrainingText tt;
    T_ASSERT(trainingtext_parse(NULL, &tt) == 0);
    for (int s = 0; s < TRAIN_STEPS; s++) T_ASSERT(strcmp(trainingtext_get(&tt, (TrainStep)s), NOTICE) == 0);
    T_ASSERT(trainingtext_parse("", &tt) == 0);
    for (int s = 0; s < TRAIN_STEPS; s++) T_ASSERT(strcmp(trainingtext_get(&tt, (TrainStep)s), NOTICE) == 0);
}


static void test_require_lines_set_the_goal(void) {
    TrainingText tt;
    trainingtext_parse("# beam_manual\nrequire: autotrim off, autopilot on, wind 60..120\nrequire: speed 1.5.., mode trim\n"
                       "hold: 3\nTry it\n# irons\nrequire: engine off, speed ..0.2, wind ..25\n", &tt);
    const TrainGoal *q = &tt.goal[TRAIN_BEAM_MANUAL];
    T_ASSERT(q->autotrim == 0 && q->autopilot == 1 && q->engine == -1 && q->mode == MODE_TRIM);
    T_NEAR(q->wind_lo, 60, 1e-6); T_NEAR(q->wind_hi, 120, 1e-6);
    T_NEAR(q->speed_lo, 1.5, 1e-6); T_ASSERT(isinf(q->speed_hi));
    T_NEAR(q->hold_s, 3, 1e-6);
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_BEAM_MANUAL), "Try it") == 0);   /* settings are not text */
    q = &tt.goal[TRAIN_IRONS];
    T_ASSERT(q->engine == 0 && q->autotrim == -1);
    T_NEAR(q->wind_lo, 0, 1e-6); T_NEAR(q->wind_hi, 25, 1e-6);
    T_NEAR(q->speed_hi, 0.2, 1e-6);
    T_ASSERT(tt.bad_line == 0);
    /* a step the file gives no goal keeps the built-in one */
    TrainGoal d;
    training_default_goal(TRAIN_BEAM_AUTO, &d);
    T_ASSERT(memcmp(&d, &tt.goal[TRAIN_BEAM_AUTO], sizeof d) == 0);
}

static void test_visit_lines_add_ranges(void) {
    TrainingText tt;
    trainingtext_parse("# points_auto\nvisit: 40..60\nvisit: 120..\n", &tt);
    const TrainGoal *q = &tt.goal[TRAIN_POINTS_AUTO];
    T_ASSERT(q->nvisit == 2 && q->autotrim == -1);   /* the file's goal replaced the built-in autotrim-on */
    T_NEAR(q->visit_lo[1], 120, 1e-6); T_NEAR(q->visit_hi[1], 180, 1e-6);
}

static void test_bad_setting_lines_are_reported(void) {
    TrainingText tt;
    trainingtext_parse("# irons\nok text\nrequire: autotrim maybe\nrequire: wind 90..30\n", &tt);
    T_ASSERT(tt.bad_line == 3);
    trainingtext_parse("# irons\nrequire: warp 9\n", &tt);
    T_ASSERT(tt.bad_line == 2);
    trainingtext_parse("hint_after: soon\n", &tt);
    T_ASSERT(tt.bad_line == 1);
}

static void test_hint_waits_for_last_page_and_time_off_task(void) {
    TrainingText tt;
    trainingtext_parse("seconds: 2\nhint_after: 5\n# beam_auto\nhint: Turn the boat\none\ntwo\n# irons\nhint_after: 1\nhint: Stop\nx\n"
                       "# close_hauled\nno hint here\n", &tt);
    /* the last page goes up at 2 s; the hint needs it up 5 s and the goal missed 5 s */
    T_ASSERT(trainingtext_hint(&tt, TRAIN_BEAM_AUTO, 6.0f, 99.0f) == NULL);
    T_ASSERT(trainingtext_hint(&tt, TRAIN_BEAM_AUTO, 7.0f, 4.0f) == NULL);
    T_ASSERT(strcmp(trainingtext_hint(&tt, TRAIN_BEAM_AUTO, 7.0f, 5.0f), "Turn the boat") == 0);
    T_ASSERT(strcmp(trainingtext_hint(&tt, TRAIN_IRONS, 1.0f, 1.0f), "Stop") == 0);   /* per-step hint_after */
    T_ASSERT(trainingtext_hint(&tt, TRAIN_CLOSE_HAULED, 99.0f, 99.0f) == NULL);
    T_ASSERT(trainingtext_hint(&tt, (TrainStep)99, 99.0f, 99.0f) == NULL);
    trainingtext_parse("# irons\nhint: Stop\nx\n", &tt);   /* no setting: the default */
    T_ASSERT(trainingtext_hint(&tt, TRAIN_IRONS, TRAINTEXT_HINT_AFTER_S - 0.1f, 99.0f) == NULL);
    T_ASSERT(trainingtext_hint(&tt, TRAIN_IRONS, TRAINTEXT_HINT_AFTER_S, 99.0f) != NULL);
}

static void test_shipped_file_has_a_block_for_every_step(void) {
    static char buf[8192];
    FILE *f = fopen("../config/training.md", "rb");
    T_ASSERT(f != NULL);
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    TrainingText tt;
    T_ASSERT(trainingtext_parse(buf, &tt) == TRAIN_STEPS);
    for (int s = 0; s < TRAIN_STEPS; s++) T_ASSERT(strcmp(trainingtext_get(&tt, (TrainStep)s), NOTICE) != 0);
    T_ASSERT(tt.bad_line == 0);
    /* the shipped goals are the built-in ones written out, and every step but the last has a hint */
    for (int s = 0; s < TRAIN_STEPS; s++) {
        TrainGoal d;
        training_default_goal((TrainStep)s, &d);
        T_ASSERT(memcmp(&d, &tt.goal[s], sizeof d) == 0);
        T_ASSERT(s == TRAIN_DONE || tt.hint[s][0]);
    }
}

int main(void) {
    T_RUN(test_parse_reads_one_block_per_step);
    T_RUN(test_pages_advance_by_the_configured_time);
    T_RUN(test_missing_blocks_fall_back_to_notice);
    T_RUN(test_long_body_is_truncated_not_overflowed);
    T_RUN(test_null_or_empty_input_gives_all_notices);
    T_RUN(test_require_lines_set_the_goal);
    T_RUN(test_visit_lines_add_ranges);
    T_RUN(test_bad_setting_lines_are_reported);
    T_RUN(test_hint_waits_for_last_page_and_time_off_task);
    T_RUN(test_shipped_file_has_a_block_for_every_step);
    return T_DONE();
}
