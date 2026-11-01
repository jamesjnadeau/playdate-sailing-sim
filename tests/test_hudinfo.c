#include "t.h"
#include <math.h>
#include <string.h>
#include "hudinfo.h"
#include "mathx.h"

static void test_shows_nav_for_wheel_and_look(void) {
    T_ASSERT(hud_shows_nav(MODE_WHEEL));
    T_ASSERT(!hud_shows_nav(MODE_TRIM));
    T_ASSERT(!hud_shows_nav(MODE_HOIST));
    T_ASSERT(hud_shows_nav(MODE_CAMERA));
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

static void test_assist_text_only_when_flow_floor_helps_and_wheel_turned(void) {
    T_ASSERT(hud_assist_text(0.8f, 0.5f) != NULL);
    T_ASSERT(strstr(hud_assist_text(0.8f, -0.5f), "RUDDER ASSIST") != NULL);
    T_ASSERT(hud_assist_text(0.0f, 1.0f) == NULL);      /* real flow is enough */
    T_ASSERT(hud_assist_text(0.05f, 1.0f) == NULL);     /* floor barely contributing */
    T_ASSERT(hud_assist_text(0.3f, 1.0f) == NULL);      /* floor still a minor part of the flow */
    T_ASSERT(hud_assist_text(0.5f, 1.0f) == NULL);      /* exactly half is not "mostly the floor" */
    T_ASSERT(hud_assist_text(0.9f, 0.0f) == NULL);      /* helm centred: nothing to assist */
    T_ASSERT(hud_assist_text(0.9f, 0.08f) == NULL);     /* wheel barely off centre */
    T_ASSERT(hud_assist_text(0.9f, -0.08f) == NULL);
    T_ASSERT(hud_assist_text(0.9f, 0.15f) != NULL);
    T_ASSERT(hud_assist_text(NAN, 1.0f) == NULL);
    T_ASSERT(hud_assist_text(0.9f, NAN) == NULL);
}

static void test_dial_angle_unchanged_when_looking_ahead(void) {
    T_NEAR(hud_dial_angle(0.7f, 0.0f), 0.7, 1e-6);
    T_NEAR(hud_dial_angle(0.0f, 0.0f), 0.0, 1e-6);
}

static void test_dial_angle_rotates_against_the_look(void) {
    const float h = 3.14159265f / 2.0f;
    T_NEAR(hud_dial_angle(0.0f, h), -h, 1e-5);
    T_NEAR(hud_dial_angle(0.0f, -h), h, 1e-5);
    T_NEAR(hud_dial_angle(h, h), 0.0, 1e-5);
}

static void test_dial_angle_behind_and_wraps(void) {
    T_NEAR(fabsf(hud_dial_angle(0.0f, 3.14159265f)), 3.14159265, 1e-5);
    float r = hud_dial_angle(3.0f, -3.0f);
    T_ASSERT(r > -3.14159265f && r <= 3.14159265f);
    T_NEAR(r, 6.0 - 2.0 * 3.14159265, 1e-5);
}

static void test_dial_angle_non_finite(void) {
    T_NEAR(hud_dial_angle(0.7f, NAN), 0.7, 1e-6);
    T_NEAR(hud_dial_angle(NAN, 0.3f), 0.0, 1e-6);
    T_NEAR(hud_dial_angle(0.7f, INFINITY), 0.7, 1e-6);
}

static void test_engine_text_while_running(void) {
    T_ASSERT(hud_engine_text(true) != NULL);
    T_ASSERT(strstr(hud_engine_text(true), "ENGINE ENGAGED") != NULL);
    T_ASSERT(hud_engine_text(false) == NULL);
}

int main(void) {
    T_RUN(test_shows_nav_for_wheel_and_look);
    T_RUN(test_heading_wraps_and_rounds);
    T_RUN(test_heading_non_finite_is_zero);
    T_RUN(test_sail_fraction_trim);
    T_RUN(test_sail_fraction_hoist_and_other_modes);
    T_RUN(test_sail_fraction_degenerate);
    T_RUN(test_fmt_time);
    T_RUN(test_fmt_time_truncates);
    T_RUN(test_fmt_mark);
    T_RUN(test_engine_text_while_running);
    T_RUN(test_assist_text_only_when_flow_floor_helps_and_wheel_turned);
    T_RUN(test_dial_angle_unchanged_when_looking_ahead);
    T_RUN(test_dial_angle_rotates_against_the_look);
    T_RUN(test_dial_angle_behind_and_wraps);
    T_RUN(test_dial_angle_non_finite);
    return T_DONE();
}
