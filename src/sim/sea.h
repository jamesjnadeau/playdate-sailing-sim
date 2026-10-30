#ifndef SEA_H
#define SEA_H
#include <stdint.h>
#include "boat.h"
#include "boatcfg.h"
#include "weather.h"

#define SEA_MAX_COMPONENTS 6
#define SEA_WRAP 1024.0   /* m; every wave vector is an integer multiple of 2pi/SEA_WRAP */

/* h = amp * cos(2pi (m x + n y)/SEA_WRAP - omega t + phase), a wave travelling toward (m, n).
   Integer (m, n) lets x and y be wrapped modulo SEA_WRAP in double precision, so the sea
   keeps its detail a million metres from the origin. */
typedef struct { int m, n; float amp, omega, phase; } SeaComp;

typedef struct {
    int n;
    SeaComp c[SEA_MAX_COMPONENTS];
    float period;       /* s; every omega is an integer multiple of 2pi/period */
    float slam_speed;   /* m/s; the bow slams only above this speed */
    float slam_gain;    /* 0 disables slamming */
    float surf_gain;    /* fraction of the downslope weight component applied as surge */
} Sea;

/* Amplitudes sum to wave_height / 2, so crest-to-trough peaks at wave_height. Components
   share the preset period or its 2nd/3rd harmonics, so the sea repeats exactly every period.
   Directions and phases come from `seed`; wave_steepness sets how much of the energy is chop. */
void sea_init(Sea *s, const WeatherPreset *p, uint32_t seed);

/* Surface elevation in metres, + up. Non-finite inputs are treated as 0. */
float sea_height(const Sea *s, float x, float y, float t);

/* Forces on the hull from 7 samples along its length and 2 across (at midships), sampled
   once per frame; the caller holds the result across substeps. All zero on a flat sea.
   pitch is + bow up (rad) and heave is the mean elevation under the hull (m), both for the camera. */
WaveForces sea_hull_forces(const Sea *s, const BoatState *b, const BoatCfg *cfg, float t);

#endif
