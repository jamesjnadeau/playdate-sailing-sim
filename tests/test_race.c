#include "t.h"
#include <math.h>
#include <string.h>
#include "race.h"
#include "mathx.h"

#define D2R(d) ((float)((d) * 3.14159265358979323846 / 180.0))

static void expect_same_track(const Track *a, const Track *b) {
    T_ASSERT(strcmp(a->name, b->name) == 0);
    T_NEAR(a->wind_from, b->wind_from, 1e-5);
    T_NEAR(a->start_x, b->start_x, 1e-4);
    T_NEAR(a->start_y, b->start_y, 1e-4);
    T_NEAR(wrap_pi(a->start_heading - b->start_heading), 0.0, 1e-5);
    T_ASSERT(a->nmarks == b->nmarks);
    for (int i = 0; i < a->nmarks; i++) {
        T_ASSERT(strcmp(a->marks[i].name, b->marks[i].name) == 0);
        T_NEAR(a->marks[i].x, b->marks[i].x, 1e-4);
        T_NEAR(a->marks[i].y, b->marks[i].y, 1e-4);
        T_NEAR(wrap_pi(a->marks[i].gate - b->marks[i].gate), 0.0, 1e-5);
        T_ASSERT(a->marks[i].rounding == b->marks[i].rounding);
    }
}

static void test_gate_port_rounding_counts(void) {
    T_ASSERT(gate_crossed(0, 0, 0, ROUND_PORT, 10, 5, 10, -5));
}

static void test_gate_wrong_direction_ignored(void) {
    T_ASSERT(!gate_crossed(0, 0, 0, ROUND_PORT, 10, -5, 10, 5));
    T_ASSERT(gate_crossed(0, 0, 0, ROUND_STARBOARD, 10, -5, 10, 5));
}

static void test_gate_ignores_back_extension(void) {
    T_ASSERT(!gate_crossed(0, 0, 0, ROUND_PORT, -10, 5, -10, -5));
}

static void test_gate_endpoint_on_ray_counts_once(void) {
    T_ASSERT(gate_crossed(0, 0, 0, ROUND_PORT, 10, 5, 10, 0));
    T_ASSERT(!gate_crossed(0, 0, 0, ROUND_PORT, 10, 0, 10, -5));
}

static void test_gate_nonfinite_false(void) {
    T_ASSERT(!gate_crossed(NAN, 0, 0, ROUND_PORT, 10, 5, 10, -5));
    T_ASSERT(!gate_crossed(0, 0, 0, ROUND_PORT, NAN, 5, 10, -5));
    T_ASSERT(!gate_crossed(0, 0, 0, ROUND_PORT, 10, 5, INFINITY, -5));
}

static void test_default_track_is_two_buoys(void) {
    Track t;
    track_default(&t);
    T_ASSERT(t.nmarks == 2);
    T_NEAR(t.marks[0].x, 400.0, 1e-4);
    T_NEAR(t.marks[0].y, 0.0, 1e-4);
    T_NEAR(t.marks[1].x, 0.0, 1e-4);
    T_NEAR(t.marks[1].y, 0.0, 1e-4);
    T_NEAR(t.wind_from, D2R(45), 1e-5);
    float line = atan2f(t.marks[0].y - t.marks[1].y, t.marks[0].x - t.marks[1].x);
    T_NEAR(fabsf(wrap_pi(t.wind_from - line)), D2R(45), 1e-4);
}

static void test_load_matches_default(void) {
    static const char json[] =
        "{\n"
        "  \"name\": \"Two Buoys\",\n"
        "  \"wind_from_deg\": 45,\n"
        "  \"start\": { \"x\": -60, \"y\": 30, \"heading_deg\": -20 },\n"
        "  \"marks\": [\n"
        "    { \"name\": \"B\", \"x\": 400, \"y\": 0, \"gate_deg\": 0,   \"rounding\": \"port\" },\n"
        "    { \"name\": \"A\", \"x\": 0,   \"y\": 0, \"gate_deg\": 180, \"rounding\": \"port\" }\n"
        "  ]\n"
        "}\n";
    Track loaded, def;
    char warn[256];
    T_ASSERT(track_load(&loaded, json, strlen(json), warn, sizeof warn));
    T_ASSERT(warn[0] == 0);
    track_default(&def);
    expect_same_track(&loaded, &def);
}

