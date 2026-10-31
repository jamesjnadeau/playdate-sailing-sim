#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "hudinfo.h"
#include "mathx.h"

#define HUD_MODE_W 54          /* inverted mode-name cell at the left of the bar */
#define HUD_SPEED_END (HUD_MODE_W + 52)   /* right edge of the speed cell */
#define HUD_HDG_END (HUD_SPEED_END + 82)  /* right edge of the heading cell */
#define HUD_PAD 6              /* text inset inside a cell */

static void text(PlaydateAPI *pd, const char *s, int x, int y) {
    pd->graphics->drawText(s, strlen(s), kASCIIEncoding, x, y);
}

/* The system font has no gear or compass glyph, so the AT and AP badges are drawn as small icons. */
static void icon_gear(PlaydateAPI *pd, int cx, int cy) {
    static const int dx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, dy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    pd->graphics->drawEllipse(cx - 4, cy - 4, 9, 9, 1, 0, 0, kColorWhite);
    for (int i = 0; i < 8; i++)   /* teeth */
        pd->graphics->drawLine(cx + dx[i] * 4, cy + dy[i] * 4, cx + dx[i] * 6, cy + dy[i] * 6, i & 1 ? 1 : 2, kColorWhite);
    pd->graphics->fillRect(cx - 1, cy - 1, 3, 3, kColorWhite);
}

static void icon_compass(PlaydateAPI *pd, int cx, int cy) {
    pd->graphics->drawEllipse(cx - 6, cy - 6, 13, 13, 1, 0, 0, kColorWhite);
    pd->graphics->fillTriangle(cx, cy - 5, cx - 2, cy, cx + 2, cy, kColorWhite);   /* north needle */
    pd->graphics->drawLine(cx - 2, cy, cx, cy + 5, 1, kColorWhite);               /* south needle */
    pd->graphics->drawLine(cx + 2, cy, cx, cy + 5, 1, kColorWhite);
}

/* Sailing term for what each d-pad mode controls. */
static const char *mode_word(Mode m) {
    switch (m) {
    case MODE_WHEEL:  return "HELM";
    case MODE_TRIM:   return "TRIM";
    case MODE_HOIST:  return "HOIST";
    case MODE_CAMERA: return "LOOK";
    }
    return "?";
}

