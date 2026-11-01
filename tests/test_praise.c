#include "t.h"
#include <string.h>
#include "praise.h"

static void test_every_message_is_set(void) {
    for (int i = 0; i < PRAISE_COUNT; i++) {
        const char *s = praise_text(i);
        T_ASSERT(s != NULL && s[0] != '\0');
    }
    T_ASSERT(strcmp(praise_text(0), "Bravo Zulu") == 0);
}

static void test_pick_in_range_and_never_repeats(void) {
    uint32_t rng = 12345u;
    int last = -1;
    for (int k = 0; k < 10000; k++) {
        int i = praise_pick(&rng, last);
        T_ASSERT(i >= 0 && i < PRAISE_COUNT);
        T_ASSERT(i != last);
        last = i;
    }
}

static void test_pick_covers_the_list(void) {
    uint32_t rng = 99u;
    int seen[PRAISE_COUNT] = { 0 };
    int last = -1;
    for (int k = 0; k < 2000; k++) {
        last = praise_pick(&rng, last);
        seen[last] = 1;
    }
    for (int i = 0; i < PRAISE_COUNT; i++) T_ASSERT(seen[i]);
}

static void test_zero_seed_still_advances(void) {
    uint32_t rng = 0u;
    int a = praise_pick(&rng, -1);
    T_ASSERT(rng != 0u);
    T_ASSERT(a >= 0 && a < PRAISE_COUNT);
}

static void test_out_of_range_text_is_safe(void) {
    T_ASSERT(praise_text(-1) != NULL);
    T_ASSERT(praise_text(PRAISE_COUNT) != NULL);
}

int main(void) {
    T_RUN(test_every_message_is_set);
    T_RUN(test_pick_in_range_and_never_repeats);
    T_RUN(test_pick_covers_the_list);
    T_RUN(test_zero_seed_still_advances);
    T_RUN(test_out_of_range_text_is_safe);
    return T_DONE();
}
