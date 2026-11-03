#ifndef FOLLOWER_H
#define FOLLOWER_H

#include <stdbool.h>
#include "boat.h"

/* Breadcrumb trail of the player's own track, read back at a fixed path
 * distance behind the boat to place the trailing ghost. */

#define FOLLOW_CAP 256
#define FOLLOW_STEP_M 0.25
#define FOLLOW_DISTANCE_M 30.0f
#define FOLLOW_EYE_MIN_M 20.0f   /* nearer the camera than this, the ghost would fill the view */

typedef struct {
    double x, y;                       /* m, local (game) frame */
    float heading, heel, wheel, u;
    float trims[BOAT_MAX_SAILS], hoists[BOAT_MAX_SAILS], swing[BOAT_MAX_SAILS];
} FollowPose;

typedef struct { FollowPose p; double s; } FollowSample;   /* s: path length from the first sample, m */

typedef struct {
    FollowSample smp[FOLLOW_CAP];
    int head, n;                       /* head: index of the newest; n: samples held */
} Follower;

void follower_clear(Follower *f);
/* Newest sample always tracks the boat's current pose; a new one is started once the boat is
 * FOLLOW_STEP_M from the sample before it. Non-finite positions are ignored. */
void follower_record(Follower *f, const BoatState *b);
/* Pose `behind_m` of path length behind the newest sample; false until the trail is that long. */
bool follower_pose(const Follower *f, float behind_m, FollowPose *out);
/* Floating-origin shift: subtract (dx, dy) from every held sample. */
void follower_shift(Follower *f, double dx, double dy);

/* True when the pose is within `min_m` of the camera (or not finite): the ghost is then not drawn. */
bool follower_near_eye(const FollowPose *p, double cam_x, double cam_y, float min_m);

#endif
