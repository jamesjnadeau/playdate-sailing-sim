#include "hudinfo.h"
#include <math.h>
#include "mathx.h"

#define R2D 57.29578f

const char *hud_mode_glyph(Mode m) {
    switch (m) {
    case MODE_WHEEL:  return "\xE2\xAC\x86";   /* U+2B06 up: matches BTN_UP */
    case MODE_TRIM:   return "\xE2\x9E\xA1";   /* U+27A1 right: matches BTN_RIGHT */
    case MODE_HOIST:  return "\xE2\xAC\x85";   /* U+2B05 left: matches BTN_LEFT */
    case MODE_CAMERA: return "\xE2\xAC\x87";   /* U+2B07 down: matches BTN_DOWN */
    }
    return "?";
}

bool hud_shows_nav(Mode m) {
    return m == MODE_WHEEL;
}

int hud_heading_deg(float rad) {
    if (!isfinite(rad)) return 0;
    float d = fmodf(rad * R2D, 360.0f);
    if (d < 0.0f) d += 360.0f;
    int i = (int)floorf(d + 0.5f) % 360;
    return i < 0 ? 0 : i;
}

float hud_sail_fraction(Mode m, const SailCfg *s, float trim_rad, float hoist) {
    if (m == MODE_HOIST) return isfinite(hoist) ? clampf(hoist, 0.0f, 1.0f) : 0.0f;
    if (m != MODE_TRIM || !isfinite(trim_rad)) return 0.0f;
    float range = s->trim_max_deg - s->trim_min_deg;
    if (!(range > 0.0f)) return 0.0f;
    return clampf((trim_rad * R2D - s->trim_min_deg) / range, 0.0f, 1.0f);
}
