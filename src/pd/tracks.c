#include "tracks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"

typedef struct { char names[TRACKS_MAX][64]; int n; } NameList;

static void collect(const char *name, void *ud) {
    NameList *nl = ud;
    size_t len = strlen(name);
    if (nl->n >= TRACKS_MAX || len < 6 || len >= 64) return;
    if (strcmp(name + len - 5, ".json") != 0) return;
    strcpy(nl->names[nl->n++], name);
}

static int by_name(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static void addwarn(char *warn, size_t cap, const char *path, const char *msg) {
    size_t n = strlen(warn);
    if (n + 3 >= cap) return;
    if (n) {
        snprintf(warn + n, cap - n, "; ");
        n = strlen(warn);
    }
    snprintf(warn + n, cap - n, "%s: %s", path, msg);
}

int tracks_load_all(PlaydateAPI *pd, Track *out, int max, char *warn, size_t warnlen) {
    NameList nl;
    nl.n = 0;
    if (warnlen) warn[0] = 0;
    if (max > TRACKS_MAX) max = TRACKS_MAX;
    pd->file->listfiles("tracks", collect, &nl, 0);
    qsort(nl.names, (size_t)nl.n, sizeof nl.names[0], by_name);

    int count = 0;
    for (int i = 0; i < nl.n && count < max; i++) {
        char path[96], msg[160];
        snprintf(path, sizeof path, "tracks/%s", nl.names[i]);
        char *buf = NULL;
        int n = assets_read_file(pd, path, &buf);
        if (n < 0) {
            addwarn(warn, warnlen, path, "missing or too large, skipped");
            continue;
        }
        msg[0] = 0;
        bool ok = track_load(&out[count], buf, (size_t)n, msg, sizeof msg);
        if (msg[0]) addwarn(warn, warnlen, path, msg);
        /* a parse error is skipped; a track with only other warnings still plays */
        if (ok || strncmp(msg, "parse error", 11) != 0) count++;
    }
    if (count == 0) {
        track_default(&out[0]);
        return 1;
    }
    return count;
}
