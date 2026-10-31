#include "journey.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

#define MIN_LEG_M 10.0f
#define SPREAD_RAD 1.0472f   /* a leg turns at most this far (60 deg) from the one before */
#define TRIES 8              /* random headings tried before heading straight away from the oldest buoy */

static float rnd(Journey *j) {
    uint32_t x = j->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    j->rng = x ? x : 1u;
    return (float)(x >> 8) / 16777216.0f;   /* [0, 1) */
}

/* h turned the shortest way out of the no-go cone about the wind's source, if it is inside. */
static float clear_of_wind(const Journey *j, float h) {
    float d = wrap_pi(h - j->wind_from);
    if (fabsf(d) >= j->no_go) return h;
    return wrap_pi(j->wind_from + (d < 0.0f ? -j->no_go : j->no_go));
}

/* Adds one buoy. Its heading is random about the last leg's, and must take it farther from the
   oldest buoy than the point it leaves from, so the course never doubles back over what it has left. */
static void spawn(Journey *j) {
    if (j->n == JOURNEY_MAX) {   /* full of passed buoys still in view: the oldest goes */
        if (j->next > 0) journey_drop_oldest(j);
        else return;
    }
    const float dist = j->min_m + rnd(j) * (j->max_m - j->min_m);
    const JourneyBuoy *old = j->n > 0 ? &j->buoy[0] : NULL;
    const double d0 = old ? hypot(j->lx - old->x, j->ly - old->y) : 0.0;
    float h = j->course;
    bool ok = false;
    for (int i = 0; i < TRIES && !ok; i++) {
        h = wrap_pi(j->course + (rnd(j) * 2.0f - 1.0f) * SPREAD_RAD);
        if (fabsf(wrap_pi(h - j->wind_from)) < j->no_go) continue;   /* into the wind: unreachable */
        ok = !old || hypot(j->lx + dist * cos(h) - old->x, j->ly + dist * sin(h) - old->y) > d0;
    }
    if (!ok) {   /* straight away from the oldest, or the nearest heading the wind allows */
        h = old ? (float)atan2(j->ly - old->y, j->lx - old->x) : j->course;
        h = clear_of_wind(j, h);
    }

    if (j->n > 0) {   /* the buoy before this one is rounded towards the side the course turns to */
        JourneyBuoy *prev = &j->buoy[j->n - 1];
        prev->rounding = wrap_pi(h - prev->gate) >= 0.0f ? ROUND_STARBOARD : ROUND_PORT;
    }
    JourneyBuoy *b = &j->buoy[j->n++];
    b->x = j->lx + dist * cos(h);
    b->y = j->ly + dist * sin(h);
    b->gate = h;
    b->rounding = ROUND_STARBOARD;   /* provisional, until the next buoy decides it */
    j->lx = b->x;
    j->ly = b->y;
    j->course = h;
}

static void top_up(Journey *j) {
    while (j->n - j->next < JOURNEY_AHEAD) {
        int before = j->n;
        int next_before = j->next;
        spawn(j);
        if (j->n == before && j->next == next_before) break;   /* cannot make room */
    }
}

void journey_init(Journey *j, float min_m, float max_m, uint32_t seed, double x, double y, float heading,
                  float wind_from, float no_go) {
    memset(j, 0, sizeof *j);
    if (!(min_m >= MIN_LEG_M)) min_m = MIN_LEG_M;
    if (!(max_m >= min_m)) max_m = min_m;
    j->min_m = min_m;
    j->max_m = max_m;
    j->rng = seed * 2654435761u + 0x9e3779b9u;
    if (!j->rng) j->rng = 1u;
    j->wind_from = isfinite(wind_from) ? wind_from : 0.0f;
    j->no_go = isfinite(no_go) && no_go > 0.0f ? fminf(no_go, 1.5f) : 0.0f;   /* < 86 deg, so a leg always exists */
    j->lx = x;
    j->ly = y;
    j->course = isfinite(heading) ? heading : 0.0f;
    top_up(j);
}

bool journey_update(Journey *j, double x0, double y0, double x1, double y1, float dt, float wind_from) {
    if (isfinite(wind_from)) j->wind_from = wind_from;
    if (isfinite(dt) && dt > 0.0f) j->t += dt;
    bool hit = false;
    if (j->next < j->n) {
        const JourneyBuoy *b = &j->buoy[j->next];
        if (gate_crossed((float)b->x, (float)b->y, b->gate, b->rounding, x0, y0, x1, y1)) {
            j->next++;
            j->passed++;
            hit = true;
        }
    }
    top_up(j);
    return hit;
}

const JourneyBuoy *journey_next(const Journey *j) {
    return j->next < j->n ? &j->buoy[j->next] : NULL;
}

bool journey_drop_oldest(Journey *j) {
    if (j->next < 1 || j->n < 1) return false;
    memmove(&j->buoy[0], &j->buoy[1], sizeof(JourneyBuoy) * (size_t)(j->n - 1));
    j->n--;
    j->next--;
    return true;
}

void journey_shift(Journey *j, double dx, double dy) {
    for (int i = 0; i < j->n; i++) {
        j->buoy[i].x -= dx;
        j->buoy[i].y -= dy;
    }
    j->lx -= dx;
    j->ly -= dy;
}
