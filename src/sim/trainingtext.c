#include "trainingtext.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const NAMES[TRAIN_STEPS] = {
    "downwind_auto", "beam_auto", "points_auto", "close_hauled", "irons", "engine_free",
    "downwind_manual", "beam_manual", "points_manual", "done",
};
static const char *const MODES[] = { "helm", "trim", "hoist", "camera" };   /* in Mode order */
static const char NOTICE[] = "config/training.md is missing text for this step";

/* Trim spaces/tabs/CRs from both ends of [*s, *s+*len). */
static void trim(const char **s, int *len) {
    while (*len > 0 && (**s == ' ' || **s == '\t')) { (*s)++; (*len)--; }
    while (*len > 0 && ((*s)[*len - 1] == ' ' || (*s)[*len - 1] == '\r' || (*s)[*len - 1] == '\t')) (*len)--;
}

static bool is(const char *s, int len, const char *word) {
    return (int)strlen(word) == len && memcmp(word, s, (size_t)len) == 0;
}

/* Step named by the heading text (len bytes), or -1. */
static int step_of(const char *s, int len) {
    trim(&s, &len);
    for (int i = 0; i < TRAIN_STEPS; i++) if (is(s, len, NAMES[i])) return i;
    return -1;
}

/* Number in exactly [s, s+len), or false. */
static bool number(const char *s, int len, float *out) {
    char tmp[32];
    trim(&s, &len);
    if (len <= 0 || len >= (int)sizeof tmp) return false;
    memcpy(tmp, s, (size_t)len);
    tmp[len] = 0;
    char *end;
    const double v = strtod(tmp, &end);
    if (*end || !isfinite(v)) return false;
    *out = (float)v;
    return true;
}

/* "LO..HI", either end optional; a missing end keeps *lo / *hi. */
static bool range(const char *s, int len, float *lo, float *hi) {
    trim(&s, &len);
    const char *dots = NULL;
    for (int i = 0; i + 1 < len; i++) if (s[i] == '.' && s[i + 1] == '.') { dots = s + i; break; }
    if (!dots) return false;
    const int ln = (int)(dots - s), hn = len - ln - 2;
    float a = *lo, b = *hi;
    if (ln > 0 && !number(s, ln, &a)) return false;
    if (hn > 0 && !number(dots + 2, hn, &b)) return false;
    if (a > b) return false;
    *lo = a;
    *hi = b;
    return true;
}

static bool on_off(const char *s, int len, signed char *out) {
    trim(&s, &len);
    if (is(s, len, "on")) { *out = 1; return true; }
    if (is(s, len, "off")) { *out = 0; return true; }
    return false;
}

/* One "name value" term of a require line. */
static bool term(const char *s, int len, TrainGoal *q) {
    trim(&s, &len);
    int k = 0;
    while (k < len && s[k] != ' ' && s[k] != '\t') k++;
    const char *v = s + k;
    const int vn = len - k;
    if (is(s, k, "autotrim")) return on_off(v, vn, &q->autotrim);
    if (is(s, k, "autopilot")) return on_off(v, vn, &q->autopilot);
    if (is(s, k, "engine")) return on_off(v, vn, &q->engine);
    if (is(s, k, "wind")) return range(v, vn, &q->wind_lo, &q->wind_hi);
    if (is(s, k, "speed")) return range(v, vn, &q->speed_lo, &q->speed_hi);
    if (is(s, k, "mode")) {
        const char *m = v;
        int mn = vn;
        trim(&m, &mn);
        for (int i = 0; i < (int)(sizeof MODES / sizeof MODES[0]); i++)
            if (is(m, mn, MODES[i])) { q->mode = (signed char)i; return true; }
    }
    return false;
}

static bool require_line(const char *s, int len, TrainGoal *q) {
    bool ok = true;
    while (len > 0) {
        int k = 0;
        while (k < len && s[k] != ',') k++;
        if (!term(s, k, q)) ok = false;
        s += k;
        len -= k;
        if (len > 0) { s++; len--; }   /* the comma */
    }
    return ok;
}

/* "key:" at the start of the line: the value after it, else NULL. */
static const char *key(const char *l, int n, const char *k, int *vn) {
    const int kn = (int)strlen(k);
    if (n < kn || memcmp(l, k, (size_t)kn) != 0) return NULL;
    *vn = n - kn;
    return l + kn;
}

