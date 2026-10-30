#ifndef ASSETS_H
#define ASSETS_H
#include <stdbool.h>
#include <stddef.h>
#include "pd_api.h"
#include "game.h"

#define ASSETS_WARN_LEN 512
#define ASSETS_BOATS 3

extern const char *const ASSET_BOAT_NAMES[ASSETS_BOATS];

/* Reads boat, weather and world configs into out params. A missing or unreadable file leaves the
   built-in default and adds a message; a parse problem adds the loader's warnings. Always fills
   all three. Returns false if any warning was raised; warn is "; "-separated. */
bool assets_load(PlaydateAPI *pd, int boat_index, BoatCfg *boat, WeatherCfg *weather,
                 WorldCfg *world, char *warn, size_t warnlen);

#endif
