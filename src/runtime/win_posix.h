/* The POSIX pieces the runtime uses, on Windows: declarations only, no <windows.h>.
 *
 * kernel32.c reimplements the Win32 calls and defines C functions under the SDK's own names (CreateFileA,
 * CreateThread, ReadFile...), so windows.h cannot be included there: its prototypes would clash with every one
 * of them. What that file needs from POSIX -- locks, folder listing, stat, time -- is declared here and
 * implemented in win_posix.c, the one place that includes windows.h for them. host.c and rt_core.c, which do
 * not shadow SDK names, include win_platform.h instead: windows.h first, then this. */
#pragma once
#ifdef _WIN32

#define _FILE_OFFSET_BITS 64  /* data/ holds files past 2 GB */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <sys/types.h>
#include <sys/stat.h>

/* ---- pthreads subset ---- */
/* The initializers are zero: the lock object is created on first use (win_posix.c), so a static
 * PTHREAD_MUTEX_INITIALIZER works as it does on POSIX without a constructor in sight. Every lock is recursive,
 * because the bridge turns each guest CRITICAL_SECTION into one and Windows allows re-entry. */
typedef struct { void *p; } pthread_mutex_t;
typedef struct { void *p; } pthread_cond_t;
#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER {0}

typedef int pthread_mutexattr_t;
typedef int pthread_condattr_t;
#define PTHREAD_MUTEX_RECURSIVE 1
#define pthread_mutexattr_init(a) (*(a) = 0, 0)
#define pthread_mutexattr_settype(a, t) (*(a) = (t), 0)
#define pthread_mutexattr_destroy(a) ((void)(a), 0)
#define pthread_condattr_init(a) (*(a) = 0, 0)
#define pthread_condattr_setclock(a, c) (*(a) = (c), 0)
#define pthread_condattr_destroy(a) ((void)(a), 0)

int pthread_mutex_init(pthread_mutex_t *m, const void *attr);
int pthread_mutex_destroy(pthread_mutex_t *m);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_cond_init(pthread_cond_t *c, const void *attr);
int pthread_cond_destroy(pthread_cond_t *c);
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *ts);
int pthread_cond_broadcast(pthread_cond_t *c);
int pthread_cond_signal(pthread_cond_t *c);

/* ---- dirent (CRT's _findfirst; the runtime only lists the game's own folder) ---- */
typedef struct WinFind DIR;
struct dirent { char d_name[260]; };
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);

/* ---- stat, 64-bit so the sizes of data/ are not truncated ----
 * _FILE_OFFSET_BITS 64 already maps stat/fstat/fseeko/ftello onto the 64-bit CRT calls (sys/stat.h and
 * stdio.h do it), so only the type needs a name that reads the same on both platforms. */
typedef struct _stat64 stat_t;
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

/* ---- time ---- */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
int rt_clock_gettime(int clk, struct timespec *ts);
void rt_sleep_us(unsigned us);
#define clock_gettime(clk, ts) rt_clock_gettime((clk), (ts))
#define usleep(us) rt_sleep_us(us)
#define sleep(seconds) rt_sleep_us((unsigned)((long long)(seconds) * 1000000))

/* ---- unistd and friends ---- */
#define fileno _fileno
#define isatty _isatty
#define chdir _chdir
#define getcwd _getcwd
#define unlink _unlink
#define rmdir _rmdir
#define mkdir(p, m) _mkdir(p)
#define ftruncate(fd, len) (_chsize_s((fd), (len)) == 0 ? 0 : -1)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define off_t long long

/* gmtime_r/localtime_r are the POSIX names; the CRT has the _s forms, whose arguments are the other way
 * round (the result first). */
static inline struct tm *gmtime_r(const time_t *t, struct tm *out) { return gmtime_s(out, t) == 0 ? out : NULL; }
static inline struct tm *localtime_r(const time_t *t, struct tm *out) { return localtime_s(out, t) == 0 ? out : NULL; }

/* ---- the launcher's pickers and message box (win_posix.c) ---- */
/* The shell's folder dialog, and the open-file dialog for a file or a disc. Both open at `start` when it is set and
 * return 1 with the path; 0 when the player closed them. `filter` is a Win32 filter string (pairs of description and
 * pattern, ending in two NULs) or NULL for "all files". */
int win_pick_path(const char *title, const char *start, char *out, size_t n);
int win_pick_file(const char *title, const char *start, const char *filter, char *out, size_t n);
void win_message(const char *title, const char *text);

#endif /* _WIN32 */
