/* What the page needs to draw a frame, as JSON. Positions are metres relative to the camera (the page
 * puts the camera at the origin), in the sim frame: x, y on the sea (y to starboard of x), z up. */
#include "web.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "follower.h"
#include "hudinfo.h"
#include "hullgeom.h"
#include "journey.h"
#include "praise.h"
#include "sail.h"

/* ---- a tiny JSON writer ---- */

static char *buf;
static size_t cap, len;
static int need_comma;

static void reserve(size_t extra) {
    if (len + extra + 1 <= cap) return;
    size_t nc = cap ? cap : 65536;
    while (nc < len + extra + 1) nc *= 2;
    char *nb = realloc(buf, nc);
    if (!nb) return;
    buf = nb;
    cap = nc;
}

static void raw(const char *s) {
    size_t n = strlen(s);
    reserve(n);
    if (len + n + 1 > cap) return;
    memcpy(buf + len, s, n);
    len += n;
    buf[len] = 0;
}

static void rawf(const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    raw(tmp);
}

static void comma(void) {
    if (need_comma) raw(",");
    need_comma = 0;
}

static void begin_doc(void) {
    len = 0;
    reserve(1);
    if (buf) buf[0] = 0;
    need_comma = 0;
}

static void key(const char *k) {
    comma();
    rawf("\"%s\":", k);
}

static void obj(const char *k) {
    if (k) key(k); else comma();
    raw("{");
}
static void obj_end(void) { raw("}"); need_comma = 1; }
static void arr(const char *k) {
    if (k) key(k); else comma();
    raw("[");
}
static void arr_end(void) { raw("]"); need_comma = 1; }

static void num_v(double v) {
    comma();
    if (!isfinite(v)) raw("0");
    else rawf("%.5g", v);
    need_comma = 1;
}
static void num(const char *k, double v) { key(k); num_v(v); }
static void numx(const char *k, double v) { key(k); comma(); if (!isfinite(v)) raw("0"); else rawf("%.4f", v); need_comma = 1; }
static void int_v(long v) { comma(); rawf("%ld", v); need_comma = 1; }
static void integer(const char *k, long v) { key(k); int_v(v); }
static void boolean(const char *k, bool v) { key(k); raw(v ? "true" : "false"); need_comma = 1; }

static void str_v(const char *s) {
    comma();
    raw("\"");
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            if (*p == '"' || *p == '\\') { char e[3] = { '\\', (char)*p, 0 }; raw(e); }
            else if (*p < 0x20) rawf("\\u%04x", *p);
            else { char c[2] = { (char)*p, 0 }; raw(c); }
        }
    }
    raw("\"");
    need_comma = 1;
}
static void str(const char *k, const char *s) {
    key(k);
    if (s) str_v(s);
    else { raw("null"); need_comma = 1; }
}

/* ---- the boat ---- */

/* One sail's mesh, (mh+1) x (mw+1) points in the boat frame before heel (x forward, y starboard, z up from
 * the waterline). The same shape the Playdate draws (render.c draw_sail). */
