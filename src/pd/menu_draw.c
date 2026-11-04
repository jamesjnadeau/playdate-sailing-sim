#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "helptext.h"
#include "hudinfo.h"
#include "menu.h"
#include "weather.h"
#include "race.h"
#include "render.h"

/* Layout follows the Yacht Club menu spec: a title block on the left, a rule, the list on the right
   and a black hint bar along the bottom. Fonts are Jersey 10 at 3x, 2x and 1x and Silkscreen, made by
   tools/make_fonts.py; if any fails to load everything falls back to the system font. Jersey's capitals
   are 10 px tall per 1x, starting 2 px per 1x below the cell top. */

typedef struct { LCDFont *title, *item, *label, *pixel, *sys; bool tried; } Fonts;
static Fonts F;

static void load_fonts(PlaydateAPI *pd) {
    if (F.tried) return;
    F.tried = true;
    const char *err;
    F.sys = pd->graphics->loadFont("/System/Fonts/Asheville-Sans-14-Bold.pft", &err);
    LCDFont **slot[] = { &F.title, &F.item, &F.label, &F.pixel };
    const char *path[] = { "fonts/Jersey10-30", "fonts/Jersey10-20", "fonts/Jersey10-10", "fonts/Silkscreen-16" };
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        *slot[i] = pd->graphics->loadFont(path[i], &err);
        if (!*slot[i]) ok = false;
    }
    if (!ok) for (int i = 0; i < 4; i++) *slot[i] = F.sys;
}

static void font(PlaydateAPI *pd, LCDFont *f, int tracking) {
    if (f) pd->graphics->setFont(f);
    pd->graphics->setTextTracking(tracking);
}

static int width(PlaydateAPI *pd, LCDFont *f, const char *s, int tracking) {
    return pd->graphics->getTextWidth(f, s, strlen(s), kUTF8Encoding, tracking);
}

static void text(PlaydateAPI *pd, const char *s, int x, int y) {
    pd->graphics->drawText(s, strlen(s), kUTF8Encoding, x, y);
}

static void cap_first(char *s) {
    if (s[0] >= 'a' && s[0] <= 'z') s[0] = (char)(s[0] - 'a' + 'A');
}

/* s with its first letter upper-cased, cut short until it fits max px in font f */
static void fit(PlaydateAPI *pd, char *out, size_t n, const char *s, LCDFont *f, int tracking, int max) {
    snprintf(out, n, "%s", s);
    cap_first(out);
    size_t len = strlen(out);
    while (len > 1 && width(pd, f, out, tracking) > max) out[--len] = 0;
}

/* Breaks s into one or two lines no wider than max in font f, at spaces or after hyphens.
   Returns the number of lines, or 0 if it will not go in two. */
static int wrap(PlaydateAPI *pd, char lines[2][32], const char *s, LCDFont *f, int tracking, int max) {
    int n = 0;
    lines[0][0] = lines[1][0] = 0;
    for (const char *p = s; *p;) {
        bool sp = false;
        while (*p == ' ') { p++; sp = true; }
        if (!*p) break;
        size_t k = strcspn(p, " -");
        if (p[k] == '-') k++;
        char *ln = lines[n], cand[64];
        snprintf(cand, sizeof cand, "%s%s%.*s", ln, ln[0] && sp ? " " : "", (int)k, p);
        if (strlen(cand) < sizeof lines[0] && width(pd, f, cand, tracking) <= max) strcpy(ln, cand);
        else {
            if (n == 1 || !ln[0] || k >= sizeof lines[0]) return 0;
            ln = lines[++n];
            snprintf(ln, sizeof lines[0], "%.*s", (int)k, p);
            if (width(pd, f, ln, tracking) > max) return 0;
        }
        p += k;
    }
    cap_first(lines[0]);
    return lines[0][0] ? n + 1 : 0;
}

