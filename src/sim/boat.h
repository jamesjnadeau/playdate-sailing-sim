#ifndef BOAT_H
#define BOAT_H
#include <stdbool.h>
#include "boatcfg.h"
#include "weather.h"
#include "sail.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef SIM_DT
#define SIM_DT (1.0f / 60.0f)
#endif

/* World frame: heading h points the bow along (cos h, sin h); starboard is
 * (-sin h, cos h), so positive yaw turns the bow to starboard. Boat frame:
 * x forward, y starboard. Angles are radians, heel > 0 leans to starboard. */

typedef struct {
    double x, y;                 /* world position, m (double: far-from-origin precision) */
    float heading;               /* rad, wrapped to (-pi, pi] */
    float u, v, r;               /* surge, sway (m/s), yaw rate (rad/s) */
    float heel, heel_rate;
    float wheel;                 /* -1..1, rudder = wheel * rudder_max */
    float trims[BOAT_MAX_SAILS]; /* rad, boom angle off centreline */
    float hoists[BOAT_MAX_SAILS];/* 0..1 flying area */
    float anchor;                /* 0 free .. 1 holding */
    /* derived each step, for HUD / camera / render */
    float awa, aws;
    float luff[BOAT_MAX_SAILS];
    float swing[BOAT_MAX_SAILS], swing_v[BOAT_MAX_SAILS]; /* boom side: -1 port .. +1 starboard, eased across on a tack/gybe */
    float helm_load;             /* rudder side force, N (+ pushes stern to starboard) */
    float pitch, heave;          /* copied from WaveForces */
} BoatState;

typedef struct {
    float wheel_target;
    float trim_target[BOAT_MAX_SAILS];
    float hoist_target[BOAT_MAX_SAILS];
    bool anchor;
} BoatInputs;

/* Added to the flat-water forces; all zeros means a flat sea. */
typedef struct {
    float drag_mult;             /* extra hull drag fraction */
    float surge;                 /* N */
    float yaw;                   /* N.m */
    float heel;                  /* N.m */
    float pitch, heave;          /* camera only */
} WaveForces;

void boat_init(BoatState *b, const BoatCfg *cfg);
void boat_inputs_init(BoatInputs *in, const BoatCfg *cfg);   /* matches boat_init's trims/hoists */

/* Apparent wind in the boat frame: awa from the bow, + from starboard. */
void boat_apparent_wind(const BoatState *b, const Wind *w, float *awa, float *aws);

/* dt <= 0 or non-finite is ignored; dt is capped at 0.05 s. */
void boat_step(BoatState *b, const BoatCfg *cfg, const Wind *w, const BoatInputs *in,
               const WaveForces *wf, float dt);

#endif
