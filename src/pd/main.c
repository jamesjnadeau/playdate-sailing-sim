#include <stdio.h>
#include <string.h>
#include "pd_api.h"
#include "assets.h"
#include "camera.h"
#include "game.h"
#include "menu.h"
#include "render.h"
#include "tracks.h"

#define WARN_SHOW_S 3.0f

typedef enum { APP_MENU, APP_PLAY } AppState;

static PlaydateAPI *pd;
static Game game;
static CameraPose cam;
static BoatCfg boat_cfg;
static WeatherCfg weather_cfg;
static WorldCfg world_cfg;
static int boat_index, preset_index = WEATHER_CHOPPY;
static uint32_t restart_count;
static char warn[ASSETS_WARN_LEN];
static char track_warn[ASSETS_WARN_LEN];
static float warn_left;
static AppState app = APP_MENU;
static Menu menu;
static Track tracks[TRACKS_MAX];
static int ntracks;

static PDMenuItem *boat_item, *weather_item;

static uint32_t fresh_seed(void) {
    return pd->system->getCurrentTimeMilliseconds() * 2654435761u + (++restart_count) * 40503u;
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
    const Track trk = game.track;   /* game_init clears the mode; a boat change keeps the race */
    game_init(&game, &boat_cfg, &weather_cfg, &world_cfg, preset_index, fresh_seed());
    if (racing) game_set_track(&game, &trk);
    camera_snap();
    pd->system->resetElapsedTime();
}

static void show_menu(void) {
    app = APP_MENU;
    menu_init(&menu, ntracks);
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

static int update(void *ud) {
    (void)ud;
    PDButtons cur, pushed, released;
    pd->system->getButtonState(&cur, &pushed, &released);
    float dt = pd->system->getElapsedTime();
    pd->system->resetElapsedTime();
    float crank = pd->system->getCrankChange();

    if (app == APP_MENU) {
        MenuAction act = menu_update(&menu, (uint32_t)pushed);
        if (act == MENU_START_FREE) {
            game_set_track(&game, NULL);
            camera_snap();
            app = APP_PLAY;
        } else if (act == MENU_START_RACE) {
            game_set_track(&game, &tracks[menu.track]);
            camera_snap();
            app = APP_PLAY;
        }
        menu_draw(pd, &menu, tracks);
        return 1;
    }

    /* A restarts once the run is over; while sailing A is the autopilot and B the anchor. */
    if (game.run != RUN_SAILING && (pushed & kButtonA)) {
        game_restart(&game, fresh_seed());
        pushed = 0;
        dt = 1.0f / 30;
    }
    game_update(&game, (uint32_t)pushed, (uint32_t)cur, crank, dt);
    camera_update(&cam, &game, &game.boat, dt);

    render_frame(pd, &game, &cam);
    if (warn_left > 0.0f) {
        warn_left -= dt;
        /* below the HUD bar, on white, clipped to one screen-width line */
        size_t len = strlen(warn) < 48 ? strlen(warn) : 48;
        pd->graphics->fillRect(0, 36, CAM_W, 18, kColorWhite);
        pd->graphics->setDrawMode(kDrawModeCopy);
        pd->graphics->drawText(warn, len, kASCIIEncoding, 4, 39);
    }
    return 1;
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg) {
    (void)arg;
    if (event == kEventPause) {
        /* the menu shows this bitmap beside its items */
        LCDBitmap *img = pd->graphics->newBitmap(CAM_W, 240, kColorWhite);
        pd->graphics->pushContext(img);
        hud_diag(pd, &game);
        pd->graphics->popContext();
        pd->system->setMenuImage(img, 0);   /* the system takes a copy */
        pd->graphics->freeBitmap(img);
        return 0;
    }
    if (event == kEventInit) {
        pd = playdate;
        static const char *boat_names[ASSETS_BOATS] = { "dinghy", "j24", "tp52" };
        static const char *weather_names[WEATHER_PRESETS] = { "sunny", "cloudy", "mist", "storm", "hurricane", "choppy" };
        ntracks = tracks_load_all(pd, tracks, TRACKS_MAX, track_warn, sizeof track_warn);
        boat_item = pd->system->addOptionsMenuItem("boat", boat_names, ASSETS_BOATS, menu_boat, NULL);
        weather_item = pd->system->addOptionsMenuItem("weather", weather_names, WEATHER_PRESETS, menu_weather, NULL);
        pd->system->setMenuItemValue(weather_item, preset_index);
        pd->system->addMenuItem("restart", menu_restart, NULL);
        pd->system->addMenuItem("main menu", menu_main, NULL);
        pd->display->setRefreshRate(30.0f);
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
        pd->system->setUpdateCallback(update, NULL);
    }
    return 0;
}
