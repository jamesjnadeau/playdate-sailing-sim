#include "t.h"
#include <math.h>
#include <string.h>
#include "follower.h"

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
    return T_DONE();
}
