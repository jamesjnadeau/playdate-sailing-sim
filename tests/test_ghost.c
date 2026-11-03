#include "t.h"
#include <math.h>
#include <string.h>
#include "ghost.h"
#include "mathx.h"

static Game g;
static BoatCfg bc;
static WeatherCfg wcfg;
static WorldCfg wc;
static Track tr;
static GhostSample store[GHOST_MAX_SAMPLES];
static GhostSample store2[GHOST_MAX_SAMPLES];
static uint8_t file[GHOST_FILE_MAX];

static void race_fresh(void) {
    boatcfg_default(&bc);
    weather_default(&wcfg);
    world_default(&wc);
    game_init(&g, &bc, &wcfg, &wc, WEATHER_SUNNY, ghost_seed("test"));
    track_default(&tr);
    game_set_track(&g, &tr);
}

/* Sails for `secs` of race time at 30 fps, recording; returns the ghost finished at the end. */
static void sail(Ghost *gh, double secs) {
    race_fresh();
    ghost_begin(gh, &g, ghost_track_hash(&tr), 77);
    while (g.race.t < secs) {
        game_update(&g, 0, 0, 0, 1.0f / 30);
        ghost_record(gh, &g);
    }
    ghost_finish(gh, &g);
}

static void test_seed_is_stable_per_course_and_never_zero(void) {
    T_ASSERT(ghost_seed("a") == ghost_seed("a"));
    T_ASSERT(ghost_seed("a") != ghost_seed("b"));
    T_ASSERT(ghost_seed("") != 0 && ghost_seed(NULL) != 0);
}

static void test_track_hash_changes_with_course(void) {
    Track a, b;
    track_default(&a);
    b = a;
    T_ASSERT(ghost_track_hash(&a) == ghost_track_hash(&b));
    b.marks[0].x += 1.0f;
    T_ASSERT(ghost_track_hash(&a) != ghost_track_hash(&b));
}

static void test_progress_runs_from_start_to_nmarks(void) {
    race_fresh();
    T_NEAR(ghost_progress(&g), 0.0, 1e-3);
    g.race.next = 1;
    double mx, my;
    race_mark_local(&g.race, &g.track, 0, &mx, &my);
    g.b.x = mx;
    g.b.y = my;
    T_NEAR(ghost_progress(&g), 1.0, 1e-3);
    g.race.finished = true;
    g.race.next = g.track.nmarks;
    T_NEAR(ghost_progress(&g), g.track.nmarks, 1e-6);
}

static void test_records_on_the_grid(void) {
    Ghost gh;
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
    sail(&gh, 10.0);
    T_ASSERT(ghost_usable(&gh));
    T_ASSERT(gh.n >= 100 && gh.n <= 103);
    T_NEAR(gh.time, g.race.t, 1e-9);
    GhostPose p;
    T_ASSERT(ghost_pose_at(&gh, 0.0, &p));
    T_NEAR(p.x, tr.start_x, 1e-3);
    T_NEAR(p.y, tr.start_y, 1e-3);
}

static void test_pose_interpolates_and_clamps(void) {
    Ghost gh;
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
    sail(&gh, 5.0);
    GhostPose a, b, m, lo, hi;
    ghost_pose_at(&gh, 2.0, &a);
    ghost_pose_at(&gh, 2.1, &b);
    ghost_pose_at(&gh, 2.05, &m);
    T_NEAR(m.x, (a.x + b.x) / 2, 1e-3);
    T_NEAR(m.y, (a.y + b.y) / 2, 1e-3);
    ghost_pose_at(&gh, -5.0, &lo);
    ghost_pose_at(&gh, 0.0, &a);
    T_NEAR(lo.x, a.x, 1e-9);
    ghost_pose_at(&gh, 1e9, &hi);
    ghost_pose_at(&gh, gh.time, &b);
    T_NEAR(hi.x, b.x, 1e-9);
    T_ASSERT(ghost_pose_at(&gh, NAN, &m));
}

