/* Mod folder scanning, loose-file overrides and HD texture lookup. Plain C and stdio: no SDL, no GL, so it
 * can be unit-tested (tools/mod_test.c) without a window or the game. */
#include "mods.h"
#include "../runtime/fs.h"
#include "../runtime/rt.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "zip.h"
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

int mods_write_png(const char *path, int w, int h, int comp, const void *px) {
    return stbi_write_png(path, w, h, comp, px, comp * w);
}

/* ---- installing an archive --------------------------------------------------------------------------
 * The launcher unpacks a mod archive into mods/<name>/, the name taken from the archive's own file name. What the
 * archive holds decides its shape: a folder with files/ or mod.ini is a mod as it stands, and anything else - a
 * zip of data/..., or of one folder - is wrapped in files/, which is the path the game reads its own data through. */

static const char *base_name(const char *p) {
    const char *b = p;
    for (const char *s = p; *s; s++)
        if (*s == '/' || *s == '\\') b = s + 1;
    return b;
}

static int rm_tree(const char *path) {
    stat_t st;
    if (path && *path && stat(path, &st)) return 0;
    if (!S_ISDIR(st.st_mode)) return unlink(path) == 0;
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char child[600];
            snprintf(child, sizeof child, "%s/%s", path, e->d_name);
            rm_tree(child);
        }
        closedir(d);
    }
    return rmdir(path) == 0;
}

/* The one thing in `path` when it is the only thing and a folder, with its full path in out: an archive that wraps
 * everything in "MyMod/" names the mod itself. */
static int sole_dir(const char *path, char *out, size_t n) {
    DIR *d = opendir(path);
    char name[260] = "";
    int count = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(name, sizeof name, "%s", e->d_name);
            count++;
        }
        closedir(d);
    }
    if (count != 1) return 0;
    snprintf(out, n, "%s/%s", path, name);
    return is_dir(out);
}

/* The archive's name without its folder and extension, made into a folder name. */
static void mod_name_from(const char *zip, char *out, size_t n) {
    const char *b = base_name(zip), *dot = strrchr(b, '.');
    size_t len = dot && dot > b ? (size_t)(dot - b) : strlen(b), k = 0;
    for (size_t i = 0; i < len && k + 1 < n; i++) {
        unsigned char c = (unsigned char)b[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                 c == '.' || c == ' ';
        out[k++] = ok ? (char)c : '_';
    }
    while (k && out[k - 1] == ' ') k--;
    out[k] = 0;
    if (!k) snprintf(out, n, "mod");
}

int mods_install(const char *zip, char *name, size_t nn, char *err, size_t er) {
    const char *stage = "mods/.install";
    char folder[128], root[600], dest[600];
    unsigned char *data = NULL;
    FILE *f = fopen(zip, "rb");
    long size = 0;
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
    }
    if (!f || size <= 0 || (unsigned long)size > (512ul << 20)) {
        if (f) fclose(f);
        snprintf(err, er, "That archive cannot be read: %s", zip);
        return 0;
    }
    if (!(data = malloc((size_t)size)) || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        snprintf(err, er, "That archive cannot be read: %s", zip);
        return 0;
    }
    fclose(f);

    mod_name_from(zip, folder, sizeof folder);
    rm_tree(stage);
    mkdir("mods", 0755);
    if (mkdir(stage, 0755) && !is_dir(stage)) {
        free(data);
        snprintf(err, er, "The launcher cannot write to mods/ in this game folder.");
        return 0;
    }
    int files = zip_extract(data, (size_t)size, stage, err, er);
    free(data);
    if (files <= 0) {
        rm_tree(stage);
        return 0;
    }

    /* Where the mod starts. A folder holding files/ or mod.ini is a mod as it stands; an archive that wraps
     * everything in one folder (MyMod/) is the mod as well; and loose data/..., which is a zip of the game's own
     * paths, is wrapped in files/, the path the game reads its data through. */
    snprintf(root, sizeof root, "%s", stage);
    char sole[600], probe[700];
    if (sole_dir(stage, sole, sizeof sole)) {
        snprintf(probe, sizeof probe, "%s/files", sole);
        int has = is_dir(probe);
        snprintf(probe, sizeof probe, "%s/mod.ini", sole);
        has = has || is_file(probe);
        const char *base = base_name(sole);
        if (has || (strcasecmp(base, "data") && strcasecmp(base, "files"))) snprintf(root, sizeof root, "%s", sole);
    }

    char filesdir[700], ini[700];
    snprintf(filesdir, sizeof filesdir, "%s/files", root);
    snprintf(ini, sizeof ini, "%s/mod.ini", root);
    if (!is_dir(filesdir) && !is_file(ini)) {
        /* Loose files: they are what the game reads over its own data/, so they go under files/. */
        char inner[700], names[64][260];
        int count = 0;
        snprintf(inner, sizeof inner, "%s/files", root);
        if (mkdir(inner, 0755) && !is_dir(inner)) {
            rm_tree(stage);
            snprintf(err, er, "The launcher cannot write to mods/ in this game folder.");
            return 0;
        }
        DIR *d = opendir(root);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) && count < 64) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || !strcmp(e->d_name, "files")) continue;
                snprintf(names[count++], sizeof names[0], "%s", e->d_name);
            }
            closedir(d);
        }
        for (int i = 0; i < count; i++) {
            char from[700], to[700];
            snprintf(from, sizeof from, "%s/%s", root, names[i]);
            snprintf(to, sizeof to, "%s/%s", inner, names[i]);
            if (rename(from, to)) {
                rm_tree(stage);
                snprintf(err, er, "The launcher cannot move the mod's files into place.");
                return 0;
            }
        }
    }

    snprintf(dest, sizeof dest, "mods/%s", folder);
    if (is_dir(dest) || is_file(dest)) {
        rm_tree(stage);
        snprintf(err, er, "There is already a mod called \"%s\".\n\nDelete mods/%s first, or rename the archive.",
                 folder, folder);
        return 0;
    }
    if (rename(root, dest)) {
        rm_tree(stage);
        snprintf(err, er, "The launcher cannot put the mod in mods/%s.", folder);
        return 0;
    }
    if (strcmp(root, stage)) rm_tree(stage);  /* the staging folder, now empty */
    snprintf(name, nn, "%s", folder);
    rt_log("mods: installed \"%s\" from %s", folder, base_name(zip));
    return 1;
}
