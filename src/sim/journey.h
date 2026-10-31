#ifndef JOURNEY_H
#define JOURNEY_H
#include <stdbool.h>
#include <stdint.h>
#include "race.h"

#define JOURNEY_MAX 8      /* buoys held at once: passed ones not yet out of sight, and those ahead */
#define JOURNEY_AHEAD 2    /* unpassed buoys kept spawned ahead of the boat */

typedef struct {
    double x, y;      /* m, local (game) frame */
    float gate;       /* rad, heading of the ray the boat crosses to pass: the way it came in */
    int rounding;     /* ROUND_PORT or ROUND_STARBOARD; final once the next buoy has spawned */
} JourneyBuoy;

typedef struct {
    float min_m, max_m;       /* distance from one buoy to the next */
    JourneyBuoy buoy[JOURNEY_MAX];   /* oldest first */
    int n;            /* buoys held */
    int next;         /* index of the buoy to pass next; buoys before it are passed; == n when none */
    int passed;       /* buoys passed over the whole journey */
    double t;         /* s since the start */
    double lx, ly;    /* where the next spawn leaves from: the newest buoy, or the start */
    float course;     /* rad, heading of the newest leg */
    uint32_t rng;
    float wind_from;  /* rad, where the wind comes from, as last given */
    float no_go;      /* rad, legs never point this close to the wind's source */
} Journey;

/* Starts at (x, y) heading `heading`. The distance range is sanitised: min >= 10 m, max >= min.
   Every leg points at least `no_go` rad off the direction the wind comes from (`wind_from`), so the
   boat can reach each buoy; a non-finite or negative no_go means no limit. */
void journey_init(Journey *j, float min_m, float max_m, uint32_t seed, double x, double y, float heading,
                  float wind_from, float no_go);
/* Advances the clock and passes the next buoy if the move (x0,y0)->(x1,y1) crosses its gate,
   then tops up the buoys ahead using the current `wind_from`. Returns true if a buoy was passed. */
bool journey_update(Journey *j, double x0, double y0, double x1, double y1, float dt, float wind_from);
/* The buoy to pass next, or NULL. */
const JourneyBuoy *journey_next(const Journey *j);
/* Removes the oldest buoy if it has been passed; false (and no change) otherwise. */
bool journey_drop_oldest(Journey *j);
/* The local frame moved by (dx, dy): local coordinates had (dx, dy) subtracted. */
void journey_shift(Journey *j, double dx, double dy);

#endif
