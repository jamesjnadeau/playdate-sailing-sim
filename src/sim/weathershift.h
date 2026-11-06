#ifndef WEATHERSHIFT_H
#define WEATHERSHIFT_H
#include <stdint.h>

/* Journey weather drift: hops between the player's ticked presets, one step
 * up or down the wind order, with a random gap between hops and a timed blend. */
typedef enum { WSHIFT_NONE, WSHIFT_START, WSHIFT_END } WShiftEvent;

typedef struct {
    uint32_t mask;      /* bit per preset allowed; never 0 after init */
    uint32_t seed, n;   /* every draw is hashf(hash2(salt, n++, seed)) */
    int from, to;       /* presets; equal when settled */
    float progress;     /* 0..1 through the blend; 1 when settled */
    float gap;          /* s until the next shift starts (settled only) */
} WeatherShift;

/* Nearest ticked preset above / below cur, picked by coin (0..1) when both exist
 * (coin < 0.5 goes down); -1 when neither. */
int wshift_neighbour(uint32_t mask, int cur, float coin);
/* mask & valid bits == 0 -> mask = 1u << fallback. Picks a random ticked start (fallback itself when only it
 * is ticked, with no draw), settles there, rolls the first gap. Returns the start preset. */
int wshift_init(WeatherShift *s, uint32_t mask, int fallback, uint32_t seed, float gap_min, float gap_max);
/* Settled: gap -= dt; at <= 0 picks the neighbour and returns WSHIFT_START (no neighbour -> re-rolls gap, NONE).
 * Blending: progress += dt / blend_s, clamped to 1; on reaching 1 settles (from = to), rolls the gap, returns END. */
WShiftEvent wshift_step(WeatherShift *s, float dt, float gap_min, float gap_max, float blend_s);
/* Eased blend weight: smoothstep(progress) - 0 at the start, 1 when settled. */
float wshift_weight(const WeatherShift *s);

#endif