static void test_wind_speed_kn(void) {
    Track t;
    char warn[256];
    static const char ok[] = "{\"wind_speed_kn\": 10, \"marks\":[{\"name\":\"M\",\"x\":1,\"y\":0,\"gate_deg\":0,\"rounding\":\"port\"}]}";
    track_load(&t, ok, strlen(ok), warn, sizeof warn);
    T_NEAR(t.wind_speed, 10.0 / 1.94384, 1e-4);
    T_ASSERT(strstr(warn, "wind_speed") == NULL);
    track_default(&t);
    T_ASSERT(t.wind_speed == 0.0f);                      /* unset: the weather preset decides */
    static const char hi[] = "{\"wind_speed_kn\": 500}";
    T_ASSERT(!track_load(&t, hi, strlen(hi), warn, sizeof warn));
    T_NEAR(t.wind_speed, 60.0 / 1.94384, 1e-4);          /* clamped, with a warning */
    static const char bad[] = "{\"wind_speed_kn\": \"fast\"}";
    track_load(&t, bad, strlen(bad), warn, sizeof warn);
    T_ASSERT(t.wind_speed == 0.0f);
    T_ASSERT(strstr(warn, "wind_speed_kn") != NULL);
}

static void test_load_bad_rounding_and_overflow_warn(void) {
    static char buf[4096];
    size_t n = 0;
    n += (size_t)snprintf(buf + n, sizeof buf - n, "{\"name\":\"Many\",\"marks\":[");
    for (int i = 0; i < 17; i++)
        n += (size_t)snprintf(buf + n, sizeof buf - n,
                              "%s{\"name\":\"M\",\"x\":%d,\"y\":0,\"gate_deg\":0,\"rounding\":\"%s\"}",
                              i ? "," : "", 10 * i, i == 0 ? "left" : "port");
    n += (size_t)snprintf(buf + n, sizeof buf - n, "]}");
    Track t;
    char warn[256];
    T_ASSERT(!track_load(&t, buf, n, warn, sizeof warn));
    T_ASSERT(t.nmarks == TRACK_MAX_MARKS);
    T_ASSERT(t.marks[0].rounding == ROUND_PORT);
    T_ASSERT(warn[0] != 0);
}

static void test_load_garbage_gives_default(void) {
    Track t, def;
    char warn[256];
    T_ASSERT(!track_load(&t, "{", 1, warn, sizeof warn));
    T_ASSERT(warn[0] != 0);
    track_default(&def);
    expect_same_track(&t, &def);
}

static const char *ROUTE_JSON =
    "{\"name\":\"R\",\"info\":\"hello\",\"wind_from_deg\":0,\"start\":{\"x\":0,\"y\":0,\"heading_deg\":0},"
    "\"marks\":[{\"name\":\"A\",\"x\":30,\"y\":0,\"gate_deg\":0,\"rounding\":\"port\"}],"
    "\"route\":[[0,0],[30,0],[30,40]]}";

static void test_route_and_info_load(void) {
    Track t;
    char warn[128];
    T_ASSERT(track_load(&t, ROUTE_JSON, strlen(ROUTE_JSON), warn, sizeof warn));
    T_ASSERT(strcmp(t.info, "hello") == 0);
    T_ASSERT(t.nroute == 3);
    T_NEAR(track_route_len(&t), 70.0, 1e-4);
    float x, y, h;
    track_route_at(&t, 45.0f, &x, &y, &h);
    T_NEAR(x, 30.0, 1e-4);
    T_NEAR(y, 15.0, 1e-4);
    T_NEAR(h, D2R(90), 1e-4);
    track_route_at(&t, 1000.0f, &x, &y, &h);   /* clamped to the end */
    T_NEAR(y, 40.0, 1e-4);
}

