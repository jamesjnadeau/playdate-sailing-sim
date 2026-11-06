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

static void test_trailing_view_orbits_boat_with_look_yaw(void) {
    fresh();
    cp.trail = true;
    g.b.heading = 0.3f;
    g.ctl.camera_yaw = 1.0f;
    settle();
    float a = 1.3f;
    /* the radius adapts to keep the masthead in view; the direction is what matters here */
    float r = -(float)hypot(cp.x - g.b.x, cp.y - g.b.y);
    T_ASSERT(r < 0.0f);
    T_NEAR(cp.x, g.b.x + cosf(a) * r, 1e-3);
    T_NEAR(cp.y, g.b.y + sinf(a) * r, 1e-3);
    T_NEAR(cp.yaw, a, 1e-3);
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
    T_ASSERT(cp.x < 100000.0 - 0.5 * bc.hull.length);
    T_NEAR(cp.y, -50000.0, 1e-3);
    T_NEAR(cp.height, bc.camera.far_height, 1e-3);
}

static void test_trailing_view_frames_mast_and_hull_with_margin(void) {
    fresh();
    cp.trail = true;
    settle();
    float m = bc.camera.frame_margin, sx, sy;
    T_ASSERT(camera_project(&cp, g.b.x + cosf((float)g.b.heading) * bc.look.mast_x,
                            g.b.y + sinf((float)g.b.heading) * bc.look.mast_x,
                            bc.look.mast_height + 0.6f, &sx, &sy, NULL));
    T_NEAR(sy, m, 3.0);
    T_ASSERT(camera_project(&cp, g.b.x - cosf((float)g.b.heading) * 0.5 * bc.hull.length,
                            g.b.y - sinf((float)g.b.heading) * 0.5 * bc.hull.length,
                            0.0f, &sx, &sy, NULL));
    T_NEAR(sy, (CAM_H - HUD_H) - m, 3.0);
}

static void level_pose(float yaw) {
    cp = (CameraPose){0};
    cp.height = 3.0f;
    cp.yaw = yaw;
}

static void test_mark_view_bottom_bound(void) {
    /* eye above the sea so a point just under the bar's top row projects into the 16px band */
    level_pose(0);
    cp.height = 10.0f;
    float wx = 0, sy = 0;
    for (float d = 12.0f; d < 20.0f; d += 0.01f) {
        float y;
        if (camera_project(&cp, d, 0.0, MARK_IND_Z, NULL, &y, NULL) &&
            y >= (CAM_H - HUD_H - 16) && y < CAM_H - HUD_H) { wx = d; sy = y; break; }
    }
    T_ASSERT(sy > 0);
    MarkView v;
    camera_mark_view(&cp, wx, 0.0, CAM_H - HUD_H, &v);
    T_ASSERT(v.on_screen);
    T_NEAR(v.y, sy, 1e-3);
    camera_mark_view(&cp, wx, 0.0, CAM_H - HUD_H - 16, &v);
    T_ASSERT(!v.on_screen);
    T_ASSERT(v.y <= CAM_H - HUD_H - 16 - MARK_EDGE_PAD + 1e-3);
}

static void test_mark_ahead_is_on_screen(void) {
    level_pose(0);
    MarkView v;
    camera_mark_view(&cp, 200.0, 0.0, CAM_H - HUD_H, &v);
    T_ASSERT(v.on_screen);
    T_NEAR(v.x, CAM_W * 0.5, 1.0);
    T_ASSERT(v.y < CAM_H - HUD_H);
}

static void test_mark_far_starboard_goes_to_right_edge(void) {
    level_pose(0);
    MarkView v;
    camera_mark_view(&cp, 20.0, 200.0, CAM_H - HUD_H, &v);
    T_ASSERT(!v.on_screen);
    T_NEAR(v.x, CAM_W - MARK_EDGE_PAD, 1.0);
    T_ASSERT(fabsf(v.angle) < (float)M_PI / 2);
}

static void test_mark_behind_goes_to_side_edge(void) {
    level_pose(0);
    MarkView v;
    camera_mark_view(&cp, -100.0, 0.0, CAM_H - HUD_H, &v);
    T_ASSERT(!v.on_screen);
    T_NEAR(v.x, CAM_W - MARK_EDGE_PAD, 1.0);
    T_NEAR(v.angle, 0.0, 1e-6);
    camera_mark_view(&cp, -100.0, -30.0, CAM_H - HUD_H, &v);
    T_ASSERT(!v.on_screen);
    T_NEAR(fabsf(v.angle), M_PI, 1e-3);
    T_NEAR(v.x, MARK_EDGE_PAD, 1.0);
}

static void test_mark_view_nonfinite_is_safe(void) {
    level_pose(0);
    MarkView v;
    camera_mark_view(&cp, NAN, NAN, CAM_H - HUD_H, &v);
    T_ASSERT(isfinite(v.x) && isfinite(v.y) && isfinite(v.angle));
    level_pose(NAN);
    camera_mark_view(&cp, 200.0, 0.0, CAM_H - HUD_H, &v);
    T_ASSERT(isfinite(v.x) && isfinite(v.y) && isfinite(v.angle));
}

