#include "irons.h"
#include <math.h>
#include <stddef.h>

void irons_init(Irons *s) {
    s->on = false;
    s->held = 0.0f;
}

bool irons_update(Irons *s, float off_wind, float no_go, float u, bool sailing, float dt) {
    if (!sailing || !isfinite(off_wind) || !isfinite(no_go) || !isfinite(u)) {
        irons_init(s);
        return false;
    }
    off_wind = fabsf(off_wind);
    if (s->on) {
        if (off_wind > no_go + IRONS_MARGIN_OUT || u > IRONS_SPEED_OUT) irons_init(s);
        return s->on;
    }
    if (off_wind < no_go && u < IRONS_SPEED_IN) {
        if (isfinite(dt) && dt > 0.0f) s->held += dt;
        if (s->held >= IRONS_HOLD_S) s->on = true;
    } else {
        s->held = 0.0f;
    }
    return s->on;
}

const char *irons_text(bool on) {
    return on ? "IN IRONS: STEER AWAY FROM THE WIND" : NULL;
}
