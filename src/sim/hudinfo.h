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

#endif
