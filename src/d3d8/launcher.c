/* Before the game: the warning screen (assets/splash.bmp), then a launcher with Play and Options, where Options
 * edits sh3.ini. The images are built into the binary as BMPs (tools/launcher_assets.py), which SDL loads itself.
 * Drawn on the CPU into a 1280x720 canvas that is blitted to the window, so it needs no shaders and leaves the game
 * a fresh GL state. */
#include "d3d8.h"
#include "../mods/mods.h"  /* mods_init: Options > Game folder re-reads mods/ */
#include <SDL2/SDL.h>
#include <math.h>

/* Who this port is by: the menu and the credits screen show it. CMake passes both in (PORT_AUTHOR, PORT_NAME);
 * these defaults are for a plain compile outside CMake. */
#ifndef PORT_AUTHOR
#define PORT_AUTHOR "phoenixd123"
#endif
#ifndef PORT_NAME
#define PORT_NAME "SH3-PC"
#endif

#define FADE_MS 700  /* every screen fades in and out */
#define CW 1280
#define CH 720

/* The images are .incbin'd into .rdata/.rodata. PE-COFF has no .previous to return to the code section, so
 * the Windows form names the sections explicitly; a leading dot is not used there (COFF section names). */
#ifdef _WIN32
#define ASSET(name, path) \
    __asm__(".section .rdata,\"dr\"\n.balign 16\n.globl " #name "_bmp\n" #name "_bmp:\n.incbin \"" SRC_DIR "/" path "\"\n" \
            ".globl " #name "_bmp_end\n" #name "_bmp_end:\n.text"); \
    extern const uint8_t name##_bmp[], name##_bmp_end[];
#else
#define ASSET(name, path) \
    __asm__(".section .rodata\n.balign 16\n.global " #name "_bmp\n" #name "_bmp:\n.incbin \"" SRC_DIR "/" path "\"\n" \
            ".global " #name "_bmp_end\n" #name "_bmp_end:\n.previous"); \
    extern const uint8_t name##_bmp[], name##_bmp_end[];
#endif
ASSET(splash, "assets/splash.bmp")
ASSET(logo, "assets/logo.bmp")  /* the README's */
ASSET(font, "assets/font.bmp")  /* ASCII 32-126, 16x6 cells of 40x80, baseline at 60, 36 px advance */

/* ---- the game's menu sounds: WAVs in data/sound/sd.afs ---- */

enum { SND_CURSOR, SND_OK };
static SDL_AudioDeviceID snd_dev;
static struct { Uint8 *pcm; Uint32 len; } snd[2];

static void sound_init(void) {
    static const int ids[2] = {0, 3};  /* what the title menu plays on a move and a choice, at full volume as there */
    FILE *f = fopen("data/sound/sd.afs", "rb");
    SDL_AudioSpec spec;
    uint32_t n, ent[2];
    for (int i = 0; f && i < 2; i++) {
        uint8_t *wav;
        if (fseek(f, 8 + 8 * ids[i], SEEK_SET) || fread(ent, 4, 2, f) != 2 || ent[1] > (1u << 20)) break;
        if (fseek(f, ent[0], SEEK_SET) || !(wav = malloc(ent[1]))) break;
        if ((n = fread(wav, 1, ent[1], f)) == ent[1]) SDL_LoadWAV_RW(SDL_RWFromConstMem(wav, n), 1, &spec, &snd[i].pcm, &snd[i].len);
        free(wav);
    }
    if (f) fclose(f);
    if (!snd[SND_CURSOR].pcm || !snd[SND_OK].pcm) { rt_log("launcher: no menu sounds in sd.afs"); return; }
    if ((snd_dev = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0))) SDL_PauseAudioDevice(snd_dev, 0);  /* both 22 kHz mono */
    else rt_log("launcher: audio: %s", SDL_GetError());
}

static void sound(int i) {
    if (!snd_dev) return;
    SDL_ClearQueuedAudio(snd_dev);
    SDL_QueueAudio(snd_dev, snd[i].pcm, snd[i].len);
}

static void sound_close(void) {
    for (Uint64 t0 = SDL_GetTicks64(); snd_dev && SDL_GetQueuedAudioSize(snd_dev) && SDL_GetTicks64() - t0 < 1000;) SDL_Delay(10);
    if (snd_dev) SDL_CloseAudioDevice(snd_dev);
    snd_dev = 0;
    for (int i = 0; i < 2; i++) SDL_FreeWAV(snd[i].pcm), snd[i].pcm = NULL, snd[i].len = 0;
}

#ifdef _WIN32
/* The sounds are read out of the game folder, so they come back with another one. */
static void sound_reload(void) { sound_close(); sound_init(); }
#endif

/* Loads a BMP to RGBA rows; NULL if it can't. */
static uint32_t *decode(const uint8_t *bmp, const uint8_t *end, int *w, int *h) {
    SDL_Surface *s = SDL_LoadBMP_RW(SDL_RWFromConstMem(bmp, (int)(end - bmp)), 1), *c;
    uint32_t *px = NULL;
    if (s && (c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0))) {
        *w = c->w;
        *h = c->h;
        if ((px = malloc((size_t)*w * *h * 4)))
            for (int y = 0; y < *h; y++) memcpy(px + (size_t)y * *w, (uint8_t *)c->pixels + (size_t)y * c->pitch, (size_t)*w * 4);
        SDL_FreeSurface(c);
    }
    SDL_FreeSurface(s);
    return px;
}

