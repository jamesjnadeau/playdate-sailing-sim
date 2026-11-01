#include <stdio.h>
#include <string.h>
#include "pd_api.h"
#include "assets.h"
#include "audio.h"
#include "camera.h"
#include "game.h"
#include "helptext.h"
#include "menu.h"
#include "render.h"
#include "tracks.h"
#ifdef SCENARIO
#include "scenario.h"
#endif

#define WARN_SHOW_S 3.0f
#define HELP_ROWS_MAX 96
#define HELP_COLS 44

typedef enum { APP_MENU, APP_PLAY } AppState;

static PlaydateAPI *pd;
static Game game;
static CameraPose cam;
static BoatCfg boat_cfg;
static WeatherCfg weather_cfg;
static WorldCfg world_cfg;
static int boat_index, preset_index = WEATHER_CHOPPY;
static int sound_level = 2;   /* kept here because the menu is rebuilt on every visit */
static uint32_t restart_count;
static char warn[ASSETS_WARN_LEN];
static char track_warn[ASSETS_WARN_LEN];
static float warn_left;
static AppState app = APP_MENU;
static Menu menu;
static Track tracks[TRACKS_MAX];
static int ntracks;
static HelpLine help[HELP_ROWS_MAX];
static int nhelp;

static PDMenuItem *boat_item, *weather_item;
static bool boat_raceable[BOATLIST_MAX];   /* cruisers are free range only */

static uint32_t fresh_seed(void) {
#ifdef SCENARIO
    return scenario_seed(++restart_count);
#else
    return pd->system->getCurrentTimeMilliseconds() * 2654435761u + (++restart_count) * 40503u;
#endif
}

static void camera_snap(void) {
    memset(&cam, 0, sizeof cam);
    cam.trail = true;
    camera_update(&cam, &game, &game.boat, 1.0f / 30);
}

static void begin(bool reload) {
    if (reload) {
        assets_load(pd, boat_index, &boat_cfg, &weather_cfg, &world_cfg, warn, sizeof warn);
        warn_left = warn[0] ? WARN_SHOW_S : 0.0f;
        if (warn[0]) pd->system->logToConsole("config: %s", warn);
    }
    const bool racing = game.mode == GAME_RACE;
    const bool journeying = game.mode == GAME_JOURNEY;
    const Track trk = game.track;   /* game_init clears the mode; a boat change keeps the race */
    game_init(&game, &boat_cfg, &weather_cfg, &world_cfg, preset_index, fresh_seed());
    if (racing) game_set_track(&game, &trk);
    if (journeying) game_set_journey(&game);
    camera_snap();
    pd->system->resetElapsedTime();
}

static void show_menu(void) {
    app = APP_MENU;
    menu_init(&menu, ntracks, assets_boat_count());
    menu_set_boat_racing(&menu, boat_raceable);
    menu.boat = boat_index;   /* the boat screen opens on the current boat */
    menu_set_sound(&menu, sound_level);
    audio_update(NULL, 0);
    audio_set_active(true);
    menu_set_weather(&menu, WEATHER_PRESETS, preset_index);
    menu_set_help(&menu, nhelp);
}

/* config/help.md is laid out once; a missing file leaves a one-line notice */
static void help_load(void) {
    char *md;
    nhelp = 0;
    if (assets_read_file(pd, "config/help.md", &md) > 0) nhelp = helptext_layout(md, HELP_COLS, help, HELP_ROWS_MAX);
    if (nhelp == 0) nhelp = helptext_layout("config/help.md is missing", HELP_COLS, help, HELP_ROWS_MAX);
}

static void menu_boat(void *ud) {
    (void)ud;
    boat_index = pd->system->getMenuItemValue(boat_item);
    begin(true);
}
static void menu_weather(void *ud) {
    (void)ud;
    preset_index = pd->system->getMenuItemValue(weather_item);
    game_set_preset(&game, preset_index);
}
static void menu_restart(void *ud) { (void)ud; game_restart(&game, fresh_seed()); }
static void menu_main(void *ud) { (void)ud; show_menu(); }

/* A race is set up from the main menu, so the system menu only offers boat and weather in free range. */
static void sys_menu_build(bool racing) {
    pd->system->removeAllMenuItems();
    boat_item = weather_item = NULL;
    if (!racing) {
        boat_item = pd->system->addOptionsMenuItem("boat", (const char **)assets_boat_names(), assets_boat_count(), menu_boat, NULL);
        weather_item = pd->system->addOptionsMenuItem("weather", WEATHER_NAMES, WEATHER_PRESETS, menu_weather, NULL);
        pd->system->setMenuItemValue(boat_item, boat_index);
        pd->system->setMenuItemValue(weather_item, preset_index);
    }
    pd->system->addMenuItem("restart", menu_restart, NULL);
    pd->system->addMenuItem("main menu", menu_main, NULL);
}

