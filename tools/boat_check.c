/* Validates boat JSON files with the game's own loader.
 *
 *   boat_check <file.json>...
 *
 * Prints "<file>: ok" or "<file>: <message>" per file; exits 1 if any file is unreadable, too big
 * for the game's 16 KB read buffer, makes boatcfg_load report a warning, or (fore-and-aft rigs
 * only) holds close-hauled with lee helm or more than 8 deg of weather helm. */
#include "balance.h"
#include "boatcfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_MAX 16384   /* matches assets.c: the game rejects anything of 16384 bytes or more */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.json>...\n", argv[0]);
        return 2;
    }
    int bad = 0;
    static char buf[FILE_MAX + 1];
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            printf("%s: cannot read\n", argv[i]);
            bad = 1;
            continue;
        }
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        if (n >= FILE_MAX) {
            printf("%s: too large (the game reads at most %d bytes)\n", argv[i], FILE_MAX - 1);
            bad = 1;
            continue;
        }
        BoatCfg cfg;
        char warn[512];
        warn[0] = 0;
        boatcfg_default(&cfg);
        if (!boatcfg_load(&cfg, buf, n, warn, sizeof warn)) {
            printf("%s: %s\n", argv[i], warn[0] ? warn : "invalid");
            bad = 1;
            continue;
        }
        float helm = 0;
        if (balance_is_fore_and_aft(&cfg)) {
            if (!balance_weather_helm_deg(&cfg, &helm)) {
                printf("%s: could not settle close-hauled\n", argv[i]);
                bad = 1;
                continue;
            }
            if (helm < 1.0f || helm > 8.0f) {
                printf("%s: weather helm %.1f deg is outside 1-8 (sail ce_x, board_x or rudder size)\n", argv[i], helm);
                bad = 1;
                continue;
            }
        }
        printf("%s: ok\n", argv[i]);
    }
    return bad;
}
