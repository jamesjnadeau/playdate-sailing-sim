#include "game.h"
#include <math.h>
#include "praise.h"
#include <string.h>

static int clamp_preset(int p) {
    return p < 0 ? 0 : p >= WEATHER_PRESETS ? WEATHER_PRESETS - 1 : p;
}

/* mask: the ticks a journey picks its start weather from (and shifts between); unused in other modes. */
static void start_weather(Game *g, uint32_t mask) {
    if (g->mode == GAME_JOURNEY)
        g->preset = wshift_init(&g->shift, mask, g->preset, g->seed,
                                g->world.journey_shift_min_s, g->world.journey_shift_max_s);
    g->wx = g->weather.preset[g->preset];
    const WeatherPreset *p = &g->wx;
    wind_init(&g->wind, p, g->seed);
    if (g->mode == GAME_RACE)
        wind_set_base(&g->wind, wrap_pi(g->track.wind_from + (float)M_PI));
    if (g->mode == GAME_RACE && g->track.wind_speed > 0.0f)
        wind_set_speed(&g->wind, p, g->track.wind_speed);
    sea_init(&g->sea, p, g->seed);
    memset(&g->wf, 0, sizeof g->wf);
}

static void reset(Game *g) {
    boat_init(&g->b, &g->boat);
    boat_inputs_init(&g->in, &g->boat);
    controls_init(&g->ctl);
    autopilot_init(&g->ap);
    autotrim_init(&g->at);
    engine_init(&g->en);
    if (g->mode == GAME_RACE) {
        g->b.x = g->track.start_x;
        g->b.y = g->track.start_y;
        g->b.heading = g->track.start_heading;
    }
    race_init(&g->race);
    follower_clear(&g->follow);
    start_weather(g, g->journey_weathers);
    if (g->mode == GAME_JOURNEY)
        journey_init(&g->journey, g->world.journey_min_m, g->world.journey_max_m, g->seed,
                     g->b.x, g->b.y, g->b.heading, wrap_pi(g->wind.dir + (float)M_PI),
                     g->boat.controls.autopilot_no_go_deg * (float)M_PI / 180.0f);
    g->praise_left = 0.0f;
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
    g->praise_rng = seed;
    reset(g);
}

void game_set_track(Game *g, const Track *t) {
    if (t && t->nmarks >= 1) {
        g->track = *t;
        g->mode = GAME_RACE;
    } else {
        g->mode = GAME_FREE;
    }
    reset(g);
}

void game_set_journey(Game *g) {
    g->mode = GAME_JOURNEY;
    reset(g);
}

void game_restart(Game *g, uint32_t new_seed) {
    g->seed = new_seed;
    reset(g);
}

void game_set_preset(Game *g, int preset) {
    g->preset = clamp_preset(preset);
    start_weather(g, 1u << g->preset);
}

void game_set_journey_weathers(Game *g, uint32_t mask) {
    g->journey_weathers = mask;
}

const WeatherPreset *game_weather(const Game *g) {
    return &g->wx;
}

int game_weather_turning(const Game *g) {
    return g->mode == GAME_JOURNEY && g->shift.from != g->shift.to ? g->shift.to : -1;
}

/* Journey only: advances the shift scheduler and applies the blend to the weather, wind and sea. */
static void shift_weather(Game *g, float dt) {
    WeatherShift *s = &g->shift;
    const WeatherPreset *ps = g->weather.preset;
    WShiftEvent ev = wshift_step(s, dt, g->world.journey_shift_min_s, g->world.journey_shift_max_s,
                                 g->world.weather_blend_s);
    if (ev == WSHIFT_START) {
        sea_init(&g->sea_from, &ps[s->from], g->seed);
        sea_init(&g->sea_to, &ps[s->to], g->seed);
        sea_anchor(&g->sea, g->b.x, g->b.y, g->t);
        g->preset = s->to;
    }
    if (s->from != s->to) {
        float w = wshift_weight(s);
        weather_blend(&g->wx, &ps[s->from], &ps[s->to], w);
        sea_morph(&g->sea, &g->sea_from, &g->sea_to, w);
        sea_move_anchor(&g->sea, g->b.x, g->b.y);
        g->wind.base = g->wx.wind_base;
    } else if (ev == WSHIFT_END) {
        sea_unanchor(&g->sea, &g->sea_to, g->t);
        g->wx = ps[s->to];
        g->wind.base = g->wx.wind_base;
    }
}

/* Floating origin: keep the boat near (0,0) so float maths on world positions (sea phase, fleck cells,
   wave bands) never loses precision. The shift is a multiple of SEA_WRAP, so the sea is unchanged. */
static void recentre(Game *g) {
    const double step = 5.0 * SEA_WRAP;   /* also a multiple of the 5 m fleck cell */
    double dx = 0, dy = 0;
    if (fabs(g->b.x) > step) dx = step * floor(g->b.x / step + 0.5);
    if (fabs(g->b.y) > step) dy = step * floor(g->b.y / step + 0.5);
    g->b.x -= dx;
    g->b.y -= dy;
    if (g->mode == GAME_RACE) race_shift(&g->race, dx, dy);   /* marks move with the boat */
    if (g->mode == GAME_JOURNEY) journey_shift(&g->journey, dx, dy);
    follower_shift(&g->follow, dx, dy);
    if (g->sea.anchored) sea_shift_anchor(&g->sea, dx, dy);
}

