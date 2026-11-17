/* For the files that may use the Win32 API directly (host.c, rt_core.c): windows.h first, because the mingw
 * CRT headers define __C89_NAMELESS and friends, which change windows.h's own layout when they come first. */
#pragma once
#ifdef _WIN32
#include <windows.h>
#include "win_posix.h"
#endif