#define TB_X 12        /* title block */
#define TB_W 140
#define DIV_X 158      /* rule between the title block and the column */
#define COL_X 166      /* menu column */
#define COL_W 226
#define ITEM_H 26
#define ITEM_GAP 2
#define COL_MID 106    /* the list is centred on this y */
#define BAR_Y 216      /* hint bar */
#define TITLE_BASE 105 /* baseline of the title's last line */

/* Eyebrow (NULL for none), a one- or two-line title, a double rule and a subtitle. The title is 3x if it fits
   one line, else 2x on up to two lines, else 1x. */
static void title_block(PlaydateAPI *pd, const char *eyebrow, const char *title, const char *sub) {
    const struct playdate_graphics *g = pd->graphics;
    char lines[2][32];
    if (eyebrow) {
        font(pd, F.label, 0);
        text(pd, eyebrow, TB_X, 38);
    }

    /* face, tracking, cell top to baseline, line pitch, most lines (3x on two would reach the eyebrow) */
    struct { LCDFont *f; int tracking, base, pitch, max; } face[] = {
        { F.title, 0, 36, 36, 1 }, { F.item, 0, 24, 24, 2 }, { F.label, 0, 12, 13, 2 } };
    int nl = 0, i = 0;
    for (; i < 3; i++) {
        nl = wrap(pd, lines, title, face[i].f, face[i].tracking, TB_W);
        if (nl && nl <= face[i].max) break;
    }
    if (!nl) {
        i = 2;
        nl = 1;
        fit(pd, lines[0], sizeof lines[0], title, face[i].f, 0, TB_W);
    }
    font(pd, face[i].f, face[i].tracking);
    for (int k = 0; k < nl; k++) text(pd, lines[k], TB_X, TITLE_BASE - face[i].base - face[i].pitch * (nl - 1 - k));

    g->fillRect(TB_X, 114, TB_W, 1, kColorBlack);
    g->fillRect(TB_X, 117, TB_W, 1, kColorBlack);
    font(pd, F.label, 0);
    nl = wrap(pd, lines, sub, F.label, 0, TB_W);
    if (!nl) {
        nl = 1;
        fit(pd, lines[0], sizeof lines[0], sub, F.label, 0, TB_W);
    }
    for (int k = 0; k < nl; k++) text(pd, lines[k], TB_X, 124 + 14 * k);
    g->fillRect(DIV_X, 36, 1, 144, kColorBlack);
}

static const LCDPattern half_white = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };

/* One list item at y: selected is a black bar with a marker, disabled is greyed by a checker.
   small sets the whole list at 1x, which may run closer to the item's right edge. 2x capitals fill
   rows 2-21 so the descenders end on the bar's last row; 1x capitals sit centred. */
static void item(PlaydateAPI *pd, const char *label, int y, bool sel, bool on, bool small) {
    const struct playdate_graphics *g = pd->graphics;
    int lx = COL_X + (sel ? 22 : 25), max = COL_X + COL_W - (small ? 2 : 8) - lx;
    LCDFont *f = small ? F.label : F.item;
    int ty = y + (small ? 5 : -2), tracking = 0;
    if (width(pd, f, label, 0) > max) tracking = -1;
    char buf[48];
    fit(pd, buf, sizeof buf, label, f, tracking, max);
    font(pd, f, tracking);
    if (sel) {
        g->fillRect(COL_X, y, COL_W, ITEM_H, kColorBlack);
        g->fillTriangle(COL_X + 8, y + 7, COL_X + 8, y + 17, COL_X + 14, y + 12, kColorWhite);
        g->setDrawMode(kDrawModeFillWhite);
        text(pd, buf, lx, ty);
        g->setDrawMode(kDrawModeCopy);
    } else {
        text(pd, buf, lx, ty);
        if (!on) g->fillRect(lx, y, COL_X + COL_W - lx, ITEM_H, (LCDColor)half_white);
    }
}

/* The visible window of the current list, from m->top, with arrows when more rows are off screen.
   If any label is too wide at 2x, the whole list steps down to 1x. */
