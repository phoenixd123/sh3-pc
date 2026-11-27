/* Before the game: the warning screen (assets/splash.bmp), then a launcher with Play and Options, where Options
 * edits sh3.ini. The images are built into the binary as BMPs (tools/launcher_assets.py), which SDL loads itself.
 * Drawn on the CPU into a 1280x720 canvas that is blitted to the window, so it needs no shaders and leaves the game
 * a fresh GL state. */
#include "d3d8.h"
#include "../mods/mods.h"  /* mods_init: Options > Game folder re-reads mods/ */
#include "../runtime/win_posix.h"  /* win_confirm: Check this copy asks before it switches folders */
#include "nexus.h"  /* the Mods screen: the Nexus list and its downloads */
#include "updater.h" /* Check for updates: the newest release, downloaded and put in place */
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
#ifndef __SWITCH__
    {"Window", "fullscreen", {"1", "0"}, {"Fullscreen", "Window"}},
#endif
    {"Game threads", "cores", {"3", "1"}, {"Three cores", "One core"}},
    {"Widescreen", "widescreen", {"1", "0"}, {"On", "Off"}},
    {"Filter", "filter", {"0", "1", "2"}, {"Off", "CRT", "VHS"}},
    {"Camera", "camera", {"off", "ots", "first"}, {"Off", "Over the shoulder", "First person"}},
#ifdef _WIN32
    {"Auto update", "autoupdate", {"1", "0"}, {"On", "Off"}},
#endif
};
#define N_OPTS (int)(sizeof opts / sizeof *opts)
#ifndef __SWITCH__
#define OPT_WINDOW 1  /* the row after Resolution */
#endif
#ifdef _WIN32
#define OPT_AUTOUPDATE (N_OPTS - 1)  /* the row above: checked when the launcher starts */
#endif

/* The rows under the sh3.ini options: the game location, which lives in the port's own sh3-pc.ini - the install
 * folder, or sh3.exe and a disc through the open-file dialog - then Back. */
#ifdef _WIN32
#define ROW_FOLDER N_OPTS
#define ROW_DISC (N_OPTS + 1)
#define ROW_CHECK (N_OPTS + 2)
#define ROW_IMPORT (N_OPTS + 3)
#define ROW_MOD (N_OPTS + 4)
#define ROW_BACK (N_OPTS + 5)
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

#ifdef _WIN32
/* nexus_key= in sh3.ini. Read when the launcher starts and again whenever the Mods screen opens, so a key added
 * to the file does not need the launcher restarted. A personal key is about 96 characters, which the 32 byte
 * value the Options rows use is far too small for. */
static int ini_nexus_key(void) {
    FILE *f = fopen("sh3.ini", "r");
    char line[256], k[192];
    rt_nexus_key[0] = 0;
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, " nexus_key = %191[^\r\n]", k) == 1) {
            size_t l = strlen(k);
            while (l && (k[l - 1] == ' ' || k[l - 1] == '\t')) k[--l] = 0;
            snprintf(rt_nexus_key, sizeof rt_nexus_key, "%s", k);
        }
    if (f) fclose(f);
    return nexus_set_key(rt_nexus_key);
}
#endif

#ifndef __SWITCH__
/* The window follows the row: fullscreen unless the file says fullscreen=0. */
static void apply_window_opt(void) {
    Opt *o = &opts[OPT_WINDOW];
    rt_fullscreen = o->cur < 0 ? strcmp(o->other, "0") != 0 : o->cur == 0;
    rt_apply_window_mode();
}
#endif

