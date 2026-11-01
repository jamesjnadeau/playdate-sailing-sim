#include "render.h"
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "mathx.h"
#include "hullgeom.h"
#include "sail.h"
#include "wake.h"

#define LEVELS 17
#define SEA_BANDS 15
#define CLOUDS 24
#define STREAKS 24
#define PI_D 3.14159265358979323846

static int sea_style = SEA_TRI;   /* the sea tiling; change here (or call render_set_sea_style) to try another */
void render_set_sea_style(int style) { sea_style = style < 0 || style >= SEA_STYLES ? SEA_GRID : style; }
int render_sea_style(void) { return sea_style; }

static LCDPattern pats[LEVELS];
static int pats_ready;

static void init_pats(void) {
    for (int l = 0; l < LEVELS; l++) {
        for (int y = 0; y < 8; y++) {
            uint8_t row = 0;
            for (int x = 0; x < 8; x++) {
                int xx = x, yy = y, v = 0;
                for (int i = 0; i < 3; i++) {
                    v = (v << 2) | ((((xx ^ yy) & 1) << 1) | (yy & 1));
                    xx >>= 1;
                    yy >>= 1;
                }
                if (v >= l * 4) row |= (uint8_t)(0x80 >> x);   /* 1 = white */
            }
            pats[l][y] = row;
            pats[l][8 + y] = 0xFF;
        }
    }
    pats_ready = 1;
}

LCDColor render_pattern(int level) {
    if (!pats_ready) init_pats();
    if (level < 0) level = 0;
    if (level >= LEVELS - 1) level = LEVELS - 2;   /* level LEVELS-1 is solid black: never used as a dither shade */
    return (LCDColor)pats[level];
}

static LCDPattern foam_pats[LEVELS];
static int foam_ready;

/* The render_pattern dither with its mask set to the colour bits: only the white pixels draw, so foam
 * stipples over the sea instead of painting black dots into it. */
static LCDColor foam_pattern(int level) {
    if (!foam_ready) {
        render_pattern(0);                                   /* make sure pats[] is built */
        for (int l = 0; l < LEVELS; l++)
            for (int y = 0; y < 8; y++) {
                foam_pats[l][y] = pats[l][y];
                foam_pats[l][8 + y] = pats[l][y];
            }
        foam_ready = 1;
    }
    if (level < 0) level = 0;
    if (level >= LEVELS - 1) level = LEVELS - 2;
    return (LCDColor)foam_pats[level];
}

static void foam_poly4(PlaydateAPI *pd, const int p[8], int level) {
    pd->graphics->fillPolygon(4, (int *)p, foam_pattern(level), kPolygonFillNonZero);
}

/* A foam stroke between two screen points: a dithered band about `w` px wide instead of a white line. */
static void foam_stroke(PlaydateAPI *pd, int x0, int y0, int x1, int y1, float w, int level) {
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0), len = sqrtf(dx * dx + dy * dy);
    float nx = 0.0f, ny = w * 0.5f;
    if (len > 0.5f) { nx = -dy / len * w * 0.5f; ny = dx / len * w * 0.5f; }
    int p[8] = { (int)(x0 + nx), (int)(y0 + ny), (int)(x1 + nx), (int)(y1 + ny),
                 (int)(x1 - nx), (int)(y1 - ny), (int)(x0 - nx), (int)(y0 - ny) };
    foam_poly4(pd, p, level);
}

static uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
static float frand(uint32_t i, uint32_t salt) { return (float)(hash32(i * 2654435761u + salt) >> 8) / 16777216.0f; }

static float fvis(const Game *g) { return g->weather.preset[g->preset].visibility; }

/* ---- sky ---- */

static void draw_clouds(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    static const int shade[WEATHER_PRESETS] = { 8, 2, 5, 6, 9, 11, 13 };
    LCDColor col = render_pattern(shade[g->preset]);
    for (int i = 0; i < CLOUDS; i++) {
        double az = frand(i, 1) * 2.0 * PI_D + g->t * (0.002 + 0.004 * frand(i, 2));
        float z = 150.0f + 350.0f * frand(i, 3);
        float w = 250.0f + 400.0f * frand(i, 4);
        float sx, sy, d;
        if (!camera_project(c, c->x + 3000.0 * cos(az), c->y + 3000.0 * sin(az), z, &sx, &sy, &d)) continue;
        float pw = w * CAM_FOCAL / d, ph = pw * 0.22f;
        if (sx + pw < 0 || sx - pw > CAM_W) continue;
        pd->graphics->fillEllipse((int)(sx - pw / 2), (int)(sy - ph / 2), (int)pw, (int)ph, 0, 0, col);
        pd->graphics->fillEllipse((int)(sx - pw / 4), (int)(sy - ph * 0.85f), (int)(pw * 0.5f), (int)(ph * 0.9f), 0, 0, col);
    }
}

/* ---- sea ---- */

/* Screen line through the sea at ground distance d ahead of the camera (in the view direction):
   returns y at x=-20 and x=CAM_W+20, or false if it cannot be projected. */
static bool sea_line(const CameraPose *c, float d, float z, float *y0, float *y1) {
    double fx = cos(c->yaw), fy = sin(c->yaw);
    double px = c->x + fx * d, py = c->y + fy * d;
    double lx = -fy * d * 2.0, ly = fx * d * 2.0;
    float ax, ay, bx, by;
    if (!camera_project(c, px - lx, py - ly, z, &ax, &ay, NULL)) return false;
    if (!camera_project(c, px + lx, py + ly, z, &bx, &by, NULL)) return false;
    if (fabsf(bx - ax) < 1e-3f) return false;
    float slope = (by - ay) / (bx - ax);
    *y0 = ay + (-20.0f - ax) * slope;
    *y1 = ay + ((float)CAM_W + 20.0f - ax) * slope;
    return true;
}

static void strip(PlaydateAPI *pd, float fy0, float fy1, float ny0, float ny1, int level) {
    int pts[8] = { -20, (int)fy0, CAM_W + 20, (int)fy1, CAM_W + 20, (int)ny1, -20, (int)ny0 };
    pd->graphics->fillPolygon(4, pts, render_pattern(level), kPolygonFillNonZero);
}

static double sv_ox, sv_oy;                         /* world position the sea view is anchored at */
static SeaView sv;                                   /* per-frame sea evaluator, origin at the camera */
static int iround(float v) { return (int)lroundf(clampf(v, -2000.0f, 2000.0f)); }

/* Crest and trough streaks: short dashes scattered at jittered, world-fixed spots and laid
   along the dominant wave's crest line. A dash shows white while a crest is under it and black
   in a trough, so highlights flow past with the waves, with no regular grid and only a
   sea_height sample per candidate. */
#define FLECK_CELL 5.0f      /* m between candidate spots, before jitter */
#define FLECK_RADIUS 12      /* cells searched each way around the camera */

static uint32_t fleck_hash(int32_t a, int32_t b) {
    uint32_t h = (uint32_t)a * 0x9E3779B1u ^ (uint32_t)b * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

/* Dominant wave: unit travel direction (nx, ny) and wavelength in metres. */
static bool dominant_wave(const Sea *sea, float *nx, float *ny, float *wl) {
    if (sea->n == 0) return false;
    int dom = 0;
    for (int k = 1; k < sea->n; k++) if (sea->c[k].amp > sea->c[dom].amp) dom = k;
    float wm = (float)sea->c[dom].m, wn = (float)sea->c[dom].n;
    float wk = hypotf(wm, wn);
    if (wk < 1e-3f) return false;
    *nx = wm / wk; *ny = wn / wk;
    *wl = (float)SEA_WRAP / wk;
    return true;
}

__attribute__((unused)) static void draw_sea_flecks(PlaydateAPI *pd, const Game *g, const CameraPose *c, float t,
                            float half, float vis) {
    const Sea *sea = &g->sea;
    float wnx, wny, wl;
    if (!dominant_wave(sea, &wnx, &wny, &wl)) return;
    float cx = -wny, cy = wnx;                        /* along the crest line */
    double fx = cos(c->yaw), fy = sin(c->yaw);
    float maxd = fminf(vis * 0.8f, FLECK_CELL * FLECK_RADIUS);
    int ci = (int)floor(c->x / FLECK_CELL), cj = (int)floor(c->y / FLECK_CELL);

    for (int dj = -FLECK_RADIUS; dj <= FLECK_RADIUS; dj++) {
        for (int di = -FLECK_RADIUS; di <= FLECK_RADIUS; di++) {
            int32_t gi = ci + di, gj = cj + dj;
            uint32_t h = fleck_hash(gi, gj);
            if ((h & 0xFF) > 90) continue;            /* ~35% of cells get a streak */
            double wx = (gi + ((h >> 8 & 0xFF) / 256.0f)) * (double)FLECK_CELL;
            double wy = (gj + ((h >> 16 & 0xFF) / 256.0f)) * (double)FLECK_CELL;
            float fwd = (float)((wx - c->x) * fx + (wy - c->y) * fy);
            if (fwd < 1.0f || fwd > maxd) continue;
            float z = sea_view_height(&sv, (float)(wx - c->x), (float)(wy - c->y));
            float r = z / half;
            bool crest = r > 0.35f, trough = r < -0.55f;
            if (!crest && !trough) continue;
            float len = wl * (0.12f + 0.18f * (float)(h >> 24) / 255.0f) * (0.5f + fminf(fabsf(r), 1.0f));
            double ax = wx - cx * len * 0.5, ay = wy - cy * len * 0.5;
            double bx = wx + cx * len * 0.5, by = wy + cy * len * 0.5;
            float x0, y0, x1, y1;
            if (!camera_project(c, ax, ay, z, &x0, &y0, NULL)) continue;
            if (!camera_project(c, bx, by, z, &x1, &y1, NULL)) continue;
            if (x0 < -40 && x1 < -40) continue;
            if (x0 > CAM_W + 40 && x1 > CAM_W + 40) continue;
            int w = fwd < 14.0f ? 2 : 1;
            pd->graphics->drawLine(iround(x0), iround(y0), iround(x1), iround(y1), w,
                                   crest ? kColorWhite : kColorBlack);
        }
    }
}

/* Near the camera the sea is cut into bands parallel to the dominant wave's crest lines, fixed
   in the world, so the pattern slants and converges with the waves and turns as the camera
   orbits. Bands beyond the horizon haze are still lateral lines (they are too far to show it). */
#define CREST_BANDS_MAX 36
#define CREST_NEAR 3.0f
#define CREST_SPREAD 2.2f    /* lateral half-width of the ground drawn, per metre ahead */

typedef struct { float a, b; } Gp;                    /* ground point: metres ahead, metres right */
typedef struct { float depth; int n; int pts[2 * 8]; int level; } CrestBand;

/* Sutherland-Hodgman against sgn * (u - lo) >= 0, where u = a*na + b*nb. */
static int clip_u(const Gp *in, int n, Gp *out, float na, float nb, float lo, float sgn) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        Gp p = in[i], q = in[(i + 1) % n];
        float fp = sgn * (p.a * na + p.b * nb - lo), fq = sgn * (q.a * na + q.b * nb - lo);
        if (fp >= 0.0f) out[m++] = p;
        if ((fp >= 0.0f) != (fq >= 0.0f)) {
            float k = fp / (fp - fq);
            out[m].a = p.a + (q.a - p.a) * k;
            out[m].b = p.b + (q.b - p.b) * k;
            m++;
        }
    }
    return m;
}

