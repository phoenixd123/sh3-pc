# Bugs

The state of every issue known against this port. "Fixed here" means the defect was found in the code and
corrected; "open" means it needs the game running to diagnose, and what that takes is written down.

## Fixed in this fork

| Was | What was wrong | Fix |
| --- | --- | --- |
| No Windows build | Only Switch and Linux targets existed. The recompiled code was already portable; the platform layer, four PE-COFF assumptions and a header collision were not. | Win32 threads/arena/exception handler (`src/runtime/host.c`), POSIX shim (`src/runtime/win_posix.[ch]`), bridges registered by constructor instead of `__start_/__stop_`, `.incbin` assets emitted as COFF sections, `WINAPI`/`APIENTRY` include order. `docs/BUILD-WINDOWS.md`. |
| `log=1` did nothing on PC | It was compiled only under `__SWITCH__`, so the setting the README documents produced no `sh3.log` outside the console. | `log=1` now also writes `sh3.log` on every platform. |
| `_` never printed in the launcher | The glyph bounds were `c >= 95` over a 96-cell atlas (ASCII 32-127), so the last cell, `_`, was skipped, and a DEL byte read past the row. | `c >= 96`. |
| The port credited nowhere in the game | Attribution lived only in the README, so the running game claimed nothing about who ported it. | A credits screen on first launch and from the launcher menu, with the port author first and the projects it stands on below. |
| `mod.ini` `priority=` ignored | Two causes: `ini_get` used one buffer for both the file path and the value it read, so every key after the first lost its path; and every mod was given a unique sort key, so the priority tiebreak never ran even once that was fixed. | Separate buffers, and unlisted mods share one order (`INT_MAX`) so `priority=` decides between them. |
| Mod files invisible to attribute checks | A mod replacing a file was opened from the mod but `GetFileAttributesA` still reported the game's copy, so a size probe saw the wrong file. | The attribute bridge consults the mod loader too. |
| The Options screen sat on a dimmed frame | Two host-layer answers were missing: `EnumDisplaySettingsA` reported one mode where the screen builds its resolution row from that list, and `ChangeDisplaySettingsA` returned success without the `WM_DISPLAYCHANGE` a real mode change sends, which the screen waits for. The retail game freezes on the same list for the opposite reason: it tests all 1100+ modes a modern driver reports. | Sixteen 4:3 modes with the port's resolution first (`src/runtime/display_modes.h`), and the change is answered with the message. |
| Picking one of the sixteen resolutions did nothing | The list was right but no pick was taken: `ChangeDisplaySettingsA` answered success and kept the port's size, and the device reset the game runs after a mode change was pinned back to `rt_res_*`, so every mode in the row was a no-op. | `rt_set_resolution` (`user32.c`) makes the pick the port's mode, the device reset is built at that size (`src/game/sh3.c`), a windowed window is resized, and `resolution=` is rewritten (`src/runtime/rt_ini.c`). Only offered modes are taken, and `resolution=` now reads up to 4096. |
| A hung screen could not be closed | Only the game's own Quit left the port, so a stuck menu left logging off Windows as the way out. | Alt+F4 / Alt+Q, and closing the window, ask the game to quit; if it does not in two seconds the port stops itself (`user32.c`). |
| Install a mod and Back were off-screen | The Windows Options rows began at y=190 in 56 px steps with 36 px text, so the twelfth row sat at 806 on a 720 px canvas and was clipped. | 30 px text in 42 px steps from 150: thirteen rows end at 654. |
| 1080p was scaled down | The window was created 1280x720 and never resized; `resolution=` only set the render size, so the letterbox blit shrank 1080p into a 720p window. | The window is created at `resolution=` and follows it, `fullscreen=1` is a borderless desktop, and Alt+Enter / Options > Window switch. |

## Open

### 1. The FMVs are skipped

`data/movie/*.000` were played through DirectShow on the PC release. The port stubs the DirectShow objects the
game creates, so nothing reads those files and every movie is skipped.

**What a fix needs, in order:** the container identified (a hex dump of one file's first 64 bytes decides between
Bink and MPEG-1/program-stream), a decoder for it (FFmpeg's Bink decoder or `pl_mpeg` for MPEG-1 — both are
single-file friendly), and the graph the game builds (`IGraphBuilder`, `IMediaControl`, `IVideoWindow`,
`IMediaEvent`) implemented over that decoder, presenting frames through the same D3D8 surface path the game
already uses for its front-buffer readback. `src/win32` has the COM-in-guest-memory pattern this needs (the D3D8
device is built the same way).

Not attempted here because it cannot be verified without the files: a decoder that is never run against real
video is a guess, and a wrong guess is worse than the documented gap.

### 2. Black screen after the Office Building bathroom cutscene

Press any button and play continues, which points at the fade/state machine rather than a crash: something the
fade-in expects to be signalled never is, so the game sits in its "wait for the screen" state.

