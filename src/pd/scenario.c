#include "scenario.h"
#include "praise.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_frame_cmd(ScCmdKind k) {
    return k == SC_C_WAIT || k == SC_C_PRESS || k == SC_C_HOLD || k == SC_C_CRANK;
}

static void set_err(char *err, size_t errn, int line, const char *msg) {
    if (err && errn) snprintf(err, errn, "line %d: %s", line, msg);
}

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static bool parse_int(const char *t, int *out) {
    char *e;
    long v;
    if (!t) return false;
    v = strtol(t, &e, 10);
    if (e == t || *e || v < 1 || v > 1000000) return false;
    *out = (int)v;
    return true;
}

static bool parse_float(const char *t, float *out) {
    char *e;
    double v;
    if (!t) return false;
    v = strtod(t, &e);
    if (e == t || *e || !isfinite(v)) return false;
    *out = (float)v;
    return true;
}

static bool parse_btn(const char *t, uint32_t *out) {
    if (!t) return false;
    if (!strcmp(t, "left")) *out = 1;
    else if (!strcmp(t, "right")) *out = 2;
    else if (!strcmp(t, "up")) *out = 4;
    else if (!strcmp(t, "down")) *out = 8;
    else if (!strcmp(t, "B")) *out = 16;
    else if (!strcmp(t, "A")) *out = 32;
    else return false;
    return true;
}