/* Each band is cut again along the crest line into world-fixed cells, and every cell is shaded from
   the full multi-directional surface (height and slope toward the eye), so crossing swells show as
   a field of humps and hollows instead of parallel strips. */
#define MOGUL_CELLS_MAX 620
#define MOGUL_MAX_ACROSS 48

static int mogul_cmp(const void *x, const void *y) {
    float a = ((const CrestBand *)x)->depth, b = ((const CrestBand *)y)->depth;
    return (a < b) - (a > b);                          /* far to near */
}

/* ---- polygon tilings of the near sea (hex / hex+centre / tri) ----
   Cells live on a lattice fixed in the wave-aligned frame (u across the crests, v along them), so they
   slant and converge with the swell. Every corner is lifted to its own wave height, and a hexagon can
   be split into six facets around a centre sampled at its own height, so each facet tilts and shades
   on its own. */
#define LAT_MAX 225

typedef struct { const CameraPose *c; double fx, fy; float half, sgain, vis, dfar; int n; } Lat;
static Lat lat;
static CrestBand polys[LAT_MAX];

static float lat_h(Gp p) {
    return sea_view_height(&sv, (float)(p.a * lat.fx - p.b * lat.fy), (float)(p.a * lat.fy + p.b * lat.fx));
}

static int lat_level(float a, float slope, float hz) {
    float fog = clampf(a / lat.vis, 0.0f, 1.0f);
    float base = 10.0f - 3.0f * clampf(a / 300.0f, 0.0f, 1.0f);
    float shade = clampf(-slope * lat.sgain, -5.0f, 5.0f) * 1.6f + clampf(hz / lat.half, -1.0f, 1.0f) * -3.0f;
    return (int)((float)lroundf(base + shade) * (1.0f - 0.9f * fog * fog));
}

/* Clip a convex ground polygon to the band's depth range, lift it onto the waves and queue it. */
static void lat_emit(const Gp *g, int n, int level) {
    Gp p1[10], p2[10];
    if (lat.n >= LAT_MAX) return;
    int m = clip_u(g, n, p1, 1.0f, 0.0f, CREST_NEAR, 1.0f);
    if (m >= 3) m = clip_u(p1, m, p2, 1.0f, 0.0f, lat.dfar, -1.0f);
    if (m < 3 || m > 8) return;
    CrestBand *cb = &polys[lat.n];
    float ca = 0.0f;
    int xmin = 1 << 20, xmax = -(1 << 20), ymin = 1 << 20;
    for (int i = 0; i < m; i++) {
        float ox = (float)(p2[i].a * lat.fx - p2[i].b * lat.fy), oy = (float)(p2[i].a * lat.fy + p2[i].b * lat.fx);
        float sx, sy;
        if (!camera_project(lat.c, lat.c->x + ox, lat.c->y + oy, sea_view_height(&sv, ox, oy), &sx, &sy, NULL)) return;
        cb->pts[2 * i] = iround(sx);
        cb->pts[2 * i + 1] = iround(sy);
        if (cb->pts[2 * i] < xmin) xmin = cb->pts[2 * i];
        if (cb->pts[2 * i] > xmax) xmax = cb->pts[2 * i];
        if (cb->pts[2 * i + 1] < ymin) ymin = cb->pts[2 * i + 1];
        ca += p2[i].a;
    }
    if (xmax < 0 || xmin >= CAM_W || ymin >= CAM_H) return;
    cb->depth = ca / (float)m;
    cb->n = m;
    cb->level = level;
    lat.n++;
}

/* One flat facet: shade from the tilt of the plane through its three lifted corners. */
static void lat_tri(const Gp *p, const float *z) {
    float d1a = p[1].a - p[0].a, d1b = p[1].b - p[0].b, d2a = p[2].a - p[0].a, d2b = p[2].b - p[0].b;
    float det = d1a * d2b - d1b * d2a, slope = 0.0f;
    if (fabsf(det) > 1e-4f) slope = ((z[1] - z[0]) * d2b - (z[2] - z[0]) * d1b) / det;   /* dz/da */
    float ca = (p[0].a + p[1].a + p[2].a) / 3.0f;
    lat_emit(p, 3, lat_level(ca, slope, (z[0] + z[1] + z[2]) / 3.0f));
}

/* Whole hexagon: shade like a grid cell, from the surface at its centre. */
static void lat_hex(Gp c, const Gp *p) {
    Gp ahead = { c.a + 1.0f, c.b }, behind = { c.a - 1.0f, c.b };
    float slope = 0.5f * (lat_h(ahead) - lat_h(behind));
    lat_emit(p, 6, lat_level(c.a, slope, lat_h(c)));
}

static bool lat_inside(float a, float b, float r) {
    return a > CREST_NEAR - r && a < lat.dfar + r && fabsf(b) < CREST_SPREAD * (a + r) + r;
}

static void draw_lattice(PlaydateAPI *pd, const CameraPose *c, float dfar, float half, float sgain, float vis,
                         float na, float nb, float bw, double s, double s2,
                         float umin, float umax, float vmin, float vmax) {
    const float pa = -nb, pb = na;
    lat.c = c; lat.fx = cos(c->yaw); lat.fy = sin(c->yaw);
    lat.half = half; lat.sgain = sgain; lat.vis = vis; lat.dfar = dfar; lat.n = 0;

    /* keep the polygon count roughly constant: a facet covers k * size^2 of ground */
    float area = 1.1f * (dfar * dfar - CREST_NEAR * CREST_NEAR);
    float kfacet = sea_style == SEA_HEX ? 2.598f : 0.433f, budget = sea_style == SEA_HEX ? 200.0f : 195.0f;
    float size = bw * (sea_style == SEA_HEX ? 0.8f : sea_style == SEA_HEXC ? 1.7f : 1.6f);
    size = fmaxf(size, sqrtf(area / (budget * kfacet)));

    if (sea_style == SEA_TRI) {
        const float L = size, du = L * 0.8660254f;
        long j0 = (long)floor((s + umin) / du) - 1, j1 = (long)floor((s + umax) / du) + 1;
        for (long j = j0; j <= j1; j++) {
            long i0 = (long)floor((s2 + vmin) / L - j * 0.5) - 1, i1 = (long)floor((s2 + vmax) / L - j * 0.5) + 1;
            for (long i = i0; i <= i1 && lat.n < LAT_MAX; i++) {
                float cu[4], cv[4];                   /* P(i,j) P(i+1,j) P(i,j+1) P(i+1,j+1) */
                for (int q = 0; q < 4; q++) {
                    long ii = i + (q & 1), jj = j + (q >> 1);
                    cu[q] = (float)(jj * (double)du - s);
                    cv[q] = (float)((ii + jj * 0.5) * (double)L - s2);
                }
                Gp g[4];
                for (int q = 0; q < 4; q++) { g[q].a = cu[q] * na + cv[q] * pa; g[q].b = cu[q] * nb + cv[q] * pb; }
                if (!lat_inside((g[0].a + g[3].a) * 0.5f, (g[0].b + g[3].b) * 0.5f, L)) continue;
                float z[4];
                for (int q = 0; q < 4; q++) z[q] = lat_h(g[q]);
                Gp t0[3] = { g[0], g[1], g[2] }, t1[3] = { g[1], g[3], g[2] };
                float z0[3] = { z[0], z[1], z[2] }, z1[3] = { z[1], z[3], z[2] };
                lat_tri(t0, z0);
                lat_tri(t1, z1);
            }
        }
    } else {
        const float R = size, colw = 1.5f * R, rowh = 1.7320508f * R;
        Gp corner[6];                                 /* offsets from the centre */
        for (int k = 0; k < 6; k++) {
            float cu = R * cosf((float)k * 1.0471976f), cv = R * sinf((float)k * 1.0471976f);
            corner[k].a = cu * na + cv * pa; corner[k].b = cu * nb + cv * pb;
        }
        long c0 = (long)floor((s + umin) / colw) - 1, c1 = (long)floor((s + umax) / colw) + 1;
        for (long col = c0; col <= c1; col++) {
            double shift = (col & 1) ? 0.5 : 0.0;
            long r0 = (long)floor((s2 + vmin) / rowh - shift) - 1, r1 = (long)floor((s2 + vmax) / rowh - shift) + 1;
            for (long row = r0; row <= r1 && lat.n < LAT_MAX; row++) {
                float du = (float)(col * (double)colw - s), dv = (float)((row + shift) * (double)rowh - s2);
                Gp ctr = { du * na + dv * pa, du * nb + dv * pb };
                if (!lat_inside(ctr.a, ctr.b, R)) continue;
                Gp p[6];
                for (int k = 0; k < 6; k++) { p[k].a = ctr.a + corner[k].a; p[k].b = ctr.b + corner[k].b; }
                if (sea_style == SEA_HEX) { lat_hex(ctr, p); continue; }
                float zc = lat_h(ctr), z[6];
                for (int k = 0; k < 6; k++) z[k] = lat_h(p[k]);
                for (int k = 0; k < 6; k++) {
                    Gp tp[3] = { ctr, p[k], p[(k + 1) % 6] };
                    float tz[3] = { zc, z[k], z[(k + 1) % 6] };
                    lat_tri(tp, tz);
                }
            }
        }
    }
    qsort(polys, (size_t)lat.n, sizeof polys[0], mogul_cmp);
    for (int i = 0; i < lat.n; i++)
        pd->graphics->fillPolygon(polys[i].n, polys[i].pts, render_pattern(polys[i].level), kPolygonFillNonZero);
}