static GLuint tex, fbo;
static int tex_w, tex_h;

/* Uploads w x h RGBA rows (row 0 at the top) and shows them letterboxed. */
static void present(const uint32_t *px, int w, int h) {
    if (w != tex_w || h != tex_h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        tex_w = w;
        tex_h = h;
    } else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    int ww, wh;
    SDL_GL_GetDrawableSize(rt_window, &ww, &wh);
    glClear(GL_COLOR_BUFFER_BIT);
    /* the texture's row 0 is the image's top, the window's row 0 its bottom */
    float s = fminf((float)ww / w, (float)wh / h);
    int dw = w * s, dh = h * s, dx = (ww - dw) / 2, dy = (wh - dh) / 2;
    glBlitFramebuffer(0, 0, w, h, dx, dy + dh, dx + dw, dy, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    SDL_GL_SwapWindow(rt_window);
}

/* ---- the canvas ---- */

static uint32_t canvas[CW * CH];
static uint32_t *layer = canvas;  /* where text() and image() draw: the canvas, or a screen of its own */
static int glyph_track;           /* extra pixels between glyphs, for the credits' letterspaced capitals */
static uint32_t *font_px;
static int font_w;

static uint32_t rgb(int r, int g, int b) { return 0xFF000000u | b << 16 | g << 8 | r; }  /* RGBA bytes */

/* Text at x (left edge) and baseline y; size = glyph height in px (80 = the atlas'), sampled bilinearly. */
static int text(int x, int y, int size, uint32_t color, const char *s) {
    float k = size / 80.0f;
    int adv = (int)(36 * k + 0.5f), cw = (int)(40 * k + 0.5f), ch = size, top = y - (int)(60 * k);
    for (; *s; s++, x += adv + glyph_track) {
        int c = (unsigned char)*s - 32;
        /* 96 cells: ASCII 32-127, so '_' (95) is the last printable one. It used to be cut off at 95. */
        if (c <= 0 || c >= 96 || !font_px) continue;
        int gx = c % 16 * 40, gy = c / 16 * 80;
        for (int py = 0; py < ch; py++) {
            int oy = top + py;
            if (oy < 0 || oy >= CH) continue;
            float fy = (py + 0.5f) / k - 0.5f;
            int y0 = (int)fy;
            float ty = fy - y0;
            if (y0 < 0) y0 = 0, ty = 0;
            if (y0 >= 79) y0 = 78, ty = 1;
            for (int px = 0; px < cw; px++) {
                int ox = x + px;
                if (ox < 0 || ox >= CW) continue;
                float fx = (px + 0.5f) / k - 0.5f;
                int x0 = (int)fx;
                float tx = fx - x0;
                if (x0 < 0) x0 = 0, tx = 0;
                if (x0 >= 39) x0 = 38, tx = 1;
                const uint32_t *r0 = font_px + (size_t)(gy + y0) * font_w + gx + x0, *r1 = r0 + font_w;
                float a = ((r0[0] >> 24) * (1 - tx) + (r0[1] >> 24) * tx) * (1 - ty) +
                          ((r1[0] >> 24) * (1 - tx) + (r1[1] >> 24) * tx) * ty;
                if (a < 1) continue;
                uint32_t *d = &layer[oy * CW + ox], o = 0xFF000000u;
                for (int sh = 0; sh < 24; sh += 8) {
                    float v = ((*d >> sh) & 0xFF) + (((color >> sh) & 0xFF) - (float)((*d >> sh) & 0xFF)) * a / 255;
                    o |= (uint32_t)v << sh;
                }
                *d = o;
            }
        }
    }
    return x;
}
/* An RGBA image scaled bilinearly into the canvas rect x, y, dw, dh, blended by its alpha. */
static void image(const uint32_t *img, int w, int h, int x, int y, int dw, int dh) {
    for (int py = 0; py < dh; py++) {
        float fy = (py + 0.5f) * h / dh - 0.5f;
        int y0 = fy < 0 ? 0 : (int)fy, y1 = y0 + 1 < h ? y0 + 1 : y0;
        float ty = fy < 0 ? 0 : fy - y0;
        for (int px = 0; px < dw; px++) {
            float fx = (px + 0.5f) * w / dw - 0.5f;
            int x0 = fx < 0 ? 0 : (int)fx, x1 = x0 + 1 < w ? x0 + 1 : x0;
            float tx = fx < 0 ? 0 : fx - x0;
            const uint32_t q[4] = {img[y0 * w + x0], img[y0 * w + x1], img[y1 * w + x0], img[y1 * w + x1]};
            float k[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty}, a = 0, c[3] = {0};
            for (int i = 0; i < 4; i++) {
                float qa = (q[i] >> 24) * k[i];
                a += qa;
                for (int j = 0; j < 3; j++) c[j] += ((q[i] >> 8 * j) & 0xFF) * qa;
            }
            if (a < 1) continue;
            uint32_t *d = &layer[(y + py) * CW + x + px], o = 0xFF000000u;
            for (int j = 0; j < 3; j++) {
                float dv = (*d >> 8 * j) & 0xFF;
                o |= (uint32_t)(dv + (c[j] / a - dv) * a / 255) << 8 * j;
            }
            *d = o;
        }
    }
}

static int text_w(int size, const char *s) { return (int)strlen(s) * (int)(36 * size / 80.0f + 0.5f); }
static void text_c(int y, int size, uint32_t color, const char *s) { text((CW - text_w(size, s)) / 2, y, size, color, s); }
/* Centred, with extra pixels between the glyphs: the credits' small letterspaced capitals. */
static void text_ct(int y, int size, uint32_t color, const char *s, int track) {
    int n = (int)strlen(s), adv = (int)(36 * size / 80.0f + 0.5f);
    glyph_track = track;
    text((CW - (n * (adv + track) - track)) / 2, y, size, color, s);
    glyph_track = 0;
}

/* ---- sh3.ini ---- */

typedef struct {
    const char *label, *key;
    const char *vals[4], *names[4];  /* NULL-terminated; vals[def] is what the game uses without the key */
    int def, cur, was;
    char other[32];  /* a value the file has that the list doesn't: shown until changed */
} Opt;

static Opt opts[] = {
    {"Resolution", "resolution", {"1280x720", "1920x1080", "960x720", "640x480"}, {"720p", "1080p", "4:3 (960x720)", "4:3 (640x480)"}},
    {"Game threads", "cores", {"3", "1"}, {"Three cores", "One core"}},
};
#define N_OPTS (int)(sizeof opts / sizeof *opts)

/* The rows under the sh3.ini options: the game location, which lives in the port's own sh3-pc.ini - the install
 * folder, or sh3.exe and a disc through the open-file dialog - then Back. */
#ifdef _WIN32
#define ROW_FOLDER N_OPTS
#define ROW_DISC (N_OPTS + 1)
#define ROW_IMPORT (N_OPTS + 2)
#define ROW_MOD (N_OPTS + 3)
#define ROW_BACK (N_OPTS + 4)
#else
#define ROW_BACK N_OPTS
#endif
#define N_ROWS (ROW_BACK + 1)

static int n_vals(const Opt *o) { int n = 0; while (n < 4 && o->vals[n]) n++; return n; }
static const char *opt_name(const Opt *o) { return o->cur < 0 ? o->other : o->names[o->cur]; }

/* The value after "key =" on an uncommented line, as the game's own parsers (sscanf " key = ") read it. */
static int ini_value(const char *line, const char *key, char *val) {
    char fmt[64];
    snprintf(fmt, sizeof fmt, " %s = %%31[^ \t\r\n;]", key);
    return sscanf(line, fmt, val) == 1;
}

static void ini_load(void) {
    FILE *f = fopen("sh3.ini", "r");
    char line[256], val[32];
    for (int i = 0; i < N_OPTS; i++) opts[i].cur = opts[i].def;
    while (f && fgets(line, sizeof line, f))
        for (int i = 0; i < N_OPTS; i++)
            if (ini_value(line, opts[i].key, val)) {
                Opt *o = &opts[i];
                o->cur = -1;
                snprintf(o->other, sizeof o->other, "%s", val);
                for (int j = 0; j < n_vals(o); j++)
                    if (!strcmp(o->vals[j], val)) o->cur = j;
            }
    if (f) fclose(f);
    for (int i = 0; i < N_OPTS; i++) opts[i].was = opts[i].cur;
}

/* Rewrites the changed keys in place (the last line that sets each, as that's the one the game ends up
 * with) and appends the ones the file lacks; comments and every other line stay as they are. */
static void ini_save(void) {
    char *lines[512];
    int n = 0;
    FILE *f = fopen("sh3.ini", "r");
    char line[256], val[32];
    while (f && n < 512 && fgets(line, sizeof line, f)) lines[n++] = strdup(line);
    if (f) fclose(f);
    if (!(f = fopen("sh3.ini", "w"))) { rt_log("launcher: can't write sh3.ini"); return; }
    for (int i = 0; i < N_OPTS; i++) {
        Opt *o = &opts[i];
        if (o->cur == o->was) continue;
        int last = -1;
        for (int j = 0; j < n; j++)
            if (ini_value(lines[j], o->key, val)) last = j;
        char set[96];
        snprintf(set, sizeof set, "%s=%s\n", o->key, o->vals[o->cur]);
        if (last >= 0) { free(lines[last]); lines[last] = strdup(set); }
        else if (n < 512) {
            if (n && !strchr(lines[n - 1], '\n')) {  /* a last line without its newline */
                lines[n - 1] = realloc(lines[n - 1], strlen(lines[n - 1]) + 2);
                strcat(lines[n - 1], "\n");
            }
            lines[n++] = strdup(set);
        }
        o->was = o->cur;
    }
    for (int j = 0; j < n; j++) { fputs(lines[j], f); free(lines[j]); }
    fclose(f);
}

/* ---- input ---- */

enum { IN_NONE, IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_OK, IN_BACK };

/* Testing: the same SH3_KEYS script the game takes drives the menus too, so Options and its pickers can be
 * walked without a keyboard (SH3_LAUNCHER=1 keeps the launcher up for it). One return per script entry. */
static int scripted_menu_key(void) {
    static Uint64 t0;
    static double fired[64];
    static int nfired;
    const char *k = getenv("SH3_KEYS");
    if (!k) return IN_NONE;
    if (!t0) t0 = SDL_GetTicks64();
    double now = (SDL_GetTicks64() - t0) / 1000.0;
    while (*k && nfired < (int)(sizeof fired / sizeof *fired)) {
        char name[32] = "";
        double at = 0, hold = 0.15;
        int n = 0, code = IN_NONE, seen = 0;
        if (sscanf(k, "%lf:%31[^,+]%n", &at, name, &n) < 2) break;
        k += n;
        if (*k == '+' && sscanf(k, "+%lf%n", &hold, &n) == 1) k += n;
        if (!strcasecmp(name, "Up")) code = IN_UP;
        else if (!strcasecmp(name, "Down")) code = IN_DOWN;
        else if (!strcasecmp(name, "Left")) code = IN_LEFT;
        else if (!strcasecmp(name, "Right")) code = IN_RIGHT;
        else if (!strcasecmp(name, "Return") || !strcasecmp(name, "Space")) code = IN_OK;
        else if (!strcasecmp(name, "Escape") || !strcasecmp(name, "Backspace")) code = IN_BACK;
        for (int i = 0; i < nfired; i++)
            if (fired[i] == at) seen = 1;
        if (code && !seen && now >= at && now < at + hold) {
            fired[nfired++] = at;
            return code;
        }
        if (*k == ',') k++;
    }
    return IN_NONE;
}

static int poll_input(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: exit(0);
        case SDL_CONTROLLERDEVICEADDED: SDL_GameControllerOpen(e.cdevice.which); break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
            case SDLK_UP: return IN_UP;
            case SDLK_DOWN: return IN_DOWN;
            case SDLK_LEFT: return IN_LEFT;
            case SDLK_RIGHT: return IN_RIGHT;
            case SDLK_RETURN: case SDLK_SPACE: return IN_OK;
            case SDLK_ESCAPE: case SDLK_BACKSPACE: return IN_BACK;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP: return IN_UP;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return IN_DOWN;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return IN_LEFT;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return IN_RIGHT;
#ifdef __SWITCH__  /* Nintendo's way round, as in the game (dinput.c): confirm on A (east), back on B (south) */
            case SDL_CONTROLLER_BUTTON_B: return IN_OK;
            case SDL_CONTROLLER_BUTTON_A: return IN_BACK;
#else
            case SDL_CONTROLLER_BUTTON_A: return IN_OK;
            case SDL_CONTROLLER_BUTTON_B: return IN_BACK;
#endif
            case SDL_CONTROLLER_BUTTON_START: return IN_OK;
            }
            break;
        case SDL_CONTROLLERAXISMOTION: {  /* the left stick, once per push past half way */
            static int held[2];
            int ax = e.caxis.axis;
            if (ax != SDL_CONTROLLER_AXIS_LEFTX && ax != SDL_CONTROLLER_AXIS_LEFTY) break;
            int dir = e.caxis.value > 16000 ? 1 : e.caxis.value < -16000 ? -1 : 0, was = held[ax];
            held[ax] = dir;
            if (dir && dir != was) return ax == SDL_CONTROLLER_AXIS_LEFTX ? (dir > 0 ? IN_RIGHT : IN_LEFT) : (dir > 0 ? IN_DOWN : IN_UP);
            break;
        }
        }
    }
    return scripted_menu_key();
}

