/* Post-process filters over the presented frame (sh3.ini filter=): Off, CRT, VHS. The pass is a fullscreen
 * triangle sampling the backbuffer where the blit would have put it, so the letterbox stays the blit's. */
#pragma once
#include "gl.h"

int filter_init(void);  /* 1 when the shader is ready; a failure leaves the plain blit in place */
void filter_draw(GLuint src, int x, int y, int w, int h, int mode, float seconds);
