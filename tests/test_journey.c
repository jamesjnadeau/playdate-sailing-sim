#include "t.h"
#include <math.h>
#include "game.h"
#include "journey.h"
#include "menu.h"

static double dist(double ax, double ay, double bx, double by) { return hypot(ax - bx, ay - by); }

static float test_wind;   /* where the wind comes from, as sail_to_next reports it */

/* Rounds the next buoy: crosses its gate ray ahead of the mark, buoy on the side the rounding names. */
static void sail_to_next(Journey *j, double *px, double *py) {
    const JourneyBuoy *b = journey_next(j);
    T_ASSERT(b != NULL);
    const double s = b->rounding == ROUND_STARBOARD ? 1.0 : -1.0;
    const double cx = b->x + cos(b->gate) * 3.0, cy = b->y + sin(b->gate) * 3.0;
    const double nx = -sin(b->gate) * 3.0 * s, ny = cos(b->gate) * 3.0 * s;
    journey_update(j, cx - nx, cy - ny, cx + nx, cy + ny, 1.0f, test_wind);
    *px = cx + nx;
    *py = cy + ny;
}

static void test_init_spawns_ahead_in_range(void) {
    Journey j;
    journey_init(&j, 100, 300, 7, 0, 0, 0, 0, 0);
    T_ASSERT(j.n == JOURNEY_AHEAD && j.next == 0 && j.passed == 0);
    double lx = 0, ly = 0;
    for (int i = 0; i < j.n; i++) {
        double d = dist(j.buoy[i].x, j.buoy[i].y, lx, ly);
        T_ASSERT(d >= 99.9 && d <= 300.1);
        lx = j.buoy[i].x;
        ly = j.buoy[i].y;
    }
}

static void test_range_sanitised(void) {
    Journey j;
    journey_init(&j, 2, 1, 1, 0, 0, 0, 0, 0);
    T_ASSERT(j.min_m >= 10.0f && j.max_m >= j.min_m);
    journey_init(&j, NAN, NAN, 1, 0, 0, NAN, 0, 0);
    T_ASSERT(j.n == JOURNEY_AHEAD && isfinite(j.buoy[0].x));
}

static void test_passing_counts_and_tops_up(void) {
    Journey j;
    journey_init(&j, 100, 300, 3, 0, 0, 0, 0, 0);
    double x = 0, y = 0;
    for (int i = 1; i <= 20; i++) {
        sail_to_next(&j, &x, &y);
        T_ASSERT(j.passed == i);
        T_ASSERT(j.n - j.next >= JOURNEY_AHEAD);
        T_ASSERT(j.n <= JOURNEY_MAX);
        journey_drop_oldest(&j);
        if (j.next > 1) journey_drop_oldest(&j);
    }
    T_NEAR(j.t, 20.0, 1e-6);
}

static void test_far_move_does_not_pass(void) {
    Journey j;
    journey_init(&j, 100, 100, 5, 0, 0, 0, 0, 0);
    const JourneyBuoy *b = journey_next(&j);
    double bx = b->x, by = b->y;
    T_ASSERT(!journey_update(&j, bx + 60, by + 60, bx + 61, by + 60, 0.1f, 0));
    T_ASSERT(j.passed == 0 && j.next == 0);
}

static void test_course_keeps_moving_away_from_oldest(void) {
    for (uint32_t seed = 1; seed <= 40; seed++) {
        Journey j;
        journey_init(&j, 100, 300, seed, 0, 0, 0, 0, 0);
        double x = 0, y = 0;
        for (int i = 0; i < 60; i++) {
            sail_to_next(&j, &x, &y);
            while (j.next > 1) journey_drop_oldest(&j);   /* keep one passed buoy: the oldest */
            if (j.next == 1 && j.n >= 3) {
                const JourneyBuoy *o = &j.buoy[0];
                for (int k = 2; k < j.n; k++) {
                    double dprev = dist(j.buoy[k - 1].x, j.buoy[k - 1].y, o->x, o->y);
                    double dk = dist(j.buoy[k].x, j.buoy[k].y, o->x, o->y);
                    if (k == j.n - 1) T_ASSERT(dk > dprev - 1e-6);
                }
            }
        }
    }
}

static void test_net_progress_not_a_loop(void) {
    Journey j;
    journey_init(&j, 100, 300, 11, 0, 0, 0, 0, 0);
    double x = 0, y = 0;
    for (int i = 0; i < 80; i++) {
        sail_to_next(&j, &x, &y);
        while (j.next > 1) journey_drop_oldest(&j);
    }
    T_ASSERT(dist(x, y, 0, 0) > 2000.0);   /* 80 legs of >=100 m heading roughly one way */
}

static void test_drop_only_when_passed(void) {
    Journey j;
    journey_init(&j, 100, 300, 2, 0, 0, 0, 0, 0);
    int n = j.n;
    T_ASSERT(!journey_drop_oldest(&j) && j.n == n);
    double x, y;
    sail_to_next(&j, &x, &y);
    T_ASSERT(journey_drop_oldest(&j));
    T_ASSERT(j.next == 0 && j.passed == 1);
}

