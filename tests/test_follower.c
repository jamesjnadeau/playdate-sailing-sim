#include "t.h"
#include <math.h>
#include <string.h>
#include "follower.h"
#include "game.h"

static void rec(Follower *f, double x, double y, float heading) {
    BoatState b;
    memset(&b, 0, sizeof b);
    b.x = x;
    b.y = y;
    b.heading = heading;
    follower_record(f, &b);
}

static void run_x(Follower *f, double from, double to, double step) {
    int n = (int)lround((to - from) / step);
    for (int i = 0; i <= n; i++) rec(f, from + i * step, 0, 0);
}

static void test_empty_has_no_pose(void) {
    Follower f;
    FollowPose p;
    follower_clear(&f);
    T_ASSERT(!follower_pose(&f, 30, &p));
}

static void test_pose_is_30m_of_path_behind(void) {
    Follower f;
    FollowPose p;
    follower_clear(&f);
    run_x(&f, 0.1, 50.0, 0.1);
    T_ASSERT(follower_pose(&f, 30, &p));
    T_NEAR(p.x, 20.0, 0.01);
    T_NEAR(p.y, 0, 1e-9);
}

static void test_needs_a_full_trail(void) {
    Follower f;
    FollowPose p;
    follower_clear(&f);
    run_x(&f, 0.0, 20.0, 0.1);
    T_ASSERT(!follower_pose(&f, 30, &p));
    run_x(&f, 20.1, 30.1, 0.1);
    T_ASSERT(follower_pose(&f, 30, &p));
}

static void test_fields_interpolate(void) {
    Follower f;
    FollowPose p;
    follower_clear(&f);
    for (int i = 0; i <= 400; i++) {
        BoatState b;
        memset(&b, 0, sizeof b);
        b.x = i * 0.1;
        b.heading = 0.01f * (float)b.x;
        b.heel = 0.02f * (float)b.x;
        b.hoists[0] = 0.5f;
        follower_record(&f, &b);
    }
    T_ASSERT(follower_pose(&f, 30, &p));
    T_NEAR(p.heading, 0.1, 1e-3);
    T_NEAR(p.heel, 0.2, 1e-3);
    T_NEAR(p.hoists[0], 0.5, 1e-6);
}

static void test_heading_across_the_seam(void) {
    Follower f;
    FollowPose p;
    follower_clear(&f);
    rec(&f, 0, 0, 3.1f);
    for (double x = 0.25; x <= 30.0 + 1e-9; x += 0.25) rec(&f, x, 0, -3.1f);
    rec(&f, 30.125, 0, -3.1f);
    T_ASSERT(follower_pose(&f, 30, &p));
    T_ASSERT(fabsf(p.heading) > 3.0f);
}

static void test_dense_records_overwrite_the_head(void) {
    Follower f;
    follower_clear(&f);
    for (int i = 1; i <= 1000; i++) rec(&f, i * 0.01, 0, 0);
    T_ASSERT(f.n <= 45);
    T_NEAR(f.smp[f.head].p.x, 10.0, 1e-9);
}

static void test_ring_wraps(void) {
    Follower f;
    FollowPose p;
    double lastx = 0;
    follower_clear(&f);
    for (int i = 1; i <= 2000; i++) {
        lastx = i * 0.5;
        rec(&f, lastx, 0, 0);
    }
    T_ASSERT(follower_pose(&f, 30, &p));
    T_NEAR(p.x, lastx - 30, 0.01);
    T_ASSERT(follower_pose(&f, 60, &p));
    T_NEAR(p.x, lastx - 60, 0.01);
}

static void test_at_rest_stays_put(void) {
    Follower f;
    FollowPose p;
    follower_clear(&f);
    for (int i = 0; i < 5000; i++) rec(&f, 3.0, 4.0, 0.5f);
    T_ASSERT(f.n <= 2);
    T_ASSERT(!follower_pose(&f, 30, &p));
    T_ASSERT(isfinite(f.smp[f.head].p.x) && isfinite(f.smp[f.head].s));
}

