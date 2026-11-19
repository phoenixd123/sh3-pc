/* The ZIP reader behind "Install a mod". Deflate comes from the zlib decoder stb_image already carries, so an
 * archive is unpacked the same way on every platform and nothing outside the tree is needed. */
#include "zip.h"
#include "../runtime/fs.h"
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define SIG_EOCD 0x06054b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_LOCAL 0x04034b50u
#define MAX_ENTRIES 8192

static unsigned le16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned long le32(const unsigned char *p) {
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}
static int is_sep(char c) { return c == '/' || c == '\\'; }
static int is_dir(const char *p) { stat_t st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

static int bad(char *err, size_t n, const char *what) {
    if (err && n) snprintf(err, n, "%s", what);
    return -1;
}

/* An entry name made relative: no drive, no root, no "..", one separator between parts. 0 when it cannot be. */
static int safe_name(const char *in, size_t len, char *out, size_t n) {
    size_t k = 0;
    for (size_t i = 0; i < len; i++) {
        char c = in[i] == '\\' ? '/' : in[i];
        if (c == ':') return 0;
        if (is_sep(c)) {
            if (!k || out[k - 1] == '/') continue;
            c = '/';
        }
        if (k + 2 >= n) return 0;
        out[k++] = c;
    }
    out[k] = 0;
    for (char *p = out; p && *p;) {
        char *e = strchr(p, '/');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == 2 && p[0] == '.' && p[1] == '.') return 0;
        p = e ? e + 1 : NULL;
    }
    return k > 0;
}

static void mkdirs(const char *path) {
    char tmp[700];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (!is_sep(*p)) continue;
        *p = 0;
        mkdir(tmp, 0755);
        *p = '/';
    }
    mkdir(tmp, 0755);
}

int zip_extract(const unsigned char *d, size_t n, const char *dest, char *err, size_t errn) {
    if (!d || n < 22) return bad(err, errn, "That file is not a ZIP archive.");
    /* The end-of-directory record: the last one in the file is the real one. */
    size_t eocd = 0;
    int found = 0;
    for (size_t i = n - 22 + 1; i-- > 0;) {
        if (le32(d + i) == SIG_EOCD) { eocd = i, found = 1; break; }
        if (i == 0) break;
    }
    if (!found) return bad(err, errn, "That file is not a ZIP archive.");
    unsigned long count = le16(d + eocd + 10), cd = le32(d + eocd + 16);
    if (!count) return bad(err, errn, "That archive has no files in it.");
    if (count > MAX_ENTRIES) return bad(err, errn, "That archive has far more entries than a mod ever needs.");
    if (cd + 46 > n) return bad(err, errn, "That archive's file list is damaged.");

    int written = 0;
    for (size_t p = (size_t)cd, i = 0; i < count; i++) {
        if (p + 46 > n || le32(d + p) != SIG_CENTRAL) return bad(err, errn, "That archive's file list is damaged.");
        unsigned method = le16(d + p + 10);
        unsigned long csize = le32(d + p + 20), lho = le32(d + p + 42);
        size_t nl = le16(d + p + 28), el = le16(d + p + 30), cl = le16(d + p + 32);
        const char *name = (const char *)d + p + 46;
        p += 46 + nl + el + cl;
        if (p > n) return bad(err, errn, "That archive's file list is damaged.");

        char rel[600];
        if (!safe_name(name, nl, rel, sizeof rel))
            return bad(err, errn, "That archive has a path that would land outside the game folder.");
        if (rel[strlen(rel) - 1] == '/') {  /* a folder of its own; the files inside make it otherwise */
            rel[strlen(rel) - 1] = 0;
            if (!rel[0]) continue;
            char dir[700];
            snprintf(dir, sizeof dir, "%s/%s", dest, rel);
            mkdirs(dir);
            continue;
        }
        if (lho + 30 > n || le32(d + lho) != SIG_LOCAL) return bad(err, errn, "That archive's file list is damaged.");
        size_t start = (size_t)lho + 30 + le16(d + lho + 26) + le16(d + lho + 28);
        if (start + csize > n) return bad(err, errn, "That archive is truncated.");

        const unsigned char *body = d + start, *out = body;
        size_t len = (size_t)csize;
        unsigned char *inflated = NULL;
        if (method == 8) {
            if (csize > INT_MAX) return bad(err, errn, "A file in that archive is too big for the mod installer.");
            int outlen = 0;
            inflated = (unsigned char *)stbi_zlib_decode_noheader_malloc((const char *)body, (int)csize, &outlen);
            if (!inflated) return bad(err, errn, "A file in that archive is compressed in a way this cannot read.");
            out = inflated;
            len = (size_t)outlen;
        } else if (method != 0) {
            return bad(err, errn, "That archive uses a compression this cannot read (only stored and deflate).");
        }

        char path[700], dir[700];
        snprintf(path, sizeof path, "%s/%s", dest, rel);
        snprintf(dir, sizeof dir, "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash) { *slash = 0; mkdirs(dir); }
        FILE *f = fopen(path, "wb");
        if (!f) {
            free(inflated);
            return bad(err, errn, "A file in that archive could not be written: Windows refuses the name, or the "
                                  "disk is full.");
        }
        size_t put = fwrite(out, 1, len, f);
        fclose(f);
        free(inflated);
        if (put != len) return bad(err, errn, "A file in that archive could not be written: the disk is full.");
        written++;
    }
    if (!written) return bad(err, errn, "That archive has no files in it.");
    return written;
}
