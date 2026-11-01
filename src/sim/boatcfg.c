#include "boatcfg.h"
#include "json.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- field descriptors: one line per field (key == C member name) ---- */

typedef enum { K_F, K_I, K_S, K_T } Kind;

typedef struct {
    const char *key;
    Kind kind;
    size_t off;
    float def, mn, mx;
    const char *sdef;
    const Pt *tdef;
    int tn;
} Field;

#define FF(S, f, d, lo, hi) { #f, K_F, offsetof(S, f), d, lo, hi, 0, 0, 0 }
#define FI(S, f, d, lo, hi) { #f, K_I, offsetof(S, f), d, lo, hi, 0, 0, 0 }
#define FS(S, f, d)         { #f, K_S, offsetof(S, f), 0, 0, 0, d, 0, 0 }
#define FT(S, f, arr)       { #f, K_T, offsetof(S, f), 0, 0, 0, 0, arr, (int)(sizeof arr / sizeof arr[0]) }
#define COUNT(a) ((int)(sizeof a / sizeof a[0]))

#define RUDDER_FLOW_FRAC 0.15f

static const Pt RESISTANCE[] = { {0, 0}, {1, 8}, {2, 35}, {3, 90}, {4, 220}, {6, 700} };
static const Pt RIGHTING[]   = { {0, 0}, {10, 390}, {25, 780}, {45, 960}, {70, 675}, {90, 0} };
static const Pt SAIL_CL[]    = { {0, 0}, {5, 0.5f}, {10, 1.0f}, {15, 1.35f}, {18, 1.4f}, {25, 1.1f},
                                 {40, 0.9f}, {60, 0.7f}, {75, 0.35f}, {90, 0.1f}, {120, 0}, {180, 0} };
static const Pt SAIL_CD[]    = { {0, 0.08f}, {10, 0.17f}, {18, 0.3f}, {30, 0.55f}, {60, 1.0f},
                                 {90, 1.3f}, {180, 1.1f} };
static const Pt HULL_WIDTH[] = { {0, 0}, {0.15f, 0.55f}, {0.5f, 1}, {0.85f, 0.9f}, {1, 0.75f} };

static const Field HULL_F[] = {
    FF(HullCfg, length, 4.0f, 1, 30),
    FF(HullCfg, beam, 1.5f, 0.3f, 6),
    FF(HullCfg, mass, 120, 5, 20000),
    FF(HullCfg, crew_mass, 80, 0, 500),
    FF(HullCfg, hiking_arm, 1.0f, 0, 3),
    FF(HullCfg, hull_speed, 3.2f, 0.5f, 15),
    FT(HullCfg, resistance, RESISTANCE),
    FF(HullCfg, heel_drag, 0.5f, 0, 10),
    FF(HullCfg, clr_depth, 0.4f, 0, 6),
    FF(HullCfg, yaw_inertia, 90, 1, 1e5f),
    FF(HullCfg, yaw_damping, 120, 0, 1e5f),
    FF(HullCfg, heel_inertia, 140, 1, 1e5f),
    FF(HullCfg, heel_damping, 180, 0, 1e5f),
    FT(HullCfg, righting, RIGHTING),
    FF(HullCfg, capsize_deg, 70, 20, 120),
};

static const Field FOIL_F[] = {
    FF(FoilCfg, board_area, 0.14f, 0, 5),
    FF(FoilCfg, board_lift_slope, 4.5f, 0, 10),
    FF(FoilCfg, board_stall_deg, 14, 3, 45),
    FF(FoilCfg, board_cd0, 0.015f, 0, 1),
    FF(FoilCfg, board_x, 0.2f, -10, 10),
    FF(FoilCfg, rudder_area, 0.06f, 0, 2),
    FF(FoilCfg, rudder_max_deg, 35, 1, 80),
    FF(FoilCfg, rudder_lift_slope, 4.0f, 0, 10),
    FF(FoilCfg, rudder_stall_deg, 16, 3, 45),
    FF(FoilCfg, rudder_x, -2.0f, -15, 15),
    FF(FoilCfg, rudder_min_flow, 0.48f, 0, 5),   /* default is rescaled from hull_speed below */
};

