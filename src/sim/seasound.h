#ifndef SEASOUND_H
#define SEASOUND_H
#include <stdint.h>
#include "game.h"

/* Procedural sea sound: the mapping from the sim state to a few mixer levels (seasound_update,
   once per frame) and the filtered-noise voices that render them (seasound_render, on the audio
   thread). No Playdate API in here, so it is unit-tested on the host. */

#define SEASOUND_RATE 44100
#define SEASOUND_LEVELS 4   /* Off, Low, Med, High */

typedef struct {
    float wash;         /* 0..1 hull and bow wash, grows with speed */
    float bright;       /* 0..1 how much of the wash is hiss rather than rumble */
    float swell;        /* 0..1 the swell passing under the boat; breathes at the wave period */
    float gurgle;       /* 0..1 water along the lee rail when heeled */
    float pan;          /* -1 port .. +1 starboard: the side the gurgle sits on */
    float wind;         /* 0..1 wind in the rigging */
    float whistle_hz;   /* centre of the rigging whistle */
    float flutter;      /* 0..1 luffing sails */
} SoundParams;

typedef struct {
    /* update side */
    SoundParams p;
    volatile float slap_ev, thump_ev;   /* one-shot energies from the update side; the renderer takes and clears them */
    volatile float master_target;       /* 0..1 */
    int level, active;
    float prev_heave, prev_pitch, hv, pr, pr_peak, cool;
    int primed;
    HitKind prev_hit;
    /* render side */
    uint32_t rng;
    float master;
    SoundParams cur;    /* p, smoothed */
    float wash_s[2][2], swell_s[2], slap_s[2], thump_s[2], gur_s[2], burble;
    float svf_low, svf_band, wobble, flut_phase, flut_gate, flut_lp, flut_body, flut_amp, flut_len;
    float slap_env, thump_env;
    float k_fast, k_master;
    float out_prev[2], out_cur[2];   /* the last two internal stereo samples, after master and soft-clip, -1..1 */
    int sub;                         /* output position within the current internal sample */
    int chunk_left;                  /* internal samples left before the next coefficient update */
    struct {                         /* per-chunk coefficients, kept here because a chunk can span two calls */
        float a_wash, g_wash, a_swell, g_swell, a_slap, g_slap, a_thump, g_thump, a_gur, g_gur;
        float a_burble, g_burble, a_wob, g_wob, slap_d, thump_d, flut_d, flut_hz, gl, gr, f, g_wi;
        int on_wash, on_swell, on_slap, on_thump, on_gur, on_wind, on_flut;
    } k;
} SeaSound;

void seasound_init(SeaSound *s);
/* level 0..SEASOUND_LEVELS-1; out of range is clamped. 0 silences. */
void seasound_set_level(SeaSound *s, int level);
/* Fades everything out (or back in to the set level) without losing the mixer state. */
void seasound_set_active(SeaSound *s, int active);
/* Reads the sim after game_update; dt is the frame time in seconds. */
void seasound_update(SeaSound *s, const Game *g, float dt);
/* Zeros the mixer levels, for screens with no sea (menus). */
void seasound_quiet(SeaSound *s);
/* Fills n samples per channel. Returns 0 when it is silent (the buffers are then left alone). */
int seasound_render(SeaSound *s, int16_t *left, int16_t *right, int n);

#endif
