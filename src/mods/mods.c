/* Mod folder scanning, loose-file overrides and HD texture lookup. Plain C and stdio: no SDL, no GL, so it
 * can be unit-tested (tools/mod_test.c) without a window or the game. */
#include "mods.h"
#include "../runtime/fs.h"
#include "../runtime/rt.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#ifndef _WIN32
#include <strings.h>
#endif

#define MAX_MODS 64

typedef struct Mod {
    char dir[256];        /* folder name under mods/ */
    char name[96];        /* what the log calls it */
    int enabled, priority, order;
} Mod;

static Mod mods[MAX_MODS];
static int nmods;
static int dump_on;

static int is_dir(const char *p) {
    stat_t st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
static int is_file(const char *p) {
    stat_t st;
    return stat(p, &st) == 0 && !S_ISDIR(st.st_mode);
}

/* Reads one key from a mod's ini ("key=value", whitespace and a # or ; comment tolerated). */
static int ini_get(const char *path, const char *key, char *out, size_t n) {
    FILE *f = fopen(path, "r");
    char line[256];
    size_t kl = strlen(key);
    out[0] = 0;
    while (f && fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, key, kl)) continue;
        p += kl;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        for (p++; *p == ' ' || *p == '\t'; p++) {}
        size_t i = 0;
        while (*p && *p != '\n' && *p != '\r' && i < n - 1) out[i++] = *p++;
        out[i] = 0;
        while (i && (out[i - 1] == ' ' || out[i - 1] == '\t')) out[--i] = 0;
    }
    if (f) fclose(f);
    return out[0] != 0;
}

static void add_mod(const char *dir) {
    if (nmods == MAX_MODS) return;
    char path[512], ini[512], val[64];  /* ini and val are separate: ini_get reads the file named by ini */
    Mod *m = &mods[nmods];
    snprintf(m->dir, sizeof m->dir, "%s", dir);
    snprintf(path, sizeof path, "mods/%s", dir);
    /* A folder counts as a mod if it says so (mod.ini) or has the folders a mod would have. */
    snprintf(ini, sizeof ini, "%s/mod.ini", path);
    int has_ini = is_file(ini);
    char f[512];
    snprintf(f, sizeof f, "%s/files", path);
    int has_files = is_dir(f);
    snprintf(f, sizeof f, "%s/textures", path);
    int has_tex = is_dir(f);
    if (!has_ini && !has_files && !has_tex) return;

    snprintf(m->name, sizeof m->name, "%s", dir);
    m->enabled = 1;
    m->priority = 0;
    m->order = INT_MAX;  /* until load_order.txt names it: unlisted mods sort after the listed ones */
    if (has_ini) {
        ini_get(ini, "name", m->name, sizeof m->name);
        if (ini_get(ini, "enabled", val, sizeof val)) m->enabled = atoi(val) != 0;
        if (ini_get(ini, "priority", val, sizeof val)) m->priority = atoi(val);
    }
    nmods++;
}

/* Load order: mods/load_order.txt first (one folder per line, earlier wins), then the rest by priority
 * descending, then by folder name, so the order does not depend on the file system. The three keys have to
 * stay distinct to do that: an unlisted mod keeps order = INT_MAX, which is what lets priority decide. */
static int cmp_mods(const void *pa, const void *pb) {
    const Mod *a = pa, *b = pb;
    if (a->order != b->order) return a->order - b->order;
    if (a->priority != b->priority) return b->priority - a->priority;
    return strcmp(a->dir, b->dir);
}

void mods_init(void) {
    nmods = 0;
    DIR *d = opendir("mods");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            add_mod(e->d_name);
        }
        closedir(d);
    }
    /* load_order.txt: listed folders first, in that order; anything not listed keeps order = MAX. */
    FILE *lo = fopen("mods/load_order.txt", "r");
    if (lo) {
        char line[256];
        int rank = 0;
        while (fgets(line, sizeof line, lo)) {
            char *p = line, *s;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == ';' || !*p) continue;
            s = p + strlen(p);
            while (s > p && (s[-1] == '\n' || s[-1] == '\r' || s[-1] == ' ' || s[-1] == '\t')) *--s = 0;
            if (strncmp(p, "mods/", 5) == 0) p += 5;
            for (int i = 0; i < nmods; i++)
                if (!strcasecmp(mods[i].dir, p)) mods[i].order = rank++;
        }
        fclose(lo);
    }
    qsort(mods, nmods, sizeof *mods, cmp_mods);
    int active = 0;
    for (int i = 0; i < nmods; i++) {
        if (mods[i].enabled) active++;
        rt_log("mods: %s %s (priority %d)", mods[i].enabled ? "loaded" : "disabled", mods[i].name, mods[i].priority);
    }
    if (nmods) rt_log("mods: %d of %d enabled", active, nmods);
    if (getenv("SH3_TEXDUMP")) dump_on = 1;
}

int mods_count(void) { return nmods; }
const char *mods_name(int i) { return i >= 0 && i < nmods ? mods[i].name : ""; }

const char *mods_override(const char *game_path, char *out, size_t n) {
    if (!game_path || !*game_path) return NULL;
    while (*game_path == '/' || *game_path == '\\') game_path++;
    for (int i = 0; i < nmods; i++) {
        if (!mods[i].enabled) continue;
        snprintf(out, n, "mods/%s/files/%s", mods[i].dir, game_path);
        if (is_file(out)) return out;
    }
    return NULL;
}

int mods_hd_enabled(void) { return nmods > 0 && rt_hd_textures; }

/* Textures are keyed by a hash of the pixels the game uploaded, so a pack never has to name game-internal
 * files: run with texdump=1, upscale the PNGs, drop them back in under the same names. */
unsigned char *mods_hd_texture(uint64_t key, int *w, int *h) {
    if (!mods_hd_enabled()) return NULL;
    char path[512];
    for (int i = 0; i < nmods; i++) {
        if (!mods[i].enabled) continue;
        snprintf(path, sizeof path, "mods/%s/textures/%016llx.png", mods[i].dir, (unsigned long long)key);
        int c;
        stbi_set_flip_vertically_on_load(0);
        unsigned char *px = stbi_load(path, w, h, &c, 4);
        if (!px) {
            rt_log("mods: %s is not a PNG stb_image can read", path);
            continue;
        }
        rt_log("mods: %s replaces a texture (%dx%d)", path, *w, *h);
        return px;
    }
    return NULL;
}

int mods_dumping(void) { return dump_on; }

void mods_dump_texture(uint64_t key, uint32_t w, uint32_t h, uint32_t fmt, const void *rgba) {
    if (!dump_on || !rgba) return;
    char path[512];
    snprintf(path, sizeof path, "texdump/%016llx.png", (unsigned long long)key);
    if (is_file(path)) return;  /* already dumped: textures are uploaded again on palette changes */
    stbi_write_png(path, (int)w, (int)h, 4, rgba, (int)w * 4);
    FILE *idx = fopen("texdump/index.txt", "a");
    if (idx) {
        fprintf(idx, "%016llx %ux%u fmt %u\n", (unsigned long long)key, w, h, fmt);
        fclose(idx);
    }
}
