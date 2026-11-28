#!/usr/bin/env bash
# Every check that needs no game files: the host-side tests, then the launcher's Play path against a stub sh3.exe.
#
#   bash tests/run.sh
#
# ZIG points at the cross compiler (default: the one build/wcheck.sh uses).
set -u
cd "$(dirname "$0")/.." || exit 1
ZIG=${ZIG:-build/.tools/zig-windows-x86_64-0.13.0/zig.exe}
OUT=build/tests
fail=0
mkdir -p "$OUT"

say() { printf '\n== %s\n' "$1"; }
bad() { printf 'FAILED: %s\n' "$1"; fail=1; }

say "the launcher (build/wcheck.sh)"
if bash build/wcheck.sh; then
    rm -f src/recomp/gen/stub.c
else
    bad "build/wcheck.sh"
fi

say "modes: the list the game is offered"
if "$ZIG" cc -target x86_64-windows-gnu -O1 -I src/runtime tests/modes_test.c -o "$OUT/modes_test.exe" &&
    (cd "$OUT" && ./modes_test.exe); then :; else bad "modes_test"; fi

say "ini: rt_ini_set on a real sh3.ini"
if "$ZIG" cc -target x86_64-windows-gnu -O1 tests/ini_test.c src/runtime/rt_ini.c -o "$OUT/ini_test.exe" &&
    (cd "$OUT" && ./ini_test.exe); then :; else bad "ini_test"; fi

say "watchdog: SH3_WATCH_KILL_MS, Ctrl+Alt+End, a game that ends by itself"
if "$ZIG" cc -target x86_64-windows-gnu -O1 tests/watchdog_test.c src/runtime/watchdog.c -o "$OUT/watchdog_test.exe" &&
    (cd "$OUT" && ./watchdog_test.exe); then :; else bad "watchdog_test"; fi

say "play: the menu's Play row end to end, against a stub sh3.exe"
if bash tests/play_e2e.sh; then :; else bad "play_e2e"; fi

printf '\n'
if [ "$fail" = 0 ]; then echo "tests: all passed"; else echo "tests: FAILURES"; fi
exit "$fail"
