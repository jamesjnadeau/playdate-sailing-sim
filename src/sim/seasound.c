#include "seasound.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

#define DECIM 2                /* output samples per internal sample: the voices run at 22,050 Hz */
#define CHUNK 32               /* internal samples per filter-coefficient update */
#define SLAP_ON 0.15f          /* rad/s of bow-down pitch rate that arms a slap */
#define SLAP_REF 0.6f          /* rad/s that gives a full-strength slap */
#define SLAP_COOL_S 0.35f
#define BED 0.4f   /* scales the constant background voices; slaps and thumps stay at full weight */
#define RATE ((float)SEASOUND_RATE / DECIM)   /* the internal rate */
#define TWO_PI 6.28318530718f

static const float LEVEL_GAIN[SEASOUND_LEVELS] = { 0.0f, 0.35f, 0.65f, 1.0f };

static float fin(float v) { return isfinite(v) ? v : 0.0f; }

static void retarget(SeaSound *s) { s->master_target = s->active ? LEVEL_GAIN[s->level] : 0.0f; }

void seasound_init(SeaSound *s) {
    memset(s, 0, sizeof *s);
    s->rng = 0x2545F491u;
    s->level = 2;
    s->active = 1;
    s->flut_len = 1.0f;
    s->k_fast = 1.0f - expf(-(float)CHUNK / (RATE * 0.06f));
    s->k_master = 1.0f - expf(-(float)CHUNK / (RATE * 0.25f));
    retarget(s);
}

void seasound_set_level(SeaSound *s, int level) {
    s->level = level < 0 ? 0 : level >= SEASOUND_LEVELS ? SEASOUND_LEVELS - 1 : level;
    retarget(s);
}

void seasound_set_active(SeaSound *s, int active) {
    s->active = active != 0;
    retarget(s);
}

void seasound_quiet(SeaSound *s) {
    memset(&s->p, 0, sizeof s->p);
    s->primed = 0;
}

void seasound_update(SeaSound *s, const Game *g, float dt) {
    if (!(dt > 1e-4f) || !(dt < 1.0f)) dt = 1.0f / 30;
    const BoatState *b = &g->b;
    SoundParams *p = &s->p;
    const float speed = hypotf(fin(b->u), fin(b->v));
    const float sp = clampf(speed / fmaxf(g->boat.hull.hull_speed, 0.1f), 0.0f, 1.3f);
    const float alive = g->run == RUN_SAILING ? 1.0f : 0.25f;

    /* sea state: heave and pitch rates of the hull, smoothed over a few frames */
    float amp = 0.0f;
    for (int i = 0; i < g->sea.n; i++) amp += g->sea.c[i].amp;
    const float period = fmaxf(g->sea.period, 1.0f);
    const float wave_hr = fmaxf(amp * TWO_PI / period, 0.05f);   /* peak heave rate of the main swell */
    float hv = 0.0f, pr = 0.0f;
    if (s->primed) {
        hv = (fin(g->wf.heave) - s->prev_heave) / dt;
        pr = (fin(g->wf.pitch) - s->prev_pitch) / dt;
    }
    s->primed = 1;
    s->prev_heave = fin(g->wf.heave);
    s->prev_pitch = fin(g->wf.pitch);
    const float k = clampf(dt / 0.12f, 0.0f, 1.0f);
    s->hv += (hv - s->hv) * k;
    s->pr += (pr - s->pr) * k;

    p->bright = 0.15f + 0.85f * clampf(sp, 0.0f, 1.0f);
    const float phase = 0.5f + 0.5f * clampf(s->hv / wave_hr, -1.0f, 1.0f);   /* once per wave: water pushes up, then drains away */
    p->swell = (0.1f + 0.9f * clampf(amp / 1.5f, 0.0f, 1.0f)) * (0.1f + 0.9f * phase) * (0.5f + 0.5f * alive);
    p->wash = alive * powf(clampf(sp, 0.0f, 1.0f), 1.3f) * (0.4f + 0.6f * phase);   /* the wash breathes with each wave */

    /* the bow falls into a trough and meets the next face: fire when the fall eases off */
    s->cool -= dt;
    if (s->pr < s->pr_peak) s->pr_peak = s->pr;
    if (s->pr > -0.4f * SLAP_ON && s->pr_peak < -SLAP_ON) {
        if (s->cool <= 0.0f && sp > 0.12f) {
            float e = clampf(-s->pr_peak / SLAP_REF, 0.2f, 1.0f) * (0.4f + 0.6f * clampf(sp, 0.0f, 1.0f));
            if (e > s->slap_ev) s->slap_ev = e;
            s->cool = SLAP_COOL_S;
        }
        s->pr_peak = 0.0f;
    } else if (s->pr > 0.0f && s->pr_peak > -SLAP_ON) {
        s->pr_peak = 0.0f;
    }

    /* heel: the lee rail dips toward the water */
    const float heel = fin(b->heel);
    p->gurgle = alive * clampf((fabsf(heel) - 0.08f) / 0.35f, 0.0f, 1.0f) * (0.3f + 0.7f * clampf(sp, 0.0f, 1.0f));
    p->pan = clampf(heel / 0.4f, -1.0f, 1.0f) * 0.7f;

    const float aws = fmaxf(fin(b->aws), 0.0f);
    p->wind = alive * powf(clampf(aws / 18.0f, 0.0f, 1.0f), 1.2f);
    p->whistle_hz = clampf(350.0f + 45.0f * aws, 350.0f, 1800.0f);

    float luff = 0.0f;
    for (int i = 0; i < BOAT_MAX_SAILS; i++) {
        float l = clampf(fin(b->luff[i]), 0.0f, 1.0f) * clampf(fin(b->hoists[i]), 0.0f, 1.0f);
        if (l > luff) luff = l;
    }
    p->flutter = alive * luff * clampf(aws / 5.0f, 0.0f, 1.0f);
}

