#include "app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "controls.h"
#include "praise.h"
#include "training.h"
#include "trainingtext.h"

#define WARN_SHOW_S 3.0f

typedef enum { APP_MENU, APP_PLAY } AppState;

static const Platform *P;
static Game game;
static CameraPose cam;
static BoatCfg boat_cfg;
static WeatherCfg weather_cfg;
static WorldCfg world_cfg;
static int boat_index, preset_index = WEATHER_CHOPPY;
static int sound_level = 2;   /* kept here because the menu is rebuilt on every visit */
static uint32_t restart_count;
static bool back_asked;   /* "go back" picked in the system menu; the run waits for A (home) or B (stay) */
static bool sys_racing;
static char warn[ASSETS_WARN_LEN];
static char track_warn[ASSETS_WARN_LEN];
static float warn_left;
static bool warn_shown;   /* the warning is on this frame */
static AppState app = APP_MENU;
static Menu menu;
static Track tracks[TRACKS_MAX];
static int ntracks;
static BestTimes best;
static bool finish_recorded;   /* this race's time has been offered to `best` */
static GhostSample *ghost_stores;   /* two GHOST_MAX_SAMPLES arrays, held only while a race is on (~576 KB) */
static Ghost ghost_best, ghost_run;   /* the personal best on this course, and the run being sailed */
static double boat_best[BOATLIST_MAX];   /* the boat screen's course: each boat's ghost time, -1 for none */
static int boat_best_track = -1;
static HelpLine help[APP_HELP_ROWS_MAX];
static int nhelp;
static bool boat_raceable[BOATLIST_MAX];   /* cruisers are free range only */
static Training lesson;
static TrainingText lesson_text;
static bool lesson_on;   /* the sailing lesson is the run on screen */

#ifdef DEMO
#define DEMO_BUILD true
#else
#define DEMO_BUILD false
#endif
static float demo_left;       /* sailing seconds left in this demo run */
static float demo_end_left;   /* seconds left on the thank-you box; > 0 once the demo run is over */

#define BEST_FILE "besttimes.txt"

static void best_load(void) {
    besttimes_init(&best);
    void *f = P->open(BEST_FILE, PLAT_READ_DATA);
    if (!f) return;
    char buf[1024];
    int n = P->read(f, buf, sizeof buf - 1);
    P->close(f);
    if (n > 0) {
        buf[n] = 0;
        besttimes_load(&best, buf);
    }
}

static void best_save(void) {
    char buf[1024];
    int n = besttimes_save(&best, buf, sizeof buf);
    if (n < 0) return;
    void *f = P->open(BEST_FILE, PLAT_WRITE);
    if (!f) return;
    P->write(f, buf, (unsigned int)n);
    P->close(f);
}

/* The current weather's ghost for this course and boat. */
static void ghost_path(char *out, size_t n, const char *course, const char *boat) {
    ghost_file_name(out, n, course, boat, WEATHER_NAMES[preset_index]);
}

static uint32_t ghost_boat_hash(void) { return ghost_hash_str(assets_boat_names()[boat_index]); }

/* Sample storage exists only for races; other modes give it back. Without it the ghost is simply off. */
static void ghost_storage(bool want) {
    if (want == (ghost_stores != NULL)) return;
    if (want) {
        ghost_stores = P->realloc(NULL, 2 * sizeof(GhostSample) * GHOST_MAX_SAMPLES);
        if (!ghost_stores) return;
        ghost_init(&ghost_best, ghost_stores, GHOST_MAX_SAMPLES);
        ghost_init(&ghost_run, ghost_stores + GHOST_MAX_SAMPLES, GHOST_MAX_SAMPLES);
    } else {
        ghost_init(&ghost_best, NULL, 0);
        ghost_init(&ghost_run, NULL, 0);
        P->realloc(ghost_stores, 0);
        ghost_stores = NULL;
    }
}

