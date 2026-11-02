#ifndef WEATHER_H
#define WEATHER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mathx.h"

/* Lightest wind first, so the menus read in order of wind speed. */
enum { WEATHER_MIST, WEATHER_SUNNY, WEATHER_CLOUDY, WEATHER_CHOPPY, WEATHER_ROLLERS, WEATHER_STORM, WEATHER_HURRICANE, WEATHER_PRESETS };

extern const char *const WEATHER_NAMES[WEATHER_PRESETS];

typedef struct {
    char name[16];
    float wind_base;        /* m/s mean */
    float wind_var;         /* m/s slow-noise amplitude */
    float shift_rate_deg;   /* hard cap on direction change, deg/s */
    float gust_speed;       /* m/s peak gust added on top */
    float gust_interval;    /* mean seconds between gusts */
    float wave_height;      /* m, crest to trough of the main swell */
    float wave_period;      /* s */
    float wave_steepness;   /* 0..1 */
    float visibility;       /* m, fog distance */
    /* sky; shades are dither levels, 0 = white .. 15 = near black */
    float sky_top;          /* shade at the top of the screen */
    float sky_horizon;      /* shade at the horizon; the sea's distance haze fades to it */
    float cloud_cover;      /* 0..1, share of the cloud slots in use */
    float cloud_shade;      /* cloud dither level */
    float cloud_size;       /* cloud width multiplier */
    float cloud_speed;      /* cloud drift multiplier */
    float rain;             /* 0..1 density of rain streaks */
    float lightning_interval; /* mean s between lightning flashes, 0 = none */
    float sun;              /* 1 draws the sun, 0 hides it */
} WeatherPreset;

typedef struct { WeatherPreset preset[WEATHER_PRESETS]; } WeatherCfg;

void weather_default(WeatherCfg *out);
/* Always fills out (from the default). False if any warning was raised. */
bool weather_load(WeatherCfg *out, const char *buf, size_t len, char *warn, size_t warnlen);

/* dir = world angle (rad) the wind blows TOWARD; speed in m/s. */
typedef struct {
    float dir, speed;
    float base;             /* m/s mean; starts as the preset's wind_base */
    double t;
    float dir0, phase[6];
    uint32_t seed, gust_n;
    double next_gust, gust_t;
    float gust_len, gust_amp;
} Wind;

void wind_init(Wind *w, const WeatherPreset *p, uint32_t seed);
void wind_step(Wind *w, const WeatherPreset *p, float dt);   /* dt clamped to [0, 1] */
void wind_vec(const Wind *w, float *wx, float *wy);          /* air velocity, world frame */
/* Sets the base direction (rad, blowing toward); the current dir keeps its offset from it. */
void wind_set_base(Wind *w, float dir0);
/* Overrides the mean speed (m/s); gusts and slow noise still come from the preset. Re-evaluates speed. */
void wind_set_speed(Wind *w, const WeatherPreset *p, float mps);

#endif
