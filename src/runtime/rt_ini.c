/* sh3.ini, one key at a time: the last line that sets a key gets the new value, or one is appended, and every other
 * line - comments, the player's own keys, a nexus_key - is copied back as it was. Both the game (rt_set_resolution,
 * fullscreen_save in user32.c) and the launcher write their settings this way. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rt_log(const char *fmt, ...); /* rt_core.c */

/* An uncommented "key = value" line: spaces around the key and the '=' are optional, as in the game's own
 * sscanf(" key = ") parsers. */
static int sets_key(const char *line, const char *key) {
    while (*line == ' ' || *line == '\t') line++;
    size_t n = strlen(key);
    if (strncmp(line, key, n)) return 0;
    line += n;
    while (*line == ' ' || *line == '\t') line++;
    return *line == '=';
}

void rt_ini_set(const char *key, const char *val) {
    char *lines[512], line[288], set[288];
    int n = 0, last = -1;
    FILE *f = fopen("sh3.ini", "r");
    while (f && n < 512 && fgets(line, sizeof line, f)) lines[n++] = strdup(line);
    if (f) fclose(f);
    for (int i = 0; i < n; i++)
        if (sets_key(lines[i], key)) last = i;
    snprintf(set, sizeof set, "%s=%s\n", key, val);
    if (last >= 0) { free(lines[last]); lines[last] = strdup(set); }
    else if (n && lines[n - 1][strlen(lines[n - 1]) - 1] != '\n') {
        /* A last line with no newline would take the new key onto itself; keep that line, then add one. */
        size_t len = strlen(lines[n - 1]), add = strlen(set);
        char *joined = n < 512 ? malloc(len + add + 2) : NULL;
        if (joined) {
            snprintf(joined, len + add + 2, "%s\n%s", lines[n - 1], set);
            free(lines[n - 1]);
            lines[n - 1] = joined;
        }
    } else if (n < 512) lines[n++] = strdup(set);
    if (!(f = fopen("sh3.ini", "w"))) {
        for (int i = 0; i < n; i++) free(lines[i]);
        rt_log("sh3.ini: can't write %s", key);
        return;
    }
    for (int i = 0; i < n; i++) { fputs(lines[i], f); free(lines[i]); }
    fclose(f);
}
