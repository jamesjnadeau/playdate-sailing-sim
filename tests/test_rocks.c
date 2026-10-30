#include "t.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rocks.h"

static WorldCfg wc;
static BoatCfg cfg;

static BoatState boat_at(double x, double y, float heading, float u, float v) {
    BoatState b; memset(&b, 0, sizeof b);
    b.x = x; b.y = y; b.heading = heading; b.u = u; b.v = v;
    return b;
}

/* world-frame velocity component into the rock (n points from rock to boat) */
static float inward(const BoatState *b, float nx, float ny) {
    float vx = b->u * cosf(b->heading) - b->v * sinf(b->heading);
    float vy = b->u * sinf(b->heading) + b->v * cosf(b->heading);
    return -(vx * nx + vy * ny);
}

/* rock at origin, boat just inside contact on its +x side, heading -x (straight at it) */
static BoatState at_rock(const Rock *r, float u) {
    return boat_at(r->r + 0.5f * cfg.hull.beam - 0.05f, 0, (float)M_PI, u, 0);
}

/* A dense field overflows the cap; the rocks kept must be the nearest ones, not the westmost. */
static void test_cap_keeps_nearest_rocks(void) {
    WorldCfg d = wc;
    d.rock_cell_size = 10.0f; d.rock_density = 1.0f; d.rock_r_min = 1.0f; d.rock_r_max = 2.0f;
    d.start_clear_radius = 0.0f;
    Rock all[64];
    int n = rocks_near(&d, 7, 500.0f, 500.0f, 150.0f, all, 64);
    T_ASSERT(n == 64);
    double far = 0;
    for (int i = 0; i < n; i++) {
        double e = hypot(all[i].x - 500.0, all[i].y - 500.0) - all[i].r;
        if (e > far) far = e;
    }
    T_ASSERT(far < 55.0);                  /* 64 cells of 10 m fit inside ~45 m; the westmost-first bug gave 110+ */
    Rock few[4];
    int m = rocks_near(&d, 7, 500.0f, 500.0f, 8.0f, few, 4);
    T_ASSERT(m == 4);                      /* rocks within 8 m are all found with a small cap */
}

static void test_same_seed_identical_different_seed_differs(void) {
    Rock a[64], b[64], c[64];
    int na = rocks_near(&wc, 11, 0, 0, 300, a, 64);
    int nb = rocks_near(&wc, 11, 0, 0, 300, b, 64);
    int nc = rocks_near(&wc, 12, 0, 0, 300, c, 64);
    T_ASSERT(na > 0 && na == nb);
    for (int i = 0; i < na; i++)
        T_ASSERT(a[i].x == b[i].x && a[i].y == b[i].y && a[i].r == b[i].r && a[i].awash == b[i].awash);
    int same = nc == na;
    for (int i = 0; same && i < na; i++) same = a[i].x == c[i].x && a[i].y == c[i].y;
    T_ASSERT(!same);
}

static void test_rocks_within_radius_and_sized(void) {
    Rock a[64];
    int n = rocks_near(&wc, 3, 500, -250, 150, a, 64);
    for (int i = 0; i < n; i++) {
        T_ASSERT(hypotf(a[i].x - 500, a[i].y + 250) - a[i].r <= 150.0f + 1e-3f);
        T_ASSERT(a[i].r >= wc.rock_r_min - 1e-4f && a[i].r <= wc.rock_r_max + 1e-4f);
        T_ASSERT(a[i].awash <= 1);
    }
}

static void test_start_area_is_clear(void) {
    for (uint32_t seed = 1; seed <= 30; seed++) {
        Rock a[64];
        int n = rocks_near(&wc, seed, 0, 0, 3 * wc.start_clear_radius, a, 64);
        for (int i = 0; i < n; i++)
            T_ASSERT(hypotf(a[i].x, a[i].y) - a[i].r >= wc.start_clear_radius - 1e-3f);
    }
    WorldCfg dense = wc; dense.rock_density = 1.0f; dense.start_clear_radius = 200.0f;
    Rock a[64];
    int n = rocks_near(&dense, 5, 0, 0, 190, a, 64);
    T_ASSERT(n == 0);
}

