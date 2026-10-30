#include "assets.h"
#include <stdio.h>
#include <string.h>

const char *const ASSET_BOAT_NAMES[ASSETS_BOATS] = { "dinghy", "spinnaker_example" };

#define FILE_MAX 16384
static char filebuf[FILE_MAX];

static void addmsg(char *warn, size_t cap, const char *fmt, const char *a, const char *b) {
    size_t n = strlen(warn);
    if (n + 1 >= cap) return;
    snprintf(warn + n, cap - n, "%s", n ? "; " : "");
    n = strlen(warn);
    snprintf(warn + n, cap - n, fmt, a, b);
}

/* Returns bytes read, or -1 when the file cannot be opened or is too large. */
static int read_file(PlaydateAPI *pd, const char *path) {
    SDFile *f = pd->file->open(path, kFileRead);
    if (!f) return -1;
    int n = pd->file->read(f, filebuf, FILE_MAX - 1);
    int more = pd->file->read(f, filebuf + n, 1);
    pd->file->close(f);
    if (n < 0 || more > 0) return -1;
    filebuf[n] = 0;
    return n;
}

bool assets_load(PlaydateAPI *pd, int boat_index, BoatCfg *boat, WeatherCfg *weather,
                 WorldCfg *world, char *warn, size_t warnlen) {
    char path[96], msg[160];
    warn[0] = 0;
    if (boat_index < 0 || boat_index >= ASSETS_BOATS) boat_index = 0;
    boatcfg_default(boat);
    weather_default(weather);
    world_default(world);

    snprintf(path, sizeof path, "boats/%s.json", ASSET_BOAT_NAMES[boat_index]);
    int n = read_file(pd, path);
    if (n < 0) addmsg(warn, warnlen, "%s: %s", path, "missing or too large, using built-in boat");
    else {
        msg[0] = 0;
        if (!boatcfg_load(boat, filebuf, (size_t)n, msg, sizeof msg)) addmsg(warn, warnlen, "%s: %s", path, msg);
    }

    n = read_file(pd, "config/weather.json");
    if (n < 0) addmsg(warn, warnlen, "%s: %s", "config/weather.json", "missing, using built-in weather");
    else {
        msg[0] = 0;
        if (!weather_load(weather, filebuf, (size_t)n, msg, sizeof msg)) addmsg(warn, warnlen, "%s: %s", "weather", msg);
    }

    n = read_file(pd, "config/world.json");
    if (n < 0) addmsg(warn, warnlen, "%s: %s", "config/world.json", "missing, using built-in world");
    else {
        msg[0] = 0;
        if (!world_load(world, filebuf, (size_t)n, msg, sizeof msg)) addmsg(warn, warnlen, "%s: %s", "world", msg);
    }
    return warn[0] == 0;
}
