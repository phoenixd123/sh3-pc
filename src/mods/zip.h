/* A ZIP reader just big enough for a mod archive: stored and deflated entries, no ZIP64 and no encryption.
 * The archive is parsed out of memory, so the caller reads the file; nothing here knows what a mod is. */
#pragma once
#include <stddef.h>

/* Writes every file entry of `data` under `dest`, creating it. Returns the number of files written, or -1 with a
 * sentence in `err` (n bytes). Entries that would land outside `dest` are refused, as is a whole archive with
 * nothing but folders in it. */
int zip_extract(const unsigned char *data, size_t n, const char *dest, char *err, size_t errn);
