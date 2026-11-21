/* The camera shifts: camera= in sh3.ini picks one, F2 cycles them in game.
 *
 * The game builds its own view matrix every frame and this layer draws with it. Moving the eye a little along
 * that view's forward, right and up axes is the closest an over the shoulder or a first person view gets without
 * touching the game's camera: what the player walks into, where the gun points, what the game culls and where
 * the menus sit all stay as they were, so the shift is purely visual. That is the trade: first person puts the
 * eye where Heather's head is but leaves her shadow and the game's own framing alone, and an object between the
 * game's camera and the shifted eye (a wall behind her) is still drawn, because the game drew it.
 */
#include "camera.h"
#include "freecam.h"
#include "freecam_math.h"
#include "../runtime/rt.h"

/* Offsets in the game's units, along forward, right and up. The presets are starting points: a player who wants
 * a different distance sets cam_forward= (and cam_right=, cam_up=) in sh3.ini, which win over all presets. */
static const char *const names[] = {"off", "over the shoulder", "first person"};
static const float preset[][3] = {
    {0.0f, 0.0f, 0.0f},
    {1.10f, 0.45f, 0.10f},
    {2.20f, 0.00f, 0.20f},
};

static void offsets(float *forward, float *right, float *up) {
    if (rt_cam_custom) {
        *forward = rt_cam_forward;
        *right = rt_cam_right;
        *up = rt_cam_up;
        return;
    }
    int m = rt_camera > 0 && rt_camera < 3 ? rt_camera : 0;
    *forward = preset[m][0];
    *right = preset[m][1];
    *up = preset[m][2];
}

int camera_active(void) { return rt_camera > 0 && rt_camera < 3 && !freecam_active(); }

void camera_view(float out[16], const float game[16]) {
    float forward, right, up;
    memcpy(out, game, 64);
    if (!camera_active()) return;
    offsets(&forward, &right, &up);
    freecam_view_shift(out, forward, right, up);
}

void camera_cycle(void) {
    rt_camera = (rt_camera + 1) % 3;
    float forward, right, up;
    offsets(&forward, &right, &up);
    rt_log("camera: %s (forward %.2f, right %.2f, up %.2f)", names[rt_camera], forward, right, up);
    if (rt_camera) rt_log("camera: the game's own camera stays put, so this moves the view only; cam_forward=, "
                          "cam_right= and cam_up= in sh3.ini set the distance");
}