static void list(PlaydateAPI *pd, const Menu *m, const char *const *labels) {
    int n = menu_rows(m), vis = n < MENU_LIST_VIS ? n : MENU_LIST_VIS;
    int h = vis * ITEM_H + (vis - 1) * ITEM_GAP, y0 = COL_MID - h / 2, cx = COL_X + COL_W / 2;
    bool small = false;
    for (int k = 0; k < n; k++) small |= width(pd, F.item, labels[k], 0) > COL_W - 22 - 8;
    for (int i = 0; i < vis; i++) {
        int k = m->top + i;
        item(pd, labels[k], y0 + i * (ITEM_H + ITEM_GAP), k == m->sel, menu_row_enabled(m, k), small);
    }
    if (m->top > 0) pd->graphics->fillTriangle(cx - 5, y0 - 4, cx + 5, y0 - 4, cx, y0 - 9, kColorBlack);
    if (m->top + vis < n) pd->graphics->fillTriangle(cx - 5, y0 + h + 4, cx + 5, y0 + h + 4, cx, y0 + h + 9, kColorBlack);
}

/* The bottom bar: A and B buttons with what they do (NULL hides one), and the d-pad's job on the right.
   Silkscreen 16's capitals sit 7 px below the cell top and are 10 px tall. */
static void hint_bar(PlaydateAPI *pd, const char *a, const char *b, const char *dpad) {
    const struct playdate_graphics *g = pd->graphics;
    const int ty = BAR_Y + 1, cy = ty + 7 + 5;   /* text y and the capitals' centre */
    g->fillRect(0, BAR_Y, CAM_W, 240 - BAR_Y, kColorBlack);
    font(pd, F.pixel, 0);
    int x = 8;
    const char *lab[2] = { a, b }, *key[2] = { "A", "B" };
    for (int i = 0; i < 2; i++) {
        if (!lab[i]) continue;
        int kx = x + 9 - width(pd, F.pixel, key[i], 0) / 2;
        if (i == 0) {
            g->fillEllipse(x, cy - 9, 18, 18, 0, 0, kColorWhite);
            g->setDrawMode(kDrawModeFillBlack);
        } else {
            g->drawEllipse(x, cy - 9, 18, 18, 1, 0, 0, kColorWhite);
            g->setDrawMode(kDrawModeFillWhite);
        }
        text(pd, key[i], kx, ty);
        g->setDrawMode(kDrawModeFillWhite);
        text(pd, lab[i], x + 23, ty);
        x += 23 + width(pd, F.pixel, lab[i], 0) + 14;
    }
    if (dpad) {
        int w = width(pd, F.pixel, dpad, 0), tx = 392 - w;
        g->setDrawMode(kDrawModeFillWhite);
        text(pd, dpad, tx, ty);
        int ax = tx - 28;   /* up and down arrows */
        g->fillTriangle(ax, cy + 4, ax + 8, cy + 4, ax + 4, cy - 4, kColorWhite);
        g->fillTriangle(ax + 11, cy - 4, ax + 19, cy - 4, ax + 15, cy + 4, kColorWhite);
    }
    g->setDrawMode(kDrawModeCopy);
}

/* Course preview: a north-up plan of the track, the full route dotted, and a solid line that
   sails it again and again so the sequence of marks reads at a glance. */
#define PV_L 12
#define PV_T 30
#define PV_W (CAM_W - 24)
#define PV_H 164
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

    const struct playdate_graphics *g = pd->graphics;
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