/* Defaults here describe the dinghy's mainsail; the jib overrides in boatcfg_default. */
static const Field SAIL_F[] = {
    FS(SailCfg, name, "sail"),
    FF(SailCfg, area, 4.6f, 0.1f, 200),
    FF(SailCfg, luff, 4.6f, 0.2f, 40),
    FF(SailCfg, foot, 2.0f, 0.2f, 20),
    FF(SailCfg, ce_x, -0.35f, -20, 20),
    FF(SailCfg, ce_z, 2.0f, 0, 40),
    FF(SailCfg, trim_min_deg, 3, 0, 90),
    FF(SailCfg, trim_max_deg, 85, 0, 90),
    FI(SailCfg, reef_levels, 1, 0, 3),
    FF(SailCfg, reef_frac, 0.7f, 0.2f, 1),
    FT(SailCfg, cl, SAIL_CL),
    FT(SailCfg, cd, SAIL_CD),
    FF(SailCfg, luff_aoa_deg, 4, 0, 20),
    FF(SailCfg, luff_drag, 0.15f, 0, 2),
    FF(SailCfg, flutter_hz, 6, 0, 30),
    FF(SailCfg, slot_gain, 0, 0, 1),
    FF(SailCfg, awa_min_deg, 0, 0, 180),
    FF(SailCfg, awa_max_deg, 180, 0, 180),
    FI(SailCfg, shape, 0, 0, 3),
    FI(SailCfg, mesh_w, 6, 2, 16),
    FI(SailCfg, mesh_h, 8, 2, 16),
    FF(SailCfg, camber, 0.10f, 0, 0.3f),
    FF(SailCfg, shade, 0.5f, 0, 1),
    FF(SailCfg, tack_x, 0.3f, -20, 20),
    FF(SailCfg, tack_z, 0.5f, 0, 20),
};

static const Field CONTROLS_F[] = {
    FF(ControlsCfg, wheel_turns, 2, 0.25f, 10),
    FF(ControlsCfg, crank_gain_wheel, 1.0f, 0.01f, 10),
    FF(ControlsCfg, crank_gain_trim, 0.3f, 0.01f, 10),
    FF(ControlsCfg, crank_gain_hoist, 0.003f, 0.0001f, 0.1f),
    FF(ControlsCfg, crank_gain_camera, 0.5f, 0.01f, 10),
    FF(ControlsCfg, rudder_rate_deg, 90, 1, 720),
    FF(ControlsCfg, trim_rate_deg, 60, 1, 720),
    FF(ControlsCfg, hoist_rate, 0.25f, 0.01f, 5),
    FF(ControlsCfg, weather_helm, 0.5f, 0, 1),
    FF(ControlsCfg, anchor_time, 2.5f, 0.1f, 30),
    FF(ControlsCfg, camera_hold_s, 0.75f, 0.1f, 3),
    FF(ControlsCfg, autopilot_hold_s, 0.6f, 0.1f, 3),
    FF(ControlsCfg, autopilot_step_deg, 45, 5, 180),
    FF(ControlsCfg, autopilot_no_go_deg, 45, 0, 90),
    FF(ControlsCfg, autopilot_crank_deg, 10, 0, 90),
    FF(ControlsCfg, autopilot_kp, 2.0f, 0, 20),
    FF(ControlsCfg, autopilot_kd, 1.0f, 0, 20),
    FF(ControlsCfg, autotrim_hold_s, 0.6f, 0.1f, 3),
    FF(ControlsCfg, autotrim_smooth_s, 4.0f, 0.1f, 30),
    FF(ControlsCfg, autotrim_heel_deg, 25, 5, 90),
    FF(ControlsCfg, autotrim_hoist_min, 0.3f, 0.05f, 1),
};