static void test_density_zero_gives_none_and_counts_are_capped(void) {
    Rock a[64];
    WorldCfg none = wc; none.rock_density = 0.0f;
    T_ASSERT(rocks_near(&none, 1, 400, 400, 500, a, 64) == 0);
    WorldCfg full = wc; full.rock_density = 1.0f;
    int n = rocks_near(&full, 1, 400, 400, 1000, a, 64);
    T_ASSERT(n == 64);
    Rock big[200];
    T_ASSERT(rocks_near(&full, 1, 400, 400, 1000, big, 200) <= 64);
    T_ASSERT(rocks_near(&full, 1, 400, 400, 1000, a, 10) == 10);
    T_ASSERT(rocks_near(&full, 1, 400, 400, 1000, a, 0) == 0);
    T_ASSERT(rocks_near(&full, 1, 400, 400, 1000, NULL, 64) == 0);
}

static void test_contact_tiers(void) {
    Rock r = { 0, 0, 2.0f, 0 };
    BoatState b = at_rock(&r, 0.4f);
    float h0 = b.heading;
    T_ASSERT(rocks_collide(&wc, &r, 1, &b, &cfg, 0.0f) == HIT_BUMP);
    T_ASSERT(inward(&b, 1, 0) <= 1e-4f);
    T_NEAR(b.heading, h0, 1e-6f);                 /* a bump does not turn the boat */

    b = boat_at(r.r + 0.5f * cfg.hull.beam - 0.05f, 0.5, (float)M_PI, 1.5f, 0);
    h0 = b.heading;
    T_ASSERT(rocks_collide(&wc, &r, 1, &b, &cfg, 0.0f) == HIT_STRIKE);
    T_ASSERT(fabsf(wrap_pi(b.heading - h0)) > 1e-3f);
    {   /* off-axis contact: the normal is rock -> boat, not +x */
        float d = hypotf((float)(b.x - r.x), (float)(b.y - r.y));
        T_ASSERT(inward(&b, (float)(b.x - r.x) / d, (float)(b.y - r.y) / d) <= 1e-4f);
    }

    b = at_rock(&r, 3.0f);
    T_ASSERT(rocks_collide(&wc, &r, 1, &b, &cfg, 0.0f) == HIT_WRECK);
    T_ASSERT(inward(&b, 1, 0) <= 1e-4f);
}

static void test_strike_kick_turns_bow_away(void) {
    Rock r = { 0, 0, 2.0f, 0 };
    /* boat to the +x side, heading slightly toward -x and +y, so the rock is on its port bow... */
    for (int s = -1; s <= 1; s += 2) {
        BoatState b = boat_at(r.r + 0.5f * cfg.hull.beam - 0.05f, 0, (float)M_PI + s * 0.3f, 1.5f, 0);
        float before = fabsf(wrap_pi(b.heading - (float)M_PI));
        T_ASSERT(rocks_collide(&wc, &r, 1, &b, &cfg, 0.0f) == HIT_STRIKE);
        /* turning away from the rock means the angle to the rock direction grows */
        T_ASSERT(fabsf(wrap_pi(b.heading - (float)M_PI)) > before);
    }
}

static void test_heave_raises_effective_speed(void) {
    Rock r = { 0, 0, 2.0f, 0 };
    BoatState a = at_rock(&r, 0.6f), b = at_rock(&r, 0.6f);
    T_ASSERT(rocks_collide(&wc, &r, 1, &a, &cfg, 0.0f) == HIT_BUMP);
    T_ASSERT(rocks_collide(&wc, &r, 1, &b, &cfg, 2.0f) == HIT_STRIKE);
}

static void test_moving_away_or_clear_is_no_hit(void) {
    Rock r = { 0, 0, 2.0f, 0 };
    BoatState away = boat_at(r.r + 0.5f * cfg.hull.beam - 0.05f, 0, 0.0f, 2.0f, 0);
    T_ASSERT(rocks_collide(&wc, &r, 1, &away, &cfg, 0.0f) == HIT_NONE);
    T_NEAR(away.u, 2.0f, 1e-5f);                   /* untouched velocity */
    T_ASSERT(hypot(away.x, away.y) >= r.r + 0.5f * cfg.hull.beam - 1e-3);  /* pushed out of the rock */
    BoatState clear = boat_at(50, 0, (float)M_PI, 3.0f, 0);
    T_ASSERT(rocks_collide(&wc, &r, 1, &clear, &cfg, 0.0f) == HIT_NONE);
    T_NEAR(clear.u, 3.0f, 1e-6f);
    T_ASSERT(rocks_collide(&wc, NULL, 0, &clear, &cfg, 0.0f) == HIT_NONE);
}

