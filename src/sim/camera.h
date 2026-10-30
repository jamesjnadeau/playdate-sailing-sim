#ifndef CAMERA_H
#define CAMERA_H
#include <stdbool.h>
#include "boatcfg.h"
#include "game.h"

#define CAM_W 400
#define CAM_H 240
#define CAM_FOCAL 220.0f          /* px; ~85 deg horizontal field of view */
#define CAM_HORIZON_Y 110.0f      /* px; horizon row at zero pitch and roll */

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

/* False for points at or behind the camera plane. depth is distance along the view axis. */
bool camera_project(const CameraPose *c, double wx, double wy, float wz,
                    float *sx, float *sy, float *depth);

#endif
