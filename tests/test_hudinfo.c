#include "t.h"
#include <math.h>
#include <string.h>
#include "hudinfo.h"
#include "boat.h"
#include "mathx.h"

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

static void test_true_wind_matches_apparent_when_stopped(void) {
    const float headings[] = { 0.4f, -2.5f };
    for (int i = 0; i < 2; i++) {
        BoatState b; memset(&b, 0, sizeof b);
        b.heading = headings[i];
        Wind w; memset(&w, 0, sizeof w);
        w.dir = 2.0f; w.speed = 6.0f;
        float awa, aws;
        boat_apparent_wind(&b, &w, &awa, &aws);
        T_NEAR(hud_true_wind_from_bow(headings[i], 2.0f), awa, 1e-5);
    }
}

static void test_true_wind_head_to_wind(void) {
    T_NEAR(hud_true_wind_from_bow(1.0f, wrap_pi(1.0f + 3.14159265f)), 0.0, 1e-5);
}

static void test_true_wind_non_finite(void) {
    T_NEAR(hud_true_wind_from_bow(NAN, 1.0f), 0.0, 1e-6);
    T_NEAR(hud_true_wind_from_bow(0.5f, INFINITY), 0.0, 1e-6);
}

/* count lit wedge pixels over the dial's square; side < 0: all lit pixels must have dy < 0, > 0: dy > 0 */
static int count_no_go(int r, float center, float half, int side) {
    int n = 0;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (hud_no_go_pixel(dx, dy, r, center, half)) {
                n++;
                if (side < 0) T_ASSERT(dy < 0);
                if (side > 0) T_ASSERT(dy > 0);
            }
    return n;
}

static void test_no_go_pixels_42_deg_on_playdate_dial(void) {
    T_ASSERT(count_no_go(8, 0.0f, deg2rad(42.0f), -1) > 0);
}

static void test_no_go_pixels_symmetric_across_wrap(void) {
    int down = count_no_go(8, 3.14159265f, deg2rad(45.0f), 1);
    T_ASSERT(down > 0);
    T_ASSERT(down == count_no_go(8, 0.0f, deg2rad(45.0f), -1));
    T_ASSERT(count_no_go(8, -3.14159265f, deg2rad(45.0f), 1) == down);
}

static void test_no_go_pixels_stay_inside_ring(void) {
    const int r = 8;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (hud_no_go_pixel(dx, dy, r, 0.3f, deg2rad(90.0f)))
                T_ASSERT(dx * dx + dy * dy < (r - 1) * (r - 1));
    T_ASSERT(!hud_no_go_pixel(0, 0, r, 0.0f, deg2rad(90.0f)));
}

static void test_no_go_pixels_degenerate(void) {
    T_ASSERT(count_no_go(8, 0.0f, 0.0f, 0) == 0);
    T_ASSERT(count_no_go(8, 0.0f, NAN, 0) == 0);
    T_ASSERT(count_no_go(8, NAN, deg2rad(45.0f), 0) == 0);
    T_ASSERT(count_no_go(8, 3.14159265f / 2.0f, 3.14159265f / 2.0f, 0) > 0);
    int side_lit = 0;   /* a 90-degree half facing up also lights pixels level with the centre's sides... */
    for (int dx = -8; dx <= 8; dx++) side_lit += hud_no_go_pixel(dx, 1, 8, 0.0f, 3.14159265f / 2.0f);
    T_ASSERT(side_lit == 0);  /* ...but none below them */
}

static void test_engine_text_while_running(void) {
    T_ASSERT(hud_engine_text(true) != NULL);
    T_ASSERT(strstr(hud_engine_text(true), "ENGINE ENGAGED") != NULL);
    T_ASSERT(hud_engine_text(false) == NULL);
}

static int within(int v, int target, int tol) { return v >= target - tol && v <= target + tol; }

static void test_cluster_matches_mockup(void) {
    HudCluster c = hud_cluster_layout(12, 19, 21, 36, 21, false, false);
    T_ASSERT(c.aws_x + 12 == 376);
    T_ASSERT(within(c.speed_x, 319, 2));
    T_ASSERT(within(c.heel_x, 291, 2));
    T_ASSERT(within(c.hdg_x, 246, 2));
    T_ASSERT(within(c.left, 232, 2));
    T_ASSERT(c.ap_x == -1 && c.at_icon_x == -1);
}

static void test_cluster_worst_case_left_edge(void) {
    HudCluster c = hud_cluster_layout(12, 19, 21, 36, 21, true, true);
    T_ASSERT(within(c.left, 171, 2));
    T_ASSERT(c.at_icon_x < c.ap_icon_x && c.ap_icon_x < c.ap_x && c.ap_x < c.hdg_icon_x);
}

