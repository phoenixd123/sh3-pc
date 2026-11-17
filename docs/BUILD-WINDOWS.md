# Building and running on Windows

The port is the same source as the Switch and Linux builds: the recompiled game code is platform-independent C,
and the Windows layer (`src/runtime/host.c`, `src/runtime/win_posix.c`, `src/win32`) provides threads, the guest
address window, crash logging, locks, files and the SDL window.

## What you need

- **Windows 10 or 11, x86-64, a GPU with OpenGL 4.5** (the renderer is OpenGL 4.5 core; the game runs on D3D8,
  which is translated to GLSL at runtime).
- **Your own copy of Silent Hill 3 for PC**, `sh3.exe` of 3,338,240 bytes, SHA-1
  `aa2aca6dba19bf510c9c2e92eefdc15e98266581`, and its `data/` folder (about 5 GB).
- **MSYS2** with the mingw-w64 toolchain (https://www.msys2.org), in an **MSYS2 MINGW64** shell:

  ```sh
  pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
      mingw-w64-x86_64-SDL2 python
  ```

  `mingw-w64-x86_64-SDL2` is SDL2 with its mingw development files; CMake finds it through `SDL2Config.cmake`.
  Any other mingw-w64 distribution works if it has SDL2's development package.

## Generate the recompiled game code once

The recompiler reads your `sh3.exe` and writes `src/recomp/gen` (gitignored, about 5 minutes). It runs on any
platform with Python 3:

```sh
python -m venv .venv
.venv/Scripts/pip install -r requirements.txt
# put your install where the scripts expect it, or pass the path to patch_exe.py/pe2xbe.py yourself
mkdir game && cp -r "/c/Program Files/Konami/Silent Hill 3/." game/
tools/regen.sh
```

`tools/regen.sh` is a POSIX shell script; run it from Git Bash or MSYS2. It needs `capstone` and `unicorn` from
`requirements.txt`.

## Build

```sh
cmake -S . -B build/windows -G Ninja
cmake --build build/windows
```

That produces `build/windows/sh3.exe`. Run it with the game folder as the argument, or from inside it:

```sh
cd game
../build/windows/sh3.exe .
```

`sh3.exe` and its `data/` folder must be in the working directory. Saves, settings, the shader cache
(`sh3-progs.bin`) and `sh3.log` are written there too.

## Windows specifics

- **The console stays.** The build defines `SDL_MAIN_HANDLED` and calls `SDL_SetMainReady()` in `rt_window_open`
  instead of linking SDL's `WinMain`, so `main()` is ours and `rt_log` keeps reaching the console. On the Switch
  logs go to `sh3.log`; on Windows both.
- **`-Wno-macro-redefined` is expected.** `rt.h` defines `WINAPI(cname, ename, n)` as its bridge macro; the
  Windows SDK defines `WINAPI` as `__stdcall`. Include order decides which one is visible in a file:
  `src/d3d8/d3d8.h` includes the GL headers before `rt.h` for exactly this reason (`windows.h` maps `APIENTRY`
  to `WINAPI`), and `host.c` writes `__stdcall` rather than `WINAPI` in its thread entry.
- **`windows.h` and the Win32 bridge cannot share a translation unit.** `src/win32/kernel32.c` defines C
  functions under the SDK's own names (`CreateFileA`, `CreateThread`, `ReadFile`, ...), so the SDK prototypes
  would clash. The POSIX pieces that file needs are declared in `src/runtime/win_posix.h` and implemented in
  `src/runtime/win_posix.c`, which is the only file that includes `windows.h` for them.
- **Bridges register themselves.** ELF builds put them in a `game_bridges` section and walk
  `__start_/__stop_`; PE-COFF has no such symbols, so on Windows each bridge adds itself to a list from a CRT
  initialiser (`BRIDGE_REG` in `rt.h`).
- **The launcher's images.** `launcher.c` `.incbin`s the BMPs from `assets/`; the Windows form emits COFF
  sections (`.rdata` then `.text`) because there is no `.previous` directive.
- **`cores=`** is a Switch setting (thread affinity); the Windows layer accepts it and ignores it.
- **`SH3_DUMP_AFTER`** (the hang watchdog that signals threads) is POSIX only: Windows has no equivalent wired
  up yet.

## What has been verified, and what has not

Verified on this machine with `zig cc` (clang 17 + mingw-w64 headers) and SDL2 2.30.9, using a stub in place of
the recompiled game code because **no game files were available**:

- Every source file compiles, and the executable links against SDL2 and the Win32 import libraries.
- It starts, reserves the 4 GB guest arena, creates the game thread, scans `mods/`, parses each `mod.ini`
  (name, `enabled`, `priority`) and honours `mods/load_order.txt`.
- The launcher runs with a real OpenGL context: the warning screen and launcher are drawn (which exercises the
  COFF `.incbin` assets, the SDL window and the GL loader), and it stays interactive.
- Fatal errors go to the console and to `sh3-crash.log` through the vectored exception handler.

**Not verified:** anything past the launcher, because that needs `sh3.exe` and `data/`. In particular the
recompiled game code, the D3D8→GL translation under real load, HD texture replacement on a real texture, audio,
and the FMV/black-screen known issues. Build the game code with `tools/regen.sh` and your own copy to test those.
