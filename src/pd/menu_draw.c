#include <stdbool.h>
#include <string.h>
#include "assets.h"
#include "helptext.h"
#include "menu.h"
#include "race.h"
#include "render.h"

#define ROW_H 30
#define ROWS_VIS 4

static void text(PlaydateAPI *pd, const char *s, int x, int y) {
    pd->graphics->drawText(s, strlen(s), kASCIIEncoding, x, y);
}

static void row(PlaydateAPI *pd, const char *s, int y, bool sel) {
    if (sel) {
        pd->graphics->fillRect(40, y - 2, CAM_W - 80, 24, kColorBlack);
        pd->graphics->setDrawMode(kDrawModeFillWhite);
        text(pd, s, 52, y);
        pd->graphics->setDrawMode(kDrawModeCopy);
    } else {
        text(pd, s, 52, y);
    }
}

void menu_draw(PlaydateAPI *pd, const Menu *m, const Track *tracks, const HelpLine *help) {
    pd->graphics->clear(kColorWhite);
    pd->graphics->setDrawMode(kDrawModeCopy);
    if (m->screen == MENU_HELP) {
        text(pd, "CONTROLS", 160, 8);
        for (int i = 0; i < MENU_HELP_VIS && m->help_top + i < m->help_rows; i++) {
            const HelpLine *h = &help[m->help_top + i];
            int y = 28 + 15 * i;
            if (h->style == HELP_HEAD) {
                pd->graphics->fillRect(8, y - 1, CAM_W - 16, 15, kColorBlack);
                pd->graphics->setDrawMode(kDrawModeFillWhite);
                text(pd, h->text, 12, y);
                pd->graphics->setDrawMode(kDrawModeCopy);
            } else if (h->style == HELP_BULLET) {
                if (h->first) text(pd, "-", 12, y);
                text(pd, h->text, 24, y);
            } else {
                text(pd, h->text, 12, y);
            }
        }
        text(pd, m->help_top > 0 ? "^" : " ", 388, 28);
        text(pd, m->help_top + MENU_HELP_VIS < m->help_rows ? "v" : " ", 388, 208);
        text(pd, "UP/DOWN scroll  B back", 8, 224);
    } else if (m->screen == MENU_MAIN) {
        text(pd, "SAILING", 150, 30);
        for (int i = 0; i < MENU_MAIN_ITEMS; i++)
            row(pd, MENU_MAIN_NAMES[i], 100 + ROW_H * i, i == m->sel);
        text(pd, "A select", 8, 220);
    } else if (m->screen == MENU_BOATS) {
        text(pd, "CHOOSE BOAT", 130, 30);
        int start = m->sel - (ROWS_VIS - 1);
        if (start < 0) start = 0;
        for (int i = 0; i < ROWS_VIS && start + i < m->nboats; i++) {
            int k = start + i;
            row(pd, ASSET_BOAT_NAMES[k], 100 + ROW_H * i, k == m->sel);
        }
        text(pd, "A race  B back", 8, 220);
    } else {
        text(pd, "RACING", 150, 30);
        int start = m->sel - (ROWS_VIS - 1);
        if (start < 0) start = 0;
        for (int i = 0; i < ROWS_VIS && start + i < m->ntracks; i++) {
            int k = start + i;
            row(pd, tracks[k].name, 100 + ROW_H * i, k == m->sel);
        }
        text(pd, "A select  B back", 8, 220);
    }
}
