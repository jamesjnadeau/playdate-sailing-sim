#ifndef CONTROLS_H
#define CONTROLS_H
#include <stdint.h>
#include "boatcfg.h"
#include "boat.h"

/* Button bits for the `pressed` (newly pressed this frame) and `held` masks. */
#define BTN_LEFT  (1u << 0)
#define BTN_RIGHT (1u << 1)
#define BTN_UP    (1u << 2)
#define BTN_DOWN  (1u << 3)
#define BTN_B     (1u << 4)
#define BTN_A     (1u << 5)

/* The crank drives whichever mode was last latched; no button needs holding.
 *   UP    -> WHEEL  (a second UP press eases the camera look back to 0 and ends buoy-follow)
 *   RIGHT -> TRIM  (re-press while in TRIM selects the next sail)
 *   LEFT  -> HOIST (re-press while in HOIST selects the next sail)
 *   DOWN  -> CAMERA look. Crank swings it all the way round the boat; a DOWN tap
 *            while already looking switches the crank between sideways and
 *            raise/lower; holding DOWN for controls.camera_hold_s toggles buoy-follow
 *            (camera yaw faces the next buoy) and behind-follow (yaw 0), swinging over
 *            camera.buoy_swing_s. A crank turn in CAMERA exits buoy-follow where the view
 *            is. Leaving CAMERA keeps the look and keeps following.
 * If several mode buttons are pressed in one frame the priority is
 * UP > RIGHT > LEFT > DOWN. The selected sail is shared by TRIM and HOIST. */
typedef enum { MODE_WHEEL, MODE_TRIM, MODE_HOIST, MODE_CAMERA } Mode;

typedef struct {
    Mode mode;
    int sail;                 /* selected sail index */
    float camera_yaw;         /* rad, what the camera uses */
    float camera_target_yaw;  /* rad, set by the crank in CAMERA mode; held when leaving it */
    float camera_pitch;       /* rad, raise (+) / lower (-) the camera about the boat */
    float camera_target_pitch;
    bool camera_vertical;     /* crank drives pitch instead of yaw */
    bool camera_recentre;     /* second UP: ease the look back to 0 */
    bool camera_follow;       /* buoy-follow: yaw faces the next buoy (pitch untouched) */
    float buoy_rel;           /* set by the game each frame: rad, bearing to next buoy minus heading; NAN = none */
    int buoy_id;              /* set by the game: changes when the target buoy changes */
    bool swing_active, swing_seen;   /* yaw blend in progress; first valid target seen since toggling */
    float swing_from, swing_t;       /* yaw the blend started from; seconds into it */
    float swing_delta;        /* blend distance, kept continuous as the target moves past +-pi */
    bool swing_dvalid;        /* swing_delta set since the blend (re)started */
    int swing_id;             /* buoy_id the blend is heading for */
    float down_time;          /* how long the current DOWN press has been held */
    bool down_tracking, down_fired, down_entered;   /* DOWN press state; entered = this press switched into CAMERA */
} Controls;

void controls_init(Controls *c);

/* Edits out->wheel_target / trim_target / hoist_target in place; out is
 * expected to persist between frames. A bad crank or dt is treated as 0. */
void controls_update(Controls *c, const BoatCfg *cfg, uint32_t pressed, uint32_t held,
                     float crank_delta_deg, float dt, BoatInputs *out);

#endif
