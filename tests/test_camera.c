#include "t.h"
#include <math.h>
#include "camera.h"

static Game g;
static BoatCfg bc;
static WeatherCfg wcfg;
static WorldCfg wc;
static CameraPose cp;

static void fresh(void) {
    boatcfg_default(&bc);
    weather_default(&wcfg);
    world_default(&wc);
    game_init(&g, &bc, &wcfg, &wc, WEATHER_SUNNY, 1);
    cp = (CameraPose){0};
}

static void settle(void) {
    for (int i = 0; i < 300; i++) camera_update(&cp, &g, &bc, 1.0f / 30);
}

static const float D2R = (float)M_PI / 180.0f;

static void test_roll_clamped_deck_roll_full(void) {
    fresh();
    g.b.heel = 30 * D2R;
    settle();
    T_NEAR(cp.roll, bc.camera.roll_limit_deg * D2R, 1e-3);
    T_NEAR(cp.deck_roll, 30 * D2R, 1e-3);
}

static void test_small_heel_passes_through(void) {
    fresh();
    g.b.heel = -5 * D2R;
    settle();
    T_NEAR(cp.roll, -5 * D2R, 1e-3);
}

static void test_smoothing_is_gradual_and_finite_on_odd_dt(void) {
    fresh();
    g.b.heel = 8 * D2R;
    camera_update(&cp, &g, &bc, 1.0f / 30);
    T_ASSERT(fabsf(cp.roll) > 0 && fabsf(cp.roll) < 8 * D2R);
    camera_update(&cp, &g, &bc, NAN);
    camera_update(&cp, &g, &bc, -1.0f);
    camera_update(&cp, &g, &bc, 1e9f);
    T_ASSERT(isfinite(cp.roll) && isfinite(cp.pitch) && isfinite(cp.x) && isfinite(cp.yaw));
}

static void test_horizon_point_projects_to_centre(void) {
    fresh();
    g.b.heading = 0;
    settle();
    float sx, sy, d;
    T_ASSERT(camera_project(&cp, cp.x + 5000, cp.y, cp.height, &sx, &sy, &d));
    T_NEAR(sx, CAM_W / 2.0, 0.5);
    T_NEAR(sy, CAM_HORIZON_Y, 0.5);
    T_NEAR(d, 5000, 1);
    /* a sea-level point far ahead sits just below the horizon line */
    T_ASSERT(camera_project(&cp, cp.x + 5000, cp.y, 0, &sx, &sy, &d));
    T_ASSERT(sy > CAM_HORIZON_Y && sy < CAM_HORIZON_Y + 2);
}

static void test_points_behind_rejected(void) {
    fresh();
    settle();
    float sx, sy, d;
    T_ASSERT(!camera_project(&cp, cp.x - 10, cp.y, 0, &sx, &sy, &d));
    T_ASSERT(!camera_project(&cp, cp.x, cp.y, 0, &sx, &sy, &d));
}

static void test_starboard_point_is_right_of_centre(void) {
    fresh();
    g.b.heading = 0;
    settle();
    float sx, sy, d;
    /* heading 0: starboard is +y */
    T_ASSERT(camera_project(&cp, cp.x + 20, cp.y + 5, 0, &sx, &sy, &d));
    T_ASSERT(sx > CAM_W / 2.0);
}

static void test_camera_yaw_turns_view_not_boat(void) {
    fresh();
    g.b.heading = 0.3f;
    g.ctl.camera_yaw = 0.5f;
    settle();
    T_NEAR(cp.yaw, 0.8, 1e-3);
    T_NEAR(g.b.heading, 0.3f, 1e-9);
}

static void test_position_follows_boat_offset_aft(void) {
    fresh();
    g.b.x = 100000.0; g.b.y = -50000.0; g.b.heading = 0;
    settle();
    T_NEAR(cp.x, 100000.0 + bc.camera.x_offset, 1e-3);
    T_NEAR(cp.y, -50000.0, 1e-3);
}

static void test_trailing_view_sits_far_aft_and_higher(void) {
    fresh();
    g.b.x = 100000.0; g.b.y = -50000.0; g.b.heading = 0;
    cp.trail = true;
    settle();
    T_NEAR(cp.x, 100000.0 + bc.camera.far_offset, 1e-3);
    T_NEAR(cp.y, -50000.0, 1e-3);
    T_NEAR(cp.height, bc.camera.far_height, 1e-3);
}

int main(void) {
    T_RUN(test_trailing_view_sits_far_aft_and_higher);
    T_RUN(test_roll_clamped_deck_roll_full);
    T_RUN(test_small_heel_passes_through);
    T_RUN(test_smoothing_is_gradual_and_finite_on_odd_dt);
    T_RUN(test_horizon_point_projects_to_centre);
    T_RUN(test_points_behind_rejected);
    T_RUN(test_starboard_point_is_right_of_centre);
    T_RUN(test_camera_yaw_turns_view_not_boat);
    T_RUN(test_position_follows_boat_offset_aft);
    T_DONE();
}
