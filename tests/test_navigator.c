#include "t.h"
#include "navigator.h"
#include "mathx.h"

#define PI_F 3.14159265358979f
static Track tr;

/* Start at the origin, one mark far to the north (upwind when the wind is from +y). */
static void upwind_track(void) {
    track_default(&tr);
    tr.nmarks = 1;
    tr.start_x = 0; tr.start_y = 0;
    tr.marks[0].x = 0; tr.marks[0].y = 1000;
    tr.marks[0].gate = PI_F / 2 * -1;   /* aim point 20 m south of the mark */
}

static void test_aim_point_is_20m_along_gate(void) {
    Mark m = { "A", 10, 5, 0, ROUND_PORT };
    float x, y;
    nav_aim_point(&m, NAV_AIM_OFFSET_M, &x, &y);
    T_NEAR(x, 30, 1e-3); T_NEAR(y, 5, 1e-3);
    m.gate = PI_F / 2;
    nav_aim_point(&m, NAV_AIM_OFFSET_M, &x, &y);
    T_NEAR(x, 10, 1e-3); T_NEAR(y, 25, 1e-3);
}

/* Mark at the origin, gate ray pointing west (the way the boat is sailing), rounded to port: the boat must
 * come in on the left of the ray, which is south here, and cross the ray 20 m out. */
static void run_mark_track(void) {
    track_default(&tr);
    tr.nmarks = 1;
    tr.start_x = 100; tr.start_y = 0;
    tr.marks[0].x = 0; tr.marks[0].y = 0; tr.marks[0].gate = PI_F; tr.marks[0].rounding = ROUND_PORT;
}

static void test_approach_is_abeam_not_through_the_mark(void) {
    run_mark_track();
    NavState s; nav_init(&s);
    const float wind = -PI_F / 2, no_go = deg2rad(40);
    /* far away: head for the corner 20 m past the mark and NAV_APPROACH_OFFSET_M to the port-rounding side */
    const float h = nav_heading(&s, &tr, 0, 100, 0, 0, 0, wind, no_go);
    T_NEAR(h, atan2f(-NAV_APPROACH_OFFSET_M, -100 - NAV_AIM_OFFSET_M), 1e-3);
    /* once far enough along the ray, cross it at the aim point (-20, 0) */
    const float h2 = nav_heading(&s, &tr, 0, -(NAV_SWITCH_ALONG_M + 1), -NAV_APPROACH_OFFSET_M, 0, 0, wind, no_go);
    T_NEAR(h2, atan2f(NAV_APPROACH_OFFSET_M, -NAV_AIM_OFFSET_M + NAV_SWITCH_ALONG_M + 1), 1e-3);
}

static void test_upwind_leg_holds_close_hauled(void) {
    upwind_track();
    NavState s; nav_init(&s);
    const float wind = PI_F / 2, no_go = deg2rad(40);
    const float h0 = nav_heading(&s, &tr, 0, 0, 0, 0, 0, wind, no_go);
    T_NEAR(fabsf(wrap_pi(h0 - wind)), no_go + NAV_UPWIND_MARGIN_RAD, 1e-4);
    for (int i = 1; i <= 10; i++) {
        const float h = nav_heading(&s, &tr, 0, 0.5 * i, 2.0 * i, 0, 0, wind, no_go);
        T_NEAR(h, h0, 1e-4);
    }
}

static void test_tacks_only_beyond_corridor(void) {
    upwind_track();
    NavState s; nav_init(&s);
    const float wind = PI_F / 2, no_go = deg2rad(40);
    nav_heading(&s, &tr, 0, 0, 0, 0, 0, wind, no_go);
    const int first = s.side;
    const double corridor = NAV_CORRIDOR_FRAC * (1000.0 - NAV_AIM_OFFSET_M);
    int flips = 0, at = 0;
    /* drift away from the leg on the side this tack heads */
    const double dir = first > 0 ? -1.0 : 1.0;   /* wind +y: side +1 points up-left */
    for (int i = 1; i <= 400; i++) {
        const int before = s.side;
        nav_heading(&s, &tr, 0, dir * i, 100, 0, 0, wind, no_go);
        if (s.side != before) { flips++; at = i; }
    }
    T_ASSERT(flips == 1);
    T_ASSERT(at > corridor && at <= corridor + 1.5);
}

/* Wind from +x, windward mark at (100, 0) rounded to port with its ray pointing upwind. */
static void test_beat_tacks_off_a_collision_course(void) {
    track_default(&tr);
    tr.nmarks = 1;
    tr.start_x = -30; tr.start_y = 20;
    tr.marks[0].x = 100; tr.marks[0].y = 0; tr.marks[0].gate = 0; tr.marks[0].rounding = ROUND_PORT;
    const float wind = 0, no_go = deg2rad(40);
    const float limit = no_go + NAV_UPWIND_MARGIN_RAD;
    NavState s; nav_init(&s);
    s.side = -1;   /* sailing south-east, away from the corner on the north side */
    /* far from the mark that tack misses it by more than the clearance, so it holds */
    T_NEAR(nav_heading(&s, &tr, 0, 30, 15, 0, 0, wind, no_go), -limit, 1e-4);
    /* closer in, that tack would pass the buoy: tack away from it */
    T_NEAR(nav_heading(&s, &tr, 0, 80, 12, 0, 0, wind, no_go), limit, 1e-4);
    T_ASSERT(s.side == 1);
}

static void test_advances_with_next(void) {
    track_default(&tr);
    tr.nmarks = 2;
    tr.start_x = 0; tr.start_y = 0;
    tr.marks[0].x = 100; tr.marks[0].y = 0;   tr.marks[0].gate = PI_F;      /* aim (80, 0) */
    tr.marks[1].x = 100; tr.marks[1].y = 200; tr.marks[1].gate = PI_F;      /* aim (80, 200) */
    NavState s; nav_init(&s);
    const float wind = -PI_F / 2;   /* from -y: both legs are reaches or runs */
    const float h0 = nav_heading(&s, &tr, 0, 0, 0, 0, 0, wind, deg2rad(40));
    T_NEAR(h0, 0, 1e-3);
    const float h1 = nav_heading(&s, &tr, 1, 80, 0, 0, 0, wind, deg2rad(40));
    T_NEAR(h1, PI_F / 2, 1e-3);
    /* floating origin: local position + origin is the track position */
    const float h2 = nav_heading(&s, &tr, 1, 30, -10, 50, 10, wind, deg2rad(40));
    T_NEAR(h2, PI_F / 2, 1e-3);
}

int main(void) {
    T_RUN(test_aim_point_is_20m_along_gate);
    T_RUN(test_approach_is_abeam_not_through_the_mark);
    T_RUN(test_upwind_leg_holds_close_hauled);
    T_RUN(test_beat_tacks_off_a_collision_course);
    T_RUN(test_tacks_only_beyond_corridor);
    T_RUN(test_advances_with_next);
    return T_DONE();
}
