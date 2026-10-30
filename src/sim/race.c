#include "race.h"
#include "json.h"
#include "mathx.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define POS_LIM 20000.0f      /* m; track positions are clamped to this */
#define MARK_NAME_LEN 8

static void set_mark(Mark *m, const char *name, float x, float y, float gate, int rounding) {
    memset(m, 0, sizeof *m);
    snprintf(m->name, sizeof m->name, "%s", name);
    m->x = x;
    m->y = y;
    m->gate = gate;
    m->rounding = rounding;
}

void track_default(Track *out) {
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", "Two Buoys");
    out->wind_from = deg2rad(45.0f);
    out->start_x = -60.0f;
    out->start_y = 30.0f;
    out->start_heading = deg2rad(-20.0f);
    out->nmarks = 2;
    set_mark(&out->marks[0], "B", 400.0f, 0.0f, 0.0f, ROUND_PORT);
    set_mark(&out->marks[1], "A", 0.0f, 0.0f, wrap_pi(deg2rad(180.0f)), ROUND_PORT);
}

/* ---- loading ---- */

typedef struct { char *buf; size_t cap, n; bool any; } Warn;

static void warnf(Warn *w, const char *fmt, ...) {
    w->any = true;
    if (!w->buf || w->cap < 2) return;
    if (w->n) {
        if (w->n + 3 >= w->cap) return;
        w->buf[w->n++] = ';';
        w->buf[w->n++] = ' ';
    }
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(w->buf + w->n, w->cap - w->n, fmt, ap);
    va_end(ap);
    if (k < 0) { w->buf[w->n] = 0; return; }
    w->n += (size_t)k < w->cap - w->n ? (size_t)k : w->cap - w->n - 1;
}

/* Reads a finite number; on failure warns and leaves *dst alone. */
static bool get_num(const JsonNode *obj, const char *key, float *dst, const char *where, Warn *w) {
    const JsonNode *nd = json_get(obj, key);
    if (!nd) { warnf(w, "%s.%s: missing", where, key); return false; }
    if (!json_is_number(nd) || !isfinite(json_num(nd, 0.0f))) {
        warnf(w, "%s.%s: expected a number", where, key);
        return false;
    }
    *dst = json_num(nd, 0.0f);
    return true;
}

static void get_pos(const JsonNode *obj, const char *key, float *dst, const char *where, Warn *w) {
    float v;
    if (!get_num(obj, key, &v, where, w)) return;
    if (v < -POS_LIM || v > POS_LIM) {
        warnf(w, "%s.%s: %g outside +/-%g, clamped", where, key, v, POS_LIM);
        v = clampf(v, -POS_LIM, POS_LIM);
    }
    *dst = v;
}

static void get_angle(const JsonNode *obj, const char *key, float *dst, const char *where, Warn *w) {
    float deg;
    if (get_num(obj, key, &deg, where, w)) *dst = wrap_pi(deg2rad(deg));
}

static void get_name(const JsonNode *obj, char *dst, size_t cap, const char *where, Warn *w) {
    const JsonNode *nd = json_get(obj, "name");
    const char *s = nd ? json_str(nd, NULL) : NULL;
    if (!s) { warnf(w, "%s.name: expected a string", where); return; }
    snprintf(dst, cap, "%s", s);
}

static void get_rounding(const JsonNode *obj, int *dst, const char *where, Warn *w) {
    const JsonNode *nd = json_get(obj, "rounding");
    const char *s = nd ? json_str(nd, NULL) : NULL;
    if (s && strcmp(s, "port") == 0) *dst = ROUND_PORT;
    else if (s && strcmp(s, "starboard") == 0) *dst = ROUND_STARBOARD;
    else {
        *dst = ROUND_PORT;
        warnf(w, "%s.rounding: expected \"port\" or \"starboard\", using port", where);
    }
}

