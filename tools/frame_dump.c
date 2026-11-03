/* Headless frame dump: renders scenarios through render_frame() against a stub 1-bit
 * Playdate graphics API and writes PGM files (tools/frames.sh converts them to PNG). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pd_api.h"
#include "game.h"
#include "camera.h"
#include "render.h"

#define W 400
#define H 240
static uint8_t fb[H][W];            /* 1 = white */

static int pat_white(LCDColor c, int x, int y) {
    if (c == kColorBlack) return 0;
    if (c == kColorWhite) return 1;
    const uint8_t *p = (const uint8_t *)c;
    return (p[y & 7] >> (7 - (x & 7))) & 1;
}
static void plot(int x, int y, LCDColor c) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    fb[y][x] = (uint8_t)pat_white(c, x, y);
}
static void s_clear(LCDColor c) { for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) plot(x, y, c); }
static void s_fillRect(int x, int y, int w, int h, LCDColor c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) plot(i, j, c);
}
static void s_drawRect(int x, int y, int w, int h, LCDColor c) {
    s_fillRect(x, y, w, 1, c); s_fillRect(x, y + h - 1, w, 1, c);
    s_fillRect(x, y, 1, h, c); s_fillRect(x + w - 1, y, 1, h, c);
}
static void s_drawLine(int x0, int y0, int x1, int y1, int wd, LCDColor c) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    if (wd < 1) wd = 1;
    for (;;) {
        for (int j = 0; j < wd; j++) for (int i = 0; i < wd; i++) plot(x0 + i - wd / 2, y0 + j - wd / 2, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void ellipse(int x, int y, int w, int h, int lw, int fill, LCDColor c) {
    float cx = x + w / 2.0f, cy = y + h / 2.0f, rx = w / 2.0f, ry = h / 2.0f;
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) {
        float u = (i + 0.5f - cx) / rx, v = (j + 0.5f - cy) / ry, d = u * u + v * v;
        float ix = lw / rx, in = (1 - ix) * (1 - ix);
        if (d <= 1 && (fill || d >= in)) plot(i, j, c);
    }
}
static void s_drawEllipse(int x, int y, int w, int h, int lw, float a, float b, LCDColor c) { (void)a; (void)b; ellipse(x, y, w, h, lw, 0, c); }
static void s_fillEllipse(int x, int y, int w, int h, float a, float b, LCDColor c) { (void)a; (void)b; ellipse(x, y, w, h, 0, 1, c); }
static void s_fillPolygon(int n, int *p, LCDColor c, LCDPolygonFillRule r) {
    (void)r;
    int ymin = H, ymax = -1;
    for (int i = 0; i < n; i++) { if (p[2 * i + 1] < ymin) ymin = p[2 * i + 1]; if (p[2 * i + 1] > ymax) ymax = p[2 * i + 1]; }
    if (ymin < 0) ymin = 0;
    if (ymax >= H) ymax = H - 1;
    for (int y = ymin; y <= ymax; y++) {
        float xs[64]; int k = 0;
        float yy = y + 0.5f;
        for (int i = 0; i < n && k < 62; i++) {
            int j = (i + 1) % n;
            float y0 = p[2 * i + 1], y1 = p[2 * j + 1];
            if ((y0 <= yy && y1 > yy) || (y1 <= yy && y0 > yy))
                xs[k++] = p[2 * i] + (yy - y0) / (y1 - y0) * (p[2 * j] - p[2 * i]);
        }
        for (int a = 1; a < k; a++) { float v = xs[a]; int b = a - 1; while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; } xs[b + 1] = v; }
        for (int a = 0; a + 1 < k; a += 2)
            for (int x = (int)ceilf(xs[a] - 0.5f); x <= (int)floorf(xs[a + 1] - 0.5f); x++) plot(x, y, c);
    }
}
static void s_fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, LCDColor c) {
    int p[6] = { x1, y1, x2, y2, x3, y3 };
    s_fillPolygon(3, p, c, kPolygonFillNonZero);
}
static int s_drawText(const void *t, size_t l, PDStringEncoding e, int x, int y) { (void)t; (void)e; (void)x; (void)y; return (int)l; }
static LCDBitmapDrawMode s_setDrawMode(LCDBitmapDrawMode m) { return m; }