static void test_shift_moves_the_trail(void) {
    Follower f;
    FollowPose p;
    follower_clear(&f);
    run_x(&f, 0.1, 50.0, 0.1);
    follower_shift(&f, 100, -40);
    T_ASSERT(follower_pose(&f, 30, &p));
    T_NEAR(p.x, -80.0, 0.01);
    T_NEAR(p.y, 40.0, 1e-9);
    T_NEAR(f.smp[f.head].p.x, -50.0, 1e-9);
    T_NEAR(f.smp[f.head].p.y, 40.0, 1e-9);
}

static Game g;
static BoatCfg bc;
static WeatherCfg wcfg;
static WorldCfg wc;
static Track tr;

static void fresh(void) {
    boatcfg_default(&bc);
    weather_default(&wcfg);
    world_default(&wc);
    game_init(&g, &bc, &wcfg, &wc, WEATHER_SUNNY, 1);
}

static void fresh_journey(void) {
    fresh();
    game_set_journey(&g);
}

static void step(bool drive) {
    if (drive) {
        g.b.u = 8.0f;
        g.b.heading = 0.0f;
    }
    game_update(&g, 0, 0, 0, 1.0f / 30);
}

/* Drives until the ghost exists; returns whether it did within `cap` frames. */
static bool drive_until_ghost(FollowPose *p, int cap) {
    for (int i = 0; i < cap; i++) {
        step(true);
        if (follower_pose(&g.follow, FOLLOW_DISTANCE_M, p)) return true;
    }
    return false;
}

static void test_journey_records_and_ghost_appears(void) {
    FollowPose p;
    fresh_journey();
    T_ASSERT(drive_until_ghost(&p, 20000));
    T_ASSERT(hypot(p.x - g.b.x, p.y - g.b.y) <= 30.001);
}

static void test_free_range_records(void) {
    FollowPose p;
    fresh();
    game_set_track(&g, NULL);
    T_ASSERT(drive_until_ghost(&p, 20000));
    T_ASSERT(hypot(p.x - g.b.x, p.y - g.b.y) <= 30.001);
}

static void test_race_records_nothing(void) {
    fresh();
    track_default(&tr);
    game_set_track(&g, &tr);
    for (int i = 0; i < 300; i++) step(true);
    T_ASSERT(g.follow.n == 0);
}

static void test_restart_clears_the_trail(void) {
    FollowPose p;
    fresh_journey();
    T_ASSERT(drive_until_ghost(&p, 20000));
    game_restart(&g, 2);
    T_ASSERT(g.follow.n <= 1);
    T_ASSERT(!follower_pose(&g.follow, FOLLOW_DISTANCE_M, &p));
    T_ASSERT(drive_until_ghost(&p, 20000));
    game_set_track(&g, NULL);
    T_ASSERT(g.follow.n <= 1);
    T_ASSERT(!follower_pose(&g.follow, FOLLOW_DISTANCE_M, &p));
    T_ASSERT(drive_until_ghost(&p, 20000));
    game_set_journey(&g);
    T_ASSERT(g.follow.n <= 1);
    T_ASSERT(!follower_pose(&g.follow, FOLLOW_DISTANCE_M, &p));
}

static void test_origin_shift_keeps_the_ghost_close(void) {
    FollowPose p;
    bool shifted = false;
    fresh();
    game_set_track(&g, NULL);
    for (int i = 0; i < 60000 && !shifted; i++) {
        double before = g.b.x;
        step(true);
        shifted = g.b.x < before;
    }
    T_ASSERT(shifted);
    T_ASSERT(follower_pose(&g.follow, FOLLOW_DISTANCE_M, &p));
    double d = hypot(p.x - g.b.x, p.y - g.b.y);
    T_ASSERT(d >= 15.0 && d <= 30.001);
}

int main(void) {
    T_RUN(test_empty_has_no_pose);
    T_RUN(test_pose_is_30m_of_path_behind);
    T_RUN(test_needs_a_full_trail);
    T_RUN(test_fields_interpolate);
    T_RUN(test_heading_across_the_seam);
    T_RUN(test_dense_records_overwrite_the_head);
    T_RUN(test_ring_wraps);
    T_RUN(test_at_rest_stays_put);
    T_RUN(test_shift_moves_the_trail);
    T_RUN(test_journey_records_and_ghost_appears);
    T_RUN(test_free_range_records);
    T_RUN(test_race_records_nothing);
    T_RUN(test_restart_clears_the_trail);
    T_RUN(test_origin_shift_keeps_the_ghost_close);
    return T_DONE();
}