bool track_load(Track *out, const char *buf, size_t len, char *warn, size_t warnlen) {
    Warn w = { warn, warnlen, 0, false };
    if (warn && warnlen) warn[0] = 0;
    track_default(out);
    if (!buf || len == 0) { warnf(&w, "no track data"); return false; }
    char err[96];
    JsonDoc *doc = json_parse(buf, len, err, sizeof err);
    if (!doc) { warnf(&w, "parse error: %s", err); return false; }
    const JsonNode *root = json_root(doc);
    if (!json_is_object(root)) {
        warnf(&w, "root: expected object");
        json_free(doc);
        return false;
    }

    get_name(root, out->name, sizeof out->name, "track", &w);
    get_angle(root, "wind_from_deg", &out->wind_from, "track", &w);

    const JsonNode *st = json_get(root, "start");
    if (!st || !json_is_object(st)) {
        warnf(&w, "start: expected an object, using the default start");
    } else {
        get_pos(st, "x", &out->start_x, "start", &w);
        get_pos(st, "y", &out->start_y, "start", &w);
        get_angle(st, "heading_deg", &out->start_heading, "start", &w);
    }

    /* marks are replaced only when the list gives at least one usable mark */
    const JsonNode *arr = json_get(root, "marks");
    if (!arr || !json_is_array(arr)) {
        warnf(&w, "marks: expected an array, using the default marks");
    } else if (json_array_len(arr) == 0) {
        warnf(&w, "marks: empty, using the default marks");
    } else {
        int n = json_array_len(arr);
        if (n > TRACK_MAX_MARKS) warnf(&w, "marks: %d given, only the first %d are used", n, TRACK_MAX_MARKS);
        Mark got[TRACK_MAX_MARKS];
        int k = 0;
        for (int i = 0; i < n && k < TRACK_MAX_MARKS; i++) {
            const JsonNode *o = json_array_at(arr, i);
            char where[24];
            snprintf(where, sizeof where, "mark %d", i);
            if (!json_is_object(o)) { warnf(&w, "%s: expected an object, skipped", where); continue; }
            Mark *m = &got[k];
            memset(m, 0, sizeof *m);
            m->rounding = ROUND_PORT;
            get_name(o, m->name, sizeof m->name, where, &w);
            get_pos(o, "x", &m->x, where, &w);
            get_pos(o, "y", &m->y, where, &w);
            get_angle(o, "gate_deg", &m->gate, where, &w);
            get_rounding(o, &m->rounding, where, &w);
            k++;
        }
        if (k == 0) {
            warnf(&w, "marks: none usable, using the default marks");
        } else {
            memcpy(out->marks, got, sizeof(Mark) * (size_t)k);
            out->nmarks = k;
        }
    }

    json_free(doc);
    return !w.any;
}

/* ---- gates and progress ---- */

static double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

bool gate_crossed(float mx, float my, float gate, int rounding,
                  double x0, double y0, double x1, double y1) {
    if (!isfinite(mx) || !isfinite(my) || !isfinite(gate) ||
        !isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)) return false;
    double dx = cos(gate), dy = sin(gate);
    double p0x = x0 - mx, p0y = y0 - my, p1x = x1 - mx, p1y = y1 - my;
    double s0 = cross2(dx, dy, p0x, p0y), s1 = cross2(dx, dy, p1x, p1y);
    bool side = rounding == ROUND_STARBOARD
              ? (s0 < 0.0 && s1 >= 0.0)
              : (s0 > 0.0 && s1 <= 0.0);
    if (!side) return false;
    double f = s0 / (s0 - s1);
    double qx = p0x + f * (p1x - p0x), qy = p0y + f * (p1y - p0y);
    return dx * qx + dy * qy >= 0.0;   /* on the ray, not its back extension */
}

void race_init(Race *r) {
    memset(r, 0, sizeof *r);
}

void race_mark_local(const Race *r, const Track *t, int i, double *x, double *y) {
    *x = t->marks[i].x - r->ox;
    *y = t->marks[i].y - r->oy;
}

bool race_update(Race *r, const Track *t, double x0, double y0, double x1, double y1, float dt) {
    if (!r->finished && isfinite(dt) && dt > 0.0f) r->t += dt;
    if (r->finished || r->next >= t->nmarks) return false;
    double mx, my;
    race_mark_local(r, t, r->next, &mx, &my);
    const Mark *m = &t->marks[r->next];
    if (!gate_crossed((float)mx, (float)my, m->gate, m->rounding, x0, y0, x1, y1)) return false;
    r->next++;
    if (r->next >= t->nmarks) r->finished = true;
    return true;
}

void race_shift(Race *r, double dx, double dy) {
    r->ox += dx;
    r->oy += dy;
}
