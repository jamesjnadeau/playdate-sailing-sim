#ifndef MENU_H
#define MENU_H
#include <stdint.h>

typedef enum { MENU_MAIN, MENU_TRACKS } MenuScreen;
typedef enum { MENU_NONE, MENU_START_FREE, MENU_START_RACE } MenuAction;
enum { MENU_ITEM_FREE, MENU_ITEM_RACE, MENU_MAIN_ITEMS };
extern const char *const MENU_MAIN_NAMES[MENU_MAIN_ITEMS];   /* "Free Range", "Racing" */

typedef struct {
    MenuScreen screen;
    int sel;        /* highlighted row on the current screen */
    int ntracks;    /* rows on the track screen */
    int track;      /* chosen track once MENU_START_RACE is returned */
} Menu;

/* Main screen, row 0; ntracks < 0 -> 0. */
void menu_init(Menu *m, int ntracks);
/* One frame of input; pressed is the BTN_* newly-pressed mask. Order: B, then UP/DOWN, then A. */
MenuAction menu_update(Menu *m, uint32_t pressed);

#endif
