#include "t.h"
#include "sail.h"
#include "boatcfg.h"
#include <math.h>
#include <stdio.h>

static BoatCfg cfg;
static const SailCfg *main_sail(void) { boatcfg_default(&cfg); return &cfg.sails[0]; }
#define D(x) deg2rad(x)

static void test_beam_reach_drives(void) {
    SailForce f = sail_force(main_sail(), D(75), 1, D(90), 8, 0, 1);
    T_ASSERT(f.fx > 50);
    T_ASSERT(f.fy < 0 || f.fy > 0);   /* sideways force exists */
    T_NEAR(f.aoa, D(15), 1e-4f);
    T_ASSERT(f.luff < 0.01f);
}

static void test_close_hauled_overtrim_stalls(void) {
    const SailCfg *s = main_sail();
    SailForce good = sail_force(s, D(12), 1, D(30), 8, 0, 1);   /* aoa 18 deg, peak CL */
    SailForce over = sail_force(s, D(0), 1, D(30), 8, 0, 1);    /* aoa 30 deg, stalled */
    T_ASSERT(good.fx > 0);
    T_ASSERT(over.aoa > D(25));
    T_ASSERT(over.fx < 0.5f * good.fx);
}

static void test_eased_past_wind_line_luffs(void) {
    const SailCfg *s = main_sail();
    SailForce good = sail_force(s, D(12), 1, D(30), 8, 0, 1);
    SailForce flap = sail_force(s, D(60), 1, D(30), 8, 0, 1);   /* boom outboard of the wind */
    T_ASSERT(flap.luff > 0.5f);
    T_ASSERT(fabsf(flap.fx) < 0.5f * good.fx);
}

static void test_zero_aoa_luffs_with_no_force(void) {
    SailForce f = sail_force(main_sail(), D(90), 1, D(90), 8, 0, 1);
    SailForce good = sail_force(main_sail(), D(75), 1, D(90), 8, 0, 1);
    T_ASSERT(f.luff > 0.5f);
    T_ASSERT(hypotf(f.fx, f.fy) < 0.2f * hypotf(good.fx, good.fy));
}

static void test_hoist_zero_gives_zeros(void) {
    SailForce f = sail_force(main_sail(), D(75), 0, D(90), 8, 0, 1);
    T_ASSERT(f.fx == 0 && f.fy == 0 && f.heel_moment == 0 && f.yaw_moment == 0);
}

static void test_force_scales_with_aws_squared(void) {
    const SailCfg *s = main_sail();
    SailForce a = sail_force(s, D(75), 1, D(90), 5, 0, 1);
    SailForce b = sail_force(s, D(75), 1, D(90), 10, 0, 1);
    T_NEAR(b.fx / a.fx, 4.0f, 1e-3f);
    T_NEAR(b.fy / a.fy, 4.0f, 1e-3f);
}

static void test_heel_reduces_drive(void) {
    const SailCfg *s = main_sail();
    SailForce up = sail_force(s, D(75), 1, D(90), 8, 0, 1);
    SailForce hl = sail_force(s, D(75), 1, D(90), 8, D(60), 1);
    T_ASSERT(hl.fx < up.fx && hl.fx > 0);
}

static void test_slot_multiplier_boosts_force(void) {
    const SailCfg *s = main_sail();
    SailForce a = sail_force(s, D(75), 1, D(90), 8, 0, 1);
    SailForce b = sail_force(s, D(75), 1, D(90), 8, 0, 1.2f);
    T_ASSERT(b.fx > a.fx);
}

static void test_port_wind_mirrors_starboard(void) {
    const SailCfg *s = main_sail();
    SailForce a = sail_force(s, D(40), 1, D(70), 8, 0, 1);
    SailForce b = sail_force(s, D(40), 1, D(-70), 8, 0, 1);
    T_NEAR(a.fx, b.fx, 1e-3f);
    T_NEAR(a.fy, -b.fy, 1e-3f);
    T_NEAR(a.heel_moment, -b.heel_moment, 1e-3f);
    T_ASSERT(a.heel_moment * a.fy > 0);   /* sideways push heels the same way it pushes */
}

