#include "boatlist.h"
#include <string.h>

void boatlist_init(BoatList *l) { memset(l, 0, sizeof *l); }

/* sunfish first, then by name */
static int order(const char *a, const char *b) {
    bool sa = strcmp(a, "sunfish") == 0, sb = strcmp(b, "sunfish") == 0;
    if (sa != sb) return sa ? -1 : 1;
    return strcmp(a, b);
}

bool boatlist_add(BoatList *l, const char *filename) {
    static const char ext[] = ".json";
    size_t len = strlen(filename), elen = sizeof ext - 1;
    if (len <= elen || strcmp(filename + len - elen, ext) != 0) return false;
    size_t stem = len - elen;
    if (stem >= BOATLIST_NAME_LEN) return false;
    for (size_t i = 0; i < stem; i++) {
        char c = filename[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    char name[BOATLIST_NAME_LEN];
    memcpy(name, filename, stem);
    name[stem] = 0;
    for (int i = 0; i < l->count; i++) if (strcmp(l->name[i], name) == 0) return false;

    /* keep the list sorted; when full, the last entry falls off */
    int at = l->count;
    while (at > 0 && order(name, l->name[at - 1]) < 0) at--;
    if (at >= BOATLIST_MAX) return true;
    int last = l->count < BOATLIST_MAX ? l->count : BOATLIST_MAX - 1;
    for (int i = last; i > at; i--) memcpy(l->name[i], l->name[i - 1], BOATLIST_NAME_LEN);
    memcpy(l->name[at], name, sizeof name);
    if (l->count < BOATLIST_MAX) l->count++;
    return true;
}

void boatlist_finish(BoatList *l) {
    if (l->count == 0) {
        strcpy(l->name[0], "sunfish");
        l->count = 1;
    }
    for (int i = 0; i < l->count; i++) l->ptr[i] = l->name[i];
}
