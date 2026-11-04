#ifndef TRAINING_H
#define TRAINING_H
#include <stdbool.h>
#include "game.h"

/* The sailing lesson: a fixed list of steps, each advanced only when the player has done it. The goals are read from
   the Game state, so the lesson never steers the boat. It writes no words; the text lives in config/training.md. */
typedef enum {
    TRAIN_DOWNWIND_AUTO, TRAIN_BEAM_AUTO, TRAIN_POINTS_AUTO,
    TRAIN_CLOSE_HAULED, TRAIN_IRONS, TRAIN_ENGINE_FREE,
    TRAIN_DOWNWIND_MANUAL, TRAIN_BEAM_MANUAL, TRAIN_POINTS_MANUAL,
    TRAIN_DONE, TRAIN_STEPS
} TrainStep;

typedef struct {
    TrainStep step;
    float held;       /* s the current goal has held without a break */
    unsigned seen;    /* points-of-sail steps: bit per point already held long enough */
} Training;

#define TRAIN_HOLD_S 2.0f   /* a goal must hold this long, so sailing through an angle does not count */

void training_init(Training *t);
/* One sailing frame. Advances at most one step per call. Does nothing unless g->run == RUN_SAILING. */
void training_update(Training *t, const Game *g, float dt);
/* Whether the lesson wants the engine kept off this frame (every step before TRAIN_ENGINE_FREE). */
bool training_engine_locked(const Training *t);
/* |angle| from the wind's source to the boat's heading, rad, 0..pi; pi is straight downwind. */
float training_off_wind(const Game *g);

#endif
