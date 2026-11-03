#ifndef NAVIGATOR_H
#define NAVIGATOR_H
#include "race.h"

/* Picks the heading an autopilot should hold to sail a course. Each mark is crossed at a point
 * NAV_AIM_OFFSET_M out along its gate ray, so the boat passes wide of the buoy and still crosses the ray the
 * race checks. The ray often points along the approach, so the boat first heads for a corner
 * NAV_APPROACH_OFFSET_M beside that point, on the side the rounding comes from, and turns to the crossing
 * point once NAV_SWITCH_ALONG_M along the ray. Upwind it holds close-hauled on one tack and tacks only when
 * it has strayed from the leg or the tack would carry it past the buoy too close. */
#define NAV_AIM_OFFSET_M 20.0f
#define NAV_APPROACH_OFFSET_M 22.0f
#define NAV_APPROACH_MAX_M 60.0f   /* the corner moves out this far to clear another buoy */
#define NAV_SWITCH_ALONG_M 15.0f
#define NAV_CLEAR_M 18.0f   /* a close-hauled tack that would pass the buoy closer than this is swapped */
#define NAV_UPWIND_MARGIN_RAD (10.0f * 0.017453292519943295f)
#define NAV_CORRIDOR_FRAC 0.15f
#define NAV_CORRIDOR_MIN_M 30.0f

typedef struct {
    int side;   /* -1/+1: which side of the wind the boat is pointing; 0 = not on a close-hauled leg yet */
} NavState;

void nav_init(NavState *s);

/* The point `offset` m from the mark along its gate ray, track frame. */
void nav_aim_point(const Mark *m, float offset, float *x, float *y);

/* Heading (rad) for a boat at local position (px, py), floating origin (ox, oy), heading for mark `next`. */
float nav_heading(NavState *s, const Track *t, int next, double px, double py, double ox, double oy,
                  float wind_from, float no_go_rad);

#endif