static void test_horizon_y_pins_horizon_and_stern(void) {
    fresh();
    bc.camera.horizon_y = 124.0f;
    cp.trail = true;
    g.b.heading = 0;
    settle();
    float sx, sy;
    T_ASSERT(camera_project(&cp, cp.x + 5000, cp.y, cp.height, &sx, &sy, NULL));
    T_NEAR(sy, 124.0, 1.0);
    T_ASSERT(camera_project(&cp, g.b.x - 0.5 * bc.hull.length, g.b.y, 0.0f, &sx, &sy, NULL));
    T_NEAR(sy, (CAM_H - HUD_H) - bc.camera.frame_margin, 3.0);
}

static void test_horizon_y_too_low_stays_finite(void) {
    fresh();
    bc.camera.horizon_y = 200.0f;   /* at/below the stern limit */
    cp.trail = true;
    settle();
    T_ASSERT(isfinite(cp.x) && isfinite(cp.pitch));
    T_ASSERT(cp.x < g.b.x);         /* still aft of the boat */
}

static void test_trail_dist_overrides_solved_distance(void) {
    fresh();
    bc.camera.horizon_y = 124.0f;
    bc.camera.trail_dist = 38.0f;
    cp.trail = true;
    g.b.heading = 0;
    settle();
    T_NEAR(g.b.x - cp.x, 38.0, 1e-2);
    float sx, sy;
    T_ASSERT(camera_project(&cp, cp.x + 5000, cp.y, cp.height, &sx, &sy, NULL));
    T_NEAR(sy, 124.0, 1.0);
}

static float eye_radius(void) {
    return hypotf(hypotf((float)(cp.x - g.b.x), (float)(cp.y - g.b.y)), cp.height);
}
static float boat_sy(void) {
    float sx, sy;
    camera_project(&cp, g.b.x, g.b.y, 0, &sx, &sy, NULL);
    return sy;
}

static void test_trailing_pitch_keeps_distance_and_boat_screen_position(void) {
    fresh(); cp.trail = true;
    settle();
    float r0 = eye_radius(), sy0 = boat_sy(), h0 = cp.height, p0 = cp.pitch;
    g.ctl.camera_pitch = 0.5f;
    settle();
    T_NEAR(eye_radius(), r0, 0.02);
    T_ASSERT(cp.height > h0);
    T_ASSERT(cp.pitch < p0);
    T_NEAR(boat_sy(), sy0, 0.5);
}

static void test_trailing_pitch_down_keeps_eye_above_water(void) {
    fresh(); cp.trail = true;
    g.ctl.camera_pitch = -deg2rad(bc.camera.pitch_range_deg);
    settle();
    T_ASSERT(cp.height >= 0.5f);
}

static void test_trailing_pitch_down_clears_the_wave_under_the_eye(void) {
    fresh();
    game_init(&g, &bc, &wcfg, &wc, WEATHER_STORM, 1);
    cp.trail = true;
    g.ctl.camera_pitch = -deg2rad(bc.camera.pitch_range_deg);
    g.b.heave = -3.0f;
    for (int i = 0; i < 600; i++) {
        g.t = i / 30.0f;
        camera_update(&cp, &g, &bc, 1.0f / 30);
        T_ASSERT(cp.height >= sea_height(&g.sea, (float)cp.x, (float)cp.y, g.t) + 0.5f - 1e-3f);
    }
}

static void test_near_pitch_looks_up_and_down(void) {
    fresh(); cp.trail = false;
    settle();
    float p0 = cp.pitch, h0 = cp.height;
    g.ctl.camera_pitch = 0.3f;
    settle();
    T_NEAR(cp.pitch - p0, 0.3, 1e-3);
    T_NEAR(cp.height, h0, 1e-4);
    g.ctl.camera_pitch = -0.3f;
    settle();
    T_NEAR(cp.pitch - p0, -0.3, 1e-3);
    T_NEAR(cp.height, h0, 1e-4);
}

static void test_follow_keeps_the_eye_behind_the_boat_and_turns_in_place(void) {
    fresh();
    cp.trail = true;
    g.b.heading = 0.3f;
    settle();
    float back = (float)hypot(cp.x - g.b.x, cp.y - g.b.y);   /* the framed distance */
    g.ctl.camera_follow = true;
    g.ctl.camera_yaw = 0.3f;   /* a small turn keeps the boat in view without moving the eye */
    settle();
    T_NEAR(cp.x, g.b.x - cosf(0.3f) * back, 1e-3);
    T_NEAR(cp.y, g.b.y - sinf(0.3f) * back, 1e-3);
    T_NEAR(cp.yaw, 0.6f, 1e-3);
    g.ctl.camera_yaw = -0.3f;   /* turning the view never moves the eye */
    settle();
    T_NEAR(cp.x, g.b.x - cosf(0.3f) * back, 1e-3);
    T_NEAR(cp.y, g.b.y - sinf(0.3f) * back, 1e-3);
}

