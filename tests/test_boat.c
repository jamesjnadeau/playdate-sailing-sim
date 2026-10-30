#include "t.h"
#include <string.h>
#include "boat.h"
#include <math.h>

#define D(x) deg2rad(x)
#define KN 0.514444f
#define DT SIM_DT

typedef struct {
    BoatCfg cfg;
    BoatState b;
    Wind w;
    BoatInputs in;
    WaveForces wf;
    float target;      /* heading autopilot target (rad) */
    int autopilot, autotrim;
    float aoa_trim;    /* deg, AoA the auto-trim aims for */
    float t;
} Sim;

/* Wind blows toward +x (dir 0), so it comes from world angle pi. twa>0: wind on starboard. */
static float twa_of(const Sim *s) { return wrap_pi((float)M_PI - s->b.heading); }

static void sim_init(Sim *s, float twa_deg, float wind_ms, float u0) {
    memset(s, 0, sizeof *s);
    boatcfg_default(&s->cfg);
    boat_init(&s->b, &s->cfg);
    s->w.dir = 0; s->w.speed = wind_ms;
    s->b.heading = (float)M_PI - D(twa_deg);
    s->b.u = u0;
    boat_inputs_init(&s->in, &s->cfg);
    s->target = s->b.heading;
    s->autopilot = 1; s->autotrim = 1; s->aoa_trim = 14;
}

static void sim_step(Sim *s) {
    float awa, aws;
    boat_apparent_wind(&s->b, &s->w, &awa, &aws);
    if (s->autotrim)
        for (int i = 0; i < s->cfg.sail_count; i++) {
            const SailCfg *sc = &s->cfg.sails[i];
            s->in.trim_target[i] = clampf(fabsf(awa) - D(s->aoa_trim), D(sc->trim_min_deg), D(sc->trim_max_deg));
        }
    if (s->autopilot)
        s->in.wheel_target = clampf(2.5f * wrap_pi(s->target - s->b.heading) - 1.5f * s->b.r, -1, 1);
    boat_step(&s->b, &s->cfg, &s->w, &s->in, &s->wf, DT);
    s->t += DT;
}

static void run(Sim *s, float secs) { int n = (int)(secs / DT); for (int i = 0; i < n; i++) sim_step(s); }

static int finite_state(const BoatState *b) {
    return isfinite(b->x) && isfinite(b->y) && isfinite(b->heading) && isfinite(b->u) && isfinite(b->v)
        && isfinite(b->r) && isfinite(b->heel) && isfinite(b->heel_rate) && isfinite(b->wheel);
}

/* Upwind VMG (m/s toward the wind) after settling on a fixed true-wind angle. */
static float vmg_at(float twa_deg) {
    Sim s; sim_init(&s, twa_deg, 10 * KN, 1.0f);
    run(&s, 40);
    float x0 = (float)s.b.x, y0 = (float)s.b.y;
    run(&s, 20);
    float dx = (float)s.b.x - x0, dy = (float)s.b.y - y0;
    /* toward the wind = -x */
    return -dx / 20.0f + 0 * dy;
}

static void test_upwind_angle(void) {
    float best = -1e9f, best_twa = 0;
    for (float twa = 30; twa <= 80; twa += 5) {
        float v = vmg_at(twa);
        if (v > best) { best = v; best_twa = twa; }
    }
    T_ASSERT(best > 0.8f);
    T_ASSERT(best_twa >= 40 && best_twa <= 50);
}

static void test_leeway_emerges(void) {
    Sim s; sim_init(&s, 45, 10 * KN, 1.0f);
    run(&s, 40);
    /* wind on starboard: leeward is port, so sway velocity is negative */
    float leeway = atan2f(s.b.v, s.b.u);
    T_ASSERT(leeway < -D(1.0f) && leeway > -D(12.0f));
}

static void test_top_speed_near_hull_speed(void) {
    for (float twa = 100; twa <= 130; twa += 30) {
        Sim s; sim_init(&s, twa, 14 * KN, 1.0f);
        run(&s, 60);
        float hs = s.cfg.hull.hull_speed;
        float sp = hypotf(s.b.u, s.b.v);
        T_ASSERT(sp > 0.9f * hs && sp < 1.1f * hs);
    }
}

static void test_tack_takes_4_to_8s_and_loses_speed(void) {
    Sim s; sim_init(&s, 45, 10 * KN, 1.0f);
    run(&s, 40);
    float u0 = s.b.u, umin = u0, t0 = s.t, tend = -1;
    s.target = (float)M_PI + D(45);   /* twa -45: wind on port */
    while (s.t - t0 < 20) {
        sim_step(&s);
        if (s.b.u < umin) umin = s.b.u;
        if (tend < 0 && twa_of(&s) < -D(40)) tend = s.t;
    }
    T_ASSERT(tend > 0);
    T_ASSERT(tend - t0 >= 4.0f && tend - t0 <= 8.0f);
    T_ASSERT(umin < 0.8f * u0);
}

