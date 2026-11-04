#ifndef MENU_H
#define MENU_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { MENU_MAIN, MENU_TRACKS, MENU_PREVIEW, MENU_BOATS, MENU_WEATHER, MENU_HELP, MENU_TRAINING } MenuScreen;
typedef enum { MENU_NONE, MENU_START_FREE, MENU_START_RACE, MENU_START_JOURNEY, MENU_START_TRAINING } MenuAction;
enum { TRAIN_ROW_CONTROLS, TRAIN_ROW_LESSON, MENU_TRAINING_ROWS };   /* rows of the Training screen */
enum { MENU_ITEM_FREE, MENU_ITEM_JOURNEY, MENU_ITEM_RACE, MENU_ITEM_TRAINING, MENU_MAIN_ITEMS };
#define MENU_SOUND_LEVELS 4
#define MENU_LIST_VIS 5    /* list rows that fit on screen; longer lists scroll */
#define MENU_HELP_VIS 10   /* help rows that fit on screen */
#define MENU_HELP_CRANK_DEG 30.0f   /* crank degrees per help row scrolled */
extern const char *const MENU_MAIN_NAMES[MENU_MAIN_ITEMS];   /* "Free Range", "Journey", "Racing", "Training" */

typedef struct {
    MenuScreen screen;
    int sel;        /* highlighted row on the current screen */
    int top;        /* first list row on screen; menu_update keeps sel within MENU_LIST_VIS of it */
    int ntracks;    /* rows on the track screen */
    int nboats;     /* rows on the boat screen, shown after a track is picked; 0 skips the screen */
    int track;      /* chosen track once MENU_START_RACE is returned */
    int help_rows;  /* laid-out rows of the help text */
    int help_top;   /* first help row on screen */
    float help_crank;   /* crank degrees not yet turned into a row */
    int nweather;   /* rows on the weather screen, shown after the boat; 0 skips the screen */
    int weather;    /* the weather row to start on; the chosen one once MENU_START_RACE is returned */
    bool journey;   /* the boats/weather screens were reached from Journey; the last A returns MENU_START_JOURNEY */
    int tick;       /* frames spent on the preview screen; drives its animation */
    int sound;      /* sea sound level 0..MENU_SOUND_LEVELS-1; A on the Sound row steps it round */
    int boat;       /* the boat row to start on; the chosen boat once MENU_START_RACE is returned */
    uint32_t main_locked;   /* bit per main-screen row: set rows are shown but cannot be selected (the demo build) */
    const bool *track_ok;   /* per track row: false rows are shown but cannot be selected (the demo build); NULL means all */
    const bool *raceable;   /* per boat row: false rows (cruisers) are skipped; NULL means every row is raceable */
} Menu;

/* Track screen -> A -> preview of the course (the confirmation) -> A -> boats -> A -> weather -> A; B steps back. */
/* Main screen, row 0; ntracks or nboats < 0 -> 0. */
void menu_init(Menu *m, int ntracks, int nboats);
/* Sets how many weather rows follow the boat screen (0 skips it); weather < 0 or >= n -> 0. */
void menu_set_weather(Menu *m, int n, int current);
/* Sets the sea sound level shown on the main screen; out of range -> the default (2). */
void menu_set_sound(Menu *m, int level);
/* Sets how many rows the help screen scrolls through. */
void menu_set_help(Menu *m, int rows);
/* Marks which boat rows may race (nboats entries). The array is not copied and must outlive the menu.
   NULL, or no row raceable, makes every row selectable. */
void menu_set_boat_racing(Menu *m, const bool *raceable);
/* Locks main-screen rows: bit i of mask set -> row i is drawn dimmed and the cursor skips it. The cursor moves
   to the first open row if it is on a locked one. */
void menu_set_main_locked(Menu *m, uint32_t mask);
/* Marks which track rows may be picked (ntracks entries). Not copied; must outlive the menu. NULL, or no row
   open, makes every row selectable. */
void menu_set_track_ok(Menu *m, const bool *ok);
/* One frame of input; pressed is the BTN_* newly-pressed mask. Order: B, then UP/DOWN, then A. */
MenuAction menu_update(Menu *m, uint32_t pressed);

/* Rows in the current screen's list; 0 on the preview and help screens. */
int menu_rows(const Menu *m);
/* Whether a row of the current list can be selected (false for cruisers on the boat screen). */
bool menu_row_enabled(const Menu *m, int row);

/* Scrolls the help screen with the crank (clockwise = down); ignored on other screens. */
void menu_crank(Menu *m, float crank_delta_deg);

#endif
