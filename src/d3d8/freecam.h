/* Freecam: F1 detaches the view from the game's camera and holds the game's clock.
 *
 * The scene keeps being drawn by the game, but with the view matrix this builds; with the clock held, the
 * game's own animation and movement do not advance, so the frame stands still while the camera moves around
 * it. `,` / `.` step the held clock one frame at a time, which is how a pose is chosen; `[` / `]` step ten.
 * See README, "Freecam". */
#pragma once
#include <stdint.h>

int freecam_active(void);
void freecam_toggle(void);

/* D3D8 SetTransform: `game` is what the game asked for. */
void freecam_view(const float *game, float out[16]);
void freecam_projection(float out[16], const float *game);

/* Once per presented frame, and on the mouse wheel. */
void freecam_frame(void);
int freecam_wheel(int steps);
/* Advance (or hold back) the frozen clock by whole frames of the game's 30 Hz. */
int freecam_step(int frames);

/* Clear the keys the freecam owns from the keyboard state the guest sees. */
void freecam_filter_keys(uint8_t *keys, int count);
