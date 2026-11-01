#include "t.h"
#include <math.h>
#include "wake.h"

static void test_wake_length_follows_speed(void) {
    T_NEAR(wake_length(0.0f), 3.0, 1e-6);
    T_NEAR(wake_length(1.0f), 9.0, 1e-6);
    T_NEAR(wake_length(0.5f), 6.0, 1e-6);
    T_NEAR(wake_length(-1.0f), 3.0, 1e-6);
    T_NEAR(wake_length(5.0f), 9.0, 1e-6);
}

static void test_fade_start_is_a_fraction_of_the_full_speed_wake(void) {
    T_NEAR(wake_fade_start(4.5f), 0.5, 1e-6);
    T_NEAR(wake_fade_start(0.0f), 0.0, 1e-6);
    T_ASSERT(wake_fade_start(20.0f) >= 1.0f);
}

static void test_line_is_whole_before_it_starts_to_split(void) {
    float fs[3] = { 0.0f, 0.25f, 0.5f };
    for (int i = 0; i < 3; i++) {
        float lo, hi;
        T_ASSERT(wake_segment(fs[i], 0.5f, 2.0f, &lo, &hi));
        T_NEAR(lo, 0.0, 1e-6);
        T_NEAR(hi, 2.0, 1e-6);
    }
}

static void test_line_splits_then_shrinks_from_both_ends(void) {
    float lo, hi;
    T_ASSERT(wake_segment(0.75f, 0.5f, 2.0f, &lo, &hi));
    T_NEAR(lo, 0.5, 1e-5);
    T_NEAR(hi, 1.5, 1e-5);
    float plo = 0.0f, phi = 2.0f, plen = 2.0f;
    for (float f = 0.5f; f <= 0.951f; f += 0.05f) {
        T_ASSERT(wake_segment(f, 0.5f, 2.0f, &lo, &hi));
        T_ASSERT(lo >= plo - 1e-6f);
        T_ASSERT(hi <= phi + 1e-6f);
        T_ASSERT(hi - lo <= plen + 1e-6f);
        plo = lo; phi = hi; plen = hi - lo;
    }
}

static void test_line_has_vanished_at_the_far_end(void) {
    float lo, hi;
    T_ASSERT(!wake_segment(1.0f, 0.5f, 2.0f, &lo, &hi));
    T_ASSERT(!wake_segment(0.99f, 0.5f, 2.0f, &lo, &hi));
}

static void test_never_splits_when_fade_is_beyond_the_wake(void) {
    float fs[3] = { 0.0f, 0.5f, 1.0f };
    float start = wake_fade_start(20.0f);
    for (int i = 0; i < 3; i++) {
        float lo, hi;
        T_ASSERT(wake_segment(fs[i], start, 2.0f, &lo, &hi));
        T_NEAR(lo, 0.0, 1e-6);
        T_NEAR(hi, 2.0, 1e-6);
    }
}

static void test_zero_fade_splits_from_the_transom(void) {
    float lo, hi, start = wake_fade_start(0.0f);
    T_ASSERT(wake_segment(0.0f, start, 2.0f, &lo, &hi));
    T_NEAR(lo, 0.0, 1e-6);
    T_NEAR(hi, 2.0, 1e-6);
    T_ASSERT(wake_segment(0.5f, start, 2.0f, &lo, &hi));
    T_ASSERT(lo > 0.0f && hi < 2.0f);
    T_ASSERT(isfinite(lo) && isfinite(hi));
}

int main(void) {
    T_RUN(test_wake_length_follows_speed);
    T_RUN(test_fade_start_is_a_fraction_of_the_full_speed_wake);
    T_RUN(test_line_is_whole_before_it_starts_to_split);
    T_RUN(test_line_splits_then_shrinks_from_both_ends);
    T_RUN(test_line_has_vanished_at_the_far_end);
    T_RUN(test_never_splits_when_fade_is_beyond_the_wake);
    T_RUN(test_zero_fade_splits_from_the_transom);
    return T_DONE();
}
