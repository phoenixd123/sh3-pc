# Mods and HD textures

The port can read game files and textures from mod folders instead of the game's own `data/`. That is how HD
texture packs, retextures, replacement audio and scripted tweaks are installed: the mod is a folder you drop next
to `sh3.exe`, and nothing in it comes from this repository.

> **No game content is included here and none can be.** A texture pack is a derivative of the game's own art, so
> it is made from *your* copy, for *your* copy, and is not something to redistribute. `tools/make_hd_pack.py`
> exists to build one from your install.

## Installing a mod from the launcher

Options > **Install a mod** asks for a `.zip`, unpacks it into `mods/<archive name>/` and loads it: no restart and no
unpacking by hand. Dropping a `.zip` on the port does the same and starts the game.

What the archive holds decides its shape, so a mod packaged any of the usual ways works as it is:

| In the archive | Becomes |
| --- | --- |
| `files/...` or `mod.ini` at the top | `mods/<name>/` as it stands |
| everything inside one folder, `MyMod/files/...` | `mods/<name>/files/...` |
| loose game paths, `data/tex/...` | `mods/<name>/files/data/tex/...` |

Refused, with the reason on screen: a file that is not an archive, an archive with nothing in it, a path that would
land outside the game folder, and a name that is already taken. Stored and deflated entries are read; anything else
has to be repacked as a normal zip. A mod installed this way is enabled like any other, so `mod.ini` and
`load_order.txt` below change it from then on.

## Where mods live

```text
sh3-pc/  (or the folder with sh3.exe)
  sh3.exe
  data/
  sh3.ini
  mods/
    load_order.txt          optional: one mod folder per line, earlier lines load first
    hd-textures/
      mod.ini
      textures/              <key>.png - HD replacements
      files/                 any game file, at the path the game opens
        data/tex/...
```

A folder under `mods/` counts as a mod if it has a `mod.ini`, a `files/` folder or a `textures/` folder.

### `mod.ini`

Every key is optional:

| Key | Meaning |
| --- | --- |
| `name=HD Texture Pack` | what the log calls it |
| `enabled=0` | skip it without deleting it |
| `priority=10` | higher loads first when `load_order.txt` does not place it |

Order matters because the first mod that provides a file wins. `load_order.txt` overrides priorities, and
anything not listed comes after the listed mods, by priority and then folder name.

## Loose files: replacing any game file

Any file the game opens for reading can be replaced. The override path is the path the game itself opens, so a
mod that replaces `data/tex/htl.tex` holds `mods/<mod>/files/data/tex/htl.tex`. This is what a retexture,
a model swap, or a translated script uses. Writes (saves, settings) never go to a mod folder.

```text
mods/silent-fix/
  mod.ini            name=Silent Fix
  files/data/...
```

## HD textures

The game reads its textures out of `.tex` archives by offset, so no filename ever reaches the renderer. Textures
are therefore identified by a hash of the pixels the game uploads:

- **Invariant:** the hash covers the level-0 pixels, the format and the size (and, for paletted textures, the
  palette). The same texture always has the same key, across areas, runs and machines.
- **Replacement:** a PNG at `mods/<mod>/textures/<key>.png` is uploaded in place of the original at *its own*
  resolution, with a generated mip chain, so a 4× upscale stays sharp instead of shimmering.
- **Paletted textures (P8):** the same indices under a different palette are a different image and therefore a
  different key, which is correct: the dump gives you one PNG per distinct appearance.

### Making a pack

1. Dump the textures the game loads. Either set `texdump=1` in `sh3.ini` or the `SH3_TEXDUMP` environment
   variable, then play through the areas you want to cover. Each upload is written once, as
   `texdump/<key>.png`, with `texdump/index.txt` listing key, size and format.
2. Upscale them. Anything that writes PNG works: `realesrgan-ncnn-vulkan`, `waifu2x-ncnn-vulkan`, chaiNNer, a
   Photoshop batch action, or just a resize if you only want 2×.
3. Build the mod folder:

   ```sh
   python tools/make_hd_pack.py --dump texdump --name "HD Texture Pack" \
       --upscale "realesrgan-ncnn-vulkan -i {in} -o {out} -n realesrgan-x4plus -s 4"
   ```

   That writes `mods/hd-texture-pack/textures/<key>.png` and its `mod.ini`. Omit `--upscale` to copy the dumps
   unchanged (useful for testing the pipeline before spending GPU time).
4. Set `hd=1` in `sh3.ini` (the default) and start the game. The log names each texture a pack replaces.

