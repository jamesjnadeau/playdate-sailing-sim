#ifndef GHOST_H
#define GHOST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game.h"

/* A ghost is the boat's track through one race, sampled at GHOST_HZ of race time, to be replayed beside the
   next attempt. Samples are kept in the track frame so the floating origin never touches them. */

#define GHOST_HZ 10
#define GHOST_MAX_SECONDS 1200                         /* longer runs are not kept */
#define GHOST_MAX_SAMPLES (GHOST_HZ * GHOST_MAX_SECONDS + 2)   /* + the start sample and the finish tail */
#define GHOST_SAILS 4                                  /* sails beyond these are not drawn on the ghost */
#define GHOST_SAMPLE_BYTES 24
#define GHOST_HEADER_BYTES 40
#define GHOST_FILE_MAX (GHOST_HEADER_BYTES + GHOST_SAMPLE_BYTES * GHOST_MAX_SAMPLES)

/* Quantised, as stored: 24 bytes a sample. */
typedef struct {
    float x, y;                  /* m, track frame */
    int16_t heading, heel;       /* rad * 10000 */
    uint16_t progress;           /* course progress * 4000, see ghost_progress */
    int8_t wheel;                /* -1..1 as -127..127 */
    uint8_t swing;               /* bit i set: sail i's boom is on the starboard side */
    int8_t trim[GHOST_SAILS];    /* rad * 80 */
    uint8_t hoist[GHOST_SAILS];  /* 0..1 as 0..255 */
} GhostSample;

/* One sample, decoded. */
typedef struct {
    double x, y;                 /* m, track frame */
    float heading, heel, wheel;
    float progress;
    float speed;                 /* m/s along the track; filled by ghost_pose_at only */
    float trim[GHOST_SAILS], hoist[GHOST_SAILS], swing[GHOST_SAILS];   /* swing is -1 or +1 */
} GhostPose;

typedef struct {
    GhostSample *s;              /* caller-owned storage for `cap` samples */
    int cap, n;
    uint32_t seed, track_hash, boat_hash;
    int preset;
    double time;                 /* finish time, s; set by ghost_finish or a decode */
    bool tail;                   /* last sample is at `time`, off the 1/GHOST_HZ grid */
    bool overflow;               /* the run outlasted the storage: not a usable ghost */
    bool done;                   /* finished recording, or decoded */
    GhostPose prev;              /* previous frame's pose, while recording */
    double prev_t;
    int cursor;                  /* ghost_time_at_progress remembers where it was */
} Ghost;

void ghost_init(Ghost *gh, GhostSample *storage, int cap);
/* Forgets everything (the storage stays). */
void ghost_clear(Ghost *gh);

/* Identity: a ghost only races under the conditions it was sailed in. */
uint32_t ghost_hash_str(const char *s);
uint32_t ghost_track_hash(const Track *t);
/* The weather seed of every attempt on a course, so the wind and sea repeat. Never 0. */
uint32_t ghost_seed(const char *course);
/* ghost_<course>_<boat>_<weather>.bin, each name cut down to letters and digits: one ghost per boat, course and
   weather. Always NUL-terminated; truncates rather than overflowing. */
void ghost_file_name(char *out, size_t n, const char *course, const char *boat, const char *weather);

/* Course progress: the next mark's index plus how far along the leg into it, 0..nmarks. */
float ghost_progress(const Game *g);

/* Starts recording at the start of a race (race time 0). */
void ghost_begin(Ghost *gh, const Game *g, uint32_t track_hash, uint32_t boat_hash);
/* Call after every game_update. Ignored once the race is finished or the ghost is not recording. */
void ghost_record(Ghost *gh, const Game *g);
/* Ends the recording at the finish. True when it is a usable ghost (not overflowed, at least two samples). */
bool ghost_finish(Ghost *gh, const Game *g);

bool ghost_usable(const Ghost *gh);
bool ghost_matches(const Ghost *gh, uint32_t seed, uint32_t track_hash, uint32_t boat_hash, int preset);

/* Where the ghost is at race time t (clamped to its ends). False when empty. */
bool ghost_pose_at(const Ghost *gh, double t, GhostPose *out);
/* The race time at which the ghost had reached course progress p, interpolated. gap = race time - that, so
   positive is behind. False when empty. */
bool ghost_time_at_progress(Ghost *gh, float p, double *t);

/* File image: header, then samples, little-endian. encode returns the bytes written, 0 if `n` is too small or
   the ghost is not usable; decode fills `gh` (which must have storage) and rejects anything malformed. */
size_t ghost_encoded_size(const Ghost *gh);
size_t ghost_encode(const Ghost *gh, uint8_t *out, size_t n);
bool ghost_decode(Ghost *gh, const uint8_t *in, size_t n);
/* Reads just the finish time from the start of a file image (at least GHOST_HEADER_BYTES), for menus that
   list which boats have a ghost. False when it is not a ghost file. */
bool ghost_peek_time(const uint8_t *in, size_t n, double *time);

#endif
