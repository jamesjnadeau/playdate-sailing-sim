/* harness <scenario.txt> <outdir> <assets_root>: runs the real main loop against pdstub. Exit 0 only
 * when the scenario parsed and ran to done.marker; a parse error exits 2 with "line N: ..." on stderr. */
#include <stdio.h>
#include <stdlib.h>
#include "pdstub.h"
#include "scenario.h"

#define SCEN_TEXT_MAX 16384

static Scenario scen;   /* ~20 KB */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <scenario.txt> <outdir> <assets_root>\n", argv[0]);
        return 64;
    }
    static char text[SCEN_TEXT_MAX];
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 66; }
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (n == 0) {   /* the device build treats an empty scenario.txt as an error too */
        fprintf(stderr, "%s: empty scenario\n", argv[1]);
        return 2;
    }

    /* scenario_init only logs a parse error, so check here to get a failing exit code */
    char err[96] = "";
    if (!scenario_parse(&scen, text, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", argv[1], err);
        return 2;
    }
    pdstub_init(argv[2], argv[3], text);
    if (pdstub_run() != 0) {
        fprintf(stderr, "%s: timed out without done.marker\n", argv[1]);
        return 1;
    }
    return 0;
}
