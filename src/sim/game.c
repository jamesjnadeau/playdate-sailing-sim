#include "game.h"
#include <math.h>
#include <string.h>

static int clamp_preset(int p) {
    return p < 0 ? 0 : p >= WEATHER_PRESETS ? WEATHER_PRESETS - 1 : p;
}

static void start_weather(Game *g) {
    const WeatherPreset *p = &g->weather.preset[g->preset];
    wind_init(&g->wind, p, g->seed);
    sea_init(&g->sea, p, g->seed);
    memset(&g->wf, 0, sizeof g->wf);
}

static void reset(Game *g) {
    boat_init(&g->b, &g->boat);
    boat_inputs_init(&g->in, &g->boat);
    controls_init(&g->ctl);
    autopilot_init(&g->ap);
    start_weather(g);
    g->t = 0.0;
    g->run = RUN_SAILING;
    g->last_hit = HIT_NONE;
    g->substeps = 0;
    g->nrocks = 0;
}

void game_init(Game *g, const BoatCfg *boat, const WeatherCfg *weather, const WorldCfg *world,
               int preset, uint32_t seed) {
    memset(g, 0, sizeof *g);
    g->boat = *boat;
    g->weather = *weather;
    g->world = *world;
    g->preset = clamp_preset(preset);
    g->seed = seed;
    reset(g);
}

void game_restart(Game *g, uint32_t new_seed) {
    g->seed = new_seed;
    reset(g);
}

void game_set_preset(Game *g, int preset) {
    g->preset = clamp_preset(preset);
    start_weather(g);
}

void game_update(Game *g, uint32_t pressed, uint32_t held, float crank_delta, float frame_dt) {
    float dt = isfinite(frame_dt) ? frame_dt : 1.0f / 30.0f;
    dt = clampf(dt, GAME_DT_MIN, GAME_DT_MAX);
    g->last_hit = HIT_NONE;

    if (g->run == RUN_SAILING) {   /* capsized or wrecked: controls are frozen */
        controls_update(&g->ctl, &g->boat, pressed, crank_delta, dt, &g->in);
        autopilot_buttons(&g->ap, &g->boat.controls, pressed, held, dt, g->b.heading,
                          wrap_pi(g->wind.dir + (float)M_PI));
        if (g->ap.on)
            g->in.wheel_target = autopilot_wheel(&g->ap, &g->boat.controls, g->b.heading, g->b.r,
                                                 wrap_pi(g->wind.dir + (float)M_PI));
    }

    wind_step(&g->wind, &g->weather.preset[g->preset], dt);
    g->wf = sea_hull_forces(&g->sea, &g->b, &g->boat, (float)fmod(g->t, g->sea.period));
    g->b.pitch = g->wf.pitch;
    g->b.heave = g->wf.heave;
    g->nrocks = rocks_near(&g->world, g->seed, (float)g->b.x, (float)g->b.y,
                           GAME_ROCK_RADIUS, g->rocks, ROCKS_MAX);

    int n = (int)ceilf(dt / SIM_DT - 1e-4f);
    if (n < 1) n = 1;
    if (n > GAME_MAX_SUBSTEPS) n = GAME_MAX_SUBSTEPS;
    float h = dt / (float)n;
    g->substeps = n;

    for (int i = 0; i < n; i++) {
        if (g->run == RUN_WRECKED) break;
        boat_step(&g->b, &g->boat, &g->wind, &g->in, &g->wf, h);
        HitKind k = rocks_collide(&g->world, g->rocks, g->nrocks, &g->b, &g->boat, g->wf.heave);
        if (k > g->last_hit) g->last_hit = k;
        if (k == HIT_WRECK) {
            g->run = RUN_WRECKED;
            g->b.u = g->b.v = g->b.r = 0.0f;
        }
    }
    if (g->run == RUN_SAILING &&
        fabsf(g->b.heel) > g->boat.hull.capsize_deg * (float)M_PI / 180.0f)
        g->run = RUN_CAPSIZED;
    g->t += dt;
}