static void sail_mesh(const Game *g, const BoatState *b, int i) {
    const BoatCfg *cfg = &g->boat;
    const SailCfg *s = &cfg->sails[i];
    float hoist = b->hoists[i];
    obj(NULL);
    integer("i", i);
    numx("hoist", hoist);
    numx("luff", b->luff[i]);
    if (hoist < 0.02f) {
        obj_end();
        return;
    }
    float side = b->swing[i];
    float fb = cfg->look.freeboard;
    float tx = s->tack_x, tz = fb + s->tack_z;
    float hz = tz + s->luff * hoist;
    float stay = clampf((hz - tz) / fmaxf(cfg->look.mast_height - tz, 0.1f), 0.0f, 1.0f);
    float hx = s->shape == 0 ? tx : tx + (cfg->look.mast_x - tx) * stay;
    float tr = b->trims[i];
    float foot = s->foot * hoist;
    float cx = tx - foot * cosf(tr), cy = side * foot * sinf(tr);
    float lufft = b->luff[i];
    int mw = s->mesh_w < 1 ? 1 : s->mesh_w > 12 ? 12 : s->mesh_w;
    int mh = s->mesh_h < 1 ? 1 : s->mesh_h > 16 ? 16 : s->mesh_h;
    /* the page draws a finer cloth than the Playdate's seams: at least 8 x 10 cells */
    if (mw < 8) mw = 8;
    if (mh < 10) mh = 10;
    integer("w", mw);
    integer("h", mh);
    arr("p");
    for (int a = 0; a <= mh; a++) {
        float u = (float)a / (float)mh;
        float lx = tx + (hx - tx) * u, lz = tz + (hz - tz) * u;
        float ex = cx + (hx - cx) * u, ey = cy * (1.0f - u);
        for (int q = 0; q <= mw; q++) {
            float v = (float)q / (float)mw;
            float chord = hypotf(ex - lx, ey);
            float bulge = s->camber * chord * 4.0f * v * (1.0f - v);
            float flut = lufft > 0.1f ? sinf((float)g->t * s->flutter_hz * 6.2832f + u * 3.0f + v * 5.0f) * 0.1f * lufft * chord * v : 0.0f;
            float x = lx + (ex - lx) * v;
            float y = ey * v + side * bulge + flut;
            if (s->shape == 2) {
                float bx, by;
                sail_billow(s, lufft, ex - lx, ey, u, v, b->awa, &bx, &by);
                x += bx;
                y = ey * v + by + flut;
            }
            if (s->shape == 3) {
                float off = (v - 0.5f) * s->foot;
                float sq_bulge = s->camber * s->foot * 4.0f * v * (1.0f - v) * sinf(3.14159265f * u) * (1.0f - lufft);
                float sq_flut = lufft > 0.1f ? sinf((float)g->t * s->flutter_hz * 6.2832f + u * 3.0f + v * 5.0f) * 0.1f * lufft * s->foot * v : 0.0f;
                x = cfg->look.mast_x - off * cosf(tr) * side + sq_bulge * sinf(tr);
                y = off * sinf(tr) + sq_bulge * side * cosf(tr) + sq_flut;
            }
            num_v(x); num_v(y); num_v(lz);   /* the luff and leech points share a height, so the row is level */
        }
    }
    arr_end();
    obj_end();
}

/* Where the boat sits and how it leans; heel is applied about `pivot` (boat-frame starboard offset). */
static void boat_pose(const Game *g, const CameraPose *c, const BoatState *b, bool live) {
    numx("x", b->x - c->x);
    numx("y", b->y - c->y);
    numx("heading", b->heading);
    numx("heel", b->heel);
    numx("pivot", g->boat.hull.hulls == 2 ? hull_pivot(b->heel, g->boat.hull.hull_spacing) : 0.0f);
    numx("pitch", live ? b->pitch : 0.0f);
    numx("heave", b->heave);
    numx("wheel", b->wheel);
    numx("u", b->u);
    if (live) {
        arr("imm");
        num_v(b->immersion[0]);
        num_v(b->immersion[1]);
        arr_end();
    }
    arr("sails");
    for (int i = 0; i < g->boat.sail_count && i < BOAT_MAX_SAILS; i++) sail_mesh(g, b, i);
    arr_end();
}

/* A recorded pose stands in for the boat, as render.c draw_ghost_pose does. */
static bool pose_state(const Game *g, const CameraPose *c, const SeaView *sv, const FollowPose *p, BoatState *gb) {
    *gb = g->b;
    gb->x = p->x;
    gb->y = p->y;
    if (hypot(gb->x - c->x, gb->y - c->y) > g->weather.preset[g->preset].visibility) return false;
    gb->heading = p->heading;
    gb->heel = p->heel;
    gb->wheel = p->wheel;
    gb->u = p->u;
    gb->v = gb->r = gb->pitch = 0.0f;
    gb->heave = sea_view_height(sv, (float)(gb->x - c->x), (float)(gb->y - c->y));
    for (int i = 0; i < BOAT_MAX_SAILS; i++) {
        gb->trims[i] = p->trims[i];
        gb->hoists[i] = p->hoists[i];
        gb->swing[i] = p->swing[i];
        gb->luff[i] = 0.0f;
    }
    boat_apparent_wind(gb, &g->wind, &gb->awa, &gb->aws);
    return true;
}