/* ---- screens ---- */

/* The palette as rgb() builds it (0xAABBGGRR: the canvas is RGBA bytes, little endian). Constants rather than
 * rgb() calls so the credits table can be a static initialiser. */
#define C_BG 0xFF0A0808u    /* rgb(8, 8, 10) */
#define C_TEXT 0xFFC8C8C8u  /* rgb(200, 200, 200) */
#define C_DIM 0xFF6E6E6Eu   /* rgb(110, 110, 110) */
#define C_SEL 0xFF1E28DCu   /* rgb(220, 40, 30) */
#define C_RULE 0xFF4A4646u  /* rgb(70, 70, 74): the credits' rules */

static uint32_t *logo_px;
static int logo_w, logo_h;

static const char *menu_items[] = {"Play", "Options", "Credits"};
#define N_ITEMS (int)(sizeof menu_items / sizeof *menu_items)

static void draw_menu(int sel) {
    for (int i = 0; i < CW * CH; i++) canvas[i] = C_BG;
    if (logo_px) image(logo_px, logo_w, logo_h, (CW - 800) / 2, 170, 800, 800 * logo_h / logo_w);
    text_c(320, 32, C_DIM, PORT_NAME);
    for (int i = 0; i < N_ITEMS; i++) text_c(450 + i * 64, 44, i == sel ? C_SEL : C_TEXT, menu_items[i]);
    text_c(690, 24, C_DIM, "A: select");
}

