#include "controls.h"
#include <math.h>
#include "mathx.h"
#include "camera.h"

void controls_init(Controls *c) {
    c->mode = MODE_WHEEL;
    c->sail = 0;
    c->camera_yaw = 0;
    c->camera_target_yaw = 0;
    c->camera_pitch = 0;
    c->camera_target_pitch = 0;
    c->camera_vertical = false;
    c->camera_recentre = false;
    c->down_time = 0;
    c->down_tracking = c->down_fired = c->down_entered = false;
}

static void select_sail(Controls *c, int count, int advance) {
    if (count < 1) { c->sail = 0; return; }
    if (c->sail < 0 || c->sail >= count) c->sail = 0;
    else if (advance) c->sail = (c->sail + 1) % count;
}

static float ease_to_zero(float v, float step) {
    return fabsf(v) <= step ? 0 : v - copysignf(step, v);
}

void controls_update(Controls *c, const BoatCfg *cfg, uint32_t pressed, uint32_t held,
                     float crank_delta_deg, float dt, BoatInputs *out) {
    const ControlsCfg *k = &cfg->controls;
    int n = cfg->sail_count;
    if (!isfinite(crank_delta_deg)) crank_delta_deg = 0;
    if (!isfinite(dt) || dt < 0) dt = 0;

    if (pressed & BTN_UP) {
        c->mode = MODE_WHEEL;
    } else if (pressed & BTN_RIGHT) {
        select_sail(c, n, c->mode == MODE_TRIM);
        c->mode = MODE_TRIM;
    } else if (pressed & BTN_LEFT) {
        select_sail(c, n, c->mode == MODE_HOIST);
        c->mode = MODE_HOIST;
    } else if (pressed & BTN_DOWN) {
        c->down_tracking = true;
        c->down_time = 0;
        c->down_fired = false;
        c->down_entered = c->mode != MODE_CAMERA;
        c->mode = MODE_CAMERA;
    }
    if (c->down_tracking) {
        if (c->mode != MODE_CAMERA) {
            c->down_tracking = false;
        } else if (held & BTN_DOWN) {
            c->down_time += dt;
            if (!c->down_fired && c->down_time >= k->camera_hold_s) {
                c->down_fired = true;
                c->camera_recentre = true;
            }
        } else {
            if (!c->down_fired && !c->down_entered) c->camera_vertical = !c->camera_vertical;
            c->down_tracking = false;
        }
    }
    if (c->mode != MODE_CAMERA) c->camera_recentre = false;
    select_sail(c, n, 0);

    switch (c->mode) {
    case MODE_WHEEL:
        /* wheel_turns is lock to lock, so full travel (2.0) takes that many turns */
        out->wheel_target = clampf(out->wheel_target
            + crank_delta_deg / 360.0f / k->wheel_turns * 2.0f * k->crank_gain_wheel, -1, 1);
        break;
    case MODE_TRIM:
        if (n > 0) {
            const SailCfg *s = &cfg->sails[c->sail];
            out->trim_target[c->sail] = clampf(out->trim_target[c->sail]
                + deg2rad(crank_delta_deg * k->crank_gain_trim),
                deg2rad(s->trim_min_deg), deg2rad(s->trim_max_deg));
        }
        break;
    case MODE_HOIST:
        if (n > 0)
            out->hoist_target[c->sail] = clampf(out->hoist_target[c->sail]
                + crank_delta_deg * k->crank_gain_hoist, 0, 1);
        break;
    case MODE_CAMERA: {
        if (crank_delta_deg != 0 && c->camera_recentre) {   /* turning the crank takes over from where the view is */
            c->camera_recentre = false;
            c->camera_target_yaw = c->camera_yaw;
            c->camera_target_pitch = c->camera_pitch;
        }
        float d = deg2rad(crank_delta_deg * k->crank_gain_camera);
        if (c->camera_vertical) {
            float lim = deg2rad(cfg->camera.pitch_range_deg), lo, hi;
            camera_look_limits(cfg, &lo, &hi);   /* no crank travel past what the camera can show */
            c->camera_target_pitch = clampf(c->camera_target_pitch + d, fmaxf(-lim, lo), fminf(lim, hi));
        } else {
            c->camera_target_yaw = wrap_pi(c->camera_target_yaw + d);
        }
        if (!c->camera_recentre) {
            c->camera_yaw = c->camera_target_yaw;
            c->camera_pitch = c->camera_target_pitch;
        }
        break;
    }
    }

    if (c->camera_recentre) {
        c->camera_target_yaw = 0;
        c->camera_target_pitch = 0;
        float step = deg2rad(cfg->camera.return_rate) * dt;
        c->camera_yaw = ease_to_zero(c->camera_yaw, step);
        c->camera_pitch = ease_to_zero(c->camera_pitch, step);
        if (c->camera_yaw == 0 && c->camera_pitch == 0) c->camera_recentre = false;
    }
}
