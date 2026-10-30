#include "t.h"
#include <math.h>
#include <string.h>
#include "hudinfo.h"
#include "mathx.h"

static void test_shows_nav_only_for_wheel(void) {
    T_ASSERT(hud_shows_nav(MODE_WHEEL));
    T_ASSERT(!hud_shows_nav(MODE_TRIM));
    T_ASSERT(!hud_shows_nav(MODE_HOIST));
    T_ASSERT(!hud_shows_nav(MODE_CAMERA));
}

static void test_heading_wraps_and_rounds(void) {
    T_ASSERT(hud_heading_deg(0.0f) == 0);
    T_ASSERT(hud_heading_deg(deg2rad(359.6f)) == 0);
    T_ASSERT(hud_heading_deg(deg2rad(359.4f)) == 359);
    T_ASSERT(hud_heading_deg(2.0f * 3.14159265f) == 0);
    T_ASSERT(hud_heading_deg(-deg2rad(90.0f)) == 270);
    T_ASSERT(hud_heading_deg(-0.0f) == 0);
}

static void test_heading_non_finite_is_zero(void) {
    T_ASSERT(hud_heading_deg(NAN) == 0);
    T_ASSERT(hud_heading_deg(INFINITY) == 0);
    T_ASSERT(hud_heading_deg(-INFINITY) == 0);
}

static void test_sail_fraction_trim(void) {
    SailCfg s;
    memset(&s, 0, sizeof s);
    s.trim_min_deg = 0;
    s.trim_max_deg = 90;
    T_NEAR(hud_sail_fraction(MODE_TRIM, &s, deg2rad(45.0f), 0), 0.5, 1e-4);
    T_NEAR(hud_sail_fraction(MODE_TRIM, &s, deg2rad(-30.0f), 0), 0.0, 1e-6);
    T_NEAR(hud_sail_fraction(MODE_TRIM, &s, deg2rad(120.0f), 0), 1.0, 1e-6);
}

static void test_sail_fraction_hoist_and_other_modes(void) {
    SailCfg s;
    memset(&s, 0, sizeof s);
    T_NEAR(hud_sail_fraction(MODE_HOIST, &s, 0, 0.25f), 0.25, 1e-6);
    T_NEAR(hud_sail_fraction(MODE_HOIST, &s, 0, 1.5f), 1.0, 1e-6);
    T_NEAR(hud_sail_fraction(MODE_WHEEL, &s, deg2rad(45.0f), 0.5f), 0.0, 0);
    T_NEAR(hud_sail_fraction(MODE_CAMERA, &s, deg2rad(45.0f), 0.5f), 0.0, 0);
}

static void test_sail_fraction_degenerate(void) {
    SailCfg s;
    memset(&s, 0, sizeof s);
    s.trim_min_deg = 30;
    s.trim_max_deg = 30;
    float f = hud_sail_fraction(MODE_TRIM, &s, deg2rad(30.0f), 0);
    T_ASSERT(isfinite(f) && f >= 0.0f && f <= 1.0f);
    T_NEAR(f, 0.0, 0);
    s.trim_max_deg = 90;
    T_NEAR(hud_sail_fraction(MODE_TRIM, &s, NAN, 0), 0.0, 0);
    T_NEAR(hud_sail_fraction(MODE_HOIST, &s, 0, NAN), 0.0, 0);
}

static void test_fmt_time(void) {
    char b[16];
    hud_fmt_time(0.0, b, sizeof b);      T_ASSERT(strcmp(b, "00:00.0") == 0);
    hud_fmt_time(75.34, b, sizeof b);    T_ASSERT(strcmp(b, "01:15.3") == 0);
    hud_fmt_time(-5.0, b, sizeof b);     T_ASSERT(strcmp(b, "00:00.0") == 0);
    hud_fmt_time(NAN, b, sizeof b);      T_ASSERT(strcmp(b, "00:00.0") == 0);
}

static void test_fmt_time_truncates(void) {
    char b[4];
    hud_fmt_time(7260.0, b, sizeof b);   /* 121:00.0 does not fit */
    T_ASSERT(strlen(b) == 3);
}

static void test_fmt_mark(void) {
    char b[32];
    hud_fmt_mark("B1", 2, 5, 340.4, b, sizeof b);   T_ASSERT(strcmp(b, "B1 2/5  340m") == 0);
    hud_fmt_mark("B1", 2, 5, NAN, b, sizeof b);     T_ASSERT(strcmp(b, "B1 2/5  0m") == 0);
}

int main(void) {
    T_RUN(test_shows_nav_only_for_wheel);
    T_RUN(test_heading_wraps_and_rounds);
    T_RUN(test_heading_non_finite_is_zero);
    T_RUN(test_sail_fraction_trim);
    T_RUN(test_sail_fraction_hoist_and_other_modes);
    T_RUN(test_sail_fraction_degenerate);
    T_RUN(test_fmt_time);
    T_RUN(test_fmt_time_truncates);
    T_RUN(test_fmt_mark);
    return T_DONE();
}