/* ---- credits ---- */
/* The port's own credits, rolled the way an ending is: black, thin letterspaced capitals for the labels and the
 * names under them, coming up out of the dark and going back into it at the top. The font is ASCII, so no em
 * dashes or (c) glyphs. */

static void fade(int in);  /* below: the credits screen fades like the others */

enum { CR_TITLE, CR_RULE, CR_GAP, CR_LABEL, CR_VALUE, CR_NOTE };
typedef struct { int kind; const char *text; } Credit;

static const Credit credit_roll[] = {
    {CR_TITLE, "SILENT HILL 3"},
    {CR_RULE, NULL},
    {CR_GAP, NULL},
    {CR_LABEL, "PORTED TO PC BY"},
    {CR_VALUE, PORT_AUTHOR},
    {CR_GAP, NULL},
    {CR_LABEL, "INSTAGRAM"},
    {CR_VALUE, "@phoenixdobsonyc"},
    {CR_GAP, NULL},
    {CR_LABEL, "ORIGINAL GAME"},
    {CR_VALUE, "Konami / Team Silent"},
    {CR_GAP, NULL},
    {CR_GAP, NULL},
    {CR_LABEL, "EVERYTHING ELSE"},
    {CR_VALUE, "CREDITS.md"},
    {CR_GAP, NULL},
    {CR_GAP, NULL},
    {CR_NOTE, "Unofficial fan port. No game files are included:"},
    {CR_NOTE, "it runs the copy of Silent Hill 3 for PC that you own."},
};
#define N_CREDITS (int)(sizeof credit_roll / sizeof *credit_roll)

