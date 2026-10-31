#include "menu.h"
#include "controls.h"

const char *const MENU_MAIN_NAMES[MENU_MAIN_ITEMS] = { "Free Range", "Racing" };

void menu_init(Menu *m, int ntracks, int nboats) {
    m->screen = MENU_MAIN;
    m->sel = 0;
    m->ntracks = ntracks < 0 ? 0 : ntracks;
    m->nboats = nboats < 0 ? 0 : nboats;
    m->track = 0;
    m->boat = 0;
}

static int rows(const Menu *m) {
    return m->screen == MENU_MAIN ? MENU_MAIN_ITEMS : m->screen == MENU_TRACKS ? m->ntracks : m->nboats;
}

MenuAction menu_update(Menu *m, uint32_t pressed) {
    if (pressed & BTN_B) {
        if (m->screen == MENU_BOATS) {
            m->screen = MENU_TRACKS;
            m->sel = m->track;
        } else if (m->screen == MENU_TRACKS) {
            m->screen = MENU_MAIN;
            m->sel = MENU_ITEM_RACE;
        }
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
    if (m->screen == MENU_TRACKS) {
        m->track = m->sel;
        if (m->nboats < 1) return MENU_START_RACE;
        m->screen = MENU_BOATS;
        m->sel = m->boat < m->nboats ? m->boat : 0;
        return MENU_NONE;
    }
    m->boat = m->sel;
    return MENU_START_RACE;
}