static bool race_ghost_pose(const Game *g, Ghost *gh, FollowPose *fp, GhostPose *gp) {
    if (!gh || g->mode != GAME_RACE || !ghost_pose_at(gh, g->race.t, gp)) return false;
    fp->x = gp->x - g->race.ox;
    fp->y = gp->y - g->race.oy;
    fp->heading = gp->heading;
    fp->heel = gp->heel;
    fp->wheel = gp->wheel;
    fp->u = gp->speed;
    for (int i = 0; i < BOAT_MAX_SAILS; i++) {
        const bool have = i < GHOST_SAILS;
        fp->trims[i] = have ? gp->trim[i] : 0.0f;
        fp->hoists[i] = have ? gp->hoist[i] : 0.0f;
        fp->swing[i] = have ? gp->swing[i] : -1.0f;
    }
    return true;
}

/* ---- the wake: the boat's recent track, kept here because the sim only records it outside races ---- */

#define WAKE_N 96
#define WAKE_STEP_M 0.5
static struct { double x, y; float u; } wake[WAKE_N];
static int wake_n;
static double wake_t = -1.0;

static void wake_track(const Game *g) {
    const BoatState *b = &g->b;
    if (g->t < wake_t || g->t - wake_t > 1.0) wake_n = 0;   /* a restart, or a long pause */
    wake_t = g->t;
    if (wake_n > 0) {
        /* the floating origin moved by a multiple of 5 * SEA_WRAP: move the trail with it */
        const double step = 5.0 * SEA_WRAP;
        double dx = b->x - wake[0].x, dy = b->y - wake[0].y;
        double sx = fabs(dx) > step / 2 ? step * floor(dx / step + 0.5) : 0.0;
        double sy = fabs(dy) > step / 2 ? step * floor(dy / step + 0.5) : 0.0;
        if (sx != 0.0 || sy != 0.0)
            for (int i = 0; i < wake_n; i++) { wake[i].x += sx; wake[i].y += sy; }
    }
    if (wake_n == 0 || hypot(b->x - wake[0].x, b->y - wake[0].y) >= WAKE_STEP_M) {
        memmove(&wake[1], &wake[0], sizeof wake[0] * (WAKE_N - 1));
        if (wake_n < WAKE_N) wake_n++;
    }
    wake[0].x = b->x;
    wake[0].y = b->y;
    wake[0].u = fabsf(b->u);
}

/* ---- marks ---- */

static void mark_obj(const Game *g, const CameraPose *c, double mx, double my, float gate, int rounding, bool next) {
    obj(NULL);
    numx("x", mx - c->x);
    numx("y", my - c->y);
    numx("z", sea_height(&g->sea, (float)mx, (float)my, (float)fmod(g->t, g->sea.period)));
    numx("gate", gate);
    integer("round", rounding);
    boolean("next", next);
    numx("dist", hypot(mx - g->b.x, my - g->b.y));
    obj_end();
}

static const char *mode_word(Mode m) {
    switch (m) {
    case MODE_WHEEL:  return "HELM";
    case MODE_TRIM:   return "TRIM";
    case MODE_HOIST:  return "HOIST";
    case MODE_CAMERA: return "LOOK";
    }
    return "?";
}

