#include <stdbool.h>
#include <string.h>
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

void menu_draw(PlaydateAPI *pd, const Menu *m, const Track *tracks) {
    pd->graphics->clear(kColorWhite);
    pd->graphics->setDrawMode(kDrawModeCopy);
    if (m->screen == MENU_MAIN) {
        text(pd, "SAILING", 150, 30);
        for (int i = 0; i < MENU_MAIN_ITEMS; i++)
            row(pd, MENU_MAIN_NAMES[i], 100 + ROW_H * i, i == m->sel);
        text(pd, "A select", 8, 220);
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