static void draw_crest_bands(PlaydateAPI *pd, const Game *g, const CameraPose *c, float t,
                             float dfar, float half, float sgain, float vis, float wnx, float wny,
                             float wl) {
    static CrestBand cells[MOGUL_CELLS_MAX];
    double fx = cos(c->yaw), fy = sin(c->yaw);
    float na = (float)(wnx * fx + wny * fy), nb = (float)(-wnx * fy + wny * fx);
    float pa = -nb, pb = na;                           /* along the crest line, camera frame */
    float bw = clampf(wl * 0.075f, 2.0f, 5.0f);
    float vw = bw * 1.4f;
    Gp trap[4] = { { CREST_NEAR, -CREST_SPREAD * CREST_NEAR }, { CREST_NEAR, CREST_SPREAD * CREST_NEAR },
                   { dfar, CREST_SPREAD * dfar }, { dfar, -CREST_SPREAD * dfar } };
    float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
    for (int i = 0; i < 4; i++) {
        float u = trap[i].a * na + trap[i].b * nb, v = trap[i].a * pa + trap[i].b * pb;
        if (u < umin) umin = u;
        if (u > umax) umax = u;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    double s = c->x * wnx + c->y * wny;               /* camera's position along the wave normal */
    double s2 = c->x * -wny + c->y * wnx;             /* ... and along the crest line */
    if (sea_style != SEA_GRID) {
        draw_lattice(pd, c, dfar, half, sgain, vis, na, nb, bw, s, s2, umin, umax, vmin, vmax);
        return;
    }
    int span = (int)ceilf((umax - umin) / bw) + 1;
    if (span > CREST_BANDS_MAX) { bw *= ceilf((float)span / CREST_BANDS_MAX); vw = bw * 1.4f; }
    long k0 = (long)floor((s + umin) / bw), k1 = (long)floor((s + umax) / bw);
    long j0 = (long)floor((s2 + vmin) / vw), j1 = (long)floor((s2 + vmax) / vw);
    if (j1 - j0 > MOGUL_MAX_ACROSS) { long mid = (j0 + j1) / 2; j0 = mid - MOGUL_MAX_ACROSS / 2; j1 = mid + MOGUL_MAX_ACROSS / 2; }
    int nc = 0;
    for (long k = k0; k <= k1; k++) {
        Gp p1[8], p2[8];
        float lo = (float)(k * (double)bw - s), hi = (float)((k + 1) * (double)bw - s);
        int nu = clip_u(trap, 4, p1, na, nb, lo, 1.0f);
        if (nu >= 3) nu = clip_u(p1, nu, p2, na, nb, hi, -1.0f);
        if (nu < 3) continue;
        for (long j = j0; j <= j1 && nc < MOGUL_CELLS_MAX; j++) {
            Gp q1[8], q2[8];
            float vlo = (float)(j * (double)vw - s2), vhi = (float)((j + 1) * (double)vw - s2);
            int n = clip_u(p2, nu, q1, pa, pb, vlo, 1.0f);
            if (n >= 3) n = clip_u(q1, n, q2, pa, pb, vhi, -1.0f);
            if (n < 3 || n > 8) continue;
            CrestBand *cb = &cells[nc];
            float ca = 0.0f, cbb = 0.0f;
            bool ok = true;
            int xmin = 1 << 20, xmax = -(1 << 20);
            for (int i = 0; i < n; i++) {
                ca += q2[i].a; cbb += q2[i].b;
                float ox = (float)(q2[i].a * fx - q2[i].b * fy), oy = (float)(q2[i].a * fy + q2[i].b * fx);
                double wx = c->x + ox, wy = c->y + oy;
                float z = sea_view_height(&sv, ox, oy);
                float sx, sy;
                if (!camera_project(c, wx, wy, z, &sx, &sy, NULL)) { ok = false; break; }
                cb->pts[2 * i] = iround(sx);
                cb->pts[2 * i + 1] = iround(sy);
                if (cb->pts[2 * i] < xmin) xmin = cb->pts[2 * i];
                if (cb->pts[2 * i] > xmax) xmax = cb->pts[2 * i];
            }
            if (!ok || xmax < 0 || xmin >= CAM_W) continue;
            ca /= (float)n; cbb /= (float)n;
            float mx = (float)(ca * fx - cbb * fy), my = (float)(ca * fy + cbb * fx);
            float hz = sea_view_height(&sv, mx, my);
            float hp = sea_view_height(&sv, mx + (float)fx, my + (float)fy);
            float hm = sea_view_height(&sv, mx - (float)fx, my - (float)fy);
            float slope = 0.5f * (hp - hm);           /* rise per metre away from the eye */
            float fog = clampf(ca / vis, 0.0f, 1.0f);
            float base = 10.0f - 3.0f * clampf(ca / 300.0f, 0.0f, 1.0f);
            float shade = clampf(-slope * sgain, -5.0f, 5.0f) * 1.6f + clampf(hz / half, -1.0f, 1.0f) * -3.0f;
            int level = (int)lroundf(base + shade + (((k + j) & 1) ? 1.0f : -1.0f));
            cb->level = (int)(level * (1.0f - 0.9f * fog * fog));
            cb->depth = ca;
            cb->n = n;
            nc++;
        }
    }
    qsort(cells, (size_t)nc, sizeof cells[0], mogul_cmp);
    for (int i = 0; i < nc; i++)
        pd->graphics->fillPolygon(cells[i].n, cells[i].pts, render_pattern(cells[i].level), kPolygonFillNonZero);
}

static void draw_sea(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    float vis = fvis(g);
    float t = (float)fmod(g->t, g->sea.period);
    const WeatherPreset *wp = &g->weather.preset[g->preset];
    /* scale the shading to this sea so even a light swell shows crests and troughs */
    float half = fmaxf(0.5f * wp->wave_height, 0.05f);
    float wl = 1.56f * wp->wave_period * wp->wave_period;
    float sgain = 5.0f / fmaxf((float)PI_D * wp->wave_height / wl, 0.03f);
    float wnx, wny, wlen;
    bool crest = dominant_wave(&g->sea, &wnx, &wny, &wlen);
    sv_ox = c->x; sv_oy = c->y;
    sea_view_init(&sv, &g->sea, c->x, c->y, t);
    float fy0, fy1;
    if (!sea_line(c, 20000.0f, 0.0f, &fy0, &fy1)) { fy0 = fy1 = CAM_HORIZON_Y; }
    float dist[SEA_BANDS];
    for (int i = 0; i < SEA_BANDS; i++) dist[SEA_BANDS - 1 - i] = 3.0f * powf(1.5f, (float)i);
    /* far -> near; dist[0] is the farthest band. Lateral bands run out to the first one inside
       the crest-band range, which then takes over. */
    float cap = fminf(vis * 0.9f, 60.0f);
    int last = SEA_BANDS - 1;
    if (crest) { last = 0; while (last < SEA_BANDS - 1 && dist[last] > cap) last++; }
    for (int i = 0; i <= last; i++) {
        float d = dist[i];
        double fx = cos(c->yaw), fy = sin(c->yaw);
        float hz = sea_view_height(&sv, (float)(fx * d), (float)(fy * d));
        float h2 = sea_view_height(&sv, (float)(fx * (d + 2)), (float)(fy * (d + 2)));
        float slope = (h2 - hz) / 2.0f;
        float y0, y1;
        if (!sea_line(c, d, hz, &y0, &y1)) continue;
        float fog = clampf(d / vis, 0.0f, 1.0f);
        float base = 10.0f - 3.0f * (d / 300.0f > 1 ? 1 : d / 300.0f);
        /* faces tipped toward the eye are light, faces tipped away dark; crests light, troughs dark */
        float shade = clampf(-slope * sgain, -5.0f, 5.0f) * 0.7f + clampf(hz / half, -1.0f, 1.0f) * -3.0f;
        int level = (int)lroundf(base + shade + ((i & 1) ? 1.0f : -1.0f));
        level = (int)(level * (1.0f - 0.9f * fog * fog));
        strip(pd, fy0, fy1, y0, y1, level);
        fy0 = y0; fy1 = y1;
    }
    strip(pd, fy0, fy1, CAM_H + 40, CAM_H + 40, 10);
    if (crest && dist[last] > CREST_NEAR + 0.5f)
        draw_crest_bands(pd, g, c, t, dist[last], half, sgain, vis, wnx, wny, wlen);
    /* draw_sea_flecks(pd, g, c, t, half, vis); */
}

static void draw_streaks(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    if (g->wind.speed < 2.0f) return;
    float wx, wy;
    wind_vec(&g->wind, &wx, &wy);
    float sp = hypotf(wx, wy);
    if (sp < 1e-3f) return;
    float ux = wx / sp, uy = wy / sp;
    float vis = fvis(g);
    for (int i = 0; i < STREAKS; i++) {
        double qx = frand(i, 11) * 120.0 + g->t * wx * 0.6;
        double qy = frand(i, 12) * 120.0 + g->t * wy * 0.6;
        qx = g->b.x + fmod(qx - g->b.x + 60.0, 120.0) - 60.0;
        qy = g->b.y + fmod(qy - g->b.y + 60.0, 120.0) - 60.0;
        float ax, ay, bx, by, da;
        if (!camera_project(c, qx, qy, 0.0f, &ax, &ay, &da)) continue;
        if (!camera_project(c, qx + ux * 6.0, qy + uy * 6.0, 0.0f, &bx, &by, NULL)) continue;
        if (da > vis * 0.8f || da < 1.0f) continue;
        if (fabsf(bx - ax) > 200 || fabsf(by - ay) > 200) continue;
        pd->graphics->drawLine((int)ax, (int)ay, (int)bx, (int)by, 1, kColorWhite);
    }
}

/* ---- rocks ---- */

__attribute__((unused)) static void draw_rocks(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    float vis = fvis(g);
    int order[ROCKS_MAX];
    float depth[ROCKS_MAX];
    int n = 0;
    for (int i = 0; i < g->nrocks; i++) {
        float sx, sy, d;
        if (!camera_project(c, g->rocks[i].x, g->rocks[i].y, 0.0f, &sx, &sy, &d)) continue;
        depth[i] = d;
        int j = n++;
        while (j > 0 && depth[order[j - 1]] < d) { order[j] = order[j - 1]; j--; }
        order[j] = i;                       /* far to near */
    }
    for (int k = 0; k < n; k++) {
        const Rock *r = &g->rocks[order[k]];
        float d = depth[order[k]];
        if (d > vis) continue;
        float h = sea_view_height(&sv, (float)(r->x - sv_ox), (float)(r->y - sv_oy));
        float bx, by, tx, ty;
        float top = r->awash ? 0.15f * r->r : 0.7f * r->r;
        if (!camera_project(c, r->x, r->y, h, &bx, &by, NULL)) continue;
        if (!camera_project(c, r->x, r->y, h + top, &tx, &ty, NULL)) continue;
        float hw = r->r * CAM_FOCAL / d;
        if (bx + hw < 0 || bx - hw > CAM_W) continue;
        float rh = fmaxf(by - ty, 1.0f);
        int fog = (int)(16.0f * clampf((d / vis - 0.5f) * 2.0f, 0.0f, 1.0f));
        LCDColor body = fog > 0 ? render_pattern(16 - fog) : kColorBlack;
        if (!r->awash) {
            int pts[2 * 9];
            for (int s = 0; s < 9; s++) {
                float a = PI_D * (float)s / 8.0f;
                pts[2 * s] = (int)(bx - hw * cosf(a));
                pts[2 * s + 1] = (int)(by - rh * sinf(a));
            }
            pd->graphics->fillPolygon(9, pts, body, kPolygonFillNonZero);
        }
        /* surf: a breaking skirt, bigger and busier when the rock is awash */
        int rings = r->awash ? 3 : 1;
        for (int q = 0; q < rings; q++) {
            float w = hw * (1.0f + 0.25f * (float)q) + 2.0f;
            float jit = sinf(g->t * 4.0f + q * 2.0f + r->x) * 2.0f;
            int ww = (int)(2 * w), hh = (int)fmaxf(rh * 0.5f, 3.0f) + q * 2;
            pd->graphics->drawEllipse((int)(bx - w), (int)(by - hh / 2 + jit), ww, hh, 1, 0, 0, kColorWhite);
        }
        if (r->awash)
            pd->graphics->fillEllipse((int)(bx - hw), (int)(by - rh), (int)(2 * hw), (int)fmaxf(rh, 2.0f), 0, 0,
                                      render_pattern(fog > 0 ? 0 : 3));
    }
}

/* ---- spray ---- */

static void draw_spray(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    const BoatState *b = &g->b;
    float sp = fabsf(b->u);
    if (sp < 1.2f) return;
    int n = (int)clampf(3.0f + sp * 2.0f + fabsf(g->wf.pitch) * 60.0f, 0.0f, 20.0f);
    uint32_t frame = (uint32_t)(g->t * 20.0);
    double ch = cos(b->heading), sh = sin(b->heading);
    for (int i = 0; i < n; i++) {
        float side = (i & 1) ? 1.0f : -1.0f;
        float life = frand(frame + i, 21);
        float fwd = g->boat.hull.length * 0.5f - life * 1.0f;
        float lat = side * (g->boat.hull.beam * 0.3f + life * 0.8f * frand(frame + i, 22));
        float z = 0.15f + life * 0.6f * frand(frame + i, 23);
        double wx = b->x + ch * fwd - sh * lat, wy = b->y + sh * fwd + ch * lat;
        float sx, sy;
        if (!camera_project(c, wx, wy, z + b->heave, &sx, &sy, NULL)) continue;
        pd->graphics->fillRect((int)sx, (int)sy, 2, 2, kColorWhite);
    }
}

/* ---- hull / water interaction ---- */

typedef struct { const PlaydateAPI *pd; const Game *g; const CameraPose *c; } Ctx;

static const float HULL_KEEL = -0.3f, HULL_TUCK = 0.45f;   /* topsides tuck to 45% of the gunwale width at the keel line */

/* heading-frame point (fwd, stbd, up from the mean waterline; heel already applied) -> screen */
static bool rel_pt(const Ctx *k, float f, float l, float z, float *sx, float *sy) {
    const BoatState *b = &k->g->b;
    double cd = cos(b->heading), sd = sin(b->heading);
    return camera_project(k->c, b->x + f * cd - l * sd, b->y + f * sd + l * cd, z + b->heave, sx, sy, NULL);
}

/* boat-frame (x fwd, y stbd, z up from waterline) -> heading frame; heel is applied in full */
static void heel_xf(const BoatState *b, float y, float z, float *l, float *up) {
    float ch = cosf(b->heel), sh = sinf(b->heel);
    *l = y * ch + z * sh; *up = z * ch - y * sh;
}

/* boat-frame (x fwd, y stbd, z up from waterline) -> screen; heel is applied in full */
static bool boat_pt(const Ctx *k, float x, float y, float z, float *sx, float *sy) {
    float l, up;
    heel_xf(&k->g->b, y, z, &l, &up);
    return rel_pt(k, x, l, up, sx, sy);
}

static float rel_surface(const Game *g, float fwd, float lat);

/* Where the sea meets the hull side at station u (0 stem .. 1 transom): the section's point on the
 * side's tuck line whose heeled height equals the local sea surface. wscale fattens the section
 * (the foam ring rides just outboard). False when this side is clear of the water at that station. */
static bool side_wl(const Game *g, int sgn, float u, float wscale, float *x, float *y, float *z) {
    const BoatState *b = &g->b;
    const LookCfg *lk = &g->boat.look;
    float w = 0.5f * g->boat.hull.beam * table_eval(lk->hull_width.p, lk->hull_width.n, u) * wscale;
    *x = g->boat.hull.length * (0.5f - u);
    float surf = rel_surface(g, *x, sgn * w);
    for (int it = 0; it < 3; it++) {      /* the surface depends on where the cut lands, which depends on the surface */
        *z = hull_side_z(w, sgn, lk->freeboard, HULL_KEEL, HULL_TUCK, b->heel, surf, y);
        float l, up;
        heel_xf(b, *y, *z, &l, &up);
        surf = rel_surface(g, *x, l);
    }
    return *z > HULL_KEEL + 1e-3f;
}

/* water-plane point from boat-frame (fwd, stbd) metres, ignoring heel: the sea surface stays level */
static bool water_pt(const Game *g, const CameraPose *c, float fwd, float lat, float *sx, float *sy) {
    const BoatState *b = &g->b;
    double ch = cos(b->heading), sh = sin(b->heading);
    return camera_project(c, b->x + ch * fwd - sh * lat, b->y + sh * fwd + ch * lat, 0.0f, sx, sy, NULL);
}

/* Which hull side is hidden from the eye (+1 starboard, -1 port, 0 when both show). The sides tilt with heel. */
static int far_side(const Game *g, const CameraPose *c) {
    const BoatState *b = &g->b;
    double ch = cos(b->heading), sh = sin(b->heading);
    float lat = (float)(-sh * (c->x - b->x) + ch * (c->y - b->y));   /* eye offset to starboard */
    return hull_hidden_side(b->heel, lat, c->height - b->heave, g->boat.hull.beam);
}

/* does the deck face the eye, or is the eye looking at the underside with the rig beyond the hull? */
static bool deck_faces_eye(const Game *g, const CameraPose *c) {
    const BoatState *b = &g->b;
    double ch = cos(b->heading), sh = sin(b->heading);
    float lat = (float)(-sh * (c->x - b->x) + ch * (c->y - b->y));
    return hull_deck_faces_eye(b->heel, lat, c->height - b->heave);
}

/* Foam where the hull meets the water and a wake that widens and fades astern. The hull foam rides
 * with the boat; the bow wave is not drawn as a separate sea-level line, which hung in place as the hull heaved. Strength follows speed; heel lifts one rail and buries the other.
 * All of it is a masked dither, so it reads as sea foam rather than a white line. */
static void draw_waterline(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    const BoatState *b = &g->b;
    float L = g->boat.hull.length, hb = 0.5f * g->boat.hull.beam;
    float sp = fabsf(b->u);
    float k = clampf(sp / 4.0f, 0.0f, 1.0f);
    enum { N = 9 };

    /* waterline ring: a band hugging the heeled hull where it meets the sea, denser on the buried (leeward) side */
    Ctx hk = { pd, g, c };
    float dip = sinf(b->heel) * 0.5f;                       /* + heel buries the starboard rail */
    int hidden = far_side(g, c);                             /* its ring is behind the hull */
    static float bury_sm[2];
    static double last_t = -1e9;
    for (int side = -1; side <= 1; side += 2) {
        if (side == hidden) {
            bury_sm[side > 0] = clampf(0.5f + side * dip * 4.0f, 0.0f, 1.0f);
            continue;
        }
        float target = clampf(0.5f + side * dip * 4.0f, 0.0f, 1.0f);
        float *sm = &bury_sm[side > 0];                       /* low-passed so the dither density doesn't flash with every wave */
        *sm = (g->t < last_t || g->t - last_t > 1.0) ? target : *sm + (target - *sm) * clampf(6.0f * (float)(g->t - last_t), 0.0f, 1.0f);
        float bury = *sm;
        int level = 1 + (int)((1.0f - bury) * 3.0f);
        float outer = 1.08f + 0.2f * bury + 0.1f * k;
        bool have = false;
        float ix = 0, iy = 0, ox = 0, oy = 0;
        for (int i = 0; i < N; i++) {
            float x, y, z, x2, y2, z2, nix, niy, nox, noy;
            float u = (float)i / (N - 1);
            if (!side_wl(g, side, u, 1.0f, &x, &y, &z) || !boat_pt(&hk, x, y, z, &nix, &niy) ||
                !side_wl(g, side, u, outer, &x2, &y2, &z2) || !boat_pt(&hk, x2, y2, z2, &nox, &noy)) { have = false; continue; }
            if (have) {
                int p[8] = { (int)ix, (int)iy, (int)ox, (int)oy, (int)nox, (int)noy, (int)nix, (int)niy };
                foam_poly4(pd, p, level);
            }
            ix = nix; iy = niy; ox = nox; oy = noy; have = true;
        }
    }
    last_t = g->t;
    float x0, y0, z0, x1, y1, z1, ax, ay, bx, by, cx, cy, dx, dy;   /* foam across the transom, spilling aft */
    if (hidden == 0 && side_wl(g, -1, 1.0f, 1.0f, &x0, &y0, &z0) && side_wl(g, 1, 1.0f, 1.0f, &x1, &y1, &z1) &&
        boat_pt(&hk, x0, y0, z0, &ax, &ay) && boat_pt(&hk, x1, y1, z1, &bx, &by) &&
        boat_pt(&hk, x1 - 0.3f, y1, z1, &cx, &cy) && boat_pt(&hk, x0 - 0.3f, y0, z0, &dx, &dy)) {
        int p[8] = { (int)ax, (int)ay, (int)bx, (int)by, (int)cx, (int)cy, (int)dx, (int)dy };
        foam_poly4(pd, p, 2);
    }

    if (sp < 0.5f) return;

    /* wake: ripples spaced astern that spread and thin out, shifting back with time. Past wake_fade_m
     * each ripple splits in the middle into two lines whose ends shrink away toward the hull. */
    int rings = 6;
    float phase = fmodf((float)g->t * (0.4f + 0.3f * k), 1.0f);
    float fade = wake_fade_start(g->boat.look.wake_fade_m);
    float depth = 0.25f + 0.35f * k;
    for (int i = 0; i < rings; i++) {
        float f = ((float)i + phase) / rings;                /* 0 at the transom .. 1 far astern */
        float back = -0.5f * L - f * wake_length(k);
        float half = hb * (0.6f + f * (1.4f + 1.5f * k));
        if (frand(i + (uint32_t)(g->t * 3.0), 41) < f * 0.6f) continue;   /* breaks up with distance */
        float lo, hi;
        if (!wake_segment(f, fade, half, &lo, &hi)) continue;
        int level = 2 + (int)(f * 10.0f);
        if (level > 14) level = 14;
        for (int s = -1; s <= 1; s += 2) {
            float ex, ey, fx, fy, gx, gy, hx, hy;
            if (!water_pt(g, c, back, s * lo, &ex, &ey) || !water_pt(g, c, back, s * hi, &fx, &fy) ||
                !water_pt(g, c, back - depth, s * hi, &gx, &gy) || !water_pt(g, c, back - depth, s * lo, &hx, &hy)) continue;
            int p[8] = { (int)ex, (int)ey, (int)fx, (int)fy, (int)gx, (int)gy, (int)hx, (int)hy };
            foam_poly4(pd, p, level);
        }
    }
}

/* ---- rig ---- */

static void draw_sail(PlaydateAPI *pd, const Ctx *k, int i) {
    const BoatCfg *cfg = &k->g->boat;
    const SailCfg *s = &cfg->sails[i];
    const BoatState *b = &k->g->b;
    float hoist = b->hoists[i];
    if (hoist < 0.02f) return;
    float side = b->swing[i];                                      /* eased across when the wind changes sides */
    float fb = cfg->look.freeboard;
    float tx = s->tack_x, tz = fb + s->tack_z;
    float hz = tz + s->luff * hoist;
    /* a headsail's luff rides the forestay: the head sits that far up the line from the tack to the masthead */
    float stay = clampf((hz - tz) / fmaxf(cfg->look.mast_height - tz, 0.1f), 0.0f, 1.0f);
    float hx = s->shape == 0 ? tx : tx + (cfg->look.mast_x - tx) * stay;
    float tr = b->trims[i];
    float foot = s->foot * hoist;                                  /* reefing shrinks the foot with the luff */
    float cx = tx - foot * cosf(tr), cy = side * foot * sinf(tr);
    float lufft = b->luff[i];
    int mw = s->mesh_w < 1 ? 1 : s->mesh_w > 12 ? 12 : s->mesh_w;
    int mh = s->mesh_h < 1 ? 1 : s->mesh_h > 16 ? 16 : s->mesh_h;
    float px[17][13], py[17][13];
    HgPt wp[17][13];                                      /* heading-frame position of each mesh point */
    float dw[17][13];                                     /* height above the sea: < 0 is under water */
    bool ok[17][13];
    float lo = 1e9f;
    for (int a = 0; a <= mh; a++) {
        float u = (float)a / (float)mh;
        float lx = tx + (hx - tx) * u, lz = tz + (hz - tz) * u;
        float ex = cx + (hx - cx) * u, ey = cy * (1.0f - u), ez = tz + (hz - tz) * u;
        for (int q = 0; q <= mw; q++) {
            float v = (float)q / (float)mw;
            float chord = hypotf(ex - lx, ey);
            float bulge = s->camber * chord * 4.0f * v * (1.0f - v);
            float flut = lufft > 0.1f ? sinf(k->g->t * s->flutter_hz * 6.2832f + u * 3.0f + v * 5.0f) * 0.1f * lufft * chord * v : 0.0f;
            float x = lx + (ex - lx) * v;
            float y = ey * v + side * bulge + flut;
            if (s->shape == 2) {                          /* a spinnaker balloons out ahead of its chord */
                float bx, by;
                sail_billow(s, lufft, ex - lx, ey, u, v, b->awa, &bx, &by);
                x += bx;
                y = ey * v + by + flut;
            }
            if (s->shape == 3) {                          /* a square sail: a yard swung about the mast, full foot width */
                float off = (v - 0.5f) * s->foot;
                float sq_bulge = s->camber * s->foot * 4.0f * v * (1.0f - v) * sinf(3.14159265f * u) * (1.0f - lufft);
                float sq_flut = lufft > 0.1f ? sinf((float)k->g->t * s->flutter_hz * 6.2832f + u * 3.0f + v * 5.0f) * 0.1f * lufft * s->foot * v : 0.0f;
                /* braced like a boom: the leeward yardarm goes aft, the belly fills downwind */
                x = cfg->look.mast_x - off * cosf(tr) * side + sq_bulge * sinf(tr);
                y = off * sinf(tr) + sq_bulge * side * cosf(tr) + sq_flut;
            }
            float z = lz + (ez - lz) * v;
            float l, up;
            heel_xf(b, y, z, &l, &up);
            wp[a][q] = (HgPt){ x, l, up };
            if (up < lo) lo = up;
            ok[a][q] = rel_pt(k, x, l, up, &px[a][q], &py[a][q]);
        }
    }
    /* a heeled rig dips into the sea: whatever is below the surface is not drawn */
    for (int a = 0; a <= mh; a++)
        for (int q = 0; q <= mw; q++)
            dw[a][q] = lo > 1.0f ? 1.0f : wp[a][q].z - rel_surface(k->g, wp[a][q].f, wp[a][q].l);

    /* outline: luff up, head across, leech down, foot back */
    HgPt op[2 * 17 + 2]; float od[2 * 17 + 2]; int on0 = 0;
    for (int a = 0; a <= mh; a++) { op[on0] = wp[a][0]; od[on0++] = dw[a][0]; }
    for (int a = mh; a >= 0; a--) { op[on0] = wp[a][mw]; od[on0++] = dw[a][mw]; }
    HgPt cl[2 * (2 * 17 + 2)]; bool cut[2 * (2 * 17 + 2)];
    int cn = hull_clip_above(op, od, on0, cl, cut, 2 * (2 * 17 + 2));
    int outline[2 * 2 * (2 * 17 + 2)], on = 0;
    bool foam[2 * (2 * 17 + 2)];
    for (int i = 0; i < cn; i++) {
        float sx, sy;
        if (!rel_pt(k, cl[i].f, cl[i].l, cl[i].z, &sx, &sy)) continue;
        outline[2 * on] = (int)sx; outline[2 * on + 1] = (int)sy; foam[on] = cut[i]; on++;
    }
    int shade = (int)clampf(s->shade * 16.0f, 0.0f, 16.0f);
    if (on >= 3) pd->graphics->fillPolygon(on, outline, render_pattern(16 - shade), kPolygonFillNonZero);
    for (int a = 0; a <= mh; a++)
        for (int q = 0; q < mw; q++) {
            if (!(a == 0 || a == mh || (a & 1) == 0)) continue;
            float d0 = dw[a][q], d1 = dw[a][q + 1];
            if (!ok[a][q] || !ok[a][q + 1] || (d0 < 0.0f && d1 < 0.0f)) continue;
            float x0 = px[a][q], y0 = py[a][q], x1 = px[a][q + 1], y1 = py[a][q + 1];
            if (d0 < 0.0f || d1 < 0.0f) {                    /* one end under: stop the seam at the surface */
                float t = d0 / (d0 - d1);
                const HgPt *p0 = &wp[a][q], *p1 = &wp[a][q + 1];
                float cx2, cy2;
                if (!rel_pt(k, p0->f + (p1->f - p0->f) * t, p0->l + (p1->l - p0->l) * t, p0->z + (p1->z - p0->z) * t, &cx2, &cy2)) continue;
                if (d0 < 0.0f) { x0 = cx2; y0 = cy2; } else { x1 = cx2; y1 = cy2; }
            }
            int seam_w = (s->shape == 3 && a == mh) ? 3 : 1;   /* a square sail's head is its heavy yard */
            pd->graphics->drawLine((int)x0, (int)y0, (int)x1, (int)y1, seam_w, kColorBlack);
        }
    for (int p = 0; p < on; p++) {
        int n2 = (p + 1) % on;
        if (foam[p] && foam[n2]) continue;                   /* the cut along the sea: no sail edge there */
        pd->graphics->drawLine(outline[2 * p], outline[2 * p + 1], outline[2 * n2], outline[2 * n2 + 1], 1, kColorBlack);
    }

    /* telltales are disabled: they were too small to read.
     * They sat near the luff, streaming along the sail when it flows and whipping about the wind when it luffs.
    float wdx = -cosf(b->awa), wdy = -sinf(b->awa);
    float lm = clampf(lufft * 1.5f, 0.0f, 1.0f);
    for (int t = 0; t < 3; t++) {
        float u = 0.25f + 0.25f * (float)t;
        float lx = tx + (hx - tx) * u, lz = tz + (hz - tz) * u;
        float ex = cx + (hx - cx) * u, ey = cy * (1.0f - u);
        float cdx = ex - lx, cdy = ey, cl = hypotf(cdx, cdy);
        if (cl < 1e-3f) continue;
        float dx = cdx / cl * (1.0f - lm) + wdx * lm, dy = cdy / cl * (1.0f - lm) + wdy * lm;
        float ax = lx + cdx * 0.12f, ay = cdy * 0.12f + side * s->camber * cl * 0.12f * 0.88f * 4.0f, az = lz;
        float sx0, sy0;
        if (!boat_pt(k, ax, ay, az, &sx0, &sy0)) continue;
        float ppx = sx0, ppy = sy0;
        for (int seg = 1; seg <= 2; seg++) {
            float ph = (float)k->g->t * (6.0f + 4.0f * lm) - (float)seg * 1.5f + (float)t * 2.0f;
            float fl = sinf(ph) * (0.03f + 0.12f * lm) * (float)seg;
            float ex2, ey2;
            if (!boat_pt(k, ax + (dx * 0.15f * (float)seg) - dy * fl, ay + dy * 0.15f * (float)seg + dx * fl, az - 0.02f * (float)seg, &ex2, &ey2)) break;
            pd->graphics->drawLine((int)ppx, (int)ppy, (int)ex2, (int)ey2, 1, kColorBlack);
            ppx = ex2; ppy = ey2;
        }
    }
    */
}

static void draw_rig(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    Ctx k = { pd, g, c };
    const LookCfg *lk = &g->boat.look;
    float sx0, sy0, sx1, sy1, sx2, sy2;
    /* sails farthest first */
    int order[BOAT_MAX_SAILS];
    int n = g->boat.sail_count;
    for (int i = 0; i < n; i++) {
        int j = i;
        while (j > 0 && g->boat.sails[order[j - 1]].ce_x < g->boat.sails[i].ce_x) { order[j] = order[j - 1]; j--; }
        order[j] = i;
    }
    for (int i = 0; i < n; i++) draw_sail(pd, &k, order[i]);
    if (boat_pt(&k, lk->mast_x, 0, lk->freeboard, &sx0, &sy0) &&
        boat_pt(&k, lk->mast_x, 0, lk->mast_height, &sx1, &sy1)) {
        pd->graphics->drawLine((int)sx0, (int)sy0, (int)sx1, (int)sy1, 2, kColorBlack);
        if (boat_pt(&k, lk->forestay_x, 0, lk->freeboard, &sx2, &sy2))
            pd->graphics->drawLine((int)sx1, (int)sy1, (int)sx2, (int)sy2, 1, kColorBlack);
        for (int sd = -1; sd <= 1; sd += 2)
            if (boat_pt(&k, lk->mast_x, sd * lk->shroud_width, lk->freeboard, &sx2, &sy2))
                pd->graphics->drawLine((int)sx1, (int)sy1, (int)sx2, (int)sy2, 1, kColorBlack);
        /* masthead ribbon streams away from the apparent wind (awa: 0 = from the bow, + = from starboard) */
        const BoatState *bs = &g->b;
        float wx = -cosf(bs->awa), wy = -sinf(bs->awa);
        float len = 0.5f + 0.1f * fminf(bs->aws, 12.0f);
        float px = sx1, py = sy1;
        for (int seg = 1; seg <= 4; seg++) {
            float d = len * (float)seg * 0.25f;
            float fl = sinf((float)g->t * 9.0f - (float)seg * 1.3f) * 0.08f * (float)seg * (0.3f + 0.1f * fminf(bs->aws, 8.0f));
            float ex, ey;
            if (!boat_pt(&k, lk->mast_x + wx * d - wy * fl, wy * d + wx * fl,
                         lk->mast_height - 0.03f * (float)seg * (float)seg, &ex, &ey)) break;
            pd->graphics->drawLine((int)px, (int)py, (int)ex, (int)ey, seg < 3 ? 3 : 2, kColorBlack);
            px = ex; py = ey;
        }
    }
}

/* ---- hull and rudder ---- */

/* rudder blade seen from above and behind: stock at the stern, trailing edge swung by the helm */
static float rel_surface(const Game *g, float fwd, float lat);

static void draw_rudder(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    Ctx k = { pd, g, c };
    float L = g->boat.hull.length;
    float chord = 0.55f * fmaxf(1.0f, sqrtf(L / 4.0f));   /* bigger boats carry a bigger blade */
    /* the physics rudder sits forward of the transom on big boats; draw the stock at the transom so
     * the blade always trails out past the stern */
    float rx = fminf(g->boat.foils.rudder_x, -0.5f * L);
    float d = g->b.wheel * g->boat.foils.rudder_max_deg * 0.017453f;
    float tx = rx - chord * cosf(d), ty = chord * sinf(d);
    float x0, y0, x1, y1, x2, y2, x3, y3;
    float zw = clampf(rel_surface(g, rx, 0.0f), -0.2f, 0.45f);   /* the blade below the surface is not drawn */
    if (!boat_pt(&k, rx, 0, 0.45f, &x0, &y0) || !boat_pt(&k, tx, ty, 0.45f, &x1, &y1) ||
        !boat_pt(&k, tx, ty, zw, &x2, &y2) || !boat_pt(&k, rx, 0, zw, &x3, &y3)) return;
    int pts[8] = { (int)x0, (int)y0, (int)x1, (int)y1, (int)x2, (int)y2, (int)x3, (int)y3 };
    pd->graphics->fillPolygon(4, pts, kColorBlack, kPolygonFillNonZero);
    pd->graphics->drawLine((int)x0, (int)y0, (int)x3, (int)y3, 3, kColorBlack);   /* stock */
}

/* sea surface relative to the hull's mean waterline, at a boat-frame point (m, + up) */
static float rel_surface(const Game *g, float fwd, float lat) {
    const BoatState *b = &g->b;
    double ch = cos(b->heading), sh = sin(b->heading);
    float dx = (float)(b->x - sv_ox + ch * fwd - sh * lat);
    float dy = (float)(b->y - sv_oy + sh * fwd + ch * lat);
    return sea_view_height(&sv, dx, dy) - b->heave;
}

/* height of each point above the sea surface; skipped (all dry) while everything is well clear of the waves */
static void sea_dist(const Game *g, const HgPt *p, int n, float *d) {
    float lo = 1e9f;
    for (int i = 0; i < n; i++) if (p[i].z < lo) lo = p[i].z;
    for (int i = 0; i < n; i++) d[i] = lo > 1.0f ? 1.0f : p[i].z - rel_surface(g, p[i].f, p[i].l);
}

/* A flat panel given in the heading frame, clipped to the part above the sea. The cut edge gets a foam line. */
static void draw_panel(PlaydateAPI *pd, const Ctx *k, const HgPt *in, int n, LCDColor fill, int lw) {
    enum { MAXP = 24 };
    float d[MAXP];
    HgPt cl[2 * MAXP];
    bool cut[2 * MAXP];
    if (n > MAXP) return;
    sea_dist(k->g, in, n, d);
    int m = hull_clip_above(in, d, n, cl, cut, 2 * MAXP);
    if (m < 3) return;
    int q[2 * 2 * MAXP], v = 0;
    bool foam_edge[2 * MAXP];            /* edge from kept point v to v + 1 lies on the sea surface */
    int prev = -1;
    for (int i = 0; i < m; i++) {
        float sx, sy;
        if (!rel_pt(k, cl[i].f, cl[i].l, cl[i].z, &sx, &sy)) continue;
        if (prev >= 0) foam_edge[v - 1] = cut[prev] && cut[i];
        q[2 * v] = (int)sx; q[2 * v + 1] = (int)sy; v++;
        prev = i;
    }
    if (v < 3) return;
    int last = prev, first = 0;
    for (int i = 0; i < m; i++) { float sx, sy; if (rel_pt(k, cl[i].f, cl[i].l, cl[i].z, &sx, &sy)) { first = i; break; } }
    foam_edge[v - 1] = cut[last] && cut[first];
    pd->graphics->fillPolygon(v, q, fill, kPolygonFillNonZero);
    for (int i = 0; i < v; i++) {
        int j = (i + 1) % v;
        if (foam_edge[i]) foam_stroke(pd, q[2 * i], q[2 * i + 1], q[2 * j], q[2 * j + 1], 3.0f, 3);
        else pd->graphics->drawLine(q[2 * i], q[2 * i + 1], q[2 * j], q[2 * j + 1], lw, kColorBlack);
    }
}

/* Sutherland-Hodgman: the subject polygon cut to a convex clip polygon, both in screen pixels.
 * Returns the vertex count of out (capped at max). */
static int clip_convex(const float *sub, int n, const float *clip, int m, float *out, int max) {
    float area = 0.0f;
    for (int i = 0; i < m; i++) {
        int j = (i + 1) % m;
        area += clip[2 * i] * clip[2 * j + 1] - clip[2 * j] * clip[2 * i + 1];
    }
    float s = area >= 0.0f ? 1.0f : -1.0f;
    float a[64], b[64];
    int na = n < 32 ? n : 32;
    for (int i = 0; i < 2 * na; i++) a[i] = sub[i];
    for (int e = 0; e < m && na > 0; e++) {
        int f = (e + 1) % m;
        float ex = clip[2 * f] - clip[2 * e], ey = clip[2 * f + 1] - clip[2 * e + 1];
        int nb = 0;
        for (int i = 0; i < na; i++) {
            int j = (i + 1) % na;
            float di = s * (ex * (a[2 * i + 1] - clip[2 * e + 1]) - ey * (a[2 * i] - clip[2 * e]));
            float dj = s * (ex * (a[2 * j + 1] - clip[2 * e + 1]) - ey * (a[2 * j] - clip[2 * e]));
            if (di >= 0.0f && nb < 31) { b[2 * nb] = a[2 * i]; b[2 * nb + 1] = a[2 * i + 1]; nb++; }
            if ((di >= 0.0f) != (dj >= 0.0f) && nb < 31) {
                float t = di / (di - dj);
                b[2 * nb] = a[2 * i] + t * (a[2 * j] - a[2 * i]);
                b[2 * nb + 1] = a[2 * i + 1] + t * (a[2 * j + 1] - a[2 * i + 1]);
                nb++;
            }
        }
        na = nb;
        for (int i = 0; i < 2 * na; i++) a[i] = b[i];
    }
    if (na > max) na = max;
    for (int i = 0; i < 2 * na; i++) out[i] = a[i];
    return na;
}

static void fill_clipped(PlaydateAPI *pd, const float *poly, int n, const float *rim, LCDColor fill) {
    float o[64];
    int c = clip_convex(poly, n, rim, 4, o, 32);
    if (c < 3) return;
    int q[64];
    for (int i = 0; i < 2 * c; i++) q[i] = (int)lroundf(o[i]);
    pd->graphics->fillPolygon(c, q, fill, kPolygonFillNonZero);
}

/* The cockpit well: a four-sided opening in the deck with a floor below it. The deck is opaque, so only
 * what shows through the opening is drawn: the floor and the inner walls facing the eye, each cut to the
 * outline of the opening on screen. The opening never shows the sea. */
static void draw_cockpit(PlaydateAPI *pd, const Ctx *k) {
    const Game *g = k->g;
    const LookCfg *lk = &g->boat.look;
    if (lk->cockpit_depth <= 0.0f) return;
    float fb = lk->freeboard, zf = fb - lk->cockpit_depth;
    /* corners, going round: forward port, forward starboard, aft starboard, aft port */
    const float cf[4] = { lk->cockpit_x1, lk->cockpit_x1, lk->cockpit_x0, lk->cockpit_x0 };
    const float cl[4] = { -lk->cockpit_w1, lk->cockpit_w1, lk->cockpit_w0, -lk->cockpit_w0 };
    HgPt top[4], bot[4];
    float rim[8], flo[8];
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        float l, z;
        heel_xf(&g->b, cl[i], fb, &l, &z);
        top[i] = (HgPt){ cf[i], l, z };
        heel_xf(&g->b, cl[i], zf, &l, &z);
        bot[i] = (HgPt){ cf[i], l, z };
        ok = ok && rel_pt(k, top[i].f, top[i].l, top[i].z, &rim[2 * i], &rim[2 * i + 1])
                && rel_pt(k, bot[i].f, bot[i].l, bot[i].z, &flo[2 * i], &flo[2 * i + 1]);
    }
    if (!ok) return;

    /* floor: the whole floor, not cut against the sea. The opening is a hole in an opaque deck, so
     * whatever is under it must paint over the opening, or a heeled boat shows the sea through it. */
    fill_clipped(pd, flo, 4, rim, render_pattern((int)(lk->hull_shade * 16.0f) + 9));

    /* walls: an inner face shows when the eye is on the well's side of its plane */
    const BoatState *b = &g->b;
    double ch = cos(b->heading), sh = sin(b->heading);
    float eye[3] = { (float)((k->c->x - b->x) * ch + (k->c->y - b->y) * sh),
                     (float)(-sh * (k->c->x - b->x) + ch * (k->c->y - b->y)),
                     k->c->height - b->heave };
    float mid[3] = { 0, 0, 0 };
    for (int i = 0; i < 4; i++) {
        mid[0] += 0.125f * (top[i].f + bot[i].f);
        mid[1] += 0.125f * (top[i].l + bot[i].l);
        mid[2] += 0.125f * (top[i].z + bot[i].z);
    }
    LCDColor wall_col = render_pattern((int)(lk->hull_shade * 16.0f) + 3);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        float u[3] = { top[j].f - top[i].f, top[j].l - top[i].l, top[j].z - top[i].z };
        float v[3] = { bot[i].f - top[i].f, bot[i].l - top[i].l, bot[i].z - top[i].z };
        float n[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
        float pm[3] = { 0.5f * (top[i].f + top[j].f), 0.5f * (top[i].l + top[j].l), 0.5f * (top[i].z + top[j].z) };
        float in = n[0] * (mid[0] - pm[0]) + n[1] * (mid[1] - pm[1]) + n[2] * (mid[2] - pm[2]);
        if (in < 0.0f) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }        /* point into the well */
        if (n[0] * (eye[0] - pm[0]) + n[1] * (eye[1] - pm[1]) + n[2] * (eye[2] - pm[2]) <= 0.0f) continue;
        float w[8] = { rim[2 * i], rim[2 * i + 1], rim[2 * j], rim[2 * j + 1],
                       flo[2 * j], flo[2 * j + 1], flo[2 * i], flo[2 * i + 1] };
        fill_clipped(pd, w, 4, rim, wall_col);
    }

    int q[8];                                  /* the rim on the deck */
    for (int i = 0; i < 8; i++) q[i] = (int)lroundf(rim[i]);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        pd->graphics->drawLine(q[2 * i], q[2 * i + 1], q[2 * j], q[2 * j + 1], 2, kColorBlack);
    }
}

