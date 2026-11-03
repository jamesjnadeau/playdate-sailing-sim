#ifndef BASELINE_H
#define BASELINE_H
#include <stdbool.h>
#include "ghost.h"

/* A ghost must stay at least this far from every buoy centre, a few boat lengths clear of the buoy. The navigator
 * aims much wider of the mark being sailed to; this floor is for buoys the boat is merely passing, such as a
 * finish mark beside the start that a first tack cannot always avoid. */
#define BASELINE_MIN_CLEAR_M 7.0f

typedef struct {
    bool finished;       /* every mark rounded, race.finished */
    double time;         /* race.t at the finish, else the time reached */
    float min_mark_m;    /* closest the boat came to any mark centre */
} BaselineResult;

/* Sails one race in `g` (already game_init'd with the boat, weather, world and preset, and game_set_track'd)
 * with the autopilot and autotrim on, steered by the navigator through points wide of each mark. Restarts the
 * game on ghost_seed(track.name) and fills `gh` (caller-owned storage). Stops at the finish, a capsize or
 * GHOST_MAX_SECONDS. */
BaselineResult baseline_sail(Game *g, Ghost *gh, const char *boat_name);

#endif
