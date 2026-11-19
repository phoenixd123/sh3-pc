/* The Windows implementation of win_posix.h: SRW-lock mutexes, CONDITION_VARIABLE waits and a dirent over
 * the CRT's _findfirst. Only this file includes windows.h for the runtime's POSIX calls -- see win_posix.h
 * for why kernel32.c cannot. */
#include <windows.h>
#include "win_posix.h"
#include <shlobj.h>  /* SHBrowseForFolderA: the launcher asks for the game folder with the shell's dialog */
#include <commdlg.h>  /* GetOpenFileNameA: and for sh3.exe or a disc with the open-file dialog */

/* ---- locks ----
 * Created on first use, so a static PTHREAD_MUTEX_INITIALIZER (all zeroes) is still valid. The slot is
 * published with a pointer compare-exchange: two threads racing to lock the same untouched mutex both build
 * one, one wins, the loser frees theirs. */
typedef struct { SRWLOCK l; DWORD owner; unsigned depth; } WinMutex;
typedef struct { CONDITION_VARIABLE c; } WinCond;

static WinMutex *mutex_of(pthread_mutex_t *m) {
    WinMutex *x = (WinMutex *)m->p;
    if (x) return x;
    WinMutex *n = calloc(1, sizeof *n);
    InitializeSRWLock(&n->l);
    void *old = InterlockedCompareExchangePointer(&m->p, n, NULL);
    if (old) { free(n); return (WinMutex *)old; }
    return n;
}

static WinCond *cond_of(pthread_cond_t *c) {
    WinCond *x = (WinCond *)c->p;
    if (x) return x;
    WinCond *n = calloc(1, sizeof *n);
    InitializeConditionVariable(&n->c);
    void *old = InterlockedCompareExchangePointer(&c->p, n, NULL);
    if (old) { free(n); return (WinCond *)old; }
    return n;
}

int pthread_mutex_init(pthread_mutex_t *m, const void *attr) { (void)attr; mutex_of(m); return 0; }
int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
/* Recursive, as the guest's CRITICAL_SECTIONs are: Windows lets the owning thread re-enter, and the bridge
 * gives every one of them a mutex here. Only the owner writes owner/depth while it holds the lock, so the
 * check before acquiring needs no ordering. */
int pthread_mutex_lock(pthread_mutex_t *m) {
    WinMutex *x = mutex_of(m);
    DWORD me = GetCurrentThreadId();
    if (x->depth && x->owner == me) { x->depth++; return 0; }
    AcquireSRWLockExclusive(&x->l);
    x->owner = me;
    x->depth = 1;
    return 0;
}
int pthread_mutex_unlock(pthread_mutex_t *m) {
    WinMutex *x = mutex_of(m);
    if (x->depth > 1) { x->depth--; return 0; }
    x->owner = 0;
    x->depth = 0;
    ReleaseSRWLockExclusive(&x->l);
    return 0;
}

int pthread_cond_init(pthread_cond_t *c, const void *attr) { (void)attr; cond_of(c); return 0; }
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    /* The caller holds the mutex, so its lock object already exists: this is the same SRWLOCK. */
    return SleepConditionVariableSRW(&cond_of(c)->c, &mutex_of(m)->l, INFINITE, 0) ? 0 : EINVAL;
}

/* Nanoseconds since 1970: FILETIME counts 100 ns ticks from 1601. */
static unsigned long long filetime_ns(void) {
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (u.QuadPart - 116444736000000000ull) * 100ull;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *ts) {
    unsigned long long ns = filetime_ns();
    long long ms = ((long long)ts->tv_sec - (long long)(ns / 1000000000ull)) * 1000 +
                   ((long long)ts->tv_nsec - (long long)(ns % 1000000000ull)) / 1000000;
    if (ms < 1) ms = 1;  /* a deadline that has passed still gets one last look at the predicate */
    if (ms > 0xFFFFFFF0ll) ms = 0xFFFFFFF0ll;
    return SleepConditionVariableSRW(&cond_of(c)->c, &mutex_of(m)->l, (DWORD)ms, 0) ? 0 : ETIMEDOUT;
}

int pthread_cond_broadcast(pthread_cond_t *c) { WakeAllConditionVariable(&cond_of(c)->c); return 0; }
int pthread_cond_signal(pthread_cond_t *c) { WakeConditionVariable(&cond_of(c)->c); return 0; }