/* How much of the roll one entry takes, drawn or not. */
static int credit_h(int kind) {
    switch (kind) {
    case CR_TITLE: return 46 + 22;
    case CR_RULE: return 1 + 26;
    case CR_GAP: return 30;
    case CR_LABEL: return 18 + 14;
    case CR_VALUE: return 30 + 30;
    default: return 17 + 10;  /* CR_NOTE */
    }
}

/* A thin rule under the title. */
static void rule(int y, int w) {
    if (y < 0 || y >= CH) return;
    for (int x = (CW - w) / 2; x < (CW + w) / 2; x++) layer[y * CW + x] = C_RULE;
}

/* One entry with its first line at y: drawn when draw is set, and y advanced past it either way. */
static void credit(const Credit *c, int *y, int draw) {
    switch (c->kind) {
    case CR_TITLE: if (draw) text_ct(*y, 46, C_TEXT, c->text, 12); break;
    case CR_RULE: if (draw) rule(*y + 8, 300); break;
    case CR_LABEL: if (draw) text_ct(*y, 18, C_DIM, c->text, 7); break;
    case CR_VALUE: if (draw) text_ct(*y, 30, C_TEXT, c->text, 2); break;
    case CR_NOTE: if (draw) text_ct(*y, 17, C_DIM, c->text, 1); break;
    }
    *y += credit_h(c->kind);
}

static int credits_height(void) {
    int y = 0;
    for (int i = 0; i < N_CREDITS; i++) credit(&credit_roll[i], &y, 0);
    return y;
}

/* The roll with its first line at top, then the top and bottom of the screen taken down to black, so lines come
 * in and go out instead of appearing and vanishing at an edge. */
