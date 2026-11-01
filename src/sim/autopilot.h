#ifndef AUTOPILOT_H
#define AUTOPILOT_H
#include <stdbool.h>
#include <stdint.h>
#include "boatcfg.h"
#include "controls.h"

/* Heading-hold autopilot. A press of A toggles it; the crank nudges the target. */
typedef struct {
    bool on;
    float target;          /* rad, world heading to hold */
} Autopilot;

void autopilot_init(Autopilot *ap);

/* Call once per frame while sailing. `heading` is the world radians the target takes when it comes on. */
void autopilot_buttons(Autopilot *ap, uint32_t pressed, float heading);

/* Crank turn (degrees) nudges the target: autopilot_crank_deg per full turn, clockwise = +heading.
 * No-op when off or non-finite. */
void autopilot_crank(Autopilot *ap, const ControlsCfg *k, float crank_delta_deg);

/* PD law -> wheel_target in -1..1. Turns the long way round rather than through the
 * no-go zone around wind_from, unless heading is already inside it. Non-finite inputs give 0. */
float autopilot_wheel(const Autopilot *ap, const ControlsCfg *k, float heading, float yaw_rate,
                      float wind_from);

#endif