void menu_draw(PlaydateAPI *pd, const Menu *m, const Track *tracks, const BestTimes *best, const double *boat_best, const HelpLine *help) {
    load_fonts(pd);
    pd->graphics->clear(kColorWhite);
    pd->graphics->setDrawMode(kDrawModeCopy);
    const char *labels[64];
    char buf[MENU_MAIN_ITEMS][24];
    const char *mode = m->journey ? "Journey" : "Racing";
    const char *go = m->journey ? "SAIL" : "RACE";
    if (m->screen == MENU_HELP) {
        font(pd, F.sys, 0);
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
        text(pd, m->help_top + MENU_HELP_VIS < m->help_rows ? "v" : " ", 388, 196);
        hint_bar(pd, NULL, "BACK", "SCROLL");
    } else if (m->screen == MENU_MAIN) {
        title_block(pd, NULL, "Crank'n Sail", "A sailing simulation");
        for (int i = 0; i < MENU_MAIN_ITEMS; i++) {
            if (i == MENU_ITEM_SOUND) snprintf(buf[i], sizeof buf[i], "%s: %s", MENU_MAIN_NAMES[i], MENU_SOUND_NAMES[m->sound]);
            else snprintf(buf[i], sizeof buf[i], "%s", MENU_MAIN_NAMES[i]);
            labels[i] = buf[i];
        }
        list(pd, m, labels);
        hint_bar(pd, "SELECT", NULL, "CHOOSE");
    } else if (m->screen == MENU_TRAINING) {
        static const char *const names[MENU_TRAINING_ROWS] = { "Controls", "Sailing Lesson" };
        title_block(pd, "Menu", "Training", "");
        list(pd, m, names);
        hint_bar(pd, "SELECT", "BACK", "CHOOSE");
    } else if (m->screen == MENU_PREVIEW) {
        const Track *t = &tracks[m->track];
        font(pd, F.item, 0);
        text(pd, t->name, PV_L, 2);
        font(pd, F.sys, 0);
        draw_preview(pd, m, t);
        char btime[16], bline[32];
        double bt;
        if (best && besttimes_get(best, t->name, &bt)) hud_fmt_time(bt, btime, sizeof btime);
        else snprintf(btime, sizeof btime, "--:--.-");
        snprintf(bline, sizeof bline, "Best %s", btime);
        text(pd, bline, CAM_W - PV_L - width(pd, F.sys, bline, 0), 2);
        text(pd, t->info, PV_L, 197);
        hint_bar(pd, "CONFIRM", "BACK", NULL);
    } else if (m->screen == MENU_BOATS) {
        for (int i = 0; i < m->nboats && i < 64; i++) labels[i] = assets_boat_names()[i];
        if (m->journey) title_block(pd, "Menu › Journey", "Journey", "Choose a boat");
        else title_block(pd, "Racing › Course", tracks[m->track].name, "Choose a boat");
        if (!m->journey && boat_best && m->sel >= 0 && m->sel < m->nboats && m->sel < 64) {
            /* left column, under the subtitle: this boat's best on the course and whether it has a ghost */
            const double bt = boat_best[m->sel];
            char tbuf[16], line[32];
            if (bt > 0) hud_fmt_time(bt, tbuf, sizeof tbuf);
            else snprintf(tbuf, sizeof tbuf, "--:--.-");
            font(pd, F.label, 0);
            snprintf(line, sizeof line, "Best %s", tbuf);
            text(pd, line, TB_X, 156);
            text(pd, bt > 0 ? "Ghost: yes" : "Ghost: none", TB_X, 170);
        }
        list(pd, m, labels);
        hint_bar(pd, m->nweather > 0 ? "SELECT" : go, "BACK", "CHOOSE");
    } else if (m->screen == MENU_WEATHER) {
        char eyebrow[24];
        snprintf(eyebrow, sizeof eyebrow, "%s › Boat", mode);
        title_block(pd, eyebrow, assets_boat_names()[m->boat], "Choose weather");
        list(pd, m, WEATHER_NAMES);
        hint_bar(pd, go, "BACK", "CHOOSE");
    } else {
        for (int i = 0; i < m->ntracks && i < 64; i++) labels[i] = tracks[i].name;
        title_block(pd, "Menu › Racing", "Racing", "Choose a course");
        list(pd, m, labels);
        hint_bar(pd, "SELECT", "BACK", "CHOOSE");
    }
    /* the HUD and main.c draw with whatever font is current */
    font(pd, F.sys, 0);
}