static void draw_credits(int top) {
    for (int i = 0; i < CW * CH; i++) canvas[i] = C_BG;
    layer = canvas;
    int y = top;
    for (int i = 0; i < N_CREDITS; i++) credit(&credit_roll[i], &y, 1);
    static uint8_t mask[CH];
    static int ready;
    if (!ready) {
        ready = 1;
        for (int r = 0; r < CH; r++) {
            int a = r * 255 / 130, b = (CH - 1 - r) * 255 / 190, v = a < b ? a : b;
            mask[r] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
    for (int r = 0; r < CH; r++) {
        uint32_t k = mask[r];
        if (k == 255) continue;
        for (int x = 0; x < CW; x++) {
            uint32_t *d = &canvas[r * CW + x];
            *d = 0xFF000000u | ((*d & 0xFF00FFu) * k >> 8 & 0xFF00FFu) | ((*d & 0xFF00u) * k >> 8 & 0xFF00u);
        }
    }
}

/* The marker file that makes the credits a first-launch screen, next to the saves (or beside the exe if there
 * is no savedata/ yet). NULL when neither can be opened. */
static FILE *credits_file(const char *mode) {
    static const char *where[] = {"savedata/port-credits", "port-credits"};
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(where[i], mode);
        if (f) return f;
    }
    return NULL;
}

static int credits_seen(void) {
    FILE *f = credits_file("rb");
    if (f) fclose(f);
    return f != NULL;
}

/* The roll runs once, from below the screen to above it, in about half a minute; A or B leaves at any point. */
static void credits(void) {
    const int height = credits_height(), speed = 60;  /* pixels per second */
    Uint64 t0 = SDL_GetTicks64();
    draw_credits(CH + 20);
    fade(1);
    for (;;) {
        int top = CH + 20 - (int)((SDL_GetTicks64() - t0) * (Uint64)speed / 1000);
        if (top < -height - 20) break;
        draw_credits(top);
        present(canvas, CW, CH);
        int in = poll_input();
        if (in == IN_OK || in == IN_BACK) break;
        SDL_Delay(8);
    }
    sound(SND_OK);
    FILE *f = credits_file("wb");
    if (f) fclose(f);
}

/* What the last folder pick said, under the hint until the next key. */
static const char *row_note;
#ifdef _WIN32

/* Right-aligned at x_right, shortened from the left when it is too wide: the end of a path is what tells two
 * installs apart. */
static void path_text(int y, int x_right, int size, uint32_t color, const char *p) {
    const int maxw = x_right - 470;  /* the label column ends well short of this */
    if (text_w(size, p) > maxw) {
        static char buf[600];
        int n = (int)strlen(p), keep = n;  /* how much of the path survives, counted from its end */
        while (keep > 0 && text_w(size, p + n - keep) + text_w(size, "...") > maxw) keep--;
        snprintf(buf, sizeof buf, "...%s", p + n - keep);
        p = buf;
    }
    text(x_right - text_w(size, p), y, size, color, p);
}
#endif

static void draw_options(int sel) {
    for (int i = 0; i < CW * CH; i++) canvas[i] = C_BG;
    glyph_track = 0;
    text_c(110, 56, C_TEXT, "Options");
    for (int i = 0; i < N_ROWS; i++) {
        int y = 190 + i * 56;
        uint32_t c = i == sel ? C_SEL : C_TEXT;
        if (i == ROW_BACK) { text_c(y + 20, 36, c, "Back"); continue; }
#ifdef _WIN32
        if (i == ROW_FOLDER) {
            text(140, y, 36, c, "Game folder");
            path_text(y, CW - 140, 36, c, rt_game_dir);
            continue;
        }
        if (i == ROW_DISC) {
            text(140, y, 36, c, "Game disc or file");
            continue;
        }
        if (i == ROW_IMPORT) {
            text(140, y, 36, c, "Import the game download");
            continue;
        }
        if (i == ROW_MOD) {
            text(140, y, 36, c, "Install a mod");
            continue;
        }
#endif
        text(140, y, 36, c, opts[i].label);
        char v[64];
        snprintf(v, sizeof v, i == sel ? "< %s >" : "%s", opt_name(&opts[i]));
        text(CW - 140 - text_w(36, v), y, 36, c, v);
    }
    text_c(654, 24, C_DIM, "Left/Right: change   A: pick   B: back (saves sh3.ini)");
    if (row_note) text_c(690, 24, C_DIM, row_note);
}

/* Fades the canvas in from black, or out to black, then leaves it as it was. Input meanwhile is dropped. */
static void fade(int in) {
    static uint32_t pic[CW * CH];
    memcpy(pic, canvas, sizeof pic);
    for (Uint64 t0 = SDL_GetTicks64(), t = 0; t < FADE_MS;) {
        poll_input();
        t = SDL_GetTicks64() - t0;
        uint32_t k = t >= FADE_MS ? 256 : (uint32_t)(t * 256 / FADE_MS);
        if (!in) k = 256 - k;
        for (int i = 0; i < CW * CH; i++)
            canvas[i] = 0xFF000000u | ((pic[i] & 0xFF00FFu) * k >> 8 & 0xFF00FFu) | ((pic[i] & 0xFF00u) * k >> 8 & 0xFF00u);
        present(canvas, CW, CH);
    }
    memcpy(canvas, pic, sizeof pic);
}

static void options(void) {
    int sel = 0;
    row_note = NULL;
    draw_options(sel);
    fade(1);
    for (int dirty = 0;; ) {
        if (dirty) { draw_options(sel); dirty = 0; }
        present(canvas, CW, CH);
        int in = poll_input();
        if (in == IN_NONE) continue;
        dirty = 1;
        Opt *o = sel < N_OPTS ? &opts[sel] : NULL;
        int back = sel == ROW_BACK;
        if (in != IN_BACK) sound(back && in == IN_OK ? SND_OK : SND_CURSOR);
        if (in == IN_UP) sel = (sel + N_ROWS - 1) % N_ROWS;
        if (in == IN_DOWN) sel = (sel + 1) % N_ROWS;
        if (o && (in == IN_LEFT || in == IN_RIGHT || in == IN_OK)) {
            int n = n_vals(o);
            o->cur = o->cur < 0 ? 0 : (o->cur + (in == IN_LEFT ? n - 1 : 1)) % n;
        }
#ifdef _WIN32
        /* Both rows pick, then share the rest: rt_game_path decides what the path is and owns the wording for
         * everything that is not the game, and nothing here changes when it refuses. */
        if ((sel == ROW_FOLDER || sel == ROW_DISC) && (in == IN_LEFT || in == IN_RIGHT || in == IN_OK)) {
            char picked[700], dir[700];
            int got = sel == ROW_FOLDER ? rt_pick_game_folder(rt_game_dir, picked, sizeof picked)
                                        : rt_pick_game_disc(rt_game_dir, picked, sizeof picked);
            const char *why = got ? rt_game_path(picked, dir, sizeof dir) : NULL;
            if (got && why) {
                /* A PS2 disc cannot run here: rt_ps2_offer explains and offers PCSX2 instead. The note says what
                 * actually happened, so it never promises an emulator that is not installed. */
                int r = rt_last_ps2 ? rt_ps2_offer(rt_ps2_path) : 0;
                if (!r) rt_say(why);
                else if (r == 2) row_note = "Opened in PCSX2";
                else row_note = rt_pcsx2_found() ? "PlayStation 2 disc: not opened"
                                                 : "PlayStation 2 disc: PCSX2 is not installed";
                if (row_note) rt_log("launcher: %s", row_note);
            }
            else if (got) {
                ini_save();  /* what was changed here belongs to the folder the port is leaving */
                if (rt_set_game_dir(dir)) {
                    ini_load();      /* the new folder's sh3.ini */
                    mods_init();     /* and its mods/ */
                    sound_reload();  /* and its sd.afs */
                    row_note = "Game location changed";
                } else
                    row_note = "That folder can no longer be entered";
            }
        }
#endif
#ifdef _WIN32
        /* The download: the Full-Rip zip is unpacked beside itself and the install inside it becomes the game
         * folder, so a player who has only downloaded the archive does not have to unpack it by hand. */
        if (sel == ROW_IMPORT && (in == IN_LEFT || in == IN_RIGHT || in == IN_OK)) {
            char zip[700], dir2[700], err[700] = "";
            if (rt_pick_game_archive(zip, sizeof zip)) {
                if (rt_import_archive(zip, dir2, sizeof dir2, err, sizeof err)) {
                    ini_save();
                    if (rt_set_game_dir(dir2)) {
                        ini_load();
                        mods_init();
                        sound_reload();
                        row_note = "Imported the download";
                    } else
                        row_note = "That folder can no longer be entered";
                    rt_log("launcher: %s", row_note);
                } else
                    rt_say(err);
            }
        }
#endif
#ifdef _WIN32
        /* A mod archive: unpacked into mods/ under the game folder, then loaded, so it is applied without a restart
         * of the port and without unpacking anything by hand. */
        if (sel == ROW_MOD && (in == IN_LEFT || in == IN_RIGHT || in == IN_OK)) {
            char zip[700], name[128], err[600] = "";
            if (rt_pick_mod_zip(zip, sizeof zip)) {
                if (mods_install(zip, name, sizeof name, err, sizeof err)) {
                    static char note[160];
                    mods_init();  /* and it is live for the game that starts after this menu */
                    snprintf(note, sizeof note, "Installed the mod \"%s\"", name);
                    row_note = note;
                } else
                    rt_say(err);
            }
        }
#endif
        if (in == IN_BACK || (back && in == IN_OK)) {
            ini_save();
            fade(0);
            return;
        }
    }
}

static int same_image(const uint8_t *img, const uint8_t *end, const char *sha1) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}, w[80];
    uint64_t n = (uint64_t)(end - img), bits = n * 8;
    char hex[41];
    for (uint64_t off = 0; off < n + 9; off += 64) {
        uint8_t b[64];
        for (int i = 0; i < 64; i++) {
            uint64_t k = off + i;
            b[i] = k < n ? img[k] : k == n ? 0x80 : 0;
        }
        if (off + 64 >= n + 9)
            for (int i = 0; i < 8; i++) b[63 - i] = (uint8_t)(bits >> 8 * i);
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | b[4 * i + 1] << 16 | b[4 * i + 2] << 8 | b[4 * i + 3];
        for (int i = 16; i < 80; i++) { uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = x << 1 | x >> 31; }
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f = i < 20 ? (bb & c) | (~bb & d) : i < 40 || i >= 60 ? bb ^ c ^ d : (bb & c) | (bb & d) | (c & d);
            uint32_t k = i < 20 ? 0x5A827999 : i < 40 ? 0x6ED9EBA1 : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
            uint32_t t = (a << 5 | a >> 27) + f + e + k + w[i];
            e = d, d = c, c = bb << 30 | bb >> 2, bb = a, a = t;
        }
        h[0] += a, h[1] += bb, h[2] += c, h[3] += d, h[4] += e;
    }
    for (int i = 0; i < 5; i++) snprintf(hex + 8 * i, 9, "%08x", h[i]);
    return !strcmp(hex, sha1);
}

