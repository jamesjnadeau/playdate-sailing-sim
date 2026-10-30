#ifndef BOATCFG_H
#define BOATCFG_H
#include <stdbool.h>
#include <stddef.h>
#include "mathx.h"

/* Boat configuration. SI units; angles are DEGREES in the struct as in the
 * file (consumers convert with deg2rad). Every field has a registered
 * min/max/default in boatcfg.c, mirrored in docs/boat-schema.md. */

#define BOAT_MAX_SAILS 8
#define TABLE_MAX 16
#define NAME_LEN 16

typedef struct { int n; Pt p[TABLE_MAX]; } Table;   /* x strictly ascending */

typedef struct {
    float length, beam;
    float mass;              /* hull + rig, kg */
    float crew_mass;         /* hiking crew, kg */
    float hiking_arm;        /* crew lever arm at full hike, m */
    float hull_speed;        /* m/s, resistance wall */
    Table resistance;        /* speed m/s -> drag N */
    float heel_drag;         /* extra drag fraction per rad^2 of heel */
    float yaw_inertia, yaw_damping;
    float heel_inertia, heel_damping;
    Table righting;          /* heel deg -> righting moment N.m */
    float capsize_deg;
} HullCfg;

typedef struct {
    float board_area, board_lift_slope, board_stall_deg, board_cd0, board_x;
    float rudder_area, rudder_max_deg, rudder_lift_slope, rudder_stall_deg, rudder_x;
} FoilCfg;

typedef struct {
    char name[NAME_LEN];
    float area, luff, foot;
    float ce_x, ce_z;                     /* centre of effort, m from boat centre / waterline */
    float trim_min_deg, trim_max_deg;
    int reef_levels;
    float reef_frac;                      /* area kept per reef level */
    Table cl, cd;                         /* AoA deg -> coefficient */
    float luff_aoa_deg, luff_drag, flutter_hz;
    float slot_gain;                      /* CL boost this sail gives the next sail aft */
    float awa_min_deg, awa_max_deg;       /* apparent wind angle window where it works */
    int shape, mesh_w, mesh_h;            /* render: 0 main, 1 headsail, 2 spinnaker */
    float camber, shade, tack_x, tack_z;
} SailCfg;

typedef struct {
    float wheel_turns;
    float crank_gain_wheel, crank_gain_trim, crank_gain_hoist, crank_gain_camera;
    float rudder_rate_deg, trim_rate_deg, hoist_rate;
    float weather_helm;                   /* 0..1 feel of rudder load on the wheel */
    float anchor_time;
    float autopilot_hold_s;               /* hold A this long to toggle the autopilot */
    float autopilot_step_deg;             /* a tap of A advances the target heading by this */
    float autopilot_no_go_deg;            /* targets this close to the wind's source are skipped */
    float autopilot_kp, autopilot_kd;     /* wheel per rad of heading error / per rad/s of yaw rate */
} ControlsCfg;

typedef struct {
    Table hull_width;                     /* station 0 bow..1 stern -> half-width fraction */
    float freeboard, mast_x, mast_height, forestay_x, shroud_width;
    int wheel_style, panel_compass;
    float hull_shade;
} LookCfg;

typedef struct {
    float height, x_offset, heel_bias;
    float far_height;               /* trailing view: eye height, m */
    float frame_margin;             /* trailing view: px kept between mast/hull and the screen edge or status panel */
    float roll_limit_deg, roll_smooth, pitch_smooth;
    float heave_gain, pitch_gain;   /* share of the wave heave / pitch the camera follows (the boat keeps all of it) */
    float yaw_range_deg, return_rate;
} CameraCfg;

typedef struct {
    HullCfg hull;
    FoilCfg foils;
    SailCfg sails[BOAT_MAX_SAILS];
    int sail_count;
    ControlsCfg controls;
    LookCfg look;
    CameraCfg camera;
    float world_units_per_metre;
} BoatCfg;

void boatcfg_default(BoatCfg *out);       /* the built-in dinghy */

/* Always fills out (starting from the default). Returns false if any warning
 * was raised; warn gets "; "-separated messages naming the offending field. */
bool boatcfg_load(BoatCfg *out, const char *buf, size_t len, char *warn, size_t warnlen);

/* Serialises cfg as boat JSON; returns bytes written (0 if cap too small). */
size_t boatcfg_to_json(const BoatCfg *cfg, char *buf, size_t cap);

#endif
