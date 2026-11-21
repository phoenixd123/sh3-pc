# Changelog

## 1.10.1 - the camera's distances, in game

- **Alt + arrows / Page Up / Page Down** while `camera=` is on move the eye a step at a time (Shift for a
  bigger step) and log `cam_forward=`, `cam_right=` and `cam_up=` in the form to paste into `sh3.ini`. F2
  starts again from the presets.
- The keys are kept from the guest while Alt is held in a camera mode, as the freecam's own keys are.
- Tested: the launcher's **Camera** row walked with scripted keys now that the "Press A" screen is passed
  (`SH3_LAUNCHER=1`, `SH3_KEYS`): `filter=0` in `sh3.ini` became `filter=0` + `camera=ots`, and a second run
  from `camera=ots` wrote `camera=first`, and a third from `camera=first` wrote `camera=off`: the row cycles and
  the file is rewritten in place. (What held the keys up was not a Windows dialog but the launcher's own "Press A"
  warning screen, which swallows input until it is passed.) The nudge keys
  were not tried in game (no sh3.exe here); the shift math they feed is the same tested in 1.10.0.

## 1.10.0 - the camera: over the shoulder and first person

- **`camera=` and F2.** F2 cycles off, over the shoulder and first person in game; `camera=ots` or
  `camera=first` starts in one, and the launcher's Options has a **Camera** row for it. The game's view is
  shifted along its own axes as it is handed to the shaders, so the game's camera, collision, aiming and
  culling are untouched: it is a camera, not a game mode. `cam_forward=`, `cam_right=` and `cam_up=` set the
  distance instead of the preset's.
- New files `src/d3d8/camera.c|h`; `freecam_view_shift` in `src/d3d8/freecam_math.c` moves the eye of a view
  matrix.
- Tested: a shift moves the eye by exactly the distances asked for along the view's own axes and does not
  turn it, a zero shift changes nothing and a degenerate matrix is left alone; the launcher builds and links
  with the new Options row, whose values are the words the parser above reads. Not tested in game (no sh3.exe
  here) and the row was not walked on screen: the preset distances are starting points, and the camera is
  visual only.

## 1.9.0 - the Mods screen and Nexus Mods

- **Mods screen** (menu entry, or **M** anywhere in the launcher): installed mods on the left, the Nexus Mods list
  for Silent Hill 3 on the right. A downloads the selected mod's main file and installs it into `mods/` in the
  background; N opens the mod's page; R refreshes; B goes back.
- **Nexus API, not scraping**: the list and the download come from `api.nexusmods.com` with the player's own
  `nexus_key=` from sh3.ini. Automatic downloads are Premium-only in the API, so a free key is told that plainly
  and pointed at the page instead of being worked around.
- New files `src/d3d8/nexus.c|h` (WinHTTP + a small JSON reader), linked against `winhttp`.
- Tested against a local stand-in for the API: the list shows, a Premium account downloads and installs a mod
  into `mods/` with its `files/` layout intact, and a free account is refused with the Premium message and no
  files written. Not tested against the live API (no account here).

## 1.8.0 - Steam Deck (Proton)