/* Reads a ghost file into the race ghost if it was sailed under the current conditions. */
static bool ghost_read(void *f) {
    if (!f) return false;
    bool ok = false;
    uint8_t *io = P->realloc(NULL, GHOST_FILE_MAX);
    if (io) {
        const int n = P->read(f, io, GHOST_FILE_MAX);
        ok = n > 0 && ghost_decode_for(&ghost_best, io, (size_t)n, game.seed, ghost_track_hash(&game.track),
                                       ghost_boat_hash(), game.preset);
        P->realloc(io, 0);
    }
    P->close(f);
    return ok;
}

/* The player's saved ghost; when there is none, or it was sailed in other conditions, the baseline shipped
 * with the game under the same name. */
static void ghost_load(void) {
    char path[96], bundled[112];
    ghost_clear(&ghost_best);
    if (!ghost_best.s) return;
    ghost_path(path, sizeof path, game.track.name, assets_boat_names()[boat_index]);
    if (ghost_read(P->open(path, PLAT_READ_DATA))) return;
    snprintf(bundled, sizeof bundled, "ghosts/%s", path);
    ghost_read(P->open(bundled, PLAT_READ));
}

static void ghost_save(const Ghost *gh) {
    char path[96];
    uint8_t *io = P->realloc(NULL, GHOST_FILE_MAX);
    if (!io) return;
    const size_t n = ghost_encode(gh, io, GHOST_FILE_MAX);
    if (n > 0) {
        ghost_path(path, sizeof path, game.track.name, assets_boat_names()[boat_index]);
        void *f = P->open(path, PLAT_WRITE);
        if (f) {
            P->write(f, io, (unsigned int)n);
            P->close(f);
        }
    }
    P->realloc(io, 0);
}

/* Fills boat_best for the course on the boat screen from the headers of its ghost files. */
static void boat_best_refresh(void) {
    const char *const *names = assets_boat_names();
    for (int i = 0; i < BOATLIST_MAX; i++) boat_best[i] = -1.0;
    for (int i = 0; i < menu.nboats && i < BOATLIST_MAX; i++) {
        char path[96];
        uint8_t hdr[GHOST_HEADER_BYTES];
        ghost_path(path, sizeof path, tracks[menu.track].name, names[i]);
        char bundled[112];
        snprintf(bundled, sizeof bundled, "ghosts/%s", path);
        for (int pass = 0; pass < 2 && boat_best[i] < 0.0; pass++) {
            void *f = pass == 0 ? P->open(path, PLAT_READ_DATA) : P->open(bundled, PLAT_READ);
            if (!f) continue;
            const int n = P->read(f, hdr, sizeof hdr);
            P->close(f);
            double t;
            if (n == (int)sizeof hdr && ghost_peek_time(hdr, sizeof hdr, &t)) boat_best[i] = t;
        }
    }
}

Ghost *app_ghost_opponent(void) {
    if (game.mode != GAME_RACE) return NULL;
    return ghost_matches(&ghost_best, game.seed, ghost_track_hash(&game.track), ghost_boat_hash(), game.preset) ? &ghost_best : NULL;
}

/* A race attempt starts (or restarts): the course's own seed, so wind and sea repeat, and a fresh recording. */
static void race_attempt(void) {
    game_restart(&game, ghost_seed(game.track.name));
    ghost_begin(&ghost_run, &game, ghost_track_hash(&game.track), ghost_boat_hash());
}

static uint32_t fresh_seed(void) { return P->seed(++restart_count); }

static void camera_snap(void) {
    memset(&cam, 0, sizeof cam);
    cam.trail = true;
    camera_update(&cam, &game, &game.boat, 1.0f / 30);
}

static void begin(bool reload) {
    if (reload) {
        assets_load(P, boat_index, &boat_cfg, &weather_cfg, &world_cfg, warn, sizeof warn);
        warn_left = warn[0] ? WARN_SHOW_S : 0.0f;
        if (warn[0]) P->log("config: %s", warn);
    }
    const bool racing = game.mode == GAME_RACE;
    const bool journeying = game.mode == GAME_JOURNEY;
    const Track trk = game.track;   /* game_init clears the mode; a boat change keeps the race */
    game_init(&game, &boat_cfg, &weather_cfg, &world_cfg, preset_index, fresh_seed());
    if (racing) game_set_track(&game, &trk);
    if (journeying) game_set_journey(&game);
    camera_snap();
    if (P->reset_time) P->reset_time();
}

