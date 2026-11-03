/* Playdate glue: the Platform over PlaydateAPI, input, the system menu, and drawing what app_step asks for.
 * The app flow itself is in src/app/app.c, shared with the web build. */
#include <stdio.h>
#include <string.h>
#include "pd_api.h"
#include "app.h"
#include "audio.h"
#include "render.h"
#ifdef SCENARIO
#include "scenario.h"
#endif

static PlaydateAPI *pd;
static PDMenuItem *boat_item, *weather_item;

static void *pf_open(const char *path, PlatMode mode) {
    const FileOptions opt = mode == PLAT_READ ? kFileRead : mode == PLAT_READ_DATA ? kFileReadData : kFileWrite;
    return pd->file->open(path, opt);
}
static int pf_read(void *f, void *buf, unsigned len) { return pd->file->read(f, buf, len); }
static int pf_write(void *f, const void *buf, unsigned len) { return pd->file->write(f, buf, len); }
static void pf_close(void *f) { pd->file->close(f); }
static void pf_list(const char *dir, void (*cb)(const char *, void *), void *ud) { pd->file->listfiles(dir, cb, ud, 0); }
static void *pf_realloc(void *ptr, size_t size) { return pd->system->realloc(ptr, size); }
static void pf_reset_time(void) { pd->system->resetElapsedTime(); }
static uint32_t pf_seed(uint32_t counter) {
#ifdef SCENARIO
    return scenario_seed(counter);
#else
    return pd->system->getCurrentTimeMilliseconds() * 2654435761u + counter * 40503u;
#endif
}

static void menu_boat(void *ud) { (void)ud; app_sys_boat(pd->system->getMenuItemValue(boat_item)); }
static void menu_weather(void *ud) { (void)ud; app_sys_weather(pd->system->getMenuItemValue(weather_item)); }
static void menu_back(void *ud) { (void)ud; app_sys_back(); }

/* A race is set up from the main menu, so the system menu only offers boat and weather in free range. */
static void pf_sys_menu(bool racing, int boat, int preset) {
    pd->system->removeAllMenuItems();
    boat_item = weather_item = NULL;
    if (!racing) {
        boat_item = pd->system->addOptionsMenuItem("boat", (const char **)assets_boat_names(), assets_boat_count(), menu_boat, NULL);
        weather_item = pd->system->addOptionsMenuItem("weather", (const char **)WEATHER_NAMES, WEATHER_PRESETS, menu_weather, NULL);
        pd->system->setMenuItemValue(boat_item, boat);
        pd->system->setMenuItemValue(weather_item, preset);
    }
    pd->system->addMenuItem("go back", menu_back, NULL);
}

static Platform plat = {
    .open = pf_open, .read = pf_read, .write = pf_write, .close = pf_close, .listfiles = pf_list,
    .realloc = pf_realloc, .seed = pf_seed, .reset_time = pf_reset_time,
    .audio_set_level = audio_set_level, .audio_set_active = audio_set_active, .audio_update = audio_update,
    .sys_menu = pf_sys_menu,
};

#ifdef SCENARIO
/* A scripted system-menu action, applied as the simulator would on selection. */
static void scenario_sys(const ScFrame *f) {
    switch (f->sys) {
    case SC_SYS_BACK: menu_back(NULL); break;
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

static int update(void *ud) {
    (void)ud;
    PDButtons cur, pushed, released;
#ifdef SCENARIO
    float dt, crank;
    ScFrame sf;
    scenario_poll_input(&cur, &pushed, &released, &crank, &dt, &sf);
    scenario_sys(&sf);
    if (sf.praise_now) app_force_praise(sf.praise);
#else
    pd->system->getButtonState(&cur, &pushed, &released);
    float dt = pd->system->getElapsedTime();
    pd->system->resetElapsedTime();
    float crank = pd->system->getCrankChange();
#endif

    const AppDraw d = app_step((uint32_t)pushed, (uint32_t)cur, crank, dt);
    if (d == APP_DRAW_MENU) {
        menu_draw(pd, app_menu(), app_tracks(), app_best(), app_boat_best(), app_help());
    } else if (d == APP_DRAW_PLAY || d == APP_DRAW_CONFIRM) {
        render_frame(pd, app_game(), app_camera(), app_ghost_opponent());
        const char *warn = app_warning();
        if (d == APP_DRAW_CONFIRM) {
            hud_back_confirm(pd);
        } else if (warn) {
            /* below the HUD bar, on white, clipped to one screen-width line */
            size_t len = strlen(warn) < 48 ? strlen(warn) : 48;
            pd->graphics->fillRect(0, 36, CAM_W, 18, kColorWhite);
            pd->graphics->setDrawMode(kDrawModeCopy);
            pd->graphics->drawText(warn, len, kASCIIEncoding, 4, 39);
        }
    }
#ifdef SHOW_FPS
    if (d == APP_DRAW_MENU || d == APP_DRAW_PLAY) pd->system->drawFPS(0, 0);
#endif
#ifdef SCENARIO
    scenario_frame_done(pd, app_game(), app_draw_name(d));
#endif
    return 1;
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg) {
    (void)arg;
    if (event == kEventResume) audio_set_active(app_playing());
    if (event == kEventPause) {
        audio_set_active(false);
        /* the menu shows this bitmap beside its items */
        LCDBitmap *img = pd->graphics->newBitmap(CAM_W, 240, kColorWhite);
        pd->graphics->pushContext(img);
        hud_pause_panel(pd, app_game());
        pd->graphics->popContext();
        pd->system->setMenuImage(img, 0);   /* the system takes a copy */
        pd->graphics->freeBitmap(img);
        return 0;
    }
    if (event == kEventInit) {
        pd = playdate;
        plat.log = pd->system->logToConsole;
        pd->display->setRefreshRate(30.0f);
        audio_init(pd);
        app_init(&plat);
#ifdef SCENARIO
        scenario_init(pd);
#endif
        pd->system->setUpdateCallback(update, NULL);
    }
    return 0;
}
