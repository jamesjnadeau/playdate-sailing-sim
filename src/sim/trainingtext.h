#ifndef TRAININGTEXT_H
#define TRAININGTEXT_H
#include "training.h"

#define TRAINTEXT_LEN 160   /* bytes per step including the NUL; longer text is cut */

typedef struct { char text[TRAIN_STEPS][TRAINTEXT_LEN]; } TrainingText;

/* Reads config/training.md: one "# <step>" heading per step, where <step> is the TrainStep name without TRAIN_,
   lowercase (downwind_auto, beam_auto, ..., done). The lines under a heading are joined with spaces. Unknown
   headings are ignored; NULL is an empty file. Returns how many steps got text. */
int trainingtext_parse(const char *md, TrainingText *out);
/* The text for a step, or a notice that the file lacks it (also for an out-of-range step). Never NULL. */
const char *trainingtext_get(const TrainingText *t, TrainStep s);

#endif