static int warning_shown;

/* A warning screen: wait_ms after it has faded in, "Press A" fades in, and the screen waits for it. */
static void splash(const uint8_t *img, const uint8_t *end, Uint64 wait_ms, const char *sha1) {
    warning_shown = same_image(img, end, sha1);
    int w, h;
    uint32_t *px = decode(img, end, &w, &h);
    if (!px) { rt_log("launcher: can't decode a splash image"); return; }
    for (int i = 0; i < CW * CH; i++) canvas[i] = rgb(0, 0, 0);
    int dw = CW, dh = CW * h / w;
    if (dh > CH) dh = CH, dw = CH * w / h;
    image(px, w, h, (CW - dw) / 2, (CH - dh) / 2, dw, dh);
    free(px);
    fade(1);
    Uint64 t0 = SDL_GetTicks64();
    while (SDL_GetTicks64() - t0 < wait_ms) { poll_input(); present(canvas, CW, CH); }
    static uint32_t pic[CW * CH];
    memcpy(pic, canvas, sizeof pic);
    for (Uint64 t1 = SDL_GetTicks64(), t = 0; poll_input() != IN_OK || t < FADE_MS;) {
        t = SDL_GetTicks64() - t1;
        int k = t >= FADE_MS ? 256 : (int)(t * 256 / FADE_MS);  /* the text's colour from black up */
        memcpy(canvas, pic, sizeof pic);
        text_c(CH - 40, 32, rgb(235 * k >> 8, 235 * k >> 8, 235 * k >> 8), "Press A");  /* the picture's corners are taken */
        present(canvas, CW, CH);
    }
    sound(SND_OK);
    fade(0);
}