static bool valid_name(const char *t) {
    size_t n;
    if (!t) return false;
    n = strlen(t);
    if (n == 0 || n > SC_NAME_MAX - 1) return false;
    for (; *t; t++) {
        char c = *t;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

/* Parse one line (already stripped of comments) into c. Returns NULL or an error message. */
static const char *parse_line(char *line, ScCmd *c, bool have_frame) {
    char *tok[5];
    int nt = 0;
    char *p = line;
    memset(c, 0, sizeof *c);
    while (*p) {
        while (*p && is_ws(*p)) p++;
        if (!*p) break;
        if (nt == 5) return "too many arguments";
        tok[nt++] = p;
        while (*p && !is_ws(*p)) p++;
        if (*p) *p++ = 0;
    }
    if (nt == 0) return "";                       /* blank */
    if (!strcmp(tok[0], "wait")) {
        c->kind = SC_C_WAIT;
        if (nt != 2 || !parse_int(tok[1], &c->n)) return "usage: wait N (N >= 1)";
    } else if (!strcmp(tok[0], "press")) {
        c->kind = SC_C_PRESS;
        if (nt != 2 || !parse_btn(tok[1], &c->btn)) return "usage: press left|right|up|down|A|B";
        c->n = 1;
    } else if (!strcmp(tok[0], "hold")) {
        c->kind = SC_C_HOLD;
        if (nt != 3 || !parse_btn(tok[1], &c->btn) || !parse_int(tok[2], &c->n))
            return "usage: hold X N (N >= 1)";
    } else if (!strcmp(tok[0], "crank")) {
        c->kind = SC_C_CRANK;
        if (nt != 4 || strcmp(tok[2], "for") || !parse_float(tok[1], &c->deg) || !parse_int(tok[3], &c->n))
            return "usage: crank DEG for N (N >= 1)";
    } else if (!strcmp(tok[0], "snap")) {
        c->kind = SC_C_SNAP;
        if (nt != 2 || !valid_name(tok[1])) return "usage: snap NAME ([A-Za-z0-9_]+)";
        if (!have_frame) return "snap before any frame";
        strcpy(c->name, tok[1]);
    } else if (!strcmp(tok[0], "state")) {
        c->kind = SC_C_STATE;
        if (nt != 1) return "usage: state";
    } else if (!strcmp(tok[0], "sysmenu")) {
        c->kind = SC_C_SYS;
        if (nt < 2) return "usage: sysmenu restart|main|boat N|weather N";
        if (!strcmp(tok[1], "restart") || !strcmp(tok[1], "main")) {
            if (nt != 2) return "usage: sysmenu restart|main";
            c->sys = tok[1][0] == 'r' ? SC_SYS_RESTART : SC_SYS_MAIN;
        } else if (!strcmp(tok[1], "boat") || !strcmp(tok[1], "weather")) {
            char *e;
            long v;
            if (nt != 3) return "usage: sysmenu boat NAME|N, sysmenu weather N";
            if (tok[1][0] == 'b' && (tok[2][0] < '0' || tok[2][0] > '9')) {
                /* a boat by name survives boats being added to or removed from boats/ */
                size_t k, len = strlen(tok[2]);
                if (len < 1 || len > 23) return "bad boat name";
                for (k = 0; k < len; k++) {
                    char ch = tok[2][k];
                    if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
                        return "bad boat name";
                }
                c->sys = SC_SYS_BOAT;
                c->n = -1;
                strcpy(c->name, tok[2]);
                return NULL;
            }
            v = strtol(tok[2], &e, 10);
            if (e == tok[2] || *e || v < 0 || v > 1000000) return "bad sysmenu index";
            c->sys = tok[1][0] == 'b' ? SC_SYS_BOAT : SC_SYS_WEATHER;
            c->n = (int)v;
        } else return "unknown sysmenu action";
    } else if (!strcmp(tok[0], "praise")) {
        c->kind = SC_C_PRAISE;
        if (nt != 2 || !parse_int(tok[1], &c->n) || c->n < 1 || c->n > PRAISE_COUNT) return "usage: praise N (N in 1..PRAISE_COUNT)";
        c->n--;   /* the script counts from 1, like the message list in praise.c */
    } else if (!strcmp(tok[0], "end")) {
        c->kind = SC_C_END;
        if (nt != 1) return "usage: end";
    } else return "unknown command";
    return NULL;
}

bool scenario_parse(Scenario *s, const char *text, char *err, size_t errn) {
    int lineno = 0;
    bool have_frame = false;
    int sys_line = 0;                 /* line of a sysmenu not yet followed by a frame */
    const char *p = text;
    memset(s, 0, sizeof *s);
    if (err && errn) err[0] = 0;
    while (*p) {
        char buf[256];
        size_t n = 0;
        const char *msg;
        ScCmd c;
        bool toolong = false;
        char *hash;
        lineno++;
        while (*p && *p != '\n') {
            if (n < sizeof buf - 1) buf[n++] = *p; else toolong = true;
            p++;
        }
        if (*p == '\n') p++;
        buf[n] = 0;
        if (toolong) { set_err(err, errn, lineno, "line too long"); return false; }
        hash = strchr(buf, '#');
        if (hash) *hash = 0;
        msg = parse_line(buf, &c, have_frame);
        if (msg && !*msg) continue;
        if (msg) { set_err(err, errn, lineno, msg); return false; }
        if (s->ncmds >= SC_MAX_CMDS) { set_err(err, errn, lineno, "too many commands"); return false; }
        if (is_frame_cmd(c.kind)) { have_frame = true; sys_line = 0; }
        if (c.kind == SC_C_SYS && !sys_line) sys_line = lineno;
        s->cmds[s->ncmds++] = c;
    }
    if (sys_line) { set_err(err, errn, sys_line, "sysmenu with no following frame"); return false; }
    return true;
}

bool scenario_poll(Scenario *s, ScFrame *f) {
    const ScCmd *c;
    uint32_t cur = 0;
    float crank = 0.0f;
    /* skip zero-time commands; sysmenu attaches to the next frame */
    while (s->pc < s->ncmds && !is_frame_cmd(s->cmds[s->pc].kind)) {
        c = &s->cmds[s->pc];
        if (c->kind == SC_C_END) { s->pc = s->ncmds; break; }
        if (c->kind == SC_C_SYS) { s->pend_sys = c->sys; s->pend_arg = c->n; strcpy(s->pend_name, c->name); }
        if (c->kind == SC_C_PRAISE) { s->pend_praise = true; s->pend_praise_idx = c->n; }
        s->pc++;
    }
    if (s->pc >= s->ncmds) return false;
    c = &s->cmds[s->pc];
    if (c->kind == SC_C_PRESS || c->kind == SC_C_HOLD) cur = c->btn;
    if (c->kind == SC_C_CRANK) crank = c->deg;
    f->cur = cur;
    f->pushed = cur & ~s->prev;
    f->released = s->prev & ~cur;
    f->crank = crank;
    f->sys = s->pend_sys;
    f->sys_arg = s->pend_arg;
    strcpy(f->sys_name, s->pend_name);
    s->pend_sys = SC_SYS_NONE;
    s->pend_arg = 0;
    s->pend_name[0] = 0;
    f->praise_now = s->pend_praise;
    f->praise = s->pend_praise_idx;
    s->pend_praise = false;
    s->prev = cur;
    if (++s->k >= c->n) { s->pc++; s->k = 0; }
    return true;
}

ScAfterKind scenario_after(Scenario *s, char name[SC_NAME_MAX]) {
    const ScCmd *c;
    if (s->k != 0) return SC_AFTER_NONE;          /* mid-command */
    /* sysmenu is consumed into the pending action so snap/state behind it still fire */
    while (s->pc < s->ncmds && s->cmds[s->pc].kind == SC_C_SYS) {
        s->pend_sys = s->cmds[s->pc].sys;
        s->pend_arg = s->cmds[s->pc].n;
        strcpy(s->pend_name, s->cmds[s->pc].name);
        s->pc++;
    }
    if (s->pc >= s->ncmds) return SC_AFTER_END;
    c = &s->cmds[s->pc];
    switch (c->kind) {
    case SC_C_SNAP:
        strcpy(name, c->name);
        s->pc++;
        return SC_AFTER_SNAP;
    case SC_C_STATE:
        s->pc++;
        return SC_AFTER_STATE;
    case SC_C_END:
        s->pc = s->ncmds;
        return SC_AFTER_END;
    default:
        return SC_AFTER_NONE;
    }
}
