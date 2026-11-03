#include "navigator.h"
#include <math.h>
#include "mathx.h"

void nav_init(NavState *s) { s->side = 0; }

void nav_aim_point(const Mark *m, float offset, float *x, float *y) {
    *x = m->x + offset * cosf(m->gate);
    *y = m->y + offset * sinf(m->gate);
}

float nav_heading(NavState *s, const Track *t, int next, double px, double py, double ox, double oy,
                  float wind_from, float no_go_rad) {
    if (next < 0) next = 0;
    if (next >= t->nmarks) next = t->nmarks - 1;
    float ax, ay, lx = t->start_x, ly = t->start_y;
    nav_aim_point(&t->marks[next], NAV_AIM_OFFSET_M, &ax, &ay);
    if (next > 0) nav_aim_point(&t->marks[next - 1], NAV_AIM_OFFSET_M, &lx, &ly);
    const double x = px + ox, y = py + oy;
    const float bearing = atan2f((float)(ay - y), (float)(ax - x));
    const float limit = no_go_rad + NAV_UPWIND_MARGIN_RAD;
    const float off_wind = wrap_pi(bearing - wind_from);
    if (fabsf(off_wind) >= limit) {
        s->side = 0;
        return bearing;
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
    return wrap_pi(wind_from + s->side * limit);
}