static void hud(const Game *g, Ghost *ghost) {
    const BoatState *b = &g->b;
    char line[64], time[16];
    const int n = g->boat.sail_count;
    const int si = (g->ctl.sail >= 0 && g->ctl.sail < n && g->ctl.sail < BOAT_MAX_SAILS) ? g->ctl.sail : 0;
    obj("hud");
    str("mode", mode_word(g->ctl.mode));
    boolean("vertical", g->ctl.mode == MODE_CAMERA && g->ctl.camera_vertical);
    boolean("nav", hud_shows_nav(g->ctl.mode));
    snprintf(line, sizeof line, "%.1f", fabsf(b->u) * 1.94384f);
    str("speed", line);
    snprintf(line, sizeof line, "%03d", hud_heading_deg(b->heading));
    str("hdg", line);
    snprintf(line, sizeof line, "%+.0f", b->heel * 57.29578f);
    str("heel", line);
    snprintf(line, sizeof line, "%.0f", b->aws);
    str("aws", line);
    snprintf(line, sizeof line, "%.0f", b->aws * 1.94384f);
    str("awsKn", line);
    str("sail", n > 0 ? g->boat.sails[si].name : "");
    numx("sailFrac", n > 0 ? hud_sail_fraction(g->ctl.mode, &g->boat.sails[si], b->trims[si], b->hoists[si]) : 0.0f);
    integer("sailIndex", si);
    integer("sails", n);
    boolean("autotrim", g->at.on);
    boolean("autopilot", g->ap.on);
    snprintf(line, sizeof line, "%03d", hud_heading_deg(g->ap.target));
    str("apTarget", line);
    numx("windDial", hud_dial_angle(b->awa + 3.14159265f, g->ctl.camera_yaw));
    numx("bowDial", hud_dial_angle(0.0f, g->ctl.camera_yaw));
    numx("awa", b->awa);
    arr("banners");
    const char *banners[2] = { hud_assist_text(b->rudder_assist, b->wheel), hud_engine_text(g->in.engine) };
    for (int i = 0; i < 2; i++) if (banners[i]) str_v(banners[i]);
    arr_end();
    str("praise", g->praise_left > 0.0f ? praise_text(g->praise) : NULL);

    /* the box in the middle of the screen: finished, or capsized */
    if (g->mode == GAME_RACE && g->run == RUN_SAILING && g->race.finished) {
        hud_fmt_time(g->race.t, time, sizeof time);
        obj("prompt");
        str("head", "FINISHED");
        snprintf(line, sizeof line, "Time %s", time);
        str("line", line);
        str("action", "continue");
        obj_end();
    } else if (g->run != RUN_SAILING) {
        obj("prompt");
        str("head", "CAPSIZED");
        str("line", "");
        str("action", "restart");
        obj_end();
    }

    GhostPose p;
    double tg;
    if (ghost && g->mode == GAME_RACE && !g->race.finished && ghost_pose_at(ghost, g->race.t, &p) &&
        ghost_time_at_progress(ghost, ghost_progress(g), &tg)) {
        snprintf(line, sizeof line, "%+.1fs", g->race.t - tg);
        str("ghostGap", line);
        numx("ghostDist", hypot(p.x - g->race.ox - g->b.x, p.y - g->race.oy - g->b.y));
    }

    /* pause panel rows, as hud_pause_panel */
    arr("pause");
    if (g->mode == GAME_JOURNEY) {
        const Journey *j = &g->journey;
        const JourneyBuoy *nb = journey_next(j);
        hud_fmt_time(j->t, time, sizeof time);
        snprintf(line, sizeof line, "Time %s  Passed %d", time, j->passed);
        str_v(line);
        if (nb) {
            snprintf(line, sizeof line, "Next buoy %.0fm", hypot(nb->x - g->b.x, nb->y - g->b.y));
            str_v(line);
            snprintf(line, sizeof line, "Round to %s", nb->rounding == ROUND_STARBOARD ? "starboard" : "port");
            str_v(line);
        }
    } else if (g->mode == GAME_RACE) {
        const Track *tr = &g->track;
        const Race *r = &g->race;
        hud_fmt_time(r->t, time, sizeof time);
        if (r->finished || r->next >= tr->nmarks) {
            str_v("FINISHED");
            snprintf(line, sizeof line, "Time %s", time);
            str_v(line);
        } else {
            char mark[48];
            double mx, my;
            race_mark_local(r, tr, r->next, &mx, &my);
            hud_fmt_mark(tr->marks[r->next].name, r->next + 1, tr->nmarks, hypot(mx - g->b.x, my - g->b.y), mark, sizeof mark);
            snprintf(line, sizeof line, "Time %s", time);
            str_v(line);
            str_v(mark);
            snprintf(line, sizeof line, "Round to %s", tr->marks[r->next].rounding == ROUND_STARBOARD ? "starboard" : "port");
            str_v(line);
        }
    }
    arr_end();
    if (g->mode == GAME_RACE) {
        hud_fmt_time(g->race.t, time, sizeof time);
        str("clock", time);
    } else if (g->mode == GAME_JOURNEY) {
        hud_fmt_time(g->journey.t, time, sizeof time);
        str("clock", time);
        integer("passed", g->journey.passed);
    }
    obj_end();
}