static void test_far_from_origin(void) {
    Rock a[64];
    int n = rocks_near(&wc, 8, 1.0e5f, -1.0e5f, 300, a, 64);
    T_ASSERT(n > 0);
    for (int i = 0; i < n; i++) T_ASSERT(isfinite(a[i].x) && isfinite(a[i].y) && a[i].r > 0);
    Rock *r = &a[0];
    /* approach it head-on from the +x side */
    BoatState b = boat_at((double)r->x + r->r + 0.5f * cfg.hull.beam - 0.05f, r->y, (float)M_PI, 1.5f, 0);
    HitKind k = rocks_collide(&wc, r, 1, &b, &cfg, 0.0f);
    T_ASSERT(k == HIT_STRIKE || k == HIT_BUMP);
    T_ASSERT(inward(&b, 1, 0) <= 1e-3f);
    T_ASSERT(isfinite(b.x) && isfinite(b.u));
    /* determinism at range */
    Rock a2[64];
    T_ASSERT(rocks_near(&wc, 8, 1.0e5f, -1.0e5f, 300, a2, 64) == n);
    T_ASSERT(memcmp(&a[0].x, &a2[0].x, sizeof(float)) == 0);
}

static void test_bad_input_stays_finite(void) {
    Rock r = { 0, 0, 2.0f, 0 };
    BoatState b = boat_at(NAN, 0, 0, INFINITY, NAN);
    T_ASSERT(rocks_collide(&wc, &r, 1, &b, &cfg, NAN) == HIT_NONE);
    Rock bad[2] = { { NAN, 0, 2, 0 }, { 0, 0, -1, 0 } };
    BoatState ok = boat_at(1, 0, 0, 1, 0);
    T_ASSERT(rocks_collide(&wc, bad, 2, &ok, &cfg, 0) == HIT_NONE);
    Rock a[64];
    WorldCfg junk; memset(&junk, 0, sizeof junk);
    int n = rocks_near(&junk, 1, 0, 0, 100, a, 64);
    T_ASSERT(n >= 0 && n <= 64);
    T_ASSERT(rocks_near(&wc, 1, NAN, 0, 100, a, 64) == 0);
    T_ASSERT(rocks_near(&wc, 1, 0, 0, NAN, a, 64) == 0);
}

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = malloc(65536);
    *n = fread(b, 1, 65536, f);
    fclose(f);
    return b;
}

static void test_shipped_config_equals_default(void) {
    size_t n = 0;
    char *buf = slurp("../config/world.json", &n);
    T_ASSERT(buf && n > 0);
    WorldCfg d, l;
    world_default(&d);
    char warn[256];
    T_ASSERT(world_load(&l, buf, n, warn, sizeof warn));
    T_ASSERT(warn[0] == 0);
    T_ASSERT(memcmp(&d, &l, sizeof d) == 0);
    free(buf);
}

static void test_load_clamps_and_warns(void) {
    WorldCfg l, d;
    world_default(&d);
    char warn[256];
    const char *s = "{\"rock_density\":9,\"rock_cell_size\":\"x\",\"rock_r_min\":50,\"rock_r_max\":1}";
    T_ASSERT(!world_load(&l, s, strlen(s), warn, sizeof warn));
    T_ASSERT(strstr(warn, "rock_density") != NULL);
    T_ASSERT(strstr(warn, "rock_cell_size") != NULL);
    T_ASSERT(l.rock_density <= 1.0f);
    T_ASSERT(l.rock_cell_size == d.rock_cell_size);
    T_ASSERT(l.rock_r_min <= l.rock_r_max && l.rock_r_max <= 0.5f * l.rock_cell_size);
    T_ASSERT(l.rock_bump_speed < l.rock_wreck_speed);
    T_ASSERT(!world_load(&l, "garbage", 7, warn, sizeof warn));
    T_ASSERT(memcmp(&l, &d, sizeof d) == 0);
    T_ASSERT(!world_load(&l, NULL, 0, warn, sizeof warn));
}

int main(void) {
    world_default(&wc);
    boatcfg_default(&cfg);
    T_RUN(test_same_seed_identical_different_seed_differs);
    T_RUN(test_cap_keeps_nearest_rocks);
    T_RUN(test_rocks_within_radius_and_sized);
    T_RUN(test_start_area_is_clear);
    T_RUN(test_density_zero_gives_none_and_counts_are_capped);
    T_RUN(test_contact_tiers);
    T_RUN(test_strike_kick_turns_bow_away);
    T_RUN(test_heave_raises_effective_speed);
    T_RUN(test_moving_away_or_clear_is_no_hit);
    T_RUN(test_far_from_origin);
    T_RUN(test_bad_input_stays_finite);
    T_RUN(test_shipped_config_equals_default);
    T_RUN(test_load_clamps_and_warns);
    T_DONE();
}
