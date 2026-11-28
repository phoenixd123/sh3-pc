/* tests/stub_game.c: stands in for the copy's own sh3.exe in tests/play_e2e.sh. It runs until it is ended, and it
 * only writes stub-survived.txt if it is still alive after six seconds - so the test can prove the kill from the
 * launcher's side alone, without looking for processes by name.
 *
 *   zig cc -target x86_64-windows-gnu tests/stub_game.c -o build/tests/play/game/sh3.exe
 */
#include <stdio.h>
#include <windows.h>

static void hang_then_survive(void) {
    Sleep(6000);
    FILE *f = fopen("stub-survived.txt", "w");
    if (f) fclose(f);
}

/* Both entries, so the stub builds however the test links it (console or the windows subsystem). */
int main(void) {
    hang_then_survive();
    return 0;
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE prev, LPSTR cmd, int show) {
    (void)h;
    (void)prev;
    (void)cmd;
    (void)show;
    hang_then_survive();
    return 0;
}
