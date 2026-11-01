#include "engine.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

#define ENGINE_ACCEL 0.25f   /* m/s^2 of push on a stopped boat */
#define ENGINE_TAPER 0.2f    /* fraction of ENGINE_SPEED over which the push fades out */

void engine_init(Engine *e) { memset(e, 0, sizeof *e); }

bool engine_buttons(Engine *e, const ControlsCfg *k, uint32_t pressed, uint32_t held, float dt) {
    if (!isfinite(dt) || dt < 0) dt = 0;
    if (pressed & BTN_B) { e->b_tracking = true; e->b_time = 0; e->b_fired = false; }
    if (!e->b_tracking) return false;

    if (held & BTN_B) {
        e->b_time += dt;
        if (!e->b_fired && e->b_time >= k->engine_hold_s) {
            e->b_fired = true;
            e->on = true;
            e->msg_left = ENGINE_MSG_S;
        }
        return false;
    }
    bool tap = !e->b_fired;
    e->b_tracking = false;
    e->b_time = 0;
    return tap;
}

void engine_tick(Engine *e, float dt) {
    if (isfinite(dt) && dt > 0) e->msg_left = fmaxf(e->msg_left - dt, 0.0f);
}

float engine_thrust(float mass, float u) {
    if (!isfinite(mass) || !isfinite(u)) return 0.0f;
    float k = clampf((ENGINE_SPEED - u) / (ENGINE_TAPER * ENGINE_SPEED), 0.0f, 1.0f);
    return mass * ENGINE_ACCEL * k;
}
