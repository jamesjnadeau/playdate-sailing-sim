/* Sails every course in tracks/ with the baseline pilot and writes the ghosts the game ships.
 *
 *   ghost_gen <repo_root> <out_dir>
 *
 * Writes ghost_<course>_<boat>_<weather>.bin for the boats below in the default weather, and prints
 * "<course> <boat> <seconds> <closest mark in m>" per file. Exits 1 if a run does not finish or passes a
 * buoy closer than 15 m, naming the course and boat. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "baseline.h"

#define MIN_CLEAR_M 15.0f
#define MAX_TRACKS 32

static const char *const BOATS[] = { "sunfish", "dinghy", "j24" };
#define N_BOATS ((int)(sizeof BOATS / sizeof *BOATS))

static Game game;
static BoatCfg boat;
static WeatherCfg weather;
static WorldCfg world;
static Track track;
static GhostSample store[GHOST_MAX_SAMPLES];
static Ghost ghost;
static char buf[16384];
static uint8_t enc[GHOST_FILE_MAX];
static char names[MAX_TRACKS][64];

static size_t slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ghost_gen: cannot read %s\n", path);
        exit(1);
    }
    const size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return n;
}

static int by_name(const void *a, const void *b) { return strcmp(a, b); }

static int list_courses(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "ghost_gen: cannot open %s\n", dir);
        exit(1);
    }
    int n = 0;
    for (struct dirent *e; (e = readdir(d)) && n < MAX_TRACKS;) {
        const size_t len = strlen(e->d_name);
        if (len > 5 && len - 5 < sizeof names[0] && !strcmp(e->d_name + len - 5, ".json")) {
            memcpy(names[n], e->d_name, len - 5);
            names[n++][len - 5] = 0;
        }
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], by_name);
    return n;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <repo_root> <out_dir>\n", argv[0]);
        return 2;
    }
    const char *root = argv[1], *out = argv[2];
    char path[512], file[128];
    size_t n;

    snprintf(path, sizeof path, "%s/tracks", root);
    const int ncourses = list_courses(path);
    weather_default(&weather);
    n = slurp((snprintf(path, sizeof path, "%s/config/weather.json", root), path));
    if (!weather_load(&weather, buf, n, NULL, 0)) return fprintf(stderr, "ghost_gen: bad weather.json\n"), 1;
    world_default(&world);
    n = slurp((snprintf(path, sizeof path, "%s/config/world.json", root), path));
    if (!world_load(&world, buf, n, NULL, 0)) return fprintf(stderr, "ghost_gen: bad world.json\n"), 1;

    int bad = 0;
    for (int c = 0; c < ncourses; c++)
        for (int b = 0; b < N_BOATS; b++) {
            boatcfg_default(&boat);
            n = slurp((snprintf(path, sizeof path, "%s/boats/%s.json", root, BOATS[b]), path));
            if (!boatcfg_load(&boat, buf, n, NULL, 0)) return fprintf(stderr, "ghost_gen: bad %s\n", path), 1;
            n = slurp((snprintf(path, sizeof path, "%s/tracks/%s.json", root, names[c]), path));
            if (!track_load(&track, buf, n, NULL, 0)) return fprintf(stderr, "ghost_gen: bad %s\n", path), 1;
            game_init(&game, &boat, &weather, &world, WEATHER_CHOPPY, ghost_seed(track.name));
            game_set_track(&game, &track);
            ghost_init(&ghost, store, GHOST_MAX_SAMPLES);

            const BaselineResult r = baseline_sail(&game, &ghost, BOATS[b]);
            printf("%s %s %.1f %.1f\n", names[c], BOATS[b], r.time, r.min_mark_m);
            if (!r.finished || r.min_mark_m < MIN_CLEAR_M || !ghost_usable(&ghost)) {
                fprintf(stderr, "ghost_gen: %s with %s %s (closest mark %.1f m)\n", names[c], BOATS[b],
                        r.finished ? "passes a buoy too close" : "did not finish", r.min_mark_m);
                bad = 1;
                continue;
            }
            const size_t len = ghost_encode(&ghost, enc, sizeof enc);
            ghost_file_name(file, sizeof file, track.name, BOATS[b], WEATHER_NAMES[WEATHER_CHOPPY]);
            snprintf(path, sizeof path, "%s/%s", out, file);
            FILE *f = fopen(path, "wb");
            if (!f || len == 0 || fwrite(enc, 1, len, f) != len) {
                fprintf(stderr, "ghost_gen: cannot write %s\n", path);
                bad = 1;
            }
            if (f) fclose(f);
        }
    return bad;
}
