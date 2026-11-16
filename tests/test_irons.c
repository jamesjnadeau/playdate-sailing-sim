#include "t.h"
#include <math.h>
#include <string.h>
#include "irons.h"
#include "mathx.h"

static const float NOGO = 0.7854f;   /* 45 deg */

/* run `secs` of frames at 30 fps */
static bool run(Irons *s, float off, float u, bool sailing, float secs) {
    bool on = s->on;
    for (int i = 0; i < (int)(secs * 30.0f + 0.5f); i++) on = irons_update(s, off, NOGO, u, sailing, 1.0f / 30.0f);
    return on;
}

static void test_head_to_wind_and_stopped_trips_after_hold(void) {
    Irons s;
    irons_init(&s);
    T_ASSERT(!run(&s, deg2rad(10.0f), 0.1f, true, 1.5f));   /* not yet */
    T_ASSERT(run(&s, deg2rad(10.0f), 0.1f, true, 0.6f));
    T_ASSERT(strstr(irons_text(s.on), "IN IRONS") != NULL);
}

static void test_tack_through_the_wind_does_not_trip(void) {
    Irons s;
    irons_init(&s);
    T_ASSERT(!run(&s, deg2rad(5.0f), 0.3f, true, 1.5f));     /* passing through the eye */
    T_ASSERT(!run(&s, deg2rad(60.0f), 0.3f, true, 0.1f));    /* out the other side resets */
    T_ASSERT(!run(&s, deg2rad(5.0f), 0.3f, true, 1.5f));
}

static void test_moving_boat_upwind_is_not_in_irons(void) {
    Irons s;
    irons_init(&s);
    T_ASSERT(!run(&s, deg2rad(40.0f), 2.0f, true, 5.0f));    /* pinching but moving */
    T_ASSERT(!run(&s, deg2rad(90.0f), 0.0f, true, 5.0f));    /* stopped on a reach: not irons */
}

static void test_backing_down_counts(void) {
    Irons s;
    irons_init(&s);
    T_ASSERT(run(&s, 0.0f, -0.8f, true, 2.5f));
}

static void test_leaves_with_hysteresis(void) {
    Irons s;
    irons_init(&s);
    T_ASSERT(run(&s, deg2rad(20.0f), 0.0f, true, 3.0f));
    T_ASSERT(run(&s, deg2rad(50.0f), 0.0f, true, 1.0f));     /* just past the edge: still stuck */
    T_ASSERT(run(&s, deg2rad(20.0f), 0.7f, true, 1.0f));     /* gathering a little way: still stuck */
    T_ASSERT(!run(&s, deg2rad(60.0f), 0.0f, true, 0.1f));    /* bow well off the wind */
    T_ASSERT(irons_text(s.on) == NULL);
    T_ASSERT(run(&s, deg2rad(20.0f), 0.0f, true, 3.0f));
    T_ASSERT(!run(&s, deg2rad(20.0f), 1.2f, true, 0.1f));    /* sailing again */
}

static void test_not_sailing_or_bad_input_clears(void) {
    Irons s;
    irons_init(&s);
    T_ASSERT(run(&s, 0.0f, 0.0f, true, 3.0f));
    T_ASSERT(!run(&s, 0.0f, 0.0f, false, 0.1f));            /* engine on, capsized, anchored, sails down */
    T_ASSERT(!run(&s, 0.0f, 0.0f, false, 5.0f));
    T_ASSERT(run(&s, 0.0f, 0.0f, true, 3.0f));
    T_ASSERT(!irons_update(&s, NAN, NOGO, 0.0f, true, 0.1f));
    T_ASSERT(!irons_update(&s, 0.0f, NOGO, INFINITY, true, 0.1f));
    T_ASSERT(!run(&s, 0.0f, 0.0f, true, 1.0f));
    T_ASSERT(!irons_update(&s, 0.0f, NOGO, 0.0f, true, NAN));   /* a bad dt adds nothing */
}

int main(void) {
    T_RUN(test_head_to_wind_and_stopped_trips_after_hold);
    T_RUN(test_tack_through_the_wind_does_not_trip);
    T_RUN(test_moving_boat_upwind_is_not_in_irons);
    T_RUN(test_backing_down_counts);
    T_RUN(test_leaves_with_hysteresis);
    T_RUN(test_not_sailing_or_bad_input_clears);
    return T_DONE();
}
