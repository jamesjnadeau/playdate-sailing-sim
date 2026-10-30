#include "t.h"
#include <math.h>
#include <string.h>
#include "hudinfo.h"
#include "mathx.h"

static void test_glyph_per_mode(void) {
    T_ASSERT(strcmp(hud_mode_glyph(MODE_WHEEL), "\xE2\xAC\x86") == 0);   /* U+2B06 up */
    T_ASSERT(strcmp(hud_mode_glyph(MODE_TRIM), "\xE2\x9E\xA1") == 0);    /* U+27A1 right */
    T_ASSERT(strcmp(hud_mode_glyph(MODE_HOIST), "\xE2\xAC\x85") == 0);   /* U+2B05 left */
    T_ASSERT(strcmp(hud_mode_glyph(MODE_CAMERA), "\xE2\xAC\x87") == 0);  /* U+2B07 down */
    T_ASSERT(strcmp(hud_mode_glyph((Mode)99), "?") == 0);
}

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

int main(void) {
    T_RUN(test_glyph_per_mode);
    T_RUN(test_shows_nav_only_for_wheel);
    T_RUN(test_heading_wraps_and_rounds);
    T_RUN(test_heading_non_finite_is_zero);
    T_RUN(test_sail_fraction_trim);
    T_RUN(test_sail_fraction_hoist_and_other_modes);
    T_RUN(test_sail_fraction_degenerate);
    return T_DONE();
}