/* the hull seen from behind */
static void draw_hull(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    Ctx k = { pd, g, c };
    const LookCfg *lk = &g->boat.look;
    float L = g->boat.hull.length, hb = 0.5f * g->boat.hull.beam, fb = lk->freeboard;
    enum { N = 9 };
    float sx, sy;
    LCDColor side_col = render_pattern((int)(lk->hull_shade * 16.0f) + 5);
    LCDColor deck_col = render_pattern((int)(lk->hull_shade * 16.0f));
    int hidden = far_side(g, c);
    bool up = deck_faces_eye(g, c);

    /* the gunwale loop, as the deck outline: starboard bow -> stern, then port stern -> bow */
    HgPt deck[2 * N];
    for (int i = 0; i < N; i++) {
        float u = (float)i / (N - 1);
        float w = hb * table_eval(lk->hull_width.p, lk->hull_width.n, u);
        float l, z;
        heel_xf(&g->b, w, fb, &l, &z);
        deck[i] = (HgPt){ L * (0.5f - u), l, z };
        heel_xf(&g->b, -w, fb, &l, &z);
        deck[2 * N - 1 - i] = (HgPt){ L * (0.5f - u), l, z };
    }
    /* seen from the underside the deck is behind the topsides, so it goes down first */
    if (!up) draw_panel(pd, &k, deck, 2 * N, deck_col, 2);

    /* topsides only: each panel runs from the gunwale down to where the sea meets the heeled side, so
     * nothing below the water is drawn and the hull sinks into troughs. The far side faces away. */
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        if (sgn == hidden) continue;
        int q[4 * N], m = 0;
        bool ok = true, dry = false;      /* dry: some of the side is above the water */
        for (int i = 0; i < N && ok; i++) {
            float u = (float)i / (N - 1);
            float w = hb * table_eval(lk->hull_width.p, lk->hull_width.n, u);
            ok = boat_pt(&k, L * (0.5f - u), sgn * w, fb, &sx, &sy);
            q[2 * m] = (int)sx; q[2 * m + 1] = (int)sy; m++;
        }
        int top = m;
        for (int i = N - 1; i >= 0 && ok; i--) {
            float x, y, z;
            side_wl(g, sgn, (float)i / (N - 1), 1.0f, &x, &y, &z);
            if (z < fb - 1e-3f) dry = true;
            ok = boat_pt(&k, x, y, z, &sx, &sy);
            q[2 * m] = (int)sx; q[2 * m + 1] = (int)sy; m++;
        }
        if (!ok || !dry) continue;        /* a side that is all under water shows nothing */
        pd->graphics->fillPolygon(m, q, side_col, kPolygonFillNonZero);
        for (int i = 0; i + 1 < top; i++)   /* gunwale */
            pd->graphics->drawLine(q[2 * i], q[2 * i + 1], q[2 * i + 2], q[2 * i + 3], 1, kColorBlack);
        for (int i = top; i + 1 < m; i++)   /* waterline: foam */
            foam_stroke(pd, q[2 * i], q[2 * i + 1], q[2 * i + 2], q[2 * i + 3], 3.0f, 3);
        pd->graphics->drawLine(q[0], q[1], q[2 * (m - 1)], q[2 * (m - 1) + 1], 1, kColorBlack);                  /* stem */
        pd->graphics->drawLine(q[2 * (top - 1)], q[2 * (top - 1) + 1], q[2 * top], q[2 * top + 1], 1, kColorBlack);   /* stern */
    }
    {
        float wt = hb * table_eval(lk->hull_width.p, lk->hull_width.n, 1.0f);
        float xs = -0.5f * L;
        float xp, yp, zp, xq, yq, zq, l, z;
        side_wl(g, -1, 1.0f, 1.0f, &xp, &yp, &zp);
        side_wl(g, 1, 1.0f, 1.0f, &xq, &yq, &zq);
        HgPt tr[4];
        heel_xf(&g->b, -wt, fb, &l, &z); tr[0] = (HgPt){ xs, l, z };
        heel_xf(&g->b, wt, fb, &l, &z);  tr[1] = (HgPt){ xs, l, z };
        heel_xf(&g->b, yq, zq, &l, &z);  tr[2] = (HgPt){ xs, l, z };
        heel_xf(&g->b, yp, zp, &l, &z);  tr[3] = (HgPt){ xs, l, z };
        draw_panel(pd, &k, tr, 4, side_col, 1);
        float ax, ay, bx, by;
        if (hidden == 0 && rel_pt(&k, xs, tr[2].l, tr[2].z, &ax, &ay) && rel_pt(&k, xs, tr[3].l, tr[3].z, &bx, &by))
            foam_stroke(pd, (int)ax, (int)ay, (int)bx, (int)by, 3.0f, 3);
    }
    if (up) {
        draw_panel(pd, &k, deck, 2 * N, deck_col, 2);
        draw_cockpit(pd, &k);
    }
}

