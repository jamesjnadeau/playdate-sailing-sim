#include "boat.h"
#include <math.h>
#include <string.h>

#define RHO_W 1000.0f
#define G 9.81f
#define ADDED_X 1.1f        /* added-mass factors */
#define ADDED_Y 1.8f
#define ANCHOR_K 6000.0f    /* N per m/s while holding */
#define HIKE_FULL_DEG 6.0f
#define HULL_WALL_K 3000.0f
#define MAX_HEEL 1.83f      /* rad, ~105 deg */

static float fin(float x, float fallback) { return isfinite(x) ? x : fallback; }

static float approach(float cur, float target, float rate, float dt) {
    float d = target - cur, s = rate * dt;
    if (d > s) return cur + s;
    if (d < -s) return cur - s;
    return target;
}

/* Table lookup that continues along the last segment's slope past the end. */
static float resist(const Table *t, float speed) {
    if (t->n < 2) return table_eval(t->p, t->n, speed);
    const Pt *a = &t->p[t->n - 2], *b = &t->p[t->n - 1];
    if (speed <= b->x) return table_eval(t->p, t->n, speed);
    return b->y + (b->y - a->y) / (b->x - a->x) * (speed - b->x);
}

/* Flat-plate-ish foil: linear to stall, then a plateau at 70% of peak. */
static float foil_cl(float slope, float stall, float a) {
    float m = fabsf(a), peak = slope * stall;
    float cl;
    if (m <= stall) cl = slope * m;
    else if (m < 2 * stall) cl = peak * (1.0f - 0.3f * (m - stall) / stall);
    else cl = 0.7f * peak;
    return a < 0 ? -cl : cl;
}

/* Rudder: lift holds its stall peak, so more wheel never means less turning force. */
static float foil_cl_hold(float slope, float stall, float a) {
    float m = fabsf(a), cl = slope * (m < stall ? m : stall);
    return a < 0 ? -cl : cl;
}

static float foil_cd(float cd0, float cl, float ar) { return cd0 + cl * cl / ((float)M_PI * ar); }

void boat_init(BoatState *b, const BoatCfg *c) {
    memset(b, 0, sizeof *b);
    for (int i = 0; i < c->sail_count && i < BOAT_MAX_SAILS; i++) {
        const SailCfg *s = &c->sails[i];
        b->trims[i] = clampf(deg2rad(30.0f), deg2rad(s->trim_min_deg), deg2rad(s->trim_max_deg));
        b->hoists[i] = s->shape == 2 ? 0.0f : 1.0f;   /* spinnaker starts stowed */
        b->swing[i] = -1.0f;
    }
}

void boat_inputs_init(BoatInputs *in, const BoatCfg *c) {
    BoatState b;
    boat_init(&b, c);
    memset(in, 0, sizeof *in);
    for (int i = 0; i < BOAT_MAX_SAILS; i++) {
        in->trim_target[i] = b.trims[i];
        in->hoist_target[i] = b.hoists[i];
    }
}

void boat_apparent_wind(const BoatState *b, const Wind *w, float *awa, float *aws) {
    float wx, wy;
    wind_vec(w, &wx, &wy);
    float fx = cosf(b->heading), fy = sinf(b->heading);
    float sx = -fy, sy = fx;
    float bx = b->u * fx + b->v * sx, by = b->u * fy + b->v * sy;
    float ax = wx - bx, ay = wy - by;
    float ab = ax * fx + ay * fy, as = ax * sx + ay * sy;
    float spd = hypotf(ab, as);
    *aws = fin(spd, 0.0f);
    *awa = spd < 1e-5f ? 0.0f : atan2f(-as, -ab);
}

