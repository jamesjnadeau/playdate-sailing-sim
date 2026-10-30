#ifndef RENDER_H
#define RENDER_H
#include "pd_api.h"
#include "camera.h"
#include "game.h"

/* Draws the whole frame: sky, sea, rocks, rig, deck, HUD and any restart prompt.
   Does not clear; callers clear to white first only if they draw behind it. */
void render_frame(PlaydateAPI *pd, const Game *g, const CameraPose *cam);

/* Ordered-dither fill: 0 = white .. 16 = black. Out-of-range levels are clamped. */
LCDColor render_pattern(int level);

/* hud.c */
void hud_draw(PlaydateAPI *pd, const Game *g);
void hud_prompt(PlaydateAPI *pd, const Game *g);

#endif
