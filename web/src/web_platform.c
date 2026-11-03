/* The Platform for the browser build: stdio over Emscripten's file system. Shipped files are preloaded
 * under /pdx; saved data (best times, ghosts) lives in /save, an IndexedDB-backed mount that is flushed
 * after every write. The same file also builds natively for web/test/parity.c (WEB_NATIVE), with the
 * shipped files read from the repository and saves going to a scratch directory. */
#include "web.h"
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seasound.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static char bundle_root[256] = "/pdx";
static char save_root[256] = "/save";
static int dirty;   /* a write closed since the last flush */

void web_platform_roots(const char *bundle, const char *save) {
    snprintf(bundle_root, sizeof bundle_root, "%s", bundle);
    snprintf(save_root, sizeof save_root, "%s", save);
}

typedef struct { FILE *f; int writing; } WFile;

static void *pf_open(const char *path, PlatMode mode) {
    char full[512];
    snprintf(full, sizeof full, "%s/%s", mode == PLAT_READ ? bundle_root : save_root, path);
    FILE *f = fopen(full, mode == PLAT_WRITE ? "wb" : "rb");
    if (!f) return NULL;
    WFile *w = malloc(sizeof *w);
    if (!w) { fclose(f); return NULL; }
    w->f = f;
    w->writing = mode == PLAT_WRITE;
    return w;
}

static int pf_read(void *h, void *buf, unsigned len) {
    WFile *w = h;
    size_t n = fread(buf, 1, len, w->f);
    return ferror(w->f) ? -1 : (int)n;
}

static int pf_write(void *h, const void *buf, unsigned len) {
    WFile *w = h;
    return (int)fwrite(buf, 1, len, w->f);
}

static void pf_close(void *h) {
    WFile *w = h;
    fclose(w->f);
    if (w->writing) dirty = 1;
    free(w);
}

static void pf_list(const char *dir, void (*cb)(const char *, void *), void *ud) {
    char full[512];
    snprintf(full, sizeof full, "%s/%s", bundle_root, dir);
    DIR *d = opendir(full);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        cb(e->d_name, ud);
    }
    closedir(d);
}

static void *pf_realloc(void *ptr, size_t size) {
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

static void pf_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* The page passes a seed base from the clock once; each run then takes the next one, as on the Playdate. */
static uint32_t seed_base = 12345u;
void web_platform_seed_base(uint32_t base) { seed_base = base; }
static uint32_t pf_seed(uint32_t counter) { return seed_base * 2654435761u + counter * 40503u; }

static SeaSound sea_sound;
static void au_level(int level) { seasound_set_level(&sea_sound, level); }
static void au_active(bool active) { seasound_set_active(&sea_sound, active); }
static void au_update(const Game *g, float dt) {
    if (g) seasound_update(&sea_sound, g, dt);
    else seasound_quiet(&sea_sound);
}
int web_audio_fill(int16_t *left, int16_t *right, int n) { return seasound_render(&sea_sound, left, right, n); }
void web_audio_active(bool on) { seasound_set_active(&sea_sound, on); }

static void pf_sys_menu(bool racing, int boat, int preset) {
    (void)racing; (void)boat; (void)preset;   /* the page reads app_sys_racing() each frame */
}

const Platform web_platform = {
    .open = pf_open, .read = pf_read, .write = pf_write, .close = pf_close, .listfiles = pf_list,
    .realloc = pf_realloc, .log = pf_log, .seed = pf_seed, .reset_time = NULL,
    .audio_set_level = au_level, .audio_set_active = au_active, .audio_update = au_update,
    .sys_menu = pf_sys_menu,
};

void web_platform_init(void) { seasound_init(&sea_sound); }

/* True once after any save was written; the page then flushes /save to IndexedDB. */
int web_platform_take_dirty(void) {
    int d = dirty;
    dirty = 0;
    return d;
}