static const Field LOOK_F[] = {
    FT(LookCfg, hull_width, HULL_WIDTH),
    FF(LookCfg, freeboard, 0.45f, 0.05f, 3),
    FF(LookCfg, mast_x, 0.3f, -10, 10),
    FF(LookCfg, mast_height, 5.2f, 0.5f, 40),
    FF(LookCfg, forestay_x, 1.8f, -10, 20),
    FF(LookCfg, shroud_width, 0.6f, 0, 5),
    FI(LookCfg, wheel_style, 1, 0, 1),
    FI(LookCfg, panel_compass, 1, 0, 1),
    FF(LookCfg, hull_shade, 0.35f, 0, 1),
    FF(LookCfg, cockpit_x0, -1.4f, -20, 20),
    FF(LookCfg, cockpit_x1, 0.2f, -20, 20),
    FF(LookCfg, cockpit_w0, 0.4f, 0, 5),
    FF(LookCfg, cockpit_w1, 0.5f, 0, 5),
    FF(LookCfg, cockpit_depth, 0.25f, 0, 2),
    FF(LookCfg, wake_fade_m, 4.0f, 0, 20),
};

static const Field CAMERA_F[] = {
    FF(CameraCfg, height, 1.2f, 0.2f, 10),
    FF(CameraCfg, x_offset, -2.7f, -10, 10),
    FF(CameraCfg, heel_bias, 0.25f, -3, 3),
    FF(CameraCfg, frame_margin, 20.0f, 0, 80),
    FF(CameraCfg, horizon_y, 0.0f, 0, 200),
    FF(CameraCfg, trail_dist, 0.0f, 0, 200),
    FF(CameraCfg, far_height, 2.5f, 0.2f, 20),
    FF(CameraCfg, roll_limit_deg, 10, 0, 45),
    FF(CameraCfg, roll_smooth, 0.4f, 0, 3),
    FF(CameraCfg, pitch_smooth, 0.3f, 0, 3),
    FF(CameraCfg, heave_gain, 0.3f, 0, 1),
    FF(CameraCfg, pitch_gain, 0.4f, 0, 1),
    FF(CameraCfg, pitch_range_deg, 45, 5, 80),
    FF(CameraCfg, return_rate, 90, 1, 720),
};

static const Field TOP_F[] = {
    FF(BoatCfg, world_units_per_metre, 10, 0.1f, 1000),
    FI(BoatCfg, boat_class, 0, 0, 1),
};

typedef struct {
    const char *name;
    size_t off;
    const Field *f;
    int n;
} Group;

static const Group GROUPS[] = {
    { "hull",     offsetof(BoatCfg, hull),     HULL_F,     COUNT(HULL_F) },
    { "foils",    offsetof(BoatCfg, foils),    FOIL_F,     COUNT(FOIL_F) },
    { "controls", offsetof(BoatCfg, controls), CONTROLS_F, COUNT(CONTROLS_F) },
    { "look",     offsetof(BoatCfg, look),     LOOK_F,     COUNT(LOOK_F) },
    { "camera",   offsetof(BoatCfg, camera),   CAMERA_F,   COUNT(CAMERA_F) },
};

/* ---- defaults ---- */

static void set_table(Table *t, const Pt *p, int n) {
    memset(t, 0, sizeof *t);
    t->n = n;
    memcpy(t->p, p, (size_t)n * sizeof(Pt));
}

static void set_name(char *dst, const char *s) {
    memset(dst, 0, NAME_LEN);
    strncpy(dst, s, NAME_LEN - 1);
}

