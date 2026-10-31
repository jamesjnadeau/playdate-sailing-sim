#ifndef MENU_H
#define MENU_H
#include <stdint.h>

typedef enum { MENU_MAIN, MENU_TRACKS, MENU_PREVIEW, MENU_BOATS, MENU_HELP } MenuScreen;
typedef enum { MENU_NONE, MENU_START_FREE, MENU_START_RACE } MenuAction;
enum { MENU_ITEM_FREE, MENU_ITEM_RACE, MENU_ITEM_HELP, MENU_MAIN_ITEMS };
#define MENU_HELP_VIS 10   /* help rows that fit on screen */
#define MENU_HELP_CRANK_DEG 30.0f   /* crank degrees per help row scrolled */
extern const char *const MENU_MAIN_NAMES[MENU_MAIN_ITEMS];   /* "Free Range", "Racing", "Controls" */

typedef struct {
    MenuScreen screen;
    int sel;        /* highlighted row on the current screen */
    int ntracks;    /* rows on the track screen */
    int nboats;     /* rows on the boat screen, shown after a track is picked; 0 skips the screen */
    int track;      /* chosen track once MENU_START_RACE is returned */
    int help_rows;  /* laid-out rows of the help text */
    int help_top;   /* first help row on screen */
    float help_crank;   /* crank degrees not yet turned into a row */
    int tick;       /* frames spent on the preview screen; drives its animation */
    int boat;       /* the boat row to start on; the chosen boat once MENU_START_RACE is returned */
} Menu;

/* Track screen -> A -> preview of the course (the confirmation) -> A -> boats; B steps back. */
/* Main screen, row 0; ntracks or nboats < 0 -> 0. */
void menu_init(Menu *m, int ntracks, int nboats);
/* Sets how many rows the help screen scrolls through. */
void menu_set_help(Menu *m, int rows);
/* One frame of input; pressed is the BTN_* newly-pressed mask. Order: B, then UP/DOWN, then A. */
MenuAction menu_update(Menu *m, uint32_t pressed);

/* Scrolls the help screen with the crank (clockwise = down); ignored on other screens. */
void menu_crank(Menu *m, float crank_delta_deg);

#endif
