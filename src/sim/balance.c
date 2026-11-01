#include "balance.h"
#include <math.h>
#include "autotrim.h"
#include "boat.h"
#include "mathx.h"

#define SETTLE_S 30.0f
#define AVERAGE_S 5.0f
#define SETTLED_RATE 0.02f      /* rad/s */
#define HOLD_TWA_DEG 45.0f
#define PIN_S 12.0f             /* heading pinned while speed and heel build, so the start-up transient cannot round the boat up */

float balance_closehauled_limit_deg(const BoatCfg *c) {
    float lim = 0.0f;
    for (int i = 0; i < c->sail_count && i < BOAT_MAX_SAILS; i++)
        if (c->sails[i].shape != 2 && c->sails[i].awa_min_deg > lim) lim = c->sails[i].awa_min_deg;
    return lim;
}

bool balance_is_fore_and_aft(const BoatCfg *c) {
    int rigs = 0;
    for (int i = 0; i < c->sail_count && i < BOAT_MAX_SAILS; i++)
        if (c->sails[i].shape != 2) rigs++;
    return rigs > 0 && balance_closehauled_limit_deg(c) < 30.0f;
}

bool balance_weather_helm_deg(const BoatCfg *c, float *rudder_deg) {
    if (!balance_is_fore_and_aft(c)) return false;
    BoatState b;
    BoatInputs in;
    Autotrim at;
    Wind w = {0};
    WaveForces wf = {0};
    boat_init(&b, c);
    boat_inputs_init(&in, c);
    autotrim_init(&at);
    at.on = true;
    w.dir = 0; w.speed = 10.0f;
    const float target = (float)M_PI - deg2rad(HOLD_TWA_DEG);
    b.heading = target;
    b.u = 0.5f * c->hull.hull_speed;

    /* PD plus a slow integral on heading: the integral ends up holding the wheel the boat needs */
    float integ = 0.0f, wheel_sum = 0.0f, rate_sum = 0.0f;
    int n = (int)((SETTLE_S + AVERAGE_S) / SIM_DT), avg_from = (int)(SETTLE_S / SIM_DT), avg_n = 0;
    const int pin_n = (int)(PIN_S / SIM_DT);
    for (int i = 0; i < n; i++) {
        if (i < pin_n) { b.heading = target; b.r = 0.0f; }
        float err = wrap_pi(target - b.heading);
        integ = clampf(integ + 0.05f * err * SIM_DT, -1.0f, 1.0f);
        in.wheel_target = clampf(2.5f * err - 1.5f * b.r + integ, -1.0f, 1.0f);
        autotrim_update(&at, c, &b, &in, SIM_DT);
        boat_step(&b, c, &w, &in, &wf, SIM_DT);
        if (i >= avg_from) { wheel_sum += b.wheel; rate_sum += fabsf(b.r); avg_n++; }
    }
    if (avg_n == 0 || rate_sum / avg_n > SETTLED_RATE) return false;
    *rudder_deg = -wheel_sum / avg_n * c->foils.rudder_max_deg;   /* positive wheel heads up, so weather helm is minus the held wheel */
    return true;
}
