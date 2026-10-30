#include "render.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "mathx.h"

#define LEVELS 17
#define SEA_BANDS 15
#define CLOUDS 24
#define STREAKS 24
#define PI_D 3.14159265358979323846

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
    if (level >= LEVELS) level = LEVELS - 1;
    return (LCDColor)pats[level];
}

static uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
static float frand(uint32_t i, uint32_t salt) { return (float)(hash32(i * 2654435761u + salt) >> 8) / 16777216.0f; }

static float fvis(const Game *g) { return g->weather.preset[g->preset].visibility; }

/* ---- sky ---- */

static void draw_clouds(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    static const int shade[WEATHER_PRESETS] = { 2, 5, 8, 11 };
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

static void draw_sea(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    float vis = fvis(g);
    float t = (float)fmod(g->t, g->sea.period);
    float fy0, fy1;
    if (!sea_line(c, 20000.0f, 0.0f, &fy0, &fy1)) { fy0 = fy1 = CAM_HORIZON_Y; }
    float dist[SEA_BANDS];
    for (int i = 0; i < SEA_BANDS; i++) dist[SEA_BANDS - 1 - i] = 3.0f * powf(1.5f, (float)i);
    /* far -> near; dist[0] is the farthest band */
    for (int i = 0; i < SEA_BANDS; i++) {
        float d = dist[i];
        double fx = cos(c->yaw), fy = sin(c->yaw);
        float hz = sea_height(&g->sea, (float)fmod(c->x + fx * d, SEA_WRAP), (float)fmod(c->y + fy * d, SEA_WRAP), t);
        float h2 = sea_height(&g->sea, (float)fmod(c->x + fx * (d + 2), SEA_WRAP), (float)fmod(c->y + fy * (d + 2), SEA_WRAP), t);
        float slope = (h2 - hz) / 2.0f;
        float y0, y1;
        if (!sea_line(c, d, hz, &y0, &y1)) continue;
        float fog = clampf(d / vis, 0.0f, 1.0f);
        float base = 10.0f - 3.0f * (d / 300.0f > 1 ? 1 : d / 300.0f);
        int level = (int)(base + clampf(-slope * 25.0f, -3.0f, 3.0f) + ((i & 1) ? 1.0f : -1.0f));
        level = (int)(level * (1.0f - 0.9f * fog * fog));
        strip(pd, fy0, fy1, y0, y1, level);
        fy0 = y0; fy1 = y1;
        if (d > vis * 1.2f) { /* beyond visibility the sea is just sky-coloured haze */ }
    }
    strip(pd, fy0, fy1, CAM_H + 40, CAM_H + 40, 10);
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

static void draw_rocks(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    float vis = fvis(g);
    float t = (float)fmod(g->t, g->sea.period);
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
        float h = sea_height(&g->sea, (float)fmod(r->x, SEA_WRAP), (float)fmod(r->y, SEA_WRAP), t);
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

/* water-plane point from boat-frame (fwd, stbd) metres, ignoring heel: the sea surface stays level */
static bool water_pt(const Game *g, const CameraPose *c, float fwd, float lat, float *sx, float *sy) {
    const BoatState *b = &g->b;
    double ch = cos(b->heading), sh = sin(b->heading);
    return camera_project(c, b->x + ch * fwd - sh * lat, b->y + sh * fwd + ch * lat, 0.0f, sx, sy, NULL);
}

/* Foam where the hull meets the water, a bow wave peeling off the stem and a wake that
 * widens and fades astern. Strength follows speed; heel lifts one rail and buries the other. */
static void draw_waterline(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    const BoatState *b = &g->b;
    const LookCfg *lk = &g->boat.look;
    float L = g->boat.hull.length, hb = 0.5f * g->boat.hull.beam;
    float sp = fabsf(b->u);
    float k = clampf(sp / 4.0f, 0.0f, 1.0f);
    float sx, sy, px, py;
    enum { N = 9 };

    /* waterline ring: the hull outline at the surface, thicker on the buried (leeward) side */
    float dip = sinf(b->heel) * 0.5f;                       /* + heel buries the starboard rail */
    for (int side = -1; side <= 1; side += 2) {
        float bury = clampf(0.5f + side * dip * 4.0f, 0.0f, 1.0f);
        int thick = 1 + (int)(bury * 2.0f + k * 1.0f);
        bool have = false;
        for (int i = 0; i < N; i++) {
            float u = (float)i / (N - 1);
            float w = hb * table_eval(lk->hull_width.p, lk->hull_width.n, u) * 1.08f;
            if (!water_pt(g, c, L * (0.5f - u), side * w, &sx, &sy)) { have = false; continue; }
            if (have) pd->graphics->drawLine((int)px, (int)py, (int)sx, (int)sy, thick, kColorWhite);
            px = sx; py = sy; have = true;
        }
    }
    float tx, ty;                                            /* foam across the transom */
    if (water_pt(g, c, -0.5f * L, -hb, &px, &py) && water_pt(g, c, -0.5f * L, hb, &tx, &ty))
        pd->graphics->drawLine((int)px, (int)py, (int)tx, (int)ty, 1, kColorWhite);

    if (sp < 0.5f) return;

    /* bow wave: two crests sweeping back and out from the stem, with a rippling edge */
    float reach = 0.8f + 2.2f * k, spread = 0.25f + 0.9f * k;
    uint32_t frame = (uint32_t)(g->t * 12.0);
    for (int side = -1; side <= 1; side += 2) {
        bool have = false;
        for (int i = 0; i <= 6; i++) {
            float f = (float)i / 6.0f;
            float jig = (frand(frame + i + (side > 0 ? 7 : 0), 31) - 0.5f) * 0.12f * k;
            if (!water_pt(g, c, 0.5f * L - f * reach, side * (hb * 0.3f + f * spread + jig), &sx, &sy)) { have = false; continue; }
            if (have) pd->graphics->drawLine((int)px, (int)py, (int)sx, (int)sy, 1, kColorWhite);
            px = sx; py = sy; have = true;
        }
    }

    /* wake: ripples spaced astern that spread and thin out, shifting back with time */
    int rings = 6;
    float phase = fmodf((float)g->t * (0.4f + 0.3f * k), 1.0f);
    for (int i = 0; i < rings; i++) {
        float f = ((float)i + phase) / rings;                /* 0 at the transom .. 1 far astern */
        float back = -0.5f * L - f * (3.0f + 6.0f * k);
        float half = hb * (0.6f + f * (1.4f + 1.5f * k));
        if (frand(i + (uint32_t)(g->t * 3.0), 41) < f * 0.6f) continue;   /* breaks up with distance */
        float ax, ay, bx, by;
        if (!water_pt(g, c, back, -half, &ax, &ay) || !water_pt(g, c, back, half, &bx, &by)) continue;
        pd->graphics->drawLine((int)ax, (int)ay, (int)bx, (int)by, 1, kColorWhite);
    }
}

/* ---- rig ---- */

typedef struct { const PlaydateAPI *pd; const Game *g; const CameraPose *c; } Ctx;

/* boat-frame (x fwd, y stbd, z up from waterline) -> screen; heel is applied in full */
static bool boat_pt(const Ctx *k, float x, float y, float z, float *sx, float *sy) {
    const BoatState *b = &k->g->b;
    float ch = cosf(b->heel), sh = sinf(b->heel);
    float yy = y * ch + z * sh, zz = z * ch - y * sh;
    double cd = cos(b->heading), sd = sin(b->heading);
    return camera_project(k->c, b->x + x * cd - yy * sd, b->y + x * sd + yy * cd, zz + b->heave, sx, sy, NULL);
}

static void draw_sail(PlaydateAPI *pd, const Ctx *k, int i) {
    const BoatCfg *cfg = &k->g->boat;
    const SailCfg *s = &cfg->sails[i];
    const BoatState *b = &k->g->b;
    float hoist = b->hoists[i];
    if (hoist < 0.02f) return;
    float side = b->swing[i];                                      /* eased across when the wind changes sides */
    float fb = cfg->look.freeboard;
    float tx = s->tack_x, tz = fb + s->tack_z;
    float hx = s->shape == 0 ? tx : tx + (cfg->look.mast_x - tx) * 0.15f;
    float hz = tz + s->luff * hoist;
    float tr = b->trims[i];
    float cx = tx - s->foot * cosf(tr), cy = side * s->foot * sinf(tr);
    float lufft = b->luff[i];
    int mw = s->mesh_w < 1 ? 1 : s->mesh_w > 12 ? 12 : s->mesh_w;
    int mh = s->mesh_h < 1 ? 1 : s->mesh_h > 16 ? 16 : s->mesh_h;
    int outline[2 * (2 * 12 + 2)];
    int on = 0;
    float px[17][13], py[17][13];
    bool ok[17][13];
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
            float z = lz + (ez - lz) * v;
            ok[a][q] = boat_pt(k, x, y, z, &px[a][q], &py[a][q]);
        }
    }
    /* outline: luff up, head across, leech down, foot back */
    for (int a = 0; a <= mh && on < 25; a++) if (ok[a][0]) { outline[2 * on] = (int)px[a][0]; outline[2 * on + 1] = (int)py[a][0]; on++; }
    for (int a = mh; a >= 0 && on < 26; a--) if (ok[a][mw]) { outline[2 * on] = (int)px[a][mw]; outline[2 * on + 1] = (int)py[a][mw]; on++; }
    int shade = (int)clampf(s->shade * 16.0f, 0.0f, 16.0f);
    if (on >= 3) pd->graphics->fillPolygon(on, outline, render_pattern(16 - shade), kPolygonFillNonZero);
    for (int a = 0; a <= mh; a++)
        for (int q = 0; q < mw; q++)
            if (ok[a][q] && ok[a][q + 1] && (a == 0 || a == mh || (a & 1) == 0))
                pd->graphics->drawLine((int)px[a][q], (int)py[a][q], (int)px[a][q + 1], (int)py[a][q + 1], 1, kColorBlack);
    for (int p = 0; p + 1 < on; p++)
        pd->graphics->drawLine(outline[2 * p], outline[2 * p + 1], outline[2 * p + 2], outline[2 * p + 3], 1, kColorBlack);
    if (on >= 3)
        pd->graphics->drawLine(outline[2 * (on - 1)], outline[2 * (on - 1) + 1], outline[0], outline[1], 1, kColorBlack);

    /* telltales near the luff: stream along the sail when it flows, whip about the wind when it luffs */
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
static void draw_rudder(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    Ctx k = { pd, g, c };
    const float chord = 0.55f;
    float rx = g->boat.foils.rudder_x;
    float d = g->b.wheel * g->boat.foils.rudder_max_deg * 0.017453f;
    float tx = rx - chord * cosf(d), ty = chord * sinf(d);
    float x0, y0, x1, y1, x2, y2, x3, y3;
    if (!boat_pt(&k, rx, 0, 0.9f, &x0, &y0) || !boat_pt(&k, tx, ty, 0.9f, &x1, &y1) ||
        !boat_pt(&k, tx, ty, -0.2f, &x2, &y2) || !boat_pt(&k, rx, 0, -0.2f, &x3, &y3)) return;
    int pts[8] = { (int)x0, (int)y0, (int)x1, (int)y1, (int)x2, (int)y2, (int)x3, (int)y3 };
    pd->graphics->fillPolygon(4, pts, kColorBlack, kPolygonFillNonZero);
    pd->graphics->drawLine((int)x0, (int)y0, (int)x3, (int)y3, 3, kColorBlack);   /* stock */
}

/* the hull seen from behind */
static void draw_hull(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    Ctx k = { pd, g, c };
    const LookCfg *lk = &g->boat.look;
    float L = g->boat.hull.length, hb = 0.5f * g->boat.hull.beam, fb = lk->freeboard;
    enum { N = 9 };
    int pts[4 * N];
    int n = 0;
    float sx, sy;
    for (int i = 0; i < N; i++) {            /* starboard gunwale bow -> stern */
        float u = (float)i / (N - 1);
        float w = hb * table_eval(lk->hull_width.p, lk->hull_width.n, u);
        if (boat_pt(&k, L * (0.5f - u), w, fb, &sx, &sy)) { pts[2 * n] = (int)sx; pts[2 * n + 1] = (int)sy; n++; }
    }
    for (int i = N - 1; i >= 0; i--) {       /* port gunwale stern -> bow */
        float u = (float)i / (N - 1);
        float w = hb * table_eval(lk->hull_width.p, lk->hull_width.n, u);
        if (boat_pt(&k, L * (0.5f - u), -w, fb, &sx, &sy)) { pts[2 * n] = (int)sx; pts[2 * n + 1] = (int)sy; n++; }
    }
    if (n < 3) return;
    /* topsides and bottom: gunwale line down to a narrower keel line, one panel per side, plus the transom */
    const float keel = -0.3f, tuck = 0.45f;
    LCDColor side_col = render_pattern((int)(lk->hull_shade * 16.0f) + 5);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        int q[4 * N], m = 0;
        bool ok = true;
        for (int i = 0; i < N && ok; i++) {
            float u = (float)i / (N - 1);
            float w = hb * table_eval(lk->hull_width.p, lk->hull_width.n, u);
            ok = boat_pt(&k, L * (0.5f - u), sgn * w, fb, &sx, &sy);
            q[2 * m] = (int)sx; q[2 * m + 1] = (int)sy; m++;
        }
        for (int i = N - 1; i >= 0 && ok; i--) {
            float u = (float)i / (N - 1);
            float w = hb * table_eval(lk->hull_width.p, lk->hull_width.n, u) * tuck;
            ok = boat_pt(&k, L * (0.5f - u), sgn * w, keel, &sx, &sy);
            q[2 * m] = (int)sx; q[2 * m + 1] = (int)sy; m++;
        }
        if (!ok) continue;
        pd->graphics->fillPolygon(m, q, side_col, kPolygonFillNonZero);
        for (int i = 0; i < m; i++)
            pd->graphics->drawLine(q[2 * i], q[2 * i + 1], q[2 * ((i + 1) % m)], q[2 * ((i + 1) % m) + 1], 1, kColorBlack);
    }
    {
        float wt = hb * table_eval(lk->hull_width.p, lk->hull_width.n, 1.0f);
        float xs = -0.5f * L, tx[4][3] = { { xs, -wt, fb }, { xs, wt, fb }, { xs, wt * tuck, keel }, { xs, -wt * tuck, keel } };
        int q[8];
        bool ok = true;
        for (int i = 0; i < 4 && ok; i++) {
            ok = boat_pt(&k, tx[i][0], tx[i][1], tx[i][2], &sx, &sy);
            q[2 * i] = (int)sx; q[2 * i + 1] = (int)sy;
        }
        if (ok) {
            pd->graphics->fillPolygon(4, q, side_col, kPolygonFillNonZero);
            for (int i = 0; i < 4; i++)
                pd->graphics->drawLine(q[2 * i], q[2 * i + 1], q[2 * ((i + 1) % 4)], q[2 * ((i + 1) % 4) + 1], 1, kColorBlack);
        }
    }
    pd->graphics->fillPolygon(n, pts, render_pattern((int)(lk->hull_shade * 16.0f)), kPolygonFillNonZero);
    for (int i = 0; i < n; i++)
        pd->graphics->drawLine(pts[2 * i], pts[2 * i + 1], pts[2 * ((i + 1) % n)], pts[2 * ((i + 1) % n) + 1], 2, kColorBlack);
}

void render_frame(PlaydateAPI *pd, const Game *g, const CameraPose *c) {
    pd->graphics->clear(kColorWhite);
    draw_clouds(pd, g, c);
    draw_sea(pd, g, c);
    draw_streaks(pd, g, c);
    draw_rocks(pd, g, c);
    draw_spray(pd, g, c);
    draw_waterline(pd, g, c);
    draw_hull(pd, g, c);
    draw_rudder(pd, g, c);   /* head stands above the transom: the waterline sits behind the panel */
    draw_rig(pd, g, c);
    hud_draw(pd, g);
    hud_prompt(pd, g);
}
