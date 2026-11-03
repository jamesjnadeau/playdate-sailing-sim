#include "navigator.h"
#include <math.h>
#include "mathx.h"

void nav_init(NavState *s) { s->side = 0; }

void nav_aim_point(const Mark *m, float offset, float *x, float *y) {
    *x = m->x + offset * cosf(m->gate);
    *y = m->y + offset * sinf(m->gate);
}

/* Where to steer for mark m from track position (x, y): the corner until far enough along the ray, then the
 * crossing point. Port roundings are crossed from the ray's left, starboard from its right. */
static void nav_target(const Track *t, int next, double x, double y, float *tx, float *ty) {
    const Mark *m = &t->marks[next];
    const float rx = cosf(m->gate), ry = sinf(m->gate);
    nav_aim_point(m, NAV_AIM_OFFSET_M, tx, ty);
    const double along = (x - m->x) * rx + (y - m->y) * ry;
    if (along >= NAV_SWITCH_ALONG_M) return;
    const float side = m->rounding == ROUND_STARBOARD ? -1.0f : 1.0f;
    const float ax = *tx, ay = *ty;
    /* Stand the corner further out while another buoy sits near it. */
    for (float off = NAV_APPROACH_OFFSET_M; off <= NAV_APPROACH_MAX_M; off += 4.0f) {
        *tx = ax + side * off * -ry;
        *ty = ay + side * off * rx;
        bool clear = true;
        for (int k = 0; k < t->nmarks; k++)
            if (k != next && hypot(t->marks[k].x - *tx, t->marks[k].y - *ty) < NAV_CLEAR_M + 4.0f) clear = false;
        if (clear) return;
    }
    *tx = ax + side * NAV_APPROACH_OFFSET_M * -ry;
    *ty = ay + side * NAV_APPROACH_OFFSET_M * rx;
}

/* Miss distance of a boat at (x, y) holding heading h from point (mx, my); infinite if the point is behind. */
static double nav_miss(double x, double y, float h, float mx, float my) {
    const double dx = cos(h), dy = sin(h), rx = mx - x, ry = my - y;
    if (dx * rx + dy * ry <= 0.0) return INFINITY;
    return fabs(dx * ry - dy * rx);
}

/* The nearest buoy ahead, nearer than `dist`, that heading h would pass within NAV_CLEAR_M of; -1 for none. */
static int nav_blocker(const Track *t, double x, double y, float h, double dist) {
    int best = -1;
    double best_d = dist;
    for (int k = 0; k < t->nmarks; k++) {
        const double d = hypot(t->marks[k].x - x, t->marks[k].y - y);
        if (d < best_d && nav_miss(x, y, h, t->marks[k].x, t->marks[k].y) < NAV_CLEAR_M) {
            best = k;
            best_d = d;
        }
    }
    return best;
}

/* Bend heading `h` around buoy k so it passes just outside NAV_CLEAR_M, on the side h already favours. */
static float nav_around(const Track *t, int k, double x, double y, float h) {
    const double mx = t->marks[k].x - x, my = t->marks[k].y - y;
    const float to_mark = atan2f((float)my, (float)mx);
    const double d = hypot(mx, my);
    const float spread = asinf((float)fmin(1.0, (NAV_CLEAR_M + 3.0f) / d));
    return wrap_pi(to_mark + (wrap_pi(h - to_mark) < 0.0f ? -spread : spread));
}

float nav_heading(NavState *s, const Track *t, int next, double px, double py, double ox, double oy,
                  float wind_from, float no_go_rad) {
    if (next < 0) next = 0;
    if (next >= t->nmarks) next = t->nmarks - 1;
    float ax, ay, lx = t->start_x, ly = t->start_y;
    const double x = px + ox, y = py + oy;
    nav_target(t, next, x, y, &ax, &ay);
    if (next > 0) nav_aim_point(&t->marks[next - 1], NAV_AIM_OFFSET_M, &lx, &ly);
    const float bearing = atan2f((float)(ay - y), (float)(ax - x));
    const float limit = no_go_rad + NAV_UPWIND_MARGIN_RAD;
    const float off_wind = wrap_pi(bearing - wind_from);
    const double aim_dist = hypot(ax - x, ay - y);
    if (fabsf(off_wind) >= limit) {
        s->side = 0;
        const int k = nav_blocker(t, x, y, bearing, aim_dist);
        if (k < 0) return bearing;
        const float around = nav_around(t, k, x, y, bearing);
        const float off = wrap_pi(around - wind_from);
        return fabsf(off) >= limit ? around : wrap_pi(wind_from + copysignf(limit, off));
    }
    if (s->side == 0) s->side = off_wind < 0.0f ? -1 : 1;

    /* Close-hauled: tack once the boat has drifted out of the corridor around the leg, on the side this tack goes. */
    const double leg_dx = ax - lx, leg_dy = ay - ly;
    const double leg_len = hypot(leg_dx, leg_dy);
    if (leg_len > 1e-3) {
        const double nx = -leg_dy / leg_len, ny = leg_dx / leg_len;      /* left of the leg */
        const double cross = (x - lx) * nx + (y - ly) * ny;
        const float h = wrap_pi(wind_from + s->side * limit);
        const double away = cos(h) * nx + sin(h) * ny;                 /* >0: this tack heads left */
        double corridor = NAV_CORRIDOR_FRAC * leg_len;
        if (corridor < NAV_CORRIDOR_MIN_M) corridor = NAV_CORRIDOR_MIN_M;
        if (fabs(cross) > corridor && cross * away > 0.0) s->side = -s->side;
    }

    /* Don't beat into a buoy: swap tacks when this one passes too close and the other clears it. */
    const float h_now = wrap_pi(wind_from + s->side * limit);
    const float h_alt = wrap_pi(wind_from - s->side * limit);
    if (nav_blocker(t, x, y, h_now, INFINITY) >= 0 && nav_blocker(t, x, y, h_alt, INFINITY) < 0)
        s->side = -s->side;
    return wrap_pi(wind_from + s->side * limit);
}
