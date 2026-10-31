#include "helptext.h"
#include <string.h>

static int emit(HelpLine *out, int n, int max, const char *s, int len, HelpStyle style, int first) {
    if (n >= max) return n;
    memcpy(out[n].text, s, (size_t)len);
    out[n].text[len] = 0;
    out[n].style = (unsigned char)style;
    out[n].first = (unsigned char)first;
    return n + 1;
}

/* markdown line with emphasis marks removed */
static int clean(const char *s, int len, char *dst, int cap) {
    int n = 0;
    for (int i = 0; i < len && n < cap - 1; i++) {
        if (s[i] == '`' || s[i] == '*') continue;
        dst[n++] = s[i];
    }
    dst[n] = 0;
    return n;
}

/* word-wraps text into rows of width cols */
static int wrap(HelpLine *out, int n, int max, const char *t, int len, int cols, HelpStyle style, int first) {
    int pos = 0;
    while (pos < len) {
        while (pos < len && t[pos] == ' ') pos++;
        if (pos >= len) break;
        int end = pos + cols;
        if (end >= len) end = len;
        else {
            int cut = end;
            while (cut > pos && t[cut] != ' ') cut--;
            if (cut > pos) end = cut;
        }
        int e = end;
        while (e > pos && t[e - 1] == ' ') e--;
        n = emit(out, n, max, t + pos, e - pos, style, first);
        first = 0;
        pos = end;
    }
    return n;
}

int helptext_layout(const char *md, int cols, HelpLine *out, int max) {
    if (cols > HELP_COLS_MAX) cols = HELP_COLS_MAX;
    if (cols < 8) cols = 8;
    int n = 0;
    int blank = 1;           /* a leading blank row would be noise */
    const char *p = md;
    while (*p && n < max) {
        const char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        if (len > 0 && p[len - 1] == '\r') len--;
        char buf[256];
        int m = clean(p, len, buf, sizeof buf);
        const char *t = buf;
        if (m == 0) {
            if (!blank) n = emit(out, n, max, "", 0, HELP_BODY, 0);
            blank = 1;
        } else {
            blank = 0;
            if (t[0] == '#') {
                while (*t == '#') { t++; m--; }
                while (*t == ' ') { t++; m--; }
                n = wrap(out, n, max, t, m, cols, HELP_HEAD, 0);
            } else if (t[0] == '-' && t[1] == ' ') {
                t += 2; m -= 2;
                n = wrap(out, n, max, t, m, cols - 2, HELP_BULLET, 1);
            } else {
                n = wrap(out, n, max, t, m, cols, HELP_BODY, 0);
            }
        }
        p = e ? e + 1 : p + len;
        if (!e) break;
    }
    while (n > 0 && out[n - 1].text[0] == 0) n--;
    return n;
}