static void test_degenerate_inputs_are_finite(void) {
    const SailCfg *s = main_sail();
    float aws[] = { 0, 8, 1e6f };
    float awa[] = { 0, D(180), D(-180), 1e9f, NAN };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 5; j++) {
            SailForce f = sail_force(s, D(30), 1, awa[j], aws[i], 0, 1);
            T_ASSERT(isfinite(f.fx) && isfinite(f.fy) && isfinite(f.heel_moment)
                     && isfinite(f.yaw_moment) && isfinite(f.luff) && isfinite(f.aoa));
        }
    SailForce z = sail_force(s, D(30), 1, D(90), 0, 0, 1);
    T_ASSERT(z.fx == 0 && z.fy == 0 && z.luff == 0 && z.aoa == 0);
}

static void test_awa_window_cuts_out_of_range_sail(void) {
    boatcfg_default(&cfg);
    SailCfg s = cfg.sails[0];
    s.awa_min_deg = 110; s.awa_max_deg = 180;
    SailForce in  = sail_force(&s, D(80), 1, D(140), 8, 0, 1);
    SailForce out = sail_force(&s, D(80), 1, D(60), 8, 0, 1);
    T_ASSERT(fabsf(in.fx) + fabsf(in.fy) > 20);
    T_ASSERT(out.fx == 0 && out.fy == 0 && out.luff > 0.99f);
}

static void test_spinnaker_billows_ahead_of_chord_on_a_run(void) {
    SailCfg s = *main_sail();
    s.camber = 0.3f;
    /* clew out to starboard and slightly aft; wind from the port quarter pushes the belly forward and starboard */
    float cx = -2, cy = 7, chord = hypotf(cx, cy), ox, oy;
    sail_billow(&s, 0, cx, cy, 0.5f, 0.5f, D(-135), &ox, &oy);
    T_ASSERT(hypotf(ox, oy) > 0.2f * s.camber * chord);   /* a visible belly, not a flat triangle */
    T_ASSERT(ox > 0 && oy > 0);
    /* the tack and clew stay pinned and the edges swell apart */
    float ax, ay, bx, by;
    sail_billow(&s, 0, cx, cy, 0.0f, 1.0f, D(-135), &ax, &ay);
    T_ASSERT(fabsf(ax) < 1e-4f && fabsf(ay) < 1e-4f);
    sail_billow(&s, 0, cx, cy, 0.5f, 0.0f, D(-135), &ax, &ay);
    sail_billow(&s, 0, cx, cy, 0.5f, 1.0f, D(-135), &bx, &by);
    T_ASSERT(ax * cx + ay * cy < 0 && bx * cx + by * cy > 0);
}

static void test_spinnaker_billow_collapses_when_luffing_and_mirrors(void) {
    SailCfg s = *main_sail();
    s.camber = 0.3f;
    float ox, oy, mx, my;
    sail_billow(&s, 1, -2, 7, 0.5f, 0.5f, D(-135), &ox, &oy);
    T_ASSERT(fabsf(ox) < 1e-5f && fabsf(oy) < 1e-5f);
    sail_billow(&s, 0, -2, 7, 0.5f, 0.5f, D(-135), &ox, &oy);
    sail_billow(&s, 0, -2, -7, 0.5f, 0.5f, D(135), &mx, &my);
    T_NEAR(mx, ox, 1e-4f);
    T_NEAR(my, -oy, 1e-4f);
}

static void test_longship_gybe_is_symmetric(void) {
    static char buf[16384];
    FILE *f = fopen("../boats/disabled/longship.json", "rb");
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    T_ASSERT(n > 0 && boatcfg_load(&cfg, buf, n, NULL, 0));
    const SailCfg *s = &cfg.sails[0];
    SailForce a = sail_force(s, D(90), 1, D(175), 6, 0, 1);
    SailForce b = sail_force(s, D(90), 1, -D(175), 6, 0, 1);
    T_ASSERT(a.fx > 0);                                       /* drives on a dead run */
    T_NEAR(a.fx, b.fx, 0.01f * fabsf(a.fx));
    T_NEAR(a.fy, -b.fy, 0.01f * fabsf(a.fy) + 1e-3f);         /* mirror image: a gybe is a non-event */
}

