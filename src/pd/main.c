#include <stdio.h>
#include <string.h>
#include "pd_api.h"
#include "assets.h"
#include "camera.h"
#include "game.h"
#include "render.h"

#define WARN_SHOW_S 3.0f

static PlaydateAPI *pd;
static Game game;
static CameraPose cam;
static BoatCfg boat_cfg;
static WeatherCfg weather_cfg;
static WorldCfg world_cfg;
static int boat_index, preset_index;
static uint32_t restart_count;
static char warn[ASSETS_WARN_LEN];
static float warn_left;

static PDMenuItem *boat_item, *weather_item, *view_item;

static uint32_t fresh_seed(void) {
    return pd->system->getCurrentTimeMilliseconds() * 2654435761u + (++restart_count) * 40503u;
}

static void begin(bool reload) {
    if (reload) {
        assets_load(pd, boat_index, &boat_cfg, &weather_cfg, &world_cfg, warn, sizeof warn);
        warn_left = warn[0] ? WARN_SHOW_S : 0.0f;
        if (warn[0]) pd->system->logToConsole("config: %s", warn);
    }
    game_init(&game, &boat_cfg, &weather_cfg, &world_cfg, preset_index, fresh_seed());
    bool trail = cam.trail;
    memset(&cam, 0, sizeof cam);
    cam.trail = trail;
    camera_update(&cam, &game, &game.boat, 1.0f / 30);
    pd->system->resetElapsedTime();
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
static void menu_view(void *ud) {
    (void)ud;
    cam.trail = pd->system->getMenuItemValue(view_item) == 1;
}
static void menu_restart(void *ud) { (void)ud; game_restart(&game, fresh_seed()); }
static void menu_reload(void *ud) { (void)ud; begin(true); }

static int update(void *ud) {
    (void)ud;
    PDButtons cur, pushed, released;
    pd->system->getButtonState(&cur, &pushed, &released);
    float dt = pd->system->getElapsedTime();
    pd->system->resetElapsedTime();
    float crank = pd->system->getCrankChange();

    /* A restarts once the run is over; while sailing it is the anchor button. */
    if (game.run != RUN_SAILING && (pushed & kButtonA)) {
        game_restart(&game, fresh_seed());
        pushed = 0;
        dt = 1.0f / 30;
    }
    game_update(&game, (uint32_t)pushed, crank, dt);
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
    if (event == kEventInit) {
        pd = playdate;
        static const char *boat_names[ASSETS_BOATS] = { "dinghy", "spinnaker" };
        static const char *weather_names[WEATHER_PRESETS] = { "sunny", "cloudy", "mist", "storm" };
        boat_item = pd->system->addOptionsMenuItem("boat", boat_names, ASSETS_BOATS, menu_boat, NULL);
        weather_item = pd->system->addOptionsMenuItem("weather", weather_names, WEATHER_PRESETS, menu_weather, NULL);
        static const char *view_names[2] = { "near", "far" };
        view_item = pd->system->addOptionsMenuItem("camera", view_names, 2, menu_view, NULL);
        pd->system->addMenuItem("restart", menu_restart, NULL);
        pd->system->addMenuItem("reload config", menu_reload, NULL);
        pd->display->setRefreshRate(30.0f);
        begin(true);
        pd->system->setUpdateCallback(update, NULL);
    }
    return 0;
}
