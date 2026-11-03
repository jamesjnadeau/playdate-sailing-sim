/* Host Playdate API stub. Rasterizers are lifted from tools/frame_dump.c; the screen is one byte
 * per pixel (1 = white) and is packed MSB-first into the SDK's 52-byte-stride layout on demand. */
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "pdstub.h"

#define W 400
#define H 240
#define MAX_FRAMES 100000
#define MAX_PATH 512
#define MAX_MENU 16
#define MAX_DIR_NAMES 128

int eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg);

struct LCDBitmap { int w, h; uint8_t *px; };
struct PDMenuItem { int used, value; PDMenuItemCallbackFunction *cb; void *ud; };
typedef struct { FILE *fp; const char *mem; size_t len, pos; } HostFile;   /* SDFile is void in the SDK */

static uint8_t screen[H * W];       /* 1 = white */
static uint8_t packed[LCD_ROWSIZE * H];
static uint8_t *tgt = screen;       /* current draw target */
static int tw = W, th = H;
static struct { uint8_t *px; int w, h; } ctx_stack[8];
static int ctx_n;

static char out_dir[MAX_PATH], assets_dir[MAX_PATH];
static char *scen_copy;
static PDCallbackFunction *update_cb;
static void *update_ud;
static unsigned frame_no;
static struct PDMenuItem menu[MAX_MENU];

static struct playdate_sys sys_api;
static struct playdate_file file_api;
static struct playdate_display disp_api;
static struct playdate_graphics gfx_api;
static PlaydateAPI api;

/* ---- rasterizers ---- */

static int pat_white(LCDColor c, int x, int y) {
    if (c == kColorBlack) return 0;
    if (c == kColorWhite) return 1;
    const uint8_t *p = (const uint8_t *)c;
    return (p[y & 7] >> (7 - (x & 7))) & 1;
}
static void plot(int x, int y, LCDColor c) {
    if (x < 0 || y < 0 || x >= tw || y >= th) return;
    if (c != kColorBlack && c != kColorWhite && !((((const uint8_t *)c)[8 + (y & 7)] >> (7 - (x & 7))) & 1)) return;   /* masked out */
    tgt[y * tw + x] = (uint8_t)pat_white(c, x, y);
}
static void s_clear(LCDColor c) { for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) plot(x, y, c); }
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
    int ymin = th, ymax = -1;
    for (int i = 0; i < n; i++) { if (p[2 * i + 1] < ymin) ymin = p[2 * i + 1]; if (p[2 * i + 1] > ymax) ymax = p[2 * i + 1]; }
    if (ymin < 0) ymin = 0;
    if (ymax >= th) ymax = th - 1;
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
/* text is masked out of the goldens, so nothing is drawn */
/* fonts: a dummy handle, and an 8 px per character width so layout code has something to measure */
static LCDFont *s_loadFont(const char *path, const char **err) { (void)path; if (err) *err = NULL; return (LCDFont *)&gfx_api; }
static void s_setFont(LCDFont *f) { (void)f; }
static void s_setTextTracking(int t) { (void)t; }
static int s_getTextWidth(LCDFont *f, const void *t, size_t l, PDStringEncoding e, int tracking) {
    (void)f; (void)t; (void)e; return (int)l * (8 + tracking);
}
static int s_drawText(const void *t, size_t l, PDStringEncoding e, int x, int y) { (void)t; (void)e; (void)x; (void)y; return (int)l; }
static LCDBitmapDrawMode s_setDrawMode(LCDBitmapDrawMode m) { return m; }

/* ---- bitmaps and contexts ---- */