/* Wind that holds its speed and direction, so every point of sail stays where the player put it. */
static void lesson_weather(void) {
    game.weather = weather_cfg;
    WeatherPreset *p = &game.weather.preset[WEATHER_SUNNY];
    p->wind_var = 0.0f;
    p->shift_rate_deg = 0.0f;
    p->gust_speed = 0.0f;
    game_set_preset(&game, WEATHER_SUNNY);
}

/* A fresh lesson run, or the same step again after a capsize: the boat starts straight downwind. */
static void lesson_place(void) {
    game.b.heading = game.wind.dir;
    lesson.held = 0.0f;
    lesson.seen = 0;
    camera_snap();
}

static void show_menu(void) {
    const bool from_lesson = lesson_on;
    if (lesson_on) {
        lesson_on = false;
        game.weather = weather_cfg;
        game_set_preset(&game, preset_index);
    }
    app = APP_MENU;
    menu_init(&menu, ntracks, assets_boat_count());
    if (from_lesson) {
        menu.screen = MENU_TRAINING;
        menu.sel = TRAIN_ROW_LESSON;
    }
    menu_set_boat_racing(&menu, boat_raceable);
    menu.boat = boat_index;   /* the boat screen opens on the current boat */
    menu_set_sound(&menu, sound_level);
    P->audio_update(NULL, 0);
    P->audio_set_active(true);
    menu_set_weather(&menu, WEATHER_PRESETS, preset_index);
    menu_set_help(&menu, nhelp);
    if (DEMO_BUILD) menu_set_main_locked(&menu, 1u << MENU_ITEM_JOURNEY | 1u << MENU_ITEM_RACE);
}

/* config/help.md is laid out once; a missing file leaves a one-line notice */
static void help_load(void) {
    char *md;
    nhelp = 0;
    if (assets_read_file(P, "config/help.md", &md) > 0) nhelp = helptext_layout(md, APP_HELP_COLS, help, APP_HELP_ROWS_MAX);
    if (nhelp == 0) nhelp = helptext_layout("config/help.md is missing", APP_HELP_COLS, help, APP_HELP_ROWS_MAX);
}

static void sys_menu_build(bool racing) {
    sys_racing = racing;
    P->sys_menu(racing, boat_index, preset_index);
}

void app_sys_boat(int boat) {
    boat_index = boat;
    begin(true);
}
void app_sys_weather(int preset) {
    preset_index = preset;
    game_set_preset(&game, preset_index);
}
/* The system menu runs outside the frame, so it only raises the question; app_step acts on the answer. */
void app_sys_back(void) { if (app == APP_PLAY) back_asked = true; }

void app_force_praise(int praise) {
    game.praise = praise;
    game.praise_left = PRAISE_SECONDS;
}

/* Passed buoys leave the course once the oldest is out of sight: behind the camera, off either side,
   or lost in the mist. They are neither drawn nor kept after that. */
static void journey_cull(Game *g, const CameraPose *c) {
    const float margin = 40.0f;   /* px, so a buoy is not dropped the instant its edge leaves the screen */
    while (g->journey.next > 0 && g->journey.n > 0) {
        const JourneyBuoy *b = &g->journey.buoy[0];
        float sx, sy, d;
        const float z = sea_height(&g->sea, (float)b->x, (float)b->y, (float)fmod(g->t, g->sea.period));
        const bool seen = camera_project(c, b->x, b->y, z, &sx, &sy, &d) && d <= g->weather.preset[g->preset].visibility &&
                          sx >= -margin && sx <= CAM_W + margin;
        if (seen || !journey_drop_oldest(&g->journey)) break;
    }
}

