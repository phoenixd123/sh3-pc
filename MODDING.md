# Mods and HD textures

The port can read game files and textures from mod folders instead of the game's own `data/`. That is how HD
texture packs, retextures, replacement audio and scripted tweaks are installed: the mod is a folder you drop next
to `sh3.exe`, and nothing in it comes from this repository.

> **No game content is included here and none can be.** A texture pack is a derivative of the game's own art, so
> it is made from *your* copy, for *your* copy, and is not something to redistribute. `tools/make_hd_pack.py`
> exists to build one from your install.

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

## Writing other kinds of mods

`files/` is the general mechanism, so anything the game reads can be swapped. What it reads and when is what
`log=1` in `sh3.ini` shows: with `log=1` every file the game fails to open is named, and with `trace=1` every
Windows call including `CreateFileA` is logged, which is how the existing behaviour was mapped out.