static LCDBitmap *s_newBitmap(int w, int h, LCDColor bg) {
    LCDBitmap *b = malloc(sizeof *b);
    b->w = w; b->h = h;
    b->px = malloc((size_t)w * h);
    uint8_t *old = tgt; int ow = tw, oh = th;
    tgt = b->px; tw = w; th = h;
    s_clear(bg);
    tgt = old; tw = ow; th = oh;
    return b;
}
static void s_freeBitmap(LCDBitmap *b) { if (b) { free(b->px); free(b); } }
static void s_pushContext(LCDBitmap *b) {
    if (ctx_n < 8) { ctx_stack[ctx_n].px = tgt; ctx_stack[ctx_n].w = tw; ctx_stack[ctx_n].h = th; ctx_n++; }
    if (b) { tgt = b->px; tw = b->w; th = b->h; }
    else { tgt = screen; tw = W; th = H; }
}
static void s_popContext(void) {
    if (ctx_n == 0) return;
    ctx_n--;
    tgt = ctx_stack[ctx_n].px; tw = ctx_stack[ctx_n].w; th = ctx_stack[ctx_n].h;
}
/* MSB-first, 1 = white, 52-byte rows (the last 2 bytes of each row are padding) */
static uint8_t *s_getFrame(void) {
    memset(packed, 0, sizeof packed);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        if (screen[y * W + x]) packed[y * LCD_ROWSIZE + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
    return packed;
}

/* ---- system ---- */

static void *s_realloc(void *p, size_t n) {
    if (n == 0) { free(p); return NULL; }
    return realloc(p, n);
}

static void s_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
static unsigned s_millis(void) { return frame_no * 33u; }
static void s_setUpdate(PDCallbackFunction *f, void *ud) { update_cb = f; update_ud = ud; }
static void s_buttons(PDButtons *c, PDButtons *p, PDButtons *r) { *c = *p = *r = (PDButtons)0; }   /* the scenario supplies input */
static float s_crank(void) { return 0.0f; }
static float s_elapsed(void) { return 1.0f / 30; }
static void s_resetElapsed(void) {}
static void s_setMenuImage(LCDBitmap *b, int xo) { (void)b; (void)xo; }
static PDMenuItem *new_item(PDMenuItemCallbackFunction *cb, void *ud) {
    for (int i = 0; i < MAX_MENU; i++) if (!menu[i].used) {
        menu[i].used = 1; menu[i].value = 0; menu[i].cb = cb; menu[i].ud = ud;
        return &menu[i];
    }
    return NULL;
}
static PDMenuItem *s_addMenuItem(const char *t, PDMenuItemCallbackFunction *cb, void *ud) { (void)t; return new_item(cb, ud); }
static PDMenuItem *s_addOptionsMenuItem(const char *t, const char **o, int n, PDMenuItemCallbackFunction *cb, void *ud) {
    (void)t; (void)o; (void)n;
    return new_item(cb, ud);
}
static void s_removeAllMenuItems(void) { memset(menu, 0, sizeof menu); }
static int s_getMenuItemValue(PDMenuItem *m) { return m ? m->value : 0; }
static void s_setMenuItemValue(PDMenuItem *m, int v) { if (m) m->value = v; }

/* ---- files ---- */

static void join(char *dst, const char *dir, const char *name) { snprintf(dst, MAX_PATH, "%s/%s", dir, name); }

static SDFile *f_open(const char *name, FileOptions mode) {
    HostFile *f = calloc(1, sizeof *f);
    char path[MAX_PATH];
    if (mode & (kFileWrite | kFileAppend)) {
        join(path, out_dir, name);
        f->fp = fopen(path, (mode & kFileAppend) == kFileAppend ? "ab" : "wb");
    } else if (strcmp(name, "scenario.txt") == 0) {
        f->mem = scen_copy; f->len = strlen(scen_copy);
        return f;
    } else {
        join(path, assets_dir, name);
        f->fp = fopen(path, "rb");
    }
    if (!f->fp) { free(f); return NULL; }
    return f;
}
static int f_close(SDFile *sf) {
    HostFile *f = sf;
    if (f->fp) fclose(f->fp);
    free(f);
    return 0;
}
static int f_read(SDFile *sf, void *buf, unsigned len) {
    HostFile *f = sf;
    if (f->fp) return (int)fread(buf, 1, len, f->fp);
    size_t n = f->len - f->pos;
    if (n > len) n = len;
    memcpy(buf, f->mem + f->pos, n);
    f->pos += n;
    return (int)n;
}
static int f_write(SDFile *sf, const void *buf, unsigned len) {
    HostFile *f = sf;
    return f->fp ? (int)fwrite(buf, 1, len, f->fp) : -1;
}
static int f_mkdir(const char *name) {
    char path[MAX_PATH];
    join(path, out_dir, name);
    return mkdir(path, 0755);
}
static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
/* Names come back sorted so the game's file order is the same on every host. */
static int f_listfiles(const char *dir, void (*cb)(const char *, void *), void *ud, int hidden) {
    char path[MAX_PATH], *names[MAX_DIR_NAMES];
    int n = 0;
    join(path, assets_dir, dir);
    DIR *d = opendir(path);
    if (!d) return -1;
    for (struct dirent *e; (e = readdir(d)) && n < MAX_DIR_NAMES;) {
        if (e->d_name[0] == '.' && !hidden) continue;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char full[MAX_PATH + 300];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        size_t len = strlen(e->d_name);
        int isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        names[n] = malloc(len + 2);
        memcpy(names[n], e->d_name, len);
        if (isdir) names[n][len++] = '/';   /* the device marks directories with a trailing slash */
        names[n][len] = 0;
        n++;
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], by_name);
    for (int i = 0; i < n; i++) { cb(names[i], ud); free(names[i]); }
    return 0;
}

