#include "t.h"
#include <stdio.h>
#include <string.h>
#include "baseline.h"
#include "mathx.h"

static Game g;
static BoatCfg bc;
static WeatherCfg wcfg;
static WorldCfg wc;
static Track tr;
static GhostSample store[GHOST_MAX_SAMPLES];
static Ghost gh;
static char buf[16384];

static const char *const TRACKS[] = {
    "trapezoid_45_100", "trapezoid_60_67", "trapezoid_70_50", "triangle_45", "triangle_45_stbd",
    "triangle_60", "triangle_60_stbd", "two_buoys", "windward_leeward",
};
static const char *const BOATS[] = { "sunfish", "dinghy", "j24" };
#define N_TRACKS ((int)(sizeof TRACKS / sizeof *TRACKS))
#define N_BOATS ((int)(sizeof BOATS / sizeof *BOATS))

static size_t slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    T_ASSERT(f != NULL);
    const size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return n;
}

static void load(const char *course, const char *boat) {
    char path[128];
    size_t n;
    snprintf(path, sizeof path, "../boats/%s.json", boat);
    n = slurp(path);
    boatcfg_default(&bc);
    T_ASSERT(boatcfg_load(&bc, buf, n, NULL, 0));
    n = slurp("../config/weather.json");
    weather_default(&wcfg);
    T_ASSERT(weather_load(&wcfg, buf, n, NULL, 0));
    n = slurp("../config/world.json");
    world_default(&wc);
    T_ASSERT(world_load(&wc, buf, n, NULL, 0));
    snprintf(path, sizeof path, "../tracks/%s.json", course);
    n = slurp(path);
    T_ASSERT(track_load(&tr, buf, n, NULL, 0));
    game_init(&g, &bc, &wcfg, &wc, WEATHER_CHOPPY, ghost_seed(tr.name));
    game_set_track(&g, &tr);
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
}

static void test_sunfish_finishes_windward_leeward(void) {
    load("windward_leeward", "sunfish");
    const BaselineResult r = baseline_sail(&g, &gh, "sunfish");
    printf("  windward_leeward sunfish: %.1f s, closest mark %.1f m\n", r.time, r.min_mark_m);
    T_ASSERT(r.finished);
    T_ASSERT(r.time > 30 && r.time < GHOST_MAX_SECONDS);
    T_ASSERT(ghost_usable(&gh));
    T_ASSERT(ghost_matches(&gh, ghost_seed(tr.name), ghost_track_hash(&tr), ghost_hash_str("sunfish"), WEATHER_CHOPPY));
}

static void test_passes_marks_wide(void) {
    load("windward_leeward", "sunfish");
    const BaselineResult r = baseline_sail(&g, &gh, "sunfish");
    T_ASSERT(r.min_mark_m >= 15.0f);
}

static uint8_t enc_a[GHOST_FILE_MAX], enc_b[GHOST_FILE_MAX];

static void test_deterministic(void) {
    load("triangle_60", "j24");
    baseline_sail(&g, &gh, "j24");
    const size_t na = ghost_encode(&gh, enc_a, sizeof enc_a);
    load("triangle_60", "j24");
    baseline_sail(&g, &gh, "j24");
    const size_t nb = ghost_encode(&gh, enc_b, sizeof enc_b);
    T_ASSERT(na > 0 && na == nb);
    T_ASSERT(memcmp(enc_a, enc_b, na) == 0);
}

/* One course x boat race; returns 1 when it failed. */
static int run_row(const char *track, const char *boat) {
    load(track, boat);
    const BaselineResult r = baseline_sail(&g, &gh, boat);
    const int ok = r.finished && r.min_mark_m >= BASELINE_MIN_CLEAR_M;
    printf("  %-18s %-8s %7.1f s  closest %5.1f m%s\n", track, boat, r.time, r.min_mark_m, ok ? "" : "   <-- FAIL");
    return !ok;
}

static void test_all_courses_all_boats_finish(void) {
    int bad = 0;
    for (int t = 0; t < N_TRACKS; t++)
        for (int b = 0; b < N_BOATS; b++) bad += run_row(TRACKS[t], BOATS[b]);
    T_ASSERT(bad == 0);
}

static const char *row_track, *row_boat;

static void test_one_row(void) {
    T_ASSERT(run_row(row_track, row_boat) == 0);
}

static int known(const char *const *list, int n, const char *name) {
    for (int i = 0; i < n; i++)
        if (strcmp(list[i], name) == 0) return 1;
    return 0;
}

/* No arguments runs everything. The Makefile shards the matrix to run it in parallel:
 *   list            print the "<track> <boat>" pairs, one per line
 *   core            run only the tests outside the matrix
 *   <track> <boat>  run one matrix row */
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (int t = 0; t < N_TRACKS; t++)
            for (int b = 0; b < N_BOATS; b++) printf("%s %s\n", TRACKS[t], BOATS[b]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "core") == 0) {
        T_RUN(test_sunfish_finishes_windward_leeward);
        T_RUN(test_passes_marks_wide);
        T_RUN(test_deterministic);
        return T_DONE();
    }
    if (argc == 3) {
        if (!known(TRACKS, N_TRACKS, argv[1]) || !known(BOATS, N_BOATS, argv[2])) {
            fprintf(stderr, "unknown track or boat: %s %s\n", argv[1], argv[2]);
            return 1;
        }
        row_track = argv[1];
        row_boat = argv[2];
        T_RUN(test_one_row);
        return T_DONE();
    }
    if (argc != 1) {
        fprintf(stderr, "usage: %s [list | core | <track> <boat>]\n", argv[0]);
        return 1;
    }
    T_RUN(test_sunfish_finishes_windward_leeward);
    T_RUN(test_passes_marks_wide);
    T_RUN(test_deterministic);
    T_RUN(test_all_courses_all_boats_finish);
    return T_DONE();
}