/* One-row status bar: mode glyph, then the mode's cells, then autopilot/autotrim icons and the wind dial. */
void hud_draw(PlaydateAPI *pd, const Game *g) {
    const BoatState *b = &g->b;
    const Mode mode = g->ctl.mode;
    char line[64];
    const int top = CAM_H - HUD_H;
    const int ty = top + 2;                /* text baseline */
    const int n = g->boat.sail_count;
    const int si = (g->ctl.sail >= 0 && g->ctl.sail < n && g->ctl.sail < BOAT_MAX_SAILS) ? g->ctl.sail : 0;

    pd->graphics->fillRect(0, top, CAM_W, HUD_H, kColorBlack);

    /* selected mode: its name in an inverted fixed-width cell */
    pd->graphics->fillRect(0, top, HUD_MODE_W, HUD_H, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeFillBlack);
    text(pd, mode_word(mode), HUD_PAD, ty);

    pd->graphics->setDrawMode(kDrawModeFillWhite);
    if (hud_shows_nav(mode)) {
        /* speed, heading and heel in fixed-width cells separated by 1px lines; the AWS number sits by the dial */
        snprintf(line, sizeof line, "%.1fkn", fabsf(b->u) * 1.94384f);
        text(pd, line, HUD_MODE_W + HUD_PAD, ty);
        pd->graphics->drawLine(HUD_SPEED_END, top, HUD_SPEED_END, CAM_H - 1, 1, kColorWhite);
        snprintf(line, sizeof line, "HDG %03d", hud_heading_deg(b->heading));
        text(pd, line, HUD_SPEED_END + HUD_PAD, ty);
        pd->graphics->drawLine(HUD_HDG_END, top, HUD_HDG_END, CAM_H - 1, 1, kColorWhite);
        snprintf(line, sizeof line, "HEEL %+.0f", b->heel * 57.29578f);
        text(pd, line, HUD_HDG_END + HUD_PAD, ty);
        snprintf(line, sizeof line, "%.0f", b->aws);
        text(pd, line, CAM_W - 12 - 8 - 26, ty);
    } else if (mode == MODE_TRIM || mode == MODE_HOIST) {
        /* selected sail: its name, then the trim or hoist bar */
        snprintf(line, sizeof line, "%.8s", n > 0 ? g->boat.sails[si].name : "");
        text(pd, line, HUD_MODE_W + HUD_PAD, ty);
        const float f = hud_sail_fraction(mode, &g->boat.sails[si], b->trims[si], b->hoists[si]);
        pd->graphics->drawRect(HUD_MODE_W + 54, top + 6, 74, 8, kColorWhite);
        pd->graphics->fillRect(HUD_MODE_W + 56, top + 8, (int)(70.0f * f), 4, kColorWhite);
    }

    const int iy = top + HUD_H / 2;
    if (g->at.on) icon_gear(pd, 274, iy);
    if (g->ap.on) {
        icon_compass(pd, 298, iy);
        snprintf(line, sizeof line, "%03d", hud_heading_deg(g->ap.target));
        text(pd, line, 308, ty);
    }

    /* apparent wind dial: the needle points where the wind is going (downwind), relative to the bow (up) */
    const int cx = CAM_W - 12, cy = top + HUD_H / 2, R = 8;
    pd->graphics->drawEllipse(cx - R, cy - R, 2 * R, 2 * R, 1, 0, 0, kColorWhite);
    float a = b->awa + 3.14159265f;
    pd->graphics->drawLine(cx, cy, (int)(cx + R * sinf(a)), (int)(cy - R * cosf(a)), 2, kColorWhite);
    pd->graphics->drawLine(cx, cy - R - 2, cx, cy - R + 2, 1, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeCopy);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void tri(PlaydateAPI *pd, float x1, float y1, float x2, float y2, float x3, float y3) {
    int ax = (int)lroundf(x1), ay = (int)lroundf(y1), bx = (int)lroundf(x2), by = (int)lroundf(y2);
    int cx = (int)lroundf(x3), cy = (int)lroundf(y3);
    pd->graphics->fillTriangle(ax, ay, bx, by, cx, cy, kColorBlack);
    pd->graphics->drawLine(ax, ay, bx, by, 1, kColorWhite);
    pd->graphics->drawLine(ax, ay, cx, cy, 1, kColorWhite);
    pd->graphics->drawLine(bx, by, cx, cy, 1, kColorWhite);
}

/* "NNNm" distance label, width ~6px per char. Left edge at x (or centred on x), kept on screen. */
static void dist_label(PlaydateAPI *pd, double dist, int x, int y, bool centred) {
    char line[64];
    snprintf(line, sizeof line, "%.0fm", dist);
    const int w = 6 * (int)strlen(line);
    if (centred) x -= w / 2;
    text(pd, line, clampi(x, 2, CAM_W - 2 - w), clampi(y, 2, CAM_H - HUD_H - 18));
}

void hud_race(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    const Track *tr = &g->track;
    const Race *r = &g->race;
    const bool done = r->finished || r->next >= tr->nmarks;
    if (done) return;

    double mx, my;
    race_mark_local(r, tr, r->next, &mx, &my);
    const double dist = hypot(mx - g->b.x, my - g->b.y);

    /* indicator: above the buoy when on screen, else an arrow on the edge pointing at it */
    MarkView mv;
    camera_mark_view(c, mx, my, (float)(CAM_H - HUD_H), &mv);
    pd->graphics->setDrawMode(kDrawModeCopy);
    if (mv.on_screen) {
        float x = mv.x, y = mv.y;
        tri(pd, x, y, x - 5.0f, y - 10.0f, x + 5.0f, y - 10.0f);
        dist_label(pd, dist, (int)lroundf(x), (int)lroundf(y) - 24, true);
    } else {
        float s = sinf(mv.angle), co = cosf(mv.angle);
        float bcx = mv.x - 6.0f * co, bcy = mv.y - 6.0f * s;
        tri(pd, mv.x + 6.0f * co, mv.y + 6.0f * s, bcx - 6.0f * s, bcy + 6.0f * co, bcx + 6.0f * s, bcy - 6.0f * co);
        dist_label(pd, dist, (int)mv.x + 10, (int)mv.y - 6, false);
    }
    pd->graphics->setDrawMode(kDrawModeCopy);
}

void hud_prompt(PlaydateAPI *pd, const Game *g) {
    if (g->mode == GAME_RACE && g->run == RUN_SAILING && g->race.finished) {
        char time[16], line[32];
        hud_fmt_time(g->race.t, time, sizeof time);
        snprintf(line, sizeof line, "Time %s", time);
        pd->graphics->fillRect(90, 74, 220, 76, kColorBlack);
        pd->graphics->drawRect(94, 78, 212, 68, kColorWhite);
        pd->graphics->setDrawMode(kDrawModeFillWhite);
        text(pd, "FINISHED", 200 - 4 * 8, 84);
        text(pd, line, 200 - 3 * (int)strlen(line), 104);
        text(pd, "Press A to continue", 94, 124);
        pd->graphics->setDrawMode(kDrawModeCopy);
        return;
    }
    if (g->run == RUN_SAILING) return;
    const char *head = g->run == RUN_CAPSIZED ? "CAPSIZED" : "WRECKED";
    pd->graphics->fillRect(90, 80, 220, 64, kColorBlack);
    pd->graphics->drawRect(94, 84, 212, 56, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    text(pd, head, 200 - 4 * (int)strlen(head), 90);
    text(pd, "Press A to restart", 98, 114);
    pd->graphics->setDrawMode(kDrawModeCopy);
}

/* System pause-menu panel: the left ~200px of a 400x240 bitmap show beside the menu. */
void hud_pause_panel(PlaydateAPI *pd, const Game *g) {
    const Track *tr = &g->track;
    const Race *r = &g->race;
    char line[64];
    char time[16], mark[48];
    int y = 6;
    pd->graphics->clear(kColorWhite);
    pd->graphics->setDrawMode(kDrawModeCopy);
    if (g->mode != GAME_RACE) return;
#define ROW(...) do { snprintf(line, sizeof line, __VA_ARGS__); text(pd, line, 4, y); y += 18; } while (0)
    hud_fmt_time(r->t, time, sizeof time);
    if (r->finished || r->next >= tr->nmarks) {
        ROW("FINISHED");
        ROW("Time %s", time);
    } else {
        double mx, my;
        race_mark_local(r, tr, r->next, &mx, &my);
        hud_fmt_mark(tr->marks[r->next].name, r->next + 1, tr->nmarks, hypot(mx - g->b.x, my - g->b.y), mark, sizeof mark);
        ROW("Time %s", time);
        ROW("%s", mark);
    }
#undef ROW
}
