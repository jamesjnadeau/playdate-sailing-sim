#include "t.h"
#include "boatlist.h"
#include <string.h>
#include <stdio.h>

static void test_accepts_json_strips_extension(void) {
    BoatList l; boatlist_init(&l);
    T_ASSERT(boatlist_add(&l, "j24.json"));
    T_ASSERT(l.count == 1);
    T_ASSERT(strcmp(l.name[0], "j24") == 0);
}

static void test_rejects_non_boats(void) {
    BoatList l; boatlist_init(&l);
    const char *junk[] = { "README.md", ".DS_Store", "x.json.bak", "old/", ".json", ".hidden.json" };
    for (size_t i = 0; i < sizeof junk / sizeof *junk; i++) T_ASSERT(!boatlist_add(&l, junk[i]));
    T_ASSERT(l.count == 0);
}

static void test_rejects_bad_names(void) {
    BoatList l; boatlist_init(&l);
    T_ASSERT(!boatlist_add(&l, "my boat.json"));
    T_ASSERT(!boatlist_add(&l, "a\"b.json"));
    T_ASSERT(!boatlist_add(&l, "a/b.json"));
    T_ASSERT(!boatlist_add(&l, "Upper.json"));
    T_ASSERT(!boatlist_add(&l, "abcdefghijklmnopqrstuvwx.json"));   /* 24-char stem */
    T_ASSERT(boatlist_add(&l, "abcdefghijklmnopqrstuvw.json"));     /* 23-char stem */
    T_ASSERT(l.count == 1);
}

static void test_sorted_sunfish_first(void) {
    BoatList l; boatlist_init(&l);
    const char *in[] = { "tp52.json", "j24.json", "sunfish.json", "longship.json", "dinghy.json" };
    const char *want[] = { "sunfish", "dinghy", "j24", "longship", "tp52" };
    for (int i = 0; i < 5; i++) T_ASSERT(boatlist_add(&l, in[i]));
    boatlist_finish(&l);
    T_ASSERT(l.count == 5);
    for (int i = 0; i < 5; i++) {
        T_ASSERT(strcmp(l.name[i], want[i]) == 0);
        T_ASSERT(l.ptr[i] == l.name[i]);
    }
}

static void test_cap_and_order(void) {
    BoatList l; boatlist_init(&l);
    char f[32];
    for (int i = 0; i < 20; i++) { snprintf(f, sizeof f, "b%02d.json", i); boatlist_add(&l, f); }
    boatlist_add(&l, "sunfish.json");
    boatlist_finish(&l);
    T_ASSERT(l.count == BOATLIST_MAX);
    T_ASSERT(strcmp(l.name[0], "sunfish") == 0);
}

static void test_duplicate_ignored(void) {
    BoatList l; boatlist_init(&l);
    T_ASSERT(boatlist_add(&l, "j24.json"));
    T_ASSERT(!boatlist_add(&l, "j24.json"));
    T_ASSERT(l.count == 1);
}

static void test_empty_gets_default(void) {
    BoatList l; boatlist_init(&l);
    boatlist_finish(&l);
    T_ASSERT(l.count == 1);
    T_ASSERT(strcmp(l.name[0], "sunfish") == 0);
    T_ASSERT(l.ptr[0] == l.name[0]);
}

int main(void) {
    T_RUN(test_accepts_json_strips_extension);
    T_RUN(test_rejects_non_boats);
    T_RUN(test_rejects_bad_names);
    T_RUN(test_sorted_sunfish_first);
    T_RUN(test_cap_and_order);
    T_RUN(test_duplicate_ignored);
    T_RUN(test_empty_gets_default);
    return T_DONE();
}
