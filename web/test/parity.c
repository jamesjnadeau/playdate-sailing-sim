/* parity <bundle_root> <save_dir>: drives the shared app through a fixed input script and prints the
 * game state every 60 frames. Built natively and with Emscripten (run under node); compare.py checks the
 * two agree. They match to rounding, not bit for bit: glibc and Emscripten's libm round sinf/cosf
 * differently, as the Playdate's own libm does from the host's. */
#include <stdio.h>
#include "web.h"
#include "controls.h"

typedef struct { int frames; uint32_t press, hold; float crank; } Step;

/* main menu -> Free Range; helm, trim, autopilot, autotrim, look; then back to the menu and into a race */
static const Step script[] = {
    { 5, 0, 0, 0 }, { 1, BTN_A, BTN_A, 0 }, { 30, 0, 0, 0 },
    { 1, BTN_UP, BTN_UP, 0 }, { 90, 0, 0, 4.0f }, { 1, BTN_RIGHT, BTN_RIGHT, 0 }, { 120, 0, 0, -3.0f },
    { 1, BTN_A, BTN_A, 0 }, { 300, 0, 0, 0 }, { 1, BTN_B, BTN_B, 0 }, { 200, 0, 0, 1.0f },
    { 1, BTN_DOWN, BTN_DOWN, 0 }, { 60, 0, 0, 6.0f }, { 1, BTN_UP, BTN_UP, 0 }, { 400, 0, 0, -0.5f },
    /* go back -> yes, then Racing: down twice to Racing, A through course, preview, boat and weather */
    { -1, 0, 0, 0 }, { 1, BTN_A, BTN_A, 0 }, { 5, 0, 0, 0 },
    { 1, BTN_DOWN, BTN_DOWN, 0 }, { 1, 0, 0, 0 }, { 1, BTN_DOWN, BTN_DOWN, 0 }, { 1, 0, 0, 0 },
    { 1, BTN_A, BTN_A, 0 }, { 1, 0, 0, 0 }, { 1, BTN_A, BTN_A, 0 }, { 1, 0, 0, 0 }, { 1, BTN_A, BTN_A, 0 },
    { 1, 0, 0, 0 }, { 1, BTN_A, BTN_A, 0 }, { 1, 0, 0, 0 }, { 1, BTN_A, BTN_A, 0 }, { 30, 0, 0, 0 },
    { 1, BTN_UP, BTN_UP, 0 }, { 600, 0, 0, 0.8f },
};

static void dump(int frame, AppDraw d) {
    const Game *g = app_game();
    printf("%d draw=%d mode=%d run=%d t=%.4f x=%.4f y=%.4f hdg=%.5f heel=%.5f u=%.5f wind=%.4f/%.4f\n", frame, d,
           g->mode, g->run, g->t, g->b.x, g->b.y, g->b.heading, g->b.heel, g->b.u, g->wind.dir, g->wind.speed);
}

int main(int argc, char **argv) {
    if (argc == 3) web_platform_roots(argv[1], argv[2]);
    web_platform_init();
    web_platform_seed_base(777u);
    app_init(&web_platform);
    int frame = 0;
    AppDraw d = APP_DRAW_MENU;
    for (size_t s = 0; s < sizeof script / sizeof script[0]; s++) {
        if (script[s].frames < 0) {   /* the system menu's "go back" */
            app_sys_back();
            continue;
        }
        for (int k = 0; k < script[s].frames; k++) {
            d = app_step(k == 0 ? script[s].press : 0, script[s].hold, script[s].crank, 1.0f / 30);
            if (++frame % 60 == 0) dump(frame, d);
            (void)web_json_frame(d);   /* the snapshot writer runs on every kind of frame */
        }
    }
    dump(frame, d);
    (void)web_json_static();
    return 0;
}