static void apply_defaults(void *base, const Field *f, int n) {
    for (int i = 0; i < n; i++) {
        char *p = (char *)base + f[i].off;
        switch (f[i].kind) {
        case K_F: *(float *)p = f[i].def; break;
        case K_I: *(int *)p = (int)f[i].def; break;
        case K_S: set_name(p, f[i].sdef); break;
        case K_T: set_table((Table *)p, f[i].tdef, f[i].tn); break;
        }
    }
}

void boatcfg_default(BoatCfg *o) {
    memset(o, 0, sizeof *o);
    for (int g = 0; g < COUNT(GROUPS); g++)
        apply_defaults((char *)o + GROUPS[g].off, GROUPS[g].f, GROUPS[g].n);
    apply_defaults(o, TOP_F, COUNT(TOP_F));

    o->foils.rudder_min_flow = RUDDER_FLOW_FRAC * o->hull.hull_speed;

    SailCfg *m = &o->sails[0], *j = &o->sails[1];
    apply_defaults(m, SAIL_F, COUNT(SAIL_F));
    apply_defaults(j, SAIL_F, COUNT(SAIL_F));
    set_name(m->name, "main");
    set_name(j->name, "jib");
    j->area = 2.2f; j->luff = 3.4f; j->foot = 1.3f;
    j->ce_x = 1.1f; j->ce_z = 1.5f;
    j->trim_min_deg = 5; j->trim_max_deg = 60;
    j->reef_levels = 0;
    j->slot_gain = 0.15f;
    j->shape = 1; j->mesh_w = 4; j->mesh_h = 6;
    j->shade = 0.7f; j->tack_x = 1.7f; j->tack_z = 0.4f;
    o->sail_count = 2;
}

/* ---- warnings ---- */

typedef struct { char *buf; size_t cap, n; bool any; } Warn;

static void wadd(Warn *w, const char *fmt, ...) {
    w->any = true;
    if (!w->buf || w->cap < 2) return;
    va_list ap;
    if (w->n) {
        if (w->n + 3 >= w->cap) return;
        w->buf[w->n++] = ';';
        w->buf[w->n++] = ' ';
    }
    va_start(ap, fmt);
    int k = vsnprintf(w->buf + w->n, w->cap - w->n, fmt, ap);
    va_end(ap);
    if (k < 0) { w->buf[w->n] = 0; return; }
    w->n += (size_t)k < w->cap - w->n ? (size_t)k : w->cap - w->n - 1;
}

/* ---- loading ---- */

static void load_table(const JsonNode *node, Table *dst, const char *path, Warn *w) {
    if (!json_is_array(node)) { wadd(w, "%s: expected array of [x,y] pairs", path); return; }
    int len = json_array_len(node);
    if (len < 2 || len > TABLE_MAX) {
        wadd(w, "%s: needs 2..%d points, got %d", path, TABLE_MAX, len);
        return;
    }
    Table tmp;
    memset(&tmp, 0, sizeof tmp);
    for (int i = 0; i < len; i++) {
        const JsonNode *e = json_array_at(node, i);
        const JsonNode *x = json_is_array(e) && json_array_len(e) == 2 ? json_array_at(e, 0) : NULL;
        const JsonNode *y = x ? json_array_at(e, 1) : NULL;
        bool ok = x && json_is_number(x) && json_is_number(y);
        float fx = ok ? json_num(x, 0) : 0, fy = ok ? json_num(y, 0) : 0;
        ok = ok && isfinite(fx) && isfinite(fy) && (i == 0 || fx > tmp.p[i - 1].x);
        if (!ok) {
            wadd(w, "%s: bad point %d (need finite [x,y], x strictly ascending)", path, i);
            return;
        }
        tmp.p[i].x = fx;
        tmp.p[i].y = fy;
    }
    tmp.n = len;
    memcpy(dst, &tmp, sizeof tmp);
}

