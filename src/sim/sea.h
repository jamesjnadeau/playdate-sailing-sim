#ifndef SEA_H
#define SEA_H
#include <stdbool.h>
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
    /* Boat-anchored form, for morphing between seas without a jump far from the origin.
       While anchored the phase at (x, y) is phb[i] + kx*(x - ax) + ky*(y - ay) (in double),
       time comes only from sea_advance, and c[i].m/n/phase are unused. */
    bool anchored;                    /* phases held at (ax, ay); t is ignored while set */
    double ax, ay;                    /* anchor, world m */
    float kx[SEA_MAX_COMPONENTS], ky[SEA_MAX_COMPONENTS];   /* rad/m, used while anchored */
    double phb[SEA_MAX_COMPONENTS];   /* phase at the anchor, wrapped to [0, 2pi) */
} Sea;

/* Amplitudes sum to wave_height / 2, so crest-to-trough peaks at wave_height. Components
   share the preset period or its 2nd/3rd harmonics, so the sea repeats exactly every period.
   Directions and phases come from `seed`; wave_steepness sets how much of the energy is chop. */
void sea_init(Sea *s, const WeatherPreset *p, uint32_t seed);

/* Surface elevation in metres, + up. Non-finite inputs are treated as 0. */
float sea_height(const Sea *s, float x, float y, float t);

/* Single-precision evaluator for drawing: the double-precision phase at an origin and time is
   folded in once, so each sample is a few float cosines. Offsets are metres from the origin
   and should stay within a few hundred metres. Agrees with sea_height to ~1e-3 m there. */
typedef struct {
    int n;
    float kx[SEA_MAX_COMPONENTS], ky[SEA_MAX_COMPONENTS], ph[SEA_MAX_COMPONENTS], amp[SEA_MAX_COMPONENTS];
} SeaView;

void sea_view_init(SeaView *v, const Sea *s, double ox, double oy, float t);
float sea_view_height(const SeaView *v, float dx, float dy);

/* Forces on the hull from 7 samples along its length and 2 across (at midships), sampled
   once per frame; the caller holds the result across substeps. All zero on a flat sea.
   pitch is + bow up (rad) and heave is the mean elevation under the hull (m), both for the camera. */
WaveForces sea_hull_forces(const Sea *s, const BoatState *b, const BoatCfg *cfg, float t);

/* World form -> anchored at (x, y) at time t; heights are unchanged. */
void sea_anchor(Sea *s, double x, double y, double t);
/* Moves the anchor (phb follows k·delta, in double); heights are unchanged. */
void sea_move_anchor(Sea *s, double x, double y);
/* The world origin moved by (dx, dy) (recentre): anchor -= (dx, dy), phases untouched. */
void sea_shift_anchor(Sea *s, double dx, double dy);
/* Anchored only: phb -= omega * dt. */
void sea_advance(Sea *s, float dt);
/* Anchored only: k (as a vector), omega, amp and period lerp from a to b by w; phb untouched.
   a and b must be sea_init'ed with the same seed (component i is the same wave). */
void sea_morph(Sea *s, const Sea *a, const Sea *b, float w);
/* Back to world form as `target` (m, n, omega, amp, period, gains), with phases solved so heights
   at time t are unchanged: phase = phb - 2pi·fmod(m·xw + n·yw, WRAP)/WRAP + fmod(omega·t, 2pi). */
void sea_unanchor(Sea *s, const Sea *target, double t);
/* Dominant component (largest amp, first on a tie): unit travel direction and wavelength (m);
   false on an empty sea. Uses kx/ky when anchored. */
bool sea_dominant(const Sea *s, float *nx, float *ny, float *wl);

#endif
