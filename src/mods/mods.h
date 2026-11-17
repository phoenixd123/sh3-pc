/* Mods: a folder of asset overrides the game reads instead of its own data/.
 *
 * mods/<name>/mod.ini      name=/priority=/enabled=
 * mods/<name>/files/...    any game file, at the same path the game opens ("data/tex/a.tex")
 * mods/<name>/textures/    HD textures, named by the hash of the texture they replace (<key>.png)
 * mods/load_order.txt      one mod folder per line, first line wins
 *
 * Nothing here contains or derives from the game's assets: a texture pack is made by the player, from their
 * own copy, with tools/make_hd_pack.py. See MODDING.md. */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* Scans mods/ and reads the load order. Call once, after chdir into the game folder. */
void mods_init(void);
int mods_count(void);
const char *mods_name(int i);

/* A file override for a game-relative path the game is opening for reading, or NULL.
 * `out` holds the path when one is returned. */
const char *mods_override(const char *game_path, char *out, size_t n);

/* HD replacement of one texture: malloc'd RGBA8 pixels (free() them), with the width and height written to
 * `w` and `h`, or NULL. `key` identifies the original, see tex_key() in d3d8_res.c. */
unsigned char *mods_hd_texture(uint64_t key, int *w, int *h);
int mods_hd_enabled(void);   /* hd= in sh3.ini and at least one mod */

/* texdump=1: writes each texture the game uploads to texdump/<key>.png, for pack authors to upscale. */
int mods_dumping(void);
void mods_dump_texture(uint64_t key, uint32_t w, uint32_t h, uint32_t fmt, const void *rgba);
