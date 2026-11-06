#ifndef WORLD_H
#define WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mathx.h"

typedef struct {
    float journey_min_m, journey_max_m;   /* m; each journey buoy lies this far (random in the range) from the last */
    float journey_shift_min_s, journey_shift_max_s;   /* s; gap between journey weather shifts (random in the range) */
    float weather_blend_s;     /* s; how long a journey weather shift takes to blend */
} WorldCfg;

void world_default(WorldCfg *out);

/* Always fills `out` from defaults, then applies the JSON object in buf. Out-of-range values
   are clamped, non-numbers keep the default; each raises a "; "-joined warning and makes the
   result false. */
bool world_load(WorldCfg *out, const char *buf, size_t len, char *warn, size_t warnlen);

#endif
