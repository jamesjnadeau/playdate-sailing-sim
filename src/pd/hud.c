#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "hudinfo.h"
#include "mathx.h"

#define HUD_GLYPH_W 22         /* inverted d-pad mode cell at the left of the bar */
#define HUD_PAD 6              /* text inset inside a cell */

static void text(PlaydateAPI *pd, const char *s, int x, int y) {
    pd->graphics->drawText(s, strlen(s), kASCIIEncoding, x, y);
}

static void text_utf8(PlaydateAPI *pd, const char *s, int x, int y) {
    pd->graphics->drawText(s, strlen(s), kUTF8Encoding, x, y);
}

/* One-row status bar: mode glyph, then the mode's cells, then AP/AT badges and the wind dial. */
void hud_draw(PlaydateAPI *pd, const Game *g) {
    const BoatState *b = &g->b;
    const Mode mode = g->ctl.mode;
    char line[64];
    const int top = CAM_H - HUD_H;
    const int ty = top + 2;                /* text baseline */
    const int n = g->boat.sail_count;
    const int si = (g->ctl.sail >= 0 && g->ctl.sail < n && g->ctl.sail < BOAT_MAX_SAILS) ? g->ctl.sail : 0;

    pd->graphics->fillRect(0, top, CAM_W, HUD_H, kColorBlack);

    /* selected mode: its d-pad glyph in an inverted cell */
    pd->graphics->fillRect(0, top, HUD_GLYPH_W, HUD_H, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeFillBlack);
    text_utf8(pd, hud_mode_glyph(mode), 6, ty);

    pd->graphics->setDrawMode(kDrawModeFillWhite);
    if (hud_shows_nav(mode)) {
        /* speed, heading and heel cells, separated by 1px lines; the AWS number sits by the dial */
        snprintf(line, sizeof line, "%.1fkn", fabsf(b->u) * 1.94384f);
        text(pd, line, 22 + HUD_PAD, ty);
        pd->graphics->drawLine(74, top, 74, CAM_H - 1, 1, kColorWhite);
        snprintf(line, sizeof line, "%03d\xC2\xB0", hud_heading_deg(b->heading));   /* U+00B0 degree sign */
        text_utf8(pd, line, 75 + HUD_PAD, ty);
        pd->graphics->drawLine(119, top, 119, CAM_H - 1, 1, kColorWhite);
        snprintf(line, sizeof line, "\xE2\x88\xA0%+.0f", b->heel * 57.29578f);       /* U+2220 angle */
        text_utf8(pd, line, 120 + HUD_PAD, ty);
        snprintf(line, sizeof line, "%.0f", b->aws);
        text(pd, line, CAM_W - 12 - 8 - 26, ty);
    } else if (mode == MODE_TRIM || mode == MODE_HOIST) {
        /* selected sail: its name, then the trim or hoist bar */
        snprintf(line, sizeof line, "%.8s", n > 0 ? g->boat.sails[si].name : "");
        text(pd, line, 22 + HUD_PAD, ty);
        const float f = hud_sail_fraction(mode, &g->boat.sails[si], b->trims[si], b->hoists[si]);
        pd->graphics->drawRect(120, top + 6, 74, 8, kColorWhite);
        pd->graphics->fillRect(122, top + 8, (int)(70.0f * f), 4, kColorWhite);
    } else {
        text(pd, "Look", 22 + HUD_PAD, ty);
    }

    if (g->ap.on) {
        snprintf(line, sizeof line, "AP %03d", hud_heading_deg(g->ap.target));
        text(pd, line, 236, ty);
    }
    if (g->at.on) text(pd, "AT", 290, ty);

    /* apparent wind dial: the tick shows where the wind comes from relative to the bow (up) */
    const int cx = CAM_W - 12, cy = top + HUD_H / 2, R = 8;
    pd->graphics->drawEllipse(cx - R, cy - R, 2 * R, 2 * R, 1, 0, 0, kColorWhite);
    float a = b->awa;
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

void hud_race(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    const Track *tr = &g->track;
    const Race *r = &g->race;
    char line[64];
    const bool done = r->finished || r->next >= tr->nmarks;
    const int mins = (int)(r->t / 60.0);

    pd->graphics->fillRect(0, 0, CAM_W, 18, kColorBlack);
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    if (done) {
        snprintf(line, sizeof line, "FINISHED  %02d:%04.1f", mins, fmod(r->t, 60.0));
        text(pd, line, 4, 2);
        pd->graphics->setDrawMode(kDrawModeCopy);
        return;
    }
    double mx, my;
    race_mark_local(r, tr, r->next, &mx, &my);
    const double dist = hypot(mx - g->b.x, my - g->b.y);
    snprintf(line, sizeof line, "MARK %s %d/%d  %4.0fm  %02d:%04.1f", tr->marks[r->next].name,
             r->next + 1, tr->nmarks, dist, mins, fmod(r->t, 60.0));
    text(pd, line, 4, 2);
    pd->graphics->setDrawMode(kDrawModeCopy);

    /* indicator: above the buoy when on screen, else an arrow on the edge pointing at it */
    MarkView mv;
    camera_mark_view(c, mx, my, (float)(CAM_H - HUD_H), &mv);
    if (mv.on_screen) {
        float x = mv.x, y = mv.y;
        tri(pd, x, y, x - 5.0f, y - 10.0f, x + 5.0f, y - 10.0f);
    } else {
        float s = sinf(mv.angle), co = cosf(mv.angle);
        float bcx = mv.x - 6.0f * co, bcy = mv.y - 6.0f * s;
        tri(pd, mv.x + 6.0f * co, mv.y + 6.0f * s, bcx - 6.0f * s, bcy + 6.0f * co, bcx + 6.0f * s, bcy - 6.0f * co);
        snprintf(line, sizeof line, "%.0fm", dist);
        int tx = clampi((int)mv.x + 10, 4, CAM_W - 48);
        int ty = clampi((int)mv.y - 6, 20, CAM_H - HUD_H - 18);
        text(pd, line, tx, ty);
    }
    pd->graphics->setDrawMode(kDrawModeCopy);
}

void hud_prompt(PlaydateAPI *pd, const Game *g) {
    if (g->run == RUN_SAILING) return;
    const char *head = g->run == RUN_CAPSIZED ? "CAPSIZED" : "WRECKED";
    pd->graphics->fillRect(90, 80, 220, 64, kColorBlack);
    pd->graphics->drawRect(94, 84, 212, 56, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    text(pd, head, 200 - 4 * (int)strlen(head), 90);
    text(pd, "Press A to restart", 98, 114);
    pd->graphics->setDrawMode(kDrawModeCopy);
}

/* Diagnostics for the system-menu panel: the left ~200px of a 400x240 bitmap show beside the menu. */
void hud_diag(PlaydateAPI *pd, const Game *g) {
    const BoatState *b = &g->b;
    char line[64];
    int y = 6;
    const float R2D = 57.29578f;
    pd->graphics->clear(kColorWhite);
    pd->graphics->setDrawMode(kDrawModeCopy);
#define ROW(...) do { snprintf(line, sizeof line, __VA_ARGS__); text(pd, line, 4, y); y += 18; } while (0)
    ROW("TWS %.1f AWS %.1f m/s", g->wind.speed, b->aws);
    ROW("AWA %+.0f  heel %+.0f  u %.1f", b->awa * R2D, b->heel * R2D, b->u);
    for (int i = 0; i < g->boat.sail_count && i < BOAT_MAX_SAILS; i++) {
        const SailForce *f = &b->sail[i];
        ROW("%.6s h%.0f t%.0f L%.1f", g->boat.sails[i].name, b->hoists[i] * 100.0f, b->trims[i] * R2D, f->luff);
        ROW(" F %+.0f %+.0f aoa %+.0f", f->fx, f->fy, f->aoa * R2D);
    }
    ROW("SAILS %+.0f %+.0f N", b->sail_fx, b->sail_fy);
    ROW(" hl %+.0f yw %+.0f Nm", b->sail_heel, b->sail_yaw);
    ROW("helm %+.0f N", b->helm_load);
#undef ROW
}
