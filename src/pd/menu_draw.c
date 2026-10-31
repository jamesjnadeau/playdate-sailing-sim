#include <stdbool.h>
#include <math.h>
#include <stdio.h>
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

/* Course preview: a north-up plan of the track, the full route dotted, and a solid line that
   sails it again and again so the sequence of marks reads at a glance. */
#define PV_L 12
#define PV_T 26
#define PV_W (CAM_W - 24)
#define PV_H 172
#define PV_LOOP 240    /* frames to sail the route once */
#define PV_HOLD 45     /* frames the finished line stays before it restarts */

typedef struct { float cx, cy, scale, x0, y0; } PvView;   /* world (x north, y east) -> screen */

static int pv_sx(const PvView *v, float wy) { return (int)lroundf(v->cx + (wy - v->y0) * v->scale); }
static int pv_sy(const PvView *v, float wx) { return (int)lroundf(v->cy - (wx - v->x0) * v->scale); }

static void pv_extend(float x, float y, float *lo_x, float *hi_x, float *lo_y, float *hi_y) {
    if (x < *lo_x) *lo_x = x;
    if (x > *hi_x) *hi_x = x;
    if (y < *lo_y) *lo_y = y;
    if (y > *hi_y) *hi_y = y;
}

static void draw_preview(PlaydateAPI *pd, const Menu *m, const Track *t) {
    float lo_x = t->start_x, hi_x = lo_x, lo_y = t->start_y, hi_y = lo_y;
    for (int i = 0; i < t->nmarks; i++) pv_extend(t->marks[i].x, t->marks[i].y, &lo_x, &hi_x, &lo_y, &hi_y);
    for (int i = 0; i < t->nroute; i++) pv_extend(t->route[i][0], t->route[i][1], &lo_x, &hi_x, &lo_y, &hi_y);
    float sx = hi_y - lo_y, sy = hi_x - lo_x;
    if (sx < 20) sx = 20;
    if (sy < 20) sy = 20;
    const int pad = 14;
    float scale = fminf((PV_W - 2 * pad) / sx, (PV_H - 2 * pad) / sy);
    PvView v = { PV_L + PV_W / 2.0f, PV_T + PV_H / 2.0f, scale, (hi_x + lo_x) / 2, (hi_y + lo_y) / 2 };

    PlaydateGraphics *g = pd->graphics;
    g->drawRect(PV_L, PV_T, PV_W, PV_H, kColorBlack);

    float len = track_route_len(t);
    if (len < 1) len = 1;
    /* the whole route as dots */
    for (float d = 0; d < len; d += 6.0f / scale) {
        float x, y, h;
        track_route_at(t, d, &x, &y, &h);
        g->fillRect(pv_sx(&v, y), pv_sy(&v, x), 2, 2, kColorBlack);
    }

    /* marks */
    for (int i = 0; i < t->nmarks; i++) {
        const Mark *k = &t->marks[i];
        int px = pv_sx(&v, k->y), py = pv_sy(&v, k->x);
        bool fin = strcmp(k->name, "FIN") == 0;
        if (fin) {
            g->drawLine(px, py - 6, px, py + 6, 2, kColorBlack);
            g->drawLine(px - 6, py, px + 6, py, 2, kColorBlack);
        } else {
            g->fillEllipse(px - 4, py - 4, 9, 9, 0, 0, kColorWhite);
            g->drawEllipse(px - 4, py - 4, 9, 9, 2, 0, 0, kColorBlack);
        }
        char lab[10];
        snprintf(lab, sizeof lab, "%s", k->name);
        text(pd, lab, px + 7, py - 14);
    }

    /* start */
    int stx = pv_sx(&v, t->start_y), sty = pv_sy(&v, t->start_x);
    g->fillRect(stx - 3, sty - 3, 7, 7, kColorBlack);
    text(pd, "S", stx + 6, sty - 4);

    /* the animated line and its head */
    int span = PV_LOOP + PV_HOLD;
    int f = m->tick % span;
    float run = f < PV_LOOP ? len * f / PV_LOOP : len;
    int px = pv_sx(&v, t->route[0][1]), py = pv_sy(&v, t->route[0][0]);
    float x = 0, y = 0, h = 0;
    for (float d = 2.0f / scale; d < run; d += 2.0f / scale) {
        track_route_at(t, d, &x, &y, &h);
        int qx = pv_sx(&v, y), qy = pv_sy(&v, x);
        g->drawLine(px, py, qx, qy, 3, kColorBlack);
        px = qx;
        py = qy;
    }
    track_route_at(t, run, &x, &y, &h);
    px = pv_sx(&v, y);
    py = pv_sy(&v, x);
    g->fillEllipse(px - 4, py - 4, 9, 9, 0, 0, kColorBlack);
    /* heading tick on the head: world heading h -> screen (sin h, -cos h) */
    g->drawLine(px, py, px + (int)lroundf(10 * sinf(h)), py - (int)lroundf(10 * cosf(h)), 2, kColorBlack);

    /* wind arrow, pointing the way the wind blows */
    float wd = t->wind_from + 3.14159265f;
    int ax = PV_L + PV_W - 26, ay = PV_T + 30;
    int tx = ax + (int)lroundf(16 * sinf(wd)), ty = ay - (int)lroundf(16 * cosf(wd));
    int bx = ax - (int)lroundf(16 * sinf(wd)), by = ay + (int)lroundf(16 * cosf(wd));
    g->fillRect(ax - 24, ay - 22, 48, 44, kColorWhite);
    g->drawLine(bx, by, tx, ty, 2, kColorBlack);
    g->fillTriangle(tx, ty,
                    tx - (int)lroundf(6 * sinf(wd) + 4 * cosf(wd)), ty + (int)lroundf(6 * cosf(wd) - 4 * sinf(wd)),
                    tx - (int)lroundf(6 * sinf(wd) - 4 * cosf(wd)), ty + (int)lroundf(6 * cosf(wd) + 4 * sinf(wd)),
                    kColorBlack);
    text(pd, "WIND", ax - 16, ay + 8);
}

void menu_draw(PlaydateAPI *pd, const Menu *m, const Track *tracks, const HelpLine *help) {
    pd->graphics->clear(kColorWhite);
    pd->graphics->setDrawMode(kDrawModeCopy);
    if (m->screen == MENU_HELP) {
        text(pd, "CONTROLS", 160, 8);
        for (int i = 0; i < MENU_HELP_VIS && m->help_top + i < m->help_rows; i++) {
            const HelpLine *h = &help[m->help_top + i];
            int y = 28 + 17 * i;
            if (h->style == HELP_HEAD) {
                pd->graphics->fillRect(8, y - 2, CAM_W - 16, 19, kColorBlack);
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
        text(pd, "UP/DOWN/crank scroll  B back", 8, 224);
    } else if (m->screen == MENU_MAIN) {
        text(pd, "SAILING", 150, 30);
        for (int i = 0; i < MENU_MAIN_ITEMS; i++)
            row(pd, MENU_MAIN_NAMES[i], 100 + ROW_H * i, i == m->sel);
        text(pd, "A select", 8, 220);
    } else if (m->screen == MENU_PREVIEW) {
        const Track *t = &tracks[m->track];
        text(pd, t->name, 8, 4);
        draw_preview(pd, m, t);
        text(pd, t->info, 8, 202);
        text(pd, "A confirm  B back", 8, 222);
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
