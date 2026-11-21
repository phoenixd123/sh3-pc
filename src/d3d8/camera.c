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
#include <SDL2/SDL.h>

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

static const char *const axis_names[] = {"forward", "right", "up"};

void camera_nudge(int axis, float delta) {
    if (!camera_active() || axis < 0 || axis > 2) return;
    if (!rt_cam_custom) {  /* start from the preset the mode was on, then it is the player's numbers */
        float forward, right, up;
        offsets(&forward, &right, &up);
        rt_cam_forward = forward;
        rt_cam_right = right;
        rt_cam_up = up;
        rt_cam_custom = 1;
    }
    float *v = axis == 0 ? &rt_cam_forward : axis == 1 ? &rt_cam_right : &rt_cam_up;
    *v += delta;
    if (*v < -20.0f) *v = -20.0f;
    if (*v > 20.0f) *v = 20.0f;
    rt_log("camera: %s %+.2f -> forward %.2f right %.2f up %.2f", axis_names[axis], delta,
           rt_cam_forward, rt_cam_right, rt_cam_up);
    rt_log("camera: sh3.ini  cam_forward=%.2f  cam_right=%.2f  cam_up=%.2f", rt_cam_forward, rt_cam_right, rt_cam_up);
}

void camera_filter_keys(uint8_t *keys, int count) {
    static const int owned[] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
                                SDL_SCANCODE_PAGEUP, SDL_SCANCODE_PAGEDOWN};
    if (!camera_active()) return;
    if (!((SDL_SCANCODE_LALT < count && keys[SDL_SCANCODE_LALT]) ||
          (SDL_SCANCODE_RALT < count && keys[SDL_SCANCODE_RALT]))) return;
    for (size_t i = 0; i < sizeof owned / sizeof *owned; i++)
        if (owned[i] < count) keys[owned[i]] = 0;
}

void camera_cycle(void) {
    rt_camera = (rt_camera + 1) % 3;
    rt_cam_custom = 0;  /* a preset comes back: F2 undoes the nudges */
    float forward, right, up;
    offsets(&forward, &right, &up);
    rt_log("camera: %s (forward %.2f, right %.2f, up %.2f)", names[rt_camera], forward, right, up);
    if (rt_camera) rt_log("camera: the game's own camera stays put, so this moves the view only; cam_forward=, "
                          "cam_right= and cam_up= in sh3.ini set the distance");
}
