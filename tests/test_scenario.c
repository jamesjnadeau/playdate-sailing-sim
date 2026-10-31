#include "t.h"
#include "scenario.h"
#include <string.h>

static Scenario S;

static void P(const char *text) {
    char err[96];
    T_ASSERT(scenario_parse(&S, text, err, sizeof err));
}

static void test_press_is_one_frame_then_release(void) {
    ScFrame f;
    P("press A\nwait 1\nend");
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(f.pushed == 32 && f.cur == 32 && f.released == 0);
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(f.cur == 0 && f.released == 32 && f.pushed == 0);
    T_ASSERT(!scenario_poll(&S, &f));
}

static void test_hold_n_frames(void) {
    ScFrame f;
    int i;
    P("hold B 3\nwait 1\nend");
    for (i = 0; i < 3; i++) {
        T_ASSERT(scenario_poll(&S, &f));
        T_ASSERT(f.cur == 16);
        T_ASSERT(i == 0 ? f.pushed == 16 : f.pushed == 0);
        T_ASSERT(f.released == 0);
    }
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(f.released == 16 && f.cur == 0);
    T_ASSERT(!scenario_poll(&S, &f));
}

static void test_crank_for(void) {
    ScFrame f;
    P("crank 15.5 for 2\nend");
    T_ASSERT(scenario_poll(&S, &f)); T_NEAR(f.crank, 15.5f, 1e-6);
    T_ASSERT(scenario_poll(&S, &f)); T_NEAR(f.crank, 15.5f, 1e-6);
    T_ASSERT(!scenario_poll(&S, &f));
    P("crank 15.5 for 2\nwait 1\nend");
    scenario_poll(&S, &f); scenario_poll(&S, &f);
    T_ASSERT(scenario_poll(&S, &f)); T_NEAR(f.crank, 0.0f, 1e-9);
}

static void test_wait_counts_frames(void) {
    ScFrame f;
    int n = 0;
    P("wait 4\nend");
    while (scenario_poll(&S, &f)) n++;
    T_ASSERT(n == 4);
    T_ASSERT(!scenario_poll(&S, &f));
}

static void test_snap_after_frame(void) {
    ScFrame f;
    char name[SC_NAME_MAX];
    P("wait 2\nsnap a\nwait 1\nsnap b\nend");
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_NONE);
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_SNAP);
    T_ASSERT(strcmp(name, "a") == 0);
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_NONE);
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_SNAP);
    T_ASSERT(strcmp(name, "b") == 0);
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_END);
}

static void test_state_after_frame(void) {
    ScFrame f;
    char name[SC_NAME_MAX];
    P("wait 1\nstate\nend");
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_STATE);
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_END);
}

static void test_sysmenu(void) {
    ScFrame f;
    P("sysmenu boat 2\nwait 2\nend");
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(f.sys == SC_SYS_BOAT && f.sys_arg == 2);
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(f.sys == SC_SYS_NONE);
    P("sysmenu restart\nsysmenu weather 1\nwait 1");
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(f.sys == SC_SYS_WEATHER && f.sys_arg == 1);
}

static void test_snap_behind_sysmenu(void) {
    ScFrame f;
    char name[SC_NAME_MAX];
    P("wait 1\nsysmenu restart\nsnap a\nstate\nwait 1\nend");
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(f.sys == SC_SYS_NONE);
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_SNAP);
    T_ASSERT(strcmp(name, "a") == 0);
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_STATE);
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_NONE);
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(f.sys == SC_SYS_RESTART);
}

static void test_snap_after_press_and_hold(void) {
    ScFrame f;
    char name[SC_NAME_MAX];
    P("press A\nsnap p\nhold B 2\nsnap h\nend");
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_SNAP && strcmp(name, "p") == 0);
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_NONE);
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_SNAP && strcmp(name, "h") == 0);
}

static bool fails(const char *text, const char *want) {
    char err[96] = "";
    if (scenario_parse(&S, text, err, sizeof err)) return false;
    return want == NULL || strstr(err, want) != NULL;
}

static void test_errors_have_line_numbers(void) {
    static char big[600 * 8 + 1];
    int i;
    T_ASSERT(fails("wait 1\nfrobnicate\n", "line 2"));
    T_ASSERT(fails("wait x", "line 1"));
    T_ASSERT(fails("press Z", "line 1"));
    T_ASSERT(fails("wait 1\nhold A", "line 2"));
    T_ASSERT(fails("wait 1\nsnap", "line 2"));
    T_ASSERT(fails("snap a\nwait 1", "line 1"));
    T_ASSERT(fails("wait 1\nsysmenu main\n", "line 2"));
    T_ASSERT(fails("crank nan for 2", "line 1"));
    big[0] = 0;
    for (i = 0; i < 600; i++) strcat(big, "wait 1\n");
    T_ASSERT(fails(big, "line 513"));
}

static void test_eof_ends(void) {
    ScFrame f;
    char name[SC_NAME_MAX];
    P("wait 1");
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(scenario_after(&S, name) == SC_AFTER_END);
    T_ASSERT(!scenario_poll(&S, &f));
}

static void test_comments_and_blank_lines(void) {
    ScFrame f;
    P("# hi\n\n wait 1 # x\nend");
    T_ASSERT(scenario_poll(&S, &f));
    T_ASSERT(!scenario_poll(&S, &f));
}

int main(void) {
    T_RUN(test_press_is_one_frame_then_release);
    T_RUN(test_hold_n_frames);
    T_RUN(test_crank_for);
    T_RUN(test_wait_counts_frames);
    T_RUN(test_snap_after_frame);
    T_RUN(test_state_after_frame);
    T_RUN(test_sysmenu);
    T_RUN(test_snap_behind_sysmenu);
    T_RUN(test_snap_after_press_and_hold);
    T_RUN(test_errors_have_line_numbers);
    T_RUN(test_eof_ends);
    T_RUN(test_comments_and_blank_lines);
    return T_DONE();
}