/* ---- race buoys ---- */

#define BUOY_TOP_M 2.5f     /* m above the sea surface */
#define BUOY_HALF_M 0.8f    /* m, half the base width */
#define GATE_LEN_M 40.0f    /* m, drawn length of the gate line from the mark */
#define GATE_HEIGHT_M 2.0f  /* m above the sea surface */
#define GATE_DASH_M 1.0f    /* m, dash length; the gap is the same */
#define ARROW_SWEEP (1.0f / 3.0f)   /* fraction of a full circle the turn arrow covers */
#define ARROW_RADIUS_M 8.0f         /* m, from the mark to the arrow's path */
#define ARROW_HEAD_M 2.5f           /* m, length of the arrow head's barbs */

typedef struct {
    float depth;
    int i;
    float bx, by, tx, ty, hw;   /* projected base centre, apex, base half-width (px) */
} MarkDraw;

static int mark_far_first(const void *x, const void *y) {
    float a = ((const MarkDraw *)x)->depth, b = ((const MarkDraw *)y)->depth;
    return (a < b) - (a > b);
}

static void draw_buoy(PlaydateAPI *pd, const MarkDraw *m) {
    int tx = (int)lroundf(m->tx), ty = (int)lroundf(m->ty);
    int lx = (int)lroundf(m->bx - m->hw), rx = (int)lroundf(m->bx + m->hw), by = (int)lroundf(m->by);
    pd->graphics->fillTriangle(tx, ty, lx, by, rx, by, kColorBlack);
    pd->graphics->drawLine(tx, ty, lx, by, 1, kColorWhite);
    pd->graphics->drawLine(tx, ty, rx, by, 1, kColorWhite);
    pd->graphics->drawLine(lx, by, rx, by, 1, kColorWhite);
}

