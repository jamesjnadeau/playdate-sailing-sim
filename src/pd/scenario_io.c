/* Playdate-side half of the scenario harness: reads the script, feeds scripted input,
 * and writes snapshots. Built only with -DSCENARIO. */
#ifdef SCENARIO
#include <stdio.h>
#include <string.h>
#include "scenario.h"
#include "seasound.h"

#define SCEN_TEXT_MAX 16384
#define FB_ROW_BYTES 50   /* 400 px / 8; LCD_ROWSIZE is 52 */
#define FB_ROWS 240
#define SCEN_DT (1.0f / 30)

static Scenario scen;   /* ~20 KB, too big for the stack */
static char scen_text[SCEN_TEXT_MAX];
static uint8_t fb_out[FB_ROW_BYTES * FB_ROWS];
static bool scen_ended, scen_marked;
static bool rec_on;                     /* `record NAME`: every frame goes to NAME/NNNNN.fb */
static char rec_name[SC_NAME_MAX];
static int rec_frame;

/* While recording, the game's sea sound goes to NAME/audio.pcm (44.1 kHz stereo s16le, one frame's worth per
 * frame, so it stays in step with the pictures). It is a second SeaSound, fed the same game every frame: the
 * mixer's own one renders on the audio thread at its own pace, which a recording cannot follow. */
#define REC_AUDIO_N (SEASOUND_RATE / 30)
static SeaSound rec_snd;
static SDFile *rec_audio;
static PlaydateAPI *spd;
static int16_t snd_l[REC_AUDIO_N], snd_r[REC_AUDIO_N], snd_lr[2 * REC_AUDIO_N];

static void audio_close(void) {
    if (rec_audio) spd->file->close(rec_audio);
    rec_audio = NULL;
}

static void write_all(PlaydateAPI *pd, const char *path, const void *buf, unsigned len) {
    /* NAME may carry a directory prefix; create each level, ignoring "already exists" */
    char dir[SC_NAME_MAX + 16];
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

/* note is NULL for a normal finish ("done"); otherwise the marker reads "error: <note>" so a
 * refresh script can tell a failed scenario from a finished one. */
static void write_marker(PlaydateAPI *pd, const char *note) {
    char buf[160];
    int n;
    if (scen_marked) return;
    scen_marked = true;
    n = note ? snprintf(buf, sizeof buf, "error: %s\n", note) : snprintf(buf, sizeof buf, "done\n");
    write_all(pd, "done.marker", buf, (unsigned)n);
}

void scenario_init(PlaydateAPI *pd) {
    char err[96] = "";
    spd = pd;
    seasound_init(&rec_snd);
    seasound_set_level(&rec_snd, 2);   /* the game's default, Med */
    seasound_set_active(&rec_snd, 1);
    int n = 0;
    SDFile *f = pd->file->open("scenario.txt", kFileRead | kFileReadData);
    if (f) {
        n = pd->file->read(f, scen_text, sizeof scen_text - 1);
        pd->file->close(f);
    }
    if (n <= 0) {
        pd->system->logToConsole("scenario: cannot read scenario.txt (missing or empty)");
        scen_ended = true;
        write_marker(pd, "cannot read scenario.txt (missing or empty)");
        return;
    }
    scen_text[n] = 0;
    if (!scenario_parse(&scen, scen_text, err, sizeof err)) {
        pd->system->logToConsole("scenario: %s", err);
        scen_ended = true;
        write_marker(pd, err);
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
        if (f->shot.rec == SC_REC_ON) {
            char path[SC_NAME_MAX + 16];
            rec_on = true;
            strcpy(rec_name, f->shot.rec_name);
            rec_frame = 0;
            audio_close();
            spd->file->mkdir(rec_name);
            snprintf(path, sizeof path, "%s/audio.pcm", rec_name);
            rec_audio = spd->file->open(path, kFileWrite);
            if (!rec_audio) spd->system->logToConsole("scenario: cannot write %s", path);
        } else if (f->shot.rec == SC_REC_OFF) {
            rec_on = false;
            audio_close();
        }
        return true;
    }
    scen_ended = true;
    rec_on = false;
    audio_close();
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

/* The frame just drawn, without LCD_ROWSIZE's padding; false if the API has none. */
static bool grab_frame(PlaydateAPI *pd) {
    /* getFrame, not getDisplayFrame. Measured on the SDK 3.1.2 simulator (Task 7): called at the
     * end of update(), getDisplayFrame returns the previously presented frame (a snap taken the
     * frame after a menu->play transition was still the menu, identical to the snap before it),
     * while getFrame returns the frame just drawn and matches the host stub's capture apart from
     * text (~0.9% of pixels, the stub draws none). */
    const uint8_t *src = pd->graphics->getFrame();
    if (!src) return false;
    for (int y = 0; y < FB_ROWS; y++) memcpy(fb_out + y * FB_ROW_BYTES, src + y * LCD_ROWSIZE, FB_ROW_BYTES);
    return true;
}

static void write_snapshot(PlaydateAPI *pd, const Game *g, const char *app_name, const char *name) {
    char path[SC_NAME_MAX + 8], st[512];
    if (grab_frame(pd)) {
        snprintf(path, sizeof path, "%s.fb", name);
        write_all(pd, path, fb_out, sizeof fb_out);
    }
    int n = state_text(g, app_name, st, sizeof st);
    snprintf(path, sizeof path, "%s.state", name);
    write_all(pd, path, st, (unsigned)n);
}

void scenario_frame_done(PlaydateAPI *pd, const Game *g, const char *app_name) {
    char name[SC_NAME_MAX];
    /* the sound runs every frame, so its smoothing and fades are settled when a recording starts */
    if (!strcmp(app_name, "play")) seasound_update(&rec_snd, g, SCEN_DT);
    else seasound_quiet(&rec_snd);
    if (!seasound_render(&rec_snd, snd_l, snd_r, REC_AUDIO_N)) {
        memset(snd_l, 0, sizeof snd_l);
        memset(snd_r, 0, sizeof snd_r);
    }
    if (rec_on && rec_audio && !scen_ended) {
        for (int i = 0; i < REC_AUDIO_N; i++) {
            snd_lr[2 * i] = snd_l[i];
            snd_lr[2 * i + 1] = snd_r[i];
        }
        pd->file->write(rec_audio, snd_lr, sizeof snd_lr);
    }
    if (rec_on && !scen_ended && grab_frame(pd)) {
        char path[SC_NAME_MAX + 16];
        snprintf(path, sizeof path, "%s/%05d.fb", rec_name, rec_frame++ % 100000);
        write_all(pd, path, fb_out, sizeof fb_out);
    }
    if (scen_ended) { write_marker(pd, NULL); return; }
    for (;;) {
        ScAfterKind k = scenario_after(&scen, name);
        if (k == SC_AFTER_NONE) break;
        if (k == SC_AFTER_END) {
            scen_ended = true;
            write_marker(pd, NULL);
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

#endif /* SCENARIO */
