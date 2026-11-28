/* tests/ini_test.c: rt_ini_set (src/runtime/rt_ini.c) on a real sh3.ini, without the game. Every test:
 * bash tests/run.sh.
 *
 * The file is the one thing the port rewrites on the player's disk: a key it does not touch (nexus_key, comments,
 * the settings it is not changing) has to come back byte for byte.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void rt_ini_set(const char *key, const char *val); /* src/runtime/rt_ini.c */

void rt_log(const char *fmt, ...) { /* rt_core.c's log, here so rt_ini.c links */
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int fails;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static char *slurp(const char *path) {
    static char buf[4096];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    /* Text mode on Windows writes \r\n; the line endings are not what this test is about. */
    char *r = buf, *w = buf;
    for (; *r; r++) if (*r != '\r') *w++ = *r;
    *w = 0;
    return buf;
}

static void put(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

int main(void) {
    static const char *const start =
        "; the player's own sh3.ini\n"
        "resolution=1280x720\n"
        "cores=3   ; three\n"
        "nexus_key=abcdef0123456789\n"
        "filter=1\n";

    put("sh3.ini", start);
    rt_ini_set("resolution", "1024x768");
    const char *got = slurp("sh3.ini");
    CHECK(!strcmp(got, "; the player's own sh3.ini\n"
                        "resolution=1024x768\n"
                        "cores=3   ; three\n"
                        "nexus_key=abcdef0123456789\n"
                        "filter=1\n"),
          "the rewritten file is not the old one with one line changed");

    rt_ini_set("resolution", "1600x1200"); /* the second write replaces the line again, it does not add one */
    got = slurp("sh3.ini");
    CHECK(strstr(got, "resolution=1280x720") == NULL, "the old resolution is still in the file");
    CHECK(strstr(got, "resolution=1600x1200\n") != NULL, "the new resolution is not in the file");
    int count = 0;
    for (const char *p = got; (p = strstr(p, "resolution=")); p++) count++;
    CHECK(count == 1, "the file has more than one resolution line");
    CHECK(strstr(got, "nexus_key=abcdef0123456789\n") != NULL, "a key the writer does not touch was lost");

    /* Spaces around the key and '=' are the same key, as the game reads them. */
    put("sh3.ini", "fullscreen = 0\nresolution=1280x720\n");
    rt_ini_set("resolution", "960x720");
    CHECK(!strcmp(slurp("sh3.ini"), "fullscreen = 0\nresolution=960x720\n"), "a spaced line moved or changed");

    /* A key the file lacks is appended, and the file keeps its last line whole. */
    put("sh3.ini", "resolution=1280x720");
    rt_ini_set("fullscreen", "1");
    CHECK(!strcmp(slurp("sh3.ini"), "resolution=1280x720\nfullscreen=1\n"),
          "a missing key was not appended after the last line");

    /* No sh3.ini at all: one is written with just the key. */
    remove("sh3.ini");
    rt_ini_set("resolution", "640x480");
    CHECK(!strcmp(slurp("sh3.ini"), "resolution=640x480\n"), "no file did not become a one-key file");

    remove("sh3.ini");
    printf(fails ? "%d check(s) failed\n" : "ini: all checks passed\n", fails);
    return fails != 0;
}
