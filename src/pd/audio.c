#include "audio.h"
#include "seasound.h"

static SeaSound sea_sound;
static bool ready;

static int source_cb(void *ctx, int16_t *left, int16_t *right, int len) {
    return seasound_render((SeaSound *)ctx, left, right, len);
}

void audio_init(PlaydateAPI *pd) {
    seasound_init(&sea_sound);
    if (!pd->sound || !pd->sound->addSource) return;
    pd->sound->addSource(source_cb, &sea_sound, 1);
    ready = true;
}

void audio_set_level(int level) { seasound_set_level(&sea_sound, level); }

void audio_set_active(bool active) { seasound_set_active(&sea_sound, active); }

void audio_update(const Game *g, float dt) {
    if (!ready) return;
    if (g) seasound_update(&sea_sound, g, dt);
    else seasound_quiet(&sea_sound);
}
