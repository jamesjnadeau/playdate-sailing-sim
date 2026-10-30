#include "camera.h"
#include <math.h>
#include "mathx.h"

#define TRAIL_RIBBON 0.6f     /* m the ribbon streams above the masthead, allowing for flutter */
#define HUD_TOP_PX 204        /* the status panel covers the screen below this row */

/* Trailing view framing. With the eye at far_height, find the distance behind the
 * boat and the pitch that put the masthead ribbon frame_margin px below the top of
 * the screen and the stern waterline frame_margin px above the status panel.
 * Config values only, so the framing is constant and the sway comes from the boat. */
static void trail_framing(const BoatCfg *cfg, float *dist, float *pitch) {
    const CameraCfg *k = &cfg->camera;
    float m = k->frame_margin;
    float top = atanf((CAM_HORIZON_Y - m) / (float)CAM_FOCAL);
    float bot = atanf((m - (HUD_TOP_PX - CAM_HORIZON_Y)) / (float)CAM_FOCAL);
    float span = top - bot;   /* angle between ribbon and stern as seen from the eye */
    float h = k->far_height, half = 0.5f * cfg->hull.length;
    float zt = cfg->look.mast_height + TRAIL_RIBBON - h;
    float lo = half + 0.5f, hi = 200.0f;
    for (int i = 0; i < 40; i++) {   /* the span the boat covers shrinks as the eye backs off */
        float d = 0.5f * (lo + hi);
        float cover = atan2f(zt, d + cfg->look.mast_x) + atan2f(h, d - half);
        if (cover > span) lo = d; else hi = d;
    }
    *dist = 0.5f * (lo + hi);
    *pitch = atan2f(zt, *dist + cfg->look.mast_x) - top;
}

/* Exponential approach; smooth is a time constant in seconds (0 = immediate). */
static float approach(float cur, float target, float smooth, float dt) {
    if (smooth <= 1e-6f) return target;
    return cur + (target - cur) * (1.0f - expf(-dt / smooth));
}

void camera_update(CameraPose *c, const Game *g, const BoatCfg *cfg, float dt) {
    if (!isfinite(dt)) dt = 1.0f / 30.0f;
    dt = clampf(dt, GAME_DT_MIN, GAME_DT_MAX);
    const CameraCfg *k = &cfg->camera;
    const BoatState *b = &g->b;

    /* trailing view orbits the boat with the look yaw; the near view turns in place */
    float orbit = c->trail ? b->heading + g->ctl.camera_yaw : b->heading;
    float fwd_x = cosf(orbit), fwd_y = sinf(orbit);
    /* heel_bias shifts the eye to the high side as the boat leans */
    float side = -k->heel_bias * sinf(b->heel);
    float xo = k->x_offset, trail_pitch = 0.0f;
    if (c->trail) {
        trail_framing(cfg, &xo, &trail_pitch);
        xo = -xo;
    }
    if (c->trail) side = 0.0f;
    c->x = b->x + (double)(fwd_x * xo - fwd_y * side);
    c->y = b->y + (double)(fwd_y * xo + fwd_x * side);
    c->height = (c->trail ? k->far_height : k->height) + b->heave;

    c->yaw = wrap_pi(b->heading + g->ctl.camera_yaw);

    float limit = deg2rad(k->roll_limit_deg);
    c->roll = approach(c->roll, clampf(b->heel, -limit, limit), k->roll_smooth, dt);
    /* trailing view looks down at the boat so the hull sits above the status panel */
    c->pitch = approach(c->pitch, b->pitch + trail_pitch, k->pitch_smooth, dt);

    c->deck_roll = b->heel;
    c->deck_pitch = b->pitch;
    c->deck_bob = b->heave;
}

bool camera_project(const CameraPose *c, double wx, double wy, float wz,
                    float *sx, float *sy, float *depth) {
    float dx = (float)(wx - c->x), dy = (float)(wy - c->y), dz = wz - c->height;
    float cy = cosf(c->yaw), sy_ = sinf(c->yaw);
    float fwd = dx * cy + dy * sy_;
    float right = -dx * sy_ + dy * cy;
    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    float f = fwd * cp + dz * sp;
    float up = -fwd * sp + dz * cp;
    if (!(f > 0.05f)) return false;

    float px = CAM_FOCAL * right / f;
    float py = CAM_FOCAL * up / f;
    float cr = cosf(c->roll), sr = sinf(c->roll);
    float rx = px * cr - py * sr;
    float ry = px * sr + py * cr;
    if (sx) *sx = CAM_W * 0.5f + rx;
    if (sy) *sy = CAM_HORIZON_Y - ry;
    if (depth) *depth = f;
    return true;
}
