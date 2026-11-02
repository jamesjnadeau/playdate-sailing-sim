#include "besttimes.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void besttimes_init(BestTimes *b) { memset(b, 0, sizeof *b); }

static int find(const BestTimes *b, const char *course) {
    for (int i = 0; i < b->n; i++)
        if (strcmp(b->name[i], course) == 0) return i;
    return -1;
}

bool besttimes_get(const BestTimes *b, const char *course, double *t) {
    if (!course) return false;
    int i = find(b, course);
    if (i < 0) return false;
    if (t) *t = b->time[i];
    return true;
}

bool besttimes_record(BestTimes *b, const char *course, double t) {
    if (!course || !course[0] || !isfinite(t) || t <= 0.0) return false;
    int i = find(b, course);
    if (i < 0) {
        if (b->n >= BESTTIMES_MAX) return false;
        i = b->n++;
        snprintf(b->name[i], sizeof b->name[i], "%s", course);
        b->time[i] = t;
        return true;
    }
    if (t >= b->time[i]) return false;
    b->time[i] = t;
    return true;
}

int besttimes_save(const BestTimes *b, char *out, size_t n) {
    size_t len = 0;
    if (n) out[0] = 0;
    for (int i = 0; i < b->n; i++) {
        int w = snprintf(out ? out + len : NULL, n > len ? n - len : 0, "%s\t%.3f\n", b->name[i], b->time[i]);
        if (w < 0 || len + (size_t)w >= n) return -1;
        len += (size_t)w;
    }
    return (int)len;
}

void besttimes_load(BestTimes *b, const char *text) {
    if (!text) return;
    while (*text) {
        const char *eol = strchr(text, '\n');
        size_t len = eol ? (size_t)(eol - text) : strlen(text);
        const char *tab = memchr(text, '\t', len);
        if (tab && tab > text && (size_t)(tab - text) < BESTTIMES_NAME_LEN) {
            char name[BESTTIMES_NAME_LEN], num[32];
            size_t nl = (size_t)(tab - text), vl = len - nl - 1;
            if (vl > 0 && vl < sizeof num) {
                memcpy(name, text, nl);
                name[nl] = 0;
                memcpy(num, tab + 1, vl);
                num[vl] = 0;
                char *end;
                double t = strtod(num, &end);
                if (end != num && *end == 0) besttimes_record(b, name, t);
            }
        }
        if (!eol) break;
        text = eol + 1;
    }
}