static void test_cluster_never_overlaps(void) {
    for (int wh = 14; wh <= 30; wh++)
        for (int wa = 8; wa <= 30; wa++)
            for (int flags = 0; flags < 4; flags++) {
                const bool at = flags & 1, ap = flags & 2;
                HudCluster c = hud_cluster_layout(wa, wh, 21, 36, 21, at, ap);
                T_ASSERT(c.aws_x + wa <= HUD_DIAL_CX - 8 - 2);
                T_ASSERT(c.speed_x + 36 < c.aws_x);
                T_ASSERT(c.heel_x + wh < c.speed_x);
                T_ASSERT(c.heel_icon_x + HUD_ICON_HEEL_W < c.heel_x);
                T_ASSERT(c.hdg_x + 21 < c.heel_icon_x);
                T_ASSERT(c.hdg_icon_x + HUD_ICON_HDG_W < c.hdg_x);
                if (ap) T_ASSERT(c.ap_x + 21 < c.hdg_icon_x && c.ap_icon_x < c.ap_x);
                if (at) T_ASSERT(c.at_icon_x + 12 < (ap ? c.ap_icon_x : c.hdg_icon_x));
                T_ASSERT(c.left <= c.hdg_icon_x);
            }
}

static void test_sail_bar_span(void) {
    int x, w;
    T_ASSERT(hud_sail_bar_span(24, 173, &x, &w));          /* name shown */
    T_ASSERT(x == 100 && w == 64);
    T_ASSERT(hud_sail_bar_span(50, 150, &x, &w));
    T_ASSERT(x >= 60 + 50 + 4 && x + w <= 150 - 6 && w >= 24);
}

static void test_sail_bar_span_name_yields_to_bar(void) {
    int x, w;
    /* "spinnake" is 65 px in the real font; with AP and AT on the cluster starts near x 105..140 */
    T_ASSERT(!hud_sail_bar_span(65, 140, &x, &w));         /* name dropped, bar keeps room */
    T_ASSERT(x == 60 && w == 64);
    T_ASSERT(!hud_sail_bar_span(65, 105, &x, &w));
    T_ASSERT(x == 60 && w == 39);
    T_ASSERT(!hud_sail_bar_span(65, 70, &x, &w));          /* no room at all: no bar */
    T_ASSERT(w == 0);
}

static void test_helm_span(void) {
    int x, w;
    T_ASSERT(hud_helm_span(173, &x, &w) && x == 60 && w == 79);   /* capped at 80, then made odd */
    T_ASSERT(hud_helm_span(105, &x, &w) && x == 60 && w == 39);   /* already odd */
    T_ASSERT(hud_helm_span(82, &x, &w) && w == 15);               /* 16 -> 15: smallest shown */
    T_ASSERT(!hud_helm_span(80, &x, &w) && w == 0);               /* 14 -> 13: hidden */
    T_ASSERT(!hud_helm_span(70, &x, &w) && w == 0);
}

static void test_helm_span_always_odd_and_clear_of_cluster(void) {
    for (int left = 60; left < 300; left++) {
        int x, w;
        if (!hud_helm_span(left, &x, &w)) continue;
        T_ASSERT(w % 2 == 1 && w <= 80 && x + w <= left - 6);
    }
}

static void test_helm_marker_x(void) {
    T_ASSERT(hud_helm_marker_x(60, 79, 0.0f) == 99);              /* centre */
    T_ASSERT(hud_helm_marker_x(60, 79, 1.0f) == 138);             /* full starboard: right end */
    T_ASSERT(hud_helm_marker_x(60, 79, -1.0f) == 60);             /* full port: left end */
    T_ASSERT(hud_helm_marker_x(60, 79, 0.5f) == 119);
    T_ASSERT(hud_helm_marker_x(60, 79, -0.5f) == 79);             /* symmetric about centre */
}

static void test_helm_marker_clamps_and_survives_non_finite(void) {
    T_ASSERT(hud_helm_marker_x(60, 79, 3.0f) == 138);
    T_ASSERT(hud_helm_marker_x(60, 79, -3.0f) == 60);
    T_ASSERT(hud_helm_marker_x(60, 79, NAN) == 99);
    T_ASSERT(hud_helm_marker_x(60, 79, INFINITY) == 99);
}

static void test_slot_text_is_right_aligned_and_stable(void) {
    /* a number narrower than its slot hugs the slot's right edge; the slot never moves with the digits */
    T_ASSERT(hud_slot_text_x(100, 30, 14) == 116);
    T_ASSERT(hud_slot_text_x(100, 30, 30) == 100);
    T_ASSERT(hud_slot_text_x(100, 30, 40) == 100);         /* wider than the slot: starts at the slot's left */
}

int main(void) {
    T_RUN(test_cluster_matches_mockup);
    T_RUN(test_cluster_worst_case_left_edge);
    T_RUN(test_cluster_never_overlaps);
    T_RUN(test_sail_bar_span);
    T_RUN(test_sail_bar_span_name_yields_to_bar);
    T_RUN(test_helm_span);
    T_RUN(test_helm_span_always_odd_and_clear_of_cluster);
    T_RUN(test_helm_marker_x);
    T_RUN(test_helm_marker_clamps_and_survives_non_finite);
    T_RUN(test_slot_text_is_right_aligned_and_stable);
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
    T_RUN(test_true_wind_matches_apparent_when_stopped);
    T_RUN(test_true_wind_head_to_wind);
    T_RUN(test_true_wind_non_finite);
    T_RUN(test_no_go_pixels_42_deg_on_playdate_dial);
    T_RUN(test_no_go_pixels_symmetric_across_wrap);
    T_RUN(test_no_go_pixels_stay_inside_ring);
    T_RUN(test_no_go_pixels_degenerate);
    return T_DONE();
}
