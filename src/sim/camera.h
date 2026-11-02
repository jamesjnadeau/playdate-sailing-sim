#ifndef CAMERA_H
#define CAMERA_H
#include <stdbool.h>
#include "boatcfg.h"
#include "game.h"

#define CAM_W 400
#define CAM_H 240
#define CAM_FOCAL 220.0f          /* px; ~85 deg horizontal field of view */
#define CAM_HORIZON_Y 110.0f      /* px; horizon row at zero pitch and roll */
#define HUD_H 20                  /* px; the bottom HUD bar: rows CAM_H - HUD_H .. CAM_H - 1 */

/* World frame as the rest of the sim: forward (cos yaw, sin yaw), starboard (-sin yaw, cos yaw).
   z is height above mean sea level (up). roll > 0 leans the view to starboard, pitch > 0 looks up. */
typedef struct {
    double x, y;                  /* double: far-from-origin precision */
    float height;
    bool trail;                   /* trailing view (set by the caller; survives updates) */
    float yaw, pitch, roll;       /* what the view uses: roll is limited, smoothed */
    float deck_roll, deck_pitch, deck_bob;   /* unlimited, for the deck / HUD drawing */
} CameraPose;

/* Smooths toward the boat; dt is clamped like the game's, non-finite counts as 1/30. */
void camera_update(CameraPose *c, const Game *g, const BoatCfg *cfg, float dt);

/* Vertical look angles (rad, lo <= 0 <= hi) the trailing camera can reach; the crank clamps to these. */
void camera_look_limits(const BoatCfg *cfg, float *lo, float *hi);

#define MARK_IND_Z 4.0f      /* m above the sea where the on-screen marker anchors */
#define MARK_EDGE_PAD 12.0f  /* px kept between an edge arrow and the screen edge */

typedef struct {
    bool on_screen;   /* true: (x, y) is the point above the mark */
    float x, y;       /* px */
    float angle;      /* off screen: screen direction toward the mark, rad (0 = right, +pi/2 = down) */
} MarkView;

/* False for points at or behind the camera plane. depth is distance along the view axis. */
bool camera_project(const CameraPose *c, double wx, double wy, float wz,
                    float *sx, float *sy, float *depth);

/* Same, for a point given as a float offset from the camera's x, y (no double arithmetic). */
bool camera_project_rel(const CameraPose *c, float dx, float dy, float wz,
                        float *sx, float *sy, float *depth);

/* bottom: lowest usable row (the HUD panel top). Always fills `out` with finite values. */
void camera_mark_view(const CameraPose *c, double wx, double wy, float bottom, MarkView *out);

#endif
