#!/usr/bin/env python3
"""Builds a mod texture pack from a texdump.

The game is run with texdump=1 (or SH3_TEXDUMP set), which writes every texture it uploads to texdump/<key>.png.
This script upscales those PNGs into mods/<name>/textures/, where the game then picks them up (hd=1 in sh3.ini).

    python tools/make_hd_pack.py --dump texdump --name "HD Texture Pack"
    python tools/make_hd_pack.py --dump texdump --name "HD" \
        --upscale "realesrgan-ncnn-vulkan -i {in} -o {out} -n realesrgan-x4plus -s 4"

Nothing here contains game content: it only rearranges files you dumped from your own copy.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys


def slug(name):
    s = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return s or "hd-pack"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", default="texdump", help="folder texdump wrote (default: texdump)")
    ap.add_argument("--name", default="HD Texture Pack", help="pack name, shown in the log")
    ap.add_argument("--out", default="mods", help="mods folder (default: mods)")
    ap.add_argument("--upscale", metavar="CMD",
                    help="command per image, with {in} and {out} placeholders; omit to copy the dumps as they are")
    ap.add_argument("--force", action="store_true", help="redo images that are already in the pack")
    ap.add_argument("--priority", type=int, default=50, help="mod.ini priority (default: 50)")
    args = ap.parse_args()

    if not os.path.isdir(args.dump):
        sys.exit(f"no dump folder '{args.dump}' - run the game with texdump=1 in sh3.ini first")

    images = sorted(f for f in os.listdir(args.dump) if f.lower().endswith(".png"))
    if not images:
        sys.exit(f"'{args.dump}' has no PNGs - did the game reach any area with texdump=1?")

    pack = os.path.join(args.out, slug(args.name))
    textures = os.path.join(pack, "textures")
    os.makedirs(textures, exist_ok=True)

    made = skipped = failed = 0
    for img in images:
        src = os.path.join(args.dump, img)
        dst = os.path.join(textures, img)
        if os.path.exists(dst) and not args.force:
            skipped += 1
            continue
        if args.upscale:
            cmd = args.upscale.replace("{in}", src).replace("{out}", dst).replace("{name}", img)
            r = subprocess.run(cmd, shell=True)
            if r.returncode == 0 and os.path.exists(dst):
                made += 1
            else:
                failed += 1
                print(f"  failed: {img}", file=sys.stderr)
        else:
            shutil.copyfile(src, dst)
            made += 1

    ini = os.path.join(pack, "mod.ini")
    if not os.path.exists(ini):
        with open(ini, "w", encoding="utf-8") as f:
            f.write(f"name={args.name}\nenabled=1\npriority={args.priority}\n")

    print(f"{pack}: {made} written, {skipped} already there, {failed} failed")
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
