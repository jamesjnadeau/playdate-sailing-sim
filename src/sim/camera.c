#include "camera.h"
#include <math.h>
#include "mathx.h"

#define TRAIL_PITCH (-0.2f)   /* rad, negative looks down */

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
    float xo = c->trail ? k->far_offset : k->x_offset;
    if (c->trail) side = 0.0f;
    c->x = b->x + (double)(fwd_x * xo - fwd_y * side);
    c->y = b->y + (double)(fwd_y * xo + fwd_x * side);
    c->height = (c->trail ? k->far_height : k->height) + b->heave;

    c->yaw = wrap_pi(b->heading + g->ctl.camera_yaw);

    float limit = deg2rad(k->roll_limit_deg);
    c->roll = approach(c->roll, clampf(b->heel, -limit, limit), k->roll_smooth, dt);
    /* trailing view looks down at the boat so the hull sits above the status panel */
    c->pitch = approach(c->pitch, b->pitch + (c->trail ? TRAIL_PITCH : 0.0f), k->pitch_smooth, dt);

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