static void test_follow_glides_the_eye_without_a_jump(void) {
    fresh();
    cp.trail = true;
    g.b.heading = 0;
    g.ctl.camera_yaw = 0.3f;
    settle();
    double x0 = cp.x, y0 = cp.y;
    g.ctl.camera_follow = true;
    camera_update(&cp, &g, &bc, 1.0f / 30);
    T_ASSERT(hypot(cp.x - x0, cp.y - y0) < 0.3 * hypot(x0 - g.b.x, y0 - g.b.y));
    settle();
    T_ASSERT(fabs(cp.y - g.b.y) < 1e-3);   /* now straight behind */
    g.ctl.camera_follow = false;
    settle();
    T_NEAR(cp.x, x0, 1e-3);   /* and back out to the orbit when follow ends */
    T_NEAR(cp.y, y0, 1e-3);
}

/* the boat's masthead, waterline centre and stern, all on screen above the status panel */
static bool boat_in_view(void) {
    float h = g.b.heading, half = 0.5f * bc.hull.length;
    double px[3] = { g.b.x + cos(h) * bc.look.mast_x, g.b.x, g.b.x - cos(h) * half };
    double py[3] = { g.b.y + sin(h) * bc.look.mast_x, g.b.y, g.b.y - sin(h) * half };
    float pz[3] = { bc.look.mast_height, 0.0f, 0.0f };
    for (int i = 0; i < 3; i++) {
        float sx, sy;
        if (!camera_project(&cp, px[i], py[i], pz[i], &sx, &sy, NULL)) return false;
        if (sx < 0 || sx > CAM_W || sy < 0 || sy > CAM_H - HUD_H) return false;
    }
    return true;
}

static void test_follow_keeps_the_boat_on_screen_whatever_the_buoy_bearing(void) {
    for (int deg = -180; deg <= 180; deg += 15) {
        fresh();
        cp.trail = true;
        g.b.heading = 0.3f;
        g.ctl.camera_follow = true;
        g.ctl.camera_yaw = deg * D2R;
        settle();
        if (!boat_in_view()) printf("  boat lost with the buoy %d deg off the heading\n", deg);
        T_ASSERT(boat_in_view());
    }
}

static void test_follow_zooms_out_as_the_buoy_swings_away(void) {
    fresh();
    cp.trail = true;
    g.b.heading = 0.3f;
    g.ctl.camera_follow = true;
    g.ctl.camera_yaw = 0.0f;
    settle();
    float d0 = (float)hypot(cp.x - g.b.x, cp.y - g.b.y);
    g.ctl.camera_yaw = 90 * D2R;
    settle();
    T_ASSERT(hypot(cp.x - g.b.x, cp.y - g.b.y) > 1.1f * d0);
}

int main(void) {
    T_RUN(test_mark_ahead_is_on_screen);
    T_RUN(test_mark_far_starboard_goes_to_right_edge);
    T_RUN(test_mark_behind_goes_to_side_edge);
    T_RUN(test_mark_view_nonfinite_is_safe);
    T_RUN(test_mark_view_bottom_bound);
    T_RUN(test_trailing_view_sits_far_aft_and_higher);
    T_RUN(test_roll_clamped_deck_roll_full);
    T_RUN(test_trailing_pitch_keeps_distance_and_boat_screen_position);
    T_RUN(test_trailing_pitch_down_keeps_eye_above_water);
    T_RUN(test_trailing_pitch_down_clears_the_wave_under_the_eye);
    T_RUN(test_near_pitch_looks_up_and_down);
    T_RUN(test_small_heel_passes_through);
    T_RUN(test_trailing_view_frames_mast_and_hull_with_margin);
    T_RUN(test_smoothing_is_gradual_and_finite_on_odd_dt);
    T_RUN(test_horizon_point_projects_to_centre);
    T_RUN(test_points_behind_rejected);
    T_RUN(test_starboard_point_is_right_of_centre);
    T_RUN(test_camera_yaw_turns_view_not_boat);
    T_RUN(test_trailing_view_orbits_boat_with_look_yaw);
    T_RUN(test_position_follows_boat_offset_aft);
    T_RUN(test_horizon_y_pins_horizon_and_stern);
    T_RUN(test_horizon_y_too_low_stays_finite);
    T_RUN(test_trail_dist_overrides_solved_distance);
    T_RUN(test_follow_keeps_the_eye_behind_the_boat_and_turns_in_place);
    T_RUN(test_follow_glides_the_eye_without_a_jump);
    T_RUN(test_follow_keeps_the_boat_on_screen_whatever_the_buoy_bearing);
    T_RUN(test_follow_zooms_out_as_the_buoy_swings_away);
    T_DONE();
}
