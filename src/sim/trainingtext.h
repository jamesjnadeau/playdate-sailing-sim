#ifndef TRAININGTEXT_H
#define TRAININGTEXT_H
#include "training.h"

#define TRAINTEXT_LEN 320   /* bytes per step including the NUL; longer text is cut */
#define TRAINTEXT_HINT_LEN 120   /* bytes per hint including the NUL */
#define TRAINTEXT_DEFAULT_S 4.0f   /* seconds per line when the file does not say */
#define TRAINTEXT_HINT_AFTER_S 15.0f   /* seconds off-task before the hint shows, when the file does not say */

/* text[step] holds that step's lines separated by '\n'; seconds[step] is how long each is shown (0 = default_s).
   goal[step] is what the step asks for (the built-in goal unless the file sets one); hint[step] is the nudge shown
   once the player has been off-task for hint_after[step] seconds (0 = default_hint_after), empty for none. */
typedef struct {
    char text[TRAIN_STEPS][TRAINTEXT_LEN];
    float seconds[TRAIN_STEPS];
    float default_s;
    TrainGoal goal[TRAIN_STEPS];
    char hint[TRAIN_STEPS][TRAINTEXT_HINT_LEN];
    float hint_after[TRAIN_STEPS];
    float default_hint_after;
    int bad_line;   /* first line the parser did not understand, 1-based; 0 when every line was fine */
} TrainingText;

/* Reads config/training.md: one "# <step>" heading per step, where <step> is the TrainStep name without TRAIN_,
   lowercase (downwind_auto, beam_auto, ..., done). Each plain line under a heading is one page of lesson text.
   Setting lines, each "key: value":
     seconds: N      how long each page shows (for the whole file when it comes before any heading, else that step)
     hint_after: N   seconds off-task before the hint shows (whole file before any heading, else that step)
     hint: <text>    what to tell a player who is not doing the step
     require: <terms, comma-separated>   the step's goal; any require or visit line replaces the built-in goal
         autotrim on|off   autopilot on|off   engine on|off   mode helm|trim|hoist|camera
         wind LO..HI (degrees off the wind's source, 0 head to wind, 180 downwind; either end may be left out)
         speed LO..HI (forward m/s; either end may be left out)
     visit: LO..HI   a wind range the heading must also hold once (up to TRAIN_VISITS; all of them, in any order)
     hold: N         seconds the goal must hold (default TRAIN_HOLD_S)
   Unknown headings are ignored; NULL is an empty file. Returns how many steps got text. */
int trainingtext_parse(const char *md, TrainingText *out);
/* The page of a step's text to show elapsed seconds into the step: each line in turn for its time, then the last
   one stays up. Copied into out (cap bytes); a step with no text gets the notice. Returns the page number. */
int trainingtext_page(const TrainingText *t, TrainStep s, float elapsed, char *out, int cap);
/* The step's hint when it is due, else NULL: the step has a hint, its last page has been up for hint_after, and
   the goal has gone unmet (miss, see Training) for hint_after too. */
const char *trainingtext_hint(const TrainingText *t, TrainStep s, float elapsed, float miss);
/* The text for a step, or a notice that the file lacks it (also for an out-of-range step). Never NULL. */
const char *trainingtext_get(const TrainingText *t, TrainStep s);

#endif
