#include "t.h"
#include "weather.h"
#include <string.h>

#define SECS 600
#define DT 0.05f

static void run(int preset, uint32_t seed, float *out_speed, float *out_dir, int n) {
    WeatherCfg w;
    weather_default(&w);
    Wind wd;
    wind_init(&wd, &w.preset[preset], seed);
    for (int i = 0; i < n; i++) {
        wind_step(&wd, &w.preset[preset], DT);
        out_speed[i] = wd.speed;
        out_dir[i] = wd.dir;
    }
}

static void test_speed_bounded_per_preset(void) {
    WeatherCfg w;
    weather_default(&w);
    int n = (int)(SECS / DT);
    float *s = malloc(sizeof(float) * (size_t)n), *d = malloc(sizeof(float) * (size_t)n);
    for (int p = 0; p < WEATHER_PRESETS; p++) {
        run(p, 7, s, d, n);
        float hi = 1.5f * (w.preset[p].wind_base + w.preset[p].gust_speed);
        for (int i = 0; i < n; i++) T_ASSERT(s[i] >= 0 && s[i] <= hi && isfinite(s[i]));
    }
    free(s); free(d);
}

static float mean_speed(int preset) {
    int n = (int)(SECS / DT);
    float *s = malloc(sizeof(float) * (size_t)n), *d = malloc(sizeof(float) * (size_t)n);
    run(preset, 3, s, d, n);
    double sum = 0;
    for (int i = 0; i < n; i++) sum += s[i];
    free(s); free(d);
    return (float)(sum / n);
}

static void test_storm_stronger_than_sunny(void) {
    T_ASSERT(mean_speed(WEATHER_STORM) > mean_speed(WEATHER_SUNNY) + 3.0f);
}

static void test_same_seed_identical_different_seed_differs(void) {
    int n = (int)(SECS / DT);
    float *a = malloc(sizeof(float) * (size_t)n), *ad = malloc(sizeof(float) * (size_t)n);
    float *b = malloc(sizeof(float) * (size_t)n), *bd = malloc(sizeof(float) * (size_t)n);
    float *c = malloc(sizeof(float) * (size_t)n), *cd = malloc(sizeof(float) * (size_t)n);
    run(WEATHER_CLOUDY, 42, a, ad, n);
    run(WEATHER_CLOUDY, 42, b, bd, n);
    run(WEATHER_CLOUDY, 43, c, cd, n);
    T_ASSERT(memcmp(a, b, sizeof(float) * (size_t)n) == 0);
    T_ASSERT(memcmp(ad, bd, sizeof(float) * (size_t)n) == 0);
    int diff = 0;
    for (int i = 0; i < n; i++) if (a[i] != c[i] || ad[i] != cd[i]) diff++;
    T_ASSERT(diff > n / 2);
    free(a); free(ad); free(b); free(bd); free(c); free(cd);
}

static void test_direction_rate_bounded(void) {
    WeatherCfg w;
    weather_default(&w);
    int n = (int)(SECS / DT);
    float *s = malloc(sizeof(float) * (size_t)n), *d = malloc(sizeof(float) * (size_t)n);
    for (int p = 0; p < WEATHER_PRESETS; p++) {
        run(p, 11, s, d, n);
        float lim = deg2rad(w.preset[p].shift_rate_deg) * 1.05f;
        for (int i = 1; i < n; i++)
            T_ASSERT(fabsf(wrap_pi(d[i] - d[i - 1])) / DT <= lim);
    }
    free(s); free(d);
}

static void test_huge_and_zero_dt_stay_finite(void) {
    WeatherCfg w;
    weather_default(&w);
    Wind wd;
    wind_init(&wd, &w.preset[WEATHER_STORM], 1);
    wind_step(&wd, &w.preset[WEATHER_STORM], 0.0f);
    T_ASSERT(isfinite(wd.speed) && isfinite(wd.dir));
    wind_step(&wd, &w.preset[WEATHER_STORM], 1e6f);
    T_ASSERT(isfinite(wd.speed) && isfinite(wd.dir) && wd.speed >= 0);
    wind_step(&wd, &w.preset[WEATHER_STORM], -5.0f);
    T_ASSERT(isfinite(wd.speed) && isfinite(wd.dir) && wd.speed >= 0);
}

static void test_wind_init_gives_nonzero_wind(void) {
    WeatherCfg w;
    weather_default(&w);
    for (uint32_t seed = 0; seed < 20; seed++) {
        Wind wd;
        wind_init(&wd, &w.preset[WEATHER_SUNNY], seed);
        T_ASSERT(wd.speed > 0.5f);
        float wx, wy;
        wind_vec(&wd, &wx, &wy);
        T_NEAR(sqrtf(wx * wx + wy * wy), wd.speed, 1e-3f);
    }
}

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
    char *buf = slurp("../config/weather.json", &n);
    T_ASSERT(buf && n > 0);
    WeatherCfg d, l;
    weather_default(&d);
    char warn[256];
    T_ASSERT(weather_load(&l, buf, n, warn, sizeof warn));
    T_ASSERT(warn[0] == 0);
    T_ASSERT(memcmp(&d, &l, sizeof d) == 0);
    free(buf);
}

static void test_load_clamps_and_warns(void) {
    WeatherCfg l;
    char warn[256];
    const char *s = "{\"storm\":{\"wind_base\":-3,\"wave_period\":\"x\"},\"mist\":5}";
    T_ASSERT(!weather_load(&l, s, strlen(s), warn, sizeof warn));
    T_ASSERT(strstr(warn, "storm.wind_base") != NULL);
    T_ASSERT(strstr(warn, "storm.wave_period") != NULL);
    T_ASSERT(strstr(warn, "mist") != NULL);
    T_ASSERT(l.preset[WEATHER_STORM].wind_base >= 0);
    WeatherCfg d;
    weather_default(&d);
    T_ASSERT(l.preset[WEATHER_STORM].wave_period == d.preset[WEATHER_STORM].wave_period);
    T_ASSERT(!weather_load(&l, "garbage", 7, warn, sizeof warn));
    T_ASSERT(memcmp(&l, &d, sizeof d) == 0);
    T_ASSERT(!weather_load(&l, NULL, 0, warn, sizeof warn));
}

int main(void) {
    T_RUN(test_speed_bounded_per_preset);
    T_RUN(test_storm_stronger_than_sunny);
    T_RUN(test_same_seed_identical_different_seed_differs);
    T_RUN(test_direction_rate_bounded);
    T_RUN(test_huge_and_zero_dt_stay_finite);
    T_RUN(test_wind_init_gives_nonzero_wind);
    T_RUN(test_shipped_config_equals_default);
    T_RUN(test_load_clamps_and_warns);
    return T_DONE();
}
