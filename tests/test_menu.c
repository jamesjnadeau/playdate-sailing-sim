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
    for (int i = 2; i < MENU_MAIN_ITEMS; i++) menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == MENU_MAIN_ITEMS - 1);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == 0);
    menu_update(&m, BTN_UP);
    T_ASSERT(m.sel == MENU_MAIN_ITEMS - 1);
}

static void test_help_opens_scrolls_and_returns(void) {
    Menu m;
    menu_init(&m, 1, 3);
    menu_set_help(&m, MENU_HELP_VIS + 2);
    m.sel = MENU_ITEM_HELP;
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

static void test_sound_row_cycles_levels(void) {
    Menu m;
    menu_init(&m, 1, 3);
    T_ASSERT(m.sound == 2);
    m.sel = MENU_ITEM_SOUND;
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_MAIN);
    T_ASSERT(m.sound == 3);
    menu_update(&m, BTN_A);
    T_ASSERT(m.sound == 0);                           /* wraps round to Off */
    menu_set_sound(&m, 1);
    T_ASSERT(m.sound == 1);
    menu_set_sound(&m, 9);
    T_ASSERT(m.sound == 2);                           /* out of range -> default */
}

static void test_help_scrolls_with_crank(void) {
    Menu m;
    menu_init(&m, 3, 2);
    menu_set_help(&m, MENU_HELP_VIS + 3);
    menu_crank(&m, 90);
    T_ASSERT(m.help_top == 0);                        /* only on the help screen */
    m.sel = MENU_ITEM_HELP;
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
    m.sel = MENU_ITEM_RACE;
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_TRACKS);
    T_ASSERT(m.sel == 0);
}

static void test_track_pick_starts_race(void) {
    Menu m;
    menu_init(&m, 3, 3);
    m.sel = MENU_ITEM_RACE;
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
    m.sel = MENU_ITEM_RACE;
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
    m.sel = MENU_ITEM_RACE;
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

static void test_weather_follows_boat(void) {
    Menu m;
    menu_init(&m, 2, 4);
    menu_set_weather(&m, 6, 5);
    m.sel = MENU_ITEM_RACE;
    menu_update(&m, BTN_A);                          /* Racing */
    menu_update(&m, BTN_A);                          /* track 0 */
    menu_update(&m, BTN_A);                          /* preview confirmed */
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);   /* boat 0 picked; weather next */
    T_ASSERT(m.screen == MENU_WEATHER);
    T_ASSERT(m.sel == 5);                            /* opens on the current weather */
    menu_update(&m, BTN_UP);
    T_ASSERT(menu_update(&m, BTN_B) == MENU_NONE);
    T_ASSERT(m.screen == MENU_BOATS);
    menu_update(&m, BTN_A);
    menu_update(&m, BTN_UP);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_RACE);
    T_ASSERT(m.weather == 4);
}

static void test_no_boats_starts_race_after_track(void) {
    Menu m;
    menu_init(&m, 2, 0);
    m.sel = MENU_ITEM_RACE;
    menu_update(&m, BTN_A);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);   /* the preview */
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_RACE);
}

static void test_preview_animation_ticks_and_restarts(void) {
    Menu m;
    menu_init(&m, 2, 3);
    m.sel = MENU_ITEM_RACE;
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
    m.sel = MENU_ITEM_RACE;
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
    m.sel = MENU_ITEM_RACE;
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_MAIN);
}

/* One frame, several buttons: B first, then UP/DOWN, then A. */
static void test_multiple_buttons_one_frame(void) {
    Menu m;
    menu_init(&m, 2, 3);
    m.sel = MENU_ITEM_JOURNEY;
    T_ASSERT(menu_update(&m, BTN_DOWN | BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_TRACKS);
    T_ASSERT(m.sel == 0);
}

/* Free range, Racing, then the track: the boat screen opens after the track's preview. */
static void enter_boats(Menu *m) {
    m->sel = MENU_ITEM_RACE;
    menu_update(m, BTN_A);
    menu_update(m, BTN_A);
    menu_update(m, BTN_A);
}

/* Cruisers (raceable 0) are skipped by UP and DOWN on the boat screen. */
static void test_boat_screen_skips_cruisers(void) {
    static const bool raceable[5] = { true, true, true, true, false };
    Menu m;
    menu_init(&m, 3, 5);
    menu_set_boat_racing(&m, raceable);
    m.boat = 3;
    enter_boats(&m);
    T_ASSERT(m.screen == MENU_BOATS);
    T_ASSERT(m.sel == 3);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == 0);
    menu_update(&m, BTN_UP);
    T_ASSERT(m.sel == 3);
}