void boat_step(BoatState *b, const BoatCfg *c, const Wind *w, const BoatInputs *in,
               const WaveForces *wf, float dt) {
    if (!(dt > 0.0f) || !isfinite(dt)) return;
    if (dt > 0.05f) dt = 0.05f;
    const HullCfg *h = &c->hull;
    const FoilCfg *f = &c->foils;

    /* --- actuators --- */
    float wheel_rate = c->controls.rudder_rate_deg / fmaxf(f->rudder_max_deg, 1.0f);
    b->wheel = approach(b->wheel, clampf(fin(in->wheel_target, 0.0f), -1, 1), wheel_rate, dt);
    for (int i = 0; i < c->sail_count && i < BOAT_MAX_SAILS; i++) {
        const SailCfg *s = &c->sails[i];
        float tt = clampf(fin(in->trim_target[i], b->trims[i]), deg2rad(s->trim_min_deg), deg2rad(s->trim_max_deg));
        b->trims[i] = approach(b->trims[i], tt, deg2rad(c->controls.trim_rate_deg), dt);
        float ht = clampf(fin(in->hoist_target[i], b->hoists[i]), 0, 1);
        b->hoists[i] = approach(b->hoists[i], ht, c->controls.hoist_rate, dt);
    }
    float at = in->anchor ? 1.0f : 0.0f;
    float arate = c->controls.anchor_time > 1e-3f ? 1.0f / c->controls.anchor_time : 1e6f;
    b->anchor = approach(b->anchor, at, arate, dt);

    /* --- sails --- */
    float awa, aws;
    boat_apparent_wind(b, w, &awa, &aws);
    b->awa = awa;
    b->aws = aws;
    float sfx = 0, sfy = 0, sheel = 0, syaw = 0;
    for (int i = 0; i < c->sail_count && i < BOAT_MAX_SAILS; i++) {
        float mult = 1.0f;
        for (int j = 0; j < c->sail_count && j < BOAT_MAX_SAILS; j++)
            if (j != i && c->sails[j].ce_x > c->sails[i].ce_x)
                mult += c->sails[j].slot_gain * b->hoists[j];
        SailForce sf = sail_force(&c->sails[i], b->trims[i], b->hoists[i], awa, aws, b->heel, mult);
        sfx += sf.fx; sfy += sf.fy; sheel += sf.heel_moment; syaw += sf.yaw_moment;
        b->luff[i] = sf.luff;
        b->sail[i] = sf;
        /* the boom is flung across by the wind: a damped spring toward the leeward side, stiffer in more wind */
        float k = 6.0f + 1.5f * fminf(aws, 15.0f), cdamp = 1.4f * sqrtf(k);
        float target = awa >= 0 ? -1.0f : 1.0f;
        b->swing_v[i] += (k * (target - b->swing[i]) - cdamp * b->swing_v[i]) * dt;
        b->swing[i] += b->swing_v[i] * dt;
        if (fabsf(b->swing[i]) > 1.0f) { b->swing[i] = b->swing[i] > 0 ? 1.0f : -1.0f; b->swing_v[i] = 0.0f; }
    }

    b->sail_fx = sfx; b->sail_fy = sfy; b->sail_heel = sheel; b->sail_yaw = syaw;

    /* --- hull and foils (forces in the boat frame) --- */
    float u = b->u, v = b->v, r = b->r;
    float speed_u = fabsf(u);
    /* hull-speed wall: wave-making drag climbs steeply past ~90% of hull speed */
    float wall = HULL_WALL_K * fmaxf(speed_u / h->hull_speed - 0.9f, 0.0f);
    float drag = (resist(&h->resistance, speed_u) + wall) * (1.0f + h->heel_drag * b->heel * b->heel)
               * (1.0f + fmaxf(wf->drag_mult, -0.5f));
    float fx_hull = u >= 0 ? -drag : drag;
    float fy_hull = -0.5f * RHO_W * (h->length * 0.1f) * v * fabsf(v);

    float vb = v + r * f->board_x;
    float sb = sqrtf(u * u + vb * vb);
    float alpha_b = atan2f(vb, speed_u + 1e-3f);
    float cl_b = foil_cl(f->board_lift_slope, deg2rad(f->board_stall_deg), alpha_b);
    float qa_b = 0.5f * RHO_W * (u * u + vb * vb) * f->board_area;
    float fy_board = -qa_b * cl_b;
    float fx_board = sb > 1e-4f ? -qa_b * foil_cd(f->board_cd0, cl_b, 4.0f) * u / sb : 0.0f;

    float vr = v + r * f->rudder_x;
    float sr = sqrtf(u * u + vr * vr);
    float delta = b->wheel * deg2rad(f->rudder_max_deg);
    float alpha_r = delta + atan2f(vr, speed_u + 1e-3f);
    float cl_r = foil_cl_hold(f->rudder_lift_slope, deg2rad(f->rudder_stall_deg), alpha_r);
    float qa_r = 0.5f * RHO_W * (u * u + vr * vr) * f->rudder_area;
    float fy_rudder = -qa_r * cl_r;
    float fx_rudder = sr > 1e-4f ? -qa_r * foil_cd(0.01f, cl_r, 3.0f) * u / sr : 0.0f;
    b->helm_load = fy_rudder;

    float fx = sfx + fx_hull + fx_board + fx_rudder + wf->surge;
    float fy = sfy + fy_hull + fy_board + fy_rudder;
    float mz = syaw + fy_board * f->board_x + fy_rudder * f->rudder_x
             - h->yaw_damping * r + wf->yaw;
    float mx = sheel + (fy_hull + fy_board + fy_rudder) * -h->clr_depth;

    /* --- heel --- */
    float heel_deg = fabsf(b->heel) * (180.0f / (float)M_PI);
    float rm = table_eval(h->righting.p, h->righting.n, heel_deg);
    float hike = clampf(b->heel / deg2rad(HIKE_FULL_DEG), -1, 1);
    float crew = h->crew_mass * G * h->hiking_arm * fmaxf(cosf(b->heel), 0.0f) * hike;
    float heel_m = mx + wf->heel - (b->heel >= 0 ? rm : -rm) - crew - h->heel_damping * b->heel_rate;
    b->heel_rate += heel_m / h->heel_inertia * dt;
    b->heel = clampf(b->heel + b->heel_rate * dt, -MAX_HEEL, MAX_HEEL);
    if (fabsf(b->heel) >= MAX_HEEL) b->heel_rate = 0;

    /* --- anchor --- */
    fx -= b->anchor * ANCHOR_K * u;
    fy -= b->anchor * ANCHOR_K * v;

    /* --- integrate (semi-implicit) --- */
    float m = h->mass + h->crew_mass;
    b->u += (fx / (m * ADDED_X) + v * r) * dt;
    b->v += (fy / (m * ADDED_Y) - u * r) * dt;
    b->r += mz / h->yaw_inertia * dt;
    b->heading = wrap_pi(b->heading + b->r * dt);

    float cx = cosf(b->heading), cy = sinf(b->heading);
    b->x += (double)(b->u * cx - b->v * cy) * dt;
    b->y += (double)(b->u * cy + b->v * cx) * dt;

    b->pitch = fin(wf->pitch, 0.0f);
    b->heave = fin(wf->heave, 0.0f);

    /* a bad input must not poison the state */
    if (!isfinite(b->u) || !isfinite(b->v) || !isfinite(b->r) || !isfinite(b->heel) ||
        !isfinite(b->heel_rate) || !isfinite(b->x) || !isfinite(b->y)) {
        b->u = b->v = b->r = b->heel = b->heel_rate = 0;
        if (!isfinite(b->x)) b->x = 0;
        if (!isfinite(b->y)) b->y = 0;
    }
}
