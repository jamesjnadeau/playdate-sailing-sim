#ifndef BALANCE_H
#define BALANCE_H
#include <stdbool.h>
#include "boatcfg.h"

/* Helm balance of a boat file, measured by driving boat_step. Shared by tests/test_rudder.c
 * and tools/boat_check.c so contributed boats get the same guard as the shipped ones. */

/* Closest apparent angle (deg) the boat's rig can work at: the largest awa_min_deg among sails
 * that are not spinnakers (shape 2). 0 when there are none. */
float balance_closehauled_limit_deg(const BoatCfg *c);

/* True for a rig that can go to windward: at least one non-spinnaker sail and a limit under 30 deg. */
bool balance_is_fore_and_aft(const BoatCfg *c);

/* Flat water, 10 m/s true wind, auto-trim on, heading held at 45 deg TWA with wind on the
 * starboard side. After 30 s the settled wheel angle, as rudder degrees (wheel * rudder_max_deg),
 * is averaged over 5 s. Positive means the rudder is set to windward (turns the bow toward the
 * wind). False, with *rudder_deg untouched, when the rig is not fore-and-aft or the yaw rate
 * has not settled below 0.02 rad/s. */
bool balance_weather_helm_deg(const BoatCfg *c, float *rudder_deg);

#endif
