#ifndef AUDIO_H
#define AUDIO_H
#include "pd_api.h"
#include "game.h"

/* Registers the sea sound with the Playdate mixer. A host without a sound API (the test harness) makes
   every call here a no-op. */
void audio_init(PlaydateAPI *pd);
/* 0..SEASOUND_LEVELS-1; 0 is off. */
void audio_set_level(int level);
/* false fades the sea out (system menu open, main menu); true brings it back. */
void audio_set_active(bool active);
/* Once per frame while sailing. */
void audio_update(const Game *g, float dt);
#endif
