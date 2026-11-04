#include "t.h"
#include <math.h>
#include <stdio.h>
#include "controls.h"
#include "mathx.h"
#include "training.h"

/* The lesson against the real sim: scripted helm, auto-trim taps and engine holds, run from the same start the app uses
   (steady wind, boat pointing straight downwind). */
#define DT (1.0f / 30)

static Game g;
static BoatCfg bc;
static WeatherCfg wcfg;
static WorldCfg wc;
static Training tr;
static char buf[16384];

static size_t slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    T_ASSERT(f != NULL);
    const size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return n;
}

static void start(void) {
    size_t n = slurp("../boats/sunfish.json");
    boatcfg_default(&bc);
    T_ASSERT(boatcfg_load(&bc, buf, n, NULL, 0));
    n = slurp("../config/weather.json");
    weather_default(&wcfg);
    T_ASSERT(weather_load(&wcfg, buf, n, NULL, 0));
    n = slurp("../config/world.json");
    world_default(&wc);
    T_ASSERT(world_load(&wc, buf, n, NULL, 0));
    wcfg.preset[WEATHER_SUNNY].wind_var = 0;
    wcfg.preset[WEATHER_SUNNY].shift_rate_deg = 0;
    wcfg.preset[WEATHER_SUNNY].gust_speed = 0;
    game_init(&g, &bc, &wcfg, &wc, WEATHER_SUNNY, 12345);
    game_set_track(&g, NULL);
    g.b.heading = g.wind.dir;
    training_init(&tr);
}

/* one frame; the lesson sees it the way app.c does, with the engine kept off until its step */
static void frame(uint32_t pressed, uint32_t held, float crank) {
    if (training_engine_locked(&tr)) g.en.on = false;
    game_update(&g, pressed, held, crank, DT);
    if (training_engine_locked(&tr)) g.en.on = false;
    training_update(&tr, &g, DT);
}

static void tap_b(void) {
    frame(BTN_B, BTN_B, 0);
    frame(0, 0, 0);
}
static void hold_b(void) {
    frame(BTN_B, BTN_B, 0);
    for (int i = 0; i < 45; i++) frame(0, BTN_B, 0);
    frame(0, 0, 0);
}

/* crank the wheel toward a heading `off` rad off the wind's source, on the given tack (+1 / -1) */
static float steer_error(float off, int tack) {
    const float source = wrap_pi(g.wind.dir + (float)M_PI);
    return wrap_pi(source + tack * off - g.b.heading);
}
static void sail(float off, int tack, float seconds) {
    for (float t = 0; t < seconds; t += DT) {
        float err = steer_error(off, tack);
        float crank = clampf(err * 200.0f, -25.0f, 25.0f);   /* deg of crank this frame; the wheel holds its position */
        frame(0, 0, crank);
    }
}

static void test_downwind_with_auto_trim_completes_first_step(void) {
    start();
    sail(deg2rad(180), 1, 3);
    T_ASSERT(tr.step == TRAIN_DOWNWIND_AUTO);   /* auto trim is still off */
    tap_b();
    T_ASSERT(g.at.on);
    sail(deg2rad(180), 1, 4);
    T_ASSERT(tr.step > TRAIN_DOWNWIND_AUTO);
}

static void test_start_heading_reaches_no_goal_by_itself(void) {
    start();
    for (int i = 0; i < 300; i++) frame(0, 0, 0);
    T_ASSERT(tr.step == TRAIN_DOWNWIND_AUTO);
}

static void test_close_hauled_then_irons_then_engine_frees_the_boat(void) {
    start();
    tap_b();
    sail(deg2rad(175), 1, 4);
    T_ASSERT(tr.step == TRAIN_BEAM_AUTO);
    sail(deg2rad(90), 1, 12);
    T_ASSERT(tr.step == TRAIN_POINTS_AUTO);
    sail(deg2rad(60), 1, 12);
    sail(deg2rad(130), 1, 12);
    T_ASSERT(tr.step == TRAIN_CLOSE_HAULED);
    sail(deg2rad(42), 1, 12);
    T_ASSERT(tr.step == TRAIN_IRONS);
    /* point at the wind and let the boat stall; it must not turn itself out of irons */
    sail(deg2rad(5), 1, 40);
    T_ASSERT(tr.step == TRAIN_ENGINE_FREE);
    T_ASSERT(!g.en.on);
    /* engine on, bow turned off the wind: the boat is free and moving */
    hold_b();
    T_ASSERT(g.en.on);
    sail(deg2rad(70), 1, 30);
    T_ASSERT(tr.step > TRAIN_ENGINE_FREE);
}

int main(void) {
    T_RUN(test_downwind_with_auto_trim_completes_first_step);
    T_RUN(test_start_heading_reaches_no_goal_by_itself);
    T_RUN(test_close_hauled_then_irons_then_engine_frees_the_boat);
    T_DONE();
}
