#ifndef HUDINFO_H
#define HUDINFO_H
#include <stdbool.h>
#include "boatcfg.h"
#include "controls.h"

/* Presentation-free HUD decisions, so the headless tests can cover them. */

/* UTF-8 d-pad glyph for the mode's button; "?" for an out-of-range Mode. */
const char *hud_mode_glyph(Mode m);

/* true only for MODE_WHEEL: speed/heading/heel/AWS cells are shown. */
bool hud_shows_nav(Mode m);

/* Radians to whole degrees 0..359: rounded, wrapped, non-finite -> 0. */
int hud_heading_deg(float rad);

/* 0..1 fill for the sail bar. HOIST: hoist as given. TRIM: (trim - min)/(max - min) in
 * the SailCfg's degrees converted from `trim_rad`. Other modes and non-finite inputs -> 0.
 * A zero or negative trim range -> 0. */
float hud_sail_fraction(Mode m, const SailCfg *s, float trim_rad, float hoist);

#endif
