#ifndef TRAININGTEXT_H
#define TRAININGTEXT_H
#include "training.h"

#define TRAINTEXT_LEN 320   /* bytes per step including the NUL; longer text is cut */
#define TRAINTEXT_DEFAULT_S 5.0f   /* seconds per line when the file does not say */

/* text[step] holds that step's lines separated by '\n'; seconds[step] is how long each is shown (0 = default_s). */
typedef struct {
    char text[TRAIN_STEPS][TRAINTEXT_LEN];
    float seconds[TRAIN_STEPS];
    float default_s;
} TrainingText;

/* Reads config/training.md: one "# <step>" heading per step, where <step> is the TrainStep name without TRAIN_,
   lowercase (downwind_auto, beam_auto, ..., done). Each line under a heading is one page of lesson text. A line "seconds: N" sets
   how long each page shows, for the whole file when it comes before any heading, else for that step. Unknown
   headings are ignored; NULL is an empty file. Returns how many steps got text. */
int trainingtext_parse(const char *md, TrainingText *out);
/* The page of a step's text to show elapsed seconds into the step: each line in turn for its time, then the last
   one stays up. Copied into out (cap bytes); a step with no text gets the notice. Returns the page number. */
int trainingtext_page(const TrainingText *t, TrainStep s, float elapsed, char *out, int cap);
/* The text for a step, or a notice that the file lacks it (also for an out-of-range step). Never NULL. */
const char *trainingtext_get(const TrainingText *t, TrainStep s);

#endif
