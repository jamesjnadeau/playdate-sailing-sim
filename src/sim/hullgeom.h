#ifndef HULLGEOM_H
#define HULLGEOM_H
#include <stdbool.h>

/* Hull / water geometry for a heeled boat, kept free of drawing so it can be tested.
 * Boat frame: x forward, y starboard, z up from the hull's mean waterline. heel > 0 puts the
 * starboard rail down. Hull sides tuck inwards from the gunwale (z = fb) to the keel line. */

/* Where the sea surface cuts the side of the hull at one section. w is the half-width at the
 * gunwale, sgn -1 port / +1 starboard, surf the sea height relative to the mean waterline at that
 * spot. Returns the boat-frame height of the cut, clamped to [keel, fb] (fb = the rail is under,
 * keel = the side is clear of the water), and the lateral offset of that point in *y. */
float hull_side_z(float w, int sgn, float fb, float keel, float tuck, float heel, float surf, float *y);

/* Which side of the hull faces away from the eye: +1 starboard, -1 port, 0 when the eye is near
 * enough to the heeled hull's midplane that both sides show. lat_e is the eye's offset to
 * starboard of the boat, h_e its height above the boat's origin. */
int hull_hidden_side(float heel, float lat_e, float h_e, float beam);

/* True when the deck's upward normal points toward the eye (false once heeled far enough that the
 * hull's underside is what the eye sees, with the deck and rig behind it). */
bool hull_deck_faces_eye(float heel, float lat_e, float h_e);

typedef struct { float f, l, z; } HgPt;   /* forward, starboard, up: heading frame, heel already applied */

/* Sutherland-Hodgman against the sea: keeps the part of the polygon with d >= 0 (d is each vertex's
 * height above the surface). out[] is filled with at most max points; cut[k] is set for the points
 * that lie on the surface. Returns the number of points written. */
int hull_clip_above(const HgPt *in, const float *d, int n, HgPt *out, bool *cut, int max);

/* Catamaran heel: the boat rolls about the keel line of its leeward hull, not about the centreline.
 * Lateral position (boat frame, + starboard) of that line: +spacing/2 when heel > 0 (starboard hull
 * leeward), -spacing/2 when heel < 0, and 0 when upright or spacing <= 0. */
float hull_pivot(float heel, float spacing);

/* Rotates the boat-frame point (y, z) by heel about the point (pivot, 0). *l is the heeled lateral
 * offset (+ starboard), *up the height above the mean waterline. With pivot 0 this is the plain
 * rotation about the origin used for a monohull. */
void hull_heel_about(float heel, float pivot, float y, float z, float *l, float *up);

/* Fraction of each hull's draft under the sea, 0..1 (clamped), for a boat heeling about
 * hull_pivot. sea_port / sea_stbd are the sea heights relative to the mean waterline under each
 * hull centre (y = -/+ spacing/2). Non-finite inputs give 1 for both hulls. */
void cat_immersion(float heel, float spacing, float draft, float sea_port, float sea_stbd, float *port, float *stbd);

#endif
