#include "ghost.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

#define ANGLE_SCALE 10000.0f
#define TRIM_SCALE 80.0f
#define PROGRESS_SCALE 4000.0f
#define MAGIC 0x54535447u   /* "GTST" little-endian bytes: G T S T */
#define VERSION 1
#define FLAG_TAIL 1

void ghost_init(Ghost *gh, GhostSample *storage, int cap) {
    memset(gh, 0, sizeof *gh);
    gh->s = storage;
    gh->cap = storage ? cap : 0;
}

void ghost_clear(Ghost *gh) {
    GhostSample *s = gh->s;
    int cap = gh->cap;
    ghost_init(gh, s, cap);
}

/* ---- identity ---- */

static uint32_t fnv(uint32_t h, const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

uint32_t ghost_hash_str(const char *s) { return s ? fnv(2166136261u, s, strlen(s)) : 2166136261u; }

uint32_t ghost_track_hash(const Track *t) {
    uint32_t h = ghost_hash_str(t->name);
    const float head[] = { t->wind_from, t->wind_speed, t->start_x, t->start_y, t->start_heading };
    h = fnv(h, head, sizeof head);
    h = fnv(h, &t->nmarks, sizeof t->nmarks);
    for (int i = 0; i < t->nmarks && i < TRACK_MAX_MARKS; i++) {
        const float m[] = { t->marks[i].x, t->marks[i].y, t->marks[i].gate };
        h = fnv(h, m, sizeof m);
        h = fnv(h, &t->marks[i].rounding, sizeof t->marks[i].rounding);
    }
    return h;
}

uint32_t ghost_seed(const char *course) {
    uint32_t h = fnv(ghost_hash_str(course), "seed", 4);
    return h ? h : 1u;
}

/* ---- progress ---- */

float ghost_progress(const Game *g) {
    const Track *t = &g->track;
    const Race *r = &g->race;
    if (r->finished || r->next >= t->nmarks) return (float)t->nmarks;
    double mx, my, px, py;
    race_mark_local(r, t, r->next, &mx, &my);
    if (r->next == 0) {
        px = (double)t->start_x - r->ox;
        py = (double)t->start_y - r->oy;
    } else {
        race_mark_local(r, t, r->next - 1, &px, &py);
    }
    const float leg = hypotf((float)(mx - px), (float)(my - py)), d = hypotf((float)(mx - g->b.x), (float)(my - g->b.y));
    const float frac = leg > 1e-3f ? clampf(1.0f - d / leg, 0.0f, 1.0f) : 0.0f;
    return (float)r->next + frac;
}

/* ---- samples <-> poses ---- */

static int8_t q8(float v, float scale) { return (int8_t)iroundf(clampf(v * scale, -127.0f, 127.0f)); }

static void pose_of_game(const Game *g, GhostPose *p) {
    memset(p, 0, sizeof *p);
    p->x = g->b.x + g->race.ox;
    p->y = g->b.y + g->race.oy;
    p->heading = g->b.heading;
    p->heel = g->b.heel;
    p->wheel = g->b.wheel;
    p->progress = ghost_progress(g);
    for (int i = 0; i < GHOST_SAILS; i++) {
        const bool have = i < g->boat.sail_count;
        p->trim[i] = have ? g->b.trims[i] : 0.0f;
        p->hoist[i] = have ? g->b.hoists[i] : 0.0f;
        p->swing[i] = g->b.swing[i] > 0.0f ? 1.0f : -1.0f;
    }
}

static void pose_lerp(const GhostPose *a, const GhostPose *b, float f, GhostPose *o) {
    f = clampf(f, 0.0f, 1.0f);
    o->x = a->x + (double)((float)(b->x - a->x) * f);
    o->y = a->y + (double)((float)(b->y - a->y) * f);
    o->heading = wrap_pi(a->heading + wrap_pi(b->heading - a->heading) * f);
    o->heel = a->heel + (b->heel - a->heel) * f;
    o->wheel = a->wheel + (b->wheel - a->wheel) * f;
    o->progress = a->progress + (b->progress - a->progress) * f;
    o->speed = b->speed;
    for (int i = 0; i < GHOST_SAILS; i++) {
        o->trim[i] = a->trim[i] + (b->trim[i] - a->trim[i]) * f;
        o->hoist[i] = a->hoist[i] + (b->hoist[i] - a->hoist[i]) * f;
        o->swing[i] = f < 0.5f ? a->swing[i] : b->swing[i];
    }
}

static void sample_of_pose(const GhostPose *p, GhostSample *s) {
    s->x = (float)p->x;
    s->y = (float)p->y;
    s->heading = (int16_t)iroundf(clampf(p->heading * ANGLE_SCALE, -32767.0f, 32767.0f));
    s->heel = (int16_t)iroundf(clampf(p->heel * ANGLE_SCALE, -32767.0f, 32767.0f));
    s->progress = (uint16_t)iroundf(clampf(p->progress * PROGRESS_SCALE, 0.0f, 65535.0f));
    s->wheel = q8(p->wheel, 127.0f);
    s->swing = 0;
    for (int i = 0; i < GHOST_SAILS; i++) {
        s->trim[i] = q8(p->trim[i], TRIM_SCALE);
        s->hoist[i] = (uint8_t)iroundf(clampf(p->hoist[i], 0.0f, 1.0f) * 255.0f);
        if (p->swing[i] > 0.0f) s->swing |= (uint8_t)(1u << i);
    }
}

static void pose_of_sample(const GhostSample *s, GhostPose *p) {
    memset(p, 0, sizeof *p);
    p->x = s->x;
    p->y = s->y;
    p->heading = (float)s->heading / ANGLE_SCALE;
    p->heel = (float)s->heel / ANGLE_SCALE;
    p->progress = (float)s->progress / PROGRESS_SCALE;
    p->wheel = (float)s->wheel / 127.0f;
    for (int i = 0; i < GHOST_SAILS; i++) {
        p->trim[i] = (float)s->trim[i] / TRIM_SCALE;
        p->hoist[i] = (float)s->hoist[i] / 255.0f;
        p->swing[i] = (s->swing >> i) & 1 ? 1.0f : -1.0f;
    }
}

/* ---- recording ---- */

static void push(Ghost *gh, const GhostPose *p) {
    if (gh->n >= gh->cap) {
        gh->overflow = true;
        return;
    }
    sample_of_pose(p, &gh->s[gh->n++]);
}

void ghost_begin(Ghost *gh, const Game *g, uint32_t track_hash, uint32_t boat_hash) {
    ghost_clear(gh);
    if (!gh->s) return;
    gh->seed = g->seed;
    gh->track_hash = track_hash;
    gh->boat_hash = boat_hash;
    gh->preset = g->preset;
    pose_of_game(g, &gh->prev);
    gh->prev_t = g->race.t;
    push(gh, &gh->prev);   /* sample 0, at race time 0 */
}

void ghost_record(Ghost *gh, const Game *g) {
    if (!gh->s || gh->done || gh->overflow || gh->n == 0 || g->race.finished) return;
    const double t = g->race.t;
    if (!(t > gh->prev_t)) return;
    GhostPose cur;
    pose_of_game(g, &cur);
    /* the frame lands between samples: place each sample on the grid by interpolating the two frames around it */
    while (!gh->overflow && (double)gh->n / GHOST_HZ <= t) {
        const double ts = (double)gh->n / GHOST_HZ;
        GhostPose at;
        pose_lerp(&gh->prev, &cur, (float)((ts - gh->prev_t) / (t - gh->prev_t)), &at);
        push(gh, &at);
    }
    gh->prev = cur;
    gh->prev_t = t;
}

bool ghost_finish(Ghost *gh, const Game *g) {
    if (!gh->s || gh->done || gh->n == 0) return false;
    gh->done = true;
    gh->time = g->race.t;
    if (!gh->overflow && gh->time > (double)(gh->n - 1) / GHOST_HZ + 1e-6) {
        /* the finish is off the grid: a last sample there, so the ghost ends on the line */
        GhostPose end;
        pose_of_game(g, &end);
        push(gh, &end);
        gh->tail = true;
    }
    return ghost_usable(gh);
}

bool ghost_usable(const Ghost *gh) {
    return gh->s && gh->done && !gh->overflow && gh->n >= 2 && isfinite(gh->time) && gh->time > 0.0;
}

bool ghost_matches(const Ghost *gh, uint32_t seed, uint32_t track_hash, uint32_t boat_hash, int preset) {
    return ghost_usable(gh) && gh->seed == seed && gh->track_hash == track_hash && gh->boat_hash == boat_hash &&
           gh->preset == preset;
}

/* ---- playback ---- */

static double sample_time(const Ghost *gh, int i) {
    return gh->tail && i == gh->n - 1 ? gh->time : (double)i / GHOST_HZ;
}

bool ghost_pose_at(const Ghost *gh, double t, GhostPose *out) {
    if (!gh->s || gh->n < 1) return false;
    if (!isfinite(t)) t = 0.0;
    int i = (int)floor(t * GHOST_HZ);
    if (i < 0) i = 0;
    if (i > gh->n - 1) i = gh->n - 1;
    while (i > 0 && sample_time(gh, i) > t) i--;
    while (i < gh->n - 1 && sample_time(gh, i + 1) <= t) i++;
    GhostPose a;
    pose_of_sample(&gh->s[i], &a);
    if (i >= gh->n - 1) {
        *out = a;
        if (gh->n >= 2) {   /* at the end it keeps the speed of its last leg */
            GhostPose b;
            pose_of_sample(&gh->s[i - 1], &b);
            const double dt = sample_time(gh, i) - sample_time(gh, i - 1);
            out->speed = dt > 1e-6 ? (float)(hypot(a.x - b.x, a.y - b.y) / dt) : 0.0f;
        }
        return true;
    }
    GhostPose b;
    pose_of_sample(&gh->s[i + 1], &b);
    const double dt = sample_time(gh, i + 1) - sample_time(gh, i);
    b.speed = dt > 1e-6 ? (float)(hypot(b.x - a.x, b.y - a.y) / dt) : 0.0f;
    pose_lerp(&a, &b, dt > 1e-6 ? (float)((t - sample_time(gh, i)) / dt) : 0.0f, out);
    return true;
}

bool ghost_time_at_progress(Ghost *gh, float p, double *t) {
    if (!gh->s || gh->n < 1 || !isfinite(p)) return false;
    int i = gh->cursor;
    if (i < 0) i = 0;
    if (i > gh->n - 1) i = gh->n - 1;
    /* the cursor walks to the last sample at or before p; progress can dip a little while beating to a mark */
    while (i < gh->n - 1 && (float)gh->s[i + 1].progress / PROGRESS_SCALE <= p) i++;
    while (i > 0 && (float)gh->s[i].progress / PROGRESS_SCALE > p) i--;
    gh->cursor = i;
    const double t0 = sample_time(gh, i);
    if (i >= gh->n - 1) {
        *t = t0;
        return true;
    }
    const float p0 = (float)gh->s[i].progress / PROGRESS_SCALE, p1 = (float)gh->s[i + 1].progress / PROGRESS_SCALE;
    const double f = p1 > p0 ? (double)clampf((p - p0) / (p1 - p0), 0.0f, 1.0f) : 0.0;
    *t = t0 + (sample_time(gh, i + 1) - t0) * f;
    return true;
}

/* ---- file image ---- */

static void put16(uint8_t *o, uint32_t v) { o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *o, uint32_t v) { put16(o, v & 0xffff); put16(o + 2, v >> 16); }
static void putf(uint8_t *o, float v) { uint32_t u; memcpy(&u, &v, 4); put32(o, u); }
static void putd(uint8_t *o, double v) { uint64_t u; memcpy(&u, &v, 8); put32(o, (uint32_t)u); put32(o + 4, (uint32_t)(u >> 32)); }
static uint32_t get16(const uint8_t *i) { return (uint32_t)i[0] | (uint32_t)i[1] << 8; }
static uint32_t get32(const uint8_t *i) { return get16(i) | get16(i + 2) << 16; }
static float getf(const uint8_t *i) { uint32_t u = get32(i); float v; memcpy(&v, &u, 4); return v; }
static double getd(const uint8_t *i) { uint64_t u = (uint64_t)get32(i) | (uint64_t)get32(i + 4) << 32; double v; memcpy(&v, &u, 8); return v; }

size_t ghost_encoded_size(const Ghost *gh) { return GHOST_HEADER_BYTES + (size_t)gh->n * GHOST_SAMPLE_BYTES; }

size_t ghost_encode(const Ghost *gh, uint8_t *out, size_t n) {
    if (!ghost_usable(gh) || n < ghost_encoded_size(gh)) return 0;
    put32(out + 0, MAGIC);
    put16(out + 4, VERSION);
    put16(out + 6, gh->tail ? FLAG_TAIL : 0);
    put32(out + 8, gh->seed);
    put32(out + 12, gh->track_hash);
    put32(out + 16, gh->boat_hash);
    put16(out + 20, (uint32_t)gh->preset);
    put16(out + 22, 0);
    putd(out + 24, gh->time);
    put32(out + 32, (uint32_t)gh->n);
    put32(out + 36, 0);
    uint8_t *o = out + GHOST_HEADER_BYTES;
    for (int i = 0; i < gh->n; i++, o += GHOST_SAMPLE_BYTES) {
        const GhostSample *s = &gh->s[i];
        putf(o + 0, s->x);
        putf(o + 4, s->y);
        put16(o + 8, (uint16_t)s->heading);
        put16(o + 10, (uint16_t)s->heel);
        put16(o + 12, s->progress);
        o[14] = (uint8_t)s->wheel;
        o[15] = s->swing;
        for (int k = 0; k < GHOST_SAILS; k++) {
            o[16 + k] = (uint8_t)s->trim[k];
            o[20 + k] = s->hoist[k];
        }
    }
    return ghost_encoded_size(gh);
}

bool ghost_decode(Ghost *gh, const uint8_t *in, size_t n) {
    GhostSample *storage = gh->s;
    const int cap = gh->cap;
    ghost_clear(gh);
    if (!storage || n < GHOST_HEADER_BYTES || get32(in) != MAGIC || get16(in + 4) != VERSION) return false;
    const uint32_t count = get32(in + 32);
    if (count < 2 || count > (uint32_t)cap || n != GHOST_HEADER_BYTES + (size_t)count * GHOST_SAMPLE_BYTES) return false;
    const double time = getd(in + 24);
    if (!isfinite(time) || time <= 0.0) return false;
    const uint8_t *i = in + GHOST_HEADER_BYTES;
    for (uint32_t k = 0; k < count; k++, i += GHOST_SAMPLE_BYTES) {
        GhostSample *s = &storage[k];
        s->x = getf(i + 0);
        s->y = getf(i + 4);
        if (!isfinite(s->x) || !isfinite(s->y)) {
            ghost_clear(gh);
            return false;
        }
        s->heading = (int16_t)get16(i + 8);
        s->heel = (int16_t)get16(i + 10);
        s->progress = (uint16_t)get16(i + 12);
        s->wheel = (int8_t)i[14];
        s->swing = i[15];
        for (int j = 0; j < GHOST_SAILS; j++) {
            s->trim[j] = (int8_t)i[16 + j];
            s->hoist[j] = i[20 + j];
        }
    }
    gh->n = (int)count;
    gh->seed = get32(in + 8);
    gh->track_hash = get32(in + 12);
    gh->boat_hash = get32(in + 16);
    gh->preset = (int)get16(in + 20);
    gh->time = time;
    gh->tail = get16(in + 6) & FLAG_TAIL;
    gh->done = true;
    return true;
}

bool ghost_decode_for(Ghost *gh, const uint8_t *in, size_t n, uint32_t seed, uint32_t track_hash, uint32_t boat_hash,
                      int preset) {
    if (ghost_decode(gh, in, n) && ghost_matches(gh, seed, track_hash, boat_hash, preset)) return true;
    ghost_clear(gh);
    return false;
}

bool ghost_peek_time(const uint8_t *in, size_t n, double *time) {
    if (n < GHOST_HEADER_BYTES || get32(in) != MAGIC || get16(in + 4) != VERSION || get32(in + 32) < 2) return false;
    const double t = getd(in + 24);
    if (!isfinite(t) || t <= 0.0) return false;
    *time = t;
    return true;
}

void ghost_file_name(char *out, size_t n, const char *course, const char *boat, const char *weather) {
    if (n == 0) return;
    const char *const parts[3] = { course, boat, weather };
    size_t k = 0;
    for (const char *c = "ghost"; *c && k + 1 < n; c++) out[k++] = *c;
    for (int part = 0; part < 3; part++) {
        const char *s = parts[part] ? parts[part] : "";
        if (k + 1 < n) out[k++] = '_';
        for (; *s && k + 5 < n; s++)
            if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')) out[k++] = *s;
    }
    for (const char *c = ".bin"; *c && k + 1 < n; c++) out[k++] = *c;
    out[k] = '\0';
}
