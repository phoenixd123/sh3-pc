#include "freecam_math.h"
#include <math.h>
#include <string.h>

static int finite4(const float *m) {
    for (int i = 0; i < 16; i++)
        if (!isfinite(m[i])) return 0;
    return 1;
}

void freecam_view_build(float m[16], const float pos[3], float yaw, float pitch) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    float f[3] = {cp * sy, sp, cp * cy};          /* forward */
    float r[3] = {f[2], 0.0f, -f[0]};             /* right = up x forward */
    float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
    if (rl < 1e-6f) { r[0] = 1; r[2] = 0; rl = 1; }
    r[0] /= rl; r[2] /= rl;
    float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};

    m[0] = r[0]; m[1] = u[0]; m[2] = f[0]; m[3] = 0;
    m[4] = r[1]; m[5] = u[1]; m[6] = f[1]; m[7] = 0;
    m[8] = r[2]; m[9] = u[2]; m[10] = f[2]; m[11] = 0;
    m[12] = -(r[0] * pos[0] + r[1] * pos[1] + r[2] * pos[2]);
    m[13] = -(u[0] * pos[0] + u[1] * pos[1] + u[2] * pos[2]);
    m[14] = -(f[0] * pos[0] + f[1] * pos[1] + f[2] * pos[2]);
    m[15] = 1;
}

int freecam_view_adopt(const float m[16], float pos[3], float *yaw, float *pitch) {
    if (!finite4(m)) return 0;
    float f[3] = {m[2], m[6], m[10]};
    float len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len < 0.5f) return 0;
    f[0] /= len; f[1] /= len; f[2] /= len;

    /* The basis vectors are the matrix's columns; the translation row is -R * eye, so eye = -R^T * t. */
    pos[0] = -(m[0] * m[12] + m[1] * m[13] + m[2] * m[14]);
    pos[1] = -(m[4] * m[12] + m[5] * m[13] + m[6] * m[14]);
    pos[2] = -(m[8] * m[12] + m[9] * m[13] + m[10] * m[14]);
    *yaw = atan2f(f[0], f[2]);
    float p = asinf(f[1] < -1.0f ? -1.0f : f[1] > 1.0f ? 1.0f : f[1]);
    const float limit = 1.5533f;  /* 89 degrees: straight up has no right vector */
    *pitch = p < -limit ? -limit : p > limit ? limit : p;
    return 1;
}

void freecam_proj_scale(float out[16], const float in[16], float factor) {
    memcpy(out, in, 64);
    if (factor > 0) { out[0] *= factor; out[5] *= factor; }
}
