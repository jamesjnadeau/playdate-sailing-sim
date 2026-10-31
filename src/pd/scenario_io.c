/* Playdate-side half of the scenario harness: reads the script, feeds scripted input,
 * and writes snapshots. Built only with -DSCENARIO. */
#include <stdio.h>
#include <string.h>
#include "scenario.h"

#define SCEN_TEXT_MAX 16384
#define FB_ROW_BYTES 50   /* 400 px / 8; LCD_ROWSIZE is 52 */
#define FB_ROWS 240
#define SCEN_DT (1.0f / 30)

static Scenario scen;   /* ~20 KB, too big for the stack */
static char scen_text[SCEN_TEXT_MAX];
static uint8_t fb_out[FB_ROW_BYTES * FB_ROWS];
static bool scen_ended, scen_marked;

static void write_all(PlaydateAPI *pd, const char *path, const void *buf, unsigned len) {
    /* NAME may carry a directory prefix; create each level, ignoring "already exists" */
    char dir[96];
    for (size_t i = 0; path[i] && i < sizeof dir - 1; i++) {
        if (path[i] != '/') continue;
        memcpy(dir, path, i);
        dir[i] = 0;
        pd->file->mkdir(dir);
    }
    SDFile *f = pd->file->open(path, kFileWrite);
    if (!f) {
        pd->system->logToConsole("scenario: cannot write %s", path);
        return;
    }
    pd->file->write(f, buf, len);
    pd->file->close(f);
}

static void write_marker(PlaydateAPI *pd) {
    if (scen_marked) return;
    scen_marked = true;
    write_all(pd, "done.marker", "done\n", 5);
}

void scenario_init(PlaydateAPI *pd) {
    char err[96] = "";
    int n = 0;
    SDFile *f = pd->file->open("scenario.txt", kFileRead | kFileReadData);
    if (f) {
        n = pd->file->read(f, scen_text, sizeof scen_text - 1);
        pd->file->close(f);
    }
    if (n <= 0) {
        pd->system->logToConsole("scenario: cannot read scenario.txt");
        scen_ended = true;
        write_marker(pd);
        return;
    }
    scen_text[n] = 0;
    if (!scenario_parse(&scen, scen_text, err, sizeof err)) {
        pd->system->logToConsole("scenario: %s", err);
        scen_ended = true;
        write_marker(pd);
    }
}

bool scenario_poll_input(PDButtons *cur, PDButtons *pushed, PDButtons *released, float *crank, float *dt, ScFrame *f) {
    memset(f, 0, sizeof *f);
    *dt = SCEN_DT;
    if (!scen_ended && scenario_poll(&scen, f)) {
        *cur = (PDButtons)f->cur;
        *pushed = (PDButtons)f->pushed;
        *released = (PDButtons)f->released;
        *crank = f->crank;
        return true;
    }
    scen_ended = true;
    memset(f, 0, sizeof *f);
    *cur = *pushed = *released = (PDButtons)0;
    *crank = 0.0f;
    return false;
}

static int state_text(const Game *g, const char *app_name, char *st, size_t n) {
    return snprintf(st, n,
                    "app=%s\nmode=%d\nrun=%d\nt=%.4f\nheading=%.4f\nspeed=%.4f\nheel=%.4f\nawa=%.4f\naws=%.4f\nwheel=%.4f\nx=%.3f\ny=%.3f\n",
                    app_name, (int)g->mode, (int)g->run, g->t, g->b.heading, g->b.u, g->b.heel, g->b.awa, g->b.aws,
                    g->b.wheel, g->b.x, g->b.y);
}

static void write_snapshot(PlaydateAPI *pd, const Game *g, const char *app_name, const char *name) {
    const uint8_t *src = pd->graphics->getDisplayFrame();
    char path[SC_NAME_MAX + 8], st[512];
    if (src) {
        for (int y = 0; y < FB_ROWS; y++) memcpy(fb_out + y * FB_ROW_BYTES, src + y * LCD_ROWSIZE, FB_ROW_BYTES);
        snprintf(path, sizeof path, "%s.fb", name);
        write_all(pd, path, fb_out, sizeof fb_out);
    }
    int n = state_text(g, app_name, st, sizeof st);
    snprintf(path, sizeof path, "%s.state", name);
    write_all(pd, path, st, (unsigned)n);
}

void scenario_frame_done(PlaydateAPI *pd, const Game *g, const char *app_name) {
    char name[SC_NAME_MAX];
    if (scen_ended) { write_marker(pd); return; }
    for (;;) {
        ScAfterKind k = scenario_after(&scen, name);
        if (k == SC_AFTER_NONE) break;
        if (k == SC_AFTER_END) {
            scen_ended = true;
            write_marker(pd);
            break;
        }
        if (k == SC_AFTER_SNAP) {
            write_snapshot(pd, g, app_name, name);
        } else {   /* SC_AFTER_STATE has no name: the state goes to the log */
            char st[512];
            state_text(g, app_name, st, sizeof st);
            pd->system->logToConsole("scenario state:\n%s", st);
        }
    }
}

uint32_t scenario_seed(uint32_t counter) {
    return counter * 2654435761u + 12345u;
}
