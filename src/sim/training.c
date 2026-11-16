#include "training.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

#define DEG (3.14159265f / 180.0f)

void training_init(Training *t) {
    t->step = TRAIN_DOWNWIND_AUTO;
    t->held = 0.0f;
    t->seen = 0;
    t->visiting = -1;
    t->miss = 0.0f;
    t->goals = NULL;
}

float training_off_wind(const Game *g) {
    float source = wrap_pi(g->wind.dir + 3.14159265f);
    float a = fabsf(wrap_pi(g->b.heading - source));
    return a > 3.14159265f ? 3.14159265f : a;
}

bool training_engine_locked(const Training *t) { return t->step < TRAIN_ENGINE_FREE; }

void training_goal_any(TrainGoal *o) {
    memset(o, 0, sizeof *o);   /* unused visit slots and padding compare equal */
    o->autotrim = o->autopilot = o->engine = o->mode = -1;
    o->wind_lo = 0.0f; o->wind_hi = 180.0f;
    o->speed_lo = -INFINITY; o->speed_hi = INFINITY;
    o->nvisit = 0;
    o->hold_s = TRAIN_HOLD_S;
    o->never = false;
}

static void visit(TrainGoal *o, float lo, float hi) {
    if (o->nvisit >= TRAIN_VISITS) return;
    o->visit_lo[o->nvisit] = lo;
    o->visit_hi[o->nvisit] = hi;
    o->nvisit++;
}

void training_default_goal(TrainStep s, TrainGoal *o) {
    training_goal_any(o);
    switch (s) {
    case TRAIN_DOWNWIND_AUTO: o->autotrim = 1; o->wind_lo = 150; break;
    case TRAIN_BEAM_AUTO: o->autotrim = 1; o->wind_lo = 70; o->wind_hi = 110; break;
    case TRAIN_POINTS_AUTO: o->autotrim = 1; o->mode = MODE_WHEEL; visit(o, 45, 70); visit(o, 110, 150); break;
    case TRAIN_CLOSE_HAULED: o->autotrim = 1; o->wind_lo = 35; o->wind_hi = 50; break;
    case TRAIN_IRONS: o->wind_hi = 30; o->speed_hi = 0.3f; break;
    case TRAIN_ENGINE_FREE: o->engine = 1; o->wind_lo = 45; o->speed_lo = 0.5f; break;
    /* sailing steps: the engine is off; downwind the autopilot holds the heading while the crank trims */
    case TRAIN_DOWNWIND_MANUAL: o->autotrim = 0; o->engine = 0; o->autopilot = 1; o->mode = MODE_TRIM; o->wind_lo = 150; break;
    case TRAIN_BEAM_MANUAL: o->autotrim = 0; o->engine = 0; o->wind_lo = 70; o->wind_hi = 110; break;
    case TRAIN_POINTS_MANUAL: o->autotrim = 0; o->engine = 0; visit(o, 45, 70); visit(o, 110, 150); break;
    default: o->never = true; break;
    }
}

const TrainGoal *training_goal(const Training *t) {
    static TrainGoal builtin[TRAIN_STEPS];
    static bool ready;
    if (!ready) {
        for (int i = 0; i < TRAIN_STEPS; i++) training_default_goal((TrainStep)i, &builtin[i]);
        ready = true;
    }
    const int s = (int)t->step >= 0 && t->step < TRAIN_STEPS ? (int)t->step : TRAIN_DONE;
    return t->goals ? &t->goals[s] : &builtin[s];
}

static bool between(float v, float lo_deg, float hi_deg) { return v >= lo_deg * DEG && v <= hi_deg * DEG; }
static bool sw(signed char want, bool on) { return want < 0 || (want == 1) == on; }

/* The visit range still to visit that off is inside, or -1. */
static int unseen_visit(const Training *t, const TrainGoal *q, float off) {
    for (int i = 0; i < q->nvisit; i++)
        if (!(t->seen & (1u << i)) && between(off, q->visit_lo[i], q->visit_hi[i])) return i;
    return -1;
}

/* True while the goal holds this frame; for a visit goal *vis is the range being held. */
static bool met(const Training *t, const TrainGoal *q, const Game *g, int *vis) {
    *vis = -1;
    if (q->never) return false;
    if (!sw(q->autotrim, g->at.on) || !sw(q->autopilot, g->ap.on) || !sw(q->engine, g->en.on)) return false;
    if (q->mode >= 0 && (int)g->ctl.mode != q->mode) return false;
    const float off = training_off_wind(g);
    if (!between(off, q->wind_lo, q->wind_hi)) return false;
    if (!(g->b.u >= q->speed_lo && g->b.u <= q->speed_hi)) return false;
    if (q->nvisit > 0) {
        *vis = unseen_visit(t, q, off);
        return *vis >= 0;
    }
    return true;
}

bool training_goal_met(const Training *t, const Game *g) {
    int vis;
    return met(t, training_goal(t), g, &vis);
}

void training_update(Training *t, const Game *g, float dt) {
    if (t->step >= TRAIN_DONE || g->run != RUN_SAILING) return;
    if (!(dt > 0.0f && dt < 10.0f) || !isfinite(g->b.heading) || !isfinite(g->b.u) || !isfinite(g->wind.dir)) {
        t->held = 0.0f;
        return;
    }
    const TrainGoal *q = training_goal(t);
    int vis;
    if (!met(t, q, g, &vis)) { t->held = 0.0f; t->visiting = -1; t->miss += dt; return; }
    t->miss = 0.0f;
    if (vis != t->visiting) { t->held = 0.0f; t->visiting = vis; }   /* a new range starts its own hold */
    t->held += dt;
    if (t->held < q->hold_s) return;
    t->held = 0.0f;
    t->visiting = -1;
    if (vis >= 0) {
        t->seen |= 1u << vis;
        if (t->seen != (1u << q->nvisit) - 1u) return;
    }
    t->step = (TrainStep)(t->step + 1);
    t->seen = 0;
    t->miss = 0.0f;
}
