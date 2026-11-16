#ifndef TRAINING_H
#define TRAINING_H
#include <stdbool.h>
#include "game.h"

/* The sailing lesson: a fixed list of steps, each advanced only when the player has done it. The goals are read from
   the Game state, so the lesson never steers the boat. It writes no words; the text lives in config/training.md,
   which can also set each step's goal (see trainingtext.h). */
typedef enum {
    TRAIN_DOWNWIND_AUTO, TRAIN_BEAM_AUTO, TRAIN_POINTS_AUTO,
    TRAIN_CLOSE_HAULED, TRAIN_IRONS, TRAIN_ENGINE_FREE,
    TRAIN_DOWNWIND_MANUAL, TRAIN_BEAM_MANUAL, TRAIN_POINTS_MANUAL,
    TRAIN_DONE, TRAIN_STEPS
} TrainStep;

#define TRAIN_HOLD_S 2.0f   /* a goal must hold this long, so sailing through an angle does not count */
#define TRAIN_VISITS 4      /* most wind ranges one step can ask to visit */

/* What a step asks for. A switch is -1 (either), 0 (off) or 1 (on); mode is -1 or a Mode (controls.h). Wind is
   degrees off the wind's source (0 head to wind, 180 dead downwind); speed is forward m/s. The step is done once
   everything holds for hold_s, and, with visits, once the heading has held inside each visit range in turn. */
typedef struct {
    signed char autotrim, autopilot, engine, mode;
    float wind_lo, wind_hi;
    float speed_lo, speed_hi;
    int nvisit;
    float visit_lo[TRAIN_VISITS], visit_hi[TRAIN_VISITS];
    float hold_s;
    bool never;              /* a goal that is never met (the last step) */
} TrainGoal;

typedef struct {
    TrainStep step;
    float held;       /* s the current goal has held without a break */
    unsigned seen;    /* visit steps: bit per visit range already held long enough */
    int visiting;     /* visit range being held now, -1 none */
    float miss;       /* s the current goal has gone unmet without a break; 0 while it is met */
    const TrainGoal *goals;   /* TRAIN_STEPS goals, or NULL for the built-in ones */
} Training;

void training_init(Training *t);
/* The goal a step has when config/training.md does not set one. */
void training_default_goal(TrainStep s, TrainGoal *out);
/* A goal that asks for nothing: every switch either way, any wind, any speed, TRAIN_HOLD_S. */
void training_goal_any(TrainGoal *out);
/* The goal in force for the current step. */
const TrainGoal *training_goal(const Training *t);
/* Whether the current step's goal is met this frame (visits: whether a range still to visit is being held). */
bool training_goal_met(const Training *t, const Game *g);
/* One sailing frame. Advances at most one step per call. Does nothing unless g->run == RUN_SAILING. */
void training_update(Training *t, const Game *g, float dt);
/* Whether the lesson wants the engine kept off this frame (every step before TRAIN_ENGINE_FREE). */
bool training_engine_locked(const Training *t);
/* |angle| from the wind's source to the boat's heading, rad, 0..pi; pi is straight downwind. */
float training_off_wind(const Game *g);

#endif
