#ifndef APP_H
#define APP_H
#include <stdbool.h>
#include <stdint.h>
#include "platform.h"
#include "assets.h"
#include "besttimes.h"
#include "camera.h"
#include "game.h"
#include "ghost.h"
#include "helptext.h"
#include "menu.h"
#include "tracks.h"
#include "training.h"

/* The game's app flow, shared by every platform: main menu -> free range / journey / race, race attempts,
 * ghosts and best times, the "go back" question. The platform polls input, calls app_step once a frame and
 * draws what it says. */

#define APP_HELP_ROWS_MAX 96
#define APP_HELP_COLS 44

/* Demo build (compile with -DDEMO; make DEMO=1): only Free Range and the APP_DEMO_TRACK race can be started
 * (Journey and the other courses are shown but locked), and each run lasts APP_DEMO_SECONDS of sailing. Then the thank-you box shows for APP_DEMO_END_SECONDS (A skips it) and the
 * game returns to the home screen. The locked rows stay on the menu, dimmed and out of reach. */
#define APP_DEMO_SECONDS 300.0f
#define APP_DEMO_END_SECONDS 6.0f
#define APP_DEMO_TRACK "Two Buoys 100m"   /* the one course the demo races, by its "name" in tracks/ */
#define APP_DEMO_MESSAGE "Thanks for playing the demo"
bool app_is_demo(void);
/* Sailing seconds left in the demo run; 0 outside a demo build. */
float app_demo_left(void);

typedef enum {
    APP_DRAW_NONE,      /* leave the screen as it is (the menu shows from the next frame) */
    APP_DRAW_MENU,      /* the main menu */
    APP_DRAW_PLAY,      /* the run */
    APP_DRAW_CONFIRM,   /* the run, frozen, with the "go back" box over it */
    APP_DRAW_DEMO_END   /* the run, frozen, with "Thanks for playing the demo" over it (demo build only) */
} AppDraw;

/* Loads tracks, best times, boats, help and the first boat, then opens the main menu. p must outlive the app. */
void app_init(const Platform *p);

/* One frame. pressed / held are BTN_* masks (controls.h), crank in degrees, dt in seconds. */
AppDraw app_step(uint32_t pressed, uint32_t held, float crank, float dt);

/* The state name the scenario harness records for a frame drawn as d: "menu" or "play". */
const char *app_draw_name(AppDraw d);

/* System menu: the platform calls these when its items are picked. */
void app_sys_boat(int boat);
void app_sys_weather(int preset);
void app_sys_back(void);

/* A scripted praise banner (scenario harness). */
void app_force_praise(int praise);

bool app_playing(void);                  /* a run is on screen (not the main menu) */
const Game *app_game(void);
Game *app_game_mut(void);
const CameraPose *app_camera(void);
const Menu *app_menu(void);
const Track *app_tracks(void);
int app_ntracks(void);
const BestTimes *app_best(void);
const double *app_boat_best(void);       /* per boat row on the boat screen; see menu_draw */
const HelpLine *app_help(void);
int app_nhelp(void);
Ghost *app_ghost_opponent(void);         /* NULL when there is none to race */
int app_boat_index(void);
int app_preset_index(void);
bool app_sys_racing(void);               /* the system menu offers only "go back" */
/* The config warning while it is on screen (play frames only), else NULL. */
const char *app_warning(void);

/* The sailing lesson: its state, and the current step's text from config/training.md. Both NULL outside a lesson. */
const Training *app_training(void);
const char *app_training_text(void);

#endif
