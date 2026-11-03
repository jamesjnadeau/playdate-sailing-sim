#ifndef TRACKS_H
#define TRACKS_H
#include <stddef.h>
#include "platform.h"
#include "race.h"

#define TRACKS_MAX 16

/* Loads every .json file in the tracks folder (sorted by file name) into out; files that fail to parse are
   skipped with a warning appended. If none load, out[0] = track_default and returns 1. */
int tracks_load_all(const Platform *p, Track *out, int max, char *warn, size_t warnlen);

#endif