static void test_shift_moves_everything(void) {
    Journey j, k;
    journey_init(&j, 100, 300, 9, 0, 0, 0, 0, 0);
    k = j;
    journey_shift(&j, 50.0, -20.0);
    for (int i = 0; i < j.n; i++) {
        T_NEAR(j.buoy[i].x, k.buoy[i].x - 50.0, 1e-9);
        T_NEAR(j.buoy[i].y, k.buoy[i].y + 20.0, 1e-9);
    }
    T_NEAR(j.lx, k.lx - 50.0, 1e-9);
}

static void test_legs_stay_out_of_the_wind(void) {
    const float no_go = 65.0f * 3.14159265f / 180.0f;
    for (uint32_t seed = 1; seed <= 30; seed++) {
        test_wind = (float)seed * 0.7f;
        Journey j;
        /* starts pointing straight into the wind, the worst case */
        journey_init(&j, 100, 300, seed, 0, 0, test_wind, test_wind, no_go);
        double x = 0, y = 0;
        for (int i = 0; i < 40; i++) {
            for (int k = 0; k < j.n; k++)
                T_ASSERT(fabsf(wrap_pi(j.buoy[k].gate - test_wind)) >= no_go - 1e-4f);
            sail_to_next(&j, &x, &y);
            while (j.next > 1) journey_drop_oldest(&j);
        }
    }
    test_wind = 0;
}

static void test_wind_shift_applies_to_new_legs(void) {
    const float no_go = 1.0f;
    Journey j;
    journey_init(&j, 100, 300, 3, 0, 0, 0, 0, no_go);
    test_wind = 2.0f;   /* the wind swings round before the next buoy is passed */
    double x = 0, y = 0;
    for (int i = 0; i < 10; i++) {
        sail_to_next(&j, &x, &y);
        while (j.next > 1) journey_drop_oldest(&j);
    }
    T_ASSERT(fabsf(wrap_pi(j.buoy[j.n - 1].gate - 2.0f)) >= no_go - 1e-4f);
    test_wind = 0;
}

static void test_deterministic_per_seed(void) {
    Journey a, b, c;
    journey_init(&a, 100, 300, 4, 0, 0, 0, 0, 0);
    journey_init(&b, 100, 300, 4, 0, 0, 0, 0, 0);
    journey_init(&c, 100, 300, 5, 0, 0, 0, 0, 0);
    T_ASSERT(a.buoy[0].x == b.buoy[0].x && a.buoy[1].y == b.buoy[1].y);
    T_ASSERT(a.buoy[0].x != c.buoy[0].x || a.buoy[0].y != c.buoy[0].y);
}

static void test_game_mode_wiring(void) {
    BoatCfg bc;
    WeatherCfg wc;
    WorldCfg wo;
    boatcfg_default(&bc);
    weather_default(&wc);
    world_default(&wo);
    T_ASSERT(wo.journey_min_m == 60.0f && wo.journey_max_m == 150.0f);
    Game g;
    game_init(&g, &bc, &wc, &wo, 1, 1);
    T_ASSERT(g.mode == GAME_FREE);
    game_set_journey(&g);
    T_ASSERT(g.mode == GAME_JOURNEY && g.journey.n == JOURNEY_AHEAD && g.journey.passed == 0);
    game_update(&g, 0, 0, 0, 0.033f);
    T_ASSERT(g.journey.t > 0.0);
    game_restart(&g, 5);
    T_ASSERT(g.mode == GAME_JOURNEY && g.journey.t == 0.0);
}

static void test_menu_journey_flow(void) {
    Menu m;
    menu_init(&m, 2, 3);
    menu_set_weather(&m, 4, 1);
    m.sel = MENU_ITEM_JOURNEY;
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE && m.screen == MENU_BOATS);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE && m.screen == MENU_WEATHER);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_JOURNEY);
    menu_init(&m, 2, 3);
    m.sel = MENU_ITEM_JOURNEY;
    menu_update(&m, BTN_A);
    T_ASSERT(menu_update(&m, BTN_B) == MENU_NONE);
    T_ASSERT(m.screen == MENU_MAIN && m.sel == MENU_ITEM_JOURNEY && !m.journey);
}

int main(void) {
    T_RUN(test_init_spawns_ahead_in_range);
    T_RUN(test_range_sanitised);
    T_RUN(test_passing_counts_and_tops_up);
    T_RUN(test_far_move_does_not_pass);
    T_RUN(test_course_keeps_moving_away_from_oldest);
    T_RUN(test_net_progress_not_a_loop);
    T_RUN(test_drop_only_when_passed);
    T_RUN(test_shift_moves_everything);
    T_RUN(test_legs_stay_out_of_the_wind);
    T_RUN(test_wind_shift_applies_to_new_legs);
    T_RUN(test_deterministic_per_seed);
    T_RUN(test_game_mode_wiring);
    T_RUN(test_menu_journey_flow);
    return T_DONE();
}
