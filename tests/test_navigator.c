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

static void test_reach_leg_heads_straight(void) {
    track_default(&tr);
    tr.nmarks = 1;
    tr.marks[0].x = 100; tr.marks[0].y = 0; tr.marks[0].gate = PI_F;   /* aim point (80, 0) */
    NavState s; nav_init(&s);
    const float h = nav_heading(&s, &tr, 0, 0, 0, 0, 0, PI_F / 2, deg2rad(40));
    T_NEAR(h, 0, 1e-3);
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
    T_RUN(test_reach_leg_heads_straight);
    T_RUN(test_upwind_leg_holds_close_hauled);
    T_RUN(test_tacks_only_beyond_corridor);
    T_RUN(test_advances_with_next);
    return T_DONE();
}
