#include "autopilot.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

void autopilot_init(Autopilot *ap) { memset(ap, 0, sizeof *ap); }

void autopilot_buttons(Autopilot *ap, uint32_t pressed, float heading) {
    if (!(pressed & BTN_A)) return;
    ap->on = !ap->on;
    if (ap->on) ap->target = isfinite(heading) ? wrap_pi(heading) : 0.0f;
}

void autopilot_crank(Autopilot *ap, const ControlsCfg *k, float crank_delta_deg) {
    if (!ap->on || !isfinite(crank_delta_deg) || !isfinite(ap->target)) return;
    ap->target = wrap_pi(ap->target + deg2rad(crank_delta_deg / 360.0f * k->autopilot_crank_deg));
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