/* A cruiser as the current boat: the screen opens on the first racer, and that is what races. */
static void test_boat_screen_opens_on_a_racer_when_current_boat_is_a_cruiser(void) {
    static const bool raceable[5] = { true, true, true, true, false };
    Menu m;
    menu_init(&m, 3, 5);
    menu_set_boat_racing(&m, raceable);
    menu_set_weather(&m, 2, 0);
    m.boat = 4;
    enter_boats(&m);
    T_ASSERT(m.sel == 0);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_WEATHER);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_RACE);
    T_ASSERT(m.boat == 0);
}

/* B from the weather screen returns to the boat row that was picked, a racer. */
static void test_boat_screen_b_returns_to_a_racer_row(void) {
    static const bool raceable[5] = { true, true, true, true, false };
    Menu m;
    menu_init(&m, 3, 5);
    menu_set_boat_racing(&m, raceable);
    menu_set_weather(&m, 2, 0);
    m.boat = 4;
    enter_boats(&m);
    menu_update(&m, BTN_A);
    T_ASSERT(m.screen == MENU_WEATHER);
    menu_update(&m, BTN_B);
    T_ASSERT(m.screen == MENU_BOATS);
    T_ASSERT(m.sel == 0);
    T_ASSERT(raceable[m.sel]);
}

/* No racer at all: every boat stays selectable rather than leaving the screen empty. */
static void test_all_cruisers_falls_back_to_all_selectable(void) {
    static const bool raceable[5] = { false, false, false, false, false };
    Menu m;
    menu_init(&m, 3, 5);
    menu_set_boat_racing(&m, raceable);
    enter_boats(&m);
    T_ASSERT(m.sel == 0);
    for (int i = 1; i < 5; i++) {
        menu_update(&m, BTN_DOWN);
        T_ASSERT(m.sel == i);
    }
}

/* No raceable list set: every boat is selectable, including the last. */
static void test_null_raceable_selects_every_boat(void) {
    Menu m;
    menu_init(&m, 3, 5);
    m.boat = 4;
    enter_boats(&m);
    T_ASSERT(m.sel == 4);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == 0);
}

/* A list longer than MENU_LIST_VIS scrolls just far enough to keep the selection on screen, wrapping too. */
static void test_long_list_scrolls_with_selection(void) {
    Menu m;
    menu_init(&m, 8, 3);
    m.sel = MENU_ITEM_RACE;
    menu_update(&m, BTN_A);
    T_ASSERT(m.screen == MENU_TRACKS);
    T_ASSERT(m.top == 0);
    for (int i = 0; i < MENU_LIST_VIS - 1; i++) menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == MENU_LIST_VIS - 1);
    T_ASSERT(m.top == 0);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.top == 1);
    menu_update(&m, BTN_UP);
    T_ASSERT(m.top == 1);   /* sel 4 is still on screen */
    m.sel = 0; menu_update(&m, BTN_UP);   /* wrap to the last track */
    T_ASSERT(m.sel == 7);
    T_ASSERT(m.top == 8 - MENU_LIST_VIS);
    menu_update(&m, BTN_DOWN);   /* and back to the first */
    T_ASSERT(m.sel == 0);
    T_ASSERT(m.top == 0);
}

/* Entering a screen with the selection low in a long list shows it at the bottom of the window. */
static void test_entering_screen_scrolls_to_selection(void) {
    Menu m;
    menu_init(&m, 3, 9);
    m.boat = 7;
    m.sel = MENU_ITEM_JOURNEY;
    menu_update(&m, BTN_A);
    T_ASSERT(m.screen == MENU_BOATS);
    T_ASSERT(m.sel == 7);
    T_ASSERT(m.top == 7 - (MENU_LIST_VIS - 1));
    menu_update(&m, BTN_B);
    T_ASSERT(m.screen == MENU_MAIN);
    T_ASSERT(m.top == 0);
}

