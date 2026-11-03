/* Rudder authority across every boat in boats/: the same checks, thresholds scaled from each
 * boat's own length and rig, so a new boat file is held to the same standard. */
#include "t.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include "autotrim.h"
#include "balance.h"
#include "boat.h"
#include "sea.h"

#define D(x) deg2rad(x)
#define TACK_K 6.0f       /* s per sqrt(metre of length) allowed for a head-up or a tack */

typedef struct {
    BoatCfg c;
    BoatState b;
    BoatInputs in;
    Autotrim at;
    Wind w;
    Sea sea;
    bool use_sea;
    float t;
} Run;

static float twa_of(const BoatState *b) { return wrap_pi((float)M_PI - b->heading); }

static void run_init(Run *r, const BoatCfg *c, float twa_deg, float wind_ms) {
    memset(r, 0, sizeof *r);
    r->c = *c;
    boat_init(&r->b, &r->c);
    boat_inputs_init(&r->in, &r->c);
    autotrim_init(&r->at);
    r->at.on = true;
    r->w.dir = 0;
    r->w.speed = wind_ms;
    r->b.heading = (float)M_PI - D(twa_deg);
    r->b.u = 0.5f * c->hull.hull_speed;
}

static void run_step(Run *r) {
    WaveForces wf = {0};
    if (r->use_sea) wf = sea_hull_forces(&r->sea, &r->b, &r->c, r->t);
    autotrim_update(&r->at, &r->c, &r->b, &r->in, SIM_DT);
    boat_step(&r->b, &r->c, &r->w, &r->in, &wf, SIM_DT);
    r->t += SIM_DT;
}

/* Run settled on a heading for `settle` seconds with a PD steer, so the full-lock cases start steady.
 * The heading is pinned for the first PIN_S seconds (as balance.c does): the boat starts upright and
 * its heel overshoot would otherwise round it up before the steer could react. */
static void run_settle(Run *r, float secs) {
    float target = r->b.heading;
    for (int i = 0; i < (int)(secs / SIM_DT); i++) {
        if (i < (int)(12.0f / SIM_DT)) { r->b.heading = target; r->b.r = 0.0f; }
        r->in.wheel_target = clampf(2.5f * wrap_pi(target - r->b.heading) - 1.5f * r->b.r, -1, 1);
        run_step(r);
    }
}

static float time_limit(const BoatCfg *c) { return TACK_K * sqrtf(c->hull.length); }

static bool load_boat(const char *file, BoatCfg *c) {
    static char buf[16384];
    char path[300];
    snprintf(path, sizeof path, "../boats/%s", file);
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    boatcfg_default(c);
    return n > 0 && boatcfg_load(c, buf, n, NULL, 0);
}

typedef void (*BoatCheck)(const char *name, const BoatCfg *c);

static void for_each_boat(BoatCheck fn) {
    DIR *d = opendir("../boats");
    T_ASSERT(d != NULL);
    int seen = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 6 || strcmp(e->d_name + n - 5, ".json") != 0) continue;
        static BoatCfg c;
        T_ASSERT(load_boat(e->d_name, &c));
        fn(e->d_name, &c);
        seen++;
    }
    closedir(d);
    T_ASSERT(seen >= 4);
}

/* --- checks --- */

static void check_force_monotone(const char *name, const BoatCfg *c) {
    float prev = 0;
    for (int k = 0; k <= 20; k++) {
        Run r;
        run_init(&r, c, 90, 0.0f);
        r.b.wheel = r.in.wheel_target = k / 20.0f;
        run_step(&r);
        float load = fabsf(r.b.helm_load);
        if (load < prev * 0.99f) fprintf(stderr, "%s: rudder force fell at wheel %.2f (%.0f -> %.0f N)\n", name, k / 20.0f, prev, load);
        T_ASSERT(load >= prev * 0.99f);
        prev = load;
    }
}

static void test_rudder_force_monotone_in_wheel(void) { for_each_boat(check_force_monotone); }

static void check_weather_helm(const char *name, const BoatCfg *c) {
    if (!balance_is_fore_and_aft(c)) return;
    float deg = 0;
    bool ok = balance_weather_helm_deg(c, &deg);
    fprintf(stderr, "%s: weather helm %.1f deg (settled %d)\n", name, deg, ok);
    T_ASSERT(ok);
    T_ASSERT(deg >= 1.0f && deg <= 8.0f);
}

static void test_weather_helm_in_band(void) { for_each_boat(check_weather_helm); }

