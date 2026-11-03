#include "assets.h"
#include <stdio.h>
#include <string.h>

static BoatList boats;

static void collect_boat(const char *name, void *ud) {
    (void)ud;
    boatlist_add(&boats, name);
}

void assets_scan_boats(const Platform *p) {
    boatlist_init(&boats);
    p->listfiles("boats", collect_boat, NULL);
    boatlist_finish(&boats);
}

int assets_boat_count(void) { return boats.count; }
const char *const *assets_boat_names(void) { return boats.ptr; }

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
static int read_file(const Platform *p, const char *path) {
    void *f = p->open(path, PLAT_READ);
    if (!f) return -1;
    int n = p->read(f, filebuf, FILE_MAX - 1);
    int more = p->read(f, filebuf + n, 1);
    p->close(f);
    if (n < 0 || more > 0) return -1;
    filebuf[n] = 0;
    return n;
}

int assets_read_file(const Platform *p, const char *path, char **buf) {
    int n = read_file(p, path);
    *buf = filebuf;
    return n;
}

void assets_boat_raceable(const Platform *p, bool out[BOATLIST_MAX]) {
    char path[96], msg[160];
    BoatCfg boat;
    for (int i = 0; i < boats.count; i++) {
        /* Reading or parsing failure leaves boatcfg_default, which is a racer. */
        boatcfg_default(&boat);
        snprintf(path, sizeof path, "boats/%s.json", boats.name[i]);
        int n = read_file(p, path);
        if (n >= 0) {
            msg[0] = 0;
            boatcfg_load(&boat, filebuf, (size_t)n, msg, sizeof msg);
        }
        out[i] = boat.boat_class != BOAT_CLASS_CRUISER;
    }
}

bool assets_load(const Platform *p, int boat_index, BoatCfg *boat, WeatherCfg *weather,
                 WorldCfg *world, char *warn, size_t warnlen) {
    char path[96], msg[160];
    warn[0] = 0;
    if (boat_index < 0 || boat_index >= boats.count) boat_index = 0;
    boatcfg_default(boat);
    weather_default(weather);
    world_default(world);

    snprintf(path, sizeof path, "boats/%s.json", boats.name[boat_index]);
    int n = read_file(p, path);
    if (n < 0) addmsg(warn, warnlen, "%s: %s", path, "missing or too large, using built-in boat");
    else {
        msg[0] = 0;
        if (!boatcfg_load(boat, filebuf, (size_t)n, msg, sizeof msg)) addmsg(warn, warnlen, "%s: %s", path, msg);
    }

    n = read_file(p, "config/weather.json");
    if (n < 0) addmsg(warn, warnlen, "%s: %s", "config/weather.json", "missing, using built-in weather");
    else {
        msg[0] = 0;
        if (!weather_load(weather, filebuf, (size_t)n, msg, sizeof msg)) addmsg(warn, warnlen, "%s: %s", "weather", msg);
    }

    n = read_file(p, "config/world.json");
    if (n < 0) addmsg(warn, warnlen, "%s: %s", "config/world.json", "missing, using built-in world");
    else {
        msg[0] = 0;
        if (!world_load(world, filebuf, (size_t)n, msg, sizeof msg)) addmsg(warn, warnlen, "%s: %s", "world", msg);
    }
    return warn[0] == 0;
}
