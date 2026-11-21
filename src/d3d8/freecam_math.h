/* The matrices the freecam builds, kept free of SDL and the runtime so they can be tested on their own.
 * D3D8 convention: row-major, row vectors, v' = v * M; forward is +Z. */
#pragma once

/* A view matrix looking from `pos` with yaw around +Y (0 looks down +Z) and pitch in radians (positive up). */
void freecam_view_build(float m[16], const float pos[3], float yaw, float pitch);

/* The position, yaw and pitch a view matrix looks from, so a freecam can start where the game's camera is.
 * 0 when the matrix is not a usable rigid view. */
int freecam_view_adopt(const float m[16], float pos[3], float *yaw, float *pitch);

/* A copy of `in` with the horizontal and vertical field of view scaled by `factor` (>1 is wider), which
 * leaves the near and far planes and the aspect ratio alone. */
void freecam_proj_scale(float out[16], const float in[16], float factor);

/* Moves the eye of a view matrix by `forward`, `right` and `up`, measured along the view's own axes, and leaves
 * the orientation alone. A matrix with a broken or degenerate basis is left as it is. */
void freecam_view_shift(float m[16], float forward, float right, float up);
