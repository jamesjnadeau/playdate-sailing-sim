#ifndef RENDER_H
#define RENDER_H
#include "pd_api.h"
#include "camera.h"
#include "game.h"
#include "helptext.h"
#include "menu.h"
#include "race.h"

/* Draws the whole frame: sky, sea, rocks, rig, deck, HUD and any restart prompt.
   Does not clear; callers clear to white first only if they draw behind it. */
void render_frame(PlaydateAPI *pd, const Game *g, const CameraPose *cam);

/* Ordered-dither fill: 0 = white .. 16 = black. Out-of-range levels are clamped. */
LCDColor render_pattern(int level);

/* Near-sea tiling, switchable at run time to compare looks: the original wave-aligned grid, hexagons,
   hexagons split into six facets around a centre point sampled at its own wave height, or triangles. */
enum { SEA_GRID, SEA_HEX, SEA_HEXC, SEA_TRI, SEA_STYLES };
void render_set_sea_style(int style);
int render_sea_style(void);

/* Main menu screen (menu_draw.c). */
void menu_draw(PlaydateAPI *pd, const Menu *m, const Track *tracks, const HelpLine *help);

/* hud.c */
void hud_draw(PlaydateAPI *pd, const Game *g);
/* Next-mark indicator and its distance label; race mode only. */
void hud_race(PlaydateAPI *pd, const Game *g, const CameraPose *c);
/* Indicator for the journey's next buoy. */
void hud_journey(PlaydateAPI *pd, const Game *g, const CameraPose *c);
/* Praise banner after a buoy is rounded; blank once its time is up. */
void hud_praise(PlaydateAPI *pd, const Game *g);
void hud_prompt(PlaydateAPI *pd, const Game *g);
/* System pause-menu panel: race time and next-mark info; blank in free sail. */
void hud_pause_panel(PlaydateAPI *pd, const Game *g);

#endif
