#ifndef HUDINFO_H
#define HUDINFO_H
#include <stdbool.h>
#include <stddef.h>
#include "boatcfg.h"
#include "controls.h"

/* Presentation-free HUD decisions, so the headless tests can cover them. */

/* Right-justified instrument cluster of the status bar. Positions are the left pixel of each text or
 * icon; widths are measured by the caller. Right to left: wind dial, wind speed, boat speed, heel (icon +
 * number), heading (icon + number), then the AP and AT icons, with 1 px separators. */
#define HUD_DIAL_CX 388
#define HUD_ICON_HDG_W 11
#define HUD_ICON_HEEL_W 12
#define HUD_ICON_AT_W 12
#define HUD_ICON_AP_W 12
typedef struct {
    int aws_x;                  /* wind-speed text; its right edge is x 376 */
    int heel_x, heel_icon_x;
    int hdg_x, hdg_icon_x;
    int speed_x;
    int ap_x, ap_icon_x;        /* -1 when AP is off */
    int at_icon_x;              /* -1 when AT is off */
    int sep[5]; int nsep;       /* x of each 1 px separator */
    int left;                   /* leftmost pixel used by the cluster */
} HudCluster;
HudCluster hud_cluster_layout(int w_aws, int w_heel, int w_hdg, int w_speed, int w_ap, bool at, bool ap);

/* Trim/hoist bar placement. The sail name starts at x 60. Returns true when the name is shown: it fits with
 * a bar of at least 24 px after it (bar at x 100 or later, at most 64 wide, ending 6 px before `left`).
 * Otherwise the name is dropped and the bar starts at x 60 and takes the room (at most 64 wide); width 0
 * (no bar) when under 16 px would be left. */
bool hud_sail_bar_span(int name_w, int left, int *x, int *w);

/* Helm indicator track in HELM mode, in the same gap the sail bar uses: starts at x 60, ends 6 px before
 * `left`, at most 80 px, and always an odd width so the track has a true centre pixel. Returns false
 * (with *w = 0) when under 15 px would be left. */
bool hud_helm_span(int left, int *x, int *w);

/* Centre pixel of the rudder marker on a track [x, x + w): x + w / 2 for a centred helm, one half-width
 * right at wheel +1 (bow turning to starboard) and left at -1, rounded. `wheel` is clamped to -1..1;
 * non-finite -> centre. */
int hud_helm_marker_x(int x, int w, float wheel);

/* Left edge for a live text of width text_w right-aligned in a slot [slot_x, slot_x + slot_w): the slot is
 * sized for the widest value, so the numbers stay put as the digits change. A text wider than the slot
 * starts at the slot's left edge. */
int hud_slot_text_x(int slot_x, int slot_w, int text_w);

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

/* Banner text for as long as the engine is running; NULL otherwise. */
const char *hud_engine_text(bool running);

/* Screen angle on the HUD wind dial (rad, clockwise from the top, wrapped to (-pi, pi]) of something at
 * `angle_from_bow` (rad, + = starboard of the bow) while the view faces `look_yaw` (rad, + = toward
 * starboard). Non-finite look_yaw counts as 0; non-finite angle_from_bow -> 0. */
float hud_dial_angle(float angle_from_bow, float look_yaw);
/* True wind's from-direction relative to the bow, in (-pi, pi]: 0 = head to wind.
 * wind_dir is where the wind blows toward. 0 for a non-finite input. */
float hud_true_wind_from_bow(float heading, float wind_dir);
/* Is pixel (dx, dy) off the dial centre (screen y down) part of the no-go wedge?
 * centre: wedge direction on the dial (0 = up, clockwise); half: half-angle.
 * Lights a 50% checkerboard strictly inside a ring of radius r. */
bool hud_no_go_pixel(int dx, int dy, int r, float center, float half);

#endif
