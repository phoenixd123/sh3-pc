/* The file-system types and calls the Win32 bridge and the mod system share, 64-bit on Windows. */
#pragma once
#ifdef _WIN32
#include "win_posix.h"  /* stat_t, dirent, stat(), S_ISDIR */
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
typedef struct stat stat_t;
#endif