static float rnd(SeaSound *s) {
    uint32_t x = s->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s->rng = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

/* One-pole coefficient for a cutoff, and the gain that brings unit-uniform white noise through two
   cascaded such poles back to about unit rms (variance a(1+r^2)/(2-a)^3 for r = 1-a). */
static float lp_a(float fc) { return 1.0f - expf(-TWO_PI * clampf(fc, 20.0f, 9000.0f) / RATE); }
static float lp2_norm(float a) {
    const float r = 1.0f - a;
    return 1.0f / (0.577f * sqrtf(a * (1.0f + r * r) / ((2.0f - a) * (2.0f - a) * (2.0f - a))));
}
static float lp2(float st[2], float a, float x) {
    st[0] += a * (x - st[0]);
    st[1] += a * (st[0] - st[1]);
    return st[1];
}

/* Per chunk: smooth the levels toward their targets, take new events and work out the coefficients. */
static void chunk_begin(SeaSound *s) {
    SoundParams *c = &s->cur;
    const SoundParams *t = &s->p;
    const float kf = s->k_fast;
    c->wash += (t->wash - c->wash) * kf;
    c->bright += (t->bright - c->bright) * kf;
    c->swell += (t->swell - c->swell) * kf;
    c->gurgle += (t->gurgle - c->gurgle) * kf;
    c->pan += (t->pan - c->pan) * kf;
    c->wind += (t->wind - c->wind) * kf;
    c->whistle_hz += (t->whistle_hz - c->whistle_hz) * kf;
    c->flutter += (t->flutter - c->flutter) * kf;
    s->master += (s->master_target - s->master) * s->k_master;

    float ev = s->slap_ev;
    if (ev > 0.0f) { s->slap_ev = 0.0f; if (ev > s->slap_env) s->slap_env = ev; }
    ev = s->thump_ev;
    if (ev > 0.0f) { s->thump_ev = 0.0f; if (ev > s->thump_env) s->thump_env = ev; }

    __typeof__(s->k) *k = &s->k;
    k->a_wash = lp_a(300.0f + 4500.0f * c->bright * c->bright); k->g_wash = lp2_norm(k->a_wash);
    k->a_swell = lp_a(150.0f + 1200.0f * c->swell); k->g_swell = lp2_norm(k->a_swell);
    k->a_slap = lp_a(250.0f + 1100.0f * s->slap_env); k->g_slap = lp2_norm(k->a_slap);
    k->a_thump = lp_a(130.0f); k->g_thump = lp2_norm(k->a_thump);
    k->a_gur = lp_a(350.0f); k->g_gur = lp2_norm(k->a_gur);
    k->a_burble = lp_a(7.0f); k->g_burble = 1.0f / (0.577f * sqrtf(k->a_burble / (2.0f - k->a_burble)));
    k->a_wob = lp_a(1.5f); k->g_wob = 1.0f / (0.577f * sqrtf(k->a_wob / (2.0f - k->a_wob)));
    k->slap_d = expf(-(float)CHUNK / (RATE * 0.3f));
    k->thump_d = expf(-(float)CHUNK / (RATE * 0.4f));
    k->gl = 0.5f * (1.0f - c->pan); k->gr = 0.5f * (1.0f + c->pan);
    k->flut_d = expf(-1.0f / (RATE * 0.16f));   /* per sample */
    k->flut_hz = 2.5f + 2.0f * c->flutter;   /* slow, uneven flaps: cloth, not a rattle */

    /* rigging whistle: a narrow band of noise whose pitch wanders with the gusts, retuned once per chunk */
    const float fc = c->whistle_hz * (1.0f + 0.06f * clampf(s->wobble * k->g_wob, -1.5f, 1.5f));
    k->f = 2.0f * sinf(3.14159265f * clampf(fc, 100.0f, 4000.0f) / RATE);
    k->g_wi = 0.5f / sqrtf(k->f * 6.0f) * 0.577f * 2.0f;

    /* voices at zero are skipped for the chunk; their filters hold state and they wake on a ramp */
    k->on_wash = c->wash >= 1e-4f; k->on_swell = c->swell >= 1e-4f;
    k->on_slap = s->slap_env >= 1e-4f || s->slap_amp >= 1e-4f;
    k->on_thump = s->thump_env >= 1e-4f || s->thump_amp >= 1e-4f;
    k->on_gur = c->gurgle >= 1e-4f; k->on_wind = c->wind >= 1e-4f; k->on_flut = c->flutter >= 1e-4f;
}

static void chunk_end(SeaSound *s) {
    s->slap_env *= s->k.slap_d;
    s->thump_env *= s->k.thump_d;
    s->slap_amp += 0.05f * (s->slap_env - s->slap_amp);     /* ~15 ms attack */
    s->thump_amp += 0.03f * (s->thump_env - s->thump_amp);  /* ~25 ms attack */
    if (s->slap_env < 1e-4f) s->slap_env = 0.0f;
    if (s->thump_env < 1e-4f) s->thump_env = 0.0f;
}

/* One internal sample of every voice, mixed, through the master gain and soft-clip. */
static void synth_one(SeaSound *s, float *out_l, float *out_r) {
    const SoundParams *c = &s->cur;
    const __typeof__(s->k) *k = &s->k;
    const float n0 = rnd(s), n1 = rnd(s), n2 = rnd(s);   /* always drawn, so each voice hears the same noise */
    float wl = 0.0f, wr = 0.0f, mono = 0.0f, gu = 0.0f;
    if (k->on_wash) {
        wl = lp2(s->wash_s[0], k->a_wash, n0) * k->g_wash * c->wash * 0.22f * BED;
        wr = lp2(s->wash_s[1], k->a_wash, n1) * k->g_wash * c->wash * 0.22f * BED;
    }
    if (k->on_swell) mono += lp2(s->swell_s, k->a_swell, n2) * k->g_swell * c->swell * 0.2f * BED;
    if (k->on_slap) mono += lp2(s->slap_s, k->a_slap, n0) * k->g_slap * s->slap_amp * 0.3f;
    if (k->on_thump) mono += lp2(s->thump_s, k->a_thump, n1) * k->g_thump * s->thump_amp * 0.5f;

    s->burble += k->a_burble * (n2 - s->burble);
    if (k->on_gur) {
        const float bu = clampf(0.5f + 0.5f * s->burble * k->g_burble, 0.0f, 1.0f);
        gu = lp2(s->gur_s, k->a_gur, n0) * k->g_gur * bu * bu * c->gurgle * 0.25f * BED;
    }

    s->wobble += k->a_wob * (n1 - s->wobble);
    if (k->on_wind) {
        s->svf_low += k->f * s->svf_band;
        const float high = n2 - s->svf_low - s->svf_band * (1.0f / 6.0f);
        s->svf_band += k->f * high;
        mono += s->svf_band * k->g_wi * c->wind * 0.55f * BED;
    }

    /* luffing sail: each flap is a snap of low-passed noise that dies away like cloth going slack.
       The phase and gate run even while silent, so flaps don't bunch up when it resumes. */
    s->flut_phase += k->flut_hz * s->flut_len / RATE;
    if (s->flut_phase >= 1.0f) {
        s->flut_phase -= 1.0f;
        s->flut_gate = 0.5f + 0.5f * (0.5f + 0.5f * n2);
        s->flut_len = 0.7f + 0.6f * (0.5f + 0.5f * n1);
    }
    s->flut_gate *= k->flut_d;
    if (k->on_flut) {
        /* per-sample coefficients at the internal rate: 1 - (1 - a)^DECIM of the 44.1 kHz values in brackets */
        s->flut_amp += 0.0013995f * (s->flut_gate - s->flut_amp);   /* (0.0007) ~30 ms swell into each flap: a flop, not a click */
        s->flut_lp += 0.043516f * (n0 - s->flut_lp);                /* (0.022) ~150 Hz billow of cloth */
        s->flut_body += 0.023856f * (n1 - s->flut_body);            /* (0.012) ~80 Hz thud of the sail filling and dropping */
        /* 0.7071 = 1/sqrt(DECIM): these filters aren't normalised, and white noise at half the rate has twice the power per Hz */
        mono += (s->flut_lp * 6.0f + s->flut_body * 14.0f) * s->flut_amp * c->flutter * 0.85f * 0.7071f * BED;
    }

    float l = mono + wl + gu * k->gl * 2.0f;
    float r = mono + wr + gu * k->gr * 2.0f;
    l *= s->master; r *= s->master;
    *out_l = l / (1.0f + 0.4f * fabsf(l));
    *out_r = r / (1.0f + 0.4f * fabsf(r));
}

int seasound_render(SeaSound *s, int16_t *left, int16_t *right, int n) {
    if (s->master_target <= 0.0f && s->master < 1e-4f) {
        s->master = 0.0f;
        s->out_prev[0] = s->out_prev[1] = s->out_cur[0] = s->out_cur[1] = 0.0f;   /* resume ramps up from silence */
        s->sub = 0;
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (s->sub == 0) {
            if (s->chunk_left == 0) { chunk_begin(s); s->chunk_left = CHUNK; }
            s->out_prev[0] = s->out_cur[0]; s->out_prev[1] = s->out_cur[1];
            synth_one(s, &s->out_cur[0], &s->out_cur[1]);
            if (--s->chunk_left == 0) chunk_end(s);
        }
        /* linear interpolation up to the output rate; the last sub-sample lands exactly on out_cur */
        const float w = (float)(s->sub + 1) / DECIM;
        left[i] = (int16_t)(clampf(s->out_prev[0] + (s->out_cur[0] - s->out_prev[0]) * w, -1.0f, 1.0f) * 30000.0f);
        right[i] = (int16_t)(clampf(s->out_prev[1] + (s->out_cur[1] - s->out_prev[1]) * w, -1.0f, 1.0f) * 30000.0f);
        if (++s->sub == DECIM) s->sub = 0;
    }
    return 1;
}