static void menu_json(void) {
    const Menu *m = app_menu();
    const Track *tracks = app_tracks();
    char tbuf[16];
    obj("menu");
    integer("screen", m->screen);
    integer("sel", m->sel);
    integer("top", m->top);
    integer("track", m->track);
    integer("boat", m->boat);
    integer("weather", m->weather);
    integer("nweather", m->nweather);
    integer("sound", m->sound);
    integer("tick", m->tick);
    boolean("journey", m->journey);
    boolean("demo", app_is_demo());
    integer("helpTop", m->help_top);
    integer("helpRows", m->help_rows);
    arr("rows");
    const int rows = menu_rows(m);
    for (int i = 0; i < rows; i++) {
        comma();
        raw("[");
        need_comma = 0;
        if (m->screen == MENU_MAIN) {
            str_v(MENU_MAIN_NAMES[i]);
        } else if (m->screen == MENU_TRACKS) {
            str_v(tracks[i].name);
        } else if (m->screen == MENU_BOATS) {
            str_v(assets_boat_names()[i]);
        } else if (m->screen == MENU_WEATHER) {
            str_v(WEATHER_NAMES[i]);
        } else if (m->screen == MENU_TRAINING) {
            str_v(i == TRAIN_ROW_LESSON ? "Sailing Lesson" : "Controls");
        } else {
            str_v("");
        }
        comma();
        raw(menu_row_enabled(m, i) ? "true" : "false");
        raw("]");
        need_comma = 1;
    }
    arr_end();
    if (m->screen == MENU_PREVIEW && m->track >= 0 && m->track < app_ntracks()) {
        double bt;
        if (besttimes_get(app_best(), tracks[m->track].name, &bt)) hud_fmt_time(bt, tbuf, sizeof tbuf);
        else snprintf(tbuf, sizeof tbuf, "--:--.-");
        str("best", tbuf);
    }
    if (m->screen == MENU_BOATS && !m->journey && m->sel >= 0 && m->sel < m->nboats && m->sel < BOATLIST_MAX) {
        const double bt = app_boat_best()[m->sel];
        if (bt > 0) hud_fmt_time(bt, tbuf, sizeof tbuf);
        else snprintf(tbuf, sizeof tbuf, "--:--.-");
        str("boatBest", tbuf);
        boolean("boatGhost", bt > 0);
    }
    obj_end();
}

