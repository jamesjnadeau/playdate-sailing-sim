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
    m->nweather = 0;
    m->weather = 0;
    m->tick = 0;
    m->help_rows = 0;
    m->help_top = 0;
    m->help_crank = 0;
    m->raceable = NULL;
}

void menu_set_boat_racing(Menu *m, const bool *raceable) {
    m->raceable = raceable;
}

/* A boat row is selectable when it may race; with no racer at all, every row is. */
static bool row_ok(const Menu *m, int row) {
    if (!m->raceable) return true;
    for (int i = 0; i < m->nboats; i++) if (m->raceable[i]) return m->raceable[row];
    return true;
}

/* Nearest selectable boat row from row, stepping dir (+1 or -1) and wrapping; row when none can race. */
static int next_ok(const Menu *m, int row, int dir) {
    int n = m->nboats;
    for (int i = 0; i < n; i++) {
        int k = ((row + dir * i) % n + n) % n;
        if (row_ok(m, k)) return k;
    }
    return row;
}

/* The boat screen's starting row: the current boat if it may race, else the first racer. */
static int boat_start(const Menu *m) {
    int row = m->boat < m->nboats ? m->boat : 0;
    return next_ok(m, row, 1);
}

void menu_set_weather(Menu *m, int n, int current) {
    m->nweather = n < 0 ? 0 : n;
    m->weather = current >= 0 && current < m->nweather ? current : 0;
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

/* After the boat: the weather screen, or straight into the race when there is none. */
static MenuAction enter_weather(Menu *m) {
    if (m->nweather < 1) return MENU_START_RACE;
    m->screen = MENU_WEATHER;
    m->sel = m->weather;
    return MENU_NONE;
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
        if (m->screen == MENU_WEATHER) {
            if (m->nboats < 1) {
                m->screen = MENU_PREVIEW;
                m->tick = 0;
            } else {
                m->screen = MENU_BOATS;
                m->sel = boat_start(m);
            }
        } else if (m->screen == MENU_BOATS) {
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
        if (m->nboats < 1) return enter_weather(m);
        m->screen = MENU_BOATS;
        m->sel = boat_start(m);
        return MENU_NONE;
    }

    int n = m->screen == MENU_MAIN ? MENU_MAIN_ITEMS : m->screen == MENU_TRACKS ? m->ntracks : m->screen == MENU_BOATS ? m->nboats : m->nweather;
    if (n > 0) {
        if (m->screen == MENU_BOATS) {
            /* Step past cruiser rows; next_ok returns the same row when none can race. */
            if (pressed & BTN_DOWN) m->sel = next_ok(m, (m->sel + 1) % n, 1);
            if (pressed & BTN_UP) m->sel = next_ok(m, (m->sel + n - 1) % n, -1);
        } else {
            if (pressed & BTN_DOWN) m->sel = (m->sel + 1) % n;
            if (pressed & BTN_UP) m->sel = (m->sel + n - 1) % n;
        }
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
    if (m->screen == MENU_WEATHER) {
        m->weather = m->sel;
        return MENU_START_RACE;
    }
    m->boat = m->sel;
    return enter_weather(m);
}
