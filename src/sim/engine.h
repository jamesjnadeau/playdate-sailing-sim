#ifndef ENGINE_H
#define ENGINE_H
#include <stdbool.h>
#include <stdint.h>
#include "boatcfg.h"
#include "controls.h"

#define ENGINE_SPEED 0.5144f   /* m/s (1 knot): the speed the engine drives the boat up to, never beyond */
#define ENGINE_MSG_S 2.0f      /* s the "engine engaged" banner stays up */

/* Auxiliary engine, switched with B. Holding B for controls.engine_hold_s starts it; a quick press of B
 * (a release before the hold completes) is reported to the caller, who turns the engine off if it is
 * running and toggles the autotrim otherwise. */
typedef struct {
    bool on;
    float msg_left;        /* s of the engaged banner still to show */
    float b_time;          /* s B has been down since its press */
    bool b_tracking;       /* a press was seen and B is still down */
    bool b_fired;          /* this hold already acted */
} Engine;

void engine_init(Engine *e);

/* Call once per frame while sailing. Returns true when B was released as a quick press. */
bool engine_buttons(Engine *e, const ControlsCfg *k, uint32_t pressed, uint32_t held, float dt);

/* Counts the banner down. */
void engine_tick(Engine *e, float dt);

/* Thrust (N) along the rudder's direction for a boat of `mass` kg doing `u` m/s forward: full push when
 * slow, easing to nothing as u reaches ENGINE_SPEED, and no braking beyond it. */
float engine_thrust(float mass, float u);

#endif