#ifdef SCENARIO
/* A scripted system-menu action, applied as the simulator would on selection. */
static void scenario_sys(const ScFrame *f) {
    switch (f->sys) {
    case SC_SYS_RESTART: menu_restart(NULL); break;
    case SC_SYS_MAIN: menu_main(NULL); break;
    case SC_SYS_BOAT:
    case SC_SYS_WEATHER: {
        PDMenuItem *item = f->sys == SC_SYS_BOAT ? boat_item : weather_item;
        if (!item) {
            pd->system->logToConsole("scenario: sysmenu item not available (racing)");
            break;
        }
        int idx = f->sys_arg;
        if (f->sys == SC_SYS_BOAT && idx < 0) {   /* by name */
            const char *const *names = assets_boat_names();
            for (int i = 0; i < assets_boat_count(); i++)
                if (!strcmp(names[i], f->sys_name)) { idx = i; break; }
            if (idx < 0) {
                pd->system->logToConsole("scenario: no boat named %s", f->sys_name);
                break;
            }
        }
        pd->system->setMenuItemValue(item, idx);
        if (f->sys == SC_SYS_BOAT) menu_boat(NULL);
        else menu_weather(NULL);
        break;
    }
    case SC_SYS_NONE: break;
    }
}
#endif

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

static int update(void *ud) {
    (void)ud;
    PDButtons cur, pushed, released;
#ifdef SCENARIO
    float dt, crank;
    ScFrame sf;
    scenario_poll_input(&cur, &pushed, &released, &crank, &dt, &sf);
    scenario_sys(&sf);
#else
    pd->system->getButtonState(&cur, &pushed, &released);
    float dt = pd->system->getElapsedTime();
    pd->system->resetElapsedTime();
    float crank = pd->system->getCrankChange();
#endif

    if (app == APP_MENU) {
        MenuAction act = menu_update(&menu, (uint32_t)pushed);
        if (menu.sound != sound_level) {
            sound_level = menu.sound;
            audio_set_level(sound_level);
        }
        menu_crank(&menu, crank);
        if (act == MENU_START_FREE) {
            game_set_track(&game, NULL);
            sys_menu_build(false);
            camera_snap();
            app = APP_PLAY;
        } else if (act == MENU_START_RACE) {
            const bool new_boat = menu.boat != boat_index;
            boat_index = menu.boat;
            preset_index = menu.weather;
            if (new_boat) begin(true);
            else game_set_preset(&game, preset_index);
            game_set_track(&game, &tracks[menu.track]);
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
        menu_draw(pd, &menu, tracks, help);
#ifdef SHOW_FPS
        pd->system->drawFPS(0, 0);
#endif
#ifdef SCENARIO
        scenario_frame_done(pd, &game, "menu");
#endif
        return 1;
    }

    if (game.mode == GAME_RACE && game.race.finished && game.run == RUN_SAILING && (pushed & kButtonA)) {
        show_menu();   /* the finish panel shows the time; A goes back to the home screen */
#ifdef SCENARIO
        scenario_frame_done(pd, &game, "menu");
#endif
        return 1;
    }

    /* A restarts once the run is over; while sailing A toggles the autopilot, a quick press of B
       the autotrim, and holding B starts the engine. */
    if (game.run != RUN_SAILING && (pushed & kButtonA)) {
        game_restart(&game, fresh_seed());
        pushed = 0;
        dt = 1.0f / 30;
    }
    game_update(&game, (uint32_t)pushed, (uint32_t)cur, crank, dt);
    camera_update(&cam, &game, &game.boat, dt);
    if (game.mode == GAME_JOURNEY) journey_cull(&game, &cam);
    audio_update(&game, dt);

    render_frame(pd, &game, &cam);
    if (warn_left > 0.0f) {
        warn_left -= dt;
        /* below the HUD bar, on white, clipped to one screen-width line */
        size_t len = strlen(warn) < 48 ? strlen(warn) : 48;
        pd->graphics->fillRect(0, 36, CAM_W, 18, kColorWhite);
        pd->graphics->setDrawMode(kDrawModeCopy);
        pd->graphics->drawText(warn, len, kASCIIEncoding, 4, 39);
    }
#ifdef SHOW_FPS
    pd->system->drawFPS(0, 0);
#endif
#ifdef SCENARIO
    scenario_frame_done(pd, &game, "play");
#endif
    return 1;
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg) {
    (void)arg;
    if (event == kEventResume) audio_set_active(app == APP_PLAY);
    if (event == kEventPause) {
        audio_set_active(false);
        /* the menu shows this bitmap beside its items */
        LCDBitmap *img = pd->graphics->newBitmap(CAM_W, 240, kColorWhite);
        pd->graphics->pushContext(img);
        hud_pause_panel(pd, &game);
        pd->graphics->popContext();
        pd->system->setMenuImage(img, 0);   /* the system takes a copy */
        pd->graphics->freeBitmap(img);
        return 0;
    }
    if (event == kEventInit) {
        pd = playdate;
        ntracks = tracks_load_all(pd, tracks, TRACKS_MAX, track_warn, sizeof track_warn);
        assets_scan_boats(pd);
        assets_boat_raceable(pd, boat_raceable);
        sys_menu_build(false);
        pd->display->setRefreshRate(30.0f);
        audio_init(pd);
        audio_set_level(sound_level);
        help_load();
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
            pd->system->logToConsole("tracks: %s", track_warn);
        }
        show_menu();
#ifdef SCENARIO
        scenario_init(pd);
#endif
        pd->system->setUpdateCallback(update, NULL);
    }
    return 0;
}
