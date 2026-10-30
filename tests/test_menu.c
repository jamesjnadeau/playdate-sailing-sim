#include "t.h"
#include "menu.h"
#include "controls.h"

static void test_starts_on_main_first_row(void) {
    Menu m;
    menu_init(&m, 1);
    T_ASSERT(m.screen == MENU_MAIN);
    T_ASSERT(m.sel == 0);
}

static void test_down_up_wrap(void) {
    Menu m;
    menu_init(&m, 1);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == 1);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == 0);
    menu_update(&m, BTN_UP);
    T_ASSERT(m.sel == 1);
}

static void test_a_on_free_starts_free(void) {
    Menu m;
    menu_init(&m, 1);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_FREE);
}

static void test_racing_opens_track_list(void) {
    Menu m;
    menu_init(&m, 1);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_TRACKS);
    T_ASSERT(m.sel == 0);
}

static void test_track_pick_starts_race(void) {
    Menu m;
    menu_init(&m, 3);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_A);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_RACE);
    T_ASSERT(m.track == 2);
}

static void test_b_goes_back_to_racing_row(void) {
    Menu m;
    menu_init(&m, 2);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_A);
    T_ASSERT(menu_update(&m, BTN_B) == MENU_NONE);
    T_ASSERT(m.screen == MENU_MAIN);
    T_ASSERT(m.sel == MENU_ITEM_RACE);
    T_ASSERT(menu_update(&m, BTN_B) == MENU_NONE);
    T_ASSERT(m.screen == MENU_MAIN);
}

static void test_no_tracks_racing_stays(void) {
    Menu m;
    menu_init(&m, 0);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_MAIN);
}

/* One frame, several buttons: B first, then UP/DOWN, then A. */
static void test_multiple_buttons_one_frame(void) {
    Menu m;
    menu_init(&m, 2);
    T_ASSERT(menu_update(&m, BTN_DOWN | BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_TRACKS);
    T_ASSERT(m.sel == 0);
}

int main(void) {
    T_RUN(test_starts_on_main_first_row);
    T_RUN(test_down_up_wrap);
    T_RUN(test_a_on_free_starts_free);
    T_RUN(test_racing_opens_track_list);
    T_RUN(test_track_pick_starts_race);
    T_RUN(test_b_goes_back_to_racing_row);
    T_RUN(test_no_tracks_racing_stays);
    T_RUN(test_multiple_buttons_one_frame);
    return T_DONE();
}