/* Full lock toward the wind from a close-hauled start; wind from `wind_ms`, optional sea. */
static void head_up_checks(const char *name, const BoatCfg *c, float start_twa, bool need_tack, bool sea, float wind_ms) {
    Run r;
    float limit = balance_closehauled_limit_deg(c);
    run_init(&r, c, start_twa, wind_ms);
    if (sea) {
        WeatherCfg wc;
        weather_default(&wc);
        sea_init(&r.sea, &wc.preset[WEATHER_CHOPPY], 5);
        r.use_sea = true;
    }
    run_settle(&r, 20.0f);
    float t_end = time_limit(c);
    float min_twa = 180, t0 = r.t;
    bool crossed = false, tacked = false;
    while (r.t - t0 < t_end) {
        r.in.wheel_target = 1.0f;
        run_step(&r);
        float twa = twa_of(&r.b);
        if (fabsf(twa) < min_twa) min_twa = fabsf(twa);
        if (twa < 0) crossed = true;
        if (crossed && twa < -D(40)) tacked = true;
    }
    float goal = need_tack ? 10.0f : (limit < 30.0f ? 50.0f : limit + 10.0f);
    if (limit >= 30.0f) goal = limit + 10.0f;
    fprintf(stderr, "%s: start %.0f min TWA %.1f (goal %.1f) tacked %d in %.1fs\n", name, start_twa, min_twa * 57.2958f, goal, tacked, t_end);
    T_ASSERT(min_twa <= D(goal));
    if (need_tack && limit < 30.0f) T_ASSERT(tacked);
}

static void check_tack(const char *name, const BoatCfg *c) {
    float limit = balance_closehauled_limit_deg(c);
    head_up_checks(name, c, limit < 30.0f ? 45.0f : limit + 15.0f, true, false, 10.0f);
}
/* Light and moderate air: the boat is slow, so the rudder has little flow and the sails still push. */
static void check_tack_3(const char *name, const BoatCfg *c) {
    float limit = balance_closehauled_limit_deg(c);
    head_up_checks(name, c, limit < 30.0f ? 45.0f : limit + 15.0f, true, false, 3.0f);
}
static void check_tack_5(const char *name, const BoatCfg *c) {
    float limit = balance_closehauled_limit_deg(c);
    head_up_checks(name, c, limit < 30.0f ? 45.0f : limit + 15.0f, true, false, 5.0f);
}
static void check_beam_reach(const char *name, const BoatCfg *c) { head_up_checks(name, c, 90.0f, false, false, 10.0f); }

static void test_full_lock_reaches_closehauled_limit(void) { for_each_boat(check_tack); }
static void test_tack_in_light_air(void) { for_each_boat(check_tack_3); }
static void test_tack_in_moderate_air(void) { for_each_boat(check_tack_5); }
static void test_full_lock_heads_up_from_beam_reach(void) { for_each_boat(check_beam_reach); }

static void test_tp52_choppy(void) {
    BoatCfg c;
    T_ASSERT(load_boat("tp52.json", &c));
    head_up_checks("tp52 choppy tack", &c, 45.0f, true, true, 10.0f);
    head_up_checks("tp52 choppy beam reach", &c, 90.0f, false, true, 10.0f);
}

static void test_balance_helpers_survive_odd_boats(void) {
    BoatCfg c;
    boatcfg_default(&c);
    c.sail_count = 0;
    float deg = 123;
    T_ASSERT(balance_closehauled_limit_deg(&c) == 0.0f);
    T_ASSERT(!balance_is_fore_and_aft(&c));
    T_ASSERT(!balance_weather_helm_deg(&c, &deg));
    T_ASSERT(deg == 123);
    T_ASSERT(load_boat("tp52.json", &c));
    int keep = -1;
    for (int i = 0; i < c.sail_count; i++) if (c.sails[i].shape == 2) keep = i;
    T_ASSERT(keep >= 0);
    c.sails[0] = c.sails[keep];
    c.sail_count = 1;
    T_ASSERT(!balance_is_fore_and_aft(&c));
    T_ASSERT(!balance_weather_helm_deg(&c, &deg));
}

/* Peak |r| over `secs` of full lock with u pinned (v, r free). */
static float peak_turn_rate(const BoatCfg *c, float u, float secs) {
    Run r;
    run_init(&r, c, 90, 0.0f);
    r.at.on = false;
    float peak = 0;
    for (int i = 0; i < (int)(secs / SIM_DT); i++) {
        r.b.u = u;
        r.in.wheel_target = 1.0f;
        run_step(&r);
        if (fabsf(r.b.r) > peak) peak = fabsf(r.b.r);
    }
    return peak;
}

static void check_stopped(const char *name, const BoatCfg *c) {
    float stopped = peak_turn_rate(c, 0.0f, 3.0f);
    float cruising = peak_turn_rate(c, 0.5f * c->hull.hull_speed, 3.0f);
    float creeping = peak_turn_rate(c, 0.1f * c->hull.hull_speed, 3.0f);
    fprintf(stderr, "%s: peak r stopped %.3f creeping %.3f cruising %.3f rad/s\n", name, stopped, creeping, cruising);
    T_ASSERT(stopped < 0.5f * cruising);
    T_ASSERT(creeping < 0.5f * cruising);        /* not just u == 0: a slow boat must not spin either */
}

