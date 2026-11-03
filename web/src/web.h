#ifndef WEB_H
#define WEB_H
#include <stdbool.h>
#include <stdint.h>
#include "app.h"

extern const Platform web_platform;
void web_platform_init(void);
void web_platform_roots(const char *bundle, const char *save);
void web_platform_seed_base(uint32_t base);
int web_platform_take_dirty(void);
int web_audio_fill(int16_t *left, int16_t *right, int n);
void web_audio_active(bool on);

/* JSON for the page (web_json.c). Both return a buffer valid until the next call. */
const char *web_json_frame(AppDraw d);    /* everything that changes per frame */
const char *web_json_static(void);        /* boat geometry, names, tracks, help: re-read when web_static_gen changes */
int web_static_gen(void);

#endif
