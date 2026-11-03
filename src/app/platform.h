#ifndef PLATFORM_H
#define PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game.h"

/* What the app layer needs from the machine it runs on: files, memory, a log, a seed, the sea
 * sound and the system menu. The Playdate build fills it from PlaydateAPI (src/pd/main.c), the web
 * build from stdio and Emscripten (web/src/web_platform.c). Nothing in src/app includes pd_api.h. */

typedef enum {
    PLAT_READ,        /* shipped with the game (the pdx on the Playdate) */
    PLAT_READ_DATA,   /* the player's saved data */
    PLAT_WRITE        /* the player's saved data, truncated */
} PlatMode;

typedef struct {
    void *(*open)(const char *path, PlatMode mode);   /* NULL when missing */
    int (*read)(void *f, void *buf, unsigned len);     /* bytes read, < 0 on error */
    int (*write)(void *f, const void *buf, unsigned len);
    void (*close)(void *f);
    /* Calls cb once per entry of a shipped directory. */
    void (*listfiles)(const char *dir, void (*cb)(const char *name, void *ud), void *ud);
    void *(*realloc)(void *ptr, size_t size);          /* size 0 frees */
    void (*log)(const char *fmt, ...);
    uint32_t (*seed)(uint32_t restart_count);          /* the seed for a fresh run */
    void (*reset_time)(void);                          /* after a slow load, so the next dt is short */
    void (*audio_set_level)(int level);
    void (*audio_set_active)(bool active);
    void (*audio_update)(const Game *g, float dt);     /* g NULL: no run on screen */
    /* Rebuild the system menu: boat and weather pickers in free range (racing false), only "go back"
       otherwise. The platform calls app_sys_boat / app_sys_weather / app_sys_back when they are picked. */
    void (*sys_menu)(bool racing, int boat, int preset);
} Platform;

#endif
