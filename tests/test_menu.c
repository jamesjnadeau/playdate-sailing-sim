#include "t.h"
#include "menu.h"
#include "controls.h"

static void test_starts_on_main_first_row(void) {
    Menu m;
    menu_init(&m, 1, 3);
    T_ASSERT(m.screen == MENU_MAIN);
    T_ASSERT(m.sel == 0);
}

static void test_down_up_wrap(void) {
    Menu m;
    menu_init(&m, 1, 3);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == 1);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == 0);
    menu_update(&m, BTN_UP);
    T_ASSERT(m.sel == MENU_ITEM_HELP);
}

static void test_help_opens_scrolls_and_returns(void) {
    Menu m;
    menu_init(&m, 1, 3);
    menu_set_help(&m, MENU_HELP_VIS + 2);
    menu_update(&m, BTN_UP);                          /* wraps to the last row: controls */
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_HELP);
    for (int i = 0; i < 5; i++) menu_update(&m, BTN_DOWN);
    T_ASSERT(m.help_top == 2);                        /* stops at the last full page */
    menu_update(&m, BTN_UP);
    T_ASSERT(m.help_top == 1);
    menu_update(&m, BTN_B);
    T_ASSERT(m.screen == MENU_MAIN);
    T_ASSERT(m.sel == MENU_ITEM_HELP);
}

static void test_help_scrolls_with_crank(void) {
    Menu m;
    menu_init(&m, 3, 2);
    menu_set_help(&m, MENU_HELP_VIS + 3);
    menu_crank(&m, 90);
    T_ASSERT(m.help_top == 0);                        /* only on the help screen */
    menu_update(&m, BTN_UP);
    menu_update(&m, BTN_A);
    menu_crank(&m, MENU_HELP_CRANK_DEG * 2);
    T_ASSERT(m.help_top == 2);
    menu_crank(&m, MENU_HELP_CRANK_DEG * 10);
    T_ASSERT(m.help_top == 3);                        /* stops at the last full page */
    menu_crank(&m, -MENU_HELP_CRANK_DEG * 20);
    T_ASSERT(m.help_top == 0);
}

static void test_short_help_does_not_scroll(void) {
    Menu m;
    menu_init(&m, 1, 3);
    menu_set_help(&m, 3);
    m.sel = MENU_ITEM_HELP;
    menu_update(&m, BTN_A);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.help_top == 0);
}

static void test_a_on_free_starts_free(void) {
    Menu m;
    menu_init(&m, 1, 3);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_FREE);
}

static void test_racing_opens_track_list(void) {
    Menu m;
    menu_init(&m, 1, 3);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_TRACKS);
    T_ASSERT(m.sel == 0);
}

static void test_track_pick_starts_race(void) {
    Menu m;
    menu_init(&m, 3, 3);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_A);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);   /* the track is picked; the preview confirms it */
    T_ASSERT(m.screen == MENU_PREVIEW);
    T_ASSERT(m.track == 2);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);   /* confirmed; the boat comes next */
    T_ASSERT(m.screen == MENU_BOATS);
}

static void test_boat_pick_starts_race(void) {
    Menu m;
    menu_init(&m, 3, 4);
    m.boat = 1;
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_A);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_A);                          /* track 1 */
    T_ASSERT(m.screen == MENU_PREVIEW);
    menu_update(&m, BTN_A);
    T_ASSERT(m.screen == MENU_BOATS);
    T_ASSERT(m.sel == 1);                            /* opens on the boat used last */
    menu_update(&m, BTN_DOWN);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_RACE);
    T_ASSERT(m.track == 1);
    T_ASSERT(m.boat == 2);
}

static void test_b_on_boats_returns_to_track_list(void) {
    Menu m;
    menu_init(&m, 3, 4);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_A);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_A);                          /* track 2 -> preview -> boats */
    menu_update(&m, BTN_A);
    T_ASSERT(m.screen == MENU_BOATS);
    T_ASSERT(menu_update(&m, BTN_B) == MENU_NONE);
    T_ASSERT(m.screen == MENU_PREVIEW);
    T_ASSERT(menu_update(&m, BTN_B) == MENU_NONE);
    T_ASSERT(m.screen == MENU_TRACKS);
    T_ASSERT(m.sel == 2);
}

static void test_no_boats_starts_race_after_track(void) {
    Menu m;
    menu_init(&m, 2, 0);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_A);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);   /* the preview */
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_RACE);
}

static void test_preview_animation_ticks_and_restarts(void) {
    Menu m;
    menu_init(&m, 2, 3);
    menu_update(&m, BTN_DOWN);
    menu_update(&m, BTN_A);
    menu_update(&m, BTN_A);                          /* tracks -> preview */
    T_ASSERT(m.screen == MENU_PREVIEW && m.tick == 0);
    menu_update(&m, 0);
    menu_update(&m, BTN_UP);                         /* up/down do nothing on the preview */
    T_ASSERT(m.tick == 2 && m.screen == MENU_PREVIEW && m.track == 0);
    menu_update(&m, BTN_B);
    menu_update(&m, BTN_A);                          /* back on the list, pick again */
    T_ASSERT(m.screen == MENU_PREVIEW && m.tick == 0);
}

static void test_b_goes_back_to_racing_row(void) {
    Menu m;
    menu_init(&m, 2, 3);
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
    menu_init(&m, 0, 3);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_MAIN);
}

/* One frame, several buttons: B first, then UP/DOWN, then A. */
static void test_multiple_buttons_one_frame(void) {
    Menu m;
    menu_init(&m, 2, 3);
    T_ASSERT(menu_update(&m, BTN_DOWN | BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_TRACKS);
    T_ASSERT(m.sel == 0);
}

int main(void) {
    T_RUN(test_starts_on_main_first_row);
    T_RUN(test_down_up_wrap);
    T_RUN(test_help_opens_scrolls_and_returns);
    T_RUN(test_help_scrolls_with_crank);
    T_RUN(test_short_help_does_not_scroll);
    T_RUN(test_a_on_free_starts_free);
    T_RUN(test_racing_opens_track_list);
    T_RUN(test_track_pick_starts_race);
    T_RUN(test_boat_pick_starts_race);
    T_RUN(test_b_on_boats_returns_to_track_list);
    T_RUN(test_no_boats_starts_race_after_track);
    T_RUN(test_preview_animation_ticks_and_restarts);
    T_RUN(test_b_goes_back_to_racing_row);
    T_RUN(test_no_tracks_racing_stays);
    T_RUN(test_multiple_buttons_one_frame);
    return T_DONE();
}