static void test_stopped_boat_does_not_spin(void) { for_each_boat(check_stopped); }

static void check_astern(const char *name, const BoatCfg *c) {
    (void)name;
    float sign[2];
    for (int k = 0; k < 2; k++) {
        Run r;
        run_init(&r, c, 90, 0.0f);
        r.at.on = false;
        for (int i = 0; i < 120; i++) {
            r.b.u = k ? -1.0f : 1.0f;
            r.in.wheel_target = 1.0f;
            run_step(&r);
        }
        sign[k] = r.b.r;
    }
    T_ASSERT(sign[0] * sign[1] > 0);
}

static void test_steering_astern_unchanged(void) { for_each_boat(check_astern); }

static void test_rudder_assist_reports_flow_floor(void) {
    BoatCfg c;
    T_ASSERT(load_boat("tp52.json", &c));
    Run r;
    run_init(&r, &c, 90, 0.0f);
    r.at.on = false;
    r.b.u = 0.0f;
    r.in.wheel_target = 1.0f;
    run_step(&r);
    T_ASSERT(r.b.rudder_assist > 0.9f);                  /* dead in the water: floor does the work */
    r.b.u = c.foils.rudder_min_flow * 2.0f;
    run_step(&r);
    T_ASSERT(r.b.rudder_assist == 0.0f);                 /* real flow above the floor */
    r.b.u = c.foils.rudder_min_flow * 0.5f;
    r.b.v = 0;
    run_step(&r);
    T_ASSERT(r.b.rudder_assist > 0.3f && r.b.rudder_assist < 0.7f);
}

/* Release the wheel after settling close-hauled: a balanced boat rounds up (TWA falls), it must
 * not bear away. This pins the sign convention of balance_weather_helm_deg: positive = weather helm. */
static void check_release_rounds_up(const char *name, const BoatCfg *c) {
    if (!balance_is_fore_and_aft(c)) return;
    Run r;
    run_init(&r, c, 45.0f, 10.0f);
    run_settle(&r, 30.0f);
    float start = twa_of(&r.b);
    for (int i = 0; i < (int)(10.0f / SIM_DT); i++) {
        r.in.wheel_target = 0.0f;
        run_step(&r);
    }
    float end = twa_of(&r.b);
    fprintf(stderr, "%s: released wheel, TWA %.1f -> %.1f deg\n", name, start * 57.2958f, end * 57.2958f);
    T_ASSERT(end < start - D(2));
}

static void test_released_wheel_rounds_up(void) { for_each_boat(check_release_rounds_up); }

/* The settled weather-helm wheel, held fixed (no steering), must hold the boat close-hauled: it
 * neither rounds up through the wind nor bears away. Wheel positive turns the bow toward the wind,
 * so the wheel that holds a boat with weather helm is minus the reported helm. */
static void check_holds_without_broaching(const char *name, const BoatCfg *c) {
    if (!balance_is_fore_and_aft(c)) return;
    float deg = 0;
    T_ASSERT(balance_weather_helm_deg(c, &deg));
    Run r;
    run_init(&r, c, 45.0f, 10.0f);
    run_settle(&r, 30.0f);
    float start = r.b.heading, worst = 0;
    bool through = false;
    r.in.wheel_target = -deg / c->foils.rudder_max_deg;
    for (int i = 0; i < (int)(60.0f / SIM_DT); i++) {
        run_step(&r);
        float drift = fabsf(wrap_pi(r.b.heading - start));
        if (drift > worst) worst = drift;
        if (twa_of(&r.b) < 0) through = true;
    }
    fprintf(stderr, "%s: wheel held %.1f deg, heading drift %.1f deg, through wind %d\n", name, deg, worst * 57.2958f, through);
    T_ASSERT(worst < D(25));
    T_ASSERT(!through);
}

static void test_closehauled_holds_without_broaching(void) { for_each_boat(check_holds_without_broaching); }

int main(void) {
    T_RUN(test_balance_helpers_survive_odd_boats);
    T_RUN(test_rudder_force_monotone_in_wheel);
    T_RUN(test_weather_helm_in_band);
    T_RUN(test_full_lock_reaches_closehauled_limit);
    T_RUN(test_tack_in_light_air);
    T_RUN(test_tack_in_moderate_air);
    T_RUN(test_full_lock_heads_up_from_beam_reach);
    T_RUN(test_tp52_choppy);
    T_RUN(test_stopped_boat_does_not_spin);
    T_RUN(test_steering_astern_unchanged);
    T_RUN(test_rudder_assist_reports_flow_floor);
    T_RUN(test_released_wheel_rounds_up);
    T_RUN(test_closehauled_holds_without_broaching);
    return T_DONE();
}