/* ---- time: a realtime clock for deadlines and a monotonic one for the frame limiter ---- */
int rt_clock_gettime(int clk, struct timespec *ts) {
    if (clk == CLOCK_MONOTONIC) {
        LARGE_INTEGER f, c;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&c);
        long long ns = (long long)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
        ts->tv_sec = (time_t)(ns / 1000000000ll);
        ts->tv_nsec = (long)(ns % 1000000000ll);
        return 0;
    }
    unsigned long long ns = filetime_ns();
    ts->tv_sec = (time_t)(ns / 1000000000ull);
    ts->tv_nsec = (long)(ns % 1000000000ull);
    return 0;
}

/* Sleep's granularity is a millisecond, where usleep took microseconds: a sub-millisecond sleep still yields
 * (Sleep(0)) rather than rounding up to 1 ms, which is what the bridge's Sleep(0) relies on. */
void rt_sleep_us(unsigned us) { Sleep((DWORD)(us >= 1000 ? us / 1000 : 0)); }

/* ---- dirent ---- */
typedef struct WinFindImpl {
    intptr_t h;
    struct _finddata_t fd;
    struct dirent e;
    int first;
} WinFindImpl;

DIR *opendir(const char *path) {
    char pat[1024];
    snprintf(pat, sizeof pat, "%s%s*", path && *path ? path : ".", path && *path ? "/" : "");
    struct _finddata_t fd;
    intptr_t h = _findfirst(pat, &fd);
    if (h == -1) return NULL;
    WinFindImpl *d = calloc(1, sizeof *d);
    if (!d) { _findclose(h); return NULL; }
    d->h = h;
    d->fd = fd;
    snprintf(d->e.d_name, sizeof d->e.d_name, "%s", fd.name);
    d->first = 1;
    return (DIR *)d;
}

struct dirent *readdir(DIR *dir) {
    WinFindImpl *d = (WinFindImpl *)dir;
    if (!d) return NULL;
    if (d->first) { d->first = 0; return &d->e; }  /* _findfirst already returned the first name */
    if (_findnext(d->h, &d->fd) != 0) return NULL;
    snprintf(d->e.d_name, sizeof d->e.d_name, "%s", d->fd.name);
    return &d->e;
}

int closedir(DIR *dir) {
    WinFindImpl *d = (WinFindImpl *)dir;
    if (!d) return 0;
    _findclose(d->h);
    free(d);
    return 0;
}

/* ---- the launcher's pickers ----
 * Two dialogs, each doing the one job it is built for. The shell's folder browser has no dependable way to return a
 * file (BIF_BROWSEINCLUDEFILES shows them, but which item is returned is the shell's business), so a file or a disc
 * goes through the real open-file dialog instead. Neither guesses from an extension: whatever comes back is read by
 * rt_core's resolver. The folder the port used last is where both dialogs open. */
static int CALLBACK pick_init(HWND h, UINT msg, LPARAM lp, LPARAM data) {
    (void)lp;
    if (msg == BFFM_INITIALIZED && data) SendMessageA(h, BFFM_SETSELECTIONA, TRUE, data);
    return 0;
}

int win_pick_path(const char *title, const char *start, char *out, size_t n) {
    char shown[MAX_PATH] = "";
    BROWSEINFOA bi;
    memset(&bi, 0, sizeof bi);
    bi.lpszTitle = title;
    bi.pszDisplayName = shown;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    bi.lpfn = pick_init;
    bi.lParam = start && *start ? (LPARAM)start : 0;
    int ok = 0;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    LPITEMIDLIST id = SHBrowseForFolderA(&bi);
    if (id) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(id, path) && path[0]) {
            snprintf(out, n, "%s", path);
            ok = 1;
        }
        CoTaskMemFree(id);
    }
    CoUninitialize();
    return ok;
}

int win_pick_file(const char *title, const char *start, const char *filter, char *out, size_t n) {
    OPENFILENAMEA ofn;
    if (!n) return 0;
    out[0] = 0;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrTitle = title;
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)n;
    ofn.lpstrFilter = filter ? filter : "All files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = start && *start ? start : NULL;
    /* NOCHANGEDIR: sh3.ini, data/ and mods/ are relative, so the dialog must not move the port's working folder. */
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameA(&ofn) ? 1 : 0;
}

void win_message(const char *title, const char *text) { MessageBoxA(NULL, text, title, MB_OK | MB_ICONINFORMATION); }
