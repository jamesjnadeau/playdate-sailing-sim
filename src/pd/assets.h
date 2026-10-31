#ifndef ASSETS_H
#define ASSETS_H
#include <stdbool.h>
#include <stddef.h>
#include "pd_api.h"
#include "game.h"
#include "boatlist.h"

#define ASSETS_WARN_LEN 512

/* Lists boats/ once at startup. Call before any other boat function; sunfish is first and the
   default, and an empty or unreadable directory leaves just sunfish. */
void assets_scan_boats(PlaydateAPI *pd);
int assets_boat_count(void);
/* assets_boat_count() names, stable until the next scan */
const char *const *assets_boat_names(void);

/* Sets out[i] true for each boat that may race (boat_class racer). An unreadable or invalid file
   counts as a racer, as the built-in default does. */
void assets_boat_raceable(PlaydateAPI *pd, bool out[BOATLIST_MAX]);

/* Reads boat, weather and world configs into out params. A missing or unreadable file leaves the
   built-in default and adds a message; a parse problem adds the loader's warnings. Always fills
   all three. Returns false if any warning was raised; warn is "; "-separated. */
bool assets_load(PlaydateAPI *pd, int boat_index, BoatCfg *boat, WeatherCfg *weather,
                 WorldCfg *world, char *warn, size_t warnlen);

/* Returns bytes read, or -1 when missing or too large. *buf is a shared buffer, valid until the next read. */
int assets_read_file(PlaydateAPI *pd, const char *path, char **buf);

#endif
