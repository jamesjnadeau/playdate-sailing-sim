#include "t.h"
#include <stdio.h>
#include <string.h>
#include "ghost.h"
#include "mathx.h"

static Track tr;
static GhostSample store[GHOST_MAX_SAMPLES];
static Ghost gh;
static uint8_t file[GHOST_FILE_MAX];
static char buf[16384];

static const char *const TRACKS[] = {
    "trapezoid_45_100", "trapezoid_60_67", "trapezoid_70_50", "triangle_45", "triangle_45_stbd",
    "triangle_60", "triangle_60_stbd", "two_buoys", "windward_leeward",
};
static const char *const BOATS[] = { "sunfish", "dinghy", "j24" };
#define N_TRACKS ((int)(sizeof TRACKS / sizeof *TRACKS))
#define N_BOATS ((int)(sizeof BOATS / sizeof *BOATS))

static size_t slurp(const char *path, void *dst, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    const size_t n = fread(dst, 1, cap, f);
    fclose(f);
    return n;
}

static void load_track(const char *course) {
    char path[128];
    snprintf(path, sizeof path, "../tracks/%s.json", course);
    const size_t n = slurp(path, buf, sizeof buf - 1);
    T_ASSERT(n > 0);
    buf[n] = 0;
    T_ASSERT(track_load(&tr, buf, n, NULL, 0));
}

static void ghost_path(char *out, size_t cap, const char *boat) {
    char name[96];
    ghost_file_name(name, sizeof name, tr.name, boat, WEATHER_NAMES[WEATHER_CHOPPY]);
    snprintf(out, cap, "../ghosts/%s", name);
}

static void test_every_course_and_boat_has_a_matching_baseline(void) {
    for (int t = 0; t < N_TRACKS; t++) {
        load_track(TRACKS[t]);
        for (int b = 0; b < N_BOATS; b++) {
            char path[160];
            ghost_path(path, sizeof path, BOATS[b]);
            const size_t n = slurp(path, file, sizeof file);
            if (n == 0) fprintf(stderr, "missing %s: run `make ghosts`\n", path);
            T_ASSERT(n > 0);
            ghost_init(&gh, store, GHOST_MAX_SAMPLES);
            const bool ok = ghost_decode(&gh, file, n) &&
                            ghost_matches(&gh, ghost_seed(tr.name), ghost_track_hash(&tr),
                                          ghost_hash_str(BOATS[b]), WEATHER_CHOPPY);
            if (!ok) fprintf(stderr, "%s is stale or damaged: run `make ghosts`\n", path);
            T_ASSERT(ok);
        }
    }
}

static void test_other_boats_have_no_baseline(void) {
    static const char *const OTHERS[] = { "longship", "tp52" };
    for (int t = 0; t < N_TRACKS; t++) {
        load_track(TRACKS[t]);
        for (int b = 0; b < 2; b++) {
            char path[160];
            ghost_path(path, sizeof path, OTHERS[b]);
            T_ASSERT(slurp(path, file, sizeof file) == 0);
        }
    }
}

int main(void) {
    T_RUN(test_every_course_and_boat_has_a_matching_baseline);
    T_RUN(test_other_boats_have_no_baseline);
    return T_DONE();
}