static struct playdate_graphics gfx;
static PlaydateAPI api;

static void dump(const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.pgm", getenv("FRAME_DIR") ? getenv("FRAME_DIR") : ".", name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P5\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) fputc(fb[y][x] ? 255 : 0, f);
    fclose(f);
}

static Game g;
static CameraPose cp;

static void shoot(const char *name) { render_frame(&api, &g, &cp, NULL); dump(name); }

static void run(int preset, bool trail, float secs, const char *tag) {
    BoatCfg bc; WeatherCfg wc; WorldCfg wo;
    boatcfg_default(&bc); weather_default(&wc); world_default(&wo);
    game_init(&g, &bc, &wc, &wo, preset, 1);
    memset(&cp, 0, sizeof cp);
    cp.trail = trail;
    camera_update(&cp, &g, &g.boat, 1.0f / 30);
    int n = (int)(secs * 30);
    float hmin = 1e9f, hmax = -1e9f, emax = 0;
    int imin = 0, imax = 0, iheel = 0;
    double sh = 0, sc = 0, shh = 0, sch = 0; int cnt = 0;
    for (int i = 0; i < n; i++) {
        game_update(&g, 0, 0, 0, 1.0f / 30);
        camera_update(&cp, &g, &g.boat, 1.0f / 30);
        if (i < 60) continue;
        float h = g.b.heave;
        if (h < hmin) { hmin = h; imin = i; }
        if (h > hmax) { hmax = h; imax = i; }
        if (fabsf(g.b.heel) > emax) { emax = fabsf(g.b.heel); iheel = i; }
        sh += h; shh += h * h; sc += cp.height; sch += cp.height * cp.height; cnt++;
    }
    double mh = sh / cnt, mc = sc / cnt;
    printf("%s: heave [%.2f,%.2f] sd boat %.3f camera %.3f  maxheel %.1f deg\n", tag, hmin, hmax,
           sqrt(shh / cnt - mh * mh), sqrt(sch / cnt - mc * mc), emax * 57.2958f);
    /* replay to the interesting frames */
    int want[3] = { imin, imax, iheel };
    const char *nm[3] = { "trough", "crest", "heel" };
    for (int k = 0; k < 3; k++) {
        game_init(&g, &bc, &wc, &wo, preset, 1);
        memset(&cp, 0, sizeof cp); cp.trail = trail;
        camera_update(&cp, &g, &g.boat, 1.0f / 30);
        for (int i = 0; i <= want[k]; i++) { game_update(&g, 0, 0, 0, 1.0f / 30); camera_update(&cp, &g, &g.boat, 1.0f / 30); }
        char nmb[128]; snprintf(nmb, sizeof nmb, "%s_%s", tag, nm[k]);
        shoot(nmb);
    }
}

static void side_views(void) {
    BoatCfg bc; WeatherCfg wc; WorldCfg wo;
    boatcfg_default(&bc); weather_default(&wc); world_default(&wo);
    const float yaws[3] = { 1.5708f, -1.5708f, 0.7854f };
    const char *nm[3] = { "side_port", "side_stbd", "side_quarter" };
    for (int k = 0; k < 3; k++) {
        game_init(&g, &bc, &wc, &wo, WEATHER_CHOPPY, 1);
        memset(&cp, 0, sizeof cp); cp.trail = true;
        g.ctl.camera_yaw = yaws[k];
        for (int i = 0; i < 300; i++) { game_update(&g, 0, 0, 0, 1.0f / 30); camera_update(&cp, &g, &g.boat, 1.0f / 30); }
        shoot(nm[k]);
    }
}

