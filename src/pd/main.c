/* Playdate glue: the Platform over PlaydateAPI, input, the system menu, and drawing what app_step asks for.
 * The app flow itself is in src/app/app.c, shared with the web build. */
#include <stdio.h>
#include <string.h>
#include "pd_api.h"
#include "app.h"
#include "audio.h"
#include "render.h"
#ifdef SCENARIO
#include <math.h>
#include "mathx.h"
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

/* Scripted shot settings: HUD, wind relative to the bow, a steady camera orbit (deg/s), a caption,
 * a lone buoy, a camera fixed beside that buoy, and the trailing ghost. */
static float orbit_dps;
static char caption[SC_NAME_MAX];
static bool buoy_set, fix_on;
static double buoy_x, buoy_y;
static float buoy_heading;          /* the boat's heading when the buoy was laid: the track it crosses */
static CameraPose fix_cam;
static void scenario_shot(const ScShot *s, float dt) {
    Game *g = app_game_mut();
    if (s->hud != SC_HUD_NONE) render_set_hud(s->hud == SC_HUD_ON);
    if (s->caption_now) strcpy(caption, s->caption);
    if (s->ghost) render_set_follower(s->ghost == 1);
    if (s->buoy_now) {
        const float h = g->b.heading, ch = cosf(h), sh = sinf(h);
        buoy_x = g->b.x + (double)(s->buoy_fwd * ch - s->buoy_right * sh);
        buoy_y = g->b.y + (double)(s->buoy_fwd * sh + s->buoy_right * ch);
        buoy_heading = h;
        buoy_set = true;
        render_set_prop_buoy(true, buoy_x, buoy_y);
    }
    if (s->fixcam == 2) fix_on = false;
    if (s->fixcam == 1 && buoy_set) {
        /* DIST m from the buoy on the side away from the boat's track (starboard), looking back across the
           track, so the boat crosses from left to right; AIM turns the view right (the buoy moves left) */
        const float ch = cosf(buoy_heading), sh = sinf(buoy_heading);
        memset(&fix_cam, 0, sizeof fix_cam);
        fix_cam.x = buoy_x - (double)(s->fix_dist * sh);
        fix_cam.y = buoy_y + (double)(s->fix_dist * ch);
        fix_cam.height = s->fix_height;
        fix_cam.yaw = wrap_pi(buoy_heading - (float)M_PI_2 + deg2rad(s->fix_aim));
        fix_cam.pitch = deg2rad(s->fix_pitch);
        fix_cam.trail = true;
        fix_on = true;
    } else if (s->fixcam == 1) {
        pd->system->logToConsole("scenario: fixcam needs a buoy first");
    }
    if (s->wind_now)
        wind_set_base(&g->wind, wrap_pi(g->b.heading + deg2rad(s->wind_deg) + (float)M_PI));
    if (s->orbit_now) orbit_dps = s->orbit_dps;
    /* before app_step: the controls leave the camera yaw alone while steering, and the view reads it */
    if (orbit_dps != 0.0f && app_playing()) {
        g->ctl.camera_yaw = wrap_pi(g->ctl.camera_yaw + deg2rad(orbit_dps) * dt);
        g->ctl.camera_target_yaw = g->ctl.camera_yaw;
    }
}
#endif

#ifdef SCENARIO
/* The caption in the home screen's title face (Jersey 10 at 2x), centred near the top, black with a
   white halo so it reads on sky and sea alike. */
static void draw_caption(void) {
    static LCDFont *font;
    static bool tried;
    const struct playdate_graphics *gr = pd->graphics;
    const size_t len = strlen(caption);
    if (!tried) {
        const char *err = NULL;
        tried = true;
        font = gr->loadFont("fonts/Jersey10-20", &err);
        if (!font) pd->system->logToConsole("scenario: caption font: %s", err ? err : "?");
    }
    if (font) gr->setFont(font);
    gr->setTextTracking(0);
    const int w = gr->getTextWidth(font, caption, len, kASCIIEncoding, 0);
    const int x = (CAM_W - w) / 2, y = 10;
    gr->setDrawMode(kDrawModeFillWhite);
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if ((dx || dy) && dx * dx + dy * dy <= 5) gr->drawText(caption, len, kASCIIEncoding, x + dx, y + dy);
    gr->setDrawMode(kDrawModeFillBlack);
    gr->drawText(caption, len, kASCIIEncoding, x, y);
    gr->setDrawMode(kDrawModeCopy);
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
    scenario_shot(&sf.shot, dt);
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
    } else if (d == APP_DRAW_PLAY || d == APP_DRAW_CONFIRM || d == APP_DRAW_DEMO_END) {
#ifdef SCENARIO
        render_frame(pd, app_game(), fix_on ? &fix_cam : app_camera(), app_ghost_opponent());
#else
        render_frame(pd, app_game(), app_camera(), app_ghost_opponent());
#endif
        const char *warn = app_warning();
        if (d == APP_DRAW_DEMO_END) {
            hud_demo_end(pd);
        } else if (d == APP_DRAW_CONFIRM) {
            hud_back_confirm(pd);
        } else if (warn && render_hud()) {
            /* below the HUD bar, on white, clipped to one screen-width line */
            size_t len = strlen(warn) < 48 ? strlen(warn) : 48;
            pd->graphics->fillRect(0, 36, CAM_W, 18, kColorWhite);
            pd->graphics->setDrawMode(kDrawModeCopy);
            pd->graphics->drawText(warn, len, kASCIIEncoding, 4, 39);
        }
    }
#ifdef SCENARIO
    if (caption[0] && (d == APP_DRAW_PLAY || d == APP_DRAW_CONFIRM)) draw_caption();
#endif
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
