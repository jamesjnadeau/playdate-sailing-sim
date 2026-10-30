#include "t.h"
#include "boatcfg.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define W 512

static bool load(BoatCfg *c, const char *s, char *warn) {
    return boatcfg_load(c, s, strlen(s), warn, W);
}

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = malloc(65536);
    *n = fread(b, 1, 65536, f);
    fclose(f);
    return b;
}

static void test_default_is_sane(void) {
    BoatCfg d;
    boatcfg_default(&d);
    T_ASSERT(d.sail_count == 2);
    T_NEAR(d.hull.length, 4.0f, 1.0f);
    T_NEAR(d.camera.roll_limit_deg, 10.0f, 0);
    T_NEAR(d.hull.capsize_deg, 70.0f, 0);
    T_ASSERT(d.hull.hull_speed > 0 && d.hull.mass > 0);
    T_ASSERT(d.sails[1].slot_gain > 0 && d.sails[0].slot_gain == 0);
}

static void test_dinghy_json_reproduces_default(void) {
    size_t n = 0;
    char *buf = slurp("../boats/dinghy.json", &n);
    T_ASSERT(buf && n > 0);
    BoatCfg d, l;
    char warn[W];
    boatcfg_default(&d);
    T_ASSERT(boatcfg_load(&l, buf, n, warn, W));
    T_ASSERT(warn[0] == 0);
    T_ASSERT(memcmp(&d, &l, sizeof d) == 0);
    free(buf);
}

static void test_to_json_round_trips(void) {
    BoatCfg d, l;
    char warn[W];
    boatcfg_default(&d);
    char *buf = malloc(65536);
    size_t n = boatcfg_to_json(&d, buf, 65536);
    T_ASSERT(n > 100 && n < 65536);
    T_ASSERT(boatcfg_load(&l, buf, n, warn, W));
    T_ASSERT(memcmp(&d, &l, sizeof d) == 0);
    free(buf);
}

static void test_out_of_range_clamps_and_warns(void) {
    BoatCfg c, d;
    char warn[W];
    boatcfg_default(&d);
    T_ASSERT(!load(&c, "{\"hull\":{\"mass\":-5}}", warn));
    T_ASSERT(strstr(warn, "hull.mass") != NULL);
    T_ASSERT(c.hull.mass > 0);
    T_ASSERT(c.hull.mass < d.hull.mass);   /* clamped to its min, not reset */
    T_NEAR(c.hull.length, d.hull.length, 0);
}

static void test_wrong_type_and_missing_group(void) {
    BoatCfg c, d;
    char warn[W];
    boatcfg_default(&d);
    T_ASSERT(!load(&c, "{\"hull\":{\"mass\":\"heavy\"},\"camera\":5}", warn));
    T_ASSERT(strstr(warn, "hull.mass") != NULL);
    T_ASSERT(strstr(warn, "camera") != NULL);
    T_NEAR(c.hull.mass, d.hull.mass, 0);
    T_NEAR(c.camera.height, d.camera.height, 0);

    T_ASSERT(load(&c, "{\"hull\":{\"length\":3.5}}", warn));   /* other groups simply absent */
    T_NEAR(c.hull.length, 3.5f, 1e-6f);
    T_NEAR(c.foils.rudder_area, d.foils.rudder_area, 0);
    T_NEAR(c.camera.roll_limit_deg, 10.0f, 0);
}

static void test_sail_counts(void) {
    BoatCfg c, d;
    char warn[W];
    boatcfg_default(&d);
    T_ASSERT(!load(&c, "{\"sails\":[]}", warn));
    T_ASSERT(c.sail_count == d.sail_count);
    T_ASSERT(memcmp(&c.sails, &d.sails, sizeof d.sails) == 0);

    char nine[256] = "{\"sails\":[";
    for (int i = 0; i < 9; i++) strcat(nine, i ? ",{\"area\":3}" : "{\"area\":3}");
    strcat(nine, "]}");
    T_ASSERT(!load(&c, nine, warn));
    T_ASSERT(c.sail_count == 8);
    T_ASSERT(strstr(warn, "sails") != NULL);
    T_NEAR(c.sails[7].area, 3.0f, 0);

    T_ASSERT(load(&c, "{\"sails\":[{\"name\":\"kite\",\"area\":9,\"awa_min_deg\":100}]}", warn));
    T_ASSERT(c.sail_count == 1);
    T_ASSERT(strcmp(c.sails[0].name, "kite") == 0);
    T_NEAR(c.sails[0].awa_min_deg, 100.0f, 0);
}

static void test_tables(void) {
    BoatCfg c, d;
    char warn[W];
    boatcfg_default(&d);
    T_ASSERT(load(&c, "{\"sails\":[{\"cl\":[[0,0],[10,1.2],[90,0.5]]}]}", warn));
    T_ASSERT(c.sails[0].cl.n == 3);
    T_NEAR(c.sails[0].cl.p[1].y, 1.2f, 1e-6f);
    T_ASSERT(!load(&c, "{\"hull\":{\"righting\":[[10,5],[5,9]]}}", warn));   /* x not ascending */
    T_ASSERT(strstr(warn, "hull.righting") != NULL);
    T_ASSERT(c.hull.righting.n == d.hull.righting.n);
}

static void test_garbage_buffer_gives_default(void) {
    BoatCfg c, d;
    char warn[W];
    boatcfg_default(&d);
    T_ASSERT(!load(&c, "{\"hull\":", warn));
    T_ASSERT(warn[0] != 0);
    T_ASSERT(memcmp(&c, &d, sizeof d) == 0);
    T_ASSERT(!load(&c, "[1,2]", warn));
    T_ASSERT(memcmp(&c, &d, sizeof d) == 0);
    T_ASSERT(!boatcfg_load(&c, NULL, 0, NULL, 0));
    T_ASSERT(memcmp(&c, &d, sizeof d) == 0);
}

static void check_keelboat(const char *path, float min_len, int shape2_awa_min) {
    size_t n;
    char *buf = slurp(path, &n);
    BoatCfg c;
    char warn[W];
    T_ASSERT(buf != NULL);
    T_ASSERT(n < 16000);                                     /* assets.c reads boats into a 16 KB buffer */
    T_ASSERT(boatcfg_load(&c, buf, n, warn, sizeof warn));   /* no warnings */
    T_ASSERT(c.hull.length >= min_len);
    T_ASSERT(c.sail_count == 3);
    T_ASSERT(c.sails[2].shape == 2);
    T_ASSERT(c.sails[2].awa_min_deg >= (float)shape2_awa_min);
    free(buf);
}

static void test_j24_json(void) { check_keelboat("../boats/j24.json", 7.3f, 90); }
static void test_tp52_json(void) { check_keelboat("../boats/tp52.json", 15.8f, 80); }

int main(void) {
    T_RUN(test_default_is_sane);
    T_RUN(test_dinghy_json_reproduces_default);
    T_RUN(test_to_json_round_trips);
    T_RUN(test_out_of_range_clamps_and_warns);
    T_RUN(test_wrong_type_and_missing_group);
    T_RUN(test_sail_counts);
    T_RUN(test_tables);
    T_RUN(test_garbage_buffer_gives_default);
    T_RUN(test_j24_json);
    T_RUN(test_tp52_json);
    return T_DONE();
}
