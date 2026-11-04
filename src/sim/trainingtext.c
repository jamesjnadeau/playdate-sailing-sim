#include "trainingtext.h"
#include <string.h>

static const char *const NAMES[TRAIN_STEPS] = {
    "downwind_auto", "beam_auto", "points_auto", "close_hauled", "irons", "engine_free",
    "downwind_manual", "beam_manual", "points_manual", "done",
};
static const char NOTICE[] = "config/training.md is missing text for this step";

/* Step named by the heading text (len bytes, spaces trimmed), or -1. */
static int step_of(const char *s, int len) {
    while (len > 0 && *s == ' ') { s++; len--; }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\r')) len--;
    for (int i = 0; i < TRAIN_STEPS; i++)
        if ((int)strlen(NAMES[i]) == len && memcmp(NAMES[i], s, (size_t)len) == 0) return i;
    return -1;
}

int trainingtext_parse(const char *md, TrainingText *out) {
    for (int i = 0; i < TRAIN_STEPS; i++) out->text[i][0] = 0;
    if (!md) return 0;
    int cur = -1, len = 0;
    const char *p = md;
    while (*p) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        const char *l = p;
        while (n > 0 && (*l == ' ' || *l == '\t')) { l++; n--; }
        while (n > 0 && (l[n - 1] == ' ' || l[n - 1] == '\r' || l[n - 1] == '\t')) n--;
        if (n > 0 && l[0] == '#') {
            const char *h = l + 1;
            int hn = n - 1;
            while (hn > 0 && *h == '#') { h++; hn--; }
            cur = step_of(h, hn);
            len = 0;
        } else if (n > 0 && cur >= 0) {
            char *dst = out->text[cur];
            if (len > 0 && len < TRAINTEXT_LEN - 1) dst[len++] = ' ';
            for (int i = 0; i < n && len < TRAINTEXT_LEN - 1; i++) dst[len++] = l[i];
            dst[len] = 0;
        }
        if (!e) break;
        p = e + 1;
    }
    int got = 0;
    for (int i = 0; i < TRAIN_STEPS; i++) if (out->text[i][0]) got++;
    return got;
}

const char *trainingtext_get(const TrainingText *t, TrainStep s) {
    if ((int)s < 0 || (int)s >= TRAIN_STEPS || !t->text[s][0]) return NOTICE;
    return t->text[s];
}
