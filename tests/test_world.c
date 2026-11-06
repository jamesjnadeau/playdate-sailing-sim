#include "t.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = malloc(65536);
    *n = fread(b, 1, 65536, f);
    fclose(f);
    return b;
}

static void test_shipped_config_equals_default(void) {
    size_t n = 0;
    char *buf = slurp("../config/world.json", &n);
    T_ASSERT(buf && n > 0);
    WorldCfg d, l;
    world_default(&d);
    char warn[256];
    T_ASSERT(world_load(&l, buf, n, warn, sizeof warn));
    T_ASSERT(warn[0] == 0);
    T_ASSERT(memcmp(&d, &l, sizeof d) == 0);
    free(buf);
}

static void test_load_clamps_and_warns(void) {
    WorldCfg l, d;
    world_default(&d);
    char warn[256];
    const char *s = "{\"journey_min_m\":9999,\"journey_max_m\":\"x\"}";
    T_ASSERT(!world_load(&l, s, strlen(s), warn, sizeof warn));
    T_ASSERT(strstr(warn, "journey_min_m") != NULL);
    T_ASSERT(strstr(warn, "journey_max_m") != NULL);
    T_ASSERT(l.journey_min_m <= 5000.0f);
    T_ASSERT(l.journey_max_m >= l.journey_min_m);
    T_ASSERT(!world_load(&l, "garbage", 7, warn, sizeof warn));
    T_ASSERT(memcmp(&l, &d, sizeof d) == 0);
    T_ASSERT(!world_load(&l, NULL, 0, warn, sizeof warn));
}

static void test_weather_shift_fields_load_and_clamp(void) {
    WorldCfg d, l;
    world_default(&d);
    T_ASSERT(d.journey_shift_min_s == 60.0f);
    T_ASSERT(d.journey_shift_max_s == 180.0f);
    T_ASSERT(d.weather_blend_s == 10.0f);
    char warn[256];
    const char *s = "{\"journey_shift_min_s\":1,\"weather_blend_s\":999}";
    T_ASSERT(!world_load(&l, s, strlen(s), warn, sizeof warn));
    T_ASSERT(l.journey_shift_min_s == 5.0f);
    T_ASSERT(l.weather_blend_s == 120.0f);
    s = "{\"journey_shift_min_s\":200,\"journey_shift_max_s\":100}";
    T_ASSERT(!world_load(&l, s, strlen(s), warn, sizeof warn));
    T_ASSERT(l.journey_shift_max_s == 200.0f);
}

int main(void) {
    T_RUN(test_shipped_config_equals_default);
    T_RUN(test_load_clamps_and_warns);
    T_RUN(test_weather_shift_fields_load_and_clamp);
    T_DONE();
}
