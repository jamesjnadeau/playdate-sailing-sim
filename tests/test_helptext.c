#include <string.h>
#include "t.h"
#include "helptext.h"

static void test_heading_bullet_body(void) {
    HelpLine l[16];
    int n = helptext_layout("# Modes\n- UP: wheel\nplain text\n", 40, l, 16);
    T_ASSERT(n == 3);
    T_ASSERT(l[0].style == HELP_HEAD && strcmp(l[0].text, "Modes") == 0);
    T_ASSERT(l[1].style == HELP_BULLET && l[1].first && strcmp(l[1].text, "UP: wheel") == 0);
    T_ASSERT(l[2].style == HELP_BODY && strcmp(l[2].text, "plain text") == 0);
}

static void test_wrap_within_cols(void) {
    HelpLine l[16];
    int n = helptext_layout("- one two three four five six seven eight nine ten\n", 20, l, 16);
    T_ASSERT(n > 1);
    T_ASSERT(l[0].first);
    for (int i = 0; i < n; i++) {
        T_ASSERT((int)strlen(l[i].text) <= 18);
        if (i) T_ASSERT(!l[i].first && l[i].style == HELP_BULLET);
    }
}

static void test_blank_collapse_and_trim(void) {
    HelpLine l[16];
    int n = helptext_layout("a\n\n\n\nb\n\n\n", 40, l, 16);
    T_ASSERT(n == 3);
    T_ASSERT(l[1].text[0] == 0);
}

static void test_max_rows(void) {
    HelpLine l[2];
    T_ASSERT(helptext_layout("a\nb\nc\nd\n", 40, l, 2) == 2);
}

static void test_strips_markup(void) {
    HelpLine l[4];
    helptext_layout("use **A** and `B`\n", 40, l, 4);
    T_ASSERT(strcmp(l[0].text, "use A and B") == 0);
}

int main(void) {
    T_RUN(test_heading_bullet_body);
    T_RUN(test_wrap_within_cols);
    T_RUN(test_blank_collapse_and_trim);
    T_RUN(test_max_rows);
    T_RUN(test_strips_markup);
    T_DONE();
}