static void ini_load(void) {
    FILE *f = fopen("sh3.ini", "r");
    char line[256], val[32];
    for (int i = 0; i < N_OPTS; i++) opts[i].cur = opts[i].def;
#ifdef _WIN32
    ini_nexus_key();  /* not an Options row: the Mods screen needs it before the game starts */
#endif
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
#ifndef __SWITCH__
    apply_window_opt();
#endif
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

enum { IN_NONE, IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_OK, IN_BACK, IN_DROP, IN_MODS, IN_PAGE, IN_REFRESH };

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
        else if (!strcasecmp(name, "M") || !strcasecmp(name, "Mods")) code = IN_MODS;
        else if (!strcasecmp(name, "N") || !strcasecmp(name, "Page")) code = IN_PAGE;
        else if (!strcasecmp(name, "R") || !strcasecmp(name, "Refresh")) code = IN_REFRESH;
        for (int i = 0; i < nfired; i++)
            if (fired[i] == at) seen = 1;
        if (code && !seen && now >= at && now < at + hold) {
            fired[nfired++] = at;
            rt_log("launcher: scripted key %d at %.1fs", code, now);  /* SH3_KEYS: what the script sent */
            return code;
        }
        if (*k == ',') k++;
    }
    return IN_NONE;
}

/* A file or folder dropped on the window (SDL_DROPFILE), handed to handle_drop by the screen loops. */
static char drop_file[700];

