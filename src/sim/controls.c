#include "controls.h"
#include <math.h>
#include "mathx.h"

void controls_init(Controls *c) {
    c->mode = MODE_WHEEL;
    c->sail = 0;
    c->camera_yaw = 0;
    c->camera_target_yaw = 0;
}

static void select_sail(Controls *c, int count, int advance) {
    if (count < 1) { c->sail = 0; return; }
    if (c->sail < 0 || c->sail >= count) c->sail = 0;
    else if (advance) c->sail = (c->sail + 1) % count;
}

void controls_update(Controls *c, const BoatCfg *cfg, uint32_t pressed,
                     float crank_delta_deg, float dt, BoatInputs *out) {
    const ControlsCfg *k = &cfg->controls;
    int n = cfg->sail_count;
    if (!isfinite(crank_delta_deg)) crank_delta_deg = 0;
    if (!isfinite(dt) || dt < 0) dt = 0;

    if (pressed & BTN_A) out->anchor = !out->anchor;

    if (pressed & BTN_UP) {
        c->mode = MODE_WHEEL;
    } else if (pressed & BTN_RIGHT) {
        select_sail(c, n, c->mode == MODE_TRIM);
        c->mode = MODE_TRIM;
    } else if (pressed & BTN_LEFT) {
        select_sail(c, n, c->mode == MODE_HOIST);
        c->mode = MODE_HOIST;
    } else if (pressed & BTN_DOWN) {
        c->mode = MODE_CAMERA;
    }
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
        float lim = deg2rad(cfg->camera.yaw_range_deg);
        c->camera_target_yaw = clampf(c->camera_target_yaw
            + deg2rad(crank_delta_deg * k->crank_gain_camera), -lim, lim);
        c->camera_yaw = c->camera_target_yaw;
        break;
    }
    }

    if (c->mode != MODE_CAMERA) {
        c->camera_target_yaw = 0;
        float step = deg2rad(cfg->camera.return_rate) * dt;
        if (fabsf(c->camera_yaw) <= step) c->camera_yaw = 0;
        else c->camera_yaw -= copysignf(step, c->camera_yaw);
    }
}
