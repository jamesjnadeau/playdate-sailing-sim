#include "autotrim.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

#define SWEEP_STEP_DEG 1.0f
#define HEEL_RELEASE 0.7f   /* hoist comes back up once heel is below this fraction of the limit */

void autotrim_init(Autotrim *at) { memset(at, 0, sizeof *at); }

void autotrim_toggle(Autotrim *at) {
    at->on = !at->on;
    at->primed = false;
}

/* Angle of attack (rad) where lift peaks, i.e. the first lobe of the CL curve. Past it the model has
 * a second, flat-plate lobe that sheeting nearly to the centreline would chase on a broad reach. */
static float stall_aoa(const SailCfg *s) {
    float best = -1.0f, at = 0;
    for (int a = 0; a <= 180; a++) {
        float cl = table_eval(s->cl.p, s->cl.n, (float)a);
        if (cl > best) { best = cl; at = (float)a; }
        else if (cl < best - 0.05f) break;
    }
    return deg2rad(at);
}

float autotrim_best_trim(const SailCfg *s, float awa, float aws, float heel) {
    float lo = deg2rad(s->trim_min_deg), hi = deg2rad(s->trim_max_deg);
    lo = fmaxf(lo, fminf(fabsf(wrap_pi(awa)) - stall_aoa(s), hi));
    float best = -1.0f, best_fx = 1e-3f;
    for (float t = lo; t <= hi + 1e-4f; t += deg2rad(SWEEP_STEP_DEG)) {
        float fx = sail_force(s, t, 1.0f, awa, aws, heel, 1.0f).fx;
        if (fx > best_fx) { best_fx = fx; best = t; }
    }
    return best;
}

void autotrim_update(Autotrim *at, const BoatCfg *cfg, const BoatState *b, BoatInputs *in, float dt) {
    if (!at->on) return;
    if (!isfinite(dt) || dt <= 0) return;
    if (!isfinite(b->awa) || !isfinite(b->aws) || !isfinite(b->heel)) return;
    const ControlsCfg *k = &cfg->controls;

    if (!at->primed) {
        at->awa_s = sinf(b->awa); at->awa_c = cosf(b->awa);
        at->aws = b->aws; at->heel = fabsf(b->heel);
        at->primed = true;
    } else {
        float a = 1.0f - expf(-dt / fmaxf(k->autotrim_smooth_s, 0.05f));
        at->awa_s += a * (sinf(b->awa) - at->awa_s);
        at->awa_c += a * (cosf(b->awa) - at->awa_c);
        at->aws += a * (b->aws - at->aws);
        at->heel += a * (fabsf(b->heel) - at->heel);
    }
    float awa = atan2f(at->awa_s, at->awa_c);

    /* depower by reefing: hoist down while over the heel limit, back up once comfortably under it */
    float limit = deg2rad(k->autotrim_heel_deg);
    float dh = 0;
    if (at->heel > limit) dh = -k->hoist_rate * 0.5f * dt;
    else if (at->heel < limit * HEEL_RELEASE) dh = k->hoist_rate * 0.5f * dt;

    /* a catamaran's sail that cannot reef eases the sheet instead; one ease shared by all such sails */
    bool cat = cfg->hull.hulls == 2;
    float de = 0;
    if (at->heel > limit) de = deg2rad(k->trim_rate_deg) * 0.5f * dt;
    else if (at->heel < limit * HEEL_RELEASE) de = -deg2rad(k->trim_rate_deg) * 0.5f * dt;
    if (cat) at->ease = fmaxf(0.0f, at->ease + de);

    float headroom = 0;   /* most trim any eased sail can still let out, so ease can't wind up past it */
    int n = cfg->sail_count < BOAT_MAX_SAILS ? cfg->sail_count : BOAT_MAX_SAILS;
    for (int i = 0; i < n; i++) {
        if (in->hoist_target[i] <= 0.01f) continue;   /* stowed: not ours to raise */
        bool ease = cat && cfg->sails[i].reef_levels == 0;
        if (!ease) in->hoist_target[i] = clampf(in->hoist_target[i] + dh, k->autotrim_hoist_min, 1.0f);
        float t = autotrim_best_trim(&cfg->sails[i], awa, at->aws, at->heel);
        if (t < 0) continue;
        float mx = deg2rad(cfg->sails[i].trim_max_deg);
        in->trim_target[i] = ease ? fminf(t + at->ease, mx) : t;
        if (ease) headroom = fmaxf(headroom, mx - t);
    }
    if (cat) at->ease = fminf(at->ease, headroom);
}
