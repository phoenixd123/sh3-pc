/* Camera: camera= in sh3.ini, or F2, moves the render camera along the view the game itself set, for an over
 * the shoulder or a first person view. The game's own camera does not move: collision, aiming, culling and the
 * menus stay where the game put them. See README, "Camera". */
#pragma once
#include <stdint.h>

/* 1 when a shift is in effect (not the freecam, which replaces the whole view instead). */
int camera_active(void);
/* A copy of the game's view, shifted by the current mode's offsets. */
void camera_view(float out[16], const float game[16]);
/* F2: off -> over the shoulder -> first person. Clears any nudge: the presets are back. */
void camera_cycle(void);

/* Alt + arrows / Page Up / Page Down while a camera mode is on: move the eye a step along one axis and log the
 * three numbers as sh3.ini lines. axis 0 forward, 1 right, 2 up. Switches to the custom offsets first. */
void camera_nudge(int axis, float delta);

/* Clear the nudge keys from the keyboard state the guest sees while Alt is held in a camera mode. */
void camera_filter_keys(uint8_t *keys, int count);
