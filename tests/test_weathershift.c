#include "t.h"
#include "weathershift.h"
#include "weather.h"

#define DT (1.0f / 30.0f)
#define BIT(p) (1u << (p))

static void test_neighbour_skips_unticked(void) {
    uint32_t m = BIT(WEATHER_SUNNY) | BIT(WEATHER_CHOPPY) | BIT(WEATHER_STORM);
    T_ASSERT(wshift_neighbour(m, WEATHER_CHOPPY, 0.1f) == WEATHER_SUNNY);
    T_ASSERT(wshift_neighbour(m, WEATHER_CHOPPY, 0.9f) == WEATHER_STORM);
}

static void test_neighbour_ends_go_one_way(void) {
    uint32_t m = BIT(WEATHER_SUNNY) | BIT(WEATHER_CHOPPY) | BIT(WEATHER_STORM);
    for (float c = 0.0f; c < 1.0f; c += 0.25f) {
        T_ASSERT(wshift_neighbour(m, WEATHER_SUNNY, c) == WEATHER_CHOPPY);
        T_ASSERT(wshift_neighbour(m, WEATHER_STORM, c) == WEATHER_CHOPPY);
    }
}

static void test_neighbour_single_tick_none(void) {
    T_ASSERT(wshift_neighbour(BIT(WEATHER_CLOUDY), WEATHER_CLOUDY, 0.3f) == -1);
}

static void test_init_start_is_ticked_and_varies(void) {
    uint32_t m = WEATHER_JOURNEY_DEFAULT;
    unsigned seen = 0;
    for (uint32_t seed = 1; seed <= 50; seed++) {
        WeatherShift s;
        int p = wshift_init(&s, m, WEATHER_CLOUDY, seed, 60, 180);
        T_ASSERT(p >= 0 && p < WEATHER_PRESETS && (m & BIT(p)));
        T_ASSERT(s.from == p && s.to == p && s.progress == 1.0f);
        seen |= BIT(p);
    }
    T_ASSERT((seen & (seen - 1)) != 0);
}

static void test_init_empty_mask_uses_fallback(void) {
    WeatherShift s;
    T_ASSERT(wshift_init(&s, 0, 3, 5, 60, 180) == 3);
    T_ASSERT(s.mask == BIT(3));
    /* bits beyond the presets are ignored too */
    T_ASSERT(wshift_init(&s, 0xFFFFFF80u, 2, 5, 60, 180) == 2);
    T_ASSERT(s.mask == BIT(2));
}

static void test_gap_within_range(void) {
    for (uint32_t seed = 1; seed <= 200; seed++) {
        WeatherShift s;
        wshift_init(&s, WEATHER_JOURNEY_DEFAULT, WEATHER_CLOUDY, seed, 60, 180);
        T_ASSERT(s.gap >= 60 && s.gap <= 180);
        int ends = 0;
        for (int i = 0; i < 100000 && ends < 3; i++)
            if (wshift_step(&s, 0.5f, 60, 180, 10) == WSHIFT_END) {
                T_ASSERT(s.gap >= 60 && s.gap <= 180);
                ends++;
            }
        T_ASSERT(ends == 3);
    }
}

static void test_single_tick_never_shifts(void) {
    WeatherShift s;
    wshift_init(&s, BIT(WEATHER_CLOUDY), WEATHER_CLOUDY, 9, 60, 180);
    for (int i = 0; i < 30000; i++) T_ASSERT(wshift_step(&s, DT, 60, 180, 10) == WSHIFT_NONE);
    T_ASSERT(s.from == WEATHER_CLOUDY && s.to == WEATHER_CLOUDY);
}

static void test_blend_lifecycle(void) {
    WeatherShift s;
    wshift_init(&s, WEATHER_JOURNEY_DEFAULT, WEATHER_CLOUDY, 4, 5, 5);
    float t = 0, started = -1, ended = -1, next = -1;
    for (int i = 0; i < 30 * 40 && next < 0; i++) {
        WShiftEvent e = wshift_step(&s, DT, 5, 5, 2);
        t += DT;
        if (e == WSHIFT_START && started < 0) {
            started = t;
            T_ASSERT(s.from != s.to);
            T_ASSERT((s.mask & BIT(s.from)) && (s.mask & BIT(s.to)));
        } else if (e == WSHIFT_END && ended < 0) {
            ended = t;
            T_ASSERT(s.from == s.to && s.progress == 1.0f);
        } else if (e == WSHIFT_START && ended >= 0) {
            next = t;
        }
    }
    T_NEAR(started, 5.0f, 2 * DT);
    T_NEAR(ended - started, 2.0f, 2 * DT);
    T_NEAR(next - ended, 5.0f, 2 * DT);
}

static void test_long_frames_clamp_progress(void) {
    WeatherShift s;
    wshift_init(&s, WEATHER_JOURNEY_DEFAULT, WEATHER_CLOUDY, 4, 1, 1);
    int ends = 0;
    while (wshift_step(&s, 0.5f, 1, 1, 0.25f) != WSHIFT_START) {}
    for (int i = 0; i < 50 && !ends; i++) {
        WShiftEvent e = wshift_step(&s, 0.1f, 1, 1, 0.25f);
        T_ASSERT(s.progress <= 1.0f);
        float w = wshift_weight(&s);
        T_ASSERT(w >= 0.0f && w <= 1.0f);
        if (e == WSHIFT_END) ends++;
    }
    T_ASSERT(ends == 1);
}

int main(void) {
    T_RUN(test_neighbour_skips_unticked);
    T_RUN(test_neighbour_ends_go_one_way);
    T_RUN(test_neighbour_single_tick_none);
    T_RUN(test_init_start_is_ticked_and_varies);
    T_RUN(test_init_empty_mask_uses_fallback);
    T_RUN(test_gap_within_range);
    T_RUN(test_single_tick_never_shifts);
    T_RUN(test_blend_lifecycle);
    T_RUN(test_long_frames_clamp_progress);
    return T_DONE();
}