/* menu_rows and menu_row_enabled describe the list being drawn. */
static void test_rows_and_enabled_rows(void) {
    static const bool raceable[4] = { true, false, true, true };
    Menu m;
    menu_init(&m, 6, 4);
    menu_set_boat_racing(&m, raceable);
    T_ASSERT(menu_rows(&m) == MENU_MAIN_ITEMS);
    T_ASSERT(menu_row_enabled(&m, 1));
    m.screen = MENU_TRACKS;
    T_ASSERT(menu_rows(&m) == 6);
    m.screen = MENU_BOATS;
    T_ASSERT(menu_rows(&m) == 4);
    T_ASSERT(menu_row_enabled(&m, 0));
    T_ASSERT(!menu_row_enabled(&m, 1));
    m.screen = MENU_PREVIEW;
    T_ASSERT(menu_rows(&m) == 0);
}

static void test_locked_main_rows_are_skipped_and_dimmed(void) {
    Menu m;
    menu_init(&m, 1, 3);
    menu_set_main_locked(&m, 1u << MENU_ITEM_JOURNEY | 1u << MENU_ITEM_RACE);
    T_ASSERT(menu_row_enabled(&m, MENU_ITEM_FREE));
    T_ASSERT(!menu_row_enabled(&m, MENU_ITEM_JOURNEY));
    T_ASSERT(!menu_row_enabled(&m, MENU_ITEM_RACE));
    T_ASSERT(menu_row_enabled(&m, MENU_ITEM_HELP));
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == MENU_ITEM_HELP);
    menu_update(&m, BTN_UP);
    T_ASSERT(m.sel == MENU_ITEM_FREE);
    menu_update(&m, BTN_UP);
    T_ASSERT(m.sel == MENU_ITEM_SOUND);
    menu_update(&m, BTN_DOWN);
    T_ASSERT(m.sel == MENU_ITEM_FREE);
    T_ASSERT(menu_update(&m, BTN_A) == MENU_START_FREE);
}

static void test_a_on_a_locked_row_does_nothing(void) {
    Menu m;
    menu_init(&m, 2, 3);
    menu_set_main_locked(&m, 1u << MENU_ITEM_JOURNEY | 1u << MENU_ITEM_RACE);
    m.sel = MENU_ITEM_RACE;   /* forced past the cursor rules */
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_MAIN);
    m.sel = MENU_ITEM_JOURNEY;
    T_ASSERT(menu_update(&m, BTN_A) == MENU_NONE);
    T_ASSERT(m.screen == MENU_MAIN && !m.journey);
}

static void test_locking_moves_cursor_off_a_locked_row(void) {
    Menu m;
    menu_init(&m, 1, 3);
    m.sel = MENU_ITEM_JOURNEY;
    menu_set_main_locked(&m, 1u << MENU_ITEM_JOURNEY | 1u << MENU_ITEM_RACE);
    T_ASSERT(m.sel == MENU_ITEM_HELP);
}

int main(void) {
    T_RUN(test_starts_on_main_first_row);
    T_RUN(test_down_up_wrap);
    T_RUN(test_help_opens_scrolls_and_returns);
    T_RUN(test_sound_row_cycles_levels);
    T_RUN(test_help_scrolls_with_crank);
    T_RUN(test_short_help_does_not_scroll);
    T_RUN(test_a_on_free_starts_free);
    T_RUN(test_racing_opens_track_list);
    T_RUN(test_track_pick_starts_race);
    T_RUN(test_boat_pick_starts_race);
    T_RUN(test_b_on_boats_returns_to_track_list);
    T_RUN(test_weather_follows_boat);
    T_RUN(test_no_boats_starts_race_after_track);
    T_RUN(test_preview_animation_ticks_and_restarts);
    T_RUN(test_b_goes_back_to_racing_row);
    T_RUN(test_no_tracks_racing_stays);
    T_RUN(test_multiple_buttons_one_frame);
    T_RUN(test_boat_screen_skips_cruisers);
    T_RUN(test_boat_screen_opens_on_a_racer_when_current_boat_is_a_cruiser);
    T_RUN(test_boat_screen_b_returns_to_a_racer_row);
    T_RUN(test_all_cruisers_falls_back_to_all_selectable);
    T_RUN(test_null_raceable_selects_every_boat);
    T_RUN(test_long_list_scrolls_with_selection);
    T_RUN(test_entering_screen_scrolls_to_selection);
    T_RUN(test_rows_and_enabled_rows);
    T_RUN(test_locked_main_rows_are_skipped_and_dimmed);
    T_RUN(test_a_on_a_locked_row_does_nothing);
    T_RUN(test_locking_moves_cursor_off_a_locked_row);
    return T_DONE();
}
