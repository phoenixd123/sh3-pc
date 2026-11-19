# Build-time patches

Every `*.patch` in this folder is applied to `game/sh3.exe` by `tools/regen.sh` before the executable is
recompiled. Nothing here is needed for a normal build: an empty folder builds the vanilla game, and a patch
that does not match your executable stops the build instead of producing a broken one.

One change per file. The format is two line kinds:

```text
# comments start with # or ;
replace 0x310C90 00000000 401F7100
string  0x6A0000 "data/pcchr/pl/ps2_egm.mdl"
```

- `replace <rva> <old> <new>` writes `<new>` at `<rva>` only when the bytes there are exactly `<old>`. Both are
  contiguous hex and must be the same length. RVAs are relative to the image base (`0x400000`), which is what
  a disassembler shows minus the base.
- `string <rva> "text"` writes a NUL-terminated string, and only into a run of zero bytes, so it cannot
  overwrite something already there. Use it for the model paths, then point a table entry at the address with
  `replace`.

Check a patch against your own copy before building:

```sh
python tools/patch_image.py "C:\Games\Silent Hill 3\sh3.exe" --check patches/my-outfits.patch
```

Why this exists: the outfit tables, the password lookup and the model resource list are data in the
executable, not code the port can replace from `src/game`. The addresses and table shapes are in
"PS2-exclusive costumes" in [MODDING.md](../MODDING.md).
