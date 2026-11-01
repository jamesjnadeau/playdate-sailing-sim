#include "t.h"
#include "boatcfg.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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

static void test_clr_depth_defaults_loads_and_clamps(void) {
    BoatCfg d, c, r, back;
    char warn[W];
    boatcfg_default(&d);
    T_NEAR(d.hull.clr_depth, 0.4f, 1e-6);
    T_ASSERT(load(&c, "{\"hull\":{\"clr_depth\":1.8}}", warn));
    T_NEAR(c.hull.clr_depth, 1.8f, 1e-6);
    T_ASSERT(!load(&c, "{\"hull\":{\"clr_depth\":99}}", warn));
    T_ASSERT(strstr(warn, "hull.clr_depth") != NULL);
    T_NEAR(c.hull.clr_depth, 6.0f, 1e-6);
    /* round trip keeps a non-default value */
    T_ASSERT(load(&r, "{\"hull\":{\"clr_depth\":1.8}}", warn));
    char *buf = malloc(65536);
    size_t n = boatcfg_to_json(&r, buf, 65536);
    T_ASSERT(n > 100 && n < 65536);
    T_ASSERT(boatcfg_load(&back, buf, n, warn, W));
    T_NEAR(back.hull.clr_depth, 1.8f, 1e-6);
    free(buf);
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

static void load_boat(const char *path, BoatCfg *c) {
    size_t n;
    char *buf = slurp(path, &n);
    char warn[W];
    T_ASSERT(buf != NULL);
    T_ASSERT(n < 16000);                                     /* assets.c reads boats into a 16 KB buffer */
    T_ASSERT(boatcfg_load(c, buf, n, warn, sizeof warn));    /* no warnings */
    free(buf);
}

static void test_j24_json(void) {
    BoatCfg c; load_boat("../boats/j24.json", &c);
    T_ASSERT(c.hull.length >= 7.3f);
    T_ASSERT(c.sail_count == 2);                             /* main + jib, no spinnaker */
    T_ASSERT(c.sails[1].shape == 1);
}

static void test_tp52_json(void) {
    BoatCfg c; load_boat("../boats/tp52.json", &c);
    T_ASSERT(c.hull.length >= 15.8f);
    T_ASSERT(c.sail_count == 3);
    T_ASSERT(c.sails[2].shape == 2 && c.sails[2].awa_min_deg >= 80);
    T_ASSERT(strcmp(c.sails[2].name, "spinnaker") == 0);
    T_NEAR(c.camera.horizon_y, 124.0, 1e-3);
    BoatCfg d; boatcfg_default(&d);
    T_NEAR(d.camera.horizon_y, 0.0, 1e-6);
}

static void test_sunfish_json(void) {
    BoatCfg c, d; load_boat("../boats/sunfish.json", &c);
    boatcfg_default(&d);
    T_ASSERT(c.sail_count == 1 && c.sails[0].shape == 0);    /* one lateen mainsail, no jib */
    T_ASSERT(c.sails[0].area > d.sails[0].area);             /* bigger than the dinghy's main */
    T_ASSERT(c.look.mast_x > d.look.mast_x);                 /* mast well forward */
}

static void test_square_shape_accepted(void) {
    BoatCfg c;
    char warn[W];
    T_ASSERT(load(&c, "{\"sails\":[{\"name\":\"sq\",\"shape\":3}]}", warn));
    T_ASSERT(warn[0] == 0);
    T_ASSERT(c.sails[0].shape == 3);
    T_ASSERT(!load(&c, "{\"sails\":[{\"shape\":4}]}", warn));   /* past the top of the range */
    T_ASSERT(strstr(warn, "shape") != NULL);
    T_ASSERT(c.sails[0].shape == 3);
    T_ASSERT(!load(&c, "{\"sails\":[{\"shape\":-1}]}", warn));  /* below the bottom */
    T_ASSERT(strstr(warn, "shape") != NULL);
    T_ASSERT(c.sails[0].shape == 0);
}

static void test_boat_class_defaults_to_racer(void) {
    BoatCfg d, c;
    char warn[W];
    boatcfg_default(&d);
    T_ASSERT(d.boat_class == BOAT_CLASS_RACER);
    T_ASSERT(load(&c, "{}", warn) && c.boat_class == BOAT_CLASS_RACER);
    load_boat("../boats/dinghy.json", &c);
    T_ASSERT(c.boat_class == BOAT_CLASS_RACER);
    load_boat("../boats/sunfish.json", &c);
    T_ASSERT(c.boat_class == BOAT_CLASS_RACER);
    load_boat("../boats/j24.json", &c);
    T_ASSERT(c.boat_class == BOAT_CLASS_RACER);
    load_boat("../boats/tp52.json", &c);
    T_ASSERT(c.boat_class == BOAT_CLASS_RACER);
}

static void test_boat_class_cruiser_accepted(void) {
    BoatCfg c;
    char warn[W];
    T_ASSERT(load(&c, "{\"boat_class\":1}", warn));
    T_ASSERT(warn[0] == 0);
    T_ASSERT(c.boat_class == BOAT_CLASS_CRUISER);
}

static void test_boat_class_out_of_range_clamps(void) {
    BoatCfg c;
    char warn[W];
    T_ASSERT(!load(&c, "{\"boat_class\":2}", warn));
    T_ASSERT(strstr(warn, "boat_class") != NULL);
    T_ASSERT(c.boat_class == BOAT_CLASS_CRUISER);
    T_ASSERT(!load(&c, "{\"boat_class\":-1}", warn));
    T_ASSERT(strstr(warn, "boat_class") != NULL);
    T_ASSERT(c.boat_class == BOAT_CLASS_RACER);
}

static void test_boat_class_round_trips(void) {
    BoatCfg d, l;
    char warn[W];
    boatcfg_default(&d);
    d.boat_class = BOAT_CLASS_CRUISER;
    char *buf = malloc(65536);
    size_t n = boatcfg_to_json(&d, buf, 65536);
    T_ASSERT(n > 100 && n < 65536);
    T_ASSERT(boatcfg_load(&l, buf, n, warn, W));
    T_ASSERT(warn[0] == 0);
    T_ASSERT(l.boat_class == BOAT_CLASS_CRUISER);
    free(buf);
}

static void test_longship_json(void) {
    BoatCfg c; load_boat("../boats/longship.json", &c);
    T_ASSERT(c.hull.length >= 11 && c.hull.length <= 13);
    T_ASSERT(c.sail_count == 1);                             /* one square sail */
    T_ASSERT(c.sails[0].shape == 3);
    T_ASSERT(c.sails[0].trim_max_deg == 90);                 /* yard can square across */
    T_ASSERT(c.sails[0].awa_min_deg >= 40 && c.sails[0].awa_min_deg <= 70);
    T_ASSERT(c.sails[0].awa_max_deg == 180);
    T_ASSERT(fabsf(c.look.mast_x) < 0.5f * c.hull.length);   /* mast inside the hull */
    T_ASSERT(c.controls.autopilot_no_go_deg >= 60);          /* A never steers into the no-go zone */
    T_ASSERT(c.boat_class == BOAT_CLASS_CRUISER);            /* cannot sail upwind, free range only */
}

static void test_every_boat_has_a_cockpit_inside_the_hull(void) {
    static const char *files[] = { "../boats/dinghy.json", "../boats/sunfish.json", "../boats/j24.json", "../boats/tp52.json", "../boats/longship.json" };
    for (int i = 0; i < 5; i++) {
        size_t n = 0;
        char *buf = slurp(files[i], &n);
        BoatCfg c;
        char warn[W];
        T_ASSERT(buf && boatcfg_load(&c, buf, n, warn, W) && warn[0] == 0);
        const LookCfg *l = &c.look;
        float hb = 0.5f * c.hull.beam, half = 0.5f * c.hull.length;
        T_ASSERT(l->cockpit_depth > 0 && l->cockpit_depth < l->freeboard + 0.3f);
        T_ASSERT(l->cockpit_x0 < l->cockpit_x1 && l->cockpit_x0 > -half && l->cockpit_x1 < half);
        T_ASSERT(l->cockpit_w0 > 0 && l->cockpit_w1 > 0 && l->cockpit_w0 < hb && l->cockpit_w1 < hb);
        free(buf);
    }
}

int main(void) {
    T_RUN(test_default_is_sane);
    T_RUN(test_dinghy_json_reproduces_default);
    T_RUN(test_to_json_round_trips);
    T_RUN(test_clr_depth_defaults_loads_and_clamps);
    T_RUN(test_out_of_range_clamps_and_warns);
    T_RUN(test_wrong_type_and_missing_group);
    T_RUN(test_sail_counts);
    T_RUN(test_tables);
    T_RUN(test_garbage_buffer_gives_default);
    T_RUN(test_j24_json);
    T_RUN(test_tp52_json);
    T_RUN(test_sunfish_json);
    T_RUN(test_square_shape_accepted);
    T_RUN(test_boat_class_defaults_to_racer);
    T_RUN(test_boat_class_cruiser_accepted);
    T_RUN(test_boat_class_out_of_range_clamps);
    T_RUN(test_boat_class_round_trips);
    T_RUN(test_longship_json);
    T_RUN(test_every_boat_has_a_cockpit_inside_the_hull);
    return T_DONE();
}
