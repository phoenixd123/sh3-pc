# Credits

**Silent Hill 3 — PC** is an unofficial, fan-made port. It contains no assets or program code from the game;
you need your own copy of Silent Hill 3 for PC.

## This port

**phoenixd123** (@phoenixdobsonyc on Instagram) — PC (Windows) platform layer, the mod system and HD texture
packs, the launcher's credits screen, and the bug fixes listed in [BUGS.md](BUGS.md).

The name on the credits screen comes from the build: `cmake -DPORT_AUTHOR="Your Name"` (it defaults to
`phoenixd123`), and `PORT_NAME` sets the title under the menu. The screen shows once on first launch — delete
`savedata/port-credits` to see it again — and is always reachable from the launcher's menu.

## Standing on

- **[sh3-nx](https://github.com/cutarev/sh3-nx) by cutarev** — the port this started from: the recompilation
  pipeline for `sh3.exe`, the Direct3D 8 → OpenGL layer, the DirectSound/DirectInput/kernel32/user32 bridges, the
  widescreen work and the Switch target. Without it there is nothing here.
- **[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) by sp00nznet** (LGPL-2.1, vendored in
  `third_party/xboxrecomp`): the x86 → C lifter the game's code is generated with.
- **[sh2-nx](https://github.com/cutarev/sh2-nx) by cutarev** — the runtime this port's runtime began as.
- **[WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack) by ThirteenAG** (MIT): the
  widescreen fix Silent Hill 3's patches come from.
- **nfsmw-nx** — the Horizon guest-memory technique the Switch build uses.
- **[stb](https://github.com/nothings/stb) by Sean Barrett** (public domain): `stb_image` and `stb_image_write`,
  for the mod system's PNGs.
- **[devkitPro](https://devkitpro.org), [switchbrew/libnx](https://github.com/switchbrew/libnx), SDL2, Mesa** —
  the Switch toolchain and the two graphics/audio layers the port sits on.
- **Ligux01** (PR #6 to sh3-nx: the winding-order lifter bug fixed in v0.06), and everyone who reported the
  bugs listed in sh3-nx's issues — their fixes are in this tree.

## Licences

The port's own code is MIT ([LICENSE](LICENSE)). Three things it does not cover:

- **third_party/xboxrecomp** is LGPL-2.1: see `third_party/xboxrecomp/LICENSE` and `LICENSES` there.
- **The widescreen patches** come from ThirteenAG's MIT-licensed
  [WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack).
- **Silent Hill 3 is Konami's**: no game code, data or assets are included in this repository or licensed by it.

## The game

**Silent Hill 3** is © Konami Digital Entertainment. Created by **Team Silent**: Kazuhide Nakazawa (director),
Akira Yamaoka (sound and music), Masahiro Ito (art), Hiroyuki Owaku and Takayoshi Sato (story), and the rest of
the team. Silent Hill is a trademark of Konami. This project is not affiliated with Konami.
