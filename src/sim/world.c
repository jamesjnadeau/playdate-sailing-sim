#include "world.h"
#include "json.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


typedef struct { const char *key; size_t off; float mn, mx; } Fld;
#define F(f, lo, hi) { #f, offsetof(WorldCfg, f), lo, hi }

static const Fld FLDS[] = {
    F(journey_min_m, 20, 5000),
    F(journey_max_m, 20, 5000),
    F(journey_shift_min_s, 5, 3600),
    F(journey_shift_max_s, 5, 3600),
    F(weather_blend_s, 1, 120),
};
#define NFLD ((int)(sizeof FLDS / sizeof FLDS[0]))

static const WorldCfg DEFAULTS = { 60.0f, 150.0f, 60.0f, 180.0f, 10.0f };

void world_default(WorldCfg *out) { *out = DEFAULTS; }

static void wadd(char *buf, size_t cap, size_t *n, bool *any, const char *fmt, ...) {
    *any = true;
    if (!buf || cap < 2) return;
    if (*n) {
        if (*n + 3 >= cap) return;
        buf[(*n)++] = ';';
        buf[(*n)++] = ' ';
    }
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(buf + *n, cap - *n, fmt, ap);
    va_end(ap);
    if (k < 0) { buf[*n] = 0; return; }
    *n += (size_t)k < cap - *n ? (size_t)k : cap - *n - 1;
}

/* Cross-field limits. Each fix raises a warning (buf may be NULL to fix silently). */
static void enforce(WorldCfg *c, char *buf, size_t cap, size_t *n, bool *any) {
    if (c->journey_max_m < c->journey_min_m) {
        wadd(buf, cap, n, any, "journey_max_m: %g below journey_min_m, raised", (double)c->journey_max_m);
        c->journey_max_m = c->journey_min_m;
    }
    if (c->journey_shift_max_s < c->journey_shift_min_s) {
        wadd(buf, cap, n, any, "journey_shift_max_s: %g below journey_shift_min_s, raised", (double)c->journey_shift_max_s);
        c->journey_shift_max_s = c->journey_shift_min_s;
    }
}

bool world_load(WorldCfg *out, const char *buf, size_t len, char *warn, size_t warnlen) {
    size_t wn = 0;
    bool any = false;
    if (warn && warnlen) warn[0] = 0;
    world_default(out);
    if (!buf || len == 0) { wadd(warn, warnlen, &wn, &any, "no world data"); return false; }
    char err[96];
    JsonDoc *doc = json_parse(buf, len, err, sizeof err);
    if (!doc) { wadd(warn, warnlen, &wn, &any, "parse error: %s", err); return false; }
    const JsonNode *root = json_root(doc);
    if (!json_is_object(root)) {
        wadd(warn, warnlen, &wn, &any, "root: expected object");
        json_free(doc);
        return false;
    }
    for (int i = 0; i < NFLD; i++) {
        const JsonNode *nd = json_get(root, FLDS[i].key);
        if (!nd) continue;
        float *dst = (float *)((char *)out + FLDS[i].off);
        if (!json_is_number(nd)) {
            wadd(warn, warnlen, &wn, &any, "%s: expected number", FLDS[i].key);
            continue;
        }
        float v = json_num(nd, *dst);
        if (!isfinite(v)) {
            wadd(warn, warnlen, &wn, &any, "%s: not a finite number", FLDS[i].key);
            continue;
        }
        if (v < FLDS[i].mn || v > FLDS[i].mx) {
            wadd(warn, warnlen, &wn, &any, "%s: %g outside [%g, %g], clamped",
                 FLDS[i].key, (double)v, (double)FLDS[i].mn, (double)FLDS[i].mx);
            v = clampf(v, FLDS[i].mn, FLDS[i].mx);
        }
        *dst = v;
    }
    json_free(doc);
    enforce(out, warn, warnlen, &wn, &any);
    return !any;
}
