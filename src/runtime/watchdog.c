/* The launcher's guard around the copy's own sh3.exe. That executable runs as a separate process, so nothing in
 * this port can answer for it: its Options screen still tests every display mode Windows reports, and while it
 * hangs there a fullscreen window leaves no way to Task Manager. The launcher stays up for as long as the game
 * runs, and Ctrl+Alt+End - a registered hotkey, which Windows delivers even when a hung window has the focus -
 * ends it. SH3_WATCH_KILL_MS ends the game after that many milliseconds instead, which is how build/watchdog_test.c
 * drives the kill with a stub child process and no keyboard. */
#ifndef _WIN32

int rt_watch_child(void *process) {
    (void)process;
    return 0;
}

#else

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void rt_log(const char *fmt, ...); /* rt_core.c */

/* Waits for the process until it ends or the key ends it; 1 when it had to be ended. */
int rt_watch_child(void *process) {
    HANDLE game = (HANDLE)process;
    int killed = 0;
    if (!RegisterHotKey(NULL, 1, MOD_CONTROL | MOD_ALT, VK_END))
        rt_log("watchdog: Ctrl+Alt+End belongs to another program, so the game runs without it");
    else
        rt_log("watchdog: Ctrl+Alt+End ends the game if it stops answering");
    const char *ms = getenv("SH3_WATCH_KILL_MS");
    uint64_t kill_at = ms ? GetTickCount64() + (uint64_t)atoll(ms) : 0;
    MSG msg;
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &game, FALSE, 100, QS_ALLINPUT);
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
            if (msg.message == WM_HOTKEY && msg.wParam == 1) {
                rt_log("watchdog: Ctrl+Alt+End, ending the game");
                killed = 1;
                goto stop;
            }
        if (r == WAIT_OBJECT_0) break; /* the game ended by itself */
        if (kill_at && GetTickCount64() > kill_at) {
            rt_log("watchdog: SH3_WATCH_KILL_MS, ending the game");
            killed = 1;
            break;
        }
    }
stop:
    if (killed) TerminateProcess(game, 0);
    UnregisterHotKey(NULL, 1);
    return killed;
}

#endif
