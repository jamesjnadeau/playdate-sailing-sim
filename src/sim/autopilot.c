#include "autopilot.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

void autopilot_init(Autopilot *ap) { memset(ap, 0, sizeof *ap); }

float autopilot_next_heading(float from, float wind_from, float step_rad, float no_go_rad) {
    if (!isfinite(from) || !isfinite(wind_from) || !isfinite(step_rad) || step_rad <= 1e-3f)
        return isfinite(from) ? wrap_pi(from) : 0.0f;
    int n = (int)ceilf(2.0f * (float)M_PI / step_rad);
    float cand = from;
    for (int i = 0; i < n; i++) {
        cand = wrap_pi(cand + step_rad);
        if (fabsf(wrap_pi(cand - wind_from)) >= no_go_rad) return cand;
    }
    return wrap_pi(from);
}

void autopilot_buttons(Autopilot *ap, const ControlsCfg *k, uint32_t pressed, uint32_t held,
                       float dt, float heading, float wind_from) {
    if (!isfinite(dt) || dt < 0) dt = 0;
    if (pressed & BTN_A) { ap->a_tracking = true; ap->a_time = 0; ap->a_fired = false; }
    if (!ap->a_tracking) return;

    if (held & BTN_A) {
        ap->a_time += dt;
        if (!ap->a_fired && ap->a_time >= k->autopilot_hold_s) {
            ap->a_fired = true;
            ap->on = !ap->on;
            if (ap->on) ap->target = isfinite(heading) ? wrap_pi(heading) : 0.0f;
        }
    } else {
        if (!ap->a_fired && ap->on)
            ap->target = autopilot_next_heading(ap->target, wind_from,
                             deg2rad(k->autopilot_step_deg), deg2rad(k->autopilot_no_go_deg));
        ap->a_tracking = false;
        ap->a_time = 0;
    }
}

float autopilot_wheel(const Autopilot *ap, const ControlsCfg *k, float heading, float yaw_rate,
                      float wind_from) {
    if (!isfinite(ap->target) || !isfinite(heading) || !isfinite(yaw_rate)) return 0.0f;
    float e = wrap_pi(ap->target - heading);
    if (isfinite(wind_from)) {
        float dw = wrap_pi(wind_from - heading);
        float no_go = deg2rad(k->autopilot_no_go_deg);
        /* the short way crosses the wind: go the long way round instead (a gybe), unless
         * already inside the no-go zone, where the short way is the way out */
        if (fabsf(dw) >= no_go && dw * e > 0 && fabsf(dw) < fabsf(e))
            e -= copysignf(2.0f * (float)M_PI, e);
    }
    return clampf(k->autopilot_kp * e - k->autopilot_kd * yaw_rate, -1, 1);
}