const char *web_json_frame(AppDraw d) {
    const Game *g = app_game();
    const CameraPose *c = app_camera();
    begin_doc();
    obj(NULL);
    integer("draw", d);
    integer("static", web_static_gen());
    boolean("racingMenu", app_sys_racing());
    integer("boatIndex", app_boat_index());
    integer("preset", app_preset_index());
    /* the camera, sea and wind go with menu frames too: the page sails a seascape behind the menu */
    num("t", g->t);
    obj("cam");
    numx("z", c->height);
    numx("x", c->x);   /* world position: the sea's ripples and foam are anchored to it so they slide past the boat */
    numx("y", c->y);
    numx("yaw", c->yaw);
    numx("pitch", c->pitch);
    numx("roll", c->roll);
    obj_end();

    SeaView sv;
    sea_view_init(&sv, &g->sea, c->x, c->y, (float)fmod(g->t, g->sea.period));
    obj("sea");
    arr("k");
    for (int i = 0; i < sv.n; i++) { num_v(sv.kx[i]); num_v(sv.ky[i]); num_v(sv.ph[i]); num_v(sv.amp[i]); }
    arr_end();
    arr("w");   /* rad/s per component: the phase falls at this rate as time runs */
    for (int i = 0; i < sv.n; i++) num_v(g->sea.c[i].omega);
    arr_end();
    obj_end();

    obj("wind");
    numx("dir", g->wind.dir);
    numx("speed", g->wind.speed);
    obj_end();

    if (d == APP_DRAW_MENU) menu_json();
    if (d == APP_DRAW_PLAY || d == APP_DRAW_CONFIRM || d == APP_DRAW_DEMO_END) {
        wake_track(g);
        integer("mode", g->mode);
        integer("run", g->run);
        str("warning", app_warning());

        obj("boat");
        boat_pose(g, c, &g->b, true);
        obj_end();

        Ghost *gh = app_ghost_opponent();
        FollowPose fp;
        GhostPose gp;
        BoatState gb;
        if (race_ghost_pose(g, gh, &fp, &gp) && pose_state(g, c, &sv, &fp, &gb)) {
            obj("ghost");
            boat_pose(g, c, &gb, false);
            obj_end();
        } else if (g->mode != GAME_RACE && follower_pose(&g->follow, FOLLOW_DISTANCE_M, &fp) &&
                   !follower_near_eye(&fp, c->x, c->y, FOLLOW_EYE_MIN_M) && pose_state(g, c, &sv, &fp, &gb)) {
            obj("ghost");
            boat_pose(g, c, &gb, false);
            obj_end();
        }
        if (gh && g->mode == GAME_RACE && race_ghost_pose(g, gh, &fp, &gp)) {
            obj("ghostAt");
            numx("x", fp.x - c->x);
            numx("y", fp.y - c->y);
            obj_end();
        }

        arr("marks");
        if (g->mode == GAME_RACE) {
            for (int i = 0; i < g->track.nmarks && i < TRACK_MAX_MARKS; i++) {
                double mx, my;
                race_mark_local(&g->race, &g->track, i, &mx, &my);
                mark_obj(g, c, mx, my, g->track.marks[i].gate, g->track.marks[i].rounding,
                         !g->race.finished && i == g->race.next);
            }
        } else if (g->mode == GAME_JOURNEY) {
            for (int i = 0; i < g->journey.n; i++) {
                const JourneyBuoy *b = &g->journey.buoy[i];
                mark_obj(g, c, b->x, b->y, b->gate, b->rounding, i == g->journey.next);
            }
        }
        arr_end();

        arr("wake");
        for (int i = 0; i < wake_n; i++) { num_v(wake[i].x - c->x); num_v(wake[i].y - c->y); num_v(wake[i].u); }
        arr_end();

        hud(g, gh);
    }
    obj_end();
    return buf;
}

/* ---- static data ---- */

static BoatCfg last_boat;
static int gen;
static bool gen_seen;

int web_static_gen(void) {
    const Game *g = app_game();
    if (!gen_seen || memcmp(&last_boat, &g->boat, sizeof last_boat) != 0) {
        last_boat = g->boat;
        gen_seen = true;
        gen++;
    }
    return gen;
}

static void table(const char *k, const Table *t) {
    arr(k);
    for (int i = 0; i < t->n; i++) { num_v(t->p[i].x); num_v(t->p[i].y); }
    arr_end();
}