static void test_heading_interpolates_across_the_wrap(void) {
    Ghost gh;
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
    sail(&gh, 1.0);
    gh.s[3].heading = (int16_t)iroundf(3.1f * 10000.0f);
    gh.s[4].heading = (int16_t)iroundf(-3.1f * 10000.0f);
    GhostPose p;
    ghost_pose_at(&gh, 0.35, &p);
    T_ASSERT(fabsf(p.heading) > 3.0f);   /* through pi, not through 0 */
}

static void test_time_at_progress_inverts_progress(void) {
    Ghost gh;
    ghost_init(&gh, store, 20);
    gh.n = 4;
    gh.done = true;
    gh.time = 0.3;
    for (int i = 0; i < 4; i++) gh.s[i].progress = (uint16_t)(i * 4000);   /* 0,1,2,3 marks, 0.1 s apart */
    double t;
    T_ASSERT(ghost_time_at_progress(&gh, 1.5f, &t));
    T_NEAR(t, 0.15, 1e-6);
    T_ASSERT(ghost_time_at_progress(&gh, 0.25f, &t));   /* cursor walks back */
    T_NEAR(t, 0.025, 1e-6);
    T_ASSERT(ghost_time_at_progress(&gh, 99.0f, &t));
    T_NEAR(t, 0.3, 1e-6);
    T_ASSERT(!ghost_time_at_progress(&gh, NAN, &t));
}

static void test_overflow_is_not_usable(void) {
    Ghost gh;
    ghost_init(&gh, store, 5);
    sail(&gh, 2.0);
    T_ASSERT(gh.overflow);
    T_ASSERT(!ghost_usable(&gh));
    T_ASSERT(ghost_encode(&gh, file, sizeof file) == 0);
}

static void test_round_trip(void) {
    Ghost gh, back;
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
    sail(&gh, 8.0);
    const size_t n = ghost_encode(&gh, file, sizeof file);
    T_ASSERT(n == ghost_encoded_size(&gh) && n == GHOST_HEADER_BYTES + (size_t)gh.n * GHOST_SAMPLE_BYTES);
    ghost_init(&back, store2, GHOST_MAX_SAMPLES);
    T_ASSERT(ghost_decode(&back, file, n));
    T_ASSERT(back.n == gh.n && back.tail == gh.tail);
    T_NEAR(back.time, gh.time, 0.0);
    T_ASSERT(ghost_matches(&back, gh.seed, gh.track_hash, gh.boat_hash, gh.preset));
    T_ASSERT(memcmp(gh.s, back.s, sizeof(GhostSample) * (size_t)gh.n) == 0);
    GhostPose a, b;
    ghost_pose_at(&gh, 4.37, &a);
    ghost_pose_at(&back, 4.37, &b);
    T_NEAR(a.x, b.x, 1e-9);
    T_NEAR(a.heading, b.heading, 1e-9);
}

static void test_peek_reads_the_finish_time_from_the_header(void) {
    Ghost gh;
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
    sail(&gh, 8.0);
    const size_t n = ghost_encode(&gh, file, sizeof file);
    double t = 0;
    T_ASSERT(ghost_peek_time(file, GHOST_HEADER_BYTES, &t));
    T_NEAR(t, gh.time, 0.0);
    T_ASSERT(!ghost_peek_time(file, GHOST_HEADER_BYTES - 1, &t));
    file[0] ^= 0xff;
    T_ASSERT(!ghost_peek_time(file, n, &t));
}

