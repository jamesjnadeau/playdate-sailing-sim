/* The functions the page calls (web/js/main.js). One web_step per animation frame. */
#include <stdlib.h>
#include "web.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

static AppDraw last = APP_DRAW_MENU;

EXPORT void web_init(uint32_t seed_base) {
    web_platform_init();
    web_platform_seed_base(seed_base);
    app_init(&web_platform);
}

/* pressed / held: BTN_* masks; crank in degrees this frame; dt in seconds. Returns the AppDraw. */
EXPORT int web_step(uint32_t pressed, uint32_t held, float crank, float dt) {
    last = app_step(pressed, held, crank, dt);
    return (int)last;
}

EXPORT const char *web_frame_json(void) { return web_json_frame(last); }
EXPORT const char *web_static_json(void) { return web_json_static(); }
EXPORT int web_static_generation(void) { return web_static_gen(); }

EXPORT void web_sys_boat(int boat) { app_sys_boat(boat); }
EXPORT void web_sys_weather(int preset) { app_sys_weather(preset); }
EXPORT void web_sys_back(void) { app_sys_back(); }
EXPORT int web_playing(void) { return app_playing(); }
EXPORT int web_take_dirty(void) { return web_platform_take_dirty(); }

/* The sea sound: n stereo frames of 16-bit samples into two buffers the page allocated with web_alloc. */
EXPORT int web_audio(int16_t *left, int16_t *right, int n) { return web_audio_fill(left, right, n); }
EXPORT void web_audio_on(int on) { web_audio_active(on != 0); }
EXPORT void *web_alloc(int n) { return malloc((size_t)n); }
