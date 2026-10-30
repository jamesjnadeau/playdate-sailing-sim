/* Emits the built-in dinghy as boats/dinghy.json:
 *   cc -std=c99 -Isrc/sim tools/gen_boat_json.c src/sim/boatcfg.c src/sim/json.c src/sim/mathx.c -lm -o /tmp/gen && /tmp/gen > boats/dinghy.json */
#include <stdio.h>
#include "boatcfg.h"

int main(void) {
    static char buf[32768];
    BoatCfg c;
    boatcfg_default(&c);
    size_t n = boatcfg_to_json(&c, buf, sizeof buf);
    if (!n) return 1;
    fwrite(buf, 1, n, stdout);
    return 0;
}