/* A point `h` m above the sea at world (px, py), projected; false if behind the camera or in the mist. */
static bool gate_pt(const Game *g, const CameraPose *c, float t, double px, double py, float h,
                    float *sx, float *sy) {
    float d;
    float z = sea_height(&g->sea, (float)px, (float)py, t) + h;
    return camera_project(c, px, py, z, sx, sy, &d) && d <= fvis(g);
}

/* One stroke of the gate markings: white under black so it reads on light and dark sea. */
static void gate_stroke(PlaydateAPI *pd, float x0, float y0, float x1, float y1) {
    pd->graphics->drawLine((int)x0, (int)y0, (int)x1, (int)y1, 3, kColorWhite);
    pd->graphics->drawLine((int)x0, (int)y0, (int)x1, (int)y1, 1, kColorBlack);
}

/* The turn arrow: a dashed arc around the mark, ARROW_SWEEP of the way round, arriving at the gate
   line at ARROW_RADIUS_M in the direction the boat has to go. It sits at twice the line's height and
   eases down so the point lands on the line; the head is a vertical fan (perpendicular to the sea). */
static void draw_turn_arrow(PlaydateAPI *pd, const Game *g, const CameraPose *c, float t,
                            double mx, double my, const Mark *mk) {
    const float sweep = ARROW_SWEEP * 2.0f * (float)M_PI;
    const float arc_len = sweep * ARROW_RADIUS_M;
    const float turn = (float)mk->rounding;           /* the heading angle grows when rounding to starboard */
    const float a0 = mk->gate - turn * sweep;         /* where the arrow starts; it ends on the gate ray */
    for (float s = 0.0f; s < arc_len; s += 2.0f * GATE_DASH_M) {
        float sx[2], sy[2];
        bool ok = true;
        for (int e = 0; e < 2 && ok; e++) {
            float u = fminf((s + e * GATE_DASH_M) / arc_len, 1.0f);
            float a = a0 + turn * sweep * u;
            float h = GATE_HEIGHT_M * (2.0f - u * u);
            ok = gate_pt(g, c, t, mx + cos(a) * ARROW_RADIUS_M, my + sin(a) * ARROW_RADIUS_M, h, &sx[e], &sy[e]);
        }
        if (ok) gate_stroke(pd, sx[0], sy[0], sx[1], sy[1]);
    }
    /* head: back along the tangent from the tip, fanned up and down */
    const double tipx = mx + cos(mk->gate) * ARROW_RADIUS_M, tipy = my + sin(mk->gate) * ARROW_RADIUS_M;
    const double tx = -turn * sin(mk->gate), ty = turn * cos(mk->gate);   /* unit travel direction at the tip */
    float tip_x, tip_y, wx[2], wy[2];
    if (!gate_pt(g, c, t, tipx, tipy, GATE_HEIGHT_M, &tip_x, &tip_y)) return;
    for (int k = 0; k < 2; k++) {
        float h = GATE_HEIGHT_M + (k ? -ARROW_HEAD_M : ARROW_HEAD_M) * 0.6f;
        if (!gate_pt(g, c, t, tipx - tx * ARROW_HEAD_M, tipy - ty * ARROW_HEAD_M, h, &wx[k], &wy[k])) return;
        gate_stroke(pd, tip_x, tip_y, wx[k], wy[k]);
    }
}

