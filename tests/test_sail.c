#include "t.h"
#include "sail.h"
#include "boatcfg.h"
#include <math.h>

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

int main(void) {
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
    T_DONE();
}
