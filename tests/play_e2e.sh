#!/usr/bin/env bash
# The launcher's Play row end to end, with no game files: tests/stub_game.c stands in for the copy's own sh3.exe.
#
#   bash tests/play_e2e.sh          (bash tests/run.sh does this after building the launcher)
#
# Scripted keys drive the menu (9 s: the splash, 11 s: Play). The two prompts the Play path raises - "write the
# registry entry?" and "start the game's own sh3.exe?" - are answered by SH3_CONFIRM=yes, and SH3_REGISTRY_READY
# stands in for the machine registry entry that writing would need an administrator prompt for.
#
# SH3_WATCH_KILL_MS ends the stub two seconds in, so what the run proves is the whole chain: Play -> confirm ->
# CreateProcess of sh3.exe -> hidden launcher window -> watcher registering Ctrl+Alt+End -> kill -> exit 0. The
# stub writes stub-survived.txt only if it reaches six seconds, so a kill that did not land is caught.
set -u
cd "$(dirname "$0")/.." || exit 1
ZIG=${ZIG:-build/.tools/zig-windows-x86_64-0.13.0/zig.exe}
OUT=build/tests/play
fail=0
bad() { echo "FAILED: $1"; fail=1; }

[ -f build/wcheck/sh3.exe ] || { echo "build/wcheck/sh3.exe is missing: run bash build/wcheck.sh first"; exit 1; }
[ -f dist/SDL2.dll ] || { echo "dist/SDL2.dll is missing: the launcher opens its window with it"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/game/data" "$OUT/launcher/savedata"
# No first-launch credits screen in the middle of the script: the launcher looks for the marker in its working
# folder, which is the game folder once the Play path can run, and in its own savedata/ before that.
touch "$OUT/game/port-credits" "$OUT/launcher/savedata/port-credits"
"$ZIG" cc -target x86_64-windows-gnu -O1 tests/stub_game.c -o "$OUT/game/sh3.exe" || exit 1
cp build/wcheck/sh3.exe "$OUT/launcher/sh3-pc-launcher.exe" || exit 1
cp dist/SDL2.dll "$OUT/launcher/" || exit 1
# autoupdate=0: no release check and no update screen in the middle of a scripted run
printf 'resolution=1280x720\nfullscreen=0\nmute=1\nautoupdate=0\n' > "$OUT/launcher/sh3.ini"
# backslashes, the way the launcher writes it, so it does not rewrite the file at startup
# (autoupdate=0: no release check and no update screen in the middle of a scripted run)
game=$(cd "$OUT/game" && { pwd -W 2>/dev/null || pwd; } | sed 's#/#\\#g')
printf '# written by the launcher: the folder holding sh3.exe and data/\ngame=%s\n' "$game" > "$OUT/launcher/sh3-pc.ini"

( cd "$OUT/launcher" && SH3_LAUNCHER=1 SH3_CONFIRM=yes SH3_REGISTRY_READY=1 SH3_WATCH_KILL_MS=2000 \
    SH3_KEYS="9:Return,12:Return" timeout -s KILL 40 ./sh3-pc-launcher.exe > run.log 2>&1 )
code=$?
log="$OUT/launcher/run.log"

[ "$code" = 0 ] || bad "the launcher exited $code; expected 0 once the game it started had been ended"
grep -q "confirm: .* -> yes" "$log" || bad "SH3_CONFIRM did not answer the Play prompts"
grep -q "launcher: started the copy's own" "$log" || bad "Play did not start the stub sh3.exe"
grep -q "watchdog: Ctrl+Alt+End ends the game if it stops answering" "$log" || bad "the watcher did not register Ctrl+Alt+End"
grep -q "watchdog: SH3_WATCH_KILL_MS, ending the game" "$log" || bad "the watcher did not end the stub"
[ -f "$OUT/game/stub-survived.txt" ] && bad "the stub was still running after the kill"

if [ "$fail" = 0 ]; then echo "play: all checks passed (menu -> sh3.exe -> kill -> exit 0)"; fi
exit "$fail"
