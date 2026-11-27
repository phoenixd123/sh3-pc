/* The display modes the port reports to the game.
 *
 * EnumDisplaySettingsA is what the game's Options screen builds its resolution list from, and it tests every
 * mode it is given before the screen appears. The original executable asked the driver for all of them - 1100+
 * on a modern PC - which is the long-standing options-menu freeze every community patch forces down to sixteen
 * modes (Steam006's PC Fix, Silent Hill 3: Rebirth, and the hand-hex-edit that sets the count to 16 and jumps
 * over the test loop). This is the same state, without patching an executable: sixteen modes, 4:3, 32 bits and
 * 60 Hz, with the mode the port actually renders at first, so the list is never empty and the current mode is
 * always in it.
 *
 * Plain data and arithmetic, no guest memory: build/modes_test.c checks it without the game. */
#ifndef SH3_DISPLAY_MODES_H
#define SH3_DISPLAY_MODES_H

#include <stdint.h>

#define SH3_MODE_COUNT 16    /* what the game's options screen is given, and the number its 16-mode patch assumes */
#define SH3_FIXED_MODES 17   /* one more than the list needs, so a current mode from the table still leaves 16 */

typedef struct { uint16_t w, h, hz; uint8_t bpp; } Sh3DisplayMode;

/* 4:3, 32-bit, 60 Hz modes - the only ones the game keeps in its list. */
static const Sh3DisplayMode sh3_fixed_modes[SH3_FIXED_MODES] = {
    {640, 480, 60, 32},   {800, 600, 60, 32},   {960, 720, 60, 32},   {1024, 768, 60, 32},
    {1152, 864, 60, 32},  {1280, 960, 60, 32},  {1360, 1020, 60, 32}, {1400, 1050, 60, 32},
    {1440, 1080, 60, 32}, {1600, 1200, 60, 32}, {1680, 1260, 60, 32}, {1800, 1350, 60, 32},
    {1920, 1440, 60, 32}, {2048, 1536, 60, 32}, {2560, 1920, 60, 32}, {2880, 2160, 60, 32},
    {3200, 2400, 60, 32},
};

/* Mode i, the port's own resolution (cur_w x cur_h) at 0 and the table's modes after it, skipping the one the
 * current mode repeats. {0, 0, 0, 0} past the end: what EnumDisplaySettingsA answers with to stop a caller. */
static inline Sh3DisplayMode sh3_display_mode(int i, uint32_t cur_w, uint32_t cur_h) {
    if (i == 0) return (Sh3DisplayMode){(uint16_t)cur_w, (uint16_t)cur_h, 60, 32};
    if (i > 0 && i < SH3_MODE_COUNT)
        for (int n = 0, k = 0; k < SH3_FIXED_MODES; k++)
            if (sh3_fixed_modes[k].w != cur_w || sh3_fixed_modes[k].h != cur_h)
                if (++n == i) return sh3_fixed_modes[k];
    return (Sh3DisplayMode){0, 0, 0, 0};
}

/* DEVMODEA, the fields the game and user32.c touch: dmSize @36, dmFields @40, dmBitsPerPel @104,
 * dmPelsWidth @108, dmPelsHeight @112, dmDisplayFrequency @120. */
#define SH3_DEVMODE_SIZE 156

static inline void sh3_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static inline uint32_t sh3_get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static inline void sh3_devmode_fill(uint8_t *dm, Sh3DisplayMode m) {
    dm[36] = SH3_DEVMODE_SIZE & 0xFF;
    dm[37] = SH3_DEVMODE_SIZE >> 8;
    /* DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY */
    sh3_put32(dm + 40, 0x40000 | 0x80000 | 0x100000 | 0x400000);
    sh3_put32(dm + 104, m.bpp);
    sh3_put32(dm + 108, m.w);
    sh3_put32(dm + 112, m.h);
    sh3_put32(dm + 120, m.hz);
}

/* What a DEVMODE asks for; 0 when it names no size the port could render. */
static inline int sh3_devmode_parse(const uint8_t *dm, Sh3DisplayMode *m) {
    uint32_t w = sh3_get32(dm + 108), h = sh3_get32(dm + 112);
    m->w = (uint16_t)w;
    m->h = (uint16_t)h;
    m->bpp = (uint8_t)sh3_get32(dm + 104);
    m->hz = (uint16_t)sh3_get32(dm + 120);
    return w >= 320 && h >= 240 && w <= 8192 && h <= 8192;
}

#endif
