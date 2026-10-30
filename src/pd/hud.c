#include <math.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "mathx.h"

static const char *const MODE_NAMES[] = { "WHEEL", "TRIM", "HOIST", "CAMERA" };

static void text(PlaydateAPI *pd, const char *s, int x, int y) {
    pd->graphics->drawText(s, strlen(s), kASCIIEncoding, x, y);
}

void hud_draw(PlaydateAPI *pd, const Game *g) {
    const BoatState *b = &g->b;
    char line[64];
    const int top = CAM_H - 36;   /* status panel sits along the bottom of the screen */
    float hdg = fmodf(b->heading * 57.29578f + 360.0f, 360.0f);

    pd->graphics->fillRect(0, top, CAM_W, 36, kColorBlack);
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    const char *sail = g->boat.sails[g->ctl.sail].name;
    snprintf(line, sizeof line, "%s %s", MODE_NAMES[g->ctl.mode],
             (g->ctl.mode == MODE_TRIM || g->ctl.mode == MODE_HOIST) ? sail : "");
    text(pd, line, 4, top + 2);
    snprintf(line, sizeof line, "%.1fkn  hdg %03.0f  heel %+.0f", fabsf(b->u) * 1.94384f, hdg, b->heel * 57.29578f);
    text(pd, line, 4, top + 18);

    if (g->ap.on) {
        snprintf(line, sizeof line, "AP %03.0f", fmodf(g->ap.target * 57.29578f + 360.0f, 360.0f));
        text(pd, line, 250, top + 2);
    }

    /* apparent wind dial: the tick shows where the wind comes from relative to the bow (up) */
    const int cx = CAM_W - 22, cy = top + 18, R = 15;
    pd->graphics->drawEllipse(cx - R, cy - R, 2 * R, 2 * R, 1, 0, 0, kColorWhite);
    float a = b->awa;
    pd->graphics->drawLine(cx, cy, (int)(cx + R * sinf(a)), (int)(cy - R * cosf(a)), 2, kColorWhite);
    pd->graphics->drawLine(cx, cy - R - 2, cx, cy - R + 2, 1, kColorWhite);
    snprintf(line, sizeof line, "%.0f", b->aws);
    text(pd, line, cx - 58, top + 10);

    /* selected sail: trim or hoist bar */
    if (g->ctl.mode == MODE_TRIM || g->ctl.mode == MODE_HOIST) {
        const SailCfg *s = &g->boat.sails[g->ctl.sail];
        float f = g->ctl.mode == MODE_HOIST ? b->hoists[g->ctl.sail]
                : (b->trims[g->ctl.sail] - s->trim_min_deg * 0.017453f) /
                  fmaxf((s->trim_max_deg - s->trim_min_deg) * 0.017453f, 1e-3f);
        f = clampf(f, 0.0f, 1.0f);
        pd->graphics->drawRect(150, top + 6, 82, 8, kColorWhite);
        pd->graphics->fillRect(152, top + 8, (int)(78.0f * f), 4, kColorWhite);
    }
    pd->graphics->setDrawMode(kDrawModeCopy);
}

void hud_prompt(PlaydateAPI *pd, const Game *g) {
    if (g->run == RUN_SAILING) return;
    const char *head = g->run == RUN_CAPSIZED ? "CAPSIZED" : "WRECKED";
    pd->graphics->fillRect(90, 80, 220, 64, kColorBlack);
    pd->graphics->drawRect(94, 84, 212, 56, kColorWhite);
    pd->graphics->setDrawMode(kDrawModeFillWhite);
    text(pd, head, 200 - 4 * (int)strlen(head), 90);
    text(pd, "Press A to restart", 98, 114);
    pd->graphics->setDrawMode(kDrawModeCopy);
}
