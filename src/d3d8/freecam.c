#include "freecam.h"
#include "freecam_math.h"
#include "../runtime/rt.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <string.h>

#define LOOK_SPEED 0.0022f
#define MOVE_SPEED 6.0f
#define FAST_MULT 4.0f
#define SLOW_MULT 0.25f
#define PITCH_LIMIT 1.5533f   /* 89 degrees */
#define GAME_FRAME_MS 33      /* the game's own 30 Hz step */

static int active, adopt_next, frozen;
static float pos[3] = {0, 2, 0}, yaw, pitch, fov = 1.0f;
static Uint64 last_tick;

int freecam_active(void) { return active; }

void freecam_toggle(void) {
    active = !active;
    if (active) {
        adopt_next = 1;
        last_tick = SDL_GetTicks64();
        if (SDL_SetRelativeMouseMode(SDL_TRUE) != 0)
            rt_log("freecam: the mouse could not be captured (%s), the view still turns", SDL_GetError());
        rt_clock_freeze(1);
        frozen = 1;
        if (rt_window) SDL_SetWindowTitle(rt_window, "Silent Hill 3 - freecam (F1)");
        rt_log("freecam on: WASD move, Q/E down/up, Shift faster, Ctrl slower, mouse look, wheel FOV");
        rt_log("freecam: , and . step one frame, [ and ] step ten; F1 returns to the game");
    } else {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        if (frozen) { rt_clock_freeze(0); frozen = 0; }
        if (rt_window) SDL_SetWindowTitle(rt_window, "Silent Hill 3");
        rt_log("freecam off");
    }
}

void freecam_view(const float *game, float out[16]) {
    if (adopt_next) {
        float p[3], y, pi;
        if (freecam_view_adopt(game, p, &y, &pi)) {
            memcpy(pos, p, sizeof pos);
            yaw = y;
            pitch = pi;
        }
        adopt_next = 0;
    }
    freecam_view_build(out, pos, yaw, pitch);
}

void freecam_projection(float out[16], const float *game) {
    /* Only a perspective projection is scaled: the HUD is drawn with an orthographic one, which has to stay
     * the size it is. A D3D projection has 1 at [11] when it is perspective and 0 when it is orthographic. */
    if (active && game[11] != 0.0f) freecam_proj_scale(out, game, fov);
    else memcpy(out, game, 64);
}

void freecam_frame(void) {
    if (!active) return;
    Uint64 now = SDL_GetTicks64();
    float dt = (float)(now - last_tick) / 1000.0f;
    last_tick = now;
    if (dt > 0.1f) dt = 0.1f;  /* a stalled frame must not teleport the camera */

    int mx = 0, my = 0;
    SDL_GetRelativeMouseState(&mx, &my);
    yaw += (float)mx * LOOK_SPEED;
    pitch -= (float)my * LOOK_SPEED;
    if (pitch > PITCH_LIMIT) pitch = PITCH_LIMIT;
    if (pitch < -PITCH_LIMIT) pitch = -PITCH_LIMIT;

    const Uint8 *k = SDL_GetKeyboardState(NULL);
    float step = (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) ? FAST_MULT
               : (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) ? SLOW_MULT : 1.0f;
    float forward = ((k[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_S] ? 1.0f : 0.0f)) * MOVE_SPEED * step * dt;
    float side = ((k[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_A] ? 1.0f : 0.0f)) * MOVE_SPEED * step * dt;
    float lift = ((k[SDL_SCANCODE_E] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_Q] ? 1.0f : 0.0f)) * MOVE_SPEED * step * dt;

    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    float f[3] = {cp * sy, sp, cp * cy};
    float r[3] = {f[2], 0.0f, -f[0]};
    float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
    if (rl > 1e-6f) { r[0] /= rl; r[2] /= rl; }
    for (int i = 0; i < 3; i++) pos[i] += f[i] * forward + r[i] * side;
    pos[1] += lift;
}

int freecam_wheel(int steps) {
    if (!active || !steps) return 0;
    fov *= powf(0.9f, (float)steps);   /* wheel up narrows the view, wheel down widens it */
    if (fov < 0.25f) fov = 0.25f;
    if (fov > 4.0f) fov = 4.0f;
    rt_log("freecam: field of view x%.2f", fov);
    return 1;
}

int freecam_step(int frames) {
    if (!active || !frames) return 0;
    if (frames > 600) frames = 600;
    if (frames < -600) frames = -600;
    rt_clock_step_ms(frames * GAME_FRAME_MS);
    rt_log("freecam: %+d frame%s", frames, frames == 1 || frames == -1 ? "" : "s");
    return 1;
}

void freecam_filter_keys(uint8_t *keys, int count) {
    static const SDL_Scancode owned[] = {
        SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_Q, SDL_SCANCODE_E,
        SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL,
        SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
        SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS,
    };
    if (!active) return;
    for (size_t i = 0; i < sizeof owned / sizeof *owned; i++)
        if (owned[i] < count) keys[owned[i]] = 0;
}
