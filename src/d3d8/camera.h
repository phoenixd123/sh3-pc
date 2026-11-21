/* Camera: camera= in sh3.ini, or F2, moves the render camera along the view the game itself set, for an over
 * the shoulder or a first person view. The game's own camera does not move: collision, aiming, culling and the
 * menus stay where the game put them. See README, "Camera". */
#pragma once

/* 1 when a shift is in effect (not the freecam, which replaces the whole view instead). */
int camera_active(void);
/* A copy of the game's view, shifted by the current mode's offsets. */
void camera_view(float out[16], const float game[16]);
/* F2: off -> over the shoulder -> first person. */
void camera_cycle(void);
