#include "camera.h"
#include <math.h>
#include "mathx.h"

#define TRAIL_RIBBON 0.6f     /* m the ribbon streams above the masthead, allowing for flutter */
#define MIN_EYE 0.5f          /* m, lowest the trailing eye may be lowered above the sea */
#define FOLLOW_MIX_S 0.5f     /* s the eye takes to glide between the orbit and the rudder */
#define HUD_TOP_PX (CAM_H - HUD_H)   /* the status panel covers the screen below this row */

/* Reachable orbit angles about the framed one: lo <= 0 keeps the eye MIN_EYE above sea level,
 * hi >= 0 stops short of straight overhead. Zero look always frames as configured. */
static void orbit_limits(float r, float phi0, float *lo, float *hi) {
    *lo = fminf(asinf(fminf(MIN_EYE / r, 1.0f)) - phi0, 0.0f);
    *hi = fmaxf(deg2rad(85.0f) - phi0, 0.0f);
}

/* Trailing view framing. With the eye at far_height, find the distance behind the
 * boat and the pitch that put the masthead ribbon frame_margin px below the top of
 * the screen and the stern waterline frame_margin px above the status panel.
 * Config values only, so the framing is constant and the sway comes from the boat.
 * With horizon_y set, the pitch instead pins the horizon to that row and only the stern
 * is framed (or trail_dist fixes the distance outright); a tall rig then sits nearer the
 * top of the screen, or crops. */
