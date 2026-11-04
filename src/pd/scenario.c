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
    char *tok[6];
    int nt = 0;
    char *p = line;
    memset(c, 0, sizeof *c);
    while (*p && is_ws(*p)) p++;
    if (!strncmp(p, "caption", 7) && (is_ws(p[7]) || !p[7])) {
        /* the rest of the line is the text, spaces and all */
        char *t = p + 7, *e;
        c->kind = SC_C_CAPTION;
        while (*t && is_ws(*t)) t++;
        e = t + strlen(t);
        while (e > t && is_ws(e[-1])) *--e = 0;
        if (!*t) return "usage: caption TEXT|off";
        if (strlen(t) > SC_NAME_MAX - 1) return "caption too long";
        for (e = t; *e; e++)
            if (*e < 32 || *e > 126) return "caption: printable ASCII only";
        if (strcmp(t, "off")) strcpy(c->name, t);
        return NULL;
    }
    while (*p) {
        while (*p && is_ws(*p)) p++;
        if (!*p) break;
        if (nt == 6) return "too many arguments";
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
        if (nt < 2) return "usage: sysmenu back|boat N|weather N";
        if (!strcmp(tok[1], "back")) {
            if (nt != 2) return "usage: sysmenu back";
            c->sys = SC_SYS_BACK;
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
    } else if (!strcmp(tok[0], "record")) {
        c->kind = SC_C_RECORD;
        if (nt != 2 || !valid_name(tok[1])) return "usage: record NAME|off";
        c->n = strcmp(tok[1], "off") != 0;
        if (c->n) strcpy(c->name, tok[1]);
    } else if (!strcmp(tok[0], "hud")) {
        c->kind = SC_C_HUD;
        if (nt != 2 || (strcmp(tok[1], "on") && strcmp(tok[1], "off"))) return "usage: hud on|off";
        c->n = !strcmp(tok[1], "on");
    } else if (!strcmp(tok[0], "wind")) {
        c->kind = SC_C_WIND;
        if (nt != 2 || !parse_float(tok[1], &c->deg)) return "usage: wind DEG";
    } else if (!strcmp(tok[0], "orbit")) {
        c->kind = SC_C_ORBIT;
        if (nt != 2 || !parse_float(tok[1], &c->deg)) return "usage: orbit DEG_PER_S";
    } else if (!strcmp(tok[0], "buoy")) {
        c->kind = SC_C_BUOY;
        if (nt != 3 || !parse_float(tok[1], &c->v[0]) || !parse_float(tok[2], &c->v[1]))
            return "usage: buoy FWD RIGHT";
    } else if (!strcmp(tok[0], "fixcam")) {
        c->kind = SC_C_FIXCAM;
        if (nt == 2 && !strcmp(tok[1], "off")) return NULL;
        c->n = 1;
        if (nt != 5 && nt != 6) return "usage: fixcam DIST HEIGHT AIM PITCH [ALONG]|off";
        c->v[4] = 0;
        for (int k = 0; k < nt - 1; k++)
            if (!parse_float(tok[k + 1], &c->v[k])) return "usage: fixcam DIST HEIGHT AIM PITCH [ALONG]|off";
        if (c->v[0] == 0 || c->v[1] <= 0) return "fixcam: DIST must be non-zero and HEIGHT positive";
    } else if (!strcmp(tok[0], "ghost")) {
        c->kind = SC_C_GHOST;
        if (nt != 2 || (strcmp(tok[1], "on") && strcmp(tok[1], "off"))) return "usage: ghost on|off";
        c->n = !strcmp(tok[1], "on");
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

/* Zero-time commands that attach to the next frame. False for any other kind. */
static bool take_pending(Scenario *s, const ScCmd *c) {
    ScShot *p = &s->pend_shot;
    switch (c->kind) {
    case SC_C_SYS: s->pend_sys = c->sys; s->pend_arg = c->n; strcpy(s->pend_name, c->name); return true;
    case SC_C_PRAISE: s->pend_praise = true; s->pend_praise_idx = c->n; return true;
    case SC_C_RECORD:
        p->rec = c->n ? SC_REC_ON : SC_REC_OFF;
        strcpy(p->rec_name, c->n ? c->name : "");
        return true;
    case SC_C_HUD: p->hud = c->n ? SC_HUD_ON : SC_HUD_OFF; return true;
    case SC_C_WIND: p->wind_now = true; p->wind_deg = c->deg; return true;
    case SC_C_ORBIT: p->orbit_now = true; p->orbit_dps = c->deg; return true;
    case SC_C_CAPTION: p->caption_now = true; strcpy(p->caption, c->name); return true;
    case SC_C_BUOY: p->buoy_now = true; p->buoy_fwd = c->v[0]; p->buoy_right = c->v[1]; return true;
    case SC_C_FIXCAM:
        p->fixcam = c->n ? 1 : 2;
        p->fix_dist = c->v[0]; p->fix_height = c->v[1]; p->fix_aim = c->v[2]; p->fix_pitch = c->v[3]; p->fix_along = c->v[4];
        return true;
    case SC_C_GHOST: p->ghost = c->n ? 1 : 2; return true;
    default: return false;
    }
}

bool scenario_poll(Scenario *s, ScFrame *f) {
    const ScCmd *c;
    uint32_t cur = 0;
    float crank = 0.0f;
    /* skip zero-time commands; sysmenu attaches to the next frame */
    while (s->pc < s->ncmds && !is_frame_cmd(s->cmds[s->pc].kind)) {
        c = &s->cmds[s->pc];
        if (c->kind == SC_C_END) { s->pc = s->ncmds; break; }
        take_pending(s, c);
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
    f->shot = s->pend_shot;
    memset(&s->pend_shot, 0, sizeof s->pend_shot);
    s->prev = cur;
    if (++s->k >= c->n) { s->pc++; s->k = 0; }
    return true;
}

ScAfterKind scenario_after(Scenario *s, char name[SC_NAME_MAX]) {
    const ScCmd *c;
    if (s->k != 0) return SC_AFTER_NONE;          /* mid-command */
    /* sysmenu, praise, record, hud, wind and orbit are consumed into the pending actions so
       snap/state behind them still fire */
    while (s->pc < s->ncmds && take_pending(s, &s->cmds[s->pc])) s->pc++;
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
