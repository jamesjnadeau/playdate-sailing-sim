#ifndef SAIL_H
#define SAIL_H
#include "boatcfg.h"

/* Boat frame: x forward, y starboard, z down. Angles in radians.
 * heel_moment > 0 rolls toward starboard; yaw_moment > 0 turns the bow to
 * starboard. aoa is the signed angle of attack (negative = eased past the
 * wind line). luff is 0 (drawing) .. 1 (flogging). */
typedef struct { float fx, fy, heel_moment, yaw_moment, luff, aoa; } SailForce;

/* awa: direction the apparent wind comes FROM, relative to the bow, positive
 * from starboard. trim: boom angle off the centreline (magnitude; the sail
 * always sits on the leeward side). hoist: 0..1 fraction of area flying.
 * slot_cl_mult scales lift (the sail ahead's slot effect). */
SailForce sail_force(const SailCfg *s, float trim_rad, float hoist, float awa_rad,
                     float aws, float heel_rad, float slot_cl_mult);

/* Billow of a drawing spinnaker, as a boat-frame (x, y) offset for the mesh point at u (up the luff, 0 foot ..
 * 1 head) and v (across the chord, 0 luff .. 1 leech). (cx, cy) is the flat chord at that height, from the luff
 * point to the clew side. The belly pushes perpendicular to the chord on the side the air flows toward (ahead and
 * to leeward on a run) and the edges swell outward; both fade to nothing as the sail luffs. */
void sail_billow(const SailCfg *s, float luff, float cx, float cy, float u, float v, float awa_rad,
                 float *ox, float *oy);

#endif