### Settings that affect the look

| `sh3.ini` | Effect |
| --- | --- |
| `hd=1` | read texture packs (default). `hd=0` uses the game's own textures with the mods installed |
| `aniso=8` | anisotropic filtering, 1-16 (default 8, `1` = off) |

`aniso` also turns on mip filtering for stages the game asked for none, which is what keeps an HD texture from
smearing at grazing angles; a texture with a single level samples exactly as before.

### Limits you will hit

- **Formats that cannot be dumped** (DXT1/3/5, V8U8) are logged and skipped by texdump, because the dumper only
  writes what it can expand to RGBA. They can still be *replaced*: the key comes from the raw bytes.
- **2D textures only.** Cube maps and render targets are the game's own; a pack entry for one is ignored.
- **Dynamic textures** (the ones the game re-uploads every frame) keep their replacement, so a texture that is
  animated with new pixels each frame shows the replacement instead. That is the right behaviour for art and the
  wrong one for anything generated, so packs should target static art.
- **P8 textures** need one PNG per palette appearance, as described above.

## PS2-exclusive costumes, from your own copy

The PlayStation 2 release has fourteen promo costumes the PC release never shipped. None of them are here, and
none can be: the models are Konami's, so they have to come from your own copy. What is here is the tooling, so
the job is one command on your machine, plus a table patch to make them selectable.

### 1. Convert the models

Each costume is a pair of ordinary PC `.mdl` files, `<name>_pc_prototype.mdl` (Heather) and
`<name>_pc_chhaa_prototype.mdl` (the alternate model), converted from your own PS2 disc. This repository does
not convert PS2 models; use a converter you trust, or models you already made.

### 2. Build the archives into a mod

```sh
python tools/ps2_outfits.py --game "C:\Games\Silent Hill 3" --models C:\converted --zip
```

That reads `data/chrpl.arc` and `data/arc.arc` from your installation, appends the models to copies, and
writes `<game>/mods/ps2-outfit-models/` (`mod.ini`, `files/data/chrpl.arc`, `files/data/arc.arc`). Only that
folder is written; the game's own files are untouched. `--zip` also writes `ps2-outfit-models.zip` beside it,
which **Options > Install a mod** installs like any other archive. `--out` puts the mod somewhere else. The port already serves game files out of `files/`, archives included, so enabling the mod
is enough for the models to load.

The tool checks both archive signatures, refuses compressed or damaged entries, refuses a path that is already
in the catalog, and writes nothing outside the mod folder.

### 3. Point the outfit tables at them

The models are in the archive now, but the PC executable's tables do not reference them: there are fourteen
unused player-model resource slots, and no password entries for the PS2 costumes. That data is in `sh3.exe`,
not in code the port can change from `src/game`, so it is edited before recompilation by a patch file:

```sh
python tools/patch_image.py "C:\Games\Silent Hill 3\sh3.exe" --check patches\my-outfits.patch
```

`tools/regen.sh` applies every `patches/*.patch` on the next build. `patches/README.md` has the file format.
The addresses, from the supported PC executable (image base `0x400000`):

| What | Where | Shape |
| --- | --- | --- |
| Player-model resource pointers | RVA `0x310C90` | 318 `u32` path pointers; slot 1 is `data/pcchr/pl/chhaa.mdl`, which confirms the executable |
| Costume inventory table | VA `0x71E02C` | 12 `u16` entries |
| Password map | VA `0x71E1B4` | `u16` costume ID and `u16` message ID per entry, 12 vanilla entries |
| Password hashes | VA `0x71E1E8` | 16 bytes per entry, same order as the map |
| Password count | RVA `0x1D809B` | the instruction that reads the entry count |

Filling a slot is a `u32` pointing at the model's path: the string can go in a run of zero bytes with
`string`, and the pointer with `replace`. The inventory table is what lists a costume in **Extra Costume**,
and the password table is what unlocks it, so all three have to agree. Do this step with the game running:
`patch_image.py` only checks that a patch lands on the bytes it expects, not that the table it builds is right.

This last step is documented rather than automated. Without your converted models and a build to run them
against, a generated table patch would be a guess.

## Writing other kinds of mods

`files/` is the general mechanism, so anything the game reads can be swapped. What it reads and when is what
`log=1` in `sh3.ini` shows: with `log=1` every file the game fails to open is named, and with `trace=1` every
Windows call including `CreateFileA` is logged, which is how the existing behaviour was mapped out.