AppDraw app_step(uint32_t pushed, uint32_t cur, float crank, float dt) {
    warn_shown = false;
    if (app == APP_MENU) {
        MenuAction act = menu_update(&menu, pushed);
        if (menu.sound != sound_level) {
            sound_level = menu.sound;
            P->audio_set_level(sound_level);
        }
        menu_crank(&menu, crank);
        if (act != MENU_NONE) ghost_storage(act == MENU_START_RACE);
        if (act == MENU_START_TRAINING) {
            game_set_track(&game, NULL);
            lesson_weather();
            training_init(&lesson);
            lesson_on = true;
            lesson_place();
            sys_menu_build(true);
            demo_left = APP_DEMO_SECONDS;
            demo_end_left = 0.0f;
            app = APP_PLAY;
        } else if (act == MENU_START_FREE) {
            game_set_track(&game, NULL);
            sys_menu_build(false);
            camera_snap();
            demo_left = APP_DEMO_SECONDS;
            demo_end_left = 0.0f;
            app = APP_PLAY;
        } else if (act == MENU_START_RACE) {
            const bool new_boat = menu.boat != boat_index;
            boat_index = menu.boat;
            preset_index = menu.weather;
            if (new_boat) begin(true);
            else game_set_preset(&game, preset_index);
            game_set_track(&game, &tracks[menu.track]);
            ghost_load();
            race_attempt();
            sys_menu_build(true);
            camera_snap();
            app = APP_PLAY;
        } else if (act == MENU_START_JOURNEY) {
            const bool new_boat = menu.boat != boat_index;
            boat_index = menu.boat;
            preset_index = menu.weather;
            if (new_boat) begin(true);
            else game_set_preset(&game, preset_index);
            game_set_journey(&game);
            sys_menu_build(true);
            camera_snap();
            app = APP_PLAY;
        }
        if (menu.screen == MENU_BOATS && !menu.journey) {
            if (boat_best_track != menu.track) {
                boat_best_refresh();
                boat_best_track = menu.track;
            }
        } else {
            boat_best_track = -1;
        }
        return APP_DRAW_MENU;
    }

    if (DEMO_BUILD && demo_end_left > 0.0f) {
        /* the demo run is over: the thank-you box stays up, then the home screen */
        demo_end_left -= dt;
        if ((pushed & BTN_A) || demo_end_left <= 0.0f) {
            demo_end_left = 0.0f;
            back_asked = false;
            show_menu();
            return APP_DRAW_NONE;
        }
        return APP_DRAW_DEMO_END;
    }

    if (back_asked) {
        /* the run stays frozen until the player answers: A goes to the home screen, B carries on sailing */
        if (pushed & BTN_A) {
            back_asked = false;
            show_menu();
            return APP_DRAW_NONE;
        }
        if (!(pushed & BTN_B)) return APP_DRAW_CONFIRM;
        back_asked = false;
        pushed = 0;   /* the B that answered "no" is not also an autotrim press */
    }

    if (lesson_on && lesson.step == TRAIN_DONE && game.run == RUN_SAILING && (pushed & BTN_A)) {
        show_menu();   /* the lesson is over; A goes back to the Training screen */
        return APP_DRAW_NONE;
    }

    if (game.mode == GAME_RACE && game.race.finished && game.run == RUN_SAILING && (pushed & BTN_A)) {
        show_menu();   /* the finish panel shows the time; A goes back to the home screen */
        return APP_DRAW_NONE;
    }

    /* A restarts once the run is over; while sailing A toggles the autopilot, a quick press of B
       the autotrim, and holding B starts the engine. */
    if (game.run != RUN_SAILING && (pushed & BTN_A)) {
        if (game.mode == GAME_RACE) race_attempt();
        else game_restart(&game, fresh_seed());
        if (lesson_on) lesson_place();   /* the same step again */
        pushed = 0;
        dt = 1.0f / 30;
    }
    if (lesson_on && training_engine_locked(&lesson)) game.en.on = false;   /* the lesson has not got to the engine yet */
    game_update(&game, pushed, cur, crank, dt);
    if (lesson_on) {
        if (training_engine_locked(&lesson)) game.en.on = false;
        training_update(&lesson, &game, dt);
    }
    if (game.mode == GAME_RACE) ghost_record(&ghost_run, &game);
    if (game.mode == GAME_RACE && game.race.finished) {
        if (!finish_recorded) {
            finish_recorded = true;
            if (besttimes_record(&best, game.track.name, game.race.t)) best_save();
            /* the ghost is the best for this boat on this course, whether or not another boat was faster */
            if (ghost_run.s && (!ghost_usable(&ghost_best) || game.race.t < ghost_best.time) && ghost_finish(&ghost_run, &game)) {
                ghost_save(&ghost_run);
                const Ghost keep = ghost_run;   /* the new best is the one on show next time; the old storage records */
                ghost_run = ghost_best;
                ghost_best = keep;
            }
        }
    } else {
        finish_recorded = false;
    }
    camera_update(&cam, &game, &game.boat, dt);
    if (game.mode == GAME_JOURNEY) journey_cull(&game, &cam);
    P->audio_update(&game, dt);
    if (DEMO_BUILD) {
        demo_left -= dt < 0.1f ? dt : 0.1f;   /* a long pause in the system menu is not sailing time */
        if (demo_left <= 0.0f) {
            demo_left = 0.0f;
            demo_end_left = APP_DEMO_END_SECONDS;
            return APP_DRAW_DEMO_END;
        }
    }
    if (warn_left > 0.0f) {
        warn_left -= dt;
        warn_shown = true;
    }
    return APP_DRAW_PLAY;
}