int trainingtext_parse(const char *md, TrainingText *out) {
    bool custom[TRAIN_STEPS];
    for (int i = 0; i < TRAIN_STEPS; i++) {
        out->text[i][0] = 0;
        out->seconds[i] = 0.0f;
        training_default_goal((TrainStep)i, &out->goal[i]);
        out->hint[i][0] = 0;
        out->hint_after[i] = 0.0f;
        custom[i] = false;
    }
    out->default_s = TRAINTEXT_DEFAULT_S;
    out->default_hint_after = TRAINTEXT_HINT_AFTER_S;
    out->bad_line = 0;
    if (!md) return 0;
    int cur = -1, len = 0, lineno = 0;
    const char *p = md;
    while (*p) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        const char *l = p, *v;
        int vn;
        bool ok = true;
        lineno++;
        trim(&l, &n);
        float f;
        if (n > 0 && l[0] == '#') {
            const char *h = l + 1;
            int hn = n - 1;
            while (hn > 0 && *h == '#') { h++; hn--; }
            cur = step_of(h, hn);
            len = 0;
        } else if ((v = key(l, n, "seconds:", &vn))) {
            ok = number(v, vn, &f) && f > 0.0f;
            if (ok) { if (cur >= 0) out->seconds[cur] = f; else out->default_s = f; }
        } else if ((v = key(l, n, "hint_after:", &vn))) {
            ok = number(v, vn, &f) && f > 0.0f;
            if (ok) { if (cur >= 0) out->hint_after[cur] = f; else out->default_hint_after = f; }
        } else if (cur < 0) {
            /* text before the first heading is ignored */
        } else if ((v = key(l, n, "hint:", &vn))) {
            trim(&v, &vn);
            if (vn > TRAINTEXT_HINT_LEN - 1) vn = TRAINTEXT_HINT_LEN - 1;
            memcpy(out->hint[cur], v, (size_t)vn);
            out->hint[cur][vn] = 0;
        } else if ((v = key(l, n, "hold:", &vn))) {
            ok = number(v, vn, &f) && f >= 0.0f;
            if (ok) out->goal[cur].hold_s = f;
        } else if ((v = key(l, n, "require:", &vn)) || (v = key(l, n, "visit:", &vn))) {
            TrainGoal *q = &out->goal[cur];
            if (!custom[cur]) {   /* the file's goal replaces the built-in one, keeping any hold: already read */
                const float hold = q->hold_s;
                training_goal_any(q);
                q->hold_s = hold;
                custom[cur] = true;
            }
            if (l[0] == 'r') {
                ok = require_line(v, vn, q);
            } else {
                float lo = 0.0f, hi = 180.0f;
                ok = q->nvisit < TRAIN_VISITS && range(v, vn, &lo, &hi);
                if (ok) { q->visit_lo[q->nvisit] = lo; q->visit_hi[q->nvisit] = hi; q->nvisit++; }
            }
        } else if (n > 0) {
            char *dst = out->text[cur];
            if (len > 0 && len < TRAINTEXT_LEN - 1) dst[len++] = '\n';
            for (int i = 0; i < n && len < TRAINTEXT_LEN - 1; i++) dst[len++] = l[i];
            dst[len] = 0;
        }
        if (!ok && !out->bad_line) out->bad_line = lineno;
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

static float per_page(const TrainingText *t, TrainStep s) {
    return (int)s >= 0 && (int)s < TRAIN_STEPS && t->seconds[s] > 0.0f ? t->seconds[s] : t->default_s;
}

int trainingtext_page(const TrainingText *t, TrainStep s, float elapsed, char *out, int cap) {
    const char *txt = trainingtext_get(t, s);
    const float per = per_page(t, s);
    int want = per > 0.0f && elapsed > 0.0f ? (int)(elapsed / per) : 0;
    int page = 0;
    while (page < want) {
        const char *nl = strchr(txt, '\n');
        if (!nl) break;
        txt = nl + 1;
        page++;
    }
    const char *nl = strchr(txt, '\n');
    int n = nl ? (int)(nl - txt) : (int)strlen(txt);
    if (n > cap - 1) n = cap - 1;
    if (n < 0) n = 0;
    memcpy(out, txt, (size_t)n);
    out[n] = 0;
    return page;
}

const char *trainingtext_hint(const TrainingText *t, TrainStep s, float elapsed, float miss) {
    if ((int)s < 0 || (int)s >= TRAIN_STEPS || !t->hint[s][0]) return NULL;
    const float after = t->hint_after[s] > 0.0f ? t->hint_after[s] : t->default_hint_after;
    int pages = 0;
    for (const char *c = trainingtext_get(t, s); *c; c++) if (*c == '\n') pages++;
    const float last_up = elapsed - (float)pages * per_page(t, s);
    return miss >= after && last_up >= after ? t->hint[s] : NULL;
}
