#include "praise.h"

static const char *const TEXT[PRAISE_COUNT] = {
    "Bravo Zulu",
    "First rate",
    "Shipshape",
    "Bristol Fashion",
    "Squared Away",
    "Running a Tight Ship",
    "Fair Winds Friend",
    "Following Seas Friend",
    "Even Keel",
    "Crack Crew",
    "Fine Company",
    "All hands to the good",
    "Pulled together",
    "Heave and rally",
    "Prime lasses and lads",
    "Good show",
    "Well steered",
    "Sweet sailing",
    "Nicely judged",
    "Smart work",
};

const char *praise_text(int i) {
    return TEXT[i < 0 || i >= PRAISE_COUNT ? 0 : i];
}

int praise_pick(uint32_t *rng, int last) {
    uint32_t x = *rng ? *rng : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    int i = (int)(x % PRAISE_COUNT);
    return i == last ? (i + 1) % PRAISE_COUNT : i;
}
