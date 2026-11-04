#ifndef RENDER_H
#define RENDER_H
#include "pd_api.h"
#include "camera.h"
#include "game.h"
#include "ghost.h"
#include "helptext.h"
#include "menu.h"
#include "race.h"
#include "besttimes.h"

/* Draws the whole frame: sky, sea, rocks, rig, deck, HUD and any restart prompt.
   Does not clear; callers clear to white first only if they draw behind it. `ghost` (may be NULL) is the
   run to race against: drawn as a light boat, with its gap on the HUD. */
void render_frame(PlaydateAPI *pd, const Game *g, const CameraPose *cam, Ghost *ghost);

/* HUD on (the default) or off: off leaves out the HUD bar, markers, banners and prompts, for clean footage. */
void render_set_hud(bool on);
bool render_hud(void);

/* A lone buoy at local (x, y), drawn in any mode in front of or behind the boat by depth (promo shots). */
void render_set_prop_buoy(bool on, double x, double y);
/* The trailing ghost of the player's recent track in free range and journey: on by default. */
void render_set_follower(bool on);

/* Ordered-dither fill: 0 = white .. 16 = black. Out-of-range levels are clamped. */
LCDColor render_pattern(int level);

/* Near-sea tiling, switchable at run time to compare looks: the original wave-aligned grid, hexagons,
   hexagons split into six facets around a centre point sampled at its own wave height, or triangles. */
enum { SEA_GRID, SEA_HEX, SEA_HEXC, SEA_TRI, SEA_STYLES };
void render_set_sea_style(int style);
int render_sea_style(void);

/* Main menu screen (menu_draw.c). boat_best: per boat row on the boat screen, the finish time of its ghost on
   the chosen course, or < 0 for none (NULL: unknown). */
void menu_draw(PlaydateAPI *pd, const Menu *m, const Track *tracks, const BestTimes *best, const double *boat_best, const HelpLine *help);

/* hud.c */
void hud_draw(PlaydateAPI *pd, const Game *g);
/* Next-mark indicator and its distance label; race mode only. */
void hud_race(PlaydateAPI *pd, const Game *g, const CameraPose *c);
/* Gap to the ghost (+ behind, - ahead) and an edge arrow while it is off screen; race mode only. */
void hud_ghost(PlaydateAPI *pd, const Game *g, const CameraPose *c, Ghost *ghost);
/* Indicator for the journey's next buoy. */
void hud_journey(PlaydateAPI *pd, const Game *g, const CameraPose *c);
/* Praise banner after a buoy is rounded; blank once its time is up. */
void hud_praise(PlaydateAPI *pd, const Game *g);
void hud_prompt(PlaydateAPI *pd, const Game *g);
/* Confirmation box shown when "go back" is picked from the system menu. */
void hud_back_confirm(PlaydateAPI *pd);
/* "Thanks for playing the demo" box over the frozen run (demo build). */
void hud_demo_end(PlaydateAPI *pd);
/* System pause-menu panel: race time and next-mark info; blank in free sail. */
void hud_pause_panel(PlaydateAPI *pd, const Game *g);

#endif