static void load_fields(const JsonNode *obj, void *base, const Field *f, int n,
                        const char *group, Warn *w) {
    for (int i = 0; i < n; i++) {
        const JsonNode *node = json_get(obj, f[i].key);
        if (!node) continue;
        char path[64];
        if (group[0]) snprintf(path, sizeof path, "%s.%s", group, f[i].key);
        else snprintf(path, sizeof path, "%s", f[i].key);
        char *p = (char *)base + f[i].off;
        switch (f[i].kind) {
        case K_F:
        case K_I: {
            if (!json_is_number(node)) { wadd(w, "%s: expected number", path); break; }
            float v = json_num(node, f[i].def);
            if (!isfinite(v)) { wadd(w, "%s: not a finite number", path); break; }
            if (f[i].kind == K_I) v = floorf(v + 0.5f);
            if (v < f[i].mn || v > f[i].mx) {
                wadd(w, "%s: %g outside [%g, %g], clamped", path, v, f[i].mn, f[i].mx);
                v = clampf(v, f[i].mn, f[i].mx);
            }
            if (f[i].kind == K_F) *(float *)p = v; else *(int *)p = (int)v;
            break;
        }
        case K_S:
            if (!json_is_string(node)) { wadd(w, "%s: expected string", path); break; }
            set_name(p, json_str(node, f[i].sdef));
            break;
        case K_T:
            load_table(node, (Table *)p, path, w);
            break;
        }
    }
}

static void load_sails(const JsonNode *sn, BoatCfg *out, Warn *w) {
    if (!json_is_array(sn)) { wadd(w, "sails: expected array; using default rig"); return; }
    int n = json_array_len(sn);
    if (n == 0) { wadd(w, "sails: empty; using default rig"); return; }
    if (n > BOAT_MAX_SAILS) {
        wadd(w, "sails: %d entries, keeping %d", n, BOAT_MAX_SAILS);
        n = BOAT_MAX_SAILS;
    }
    memset(out->sails, 0, sizeof out->sails);
    out->sail_count = n;
    for (int i = 0; i < n; i++) {
        char path[24];
        snprintf(path, sizeof path, "sails[%d]", i);
        SailCfg *s = &out->sails[i];
        apply_defaults(s, SAIL_F, COUNT(SAIL_F));
        const JsonNode *e = json_array_at(sn, i);
        if (!json_is_object(e)) { wadd(w, "%s: expected object", path); continue; }
        load_fields(e, s, SAIL_F, COUNT(SAIL_F), path, w);
        if (s->trim_min_deg > s->trim_max_deg) {
            wadd(w, "%s.trim_min_deg > trim_max_deg; reset", path);
            s->trim_min_deg = 3; s->trim_max_deg = 85;
        }
        if (s->awa_min_deg > s->awa_max_deg) {
            wadd(w, "%s.awa_min_deg > awa_max_deg; reset", path);
            s->awa_min_deg = 0; s->awa_max_deg = 180;
        }
    }
}

bool boatcfg_load(BoatCfg *out, const char *buf, size_t len, char *warn, size_t warnlen) {
    Warn w = { warn, warnlen, 0, false };
    if (warn && warnlen) warn[0] = 0;
    boatcfg_default(out);
    if (!buf || len == 0) { wadd(&w, "no config data"); return false; }

    char err[96];
    JsonDoc *doc = json_parse(buf, len, err, sizeof err);
    if (!doc) { wadd(&w, "parse error: %s", err); return false; }
    const JsonNode *root = json_root(doc);
    if (!json_is_object(root)) {
        wadd(&w, "root: expected object");
        json_free(doc);
        return false;
    }

    load_fields(root, out, TOP_F, COUNT(TOP_F), "", &w);
    for (int g = 0; g < COUNT(GROUPS); g++) {
        const JsonNode *gn = json_get(root, GROUPS[g].name);
        if (!gn) continue;
        if (!json_is_object(gn)) { wadd(&w, "%s: expected object", GROUPS[g].name); continue; }
        load_fields(gn, (char *)out + GROUPS[g].off, GROUPS[g].f, GROUPS[g].n, GROUPS[g].name, &w);
    }
    /* an omitted rudder_min_flow follows the loaded hull speed rather than the dinghy's */
    const JsonNode *fn = json_get(root, "foils");
    if (!(json_is_object(fn) && json_get(fn, "rudder_min_flow")))
        out->foils.rudder_min_flow = RUDDER_FLOW_FRAC * out->hull.hull_speed;
    const JsonNode *sn = json_get(root, "sails");
    if (sn) load_sails(sn, out, &w);

    json_free(doc);
    return !w.any;
}

