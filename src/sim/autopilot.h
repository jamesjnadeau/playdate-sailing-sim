#ifndef AUTOPILOT_H
#define AUTOPILOT_H
#include <stdbool.h>
#include <stdint.h>
#include "boatcfg.h"
#include "controls.h"

/* Heading-hold autopilot. Hold A to toggle; while on, a tap of A advances the
 * target to the next heading that is not into the wind. */
typedef struct {
    bool on;
    float target;          /* rad, world heading to hold */
    float a_time;          /* s A has been down since its press */
    bool a_tracking;       /* a press was seen and A is still down */
    bool a_fired;          /* this hold already toggled */
} Autopilot;

void autopilot_init(Autopilot *ap);

/* Call once per frame while sailing. heading and wind_from are world radians;
 * wind_from is where the wind comes FROM. */
void autopilot_buttons(Autopilot *ap, const ControlsCfg *k, uint32_t pressed, uint32_t held,
                       float dt, float heading, float wind_from);

/* Next target after `from`: +step increments, skipping those within no_go_rad of
 * wind_from. Returns `from` unchanged if every increment is blocked. Wrapped to (-pi, pi]. */
float autopilot_next_heading(float from, float wind_from, float step_rad, float no_go_rad);

/* Crank turn (degrees) nudges the target: autopilot_crank_deg per full turn, clockwise = +heading.
 * No-op when off or non-finite. */
void autopilot_crank(Autopilot *ap, const ControlsCfg *k, float crank_delta_deg);

/* PD law -> wheel_target in -1..1. Turns the long way round rather than through the
 * no-go zone around wind_from, unless heading is already inside it. Non-finite inputs give 0. */
float autopilot_wheel(const Autopilot *ap, const ControlsCfg *k, float heading, float yaw_rate,
                      float wind_from);

#endif