static int poll_input(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: exit(0);
        case SDL_DROPFILE:
            snprintf(drop_file, sizeof drop_file, "%s", e.drop.file);
            SDL_free(e.drop.file);
            return IN_DROP;
        case SDL_CONTROLLERDEVICEADDED: SDL_GameControllerOpen(e.cdevice.which); break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
            case SDLK_UP: return IN_UP;
            case SDLK_DOWN: return IN_DOWN;
            case SDLK_LEFT: return IN_LEFT;
            case SDLK_RIGHT: return IN_RIGHT;
            case SDLK_RETURN: case SDLK_SPACE: return IN_OK;
            case SDLK_ESCAPE: case SDLK_BACKSPACE: return IN_BACK;
            case SDLK_m: return IN_MODS;
            case SDLK_n: return IN_PAGE;
            case SDLK_r: return IN_REFRESH;
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

static const char *menu_items[] = {"Play", "Options", "Mods", "Credits", "Check for updates"};
#define N_ITEMS (int)(sizeof menu_items / sizeof *menu_items)

/* The update check runs on its own thread while the menu is up, so the menu is never held by the network. */
#ifdef _WIN32
static volatile LONG upd_state;  /* 0 not asked, 1 asking, 2 current, 3 newer, 4 error */
static char upd_banner[160];     /* "Update v1.11.0 is ready  -  Check for updates" */
#endif

static void draw_menu(int sel) {
    for (int i = 0; i < CW * CH; i++) canvas[i] = C_BG;
    if (logo_px) image(logo_px, logo_w, logo_h, (CW - 800) / 2, 170, 800, 800 * logo_h / logo_w);
    text_c(320, 32, C_DIM, PORT_NAME);
    for (int i = 0; i < N_ITEMS; i++) text_c(440 + i * 54, 44, i == sel ? C_SEL : C_TEXT, menu_items[i]);
#ifdef _WIN32
    if (upd_state == 3) text_c(700, 22, C_SEL, upd_banner);
    else
#endif
    text_c(700, 22, C_DIM, "Enter: select    M: mods    Esc: back");
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
    text_c(96, 46, C_TEXT, "Options");
    /* 13 rows on Windows (7 settings, five folder rows, Back): 30 px text in 42 px steps from 150, so the last
     * lands at 654. At the old 36 px text and 56 px steps from 190 they reached 806, and Install a mod and Back
     * were drawn past the 720 px canvas - unreachable and invisible. */
    for (int i = 0; i < N_ROWS; i++) {
        int y = 150 + i * 42;
        uint32_t c = i == sel ? C_SEL : C_TEXT;
        if (i == ROW_BACK) { text_c(y + 16, 30, c, "Back"); continue; }
#ifdef _WIN32
        if (i == ROW_FOLDER) {
            text(140, y, 30, c, "Game folder");
            path_text(y, CW - 140, 36, c, rt_game_dir);
            continue;
        }
        if (i == ROW_DISC) {
            text(140, y, 30, c, "Game disc or file");
            continue;
        }
        if (i == ROW_CHECK) {
            text(140, y, 30, c, "Check this copy");
            continue;
        }
        if (i == ROW_IMPORT) {
            text(140, y, 30, c, "Import a .zip download");
            continue;
        }
        if (i == ROW_MOD) {
            text(140, y, 30, c, "Install a mod");
            continue;
        }
#endif
        text(140, y, 30, c, opts[i].label);
        char v[64];
        snprintf(v, sizeof v, i == sel ? "< %s >" : "%s", opt_name(&opts[i]));
        text(CW - 140 - text_w(30, v), y, 30, c, v);
    }
    text_c(682, 20, C_DIM, "Left/Right: change   Enter: pick   Esc: back (saves sh3.ini)");
    if (row_note) text_c(706, 20, C_DIM, row_note);
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

/* The three things a picked or dropped path can lead to, shared by the rows and the window's drops. */
static void use_game_dir(const char *dir) {
    ini_save();
    if (rt_set_game_dir(dir)) {
        ini_load();      /* the new folder's sh3.ini */
#ifdef _WIN32
        nexus_set_key(rt_nexus_key);  /* and its nexus_key= */
#endif
        mods_init();     /* and its mods/ */
        sound_reload();  /* and its sd.afs */
        row_note = "Game location changed";
    } else
        row_note = "That folder can no longer be entered";
}

static void import_zip(const char *zip) {
    char dir[700], err[700] = "";
    if (rt_import_archive(zip, dir, sizeof dir, err, sizeof err)) {
        use_game_dir(dir);
        row_note = "Imported the download";
    } else
        rt_say(err);
}

/* A file or folder dropped on the launcher window: the same answer as Check this copy, acted on when it can be.
 * The point of dragging is not to have to find the row afterwards. */
static void handle_drop(const char *path) {
    char dir[700], msg[1200], err[700] = "";
    int kind = rt_check_copy(path, dir, sizeof dir, msg, sizeof msg);
    row_note = NULL;
    if (kind == RT_COPY_GAME) {
        use_game_dir(dir);
        if (row_note) rt_say(row_note);
    } else if (kind == RT_COPY_DOWNLOAD) {
        import_zip(path);
        if (row_note) rt_say(row_note);
    } else if (kind == RT_COPY_MOD) {
        char name[128];
        if (mods_install(path, name, sizeof name, err, sizeof err)) {
            static char note[160];
            mods_init();  /* and it is live for the game that starts after this menu */
            snprintf(note, sizeof note, "Installed the mod \"%s\"", name);
            row_note = note;
            rt_say(note);
        } else
            rt_say(err);
    } else if (kind == RT_COPY_PS2) {
        rt_ps2_offer(rt_ps2_path);
    } else
        rt_say(msg);
    if (row_note) rt_log("launcher: %s", row_note);
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
        if (in == IN_DROP) {
            handle_drop(drop_file);
            draw_options(sel);
            continue;
        }
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
#ifndef __SWITCH__
        /* The window changes as the row is picked, so the choice is seen before the game starts. */
        if (sel == OPT_WINDOW && o && (in == IN_LEFT || in == IN_RIGHT || in == IN_OK)) apply_window_opt();
#endif
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
            else if (got) use_game_dir(dir);
        }
#endif
#ifdef _WIN32
        /* Check this copy: name what was picked, then offer the step that uses it. */
        if (sel == ROW_CHECK && (in == IN_LEFT || in == IN_RIGHT || in == IN_OK)) {
            char picked[700], dir[700], msg[1200];
            if (rt_pick_copy(picked, sizeof picked)) {
                int kind = rt_check_copy(picked, dir, sizeof dir, msg, sizeof msg);
                if (kind == RT_COPY_GAME) {
                    row_note = "Playable copy";
                    if (win_confirm("Silent Hill 3", msg)) use_game_dir(dir);
                } else if (kind == RT_COPY_DOWNLOAD) {
                    row_note = "Full-Rip download";
                    if (win_confirm("Silent Hill 3", msg)) import_zip(picked);
                } else if (kind == RT_COPY_PS2) {
                    row_note = "PlayStation 2 disc";
                    if (!rt_ps2_offer(rt_ps2_path)) rt_say(msg);
                } else if (kind == RT_COPY_PC_DISC) {
                    row_note = "PC installation disc";
                    rt_say(msg);
                } else if (kind == RT_COPY_MOD) {
                    row_note = "Mod archive";
                    rt_say(msg);
                } else {
                    row_note = "Not a Silent Hill 3 copy";
                    rt_say(msg);
                }
                rt_log("launcher: %s", row_note);
            }
        }
#endif
#ifdef _WIN32
        /* The download: the Full-Rip zip is unpacked beside itself and the install inside it becomes the game
         * folder, so a player who has only downloaded the archive does not have to unpack it by hand. */
        if (sel == ROW_IMPORT && (in == IN_LEFT || in == IN_RIGHT || in == IN_OK)) {
            char zip[700];
            if (rt_pick_game_archive(zip, sizeof zip)) import_zip(zip);
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
        text_c(CH - 40, 32, rgb(235 * k >> 8, 235 * k >> 8, 235 * k >> 8), "Press Enter or A");  /* the picture's corners are taken */
        present(canvas, CW, CH);
    }
    sound(SND_OK);
    fade(0);
}


/* ---- mods --------------------------------------------------------------------------------------------
 * Two columns: what is installed from mods/, and the Nexus Mods list for Silent Hill 3. A downloads the
 * selected mod's main file and installs it; N opens its page (the way to download without Premium); R asks
 * Nexus for the list again. The key comes from nexus_key= in sh3.ini. */
#ifdef _WIN32
static NexusMod nx_mods[NEXUS_MAX_MODS];
static int nx_count;
static volatile LONG nx_list_done;      /* the fetch thread finished */
static char nx_list_err[600];
static char nx_note[600];

typedef struct {
    int mod;
    char zip[700];
    char err[600];
    volatile LONG ok;
} NxJob;
static NxJob nx_job;
static volatile LONG nx_job_done;

static DWORD __stdcall nx_list_thread(void *unused) {
    char err[600] = "";
    (void)unused;
    int n = nexus_list(nx_mods, NEXUS_MAX_MODS, err, sizeof err);
    if (n > 0) nx_count = n;
    else snprintf(nx_list_err, sizeof nx_list_err, "%s", err);
    rt_log("mods: Nexus list: %d%s%s", n, err[0] ? " - " : "", err);
    InterlockedExchange(&nx_list_done, 1);
    return 0;
}

static DWORD __stdcall nx_dl_thread(void *unused) {
    char err[600] = "", who[64] = "";
    (void)unused;
    int prem = nexus_account(who, sizeof who, err, sizeof err);
    rt_log("mods: account %d %s", prem, who);
    if (prem != 1) {
        if (prem == 0)
            snprintf(err, sizeof err,
                     "Automatic downloads need Nexus Premium. Press N to open the mod's page, download the file "
                     "there, and drop the .zip on this window to install it.");
        snprintf(nx_job.err, sizeof nx_job.err, "%s", err);
        rt_log("mods: refused: %s", err);
        InterlockedExchange(&nx_job_done, 1);
        return 0;
    }
    nx_job.ok = nexus_fetch_main(nx_job.mod, nx_job.zip, err, sizeof err);
    rt_log("mods: fetch %d -> %d %s", nx_job.mod, (int)nx_job.ok, err);
    if (!nx_job.ok) snprintf(nx_job.err, sizeof nx_job.err, "%s", err);
    InterlockedExchange(&nx_job_done, 1);
    return 0;
}

/* Long sentences over as many lines as they need. */
static void wrap_text(int x, int y, int size, uint32_t c, const char *s) {
    char line[300] = "";
    size_t ll = 0;
    int maxw = CW - 2 * x - 20;
    while (*s) {
        if (*s == '\n') {
            if (ll) text(x, y, size, c, line);
            y += size + 8;
            line[0] = 0;
            ll = 0;
            s++;
            continue;
        }
        const char *w = s;
        while (*w && *w != ' ' && *w != '\n') w++;
        char cand[320];
        snprintf(cand, sizeof cand, "%s%s%.*s", line, ll ? " " : "", (int)(w - s), s);
        if (text_w(size, cand) > maxw && ll) {
            text(x, y, size, c, line);
            y += size + 8;
            snprintf(line, sizeof line, "%.*s", (int)(w - s), s);
            ll = strlen(line);
        } else {
            snprintf(line, sizeof line, "%s", cand);
            ll = strlen(line);
        }
        s = *w ? w + 1 : w;
    }
    if (ll) text(x, y, size, c, line);
}

static void draw_mods(int sel) {
    for (int i = 0; i < CW * CH; i++) canvas[i] = C_BG;
    text_c(24, 34, C_TEXT, "MODS");
    text_c(76, 17, C_DIM, "installed from mods/");
    int n = mods_count();
    if (!n) text(70, 110, 20, C_DIM, "None yet.");
    for (int i = 0; i < n && i < 14; i++) text(70, 112 + i * 30, 20, C_TEXT, mods_name(i));

    text(660, 76, 17, C_DIM, "nexusmods.com/silenthill3/mods");
    if (!nexus_have_key()) {
        wrap_text(660, 110, 18, C_DIM,
                  "Add your own API key to sh3.ini as nexus_key=... (the API Access page on your Nexus "
                  "account), then reopen this screen: sh3.ini is read again each time it opens.");
    } else if (nx_count == 0 && !nx_list_done) {
        text(660, 110, 20, C_DIM, "Asking Nexus...");
    } else if (nx_count == 0) {
        wrap_text(660, 110, 18, C_DIM, nx_list_err[0] ? nx_list_err : "No mods came back.");
    } else {
        int first = 0;
        if (sel >= 12) first = sel - 11;
        for (int i = first; i < nx_count && i < first + 12; i++) {
            char row[220];
            snprintf(row, sizeof row, "%s%s", i == sel ? "> " : "  ", nx_mods[i].name);
            text(660, 110 + (i - first) * 30, 20, i == sel ? C_SEL : C_TEXT, row);
            char by[120];
            snprintf(by, sizeof by, "%.24s", nx_mods[i].author);
            text(660 + text_w(20, row) + 14, 113 + (i - first) * 30, 15, C_DIM, by);
        }
    }
    if (nx_job_done && nx_job.err[0]) wrap_text(70, 560, 18, C_DIM, nx_job.err);
    else if (nx_note[0]) wrap_text(70, 560, 18, C_DIM, nx_note);
    text_c(668, 22, C_DIM, "Enter: download and install   N: open the page   R: refresh   Esc: back");
    (void)sel;
}

static void mods_screen(void) {
    int sel = 0, dirty = 1, last_list = -1;
    char key_before[128];
    nx_job_done = 0;
    nx_job.err[0] = 0;
    nx_note[0] = 0;
    /* Read sh3.ini again here: adding a key should take effect when this screen is reopened, not at the next
     * start. A different key throws the old list away. */
    snprintf(key_before, sizeof key_before, "%s", rt_nexus_key);
    ini_nexus_key();
    if (strcmp(key_before, rt_nexus_key)) {
        nx_count = 0;
        nx_list_done = 0;
        nx_list_err[0] = 0;
    }
    if (nexus_have_key() && nx_count == 0) {
        nx_list_done = 0;
        nx_list_err[0] = 0;
        CreateThread(NULL, 0, nx_list_thread, NULL, 0, NULL);
    }
    fade(1);
    for (;;) {
        if (dirty) { draw_mods(sel); dirty = 0; }
        present(canvas, CW, CH);
        int in = poll_input();
        if (in == IN_DROP) {
            handle_drop(drop_file);
            mods_init();
            dirty = 1;
            continue;
        }
        if (nx_list_done != last_list) { last_list = nx_list_done; dirty = 1; }
        if (nx_job_done) {
            char name[128] = "", err[600] = "";
            if (nx_job.ok) {
                if (mods_install(nx_job.zip, name, sizeof name, err, sizeof err)) {
                    rt_log("mods: installed %s from %s", name, nx_job.zip);
                    mods_init();
                    snprintf(nx_note, sizeof nx_note, "Installed \"%s\". It is in mods/ now.", name);
                } else
                    snprintf(nx_note, sizeof nx_note, "%s", err);
                DeleteFileA(nx_job.zip);
            } else
                snprintf(nx_note, sizeof nx_note, "%s", nx_job.err);
            nx_job_done = 0;
            dirty = 1;
        }
        if (in == IN_NONE) continue;
        if (in == IN_BACK) break;
        if (in == IN_REFRESH) {
            nx_count = 0;
            nx_list_done = 0;
            nx_list_err[0] = 0;
            if (nexus_have_key()) CreateThread(NULL, 0, nx_list_thread, NULL, 0, NULL);
            dirty = 1;
            continue;
        }
        if (in == IN_PAGE) {
            if (nx_count) {
                char url[300];
                nexus_page_url(nx_mods[sel].id, url, sizeof url);
                rt_open_url(url);
                snprintf(nx_note, sizeof nx_note, "Opened the page in your browser: download the file there and "
                                                  "drop the .zip on this window.");
            } else
                snprintf(nx_note, sizeof nx_note, "Open https://www.nexusmods.com/silenthill3/mods in your browser.");
            dirty = 1;
            continue;
        }
        if (in == IN_DOWN) {
            if (nx_count) sel = (sel + 1) % nx_count;
            dirty = 1;
            continue;
        }
        if (in == IN_UP) {
            if (nx_count) sel = (sel + nx_count - 1) % nx_count;
            dirty = 1;
            continue;
        }
        if (in == IN_OK && nx_count && sel < nx_count) {
            char tmp[700], pat[760], safe[128];
            DWORD n = GetTempPathA(sizeof tmp, tmp);
            if (!n) { snprintf(nx_note, sizeof nx_note, "Windows did not name a temporary folder."); dirty = 1; continue; }
            size_t o = 0;
            for (const char *q = nx_mods[sel].name; *q && o < sizeof safe - 1; q++) {
                char c = *q;
                if (strchr("<>:\"/\\|?*", c)) c = '_';
                safe[o++] = c;
            }
            safe[o] = 0;
            if (!safe[0]) snprintf(safe, sizeof safe, "mod-%d", nx_mods[sel].id);
            snprintf(nx_job.zip, sizeof nx_job.zip, "%s%s.zip", tmp, safe);
            nx_job.mod = nx_mods[sel].id;
            nx_job.ok = 0;
            nx_job.err[0] = 0;
            snprintf(pat, sizeof pat, "%s", nx_mods[sel].name);
            snprintf(nx_note, sizeof nx_note, "Downloading \"%s\"...", pat);
            nx_job_done = 0;
            CreateThread(NULL, 0, nx_dl_thread, NULL, 0, NULL);
            dirty = 1;
            continue;
        }
        if (in == IN_LEFT || in == IN_RIGHT) dirty = 1;
    }
    fade(0);
}
#else
static void mods_screen(void) {}
#endif

/* ---- updates ------------------------------------------------------------------------------------------
 * GitHub's newest release of this port, from the launcher itself; the check needs no key and no account. A
 * newer release is downloaded and its files are put beside this executable by a script that waits for this
 * process to end - Windows locks a running .exe and the DLLs it has loaded - and then starts it again. Only the
 * files the release carries are written, so the game folder's sh3.ini, savedata/ and mods/ are left alone. */
#ifdef _WIN32
static UpdaterRelease upd_rel;
static char upd_err[600];
static char upd_dl_err[600];
static volatile LONG upd_dl;     /* 0 nothing, 1 downloading, 2 staged, 3 failed */
static volatile LONG upd_leave;  /* the files are staged: this process has to end for them to be replaced */
static int upd_offered;          /* the automatic offer was made once */

static DWORD __stdcall upd_check_thread(void *unused) {
    char err[600] = "";
    int r;
    (void)unused;
    r = updater_check(&upd_rel, err, sizeof err);
    if (r < 0) {
        snprintf(upd_err, sizeof upd_err, "%s", err);
        InterlockedExchange(&upd_state, 4);
    } else if (r > 0) {
        snprintf(upd_banner, sizeof upd_banner, "Update %s is ready  -  Check for updates", upd_rel.tag);
        InterlockedExchange(&upd_state, 3);
    } else
        InterlockedExchange(&upd_state, 2);
    rt_log("updater: newest is %s, this build is %s: %d%s%s", upd_rel.tag[0] ? upd_rel.tag : "none", updater_version(), r,
           err[0] ? " - " : "", err);
    return 0;
}

static void start_check(void) {
    if (upd_state != 0) return;
    InterlockedExchange(&upd_state, 1);
    CreateThread(NULL, 0, upd_check_thread, NULL, 0, NULL);
}

static DWORD __stdcall upd_install_thread(void *unused) {
    char zip[700], err[600] = "";
    int ok;
    (void)unused;
    ok = updater_download(upd_rel.url, zip, sizeof zip, err, sizeof err) && updater_install(zip, err, sizeof err);
    if (!ok) {
        snprintf(upd_dl_err, sizeof upd_dl_err, "%s", err);
        InterlockedExchange(&upd_dl, 3);
        rt_log("updater: %s not installed: %s", upd_rel.tag, err);
        return 0;
    }
    InterlockedExchange(&upd_dl, 2);
    InterlockedExchange(&upd_leave, 1);
    return 0;
}

/* A plain progress bar: the launcher draws on the CPU, so it is a rectangle. */
static void bar(int x, int y, int w, int h, int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    for (int py = y; py < y + h && py < CH; py++)
        for (int px = x; px < x + w && px < CW; px++) canvas[py * CW + px] = px < x + w * pct / 100 ? C_SEL : C_RULE;
}

static void draw_updates(void) {
    char line[400];
    for (int i = 0; i < CW * CH; i++) canvas[i] = C_BG;
    text_c(110, 56, C_TEXT, "Updates");
    snprintf(line, sizeof line, "This launcher is version %s.", updater_version());
    text_c(184, 26, C_DIM, line);
    if (upd_dl == 1) {
        text_c(268, 30, C_TEXT, "Downloading the new version...");
        bar(340, 320, 600, 10, updater_progress);
        snprintf(line, sizeof line, "%d%%", updater_progress);
        text_c(350, 22, C_DIM, line);
    } else if (upd_dl == 2) {
        text_c(268, 30, C_TEXT, "The update is ready. The launcher restarts itself.");
    } else if (upd_dl == 3) {
        text_c(268, 30, C_SEL, "The update could not be installed.");
        wrap_text(140, 312, 20, C_DIM, upd_dl_err);
    } else if (upd_state <= 1) {
        text_c(268, 30, C_TEXT, "Asking GitHub for the newest release...");
    } else if (upd_state == 2) {
        text_c(268, 30, C_TEXT, "That is the newest version.");
    } else if (upd_state == 3) {
        snprintf(line, sizeof line, "Version %s is available.", upd_rel.tag);
        text_c(250, 34, C_TEXT, line);
        snprintf(line, sizeof line, "%.110s", upd_rel.name);
        text_c(296, 22, C_DIM, line);
        if (upd_rel.size > 0) {
            snprintf(line, sizeof line, "The download is %lld MB.", upd_rel.size / (1024 * 1024));
            text_c(330, 22, C_DIM, line);
        }
        if (!updater_can_install()) {
            wrap_text(140, 390, 20, C_DIM,
                      "This build was made from the source tree, not from the release, so it is updated by "
                      "rebuilding it. Download the release's launcher to use Check for updates.");
        } else
            wrap_text(140, 390, 20, C_DIM,
                      "It replaces the launcher, SDL2.dll and README.txt beside this program, then starts the "
                      "launcher again. sh3.ini, savedata/ and mods/ are not touched.");
    } else {
        text_c(268, 30, C_SEL, "The check did not work.");
        wrap_text(140, 312, 20, C_DIM, upd_err);
    }
    if (upd_dl == 3) text_c(690, 22, C_DIM, "Enter: try again   Esc: back");
    else if (upd_state == 3 && upd_dl == 0 && updater_can_install())
        text_c(690, 22, C_DIM, "Enter: download and install   Esc: back");
    else text_c(690, 22, C_DIM, "Esc: back");
}

/* 1 when the update is staged and this process has to end for its files to be replaced. */
static int updates_screen(void) {
    start_check();
    fade(1);
    for (;;) {
        draw_updates();
        present(canvas, CW, CH);
        if (upd_leave) {
            draw_updates();
            present(canvas, CW, CH);
            SDL_Delay(700);  /* the script replaces the files the moment this process ends */
            return 1;
        }
        int in = poll_input();
        if (in == IN_BACK) break;
        if (in == IN_OK && (upd_state == 3 || upd_dl == 3) && (upd_dl == 0 || upd_dl == 3) &&
            (upd_dl == 3 || updater_can_install())) {
            sound(SND_OK);
            InterlockedExchange(&upd_dl, 1);
            updater_progress = 0;
            CreateThread(NULL, 0, upd_install_thread, NULL, 0, NULL);
        }
        SDL_Delay(16);
    }
    fade(0);
    return 0;
}
#else
static int updates_screen(void) { return 0; }
#endif

void rt_game_setup(void);  /* rt_core.c */

void launcher_run(void) {
#ifndef __SWITCH__
    /* Scripted runs (SH3_KEYS) go straight to the game; SH3_LAUNCHER=1 keeps the launcher for testing it. */
    if (getenv("SH3_KEYS") && !getenv("SH3_LAUNCHER")) { rt_game_setup(); return; }
#endif
    ini_load();  /* before the window: fullscreen= has to be known when it is created */
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
#ifdef _WIN32
    nexus_set_key(rt_nexus_key);
    if (opts[OPT_AUTOUPDATE].cur == 0) start_check();  /* silent: the menu carries the answer when it comes */
#endif
    draw_menu(0);
    if (getenv("SH3_DROP")) {  /* a scripted drop, for tests */
        handle_drop(getenv("SH3_DROP"));
        draw_menu(0);
    }
    fade(1);
    for (int sel = 0, dirty = 0;; ) {
        if (dirty) { draw_menu(sel); dirty = 0; }
        present(canvas, CW, CH);
#ifdef _WIN32
        /* Auto update: the check that ran while the warning screen was up found a newer release, so offer it
         * once. B in the screen plays on this version; the menu row is there for a manual check. */
        if (upd_state == 3 && !upd_offered) {
            upd_offered = 1;
            fade(0);
            if (updates_screen()) exit(0);
            draw_menu(sel);
            fade(1);
            continue;
        }
#endif
        int in = poll_input();
        if (in == IN_DROP) {
            handle_drop(drop_file);
            draw_menu(sel);
            continue;
        }
        if (in == IN_MODS) {
            fade(0);
            mods_screen();
            draw_menu(sel);
            fade(1);
            continue;
        }
        if (in == IN_UP || in == IN_DOWN) sel = (sel + (in == IN_UP ? N_ITEMS - 1 : 1)) % N_ITEMS, dirty = 1, sound(SND_CURSOR);
        if (in == IN_OK) sound(SND_OK);
        if (in == IN_OK && sel == 0) {
#ifdef PORT_LAUNCHER_ONLY
            /* The launcher-only build (build/wcheck.sh, and the release asset) has no recompiled game code. The
             * copy the player imported has its own sh3.exe, and rt_run_original asks before starting it; the
             * launcher's work is done once it is running. */
            if (rt_run_original()) {
                fade(0);
                exit(0);  /* the copy's own game is running: this process has nothing left to do */
            }
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
            mods_screen();
            draw_menu(sel);
            fade(1);
        }
        if (in == IN_OK && sel == 3) {
            fade(0);
            credits();
            draw_menu(sel);
            fade(1);
        }
        if (in == IN_OK && sel == 4) {
            fade(0);
            if (updates_screen()) exit(0);
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
