#include "follower.h"
#include <math.h>
#include <string.h>
#include "mathx.h"

static int wrap_idx(int i) { return (i % FOLLOW_CAP + FOLLOW_CAP) % FOLLOW_CAP; }

static FollowPose pose_of(const BoatState *b) {
    FollowPose p;
    p.x = b->x;
    p.y = b->y;
    p.heading = b->heading;
    p.heel = b->heel;
    p.wheel = b->wheel;
    p.u = b->u;
    memcpy(p.trims, b->trims, sizeof p.trims);
    memcpy(p.hoists, b->hoists, sizeof p.hoists);
    memcpy(p.swing, b->swing, sizeof p.swing);
    return p;
}

void follower_clear(Follower *f) {
    memset(f, 0, sizeof *f);
}

void follower_record(Follower *f, const BoatState *b) {
    if (!isfinite(b->x) || !isfinite(b->y)) return;
    FollowPose p = pose_of(b);
    if (f->n == 0) {
        f->head = 0;
        f->n = 1;
        f->smp[0].p = p;
        f->smp[0].s = 0;
        return;
    }
    FollowSample *newest = &f->smp[f->head];
    /* The sample before the newest is the anchor the step is measured from, so the newest can keep
     * following the boat without ever being left behind. The first sample stays fixed. */
    const FollowSample *anchor = f->n >= 2 ? &f->smp[wrap_idx(f->head - 1)] : newest;
    double da = hypot(p.x - anchor->p.x, p.y - anchor->p.y);
    if (da >= FOLLOW_STEP_M) {
        double s = newest->s + hypot(p.x - newest->p.x, p.y - newest->p.y);
        f->head = wrap_idx(f->head + 1);
        if (f->n < FOLLOW_CAP) f->n++;
        f->smp[f->head].p = p;
        f->smp[f->head].s = s;
    } else if (f->n >= 2) {
        newest->p = p;
        newest->s = anchor->s + da;
    }
}

static void lerp_pose(FollowPose *o, const FollowPose *a, const FollowPose *b, float t) {
    o->x = a->x + (b->x - a->x) * t;
    o->y = a->y + (b->y - a->y) * t;
    o->heading = wrap_pi(a->heading + wrap_pi(b->heading - a->heading) * t);
    o->heel = a->heel + (b->heel - a->heel) * t;
    o->wheel = a->wheel + (b->wheel - a->wheel) * t;
    o->u = a->u + (b->u - a->u) * t;
    for (int i = 0; i < BOAT_MAX_SAILS; i++) {
        o->trims[i] = a->trims[i] + (b->trims[i] - a->trims[i]) * t;
        o->hoists[i] = a->hoists[i] + (b->hoists[i] - a->hoists[i]) * t;
        o->swing[i] = a->swing[i] + (b->swing[i] - a->swing[i]) * t;
    }
}

bool follower_pose(const Follower *f, float behind_m, FollowPose *out) {
    if (f->n < 2 || !isfinite(behind_m) || behind_m < 0) return false;
    const FollowSample *newest = &f->smp[f->head];
    const FollowSample *oldest = &f->smp[wrap_idx(f->head - f->n + 1)];
    if (newest->s - oldest->s < behind_m) return false;
    double target = newest->s - behind_m;
    int hi = f->head;
    for (int k = 0; k < f->n - 2; k++) {
        int lo = wrap_idx(hi - 1);
        if (f->smp[lo].s <= target) break;
        hi = lo;
    }
    int lo = wrap_idx(hi - 1);
    double span = f->smp[hi].s - f->smp[lo].s;
    float t = span > 1e-9 ? (float)((target - f->smp[lo].s) / span) : 0.0f;
    t = clampf(t, 0.0f, 1.0f);
    lerp_pose(out, &f->smp[lo].p, &f->smp[hi].p, t);
    return true;
}

void follower_shift(Follower *f, double dx, double dy) {
    for (int i = 0; i < f->n; i++) {
        FollowSample *s = &f->smp[wrap_idx(f->head - i)];
        s->p.x -= dx;
        s->p.y -= dy;
    }
}
