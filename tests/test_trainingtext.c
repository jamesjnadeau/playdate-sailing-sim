#include <string.h>
#include "t.h"
#include "trainingtext.h"

#define NOTICE "config/training.md is missing text for this step"

static void test_parse_reads_one_block_per_step(void) {
    TrainingText tt;
    const char *md = "# downwind_auto\nfirst\n# beam_auto\nsecond line\nmore\n# done\nlast\n";
    T_ASSERT(trainingtext_parse(md, &tt) == 3);
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_DOWNWIND_AUTO), "first") == 0);
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_BEAM_AUTO), "second line more") == 0);
    T_ASSERT(strcmp(trainingtext_get(&tt, TRAIN_DONE), "last") == 0);
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
}

int main(void) {
    T_RUN(test_parse_reads_one_block_per_step);
    T_RUN(test_missing_blocks_fall_back_to_notice);
    T_RUN(test_long_body_is_truncated_not_overflowed);
    T_RUN(test_null_or_empty_input_gives_all_notices);
    T_RUN(test_shipped_file_has_a_block_for_every_step);
    return T_DONE();
}
