#ifndef ROCKS_H
#define ROCKS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "boat.h"
#include "boatcfg.h"
#include "mathx.h"

#define ROCKS_MAX 64   /* rocks_near never returns more than this */

typedef struct {
    float rock_cell_size;      /* m; one rock at most per square cell */
    float rock_density;        /* 0..1, probability that a cell holds a rock */
    float rock_r_min, rock_r_max;   /* m; r_max <= cell/2 so a rock lies inside its cell */
    float rock_bump_speed;     /* m/s; impact speed below this is a BUMP */
    float rock_wreck_speed;    /* m/s; impact speed at or above this is a WRECK */
    float start_clear_radius;  /* m; no rock reaches within this distance of the origin */
} WorldCfg;

typedef struct { float x, y, r; uint8_t awash; } Rock;   /* awash: breaking water over it */

typedef enum { HIT_NONE, HIT_BUMP, HIT_STRIKE, HIT_WRECK } HitKind;

void world_default(WorldCfg *out);

/* Always fills `out` from defaults, then applies the JSON object in buf. Out-of-range values
   are clamped, non-numbers keep the default; each raises a "; "-joined warning and makes the
   result false. */
bool world_load(WorldCfg *out, const char *buf, size_t len, char *warn, size_t warnlen);

/* Rocks whose edge is within `radius` of (x, y), in a fixed cell order, at most
   min(max, ROCKS_MAX). The scatter depends only on (cfg, seed), never on call order. A hand-built
   cfg is sanitised first; non-finite x, y or radius, a NULL out or max <= 0 return 0. */
int rocks_near(const WorldCfg *cfg, uint32_t seed, float x, float y, float radius, Rock *out, int max);

/* Resolves contact with each rock: removes the velocity component into the rock, damps the rest
   by tier, pushes the hull out, and kicks the heading on a STRIKE. Wave `heave` (m) raises the
   effective impact speed. Returns the worst tier hit; a boat sliding along or away is HIT_NONE. */
HitKind rocks_collide(const WorldCfg *cfg, const Rock *rocks, int n, BoatState *b,
                      const BoatCfg *boat, float heave);

#endif
