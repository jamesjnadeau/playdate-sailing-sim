#include "menu.h"
#include "controls.h"

const char *const MENU_MAIN_NAMES[MENU_MAIN_ITEMS] = { "Free Range", "Racing" };

void menu_init(Menu *m, int ntracks) {
    m->screen = MENU_MAIN;
    m->sel = 0;
    m->ntracks = ntracks < 0 ? 0 : ntracks;
    m->track = 0;
}

static int rows(const Menu *m) {
    return m->screen == MENU_MAIN ? MENU_MAIN_ITEMS : m->ntracks;
}

MenuAction menu_update(Menu *m, uint32_t pressed) {
    if (m->screen == MENU_TRACKS && (pressed & BTN_B)) {
        m->screen = MENU_MAIN;
        m->sel = MENU_ITEM_RACE;
    }

    int n = rows(m);
    if (n > 0) {
        if (pressed & BTN_DOWN) m->sel = (m->sel + 1) % n;
        if (pressed & BTN_UP) m->sel = (m->sel + n - 1) % n;
    }

    if (!(pressed & BTN_A)) return MENU_NONE;
    if (m->screen == MENU_MAIN) {
        if (m->sel == MENU_ITEM_FREE) return MENU_START_FREE;
        if (m->ntracks < 1) return MENU_NONE;
        m->screen = MENU_TRACKS;
        m->sel = 0;
        return MENU_NONE;
    }
    m->track = m->sel;
    return MENU_START_RACE;
}
