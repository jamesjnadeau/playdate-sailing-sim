#ifndef PRAISE_H
#define PRAISE_H
#include <stdint.h>

#define PRAISE_COUNT 20
#define PRAISE_SECONDS 2.5f   /* s a message stays on screen after a buoy is rounded */

/* Message i, 0 <= i < PRAISE_COUNT. */
const char *praise_text(int i);
/* A random message index, never `last` (pass -1 for none). Advances *rng (xorshift32, as journey.c). */
int praise_pick(uint32_t *rng, int last);

#endif