/* Tells the buoy-follow camera where the next buoy lies, relative to the bow; NAN when there is none. */
static void aim_camera(Game *g) {
    double mx, my;
    bool have = false;
    int id = g->ctl.buoy_id;
    if (g->mode == GAME_RACE && !g->race.finished && g->race.next < g->track.nmarks) {
        race_mark_local(&g->race, &g->track, g->race.next, &mx, &my);
        id = g->race.next;
        have = true;
    } else if (g->mode == GAME_JOURNEY) {
        const JourneyBuoy *nb = journey_next(&g->journey);
        if (nb) {
            mx = nb->x;
            my = nb->y;
            id = g->journey.passed;
            have = true;
        }
    }
    g->ctl.buoy_rel = have ? wrap_pi((float)atan2(my - g->b.y, mx - g->b.x) - g->b.heading) : NAN;
    g->ctl.buoy_id = id;
}

void game_update(Game *g, uint32_t pressed, uint32_t held, float crank_delta, float frame_dt) {
    float dt = isfinite(frame_dt) ? frame_dt : 1.0f / 30.0f;
    dt = clampf(dt, GAME_DT_MIN, GAME_DT_MAX);
    g->last_hit = HIT_NONE;
    if (g->mode == GAME_JOURNEY) shift_weather(g, dt);

    if (g->run == RUN_SAILING) {   /* capsized or wrecked: controls are frozen */
        aim_camera(g);
        controls_update(&g->ctl, &g->boat, pressed, held, crank_delta, dt, &g->in);
        autopilot_buttons(&g->ap, pressed, g->b.heading);
        if (g->ap.on && g->ctl.mode == MODE_WHEEL)
            autopilot_crank(&g->ap, &g->boat.controls, crank_delta);
        if (g->ap.on)
            g->in.wheel_target = autopilot_wheel(&g->ap, &g->boat.controls, g->b.heading, g->b.r,
                                                 wrap_pi(g->wind.dir + (float)M_PI));
        /* B: a hold starts the engine, a quick press stops it if it runs and otherwise toggles the autotrim */
        if (engine_buttons(&g->en, &g->boat.controls, pressed, held, dt)) {
            if (g->en.on) g->en.on = false;
            else autotrim_toggle(&g->at);
        }
        autotrim_update(&g->at, &g->boat, &g->b, &g->in, dt);
    }
    g->in.engine = g->en.on && g->run == RUN_SAILING;

    wind_step(&g->wind, &g->wx, dt);
    g->wf = sea_hull_forces(&g->sea, &g->b, &g->boat, (float)fmod(g->t, g->sea.period));
    g->b.pitch = g->wf.pitch;
    g->b.heave = g->wf.heave;
    /* Rocks are disabled for now (generation, collision and drawing). */
    g->nrocks = 0;
    /* g->nrocks = rocks_near(&g->world, g->seed, (float)g->b.x, (float)g->b.y,
                              GAME_ROCK_RADIUS, g->rocks, ROCKS_MAX); */

    int n = (int)ceilf(dt / SIM_DT - 1e-4f);
    if (n < 1) n = 1;
    if (n > GAME_MAX_SUBSTEPS) n = GAME_MAX_SUBSTEPS;
    float h = dt / (float)n;
    g->substeps = n;

    const double x0 = g->b.x, y0 = g->b.y;
    for (int i = 0; i < n; i++) {
        if (g->run == RUN_WRECKED) break;
        boat_step(&g->b, &g->boat, &g->wind, &g->in, &g->wf, h);
        /* HitKind k = rocks_collide(&g->world, g->rocks, g->nrocks, &g->b, &g->boat, g->wf.heave);
        if (k > g->last_hit) g->last_hit = k;
        if (k == HIT_WRECK) {
            g->run = RUN_WRECKED;
            g->b.u = g->b.v = g->b.r = 0.0f;
        } */
    }
    bool rounded = false;
    if (g->mode == GAME_RACE && g->run == RUN_SAILING)
        rounded = race_update(&g->race, &g->track, x0, y0, g->b.x, g->b.y, dt);
    if (g->mode == GAME_JOURNEY && g->run == RUN_SAILING)
        rounded = journey_update(&g->journey, x0, y0, g->b.x, g->b.y, dt, wrap_pi(g->wind.dir + (float)M_PI));
    if (rounded) {
        g->praise = praise_pick(&g->praise_rng, g->praise);
        g->praise_left = PRAISE_SECONDS;
    }
    if (g->praise_left > 0.0f) g->praise_left -= dt;
    if (g->run == RUN_SAILING &&
        fabsf(g->b.heel) > g->boat.hull.capsize_deg * (float)M_PI / 180.0f)
        g->run = RUN_CAPSIZED;
    g->t += (double)dt;
    if (g->sea.anchored) sea_advance(&g->sea, dt);   /* the anchored phase always means "at time g->t" */
    recentre(g);
    if (g->mode != GAME_RACE) follower_record(&g->follow, &g->b);
}