/* A mark's gate: a dashed line raised above the sea along the ray the boat has to cross,
   and the arrow showing which way round to cross it. (mx, my) is the mark in the local frame. */
static void draw_gate(PlaydateAPI *pd, const Game *g, const CameraPose *c, double mx, double my,
                      const Mark *mk) {
    const float t = (float)fmod(g->t, g->sea.period);
    const double dx = cos(mk->gate), dy = sin(mk->gate);
    for (float s = 0.0f; s < GATE_LEN_M; s += 2.0f * GATE_DASH_M) {
        float sx[2], sy[2];
        bool ok = true;
        for (int e = 0; e < 2 && ok; e++)
            ok = gate_pt(g, c, t, mx + dx * (s + e * GATE_DASH_M), my + dy * (s + e * GATE_DASH_M),
                         GATE_HEIGHT_M, &sx[e], &sy[e]);
        if (ok) gate_stroke(pd, sx[0], sy[0], sx[1], sy[1]);
    }
    draw_turn_arrow(pd, g, c, t, mx, my, mk);
}

/* Projects the mark at local (mx, my) into dm[*n] as mark number i; skipped if it is out of view. */
static void add_mark(const Game *g, const CameraPose *c, float t, MarkDraw *dm, int *n, int i,
                     double mx, double my) {
    float zb = sea_height(&g->sea, (float)mx, (float)my, t);
    MarkDraw *m = &dm[*n];
    float bd, td;
    if (!camera_project(c, mx, my, zb, &m->bx, &m->by, &bd)) return;
    if (!camera_project(c, mx, my, zb + BUOY_TOP_M, &m->tx, &m->ty, &td)) return;
    if (bd > fvis(g)) return;   /* hidden in mist and similar */
    m->depth = bd;
    m->i = i;
    m->hw = fmaxf(2.0f, BUOY_HALF_M * CAM_FOCAL / bd);
    (*n)++;
}

