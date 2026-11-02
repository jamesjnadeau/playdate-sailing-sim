#include "t.h"
#include <string.h>
#include "besttimes.h"

static void test_empty_has_no_time(void) {
    BestTimes b;
    besttimes_init(&b);
    double t;
    T_ASSERT(!besttimes_get(&b, "Windward", &t));
}

static void test_record_keeps_only_the_faster_time(void) {
    BestTimes b;
    besttimes_init(&b);
    double t;
    T_ASSERT(besttimes_record(&b, "Windward", 120.0));
    T_ASSERT(!besttimes_record(&b, "Windward", 130.0));
    T_ASSERT(!besttimes_record(&b, "Windward", 120.0));
    T_ASSERT(besttimes_get(&b, "Windward", &t));
    T_NEAR(t, 120.0, 1e-9);
    T_ASSERT(besttimes_record(&b, "Windward", 99.5));
    T_ASSERT(besttimes_get(&b, "Windward", &t));
    T_NEAR(t, 99.5, 1e-9);
}

static void test_courses_are_independent(void) {
    BestTimes b;
    besttimes_init(&b);
    double t;
    besttimes_record(&b, "A", 50.0);
    besttimes_record(&b, "B", 70.0);
    T_ASSERT(besttimes_get(&b, "A", &t));
    T_NEAR(t, 50.0, 1e-9);
    T_ASSERT(besttimes_get(&b, "B", &t));
    T_NEAR(t, 70.0, 1e-9);
    T_ASSERT(!besttimes_get(&b, "C", &t));
}

static void test_rejects_bad_times(void) {
    BestTimes b;
    besttimes_init(&b);
    T_ASSERT(!besttimes_record(&b, "A", 0.0));
    T_ASSERT(!besttimes_record(&b, "A", -3.0));
    T_ASSERT(!besttimes_record(&b, "A", NAN));
    T_ASSERT(!besttimes_record(&b, "A", INFINITY));
    T_ASSERT(!besttimes_record(&b, NULL, 10.0));
    T_ASSERT(!besttimes_record(&b, "", 10.0));
    double t;
    T_ASSERT(!besttimes_get(&b, "A", &t));
}

static void test_full_table_drops_new_courses_but_still_improves_old_ones(void) {
    BestTimes b;
    besttimes_init(&b);
    char name[16];
    for (int i = 0; i < BESTTIMES_MAX; i++) {
        snprintf(name, sizeof name, "c%d", i);
        T_ASSERT(besttimes_record(&b, name, 100.0));
    }
    T_ASSERT(!besttimes_record(&b, "extra", 10.0));
    T_ASSERT(besttimes_record(&b, "c0", 90.0));
}

static void test_save_and_load_round_trip(void) {
    BestTimes a, b;
    besttimes_init(&a);
    besttimes_record(&a, "Windward Leeward", 123.4);
    besttimes_record(&a, "Triangle", 88.0);
    char buf[512];
    T_ASSERT(besttimes_save(&a, buf, sizeof buf) > 0);
    besttimes_init(&b);
    besttimes_load(&b, buf);
    double t;
    T_ASSERT(besttimes_get(&b, "Windward Leeward", &t));
    T_NEAR(t, 123.4, 1e-3);
    T_ASSERT(besttimes_get(&b, "Triangle", &t));
    T_NEAR(t, 88.0, 1e-3);
}

static void test_load_skips_garbage(void) {
    BestTimes b;
    besttimes_init(&b);
    besttimes_load(&b, "junk\n\t5\nA\tabc\nB\t-4\nC\t61.5\nD\tnan\n");
    double t;
    T_ASSERT(!besttimes_get(&b, "A", &t));
    T_ASSERT(!besttimes_get(&b, "B", &t));
    T_ASSERT(!besttimes_get(&b, "D", &t));
    T_ASSERT(besttimes_get(&b, "C", &t));
    T_NEAR(t, 61.5, 1e-9);
    besttimes_load(&b, NULL);
}

static void test_save_reports_truncation(void) {
    BestTimes a;
    besttimes_init(&a);
    besttimes_record(&a, "A long course name", 12.0);
    char buf[8];
    T_ASSERT(besttimes_save(&a, buf, sizeof buf) < 0);
}

int main(void) {
    T_RUN(test_empty_has_no_time);
    T_RUN(test_record_keeps_only_the_faster_time);
    T_RUN(test_courses_are_independent);
    T_RUN(test_rejects_bad_times);
    T_RUN(test_full_table_drops_new_courses_but_still_improves_old_ones);
    T_RUN(test_save_and_load_round_trip);
    T_RUN(test_load_skips_garbage);
    T_RUN(test_save_reports_truncation);
    return T_DONE();
}