static void test_missing_route_uses_start_and_marks(void) {
    Track t;
    const char *js = "{\"name\":\"R\",\"wind_from_deg\":0,\"marks\":[{\"name\":\"A\",\"x\":30,\"y\":0,\"gate_deg\":0,\"rounding\":\"port\"}]}";
    char warn[128];
    track_load(&t, js, strlen(js), warn, sizeof warn);
    T_ASSERT(t.nroute == 2);
    T_NEAR(t.route[1][0], 30.0, 1e-4);
}

static void test_bad_route_warns_and_falls_back(void) {
    Track t;
    const char *js = "{\"name\":\"R\",\"route\":[[1,2],\"x\"],\"marks\":[{\"name\":\"A\",\"x\":30,\"y\":0,\"gate_deg\":0,\"rounding\":\"port\"}]}";
    char warn[128];
    T_ASSERT(!track_load(&t, js, strlen(js), warn, sizeof warn));
    T_ASSERT(t.nroute == 2);
    T_NEAR(t.route[1][0], 30.0, 1e-4);
}

static Track trk;
static Race r;

static void fresh_race(void) {
    track_default(&trk);
    race_init(&r);
}

static void test_race_rounds_in_order_and_finishes(void) {
    fresh_race();
    T_ASSERT(!race_update(&r, &trk, -10, -5, -10, 5, 0));   /* A first: not next */
    T_ASSERT(r.next == 0);
    T_ASSERT(race_update(&r, &trk, 410, 5, 410, -5, 0));    /* B to port */
    T_ASSERT(r.next == 1);
    T_ASSERT(race_update(&r, &trk, -10, -5, -10, 5, 0));    /* A to port: finish */
    T_ASSERT(r.finished && r.next == 2);
}

static void test_race_clock_freezes_at_finish(void) {
    fresh_race();
    race_update(&r, &trk, 100, 100, 100, 101, 0.5f);
    race_update(&r, &trk, 100, 101, 100, 102, 0.5f);
    T_NEAR(r.t, 1.0, 1e-9);
    race_update(&r, &trk, 410, 5, 410, -5, 0.5f);
    race_update(&r, &trk, -10, -5, -10, 5, 0.5f);
    T_ASSERT(r.finished);
    double tf = r.t;
    race_update(&r, &trk, 0, 0, 0, 1, 2.0f);
    T_NEAR(r.t, tf, 1e-12);
}

static void test_race_wrong_way_then_right_way(void) {
    fresh_race();
    T_ASSERT(!race_update(&r, &trk, 410, -5, 410, 5, 0));   /* wrong side */
    T_ASSERT(r.next == 0);
    T_ASSERT(race_update(&r, &trk, 410, 5, 410, -5, 0));    /* right side */
    T_ASSERT(r.next == 1);
}

static void test_race_shift_moves_local_marks(void) {
    fresh_race();
    race_shift(&r, 5120, 0);
    double x, y;
    race_mark_local(&r, &trk, 0, &x, &y);
    T_NEAR(x, 400 - 5120, 1e-6);
    T_NEAR(y, 0, 1e-6);
    T_ASSERT(!race_update(&r, &trk, -4740, 5, -4740, -5, 0)); /* behind the shifted mark */
    T_ASSERT(race_update(&r, &trk, -4710, 5, -4710, -5, 0)); /* shifted local coordinates */
    T_ASSERT(r.next == 1);
}

int main(void) {
    T_RUN(test_gate_port_rounding_counts);
    T_RUN(test_gate_wrong_direction_ignored);
    T_RUN(test_gate_ignores_back_extension);
    T_RUN(test_gate_endpoint_on_ray_counts_once);
    T_RUN(test_gate_nonfinite_false);
    T_RUN(test_default_track_is_two_buoys);
    T_RUN(test_load_matches_default);
    T_RUN(test_load_bad_rounding_and_overflow_warn);
    T_RUN(test_load_garbage_gives_default);
    T_RUN(test_route_and_info_load);
    T_RUN(test_missing_route_uses_start_and_marks);
    T_RUN(test_bad_route_warns_and_falls_back);
    T_RUN(test_race_rounds_in_order_and_finishes);
    T_RUN(test_wind_speed_kn);
    T_RUN(test_race_clock_freezes_at_finish);
    T_RUN(test_race_wrong_way_then_right_way);
    T_RUN(test_race_shift_moves_local_marks);
    T_DONE();
}
