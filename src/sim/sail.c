#include "sail.h"
#include <math.h>

#define RHO_AIR 1.2f
#define PI_F 3.14159265358979f
#define AWS_MAX 100.0f
#define WINDOW_RAMP_DEG 5.0f

SailForce sail_force(const SailCfg *s, float trim_rad, float hoist, float awa_rad,
                     float aws, float heel_rad, float slot_cl_mult) {
    SailForce z = {0, 0, 0, 0, 0, 0};
    if (!isfinite(aws) || !isfinite(hoist) || !isfinite(trim_rad) || !isfinite(heel_rad)) return z;
    aws = clampf(aws, 0, AWS_MAX);
    hoist = clampf(hoist, 0, 1);
    if (aws <= 0 || hoist <= 0) return z;

    float awa = wrap_pi(awa_rad);
    float a = fabsf(awa);
    float sgn = awa < 0 ? -1.0f : 1.0f;

    float aoa = a - trim_rad;
    float luff = 0;
    float lim = deg2rad(s->luff_aoa_deg);
    if (aoa < lim) luff = lim > 0 ? clampf((lim - aoa) / lim, 0, 1) : (aoa < 0 ? 1.0f : 0.0f);

    /* outside the sail's working wind-angle window it is simply flogging */
    float ad = a * 180.0f / PI_F, win = 1;
    if (s->awa_min_deg > 0) win *= clampf((ad - s->awa_min_deg) / WINDOW_RAMP_DEG, 0, 1);
    if (s->awa_max_deg < 180) win *= clampf((s->awa_max_deg - ad) / WINDOW_RAMP_DEG, 0, 1);
    luff = luff + (1 - luff) * (1 - win);

    float aoa_deg = fmaxf(aoa, 0) * 180.0f / PI_F;
    float cl = table_eval(s->cl.p, s->cl.n, aoa_deg) * slot_cl_mult * (1 - luff);
    float cd = table_eval(s->cd.p, s->cd.n, aoa_deg) + luff * s->luff_drag;

    float q = 0.5f * RHO_AIR * aws * aws;
    float area = s->area * hoist * clampf(cosf(heel_rad), 0, 1);
    float qa = q * area * win;

    /* flow direction (air velocity) and lift direction (perpendicular, leeward) */
    float fxd = -cosf(a), fyd = -sgn * sinf(a);
    float lxd = sinf(a),  lyd = -sgn * cosf(a);
    z.fx = qa * (cl * lxd + cd * fxd);
    z.fy = qa * (cl * lyd + cd * fyd);
    z.heel_moment = z.fy * s->ce_z;
    z.yaw_moment = z.fy * s->ce_x;
    z.luff = luff;
    z.aoa = aoa;
    return z;
}
