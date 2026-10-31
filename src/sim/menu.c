#include "menu.h"
#include "controls.h"

const char *const MENU_MAIN_NAMES[MENU_MAIN_ITEMS] = { "Free Range", "Racing", "Controls" };

void menu_init(Menu *m, int ntracks, int nboats) {
    m->screen = MENU_MAIN;
    m->sel = 0;
    m->ntracks = ntracks < 0 ? 0 : ntracks;
    m->nboats = nboats < 0 ? 0 : nboats;
    m->track = 0;
    m->boat = 0;
    m->tick = 0;
    m->help_rows = 0;
    m->help_top = 0;
    m->help_crank = 0;
}

void menu_set_help(Menu *m, int rows) {
    m->help_rows = rows < 0 ? 0 : rows;
    m->help_top = 0;
}

void menu_crank(Menu *m, float d) {
    if (m->screen != MENU_HELP || !(d > -3600.0f && d < 3600.0f)) return;   /* also drops NaN */
    int last = m->help_rows - MENU_HELP_VIS;
    if (last < 0) last = 0;
    m->help_crank += d;
    while (m->help_crank >= MENU_HELP_CRANK_DEG) { m->help_crank -= MENU_HELP_CRANK_DEG; if (m->help_top < last) m->help_top++; }
    while (m->help_crank <= -MENU_HELP_CRANK_DEG) { m->help_crank += MENU_HELP_CRANK_DEG; if (m->help_top > 0) m->help_top--; }
}

static MenuAction help_update(Menu *m, uint32_t pressed) {
    int last = m->help_rows - MENU_HELP_VIS;
    if (last < 0) last = 0;
    if (pressed & BTN_DOWN) m->help_top = m->help_top < last ? m->help_top + 1 : last;
    if (pressed & BTN_UP) m->help_top = m->help_top > 0 ? m->help_top - 1 : 0;
    if (pressed & (BTN_B | BTN_A)) {
        m->screen = MENU_MAIN;
        m->sel = MENU_ITEM_HELP;
    }
    return MENU_NONE;
}

MenuAction menu_update(Menu *m, uint32_t pressed) {
    if (m->screen == MENU_HELP) return help_update(m, pressed);
    if (pressed & BTN_B) {
        if (m->screen == MENU_BOATS) {
            m->screen = MENU_PREVIEW;
            m->tick = 0;
        } else if (m->screen == MENU_PREVIEW) {
            m->screen = MENU_TRACKS;
            m->sel = m->track;
        } else if (m->screen == MENU_TRACKS) {
            m->screen = MENU_MAIN;
            m->sel = MENU_ITEM_RACE;
        }
    }

    if (m->screen == MENU_PREVIEW) {
        m->tick++;
        if (!(pressed & BTN_A)) return MENU_NONE;
        if (m->nboats < 1) return MENU_START_RACE;
        m->screen = MENU_BOATS;
        m->sel = m->boat < m->nboats ? m->boat : 0;
        return MENU_NONE;
    }

    int n = m->screen == MENU_MAIN ? MENU_MAIN_ITEMS : m->screen == MENU_TRACKS ? m->ntracks : m->nboats;
    if (n > 0) {
        if (pressed & BTN_DOWN) m->sel = (m->sel + 1) % n;
        if (pressed & BTN_UP) m->sel = (m->sel + n - 1) % n;
    }

    if (!(pressed & BTN_A)) return MENU_NONE;
    if (m->screen == MENU_MAIN) {
        if (m->sel == MENU_ITEM_FREE) return MENU_START_FREE;
        if (m->sel == MENU_ITEM_HELP) {
            m->screen = MENU_HELP;
            m->help_top = 0;
            m->help_crank = 0;
            return MENU_NONE;
        }
        if (m->ntracks < 1) return MENU_NONE;
        m->screen = MENU_TRACKS;
        m->sel = 0;
        return MENU_NONE;
    }
    if (m->screen == MENU_TRACKS) {
        m->track = m->sel;
        m->screen = MENU_PREVIEW;
        m->tick = 0;
        return MENU_NONE;
    }
    m->boat = m->sel;
    return MENU_START_RACE;
}