/* Boat pinned at a heel angle (deg, + = starboard rail down) so the extreme-heel look can be checked. */
static void forced_heel(const char *tag, float deg, float yaw) {
    BoatCfg bc; WeatherCfg wc; WorldCfg wo;
    boatcfg_default(&bc); weather_default(&wc); world_default(&wo);
    game_init(&g, &bc, &wc, &wo, WEATHER_CHOPPY, 1);
    memset(&cp, 0, sizeof cp); cp.trail = true;
    g.ctl.camera_yaw = yaw;
    for (int i = 0; i < 150; i++) { game_update(&g, 0, 0, 0, 1.0f / 30); camera_update(&cp, &g, &g.boat, 1.0f / 30); }
    g.b.heel = deg * 0.0174533f; g.b.heel_rate = 0;
    camera_update(&cp, &g, &g.boat, 1.0f / 30);
    shoot(tag);
}

/* The longship (square sail) steered by the autopilot at a fixed true-wind angle (deg, + = wind on
 * starboard), with its sail trimmed to trim_deg and hoisted; shot after secs of sailing. */
static void longship_scene(const char *tag, float twa_deg, float trim_deg, float secs) {
    BoatCfg bc; WeatherCfg wc; WorldCfg wo;
    boatcfg_default(&bc); weather_default(&wc); world_default(&wo);
    FILE *jf = fopen("boats/disabled/longship.json", "rb");
    if (jf) { static char buf[16384]; size_t n = fread(buf, 1, sizeof buf - 1, jf); fclose(jf); buf[n] = 0;
              char w[128]; boatcfg_load(&bc, buf, n, w, sizeof w); }
    game_init(&g, &bc, &wc, &wo, WEATHER_SUNNY, 1);
    memset(&cp, 0, sizeof cp); cp.trail = true;
    float wind_from = wrap_pi(g.wind.dir + (float)M_PI);
    g.ap.on = true;
    g.ap.target = wrap_pi(wind_from - twa_deg * 0.0174533f);
    int n = (int)(secs * 30);
    for (int i = 0; i < n; i++) {
        for (int s = 0; s < bc.sail_count; s++) { g.in.trim_target[s] = trim_deg * 0.0174533f; g.in.hoist_target[s] = 1.0f; }
        game_update(&g, 0, 0, 0, 1.0f / 30);
        camera_update(&cp, &g, &g.boat, 1.0f / 30);
    }
    shoot(tag);
}

int main(void) {
    memset(&gfx, 0, sizeof gfx);
    gfx.clear = s_clear; gfx.drawLine = s_drawLine; gfx.drawRect = s_drawRect; gfx.fillRect = s_fillRect;
    gfx.drawEllipse = s_drawEllipse; gfx.fillEllipse = s_fillEllipse; gfx.fillPolygon = s_fillPolygon;
    gfx.fillTriangle = s_fillTriangle;
    gfx.drawText = s_drawText; gfx.setDrawMode = s_setDrawMode;
    memset(&api, 0, sizeof api);
    api.graphics = &gfx;
    run(WEATHER_SUNNY, true, 20, "sunny_trail");
    run(WEATHER_CHOPPY, true, 30, "choppy_trail");
    run(WEATHER_HURRICANE, true, 30, "hurr_trail");
    run(WEATHER_CHOPPY, false, 30, "choppy_near");
    side_views();
    forced_heel("heel55_stbd", 55, 0);
    forced_heel("heel55_port", -55, 0);
    forced_heel("heel55_quarter", 55, 0.7854f);
    forced_heel("heel55_side", 55, 1.5708f);
    longship_scene("longship_reach", 90, 60, 60);
    longship_scene("longship_run", 180, 90, 60);
    longship_scene("longship_braced_stbd", 120, 40, 60);
    longship_scene("longship_braced_port", -120, 40, 60);
    return 0;
}
