#ifndef HUDINFO_H
#define HUDINFO_H
#include <stdbool.h>
#include <stddef.h>
#include "boatcfg.h"
#include "controls.h"

/* Presentation-free HUD decisions, so the headless tests can cover them. */

/* true for MODE_WHEEL and MODE_CAMERA: speed/heading/heel/AWS cells are shown. */
bool hud_shows_nav(Mode m);

/* Radians to whole degrees 0..359: rounded, wrapped, non-finite -> 0. */
int hud_heading_deg(float rad);

/* 0..1 fill for the sail bar. HOIST: hoist as given. TRIM: (trim - min)/(max - min) in
 * the SailCfg's degrees converted from `trim_rad`. Other modes and non-finite inputs -> 0.
 * A zero or negative trim range -> 0. */
float hud_sail_fraction(Mode m, const SailCfg *s, float trim_rad, float hoist);

/* Seconds to "MM:SS.S" (tenths rounded). Negative or non-finite -> 0. Truncated to n. */
void hud_fmt_time(double t, char *out, size_t n);

/* "<name> <index>/<total>  <dist>m" with dist in whole metres. Non-finite or negative
 * dist -> 0. Truncated to n. */
void hud_fmt_mark(const char *name, int index, int total, double dist_m, char *out, size_t n);

/* Banner text while the rudder_min_flow floor is helping the player steer: `rudder_assist` is
 * BoatState.rudder_assist (0..1) and `wheel` the helm (-1..1). Shown only when the floor is most of
 * the flow (assist > 0.5) and the helm is clearly turned (|wheel| > 0.1). NULL when the real flow is enough,
 * the helm is centred, or an input is non-finite. */
const char *hud_assist_text(float rudder_assist, float wheel);

/* Banner text while the engine's "engaged" notice is up (`msg_left` > 0 s); NULL otherwise. */
const char *hud_engine_text(float msg_left);

/* Screen angle on the HUD wind dial (rad, clockwise from the top, wrapped to (-pi, pi]) of something at
 * `angle_from_bow` (rad, + = starboard of the bow) while the view faces `look_yaw` (rad, + = toward
 * starboard). Non-finite look_yaw counts as 0; non-finite angle_from_bow -> 0. */
float hud_dial_angle(float angle_from_bow, float look_yaw);

#endif
