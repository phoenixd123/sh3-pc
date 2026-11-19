# Changelog

## 1.1.0 � a launcher that finds the game, a real credit roll, F12 screenshots

- **The port asks for the game.** Started without an argument it opens the Windows folder picker, checks that
  `sh3.exe` and `data/` are in the folder and saves it in `sh3-pc.ini` beside its own executable; later launches go
  straight to the launcher. A path argument, `SH3_DIR` and "drop the executable in the game folder" still work, a
  folder that is not the game is refused with a message, and an `sh3.exe` of the wrong size is logged instead of
  crashing silently.
- **Options > Game folder, and a disc.** The launcher's Options screen shows the folder the game runs from. Its
  **Game folder** row picks another one with the Windows folder dialog, and a **Game disc or file** row opens the
  real open-file dialog for the game's own `sh3.exe` or a disc image (`.iso`, `.bin`, `.cue`). Whichever row is
  used, the path is resolved exactly as on the command line, saved in `sh3-pc.ini`, and `sh3.ini`, `mods/` and the
  menu sounds are reloaded from it without a restart. A path that is not the game is refused with the same message
  as at startup, and nothing changes.
- **Any path works, and a disc is named.** The path given to the launcher can be the game's folder, its own
  `sh3.exe`, or a disc. A disc is read as ISO 9660 (2048-byte `.iso` or a 2352-byte `.bin` rip, with or without a
  `.cue`): a `SYSTEM.CNF` boot record makes it the PlayStation 2 game, so the message says so and names the program
  it starts, and `sh3.exe`/`setup.exe` at the root makes it a PC installation disc. An ELF is read as far as its
  MIPS header. Nothing is guessed from the file extension, and nothing pretends the PS2 release can run here.
- **The credits are a credit roll**, not a card: letterspaced capitals, the names under the labels, scrolling out of
  and back into black in about 26 seconds, skippable with A.
- **F12** writes the presented frame to `screenshots/sh3-<date>-<time>.png`, at the game's resolution.
- Docs: what the 2003 release shipped on, what its copy protection did, why the recompiler needs the unprotected
  executable, and that the PS2 release is not a substitute.

## 1.0.0 — first full release

The port is feature-complete for what it set out to be: a Windows build of the recompiled game with mods, HD
texture packs and one-command settings. 1.0.0 is 0.1.0 with the naming settled and the documentation finished.

- **One name.** The port is credited as `phoenixd123` in the launcher, the splash, the README and the repo URLs;
  the Instagram handle stays `@phoenixdobsonyc`.
- **Screenshots** of the launcher and the credits screen in the README, taken from a build of this tree.
- **`LICENSE`** is MIT, with the third-party licences spelled out in [CREDITS.md](CREDITS.md).

What 1.0 does *not* mean: that the game has been played through. Nothing past the first areas has been tested in
this fork, and no build has been run against a real copy of the game on the machine it was written on. The known
issues in [BUGS.md](BUGS.md) are unchanged and each one says what a fix needs.

## 0.1.0 — first release

The first public release of this project: sh3-nx, which was a Nintendo Switch port, becomes a PC port with mods.
Everything below was written for this release; the Switch history it came from is in the README and in
[CREDITS.md](CREDITS.md).

### Added

- **Windows (x86-64).** The recompiled code was already portable; the platform layer was not. `src/runtime/host.c`
  gained a Win32 thread backend with pause/resume, a 4 GB guest arena and vectored exception handling,
  `src/runtime/win_posix.c` supplies the locks, clocks and folder listing the runtime expects, and three PE-COFF
  assumptions were fixed (bridge tables registered by a CRT constructor instead of `__start_/__stop_`, the
  launcher's `.incbin` assets emitted as COFF sections, and the `WINAPI`/`APIENTRY` collision with the SDK).
  See [docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md).
- **Mod support.** `mods/<name>/files/...` replaces any file the game opens for reading; `mods/load_order.txt` and
  `mod.ini` (`name=`, `enabled=`, `priority=`) set the order. See [MODDING.md](MODDING.md).
- **HD texture packs.** `texdump=1` writes every texture the game uploads to `texdump/<key>.png`;
  `tools/make_hd_pack.py` upscales them into a pack. Textures are keyed by a hash of the pixels the game uploads,
  because the game reads `.tex` archives by offset and no filename reaches the renderer.
- **Anisotropic filtering** (`aniso=` in `sh3.ini`, default 8) and mip filtering for stages that asked for none,
  which is what keeps an HD texture sharp at grazing angles.
- **A credits screen** on first launch, and from the launcher menu, naming the port author, Konami and Team Silent,
  and the two upstream projects the running code comes from.
- `log=1` now writes `sh3.log` next to the executable on every platform, not just the Switch.

### Fixed

- The launcher's font dropped its last glyph: the atlas is 96 cells (ASCII 32-127) and the bounds stopped at 95,
  so `_` never printed.
- `mod.ini` `priority=` never applied: an ini parser reused one buffer for the path and the value, and every mod
  was given a unique sort key, so the priority tiebreak could not run.
- `GetFileAttributesA` ignored mods, so a size probe saw the game's copy of a file that a mod had replaced.

### Changed

- The project is **sh3-pc**: CMake project name, launcher title, NACP author, the Switch output (`sh3-pc.nro`) and
  its folder (`/switch/sh3-pc/`). The generated code and the game's behaviour are unchanged.

### Not in this release

- **No binary download.** The recompiler turns *your* `sh3.exe` into C, so a build is specific to the copy of the
  game you own; shipping one would ship the game's code. Build it with [docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md).
- The FMVs are still skipped, the black screen after the Office Building bathroom and the stretched logos and
  credits on a wide screen are still there: they need the game running to diagnose, and this machine never had it.
  [BUGS.md](BUGS.md) has the root-cause analysis and what each fix needs.
- Nothing past the first areas of the game has been played in this fork.