static void test_dead_run_has_little_side_force(void) {
    SailForce f = sail_force(main_sail(), D(90), 1, D(180), 8, 0, 1);
    T_ASSERT(f.fx > 50);
    T_ASSERT(fabsf(f.fy) < 0.1f * f.fx);
}

static void test_heel_adds_weather_helm(void) {
    const SailCfg *s = main_sail();
    SailForce f = sail_force(s, D(60), 1, D(-90), 8, D(20), 1);
    float expected = f.fy * s->ce_x - 0.5f * f.fx * s->ce_z * sinf(D(20));
    T_NEAR(f.yaw_moment, expected, 0.001f * fabsf(expected) + 1e-3f);
    T_ASSERT(f.yaw_moment < f.fy * s->ce_x);
}

static void test_heel_helm_mirrors_on_port_tack(void) {
    const SailCfg *s = main_sail();
    SailForce stbd = sail_force(s, D(60), 1, D(-90), 8, D(20), 1);
    SailForce port = sail_force(s, D(60), 1, D(90), 8, D(-20), 1);
    T_NEAR(port.yaw_moment, -stbd.yaw_moment, 0.001f * fabsf(stbd.yaw_moment) + 1e-3f);
}

static void test_upright_has_no_heel_helm(void) {
    const SailCfg *s = main_sail();
    SailForce f = sail_force(s, D(60), 1, D(-90), 8, 0, 1);
    T_NEAR(f.yaw_moment, f.fy * s->ce_x, 1e-3f);
}

static void test_no_depower_below_20_deg(void) {
    const SailCfg *s = main_sail();
    SailForce f0 = sail_force(s, D(0), 1, D(15), 8, 0, 1);
    SailForce f15 = sail_force(s, D(0), 1, D(15), 8, D(15), 1);
    T_NEAR(f15.fy, f0.fy * cosf(D(15)) * cosf(D(15)), 1e-3f * fabsf(f0.fy));
}

static void test_depower_flattens_sail_at_40_deg(void) {
    const SailCfg *s = main_sail();
    SailForce f0 = sail_force(s, D(0), 1, D(15), 8, 0, 1);
    SailForce f40 = sail_force(s, D(0), 1, D(15), 8, D(40), 1);
    /* magnitudes: the sideways force of a heeled sail is smaller than its upright value, sign-agnostic */
    T_ASSERT(fabsf(f40.fy) < 0.75f * fabsf(f0.fy) * cosf(D(40)) * cosf(D(40)));
}

static void test_depower_survives_extreme_heel(void) {
    const SailCfg *s = main_sail();
    float heels[] = { D(105), D(-105), NAN };
    for (int i = 0; i < 3; i++) {
        SailForce f = sail_force(s, D(0), 1, D(60), 8, heels[i], 1);
        T_ASSERT(isfinite(f.fx) && isfinite(f.fy) && isfinite(f.heel_moment) && isfinite(f.yaw_moment));
    }
}

int main(void) {
    T_RUN(test_no_depower_below_20_deg);
    T_RUN(test_depower_flattens_sail_at_40_deg);
    T_RUN(test_depower_survives_extreme_heel);
    T_RUN(test_heel_adds_weather_helm);
    T_RUN(test_heel_helm_mirrors_on_port_tack);
    T_RUN(test_upright_has_no_heel_helm);
    T_RUN(test_beam_reach_drives);
    T_RUN(test_close_hauled_overtrim_stalls);
    T_RUN(test_eased_past_wind_line_luffs);
    T_RUN(test_zero_aoa_luffs_with_no_force);
    T_RUN(test_hoist_zero_gives_zeros);
    T_RUN(test_force_scales_with_aws_squared);
    T_RUN(test_heel_reduces_drive);
    T_RUN(test_slot_multiplier_boosts_force);
    T_RUN(test_port_wind_mirrors_starboard);
    T_RUN(test_degenerate_inputs_are_finite);
    T_RUN(test_awa_window_cuts_out_of_range_sail);
    T_RUN(test_spinnaker_billows_ahead_of_chord_on_a_run);
    T_RUN(test_spinnaker_billow_collapses_when_luffing_and_mirrors);
    T_RUN(test_longship_gybe_is_symmetric);
    T_RUN(test_dead_run_has_little_side_force);
    T_DONE();
}