void rt_game_setup(void);  /* rt_core.c */

void launcher_run(void) {
#ifndef __SWITCH__
    /* Scripted runs (SH3_KEYS) go straight to the game; SH3_LAUNCHER=1 keeps the launcher for testing it. */
    if (getenv("SH3_KEYS") && !getenv("SH3_LAUNCHER")) { rt_game_setup(); return; }
#endif
    rt_window_open();
    gl_context_open();
    int fh;
    font_px = decode(font_bmp, font_bmp_end, &font_w, &fh);
    for (int i = 0; font_px && i < font_w * fh; i++) font_px[i] = font_px[i] << 24 | 0xFFFFFFu;  /* gray = coverage */
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glClearColor(0, 0, 0, 1);
    logo_px = decode(logo_bmp, logo_bmp_end, &logo_w, &logo_h);
    sound_init();
    splash(splash_bmp, splash_bmp_end, 4000, SPLASH_SHA1);
    if (!credits_seen()) credits();  /* first launch: who made this port, once */
    ini_load();
    draw_menu(0);
    fade(1);
    for (int sel = 0, dirty = 0;; ) {
        if (dirty) { draw_menu(sel); dirty = 0; }
        present(canvas, CW, CH);
        int in = poll_input();
        if (in == IN_UP || in == IN_DOWN) sel = (sel + (in == IN_UP ? N_ITEMS - 1 : 1)) % N_ITEMS, dirty = 1, sound(SND_CURSOR);
        if (in == IN_OK) sound(SND_OK);
        if (in == IN_OK && sel == 0) {
#ifdef PORT_LAUNCHER_ONLY
            /* The launcher-only build (build/wcheck.sh, and the release asset) has no recompiled game code:
             * say that instead of dropping the player into the crash handler. */
            rt_say("This launcher has no recompiled game code, so it cannot start Silent Hill 3.\n\n"
                   "Build the port from your own copy (docs/BUILD-WINDOWS.md) and run that sh3.exe.");
            dirty = 1;
            continue;
#else
            fade(0);
            break;
#endif
        }
        if (in == IN_OK && sel == 1) {
            fade(0);
            options();
            draw_menu(sel);
            fade(1);
        }
        if (in == IN_OK && sel == 2) {
            fade(0);
            credits();
            draw_menu(sel);
            fade(1);
        }
    }
    /* leave GL as the game expects a new context: framebuffer 0, nothing bound */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    free(font_px);
    free(logo_px);
    sound_close();
    if (warning_shown) rt_game_setup();
}
