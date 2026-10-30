#ifndef CONTROLS_H
#define CONTROLS_H
#include <stdint.h>
#include "boatcfg.h"
#include "boat.h"

/* Button bits for the `pressed` mask (buttons newly pressed this frame). */
#define BTN_LEFT  (1u << 0)
#define BTN_RIGHT (1u << 1)
#define BTN_UP    (1u << 2)
#define BTN_DOWN  (1u << 3)
#define BTN_B     (1u << 4)
#define BTN_A     (1u << 5)

/* The crank drives whichever mode was last latched; no button needs holding.
 *   UP    -> WHEEL
 *   RIGHT -> TRIM  (re-press while in TRIM selects the next sail)
 *   LEFT  -> HOIST (re-press while in HOIST selects the next sail)
 *   DOWN  -> CAMERA yaw (returns to 0 at camera.return_rate once left)
 * If several mode buttons are pressed in one frame the priority is
 * UP > RIGHT > LEFT > DOWN. The selected sail is shared by TRIM and HOIST. */
typedef enum { MODE_WHEEL, MODE_TRIM, MODE_HOIST, MODE_CAMERA } Mode;

typedef struct {
    Mode mode;
    int sail;                 /* selected sail index */
    float camera_yaw;         /* rad, what the camera uses */
    float camera_target_yaw;  /* rad, set by the crank in CAMERA mode; held when leaving it */
    bool camera_recentre;     /* DOWN pressed again in CAMERA mode: ease the look back to 0 */
} Controls;

void controls_init(Controls *c);

/* Edits out->wheel_target / trim_target / hoist_target in place; out is
 * expected to persist between frames. A bad crank or dt is treated as 0. */
void controls_update(Controls *c, const BoatCfg *cfg, uint32_t pressed,
                     float crank_delta_deg, float dt, BoatInputs *out);

#endif