/* Far to near, the next mark last so it sits on top. */
static void draw_buoys(PlaydateAPI *pd, MarkDraw *dm, int n, int next) {
    qsort(dm, (size_t)n, sizeof dm[0], mark_far_first);
    for (int k = 0; k < n; k++)
        if (dm[k].i != next) draw_buoy(pd, &dm[k]);
    for (int k = 0; k < n; k++)
        if (dm[k].i == next) draw_buoy(pd, &dm[k]);
}

static void draw_marks(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    MarkDraw dm[TRACK_MAX_MARKS];
    int n = 0;
    const float t = (float)fmod(g->t, g->sea.period);
    for (int i = 0; i < g->track.nmarks && i < TRACK_MAX_MARKS; i++) {
        double mx, my;
        race_mark_local(&g->race, &g->track, i, &mx, &my);
        add_mark(g, c, t, dm, &n, i, mx, my);
    }
    if (!g->race.finished && g->race.next < g->track.nmarks) {
        double mx, my;
        race_mark_local(&g->race, &g->track, g->race.next, &mx, &my);
        draw_gate(pd, g, c, mx, my, &g->track.marks[g->race.next]);
    }
    draw_buoys(pd, dm, n, g->race.next);
}

static void draw_journey(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    MarkDraw dm[JOURNEY_MAX];
    int n = 0;
    const float t = (float)fmod(g->t, g->sea.period);
    for (int i = 0; i < g->journey.n; i++)
        add_mark(g, c, t, dm, &n, i, g->journey.buoy[i].x, g->journey.buoy[i].y);
    const JourneyBuoy *nb = journey_next(&g->journey);
    if (nb) {
        Mark mk = { 0 };
        mk.x = (float)nb->x;
        mk.y = (float)nb->y;
        mk.gate = nb->gate;
        mk.rounding = nb->rounding;
        draw_gate(pd, g, c, nb->x, nb->y, &mk);
    }
    draw_buoys(pd, dm, n, g->journey.next);
}

void render_frame(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    pd->graphics->clear(kColorWhite);
    draw_clouds(pd, g, c);
    draw_sea(pd, g, c);
    draw_streaks(pd, g, c);
    /* draw_rocks(pd, g, c);   rocks disabled for now */
    if (g->mode == GAME_RACE) draw_marks(pd, g, c);
    if (g->mode == GAME_JOURNEY) draw_journey(pd, g, c);
    draw_spray(pd, g, c);
    draw_waterline(pd, g, c);
    /* heeled so far that the underside faces the eye: the rig is beyond the hull, which then hides it */
    bool rig_behind = !deck_faces_eye(g, c);
    if (rig_behind) draw_rig(pd, g, c);
    draw_hull(pd, g, c);
    draw_rudder(pd, g, c);   /* head stands above the transom: the waterline sits behind the panel */
    if (!rig_behind) draw_rig(pd, g, c);
    hud_draw(pd, g);
    if (g->mode == GAME_RACE) hud_race(pd, g, c);
    if (g->mode == GAME_JOURNEY) hud_journey(pd, g, c);
    hud_prompt(pd, g);
}
