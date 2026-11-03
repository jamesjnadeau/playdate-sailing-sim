#include "baseline.h"
#include <math.h>
#include "mathx.h"
#include "navigator.h"

#define BASELINE_DT (1.0f / 30.0f)
#define BASELINE_BEAR_OFF_RAD (10.0f * (float)M_PI / 180.0f)

/* How a tack is made. A tack that works in one wind and boat can stall in another, so each run tries these
 * and keeps the fastest that stays clear of the buoys. */
typedef struct {
    float speed;   /* m/s needed to start a tack */
    float stall;   /* m/s below which a boat inside the no-go zone bears off */
    float lead_deg;   /* how far ahead of the heading a tack aims */
} TackStyle;

static const TackStyle STYLES[] = {
    { 2.5f, 0.2f, 40.0f }, { 2.5f, 0.5f, 40.0f }, { 2.8f, 0.5f, 40.0f }, { 2.8f, 0.2f, 40.0f },
    { 2.5f, 0.5f, 30.0f }, { 2.8f, 0.5f, 30.0f }, { 2.0f, 0.5f, 40.0f }, { 2.2f, 0.5f, 35.0f },
    { 2.0f, 0.5f, 20.0f }, { 2.5f, 0.5f, 20.0f },
};
#define N_STYLES ((int)(sizeof STYLES / sizeof *STYLES))

static BaselineResult sail(Game *g, Ghost *gh, const char *boat_name, const TackStyle *ts) {
    BaselineResult r = { false, 0.0, INFINITY };
    game_restart(g, ghost_seed(g->track.name));
    ghost_begin(gh, g, ghost_track_hash(&g->track), ghost_hash_str(boat_name));

    const float lead_rad = ts->lead_deg * (float)M_PI / 180.0f;
    NavState nav;
    bool tacking = false;
    nav_init(&nav);
    /* A turns the autopilot on; a quick press of B (down one frame, up the next) turns the autotrim on. */
    game_update(g, BTN_A | BTN_B, BTN_B, 0.0f, BASELINE_DT);
    ghost_record(gh, g);
    game_update(g, 0, 0, 0.0f, BASELINE_DT);
    ghost_record(gh, g);

    while (!g->race.finished && g->run == RUN_SAILING && g->race.t < GHOST_MAX_SECONDS) {
        const float wind_from = wrap_pi(g->wind.dir + (float)M_PI);
        const float no_go = g->boat.controls.autopilot_no_go_deg * (float)M_PI / 180.0f;
        const float want = nav_heading(&nav, &g->track, g->race.next, g->b.x, g->b.y, g->race.ox, g->race.oy,
                                       wind_from, no_go);
        /* The autopilot never turns through the no-go zone from outside it (it gybes the long way round), so a
         * tack is made in steps: with way on, head to wind until inside the zone, then it takes the short way
         * across. Inside the zone the target leads the heading by a bounded angle, so the rudder is not slammed
         * over and the boat keeps its speed. A boat that stalls in the zone bears off on its current side
         * to regain speed and tries again. */
        const float off_now = wrap_pi(g->b.heading - wind_from), off_want = wrap_pi(want - wind_from);
        const bool crossing = off_now * off_want < 0 && fabsf(off_want) < (float)M_PI / 2;
        const float hold = wrap_pi(wind_from + copysignf(no_go + BASELINE_BEAR_OFF_RAD, off_now));
        const float toward_wind = wrap_pi(g->b.heading - copysignf(lead_rad, off_now));
        const float toward_want = wrap_pi(g->b.heading + clampf(wrap_pi(want - g->b.heading), -lead_rad, lead_rad));
        if (fabsf(off_now) >= no_go) {
            tacking = crossing && g->b.u >= ts->speed;
            if (!crossing) g->ap.target = want;
            else g->ap.target = tacking ? toward_wind : hold;
        } else if (tacking && crossing && g->b.u >= ts->stall) {
            g->ap.target = toward_wind;
        } else if (!crossing && g->b.u >= ts->stall) {
            tacking = false;
            g->ap.target = toward_want;
        } else {
            tacking = false;
            g->ap.target = hold;
        }
        game_update(g, 0, 0, 0.0f, BASELINE_DT);
        ghost_record(gh, g);
        /* Clearance counts against every buoy, not only the one being sailed to. */
        for (int k = 0; k < g->track.nmarks; k++) {
            double mx, my;
            race_mark_local(&g->race, &g->track, k, &mx, &my);
            const float d = (float)hypot(mx - g->b.x, my - g->b.y);
            if (d < r.min_mark_m) r.min_mark_m = d;
        }
    }
    ghost_finish(gh, g);
    r.finished = g->race.finished && g->run == RUN_SAILING;
    r.time = g->race.t;
    return r;
}

BaselineResult baseline_sail(Game *g, Ghost *gh, const char *boat_name) {
    BaselineResult best = { false, 0.0, 0.0f };
    int best_i = -1;
    for (int i = 0; i < N_STYLES; i++) {
        const BaselineResult r = sail(g, gh, boat_name, &STYLES[i]);
        const bool clean = r.finished && r.min_mark_m >= BASELINE_MIN_CLEAR_M;
        const bool best_clean = best_i >= 0 && best.finished && best.min_mark_m >= BASELINE_MIN_CLEAR_M;
        const bool better = best_i < 0 || (clean && (!best_clean || r.time < best.time)) ||
                            (!best_clean && r.finished == best.finished && r.min_mark_m > best.min_mark_m) ||
                            (!best_clean && r.finished && !best.finished);
        if (better) { best = r; best_i = i; }
    }
    return sail(g, gh, boat_name, &STYLES[best_i]);   /* leaves the winner's ghost in gh */
}
