#include "seasound.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

#define CHUNK 32               /* samples per filter-coefficient update */
#define SLAP_ON 0.15f          /* rad/s of bow-down pitch rate that arms a slap */
#define SLAP_REF 0.6f          /* rad/s that gives a full-strength slap */
#define SLAP_COOL_S 0.35f
#define BED 0.4f   /* scales the constant background voices; slaps and thumps stay at full weight */
#define RATE ((float)SEASOUND_RATE)
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

    /* contact with a rock */
    if (g->last_hit != HIT_NONE && s->prev_hit == HIT_NONE) {
        float e = g->last_hit == HIT_WRECK ? 1.0f : g->last_hit == HIT_STRIKE ? 0.8f : 0.5f;
        if (e > s->thump_ev) s->thump_ev = e;
    }
    s->prev_hit = g->last_hit;
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

int seasound_render(SeaSound *s, int16_t *left, int16_t *right, int n) {
    if (s->master_target <= 0.0f && s->master < 1e-4f) {
        s->master = 0.0f;
        return 0;
    }
    SoundParams *c = &s->cur;
    for (int base = 0; base < n; base += CHUNK) {
        const int len = n - base < CHUNK ? n - base : CHUNK;
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

        const float a_wash = lp_a(300.0f + 4500.0f * c->bright * c->bright), g_wash = lp2_norm(a_wash);
        const float a_swell = lp_a(150.0f + 1200.0f * c->swell), g_swell = lp2_norm(a_swell);
        const float a_slap = lp_a(400.0f + 2800.0f * s->slap_env), g_slap = lp2_norm(a_slap);
        const float a_thump = lp_a(130.0f), g_thump = lp2_norm(a_thump);
        const float a_gur = lp_a(350.0f), g_gur = lp2_norm(a_gur);
        const float a_burble = lp_a(7.0f), g_burble = 1.0f / (0.577f * sqrtf(a_burble / (2.0f - a_burble)));
        const float a_wob = lp_a(1.5f), g_wob = 1.0f / (0.577f * sqrtf(a_wob / (2.0f - a_wob)));
        const float slap_d = expf(-(float)CHUNK / (RATE * 0.22f));
        const float thump_d = expf(-(float)CHUNK / (RATE * 0.4f));
        const float gl = 0.5f * (1.0f - c->pan), gr = 0.5f * (1.0f + c->pan);
        const float flut_d = expf(-1.0f / (RATE * 0.16f));   /* per sample */
        const float flut_hz = 2.5f + 2.0f * c->flutter;   /* slow, uneven flaps: cloth, not a rattle */

        /* rigging whistle: a narrow band of noise whose pitch wanders with the gusts, retuned once per chunk */
        const float fc = c->whistle_hz * (1.0f + 0.06f * clampf(s->wobble * g_wob, -1.5f, 1.5f));
        const float f = 2.0f * sinf(3.14159265f * clampf(fc, 100.0f, 4000.0f) / RATE);
        const float g_wi = 0.5f / sqrtf(f * 6.0f) * 0.577f * 2.0f;

        /* voices at zero are skipped for the chunk; their filters hold state and they wake on a ramp */
        const int on_wash = c->wash >= 1e-4f, on_swell = c->swell >= 1e-4f;
        const int on_slap = s->slap_env >= 1e-4f, on_thump = s->thump_env >= 1e-4f;
        const int on_gur = c->gurgle >= 1e-4f, on_wind = c->wind >= 1e-4f, on_flut = c->flutter >= 1e-4f;

        for (int i = 0; i < len; i++) {
            const float n0 = rnd(s), n1 = rnd(s), n2 = rnd(s);   /* always drawn, so each voice hears the same noise */
            float wl = 0.0f, wr = 0.0f, mono = 0.0f, gu = 0.0f;
            if (on_wash) {
                wl = lp2(s->wash_s[0], a_wash, n0) * g_wash * c->wash * 0.22f * BED;
                wr = lp2(s->wash_s[1], a_wash, n1) * g_wash * c->wash * 0.22f * BED;
            }
            if (on_swell) mono += lp2(s->swell_s, a_swell, n2) * g_swell * c->swell * 0.2f * BED;
            if (on_slap) mono += lp2(s->slap_s, a_slap, n0) * g_slap * s->slap_env * 0.35f;
            if (on_thump) mono += lp2(s->thump_s, a_thump, n1) * g_thump * s->thump_env * 0.6f;

            s->burble += a_burble * (n2 - s->burble);
            if (on_gur) {
                const float bu = clampf(0.5f + 0.5f * s->burble * g_burble, 0.0f, 1.0f);
                gu = lp2(s->gur_s, a_gur, n0) * g_gur * bu * bu * c->gurgle * 0.25f * BED;
            }

            s->wobble += a_wob * (n1 - s->wobble);
            if (on_wind) {
                s->svf_low += f * s->svf_band;
                const float high = n2 - s->svf_low - s->svf_band * (1.0f / 6.0f);
                s->svf_band += f * high;
                mono += s->svf_band * g_wi * c->wind * 0.55f * BED;
            }

            /* luffing sail: each flap is a snap of low-passed noise that dies away like cloth going slack.
               The phase and gate run even while silent, so flaps don't bunch up when it resumes. */
            s->flut_phase += flut_hz * s->flut_len / RATE;
            if (s->flut_phase >= 1.0f) {
                s->flut_phase -= 1.0f;
                s->flut_gate = 0.5f + 0.5f * (0.5f + 0.5f * n2);
                s->flut_len = 0.7f + 0.6f * (0.5f + 0.5f * n1);
            }
            s->flut_gate *= flut_d;
            if (on_flut) {
                s->flut_amp += 0.0007f * (s->flut_gate - s->flut_amp);   /* ~30 ms swell into each flap: a flop, not a click */
                s->flut_lp += 0.022f * (n0 - s->flut_lp);               /* ~150 Hz billow of cloth */
                s->flut_body += 0.012f * (n1 - s->flut_body);           /* ~80 Hz thud of the sail filling and dropping */
                mono += (s->flut_lp * 6.0f + s->flut_body * 14.0f) * s->flut_amp * c->flutter * 0.85f * BED;
            }

            float l = mono + wl + gu * gl * 2.0f;
            float r = mono + wr + gu * gr * 2.0f;
            l *= s->master; r *= s->master;
            l /= 1.0f + 0.4f * fabsf(l);
            r /= 1.0f + 0.4f * fabsf(r);
            left[base + i] = (int16_t)(clampf(l, -1.0f, 1.0f) * 30000.0f);
            right[base + i] = (int16_t)(clampf(r, -1.0f, 1.0f) * 30000.0f);
        }
        s->slap_env *= slap_d;
        s->thump_env *= thump_d;
        if (s->slap_env < 1e-4f) s->slap_env = 0.0f;
        if (s->thump_env < 1e-4f) s->thump_env = 0.0f;
    }
    return 1;
}
