#include "rocks.h"
#include "json.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define RADIUS_MAX 1000.0f
#define CELL_LIM 2.0e9          /* cell indices stay well inside int32 */
#define HEAVE_GAIN 0.5f         /* per metre of heave, on the effective impact speed */
#define DAMP_BUMP 0.7f
#define DAMP_STRIKE 0.4f
#define DAMP_WRECK 0.1f
#define STRIKE_KICK 0.3f        /* rad */

typedef struct { const char *key; size_t off; float mn, mx; } Fld;
#define F(f, lo, hi) { #f, offsetof(WorldCfg, f), lo, hi }

static const Fld FLDS[] = {
    F(rock_cell_size, 10, 500),
    F(rock_density, 0, 1),
    F(rock_r_min, 0.5f, 50),
    F(rock_r_max, 0.5f, 50),
    F(rock_bump_speed, 0.1f, 10),
    F(rock_wreck_speed, 0.2f, 20),
    F(start_clear_radius, 0, 2000),
};
#define NFLD ((int)(sizeof FLDS / sizeof FLDS[0]))

static const WorldCfg DEFAULTS = { 40.0f, 0.25f, 1.5f, 5.0f, 1.0f, 2.8f, 60.0f };

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
    if (c->rock_r_max > 0.5f * c->rock_cell_size) {
        wadd(buf, cap, n, any, "rock_r_max: %g exceeds half the cell size, clamped", c->rock_r_max);
        c->rock_r_max = 0.5f * c->rock_cell_size;
    }
    if (c->rock_r_min > c->rock_r_max) {
        wadd(buf, cap, n, any, "rock_r_min: %g exceeds rock_r_max, clamped", c->rock_r_min);
        c->rock_r_min = c->rock_r_max;
    }
    if (!(c->rock_wreck_speed > c->rock_bump_speed)) {
        wadd(buf, cap, n, any, "rock_wreck_speed: not above rock_bump_speed, raised");
        c->rock_wreck_speed = c->rock_bump_speed + 0.1f;
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
                 FLDS[i].key, v, FLDS[i].mn, FLDS[i].mx);
            v = clampf(v, FLDS[i].mn, FLDS[i].mx);
        }
        *dst = v;
    }
    json_free(doc);
    enforce(out, warn, warnlen, &wn, &any);
    return !any;
}

/* A WorldCfg built by hand gets the loader's limits; non-finite fields fall back to defaults. */
static WorldCfg sane(const WorldCfg *in) {
    WorldCfg c = *in;
    for (int i = 0; i < NFLD; i++) {
        float *p = (float *)((char *)&c + FLDS[i].off);
        float d = *(const float *)((const char *)&DEFAULTS + FLDS[i].off);
        *p = isfinite(*p) ? clampf(*p, FLDS[i].mn, FLDS[i].mx) : d;
    }
    size_t n = 0;
    bool any = false;
    enforce(&c, NULL, 0, &n, &any);
    return c;
}

enum { K_PRESENT = 1, K_RADIUS, K_OFFX, K_OFFY, K_AWASH };
static float cellf(int32_t cx, int32_t cy, uint32_t seed, int k) {
    return hashf(hash2(cx, cy, seed + (uint32_t)k * 0x9e3779b1u));
}

