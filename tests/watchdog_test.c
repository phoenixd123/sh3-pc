/* tests/watchdog_test.c: the kill the launcher holds over the copy's own sh3.exe (src/runtime/watchdog.c), with
 * this same binary as the stub child. No game, no keyboard. Every test: bash tests/run.sh.
 *
 * The three ways out: SH3_WATCH_KILL_MS after a game that never ends, the WM_HOTKEY the registered Ctrl+Alt+End
 * delivers, and a game that ends by itself.
 */
#include <stdarg.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

int rt_watch_child(void *process);

void rt_log(const char *fmt, ...) { /* rt_core.c's log, here so watchdog.c links */
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int fails;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static HANDLE spawn(const char *mode) {
    char exe[600], cmd[700];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    GetModuleFileNameA(NULL, exe, sizeof exe);
    snprintf(cmd, sizeof cmd, "\"%s\" %s", exe, mode);
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("FAIL: could not start the stub child\n");
        fails++;
        return NULL;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static int alive(HANDLE p) {
    DWORD code = 0;
    return GetExitCodeProcess(p, &code) && code == STILL_ACTIVE;
}

static void release(HANDLE p) {
    WaitForSingleObject(p, 3000);
    CloseHandle(p);
}

int main(int argc, char **argv) {
    if (argc > 1) {  /* the stub child */
        if (!strcmp(argv[1], "--exit")) return 0;
        for (;;) Sleep(1000);
    }

    MSG msg;
    PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE);  /* a message queue, for the posted hotkey below */

    /* 1. A game that never ends is ended after SH3_WATCH_KILL_MS. */
    _putenv("SH3_WATCH_KILL_MS=800");
    HANDLE p = spawn("--hang");
    if (p) {
        ULONGLONG t0 = GetTickCount64();
        int killed = rt_watch_child(p);
        ULONGLONG took = GetTickCount64() - t0;
        CHECK(killed == 1, "a game that never ends was not ended after SH3_WATCH_KILL_MS");
        CHECK(!alive(p), "the game is still running after SH3_WATCH_KILL_MS");
        CHECK(took >= 500 && took < 5000, "the kill took an unexpected time");
        release(p);
    }
    _putenv("SH3_WATCH_KILL_MS=");

    /* 2. Ctrl+Alt+End: the exactly-shaped WM_HOTKEY Windows posts for the registered key. */
    p = spawn("--hang");
    if (p) {
        PostThreadMessageA(GetCurrentThreadId(), WM_HOTKEY, 1, 0);
        int killed = rt_watch_child(p);
        CHECK(killed == 1, "the hotkey did not end the game");
        CHECK(!alive(p), "the game is still running after the hotkey");
        release(p);
    }

    /* 3. A game that ends by itself is left alone. */
    p = spawn("--exit");
    if (p) {
        int killed = rt_watch_child(p);
        CHECK(killed == 0, "a game that ended by itself was reported as killed");
        release(p);
    }

    printf(fails ? "%d check(s) failed\n" : "watchdog: all checks passed\n", fails);
    return fails != 0;
}
