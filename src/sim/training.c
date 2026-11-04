#include "training.h"
#include <math.h>
#include "mathx.h"

#define DEG (3.14159265f / 180.0f)

enum { POINT_CLOSE = 1u, POINT_BROAD = 2u };

void training_init(Training *t) {
    t->step = TRAIN_DOWNWIND_AUTO;
    t->held = 0.0f;
    t->seen = 0;
}

float training_off_wind(const Game *g) {
    float source = wrap_pi(g->wind.dir + 3.14159265f);
    float a = fabsf(wrap_pi(g->b.heading - source));
    return a > 3.14159265f ? 3.14159265f : a;
}

bool training_engine_locked(const Training *t) { return t->step < TRAIN_ENGINE_FREE; }

static bool between(float v, float lo_deg, float hi_deg) { return v >= lo_deg * DEG && v <= hi_deg * DEG; }

/* The point of sail the heading is on, as a POINT_* bit; 0 when on neither. */
static unsigned point_of_sail(float off) {
    if (between(off, 45, 70)) return POINT_CLOSE;
    if (between(off, 110, 150)) return POINT_BROAD;
    return 0;
}

/* True while the current step's goal is met this frame. Points steps report the point being held via *point. */
static bool goal_met(const Training *t, const Game *g, float off, unsigned *point) {
    *point = 0;
    switch (t->step) {
    case TRAIN_DOWNWIND_AUTO: return g->at.on && off > 150 * DEG;
    case TRAIN_BEAM_AUTO: return g->at.on && between(off, 70, 110);
    case TRAIN_POINTS_AUTO: *point = point_of_sail(off); return g->at.on && *point;
    case TRAIN_CLOSE_HAULED: return g->at.on && between(off, 35, 50);
    case TRAIN_IRONS: return off < 30 * DEG && g->b.u < 0.3f;
    case TRAIN_ENGINE_FREE: return g->en.on && off > 45 * DEG && g->b.u > 0.5f;
    case TRAIN_DOWNWIND_MANUAL: return !g->at.on && off > 150 * DEG;
    case TRAIN_BEAM_MANUAL: return !g->at.on && between(off, 70, 110);
    case TRAIN_POINTS_MANUAL: *point = point_of_sail(off); return !g->at.on && *point;
    default: return false;
    }
}

void training_update(Training *t, const Game *g, float dt) {
    if (t->step >= TRAIN_DONE || g->run != RUN_SAILING) return;
    if (!(dt > 0.0f && dt < 10.0f) || !isfinite(g->b.heading) || !isfinite(g->b.u) || !isfinite(g->wind.dir)) {
        t->held = 0.0f;
        return;
    }
    unsigned point;
    if (!goal_met(t, g, training_off_wind(g), &point)) { t->held = 0.0f; return; }
    t->held += dt;
    if (t->held < TRAIN_HOLD_S) return;
    t->held = 0.0f;
    if (point) {
        t->seen |= point;
        if (t->seen != (POINT_CLOSE | POINT_BROAD)) return;
    }
    t->step = (TrainStep)(t->step + 1);
    t->seen = 0;
}
