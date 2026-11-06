#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "boat.h"
#include "autopilot.h"
#include "autotrim.h"
#include "boatcfg.h"
#include "engine.h"
#include "follower.h"
#include "controls.h"
#include "journey.h"
#include "race.h"
#include "rocks.h"
#include "sea.h"
#include "weather.h"
#include "weathershift.h"

#define GAME_MAX_SUBSTEPS 12
#define GAME_DT_MIN 0.005f
#define GAME_DT_MAX 0.1f
#define GAME_ROCK_RADIUS 150.0f   /* m; rocks refreshed around the boat each frame */

typedef enum { RUN_SAILING, RUN_CAPSIZED, RUN_WRECKED } RunState;

typedef enum { GAME_FREE, GAME_RACE, GAME_JOURNEY } GameMode;

typedef struct {
    BoatCfg boat;           /* copies, so a restart never depends on caller storage */
    WeatherCfg weather;
    WorldCfg world;
    int preset;
    uint32_t seed;
    uint32_t journey_weathers;  /* ticks for the next journey start; 0 = just `preset` */
    GameMode mode;
    Track track;            /* copy; used when mode == GAME_RACE */

    BoatState b;
    BoatInputs in;
    Controls ctl;
    Autopilot ap;
    Autotrim at;
    Engine en;
    Wind wind;
    Sea sea;
    WaveForces wf;          /* sampled once per frame, held across substeps */
    double t;
    RunState run;
    HitKind last_hit;       /* worst contact seen this frame */
    int substeps;           /* substeps run by the last update */
    Race race;
    Journey journey;        /* used when mode == GAME_JOURNEY */
    WeatherShift shift;     /* used when mode == GAME_JOURNEY */
    WeatherPreset wx;       /* the weather in force: preset[preset], or the blend during a shift */
    Sea sea_from, sea_to;   /* the blend's endpoint seas */
    Follower follow;        /* player's recent track, for the trailing ghost (free range and journey) */
    uint32_t praise_rng;    /* picks each praise message */
    int praise;             /* index of the message on screen, see praise.h */
    float praise_left;      /* s the message stays on screen; 0 when none */

    Rock rocks[ROCKS_MAX];
    int nrocks;
} Game;

void game_init(Game *g, const BoatCfg *boat, const WeatherCfg *weather, const WorldCfg *world,
               int preset, uint32_t seed);

/* pressed: BTN_* mask of buttons newly pressed this frame; held: buttons down now. frame_dt is clamped to
   [GAME_DT_MIN, GAME_DT_MAX] (non-finite counts as 1/30) and run in at most GAME_MAX_SUBSTEPS. */
void game_update(Game *g, uint32_t pressed, uint32_t held, float crank_delta, float frame_dt);

/* One call back to the start state with a new seed (rocks, wind and sea follow the seed). */
void game_restart(Game *g, uint32_t new_seed);

/* NULL, or a track with no marks, gives the free range. Restarts the run. */
void game_set_track(Game *g, const Track *t);

/* Endless buoys from the world's journey_min_m..journey_max_m. Restarts the run. */
void game_set_journey(Game *g);

/* Journey weather ticks (bit per preset); stored, applied at the next journey (re)start. */
void game_set_journey_weathers(Game *g, uint32_t mask);

/* The weather in force (&g->wx); every reader uses this. */
const WeatherPreset *game_weather(const Game *g);

/* The preset a journey shift is blending toward, else -1. */
int game_weather_turning(const Game *g);

/* Out-of-range presets are clamped; the wind and sea are re-seeded from the current seed.
   In a journey the weather settles on this preset (no shifts) until the next journey (re)start. */
void game_set_preset(Game *g, int preset);

#endif
