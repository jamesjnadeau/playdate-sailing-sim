#include <string.h>
#include "hudinfo.h"
#include <math.h>
#include <stdio.h>
#include "mathx.h"

#define R2D 57.29578f

#define CL_GAP_ICON 3     /* icon to its text */
#define CL_GAP_SEP_L 5    /* separator to the cell on its right edge... */
#define CL_GAP_SEP_R 4    /* ...and to the text on its left */

HudCluster hud_cluster_layout(int w_aws, int w_heel, int w_hdg, int w_speed, int w_ap, bool at, bool ap) {
    HudCluster c;
    memset(&c, 0, sizeof c);
    c.ap_x = c.ap_icon_x = c.at_icon_x = -1;
    c.aws_x = 376 - w_aws;
    c.sep[c.nsep++] = c.aws_x - CL_GAP_SEP_L;
    c.heel_x = c.sep[c.nsep - 1] - CL_GAP_SEP_R - w_heel;
    c.heel_icon_x = c.heel_x - CL_GAP_ICON - HUD_ICON_HEEL_W;
    c.sep[c.nsep++] = c.heel_icon_x - CL_GAP_SEP_L;
    c.hdg_x = c.sep[c.nsep - 1] - CL_GAP_SEP_R - w_hdg;
    c.hdg_icon_x = c.hdg_x - CL_GAP_ICON - HUD_ICON_HDG_W;
    c.sep[c.nsep++] = c.hdg_icon_x - CL_GAP_SEP_L;
    c.speed_x = c.sep[c.nsep - 1] - CL_GAP_SEP_R - w_speed;
    c.left = c.speed_x;
    if (ap || at) c.sep[c.nsep++] = c.speed_x - CL_GAP_SEP_L;
    int edge = c.speed_x - CL_GAP_SEP_L - CL_GAP_SEP_R;   /* right edge of the next element */
    if (ap) {
        c.ap_x = edge - w_ap;
        c.ap_icon_x = c.ap_x - CL_GAP_ICON - HUD_ICON_AP_W;
        edge = c.ap_icon_x - CL_GAP_SEP_R;
        c.left = c.ap_icon_x;
    }
    if (at) {
        c.at_icon_x = edge - HUD_ICON_AT_W;
        c.left = c.at_icon_x;
    }
    return c;
}

void hud_sail_bar_span(int name_w, int left, int *x, int *w) {
    *x = 60 + name_w + 4;
    if (*x < 100) *x = 100;
    *w = left - 6 - *x;
    if (*w > 64) *w = 64;
    if (*w < 24) *w = 0;
}

int hud_heading_deg(float rad) {
    if (!isfinite(rad)) return 0;
    float d = fmodf(rad * R2D, 360.0f);
    if (d < 0.0f) d += 360.0f;
    int i = (int)floorf(d + 0.5f) % 360;
    return i < 0 ? 0 : i;
}

float hud_sail_fraction(Mode m, const SailCfg *s, float trim_rad, float hoist) {
    if (m == MODE_HOIST) return isfinite(hoist) ? clampf(hoist, 0.0f, 1.0f) : 0.0f;
    if (m != MODE_TRIM || !isfinite(trim_rad)) return 0.0f;
    float range = s->trim_max_deg - s->trim_min_deg;
    if (!(range > 0.0f)) return 0.0f;
    return clampf((trim_rad * R2D - s->trim_min_deg) / range, 0.0f, 1.0f);
}

void hud_fmt_time(double t, char *out, size_t n) {
    if (!isfinite(t) || t < 0.0) t = 0.0;
    if (t > 6000000.0) t = 6000000.0;          /* cap so the minute count fits an int */
    long tenths = (long)(t * 10.0 + 0.5);      /* round once so 59.96 s never prints as 60.0 */
    snprintf(out, n, "%02ld:%02ld.%ld", tenths / 600, (tenths % 600) / 10, tenths % 10);
}

void hud_fmt_mark(const char *name, int index, int total, double dist_m, char *out, size_t n) {
    if (!isfinite(dist_m) || dist_m < 0.0) dist_m = 0.0;
    if (dist_m > 1e9) dist_m = 1e9;
    snprintf(out, n, "%s %d/%d  %.0fm", name ? name : "", index, total, dist_m);
}

const char *hud_assist_text(float rudder_assist, float wheel) {
    if (!isfinite(rudder_assist) || !isfinite(wheel)) return NULL;
    return rudder_assist > 0.5f && fabsf(wheel) > 0.1f ? "RUDDER ASSIST: LOW SPEED" : NULL;
}

const char *hud_engine_text(bool running) {
    return running ? "ENGINE ENGAGED" : NULL;
}

float hud_dial_angle(float angle_from_bow, float look_yaw) {
    if (!isfinite(look_yaw)) look_yaw = 0.0f;
    return wrap_pi(angle_from_bow - look_yaw);
}