static void test_matches_rejects_other_conditions(void) {
    Ghost gh;
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
    sail(&gh, 3.0);
    T_ASSERT(ghost_matches(&gh, gh.seed, gh.track_hash, gh.boat_hash, gh.preset));
    T_ASSERT(!ghost_matches(&gh, gh.seed + 1, gh.track_hash, gh.boat_hash, gh.preset));
    T_ASSERT(!ghost_matches(&gh, gh.seed, gh.track_hash + 1, gh.boat_hash, gh.preset));
    T_ASSERT(!ghost_matches(&gh, gh.seed, gh.track_hash, gh.boat_hash + 1, gh.preset));
    T_ASSERT(!ghost_matches(&gh, gh.seed, gh.track_hash, gh.boat_hash, gh.preset + 1));
}

static void test_decode_rejects_malformed(void) {
    Ghost gh, back;
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
    sail(&gh, 3.0);
    const size_t n = ghost_encode(&gh, file, sizeof file);
    ghost_init(&back, store2, GHOST_MAX_SAMPLES);
    T_ASSERT(!ghost_decode(&back, file, 0));
    T_ASSERT(!ghost_decode(&back, file, GHOST_HEADER_BYTES - 1));
    T_ASSERT(!ghost_decode(&back, file, n - 1));      /* truncated */
    T_ASSERT(!ghost_decode(&back, file, n + 1));      /* trailing bytes */
    file[0] ^= 0xff;
    T_ASSERT(!ghost_decode(&back, file, n));          /* magic */
    file[0] ^= 0xff;
    file[4] = 99;
    T_ASSERT(!ghost_decode(&back, file, n));          /* version */
    file[4] = 1;
    file[32] = 0xff; file[33] = 0xff; file[34] = 0xff; file[35] = 0x7f;
    T_ASSERT(!ghost_decode(&back, file, n));          /* count beyond the storage */
    T_ASSERT(!ghost_usable(&back));
    ghost_init(&back, store2, 3);
    ghost_encode(&gh, file, sizeof file);
    T_ASSERT(!ghost_decode(&back, file, n));          /* smaller storage than the file */
    ghost_init(&back, NULL, 0);
    T_ASSERT(!ghost_decode(&back, file, n));
}

static void test_decode_rejects_nonfinite_position(void) {
    Ghost gh, back;
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
    sail(&gh, 2.0);
    const size_t n = ghost_encode(&gh, file, sizeof file);
    const float bad = NAN;
    memcpy(file + GHOST_HEADER_BYTES + GHOST_SAMPLE_BYTES, &bad, 4);
    ghost_init(&back, store2, GHOST_MAX_SAMPLES);
    T_ASSERT(!ghost_decode(&back, file, n));
}

static void test_begin_clears_the_previous_run(void) {
    Ghost gh;
    ghost_init(&gh, store, GHOST_MAX_SAMPLES);
    sail(&gh, 3.0);
    race_fresh();
    ghost_begin(&gh, &g, 1, 2);
    T_ASSERT(gh.n == 1 && !gh.done && !ghost_usable(&gh));
}

static void test_capacity_of_twenty_minutes(void) {
    T_ASSERT(GHOST_MAX_SAMPLES == 12002);
    T_ASSERT(GHOST_FILE_MAX == 40 + 24 * 12002);
}

int main(void) {
    T_RUN(test_seed_is_stable_per_course_and_never_zero);
    T_RUN(test_track_hash_changes_with_course);
    T_RUN(test_progress_runs_from_start_to_nmarks);
    T_RUN(test_records_on_the_grid);
    T_RUN(test_pose_interpolates_and_clamps);
    T_RUN(test_heading_interpolates_across_the_wrap);
    T_RUN(test_time_at_progress_inverts_progress);
    T_RUN(test_overflow_is_not_usable);
    T_RUN(test_round_trip);
    T_RUN(test_peek_reads_the_finish_time_from_the_header);
    T_RUN(test_matches_rejects_other_conditions);
    T_RUN(test_decode_rejects_malformed);
    T_RUN(test_decode_rejects_nonfinite_position);
    T_RUN(test_begin_clears_the_previous_run);
    T_RUN(test_capacity_of_twenty_minutes);
    return T_DONE();
}
