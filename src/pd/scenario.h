#ifndef SCENARIO_H
#define SCENARIO_H
/* Scripted-input scenario parser and per-frame player. Pure C, no Playdate
 * dependencies, no allocation. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SC_MAX_CMDS 512
#define SC_NAME_MAX 48

typedef enum { SC_SYS_NONE, SC_SYS_BACK,SC_SYS_BOAT, SC_SYS_WEATHER } ScSys;

typedef struct {
    uint32_t cur, pushed, released;   /* BTN_* / PDButtons bits: left 1, right 2, up 4, down 8, B 16, A 32 */
    float crank;                      /* degrees this frame */
    ScSys sys; int sys_arg;           /* system-menu action to apply BEFORE this frame's update */
    char sys_name[SC_NAME_MAX];       /* `sysmenu boat NAME`: boat by name (sys_arg is then -1) */
    bool praise_now; int praise;      /* `praise N`: show message N on this frame */
} ScFrame;

typedef enum {
    SC_C_WAIT, SC_C_PRESS, SC_C_HOLD, SC_C_CRANK,
    SC_C_SNAP, SC_C_STATE, SC_C_SYS, SC_C_PRAISE, SC_C_END
} ScCmdKind;

typedef struct {
    ScCmdKind kind;
    uint32_t btn;
    int n;                            /* frames, or sysmenu argument */
    float deg;
    ScSys sys;
    char name[SC_NAME_MAX];
} ScCmd;

struct Scenario {
    ScCmd cmds[SC_MAX_CMDS];
    int ncmds;
    int pc;                           /* next command */
    int k;                            /* frames already emitted of cmds[pc] */
    uint32_t prev;                    /* buttons held on the previous frame */
    ScSys pend_sys; int pend_arg;     /* sysmenu waiting for the next frame */
    char pend_name[SC_NAME_MAX];
    bool pend_praise; int pend_praise_idx;   /* praise waiting for the next frame */
};
typedef struct Scenario Scenario;

typedef enum { SC_AFTER_NONE, SC_AFTER_SNAP, SC_AFTER_STATE, SC_AFTER_END } ScAfterKind;

/* err receives "line N: ..." on failure. */
bool scenario_parse(Scenario *s, const char *text, char *err, size_t errn);
/* Next frame's input; false once finished. */
bool scenario_poll(Scenario *s, ScFrame *f);
/* Call after each frame until NONE. name receives the snap name. */
ScAfterKind scenario_after(Scenario *s, char name[SC_NAME_MAX]);

#ifdef SCENARIO
/* Playdate-side seam, implemented in scenario_io.c; main.c calls these only under -DSCENARIO. */
#include "pd_api.h"
#include "game.h"

/* Reads "scenario.txt"; on a read or parse error logs it, writes done.marker and ends. */
void scenario_init(PlaydateAPI *pd);
/* Replaces the button/crank/dt reads; dt is fixed at 1/30. False once the scenario is finished
 * (inputs are then all zero). */
bool scenario_poll_input(PDButtons *cur, PDButtons *pushed, PDButtons *released, float *crank, float *dt, ScFrame *f);
/* After the frame is drawn: drains scenario_after (snap writes NAME.fb and NAME.state, end writes done.marker). */
void scenario_frame_done(PlaydateAPI *pd, const Game *g, const char *app_name);
/* Deterministic stand-in for fresh_seed. */
uint32_t scenario_seed(uint32_t counter);
#endif

#endif