int rocks_near(const WorldCfg *cfg, uint32_t seed, float x, float y, float radius, Rock *out, int max) {
    if (!cfg || !out || max <= 0 || !isfinite(x) || !isfinite(y) || !isfinite(radius)) return 0;
    WorldCfg c = sane(cfg);
    if (max > ROCKS_MAX) max = ROCKS_MAX;
    radius = clampf(radius, 0.0f, RADIUS_MAX);
    if (!(c.rock_density > 0.0f)) return 0;

    double cell = c.rock_cell_size, reach = (double)radius + c.rock_r_max;
    double x0 = floor(((double)x - reach) / cell), x1 = floor(((double)x + reach) / cell);
    double y0 = floor(((double)y - reach) / cell), y1 = floor(((double)y + reach) / cell);
    x0 = fmax(x0, -CELL_LIM); x1 = fmin(x1, CELL_LIM);
    y0 = fmax(y0, -CELL_LIM); y1 = fmin(y1, CELL_LIM);

    /* Past the cap the nearest rocks win: a full list drops its farthest member for a closer one. */
    int n = 0;
    double edge[ROCKS_MAX];
    for (int64_t cx = (int64_t)x0; cx <= (int64_t)x1; cx++)
        for (int64_t cy = (int64_t)y0; cy <= (int64_t)y1; cy++) {
            int32_t ix = (int32_t)cx, iy = (int32_t)cy;
            if (!(cellf(ix, iy, seed, K_PRESENT) < c.rock_density)) continue;
            double r = c.rock_r_min + (double)cellf(ix, iy, seed, K_RADIUS) * (c.rock_r_max - c.rock_r_min);
            double rx = (double)ix * cell + r + (double)cellf(ix, iy, seed, K_OFFX) * (cell - 2 * r);
            double ry = (double)iy * cell + r + (double)cellf(ix, iy, seed, K_OFFY) * (cell - 2 * r);
            if (hypot(rx, ry) - r < c.start_clear_radius) continue;
            double e = hypot(rx - x, ry - y) - r;
            if (e > radius) continue;
            int slot = n;
            if (n == max) {
                slot = 0;
                for (int i = 1; i < n; i++) if (edge[i] > edge[slot]) slot = i;
                if (!(e < edge[slot])) continue;
            } else {
                n++;
            }
            edge[slot] = e;
            out[slot].x = (float)rx;
            out[slot].y = (float)ry;
            out[slot].r = (float)r;
            out[slot].awash = cellf(ix, iy, seed, K_AWASH) > 0.5f;
        }
    return n;
}

HitKind rocks_collide(const WorldCfg *cfg, const Rock *rocks, int n, BoatState *b,
                      const BoatCfg *boat, float heave) {
    if (!cfg || !rocks || n <= 0 || !b || !boat) return HIT_NONE;
    if (!isfinite(b->x) || !isfinite(b->y) || !isfinite(b->heading) ||
        !isfinite(b->u) || !isfinite(b->v)) return HIT_NONE;
    WorldCfg c = sane(cfg);
    float hv = isfinite(heave) ? fabsf(heave) : 0.0f;
    float half = 0.5f * fmaxf(boat->hull.beam, 0.2f);
    HitKind worst = HIT_NONE;

    for (int i = 0; i < n; i++) {
        const Rock *k = &rocks[i];
        if (!isfinite(k->x) || !isfinite(k->y) || !(k->r > 0.0f)) continue;
        double cr = (double)k->r + half;
        double dx = b->x - k->x, dy = b->y - k->y;
        double dist = hypot(dx, dy);
        if (!(dist < cr)) continue;
        double nx = 1.0, ny = 0.0;
        if (dist > 1e-9) { nx = dx / dist; ny = dy / dist; }

        double ch = cos(b->heading), sh = sin(b->heading);
        double vx = b->u * ch - b->v * sh, vy = b->u * sh + b->v * ch;
        double inward = -(vx * nx + vy * ny);
        b->x = k->x + nx * cr;
        b->y = k->y + ny * cr;
        if (inward <= 0.0) continue;                 /* already leaving, or sliding along */

        float eff = (float)inward * (1.0f + HEAVE_GAIN * hv);
        HitKind kind = eff < c.rock_bump_speed ? HIT_BUMP
                     : eff < c.rock_wreck_speed ? HIT_STRIKE : HIT_WRECK;
        float damp = kind == HIT_BUMP ? DAMP_BUMP : kind == HIT_STRIKE ? DAMP_STRIKE : DAMP_WRECK;
        vx = (vx + inward * nx) * damp;
        vy = (vy + inward * ny) * damp;
        if (kind == HIT_STRIKE) {
            double cross = ch * ny - sh * nx;
            b->heading = wrap_pi(b->heading + (cross >= 0.0 ? STRIKE_KICK : -STRIKE_KICK));
            ch = cos(b->heading); sh = sin(b->heading);
        }
        b->u = (float)(vx * ch + vy * sh);
        b->v = (float)(-vx * sh + vy * ch);
        if (kind > worst) worst = kind;
    }
    return worst;
}
