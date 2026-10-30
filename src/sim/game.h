#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "boat.h"
#include "autopilot.h"
#include "autotrim.h"
#include "boatcfg.h"
#include "controls.h"
#include "race.h"
#include "rocks.h"
#include "sea.h"
#include "weather.h"

#define GAME_MAX_SUBSTEPS 12
#define GAME_DT_MIN 0.005f
#define GAME_DT_MAX 0.1f
#define GAME_ROCK_RADIUS 150.0f   /* m; rocks refreshed around the boat each frame */

typedef enum { RUN_SAILING, RUN_CAPSIZED, RUN_WRECKED } RunState;

typedef enum { GAME_FREE, GAME_RACE } GameMode;

typedef struct {
    BoatCfg boat;           /* copies, so a restart never depends on caller storage */
    WeatherCfg weather;
    WorldCfg world;
    int preset;
    uint32_t seed;
    GameMode mode;
    Track track;            /* copy; used when mode == GAME_RACE */

    BoatState b;
    BoatInputs in;
    Controls ctl;
    Autopilot ap;
    Autotrim at;
    Wind wind;
    Sea sea;
    WaveForces wf;          /* sampled once per frame, held across substeps */
    double t;
    RunState run;
    HitKind last_hit;       /* worst contact seen this frame */
    int substeps;           /* substeps run by the last update */
    Race race;

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

/* Out-of-range presets are clamped; the wind and sea are re-seeded from the current seed. */
void game_set_preset(Game *g, int preset);

#endif