/* ---- setup and run ---- */

PlaydateAPI *pdstub_init(const char *outdir, const char *assets_root, const char *scenario_text) {
    snprintf(out_dir, sizeof out_dir, "%s", outdir);
    snprintf(assets_dir, sizeof assets_dir, "%s", assets_root);
    scen_copy = strdup(scenario_text);
    mkdir(out_dir, 0755);

    memset(&sys_api, 0, sizeof sys_api);
    sys_api.realloc = s_realloc;
    sys_api.logToConsole = s_log;
    sys_api.getCurrentTimeMilliseconds = s_millis;
    sys_api.setUpdateCallback = s_setUpdate;
    sys_api.getButtonState = s_buttons;
    sys_api.getCrankChange = s_crank;
    sys_api.getElapsedTime = s_elapsed;
    sys_api.resetElapsedTime = s_resetElapsed;
    sys_api.setMenuImage = s_setMenuImage;
    sys_api.addMenuItem = s_addMenuItem;
    sys_api.addOptionsMenuItem = s_addOptionsMenuItem;
    sys_api.removeAllMenuItems = s_removeAllMenuItems;
    sys_api.getMenuItemValue = s_getMenuItemValue;
    sys_api.setMenuItemValue = s_setMenuItemValue;

    memset(&file_api, 0, sizeof file_api);
    file_api.open = f_open; file_api.close = f_close; file_api.read = f_read; file_api.write = f_write;
    file_api.mkdir = f_mkdir; file_api.listfiles = f_listfiles;

    memset(&disp_api, 0, sizeof disp_api);
    disp_api.setRefreshRate = (void (*)(float))s_resetElapsed;   /* ignores its argument; the frame counter is the clock */

    memset(&gfx_api, 0, sizeof gfx_api);
    gfx_api.clear = s_clear; gfx_api.drawLine = s_drawLine; gfx_api.drawRect = s_drawRect; gfx_api.fillRect = s_fillRect;
    gfx_api.drawEllipse = s_drawEllipse; gfx_api.fillEllipse = s_fillEllipse; gfx_api.fillPolygon = s_fillPolygon;
    gfx_api.fillTriangle = s_fillTriangle; gfx_api.drawText = s_drawText; gfx_api.setDrawMode = s_setDrawMode;
    gfx_api.loadFont = s_loadFont; gfx_api.setFont = s_setFont; gfx_api.setTextTracking = s_setTextTracking;
    gfx_api.getTextWidth = s_getTextWidth;
    gfx_api.newBitmap = s_newBitmap; gfx_api.freeBitmap = s_freeBitmap;
    gfx_api.pushContext = s_pushContext; gfx_api.popContext = s_popContext;
    gfx_api.getFrame = s_getFrame; gfx_api.getDisplayFrame = s_getFrame;

    memset(&api, 0, sizeof api);
    api.system = &sys_api; api.file = &file_api; api.display = &disp_api; api.graphics = &gfx_api;
    return &api;
}

int pdstub_run(void) {
    char marker[MAX_PATH];
    struct stat st;
    join(marker, out_dir, "done.marker");
    remove(marker);
    eventHandler(&api, kEventInit, 0);
    for (frame_no = 0; frame_no < MAX_FRAMES; frame_no++) {
        if (!update_cb) return 1;
        update_cb(update_ud);
        if (stat(marker, &st) == 0) return 0;
    }
    return 1;
}