/* ---- serialising (generates boats/dinghy.json from the built-in dinghy) ---- */

typedef struct { char *b; size_t cap, n; bool over; } Out;

static void wp(Out *o, const char *fmt, ...) {
    if (o->over) return;
    va_list ap;
    va_start(ap, fmt);
    int k = o->cap > o->n ? vsnprintf(o->b + o->n, o->cap - o->n, fmt, ap) : -1;
    va_end(ap);
    if (k < 0 || (size_t)k >= o->cap - o->n) o->over = true;
    else o->n += (size_t)k;
}

/* shortest %g form that reads back to the same float */
static void fmtf(char *dst, float v) {
    for (int prec = 0; prec <= 6; prec++) {
        snprintf(dst, 32, "%.*f", prec, (double)v);
        if (strtof(dst, NULL) == v) return;
    }
    for (int prec = 1; prec <= 9; prec++) {
        snprintf(dst, 32, "%.*g", prec, (double)v);
        if (strtof(dst, NULL) == v) return;
    }
}

static void write_fields(Out *o, const void *base, const Field *f, int n, const char *ind) {
    char num[32];
    for (int i = 0; i < n; i++) {
        const char *p = (const char *)base + f[i].off;
        wp(o, "%s\n%s\"%s\": ", i ? "," : "", ind, f[i].key);
        switch (f[i].kind) {
        case K_F: fmtf(num, *(const float *)p); wp(o, "%s", num); break;
        case K_I: wp(o, "%d", *(const int *)p); break;
        case K_S: {
            wp(o, "\"");
            for (int k = 0; k < NAME_LEN && p[k]; k++) {
                char c = p[k];
                if (c == '"' || c == '\\') wp(o, "\\%c", c);
                else wp(o, "%c", (unsigned char)c < 32 ? '?' : c);
            }
            wp(o, "\"");
            break;
        }
        case K_T: {
            const Table *t = (const Table *)p;
            wp(o, "[");
            for (int k = 0; k < t->n; k++) {
                char xs[32], ys[32];
                fmtf(xs, t->p[k].x);
                fmtf(ys, t->p[k].y);
                wp(o, "%s[%s, %s]", k ? ", " : "", xs, ys);
            }
            wp(o, "]");
            break;
        }
        }
    }
}

size_t boatcfg_to_json(const BoatCfg *cfg, char *buf, size_t cap) {
    Out o = { buf, cap, 0, false };
    wp(&o, "{");
    write_fields(&o, cfg, TOP_F, COUNT(TOP_F), "  ");
    for (int g = 0; g < COUNT(GROUPS); g++) {
        wp(&o, ",\n  \"%s\": {", GROUPS[g].name);
        write_fields(&o, (const char *)cfg + GROUPS[g].off, GROUPS[g].f, GROUPS[g].n, "    ");
        wp(&o, "\n  }");
    }
    wp(&o, ",\n  \"sails\": [");
    for (int i = 0; i < cfg->sail_count; i++) {
        wp(&o, "%s\n    {", i ? "," : "");
        write_fields(&o, &cfg->sails[i], SAIL_F, COUNT(SAIL_F), "      ");
        wp(&o, "\n    }");
    }
    wp(&o, "\n  ]\n}\n");
    return o.over ? 0 : o.n;
}