const char *web_json_static(void) {
    const Game *g = app_game();
    const BoatCfg *b = &g->boat;
    begin_doc();
    obj(NULL);
    integer("gen", web_static_gen());
    arr("boats");
    for (int i = 0; i < assets_boat_count(); i++) str_v(assets_boat_names()[i]);
    arr_end();
    arr("weathers");
    for (int i = 0; i < WEATHER_PRESETS; i++) str_v(WEATHER_NAMES[i]);
    arr_end();
    arr("presets");
    for (int i = 0; i < WEATHER_PRESETS; i++) {
        const WeatherPreset *p = &g->weather.preset[i];
        obj(NULL);
        str("name", p->name);
        num("wind", p->wind_base);
        num("waveHeight", p->wave_height);
        num("wavePeriod", p->wave_period);
        num("visibility", p->visibility);
        num("skyTop", p->sky_top);
        num("skyHorizon", p->sky_horizon);
        num("cloudCover", p->cloud_cover);
        num("cloudShade", p->cloud_shade);
        num("cloudSize", p->cloud_size);
        num("cloudSpeed", p->cloud_speed);
        num("rain", p->rain);
        num("lightning", p->lightning_interval);
        num("sun", p->sun);
        obj_end();
    }
    arr_end();

    arr("tracks");
    for (int i = 0; i < app_ntracks(); i++) {
        const Track *t = &app_tracks()[i];
        obj(NULL);
        str("name", t->name);
        str("info", t->info);
        num("windFrom", t->wind_from);
        num("sx", t->start_x);
        num("sy", t->start_y);
        arr("marks");
        for (int k = 0; k < t->nmarks; k++) {
            obj(NULL);
            str("name", t->marks[k].name);
            num("x", t->marks[k].x);
            num("y", t->marks[k].y);
            obj_end();
        }
        arr_end();
        arr("route");
        for (int k = 0; k < t->nroute; k++) { num_v(t->route[k][0]); num_v(t->route[k][1]); }
        arr_end();
        obj_end();
    }
    arr_end();

    arr("help");
    for (int i = 0; i < app_nhelp(); i++) {
        const HelpLine *h = &app_help()[i];
        comma();
        raw("[");
        need_comma = 0;
        str_v(h->text);
        int_v(h->style);
        int_v(h->first);
        raw("]");
        need_comma = 1;
    }
    arr_end();

    obj("boat");
    str("name", assets_boat_names()[app_boat_index()]);
    num("length", b->hull.length);
    num("beam", b->hull.beam);
    integer("hulls", b->hull.hulls);
    num("spacing", b->hull.hull_spacing);
    num("draft", b->hull.draft);
    num("keel", -0.3);    /* render.c HULL_KEEL: a monohull's drawn keel line */
    num("tuck", 0.45);    /* render.c HULL_TUCK */
    num("clrDepth", b->hull.clr_depth);
    num("freeboard", b->look.freeboard);
    table("width", &b->look.hull_width);
    num("mastX", b->look.mast_x);
    num("mastHeight", b->look.mast_height);
    num("forestayX", b->look.forestay_x);
    num("shroudWidth", b->look.shroud_width);
    num("hullShade", b->look.hull_shade);
    num("cockpitX0", b->look.cockpit_x0);
    num("cockpitX1", b->look.cockpit_x1);
    num("cockpitW0", b->look.cockpit_w0);
    num("cockpitW1", b->look.cockpit_w1);
    num("cockpitDepth", b->look.cockpit_depth);
    num("rudderX", fminf(b->foils.rudder_x, -0.5f * b->hull.length));
    num("rudderMax", b->foils.rudder_max_deg * 0.017453f);
    num("rudderArea", b->foils.rudder_area);
    num("boardX", b->foils.board_x);
    num("boardArea", b->foils.board_area);
    integer("wheelStyle", b->look.wheel_style);
    arr("sails");
    for (int i = 0; i < b->sail_count; i++) {
        const SailCfg *s = &b->sails[i];
        obj(NULL);
        str("name", s->name);
        integer("shape", s->shape);
        num("shade", s->shade);
        num("tackX", s->tack_x);
        num("tackZ", s->tack_z);
        num("foot", s->foot);
        num("luff", s->luff);
        obj_end();
    }
    arr_end();
    obj_end();
    obj_end();
    return buf;
}