**What a fix needs:** reproduce, then `log=1` plus `SH3_COVER=<frame>` to log the functions that run after the
cutscene and find the one that should have cleared the fade. The pause-menu version of this class of bug (a
fade-in counted as unfinished, greying Save/Load out) is already diagnosed and fixed in `src/game/sh3.c`
(`sub_00495B60`), so the same shape of fix is plausible.

### 3. Logos and credits stretched on a wide screen

The Konami and KCET logos and the credits are drawn as full-screen 2D, so at 16:9 they stretch. Gameplay and
menus are handled: `sub_00682BA0` centres the 2D in a 4:3 area and a whitelist of call sites
(`full_width_calls` in `src/game/sh3.c`) keeps the elements that are meant to span the screen.

**What a fix needs:** the call sites those screens use, from a `SH3_COVER` trace of the logo sequence — then the
logo/credits images are either removed from the span-everything behaviour or letterboxed by returning the 4:3
mapping for their frame. It is a list edit once the addresses are known; guessing the addresses is what breaks
the other 2D.

### 4. Untested past the first areas

The port has been played to the first areas only. The two crashes that were fixed upstream were the same cause —
callbacks that the disassembler never found, so the lifter never translated them — and nine more instances were
fixed at the same time. More of that class will exist.

**What a fix needs:** play it. A crash writes `sh3-crash.log` next to the executable with the last indirect call
targets and the guest stack, which is exactly what identifies an untranslated callback; `tools/seed_from_log`
and `tools/observed_seeds.txt` are how those become part of the next `tools/regen.sh` run.

### 5. Save compatibility between versions

Saves, `savedata/key.ini` and the shader cache live next to the executable, as before. A `key.ini` written by an
older build keeps its old button layout (delete it for the current defaults), and there is no save migration:
the file format is the game's own.

### 6. The PS2-exclusive outfits are not available (issue #1)

The PlayStation 2 release has fourteen magazine-promo costumes the PC release never shipped: no models for them in
`chrpl.arc`, and no entries in the executable's outfit tables.

[A mod that ports them exists](https://github.com/amiraliuks/silent-hill-3-enhanced), and it is not something this
port can load: it ships as a 32-bit ASI DLL that patches the original `sh3.exe` in memory (MinHook detours on RVAs
of the `0x400000` image: the player-model pointer table at `0x310C90`, the password lookup at `0x1D7F90`, the
costume inventory table at `0x71E02C`). This port never runs that executable — it recompiles it to native C — so
there is no process to hook and no 32-bit address space to patch.

**What a fix needs, in order:**

1. The converted models. They are ordinary PC `.mdl` files written into patched copies of `data/chrpl.arc` and
   `data/arc.arc` by the mod's own tool, made from the player's PS2 copy: its NOTICE forbids redistributing them,
   so they cannot be bundled here either. `mods/<name>/files/` already serves any file the game opens for reading,
   archives included, so that half is a `mods/` folder and no code at all; `tools/ps2_outfits.py` builds it in one command.
2. The executable-side tables, re-expressed as a build-time patch: the fourteen model-path pointers, the
   twelve-entry password table and the count that walks it, and the costume inventory table. `tools/regen.sh`
   already patches the image before lifting it (the widescreen fix lives there), so this belongs in that stage —
   as data edits, not the mod's runtime code cave; `patches/*.patch` is that stage, applied by
   `tools/patch_image.py` with every edit checked against the original bytes.
3. The game running. The mod's own documentation calls the models prototypes and says outfit rendering still needs
   a stability pass, so a blind table patch could not be called done.

Not attempted here because there are no game files: a table patch that is never loaded against real archives is a
guess, and the assets cannot be shipped. The local pipeline is in place instead: `tools/ps2_outfits.py` builds the
archive mod from models the player converts, and `patches/*.patch` carries the table edits into the build (see
"PS2-exclusive costumes" in `MODDING.md`).

### 7. The copy's own sh3.exe still hangs on Options

The launcher-only build plays an imported Full-Rip by starting that folder's own 2003 `sh3.exe`. That executable
tests every display mode Windows reports before its Options screen appears, and on a modern PC (1100+ modes) it
hangs there. Nothing the launcher does reaches inside another process: the hotkey it registers (Ctrl+Alt+End)
ends the game instead of fixing the screen, and the `sh3.ini` settings this port documents are the port's, not
that executable's.

**What a fix needs:** code loaded into that process, which is what the community fixes are (Steam006's PC Fix,
Silent Hill 3: Rebirth, the hex edit that cuts the list to sixteen). Injecting an unsigned DLL into someone's
game is what antivirus software flags, and it cannot be tested here without the game, so this project takes the
route it can stand behind: it recompiles that executable, and the recompiled port has the sixteen-mode list and
the `WM_DISPLAYCHANGE` answer the 2003 build lacks (`docs/BUILD-WINDOWS.md`).
