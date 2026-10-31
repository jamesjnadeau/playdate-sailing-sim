#ifndef PDSTUB_H
#define PDSTUB_H
/* Host stand-in for the Playdate C API: just enough of system, file, display and graphics for the
 * real src/pd main loop to run headless. Reads come from assets_root (plus the virtual file
 * "scenario.txt"), writes go to outdir. */
#include "pd_api.h"

/* The returned API stays valid for the life of the process. scenario_text is copied. */
PlaydateAPI *pdstub_init(const char *outdir, const char *assets_root, const char *scenario_text);
/* Sends kEventInit, then calls the update callback until done.marker appears in outdir or
 * 100000 frames pass. Non-zero on timeout. */
int pdstub_run(void);

#endif
