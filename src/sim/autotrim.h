#ifndef AUTOTRIM_H
#define AUTOTRIM_H
#include <stdbool.h>
#include <stdint.h>
#include "boat.h"
#include "boatcfg.h"
#include "controls.h"

/* Auto sail trim. Hold B to toggle; a short tap of B is left for the anchor. While on, each
 * flying sail's trim target follows the angle that gives the most drive at the heading the
 * boat is on, and the hoist eases down when the boat heels too far and back up when it
 * flattens. It works from low-pass-filtered wind and heel, so gusts don't make it jerky. */
typedef struct {
    bool on;
    float awa_s, awa_c;    /* filtered apparent wind angle, as sin/cos so it wraps cleanly */
    float aws;             /* filtered apparent wind speed, m/s */
    float heel;            /* filtered |heel|, rad */
    bool primed;           /* filters hold real data */
    float b_time;          /* s B has been down since its press */
    bool b_tracking;       /* a press was seen and B is still down */
    bool b_fired;          /* this hold already toggled */
} Autotrim;

void autotrim_init(Autotrim *at);

/* Call once per frame while sailing. Returns true when B was released as a short tap
 * (the caller toggles the anchor). */
bool autotrim_buttons(Autotrim *at, const ControlsCfg *k, uint32_t pressed, uint32_t held, float dt);

/* Trim (rad) from trim_min..trim_max with the largest forward force for this apparent wind.
 * Returns a negative value when the sail makes no drive here (flogging or out of its window). */
float autotrim_best_trim(const SailCfg *s, float awa, float aws, float heel);

/* Edits in->trim_target / hoist_target while on; does nothing when off. Sails stowed
 * (hoist target 0) are left alone, so a spinnaker the player hasn't hoisted stays down. */
void autotrim_update(Autotrim *at, const BoatCfg *cfg, const BoatState *b, BoatInputs *in, float dt);

#endif