static void test_irons_no_steerage(void) {
    Sim s; sim_init(&s, 0, 10 * KN, 0.3f);
    s.autopilot = 0;
    s.in.wheel_target = 1;   /* hard over */
    float worst = 0;
    for (int i = 0; i < (int)(5 / DT); i++) {
        sim_step(&s);
        worst = fmaxf(worst, fabsf(s.b.r));
    }
    T_ASSERT(worst < D(5));
}

static void test_capsize_in_gust(void) {
    Sim s; sim_init(&s, 70, 35 * KN, 1.0f);
    s.autotrim = 0;
    for (int i = 0; i < s.cfg.sail_count; i++) s.in.trim_target[i] = D(45);
    for (int i = 0; i < s.cfg.sail_count; i++) s.b.trims[i] = D(45);
    float maxheel = 0;
    for (int i = 0; i < (int)(30 / DT); i++) {
        sim_step(&s);
        maxheel = fmaxf(maxheel, fabsf(s.b.heel));
    }
    T_ASSERT(maxheel > D(s.cfg.hull.capsize_deg));
}

static void test_moderate_wind_does_not_capsize(void) {
    Sim s; sim_init(&s, 60, 12 * KN, 1.0f);
    float maxheel = 0;
    for (int i = 0; i < (int)(60 / DT); i++) {
        sim_step(&s);
        maxheel = fmaxf(maxheel, fabsf(s.b.heel));
    }
    T_ASSERT(maxheel < D(45));
}

static void test_no_nan_zero_wind(void) {
    Sim s; sim_init(&s, 0, 0, 0);
    s.in.wheel_target = 0.5f;
    run(&s, 60);
    T_ASSERT(finite_state(&s.b));
    T_ASSERT(fabsf(s.b.u) < 0.01f);
}

static void test_bad_dt_stays_finite(void) {
    Sim s; sim_init(&s, 60, 12 * KN, 1.0f);
    float dts[] = { 0, -1, NAN, INFINITY, 1e9f, 10 };
    for (int i = 0; i < 6; i++) {
        boat_step(&s.b, &s.cfg, &s.w, &s.in, &s.wf, dts[i]);
        T_ASSERT(finite_state(&s.b));
    }
}

static void test_far_from_origin_still_moves(void) {
    Sim s; sim_init(&s, 90, 10 * KN, 1.5f);
    s.b.x = 5.0e6; s.b.y = -5.0e6;
    double x0 = s.b.x, y0 = s.b.y;
    run(&s, 10);
    T_ASSERT(hypot(s.b.x - x0, s.b.y - y0) > 5.0);
}

static void test_boom_eases_across_on_gybe(void) {
    Sim s;
    sim_init(&s, -150, 6, 2);                 /* wind on port: boom out to starboard */
    s.autopilot = 0; s.autotrim = 0;
    for (int i = 0; i < (int)(4.0f / DT); i++) sim_step(&s);
    T_ASSERT(s.b.swing[0] > 0.95f);
    s.b.heading = (float)M_PI - D(150);       /* wind jumps to the starboard side */
    sim_step(&s);
    T_ASSERT(s.b.swing[0] > 0.8f);            /* not an instant flip */
    int steps = 1;
    while (s.b.swing[0] > -0.95f && steps < 300) { sim_step(&s); steps++; }
    T_ASSERT(steps * DT > 0.3f && steps * DT < 3.0f);
}

static void test_anchor_stops_the_boat(void) {
    Sim s; sim_init(&s, 90, 10 * KN, 2.0f);
    s.in.anchor = true;
    run(&s, 15);
    T_ASSERT(s.b.anchor > 0.99f);
    T_ASSERT(hypotf(s.b.u, s.b.v) < 0.3f);
}

int main(void) {
    T_RUN(test_upwind_angle);
    T_RUN(test_boom_eases_across_on_gybe);
    T_RUN(test_leeway_emerges);
    T_RUN(test_top_speed_near_hull_speed);
    T_RUN(test_tack_takes_4_to_8s_and_loses_speed);
    T_RUN(test_irons_no_steerage);
    T_RUN(test_capsize_in_gust);
    T_RUN(test_moderate_wind_does_not_capsize);
    T_RUN(test_no_nan_zero_wind);
    T_RUN(test_bad_dt_stays_finite);
    T_RUN(test_far_from_origin_still_moves);
    T_RUN(test_anchor_stops_the_boat);
    T_DONE();
}
