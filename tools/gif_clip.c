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
static int s_drawText(const void *t, size_t l, PDStringEncoding e, int x, int y) { (void)t; (void)e; (void)x; (void)y; return (int)l; }
static LCDBitmapDrawMode s_setDrawMode(LCDBitmapDrawMode m) { return m; }





static void s_fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, LCDColor c) {
    int p[6] = { x1, y1, x2, y2, x3, y3 };
    s_fillPolygon(3, p, c, kPolygonFillNonZero);
}
static struct playdate_graphics gfx_;
static Game g;
static CameraPose cp;
static void dump_frame(const char *dir, int i) {
    char path[512];
    snprintf(path, sizeof path, "%s/f%03d.pgm", dir, i);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P5\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) fputc(fb[y][x] ? 255 : 0, f);
    fclose(f);
}

/* Scripted promo clip: sail, switch to camera mode, pan, hold. Env: BOAT (boat json, default
 * boats/j24.json), FRAME_DIR, PRESET,
 * WIND (mean m/s override), REL (heading off the wind, deg, + = wind on the starboard side), PAN_DEG, FPS. */
int main(void) {
    const char *dir = getenv("FRAME_DIR") ? getenv("FRAME_DIR") : ".";
    int preset = getenv("PRESET") ? atoi(getenv("PRESET")) : WEATHER_STORM;
    float rel = getenv("REL") ? (float)atof(getenv("REL")) : 90.0f;
    float pan = getenv("PAN_DEG") ? (float)atof(getenv("PAN_DEG")) : 120.0f;
    int fps = getenv("FPS") ? atoi(getenv("FPS")) : 20;
    float dt = 1.0f / fps;

    gfx_.clear = s_clear; gfx_.drawLine = s_drawLine; gfx_.drawRect = s_drawRect; gfx_.fillRect = s_fillRect;
    gfx_.drawEllipse = s_drawEllipse; gfx_.fillEllipse = s_fillEllipse; gfx_.fillPolygon = s_fillPolygon;
    gfx_.fillTriangle = s_fillTriangle; gfx_.drawText = s_drawText; gfx_.setDrawMode = s_setDrawMode;
    static PlaydateAPI a; a.graphics = &gfx_;

    BoatCfg bc; WeatherCfg wc; WorldCfg wo;
    boatcfg_default(&bc); weather_default(&wc); world_default(&wo);
    const char *boat_path = getenv("BOAT") ? getenv("BOAT") : "boats/j24.json";
    FILE *jf = fopen(boat_path, "rb");
    if (!jf) { fprintf(stderr, "gif_clip: cannot open boat file %s\n", boat_path); return 1; }
    static char buf[1 << 16]; size_t n = fread(buf, 1, sizeof buf - 1, jf); fclose(jf); buf[n] = 0;
    char w[128]; boatcfg_load(&bc, buf, n, w, sizeof w);
    game_init(&g, &bc, &wc, &wo, preset, 1);
    if (getenv("WIND")) wind_set_speed(&g.wind, &g.weather.preset[g.preset], (float)atof(getenv("WIND")));
    memset(&cp, 0, sizeof cp); cp.trail = true;

    /* hold a fixed heading off the wind with the autopilot; sails stay fully hoisted, trimmed for drive */
    float wind_from = wrap_pi(g.wind.dir + (float)M_PI);
    g.ap.on = true;
    g.ap.target = wrap_pi(wind_from + rel * 0.0174533f);
    const int warm = 40 * fps, n1 = 3 * fps, n2 = 6 * fps, n3 = 3 * fps;
    int frame = 0;
    float panned = 0, minheel = 1e9f; float maxluff = 0;
    for (int i = 0; i < warm + n1 + n2 + n3; i++) {
        uint32_t pressed = 0; float crank = 0;
        int k = i - warm;                       /* clip frame */
        if (k == n1) pressed = BTN_DOWN;        /* switch to camera mode */
        if (k >= n1 && k < n1 + n2) {           /* eased pan: smoothstep of progress */
            float u0 = (float)(k - n1) / n2, u1 = (float)(k - n1 + 1) / n2;
            float s0 = u0 * u0 * (3 - 2 * u0), s1 = u1 * u1 * (3 - 2 * u1);
            float target_deg = (s1 - s0) * pan;               /* camera yaw this frame */
            crank = target_deg / (bc.controls.crank_gain_camera);
            panned += target_deg;
        }
        float awa = g.b.awa;
        for (int s = 0; s < bc.sail_count; s++) {
            float t = autotrim_best_trim(&bc.sails[s], awa, g.b.aws, g.b.heel);
            if (t >= 0) g.in.trim_target[s] = t;
            g.in.hoist_target[s] = 1.0f;
        }
        game_update(&g, pressed, 0, crank, dt);
        camera_update(&cp, &g, &g.boat, dt);
        if (k >= 0) {
            float h = fabsf(g.b.heel) * 57.2958f;
            if (h < minheel) minheel = h;
            for (int s = 0; s < bc.sail_count; s++) if (g.b.luff[s] > maxluff) maxluff = g.b.luff[s];
            if (k % fps == 0) printf("t=%4.1f heel %5.1f aws %4.1f u %4.1f yaw %4.0f luff %.2f/%.2f run %d\n", (double)k / fps,
                (double)(g.b.heel * 57.2958f), (double)g.b.aws, (double)g.b.u, (double)(g.ctl.camera_yaw * 57.2958f),
                (double)g.b.luff[0], (double)g.b.luff[1], (int)g.run);
            render_frame(&a, &g, &cp);
            dump_frame(dir, frame++);
        }
    }
    printf("frames %d, min |heel| %.1f deg, max luff %.2f, panned %.0f\n", frame, (double)minheel, (double)maxluff, (double)panned);
    return 0;
}