const char *app_draw_name(AppDraw d) { return d == APP_DRAW_PLAY || d == APP_DRAW_CONFIRM || d == APP_DRAW_DEMO_END ? "play" : "menu"; }

void app_init(const Platform *p) {
    P = p;
    ntracks = tracks_load_all(P, tracks, TRACKS_MAX, track_warn, sizeof track_warn);
    best_load();
    assets_scan_boats(P);
    assets_boat_raceable(P, boat_raceable);
    sys_menu_build(false);
    P->audio_set_level(sound_level);
    help_load();
    {
        char *md;
        trainingtext_parse(assets_read_file(P, "config/training.md", &md) > 0 ? md : NULL, &lesson_text);
    }
    begin(true);
    if (track_warn[0]) {
        /* append track warnings to the config line, "; "-separated */
        size_t n = strlen(warn), len = strlen(track_warn);
        if (n && n + 2 < sizeof warn) {
            warn[n++] = ';';
            warn[n++] = ' ';
            warn[n] = 0;
        }
        if (len > sizeof warn - n - 1) len = sizeof warn - n - 1;
        memcpy(warn + n, track_warn, len);
        warn[n + len] = 0;
        warn_left = WARN_SHOW_S;
        P->log("tracks: %s", track_warn);
    }
    show_menu();
}

bool app_is_demo(void) { return DEMO_BUILD; }
float app_demo_left(void) { return DEMO_BUILD && app == APP_PLAY ? demo_left : 0.0f; }
bool app_playing(void) { return app == APP_PLAY; }
const Game *app_game(void) { return &game; }
Game *app_game_mut(void) { return &game; }
const CameraPose *app_camera(void) { return &cam; }
const Menu *app_menu(void) { return &menu; }
const Track *app_tracks(void) { return tracks; }
int app_ntracks(void) { return ntracks; }
const BestTimes *app_best(void) { return &best; }
const double *app_boat_best(void) { return boat_best; }
const HelpLine *app_help(void) { return help; }
int app_nhelp(void) { return nhelp; }
int app_boat_index(void) { return boat_index; }
int app_preset_index(void) { return preset_index; }
bool app_sys_racing(void) { return sys_racing; }
const char *app_warning(void) { return warn_shown ? warn : NULL; }
const Training *app_training(void) { return lesson_on ? &lesson : NULL; }
const char *app_training_text(void) { return lesson_on && app == APP_PLAY ? trainingtext_get(&lesson_text, lesson.step) : NULL; }
