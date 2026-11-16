#ifndef IRONS_H
#define IRONS_H
#include <stdbool.h>

/* "In irons": the bow points into the wind's no-go zone and the boat has all but stopped, so the sails
 * flog and the rudder has no water to bite. Entering takes IRONS_HOLD_S of those conditions, so a tack
 * swinging through the wind never trips it; leaving needs the bow clearly off the wind or real way on. */
#define IRONS_HOLD_S 2.0f
#define IRONS_SPEED_IN 0.5f       /* m/s forward (u): below this counts as stopped (backing down too) */
#define IRONS_SPEED_OUT 1.0f      /* m/s forward: above this the boat is sailing again */
#define IRONS_MARGIN_OUT 0.1745f  /* rad (10 deg) past the no-go edge before the bow counts as off the wind */

typedef struct {
    bool on;       /* the boat is in irons */
    float held;    /* s the entry conditions have held while not yet in irons */
} Irons;

void irons_init(Irons *s);

/* Once per frame. off_wind: rad between the bow and where the wind comes from (0 head to wind .. pi);
 * no_go: rad, the boat's no-go half-angle; u: m/s forward; sailing: the player is sailing (not capsized,
 * engine off, not anchored, some sail up). Not sailing, or a non-finite input, clears the state.
 * Returns s->on. */
bool irons_update(Irons *s, float off_wind, float no_go, float u, bool sailing, float dt);

/* Banner text while in irons; NULL otherwise. */
const char *irons_text(bool on);

#endif
