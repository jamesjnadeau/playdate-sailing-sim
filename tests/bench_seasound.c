#define _POSIX_C_SOURCE 199309L   /* clock_gettime */
/* Times seasound_render with every voice sounding. Host speed isn't device speed,
 * but the ratio between two versions of the synth is a fair guide. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "seasound.h"

#define BLOCK 256          /* the mixer's typical block size */
#define SECONDS 10

static SeaSound ss;
static int16_t L[BLOCK], R[BLOCK];
static volatile int32_t sink;

int main(void) {
    seasound_init(&ss);
    ss.p = (SoundParams){ .wash = 1, .bright = 0.6f, .swell = 1, .gurgle = 1, .pan = 0.3f,
                          .wind = 1, .whistle_hz = 700, .flutter = 1 };
    int blocks = SEASOUND_RATE * SECONDS / BLOCK;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < blocks; b++) {
        if (b % 40 == 0) ss.slap_ev = ss.thump_ev = 1.0f;
        seasound_render(&ss, L, R, BLOCK);
        sink += L[b % BLOCK] + R[(b * 7) % BLOCK];
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
    printf("seasound_render: %.2f ns per output sample\n", ns / ((double)blocks * BLOCK));
    return 0;
}