static void trail_framing(const BoatCfg *cfg, float *dist, float *pitch) {
    const CameraCfg *k = &cfg->camera;
    float m = k->frame_margin;
    float top = atanf((CAM_HORIZON_Y - m) / (float)CAM_FOCAL);
    float bot = atanf((m - (HUD_TOP_PX - CAM_HORIZON_Y)) / (float)CAM_FOCAL);
    float span = top - bot;   /* angle between ribbon and stern as seen from the eye */
    float h = k->far_height, half = 0.5f * cfg->hull.length;
    if (k->horizon_y > 0.0f) {
        float p = atanf((k->horizon_y - CAM_HORIZON_Y) / (float)CAM_FOCAL);
        float a = fmaxf(-bot - p, 0.02f);   /* stern depression below the view axis */
        *dist = k->trail_dist > 0.0f ? k->trail_dist : fminf(half + h / tanf(a), 200.0f);
        *pitch = p;
        return;
    }
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

void camera_look_limits(const BoatCfg *cfg, float *lo, float *hi) {
    float xo, pitch;
    trail_framing(cfg, &xo, &pitch);
    float r = hypotf(xo, cfg->camera.far_height), phi0 = atan2f(cfg->camera.far_height, xo);
    orbit_limits(r, phi0, lo, hi);
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
    float xo = k->x_offset, trail_pitch = 0.0f, eye = k->height, look = g->ctl.camera_pitch;
    bool look_tipped_down = false;
    if (c->trail) {
        trail_framing(cfg, &xo, &trail_pitch);
        /* vertical look pivots the eye about the boat centre at sea level at a fixed radius and
         * tips the view by the same angle, so the boat keeps its screen position */
        float r = hypotf(xo, k->far_height), phi0 = atan2f(k->far_height, xo);
        float lo, hi;
        orbit_limits(r, phi0, &lo, &hi);
        float phi = clampf(phi0 + look, phi0 + lo, phi0 + hi);
        xo = -r * cosf(phi);
        eye = r * sinf(phi);
        trail_pitch -= phi - phi0;
        look_tipped_down = phi < phi0;
        look = 0.0f;
    }
    if (c->trail) side = 0.0f;
    /* buoy-follow sits the eye over the rudder, clear of the mast, and turns it in place */
    c->follow_mix = approach(c->follow_mix, c->trail && g->ctl.camera_follow ? 1.0f : 0.0f, FOLLOW_MIX_S, dt);
    float stern = fminf(cfg->foils.rudder_x, -0.5f * cfg->hull.length) * c->follow_mix;
    xo *= 1.0f - c->follow_mix;
    c->x = b->x + (double)(fwd_x * xo - fwd_y * side + cosf(b->heading) * stern);
    c->y = b->y + (double)(fwd_y * xo + fwd_x * side + sinf(b->heading) * stern);
    c->height = eye + k->heave_gain * b->heave;
    if (c->trail && look_tipped_down) {   /* a lowered eye must clear the wave under it, not just mean sea level */
        float sea = sea_height(&g->sea, (float)c->x, (float)c->y, g->t);
        c->height = fmaxf(c->height, sea + MIN_EYE);
    }

    c->yaw = wrap_pi(b->heading + g->ctl.camera_yaw);

    float limit = deg2rad(k->roll_limit_deg);
    c->roll = approach(c->roll, clampf(b->heel, -limit, limit), k->roll_smooth, dt);
    /* trailing view looks down at the boat so the hull sits above the status panel */
    c->pitch = approach(c->pitch, k->pitch_gain * b->pitch + trail_pitch + look, k->pitch_smooth, dt);

    c->deck_roll = b->heel;
    c->deck_pitch = b->pitch;
    c->deck_bob = b->heave;
}

/* The pose's trig is the same for every point of a frame, so keep the last set. */
typedef struct { float yaw, pitch, roll, cy, sy, cp, sp, cr, sr; bool ok; } PoseTrig;

bool camera_project_rel(const CameraPose *c, float dx, float dy, float wz,
                        float *sx, float *sy, float *depth) {
    static PoseTrig k;
    if (!k.ok || k.yaw != c->yaw || k.pitch != c->pitch || k.roll != c->roll) {
        k.yaw = c->yaw; k.pitch = c->pitch; k.roll = c->roll;
        k.cy = cosf(c->yaw); k.sy = sinf(c->yaw);
        k.cp = cosf(c->pitch); k.sp = sinf(c->pitch);
        k.cr = cosf(c->roll); k.sr = sinf(c->roll);
        k.ok = true;
    }
    float dz = wz - c->height;
    float fwd = dx * k.cy + dy * k.sy;
    float right = -dx * k.sy + dy * k.cy;
    float f = fwd * k.cp + dz * k.sp;
    float up = -fwd * k.sp + dz * k.cp;
    if (!(f > 0.05f)) return false;

    float px = CAM_FOCAL * right / f;
    float py = CAM_FOCAL * up / f;
    float rx = px * k.cr - py * k.sr;
    float ry = px * k.sr + py * k.cr;
    if (sx) *sx = CAM_W * 0.5f + rx;
    if (sy) *sy = CAM_HORIZON_Y - ry;
    if (depth) *depth = f;
    return true;
}

bool camera_project(const CameraPose *c, double wx, double wy, float wz,
                    float *sx, float *sy, float *depth) {
    return camera_project_rel(c, (float)(wx - c->x), (float)(wy - c->y), wz, sx, sy, depth);
}

void camera_mark_view(const CameraPose *c, double wx, double wy, float bottom, MarkView *out) {
    const float pad = MARK_EDGE_PAD;
    const float cx = CAM_W * 0.5f, cy = bottom * 0.5f;
    const float hw = CAM_W * 0.5f - pad, hh = bottom * 0.5f - pad;
    float sx, sy;
    out->on_screen = false;
    out->x = CAM_W - pad;
    out->y = CAM_HORIZON_Y;
    out->angle = 0;
    if (camera_project(c, wx, wy, MARK_IND_Z, &sx, &sy, NULL)) {
        if (sx >= 0 && sx < CAM_W && sy >= 0 && sy < bottom) {
            out->on_screen = true;
            out->x = sx;
            out->y = sy;
            return;
        }
        /* projected but off screen: pull it to the padded box along the line from its centre */
        float dx = sx - cx, dy = sy - cy;
        float t = 1.0f;
        if (fabsf(dx) > 1e-6f) t = fminf(t, hw / fabsf(dx));
        if (fabsf(dy) > 1e-6f) t = fminf(t, hh / fabsf(dy));
        out->x = cx + dx * t;
        out->y = cy + dy * t;
        out->angle = atan2f(dy, dx);
    } else {
        /* behind the camera: the side it is on decides the edge */
        float rel = wrap_pi((float)(atan2(wy - c->y, wx - c->x) - c->yaw));
        if (rel >= 0) {
            out->x = CAM_W - pad;
            out->angle = 0;
        } else {
            out->x = pad;
            out->angle = (float)M_PI;
        }
        out->y = CAM_HORIZON_Y;
    }
    if (!(isfinite(out->x) && isfinite(out->y) && isfinite(out->angle))) {
        out->x = CAM_W - pad;
        out->y = CAM_HORIZON_Y;
        out->angle = 0;
    }
}
