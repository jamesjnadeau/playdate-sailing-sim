#include "t.h"
#include "mathx.h"

#define PIF 3.14159265f

static void test_wrap_pi_folds_into_half_open_range(void) {
    T_NEAR(wrap_pi(3.0f * PIF), PIF, 1e-4);
    T_NEAR(wrap_pi(-1.5f * PIF), 0.5f * PIF, 1e-4);
    T_NEAR(wrap_pi(0.25f), 0.25f, 1e-6);
    T_NEAR(wrap_pi(-PIF), PIF, 1e-4);
    T_ASSERT(wrap_pi(1000.0f) > -PIF - 1e-3f && wrap_pi(1000.0f) <= PIF + 1e-3f);
}

static void test_clampf_and_deg2rad(void) {
    T_NEAR(clampf(5, 0, 1), 1, 0);
    T_NEAR(clampf(-5, 0, 1), 0, 0);
    T_NEAR(clampf(0.5f, 0, 1), 0.5f, 0);
    T_NEAR(deg2rad(180.0f), PIF, 1e-5);
}

static void test_table_eval(void) {
    const Pt t[] = {{0, 0}, {10, 100}};
    T_NEAR(table_eval(t, 2, 5), 50, 1e-4);
    T_NEAR(table_eval(t, 2, -3), 0, 1e-6);
    T_NEAR(table_eval(t, 2, 99), 100, 1e-6);
    T_NEAR(table_eval(t, 0, 1), 0, 0);
    T_NEAR(table_eval(t, 1, 7), 0, 1e-6);
}

static void test_hash_deterministic_and_ordered(void) {
    T_ASSERT(hash2(1, 2, 7) == hash2(1, 2, 7));
    T_ASSERT(hash2(1, 2, 7) != hash2(2, 1, 7));
    T_ASSERT(hash2(1, 2, 7) != hash2(1, 2, 8));
}

static void test_hashf_in_unit_interval(void) {
    for (uint32_t i = 0; i < 10000; i++) {
        float f = hashf(hash2((int32_t)i, -(int32_t)i, 3));
        T_ASSERT(f >= 0.0f && f < 1.0f);
    }
}

int main(void) {
    T_RUN(test_wrap_pi_folds_into_half_open_range);
    T_RUN(test_clampf_and_deg2rad);
    T_RUN(test_table_eval);
    T_RUN(test_hash_deterministic_and_ordered);
    T_RUN(test_hashf_in_unit_interval);
    return T_DONE();
}
