#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "hudinfo.h"
#include "mathx.h"
#include "praise.h"

#define HUD_MODE_W 54          /* inverted mode-name cell at the left of the bar */
#define HUD_SPEED_END (HUD_MODE_W + 52)   /* right edge of the speed cell */
#define HUD_HDG_END (HUD_SPEED_END + 82)  /* right edge of the heading cell */
#define HUD_PAD 6              /* text inset inside a cell */

static void text(PlaydateAPI *pd, const char *s, int x, int y) {
    pd->graphics->drawText(s, strlen(s), kASCIIEncoding, x, y);
}

#define MSG_BACK_DX 1          /* offset of the white layer behind an on-screen message */
#define MSG_BACK_DY 1

/* Centred on-screen message: black text over a white layer offset behind it. The one place the style of
   praise and the assist/engine banners lives. */
static void message(PlaydateAPI *pd, const char *s, int y) {
    const int w = pd->graphics->getTextWidth(NULL, s, strlen(s), kASCIIEncoding, 0);   /* NULL: the current font */
    int x = (CAM_W - w) / 2;
    if (x < 0) x = 0;
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    text(pd, s, x + MSG_BACK_DX, y + MSG_BACK_DY);
    pd->graphics->setDrawMode(kDrawModeCopy);
    text(pd, s, x, y);
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
    if (mode == MODE_CAMERA && g->ctl.camera_vertical) {   /* harpoon: the crank raises and lowers the view */
        const int hx = HUD_MODE_W - 8;
        pd->graphics->drawLine(hx, top + 4, hx, top + 15, 1, kColorBlack);
        pd->graphics->drawLine(hx, top + 4, hx + 3, top + 8, 1, kColorBlack);
    }

    pd->graphics->setDrawMode(kDrawModeFillWhite);
    if (hud_shows_nav(mode)) {
        /* speed, heading and heel in fixed-width cells separated by 1px lines; the AWS number sits by the dial */
        snprintf(line, sizeof line, "%.1fkn", (double)(fabsf(b->u) * 1.94384f));
        text(pd, line, HUD_MODE_W + HUD_PAD, ty);
        pd->graphics->drawLine(HUD_SPEED_END, top, HUD_SPEED_END, CAM_H - 1, 1, kColorWhite);
        snprintf(line, sizeof line, "HDG %03d", hud_heading_deg(b->heading));
        text(pd, line, HUD_SPEED_END + HUD_PAD, ty);
        pd->graphics->drawLine(HUD_HDG_END, top, HUD_HDG_END, CAM_H - 1, 1, kColorWhite);
        snprintf(line, sizeof line, "HEEL %+.0f", (double)(b->heel * 57.29578f));
        text(pd, line, HUD_HDG_END + HUD_PAD, ty);
        snprintf(line, sizeof line, "%.0f", (double)b->aws);
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

    /* apparent wind dial, relative to the view: up is where the camera looks, the notch marks the bow and
       the needle points where the wind is going (downwind) */
    const int cx = CAM_W - 12, cy = top + HUD_H / 2, R = 8;
    const float look = g->ctl.camera_yaw;
    pd->graphics->drawEllipse(cx - R, cy - R, 2 * R, 2 * R, 1, 0, 0, kColorWhite);
    float a = hud_dial_angle(b->awa + 3.14159265f, look);
    pd->graphics->drawLine(cx, cy, (int)(cx + R * sinf(a)), (int)(cy - R * cosf(a)), 2, kColorWhite);
    float bow = hud_dial_angle(0.0f, look);
    pd->graphics->drawLine((int)lroundf(cx + (R - 2) * sinf(bow)), (int)lroundf(cy - (R - 2) * cosf(bow)),
                           (int)lroundf(cx + (R + 2) * sinf(bow)), (int)lroundf(cy - (R + 2) * cosf(bow)),
                           1, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeCopy);

    /* the rudder flow floor is helping a slow boat steer: say so, above the status bar */
    const char *banners[2] = { hud_assist_text(b->rudder_assist, b->wheel), hud_engine_text(g->in.engine) };
    int row = 0;
    for (int i = 0; i < 2; i++) {
        if (!banners[i]) continue;
        message(pd, banners[i], top - 16 - 16 * row++ + 1);
    }
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

/* Above the buoy when on screen, else an arrow on the edge pointing at it. */
static void mark_indicator(PlaydateAPI *pd, const CameraPose *c, double mx, double my, double dist) {
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

void hud_race(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    const Track *tr = &g->track;
    const Race *r = &g->race;
    const bool done = r->finished || r->next >= tr->nmarks;
    if (done) return;

    double mx, my;
    race_mark_local(r, tr, r->next, &mx, &my);
    mark_indicator(pd, c, mx, my, hypot(mx - g->b.x, my - g->b.y));
}

void hud_ghost(PlaydateAPI *pd, const Game *g, const CameraPose *c, Ghost *ghost) {
    GhostPose p;
    double tg;
    if (!ghost || g->mode != GAME_RACE || g->race.finished || !ghost_pose_at(ghost, g->race.t, &p) ||
        !ghost_time_at_progress(ghost, ghost_progress(g), &tg)) return;

    char line[24];
    snprintf(line, sizeof line, "GHOST %+.1fs", g->race.t - tg);
    const int w = 6 * (int)strlen(line) + 8, x = (CAM_W - w) / 2;
    pd->graphics->setDrawMode(kDrawModeCopy);
    pd->graphics->fillRect(x, 2, w, 14, kColorWhite);
    pd->graphics->drawRect(x, 2, w, 14, kColorBlack);
    text(pd, line, x + 4, 3);

    MarkView mv;
    camera_mark_view(c, p.x - g->race.ox, p.y - g->race.oy, (float)(CAM_H - HUD_H), &mv);
    if (mv.on_screen) return;   /* the boat itself is in view */
    float s = sinf(mv.angle), co = cosf(mv.angle);
    float bcx = mv.x - 6.0f * co, bcy = mv.y - 6.0f * s;
    tri(pd, mv.x + 6.0f * co, mv.y + 6.0f * s, bcx - 6.0f * s, bcy + 6.0f * co, bcx + 6.0f * s, bcy - 6.0f * co);
    dist_label(pd, hypot(p.x - g->race.ox - g->b.x, p.y - g->race.oy - g->b.y), (int)mv.x + 10, (int)mv.y - 6, false);
}

void hud_journey(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    const JourneyBuoy *b = journey_next(&g->journey);
    if (!b) return;
    mark_indicator(pd, c, b->x, b->y, hypot(b->x - g->b.x, b->y - g->b.y));
}

/* Praise after a buoy is rounded, drawn by message(), cleared once its time is up. No box behind it, so it
   reads over sea and sky alike. */
void hud_praise(PlaydateAPI *pd, const Game *g) {
    if (g->praise_left <= 0.0f) return;
    message(pd, praise_text(g->praise), 26);
}

/* The lesson's text for the current step, word-wrapped under the HUD bar. */
void hud_lesson(PlaydateAPI *pd, const char *s) {
    enum { COLS = 44, MAX_LINES = 4 };
    char line[COLS + 1];
    int n = 0, y = 40;
    while (*s && n < MAX_LINES) {
        int len = (int)strlen(s);
        if (len > COLS) {
            len = COLS;
            while (len > 0 && s[len] != ' ') len--;
            if (len == 0) len = COLS;
        }
        memcpy(line, s, (size_t)len);
        line[len] = 0;
        message(pd, line, y);
        y += 16;
        n++;
        s += len;
        while (*s == ' ') s++;
    }
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

/* Asked before "go back" leaves the run; the same box as the finish and wreck panels. */
void hud_back_confirm(PlaydateAPI *pd) {
    pd->graphics->fillRect(90, 80, 220, 64, kColorBlack);
    pd->graphics->drawRect(94, 84, 212, 56, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    text(pd, "GO BACK?", 200 - 4 * 8, 90);
    text(pd, "Return to the home screen", 98, 106);
    text(pd, "A: yes     B: no", 98, 122);
    pd->graphics->setDrawMode(kDrawModeCopy);
}

void hud_demo_end(PlaydateAPI *pd) {
    static const char *const lines[] = { "Thanks for playing the demo", "Back to the home screen...", "A: continue" };
    pd->graphics->fillRect(60, 76, 280, 72, kColorBlack);
    pd->graphics->drawRect(64, 80, 272, 64, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    for (int i = 0; i < 3; i++) text(pd, lines[i], 200 - (int)strlen(lines[i]) * 4, 88 + i * 16);   /* 8 px a character, centred */
    pd->graphics->setDrawMode(kDrawModeCopy);
}

/* Course map in the pause panel: north-up, x north / y east in the track frame. */
#define MAP_L 2
#define MAP_T 62
#define MAP_W 194
#define MAP_H 174

typedef struct { float cx, cy, scale, x0, y0; } MapView;

static int map_sx(const MapView *v, float wy) { return (int)lroundf(v->cx + (wy - v->y0) * v->scale); }
static int map_sy(const MapView *v, float wx) { return (int)lroundf(v->cy - (wx - v->x0) * v->scale); }

static void map_dashed(PlaydateAPI *pd, int x0, int y0, int x1, int y1) {
    const float dx = (float)(x1 - x0), dy = (float)(y1 - y0), len = hypotf(dx, dy);
    for (float d = 0; d < len; d += 6.0f) {
        float e = fminf(d + 3.0f, len);
        pd->graphics->drawLine((int)lroundf(x0 + dx * d / len), (int)lroundf(y0 + dy * d / len),
                               (int)lroundf(x0 + dx * e / len), (int)lroundf(y0 + dy * e / len), 1, kColorBlack);
    }
}

/* A 270 degree arc round (cx, cy) with an arrowhead, clockwise on the map for starboard roundings
   and anticlockwise for port: the way the boat has to go round the mark. `gap` is the map bearing
   (rad, 0 up, clockwise) the 90 degree gap is centred on: where the boat comes in from. */
static void map_round_arrow(PlaydateAPI *pd, int cx, int cy, int rounding, float gap) {
    const float R = 11.0f, sweep = 4.712389f;   /* 270 deg */
    const float dir = rounding == ROUND_STARBOARD ? 1.0f : -1.0f;
    const float a0 = gap - dir * sweep / 2;
    int px = 0, py = 0;
    float a = a0;
    for (int i = 0; i <= 12; i++) {
        a = a0 + dir * sweep * i / 12.0f;
        int qx = cx + (int)lroundf(R * sinf(a)), qy = cy - (int)lroundf(R * cosf(a));
        if (i) pd->graphics->drawLine(px, py, qx, qy, 2, kColorBlack);
        px = qx;
        py = qy;
    }
    /* tangent at the end of the arc, in screen coordinates */
    const float tx = dir * cosf(a), ty = dir * sinf(a);
    pd->graphics->fillTriangle(px + (int)lroundf(6 * tx), py + (int)lroundf(6 * ty),
                               px - (int)lroundf(4 * ty), py + (int)lroundf(4 * tx),
                               px + (int)lroundf(4 * ty), py - (int)lroundf(4 * tx), kColorBlack);
}

/* Wind arrow in the map's corner, `wd` the map bearing the wind blows towards. */
static void map_wind_arrow(PlaydateAPI *pd, float wd) {
    const struct playdate_graphics *gr = pd->graphics;
    const int ax = MAP_L + 18, ay = MAP_T + 22;
    const int tx = ax + (int)lroundf(12 * sinf(wd)), ty = ay - (int)lroundf(12 * cosf(wd));
    gr->drawLine(ax - (int)lroundf(12 * sinf(wd)), ay + (int)lroundf(12 * cosf(wd)), tx, ty, 2, kColorBlack);
    gr->fillTriangle(tx, ty,
                     tx - (int)lroundf(5 * sinf(wd) + 3 * cosf(wd)), ty + (int)lroundf(5 * cosf(wd) - 3 * sinf(wd)),
                     tx - (int)lroundf(5 * sinf(wd) - 3 * cosf(wd)), ty + (int)lroundf(5 * cosf(wd) + 3 * sinf(wd)),
                     kColorBlack);
    text(pd, "WIND", MAP_L + 4, MAP_T + MAP_H - 18);
}

static void draw_course_map(PlaydateAPI *pd, const Game *g) {
    const Track *tr = &g->track;
    const Race *r = &g->race;
    const struct playdate_graphics *gr = pd->graphics;
    const float bx = (float)(g->b.x + r->ox), by = (float)(g->b.y + r->oy);   /* boat, track frame */
    float lo_x = tr->start_x, hi_x = lo_x, lo_y = tr->start_y, hi_y = lo_y;
#define EXT(X, Y) do { float _x = (X), _y = (Y); \
        if (_x < lo_x) lo_x = _x; \
        if (_x > hi_x) hi_x = _x; \
        if (_y < lo_y) lo_y = _y; \
        if (_y > hi_y) hi_y = _y; } while (0)
    for (int i = 0; i < tr->nmarks; i++) EXT(tr->marks[i].x, tr->marks[i].y);
    EXT(bx, by);
#undef EXT
    float sx = hi_y - lo_y, sy = hi_x - lo_x;
    if (sx < 20) sx = 20;
    if (sy < 20) sy = 20;
    const int pad = 18;
    MapView v = { MAP_L + MAP_W / 2.0f, MAP_T + MAP_H / 2.0f,
                  fminf((MAP_W - 2 * pad) / sx, (MAP_H - 2 * pad) / sy), (hi_x + lo_x) / 2, (hi_y + lo_y) / 2 };
    gr->drawRect(MAP_L, MAP_T, MAP_W, MAP_H, kColorBlack);

    const bool done = r->finished || r->next >= tr->nmarks;
    int px = map_sx(&v, tr->start_y), py = map_sy(&v, tr->start_x);
    gr->fillRect(px - 3, py - 3, 7, 7, kColorBlack);
    /* the legs, dashed; the leg being sailed is drawn solid from the boat */
    for (int i = 0; i < tr->nmarks; i++) {
        int qx = map_sx(&v, tr->marks[i].y), qy = map_sy(&v, tr->marks[i].x);
        map_dashed(pd, px, py, qx, qy);
        px = qx;
        py = qy;
    }
    for (int i = 0; i < tr->nmarks; i++) {
        const Mark *k = &tr->marks[i];
        int mx = map_sx(&v, k->y), my = map_sy(&v, k->x);
        const bool next = !done && i == r->next;
        if (strcmp(k->name, "FIN") == 0) {
            gr->drawLine(mx, my - 5, mx, my + 5, 2, kColorBlack);
            gr->drawLine(mx - 5, my, mx + 5, my, 2, kColorBlack);
        } else {
            gr->fillEllipse(mx - 4, my - 4, 9, 9, 0, 0, next ? kColorBlack : kColorWhite);
            gr->drawEllipse(mx - 4, my - 4, 9, 9, 2, 0, 0, kColorBlack);
        }
        if (next) map_round_arrow(pd, mx, my, k->rounding, 3.14159265f);   /* gap at the bottom of the mark */
        char lab[10];
        snprintf(lab, sizeof lab, "%s", k->name);
        text(pd, lab, mx + 8, my - 18);
    }
    if (!done) {
        const Mark *k = &tr->marks[r->next];
        gr->drawLine(map_sx(&v, by), map_sy(&v, bx), map_sx(&v, k->y), map_sy(&v, k->x), 3, kColorBlack);
    }

    /* the boat: a triangle pointing along its heading (heading h -> screen (sin h, -cos h)) */
    const float h = g->b.heading, fx = sinf(h), fy = -cosf(h);
    const int ox = map_sx(&v, by), oy = map_sy(&v, bx);
    gr->fillEllipse(ox - 6, oy - 6, 13, 13, 0, 0, kColorWhite);
    gr->fillTriangle(ox + (int)lroundf(8 * fx), oy + (int)lroundf(8 * fy),
                     ox - (int)lroundf(5 * fx + 4 * fy), oy - (int)lroundf(5 * fy - 4 * fx),
                     ox - (int)lroundf(5 * fx - 4 * fy), oy - (int)lroundf(5 * fy + 4 * fx), kColorBlack);

    map_wind_arrow(pd, tr->wind_from + 3.14159265f);
}

/* Journey map: the boat, the next buoy with the way to round it, and the buoy after, north-up. */
static void draw_journey_map(PlaydateAPI *pd, const Game *g) {
    const Journey *j = &g->journey;
    const JourneyBuoy *b = journey_next(j);
    const struct playdate_graphics *gr = pd->graphics;
    gr->drawRect(MAP_L, MAP_T, MAP_W, MAP_H, kColorBlack);
    if (!b) return;
    const JourneyBuoy *c = j->next + 1 < j->n ? &j->buoy[j->next + 1] : NULL;
    const float bx = (float)g->b.x, by = (float)g->b.y;
    float lo_x = fminf(bx, (float)b->x), hi_x = fmaxf(bx, (float)b->x);
    float lo_y = fminf(by, (float)b->y), hi_y = fmaxf(by, (float)b->y);
    if (c) {
        lo_x = fminf(lo_x, (float)c->x); hi_x = fmaxf(hi_x, (float)c->x);
        lo_y = fminf(lo_y, (float)c->y); hi_y = fmaxf(hi_y, (float)c->y);
    }
    float sx = hi_y - lo_y, sy = hi_x - lo_x;
    if (sx < 60) sx = 60;
    if (sy < 60) sy = 60;
    const int pad = 22;
    MapView v = { MAP_L + MAP_W / 2.0f, MAP_T + MAP_H / 2.0f,
                  fminf((MAP_W - 2 * pad) / sx, (MAP_H - 2 * pad) / sy), (hi_x + lo_x) / 2, (hi_y + lo_y) / 2 };
    const int mx = map_sx(&v, (float)b->y), my = map_sy(&v, (float)b->x);
    if (c) {
        const int qx = map_sx(&v, (float)c->y), qy = map_sy(&v, (float)c->x);
        map_dashed(pd, mx, my, qx, qy);
        gr->fillEllipse(qx - 3, qy - 3, 7, 7, 0, 0, kColorWhite);
        gr->drawEllipse(qx - 3, qy - 3, 7, 7, 2, 0, 0, kColorBlack);
    }
    gr->fillEllipse(mx - 4, my - 4, 9, 9, 0, 0, kColorBlack);
    gr->drawEllipse(mx - 4, my - 4, 9, 9, 2, 0, 0, kColorBlack);
    map_round_arrow(pd, mx, my, b->rounding, b->gate + (float)b->rounding * 1.57079633f + 3.14159265f);   /* open behind the incoming leg */
    gr->drawLine(map_sx(&v, by), map_sy(&v, bx), mx, my, 1, kColorBlack);

    const float h = g->b.heading, fx = sinf(h), fy = -cosf(h);
    const int ox = map_sx(&v, by), oy = map_sy(&v, bx);
    gr->fillEllipse(ox - 6, oy - 6, 13, 13, 0, 0, kColorWhite);
    gr->fillTriangle(ox + (int)lroundf(8 * fx), oy + (int)lroundf(8 * fy),
                     ox - (int)lroundf(5 * fx + 4 * fy), oy - (int)lroundf(5 * fy - 4 * fx),
                     ox - (int)lroundf(5 * fx - 4 * fy), oy - (int)lroundf(5 * fy + 4 * fx), kColorBlack);
    map_wind_arrow(pd, g->wind.dir);
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
    if (g->mode == GAME_JOURNEY) {
        const Journey *j = &g->journey;
        const JourneyBuoy *b = journey_next(j);
#define ROW(...) do { snprintf(line, sizeof line, __VA_ARGS__); text(pd, line, 4, y); y += 18; } while (0)
        hud_fmt_time(j->t, time, sizeof time);
        ROW("Time %s  Passed %d", time, j->passed);
        if (b) {
            ROW("Next buoy %.0fm", hypot(b->x - g->b.x, b->y - g->b.y));
            ROW("Round to %s", b->rounding == ROUND_STARBOARD ? "starboard" : "port");
        }
#undef ROW
        draw_journey_map(pd, g);
        return;
    }
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
        ROW("Round to %s", tr->marks[r->next].rounding == ROUND_STARBOARD ? "starboard" : "port");
    }
#undef ROW
    draw_course_map(pd, g);
}