- **Import without `tar.exe`.** A Proton prefix has no `tar.exe`, so Import (and mod zips) failed on the Deck.
  The archive is now unpacked by the launcher itself with miniz: entries stream to disk, `/` becomes `\`, and
  `..` paths are skipped. Windows still uses `tar.exe` when it has one; `SH3_NO_TAR=1` forces the built-in reader.
- **Install entry in the Proton prefix.** `wine_get_version` in `ntdll` is the test; under it the launcher writes
  `HKLM\SOFTWARE\WOW6432Node\KONAMI\SILENT HILL 3` and the 32-bit view directly instead of asking for
  administrator rights, which Proton does not have. Windows behaviour is unchanged.
- Controller navigation (A/B, D-pad, left stick, Start) was already in the launcher; the README now documents the
  Deck setup with it, and how to add the launcher to Steam with Proton.
- Tested on Windows: the built-in reader unpacks a 369 KB zip with nested folders to byte-identical files
  (300 KB deflate payload), finds `sh3.exe` with `data/`, and saves the folder; the `tar.exe` path still works.
  Not tested on a Deck or under Proton.

## 1.7.1 - the copy's registry entry

- **"The game is not installed properly".** That box is the game's own: it asks Windows where it was installed
  and stops when the answer is missing, which is what a copied install (a Full-Rip) has. Play now writes the
  entry for the folder it is about to start: HKCU directly (what the PC Fix reads) and, with the permission
  prompt, HKLM for the unpatched game. Declining leaves everything as it was.
- The keys are the installer's: `movie`, `sound`, `data`, `installdir` as `<folder>\`, `save` as
  `<folder>\savedata\`.

## 1.7.0 - switches

- **Widescreen is a toggle.** `widescreen=0` in `sh3.ini`, or **Options > Widescreen**, restores the original
  4:3 camera, the centered menus and the cutscene borders; `1` (the default) is ThirteenAG's fix.
- **Filters**, for the look of it: `filter=1` draws the frame through a CRT pass (scanlines, a warm tube, darker
  corners) and `filter=2` through a VHS pass (colour bleed, grain, a slow roll). **Options > Filter** sets it; the
  default is off, and a shader that fails to build leaves the plain blit alone.
- **Steam Deck notes** in the README: the Linux build is the one to use, and the release has no game code.

## 1.6.4 - Check this copy, and drops

- **Options > Check this copy** names what you picked and what to do with it: an installed copy (it offers to use
  it), a PlayStation 2 disc (it offers PCSX2), the PC installation disc (install it first), the Full-Rip `.zip`
  (it offers to import it), a mod archive, or something that is not Silent Hill 3.
- **The PS2 message opens pcsx2.net when PCSX2 is not installed**, instead of only naming the site.
- **Drag onto the launcher window**: a game folder, the game's `sh3.exe`, a disc image, the Full-Rip `.zip` or a
  mod `.zip` is recognised and used on the spot. Dropping onto the exe still works too.

## 1.6.3 - the launcher opens without a game

- **No more blank console.** The release is linked as a Windows program, so double-clicking it no longer opens a
  command prompt behind the launcher.
- **The launcher shows itself even with no game installed.** It used to ask for a folder first and exit when it
  could not find `sh3.exe`; now it opens the menu, and **Options** holds Import a .zip download, Game folder,
  Game disc or file and Mods.
- Play with no game folder says so and points at Options.

## 1.6.2 - cue sheets

- **A .cue with an absolute FILE path now works.** It used to have the cue's folder glued in front of the path,
  so the image was never found; the absolute path is used as it is.
- **A .cue whose .bin is missing says so**, naming the cue and the FILE line to check, instead of "that file is
  not part of Silent Hill 3". Both files have to be in the same folder.
- The Options row is now **Import a .zip download**: a disc image (.bin/.cue/.iso) goes through **Game disc or
  file**, where it is read and either handed to PCSX2 (PS2) or named as an installation disc (PC).

## 1.6.1 - Play hands over to the copy's own game

- **The launcher-only build can start the imported download.** Play used to say only that the recompiled code
  was missing. It now asks whether to start the copy's own `sh3.exe` in the game folder (the NoCD executable the
  Full-Rip ships), starts it there and exits; nothing is copied or replaced. A build made from your own copy
  still runs the port instead.
- **No accidental self-start.** If the "game" executable is this launcher, or a copy of it, Play says so
  instead of starting a loop.

## 1.6.0 - the download, imported

- **Options > Import the game download** takes the Full-Rip zip (a pre-installed copy with `sh3.exe` and
  `data/` inside), unpacks it beside itself and makes that folder the game. A 2.6 GB archive is unpacked by
  Windows' own `tar.exe` in its own console, so nothing is held in memory and the file list shows progress; a
  folder that is already unpacked is reused. Dropping the zip on the executable does the same.
- **A zip is still a mod** when it has no `sh3.exe` and `data/`: the same drop installs it into `mods/`, as
  before. The decision is made from the archive's own file list, without unpacking it.
- **Play in the launcher-only build says why it cannot start.** It used to drop into the crash handler; now it
  explains that the recompiled game code is what a build of your own copy adds.

## 1.5.0 - the PS2 disc, handed to PCSX2

- **A PS2 disc is no longer a dead end.** Picking one - in Options, by dropping it on the executable, or on the
  command line - still says that this port recompiles the 2003 PC release and cannot run MIPS code, but when
  PCSX2 is installed the same dialog offers to open the disc in it. A `.cue` hands over the `.bin` it names.
- **PCSX2 is found where it installs** (`%LOCALAPPDATA%\Programs\PCSX2`, `%ProgramFiles%\PCSX2`, then PATH) and
  started with the image as its argument. Without PCSX2 the dialog says where to get it, and nothing else changes.
- **The note under the Options row tells the truth.** It says "Opened in PCSX2" only when the emulator actually
  started, and otherwise says either that the disc was not opened or that PCSX2 is not installed.

## 1.4.0 - freecam

- **F1 detaches the camera and holds the game's clock.** The game keeps drawing, so the view moves smoothly
  while the world stands still: WASD and Q/E move, the mouse looks, Shift and Ctrl change the speed, and the
  wheel scales the field of view without touching near or far. F1 returns to the game where it stopped.
- **Frame stepping.** `,` and `.` advance the held clock one frame at a time, `[` and `]` ten, which is how a
  model is posed for a shot. Named animation keyframes are not exposed: their table is in model data the
  render layer does not read.
- **A freeze that cannot hang the game.** If nothing presents for 400 ms the port releases one frame and logs
  it, so a build that waits on its own frame limiter slows down instead of deadlocking.

## 1.3.0 - PS2 costume tooling, all local

- **`tools/ps2_outfits.py`** builds a mod from converted PS2 costume models: it appends them to copies of the
  player's own `data/chrpl.arc` and `data/arc.arc`, writes `mods/ps2-outfit-models/`, and can emit the `.zip`
  that **Options > Install a mod** takes. No models, paths or passwords are bundled.
- **`patches/*.patch`** applies build-time byte edits to `sh3.exe` before it is lifted (the outfit tables, the
  password lookup), each checked against the original bytes; `tools/patch_image.py` applies or checks one file
  on its own. The addresses and table shapes are in MODDING.md.

## 1.2.0 — mods, installed from the launcher

- **Install a mod, from the game.** Options has an **Install a mod** row that asks for a `.zip`, unpacks it into
  `mods/<archive name>/` and loads it straight away; dropping the archive on the executable does the same and starts
  the game. The archive's layout is worked out from what is inside: a `files/` folder or `mod.ini` as it stands, a
  single wrapper folder stripped, and loose game paths (`data/...`) wrapped in `files/`.
- **A ZIP reader in the port**, so none of this needs an unzip tool, a shell or a network: stored and deflated
  entries, and a refusal with the reason for a file that is not an archive, an archive with nothing in it, a path
  that would land outside the game folder, and a mod name that is already taken.
- **The credits roll names only this port's author.** The recompilation it stands on is credited in
  [CREDITS.md](CREDITS.md), which is where the licences require it.

## 1.1.1 — the launcher as a download, and a picker that takes files

- **Download the launcher.** The Windows launcher is attached to this release as a zip (`sh3-pc-launcher.exe`,
  `SDL2.dll`, `README.txt`), so it runs without a build. It is the launcher only: the recompiled game code is
  generated from the player's own `sh3.exe` and is not distributed here, and Play stops at the crash handler until
  that build exists ([docs/BUILD-WINDOWS.md](docs/BUILD-WINDOWS.md)).
- **Options picks a disc or a single file for real.** The previous try asked the shell's folder browser to show
  files (`BIF_BROWSEINCLUDEFILES`), which lists them but gives no dependable way to return one. **Game folder** is
  the plain folder browser again, and a new **Game disc or file** row opens the standard Windows open-file dialog
  for `sh3.exe` or a `.iso`/`.bin`/`.cue`/`.img` disc. Both rows resolve the path the same way the command line
  does, save it in `sh3-pc.ini` and reload `sh3.ini`, `mods/` and the menu sounds; a path that is not the game is
  refused with the same message and nothing changes.

## 1.1.0 — a launcher that finds the game, a real credit roll, F12 screenshots

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

## 1.0.0 â€” first full release

The port is feature-complete for what it set out to be: a Windows build of the recompiled game with mods, HD
texture packs and one-command settings. 1.0.0 is 0.1.0 with the naming settled and the documentation finished.

- **One name.** The port is credited as `phoenixd123` in the launcher, the splash, the README and the repo URLs;
  the Instagram handle stays `@phoenixdobsonyc`.
- **Screenshots** of the launcher and the credits screen in the README, taken from a build of this tree.
- **`LICENSE`** is MIT, with the third-party licences spelled out in [CREDITS.md](CREDITS.md).

What 1.0 does *not* mean: that the game has been played through. Nothing past the first areas has been tested in
this fork, and no build has been run against a real copy of the game on the machine it was written on. The known
issues in [BUGS.md](BUGS.md) are unchanged and each one says what a fix needs.

## 0.1.0 â€” first release

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
