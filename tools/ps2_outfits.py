#!/usr/bin/env python3
"""Packs converted PS2 costume models into a mod this port loads.

The port reads any file the game opens for reading out of mods/<name>/files/, archives included, so the two
archives written here replace data/chrpl.arc and data/arc.arc without touching the installation. The mod goes
in <game>/mods/; the game's own files are only read. The models come from your own copy of the game,
converted on your machine; nothing here ships or downloads any of it.

    python tools/ps2_outfits.py --game "C:/Silent Hill 3" --models ./converted
    python tools/ps2_outfits.py --game "C:/Silent Hill 3" --models ./converted --zip

The models folder holds one pair per costume, in either of these shapes:

    <name>_pc_prototype.mdl  +  <name>_pc_chhaa_prototype.mdl
    <name>.mdl               +  <name>_chhaa.mdl

`--zip` also writes mods/<name>.zip, which Options > Install a mod (or dropping it on the port) installs.
This tool only fills the archives. The executable's outfit tables are a separate step; see MODDING.md.
"""
import argparse
import gzip
import io
import os
import re
import struct
import sys
import zipfile

ARC_SIG = 0x20030507   # data/chrpl.arc: 16-byte header, count * 16-byte entries, a small directory, then the data
CAT_SIG = 0x20030417   # data/arc.arc: the master catalog, gzipped, made of typed records
PATH_PREFIX = "data/pcchr/pl"
MOD_NAME = "ps2-outfit-models"

PRIMARY_SUFFIX = "_pc_prototype.mdl"
ALT_SUFFIX = "_pc_chhaa_prototype.mdl"
PLAIN_ALT_SUFFIX = "_chhaa.mdl"


def align16(value):
    return (value + 15) & ~15


def fail(message):
    sys.exit(message)


# ---- the model folder -----------------------------------------------------------------------------------

def discover_pairs(models_dir):
    """One (key, primary path, alternate path) per costume, from either naming scheme."""
    if not os.path.isdir(models_dir):
        fail(f"no model folder '{models_dir}'")

    names = sorted(os.listdir(models_dir))
    pairs = {}
    for name in names:
        low = name.lower()
        if low.endswith(ALT_SUFFIX):
            key = name[:-len(ALT_SUFFIX)]
            pairs.setdefault(key, {})["alt"] = name
        elif low.endswith(PRIMARY_SUFFIX):
            key = name[:-len(PRIMARY_SUFFIX)]
            pairs.setdefault(key, {})["primary"] = name
    # The plain <name>.mdl + <name>_chhaa.mdl shape, only where an alternate already exists.
    for key, pair in pairs.items():
        if "alt" not in pair or "primary" in pair:
            continue
        plain = key + ".mdl"
        if plain in names:
            pair["primary"] = plain

    if not pairs:
        fail(f"'{models_dir}' has no converted models: expected <name>{PRIMARY_SUFFIX} and "
             f"<name>{ALT_SUFFIX} (or <name>.mdl and <name>{PLAIN_ALT_SUFFIX})")

    complete = []
    for key in sorted(pairs, key=str.lower):
        pair = pairs[key]
        if "primary" not in pair:
            fail(f"'{key}' has {pair.get('alt')} but no primary model")
        if "alt" not in pair:
            fail(f"'{key}' has {pair['primary']} but no alternate model")
        complete.append((key, os.path.join(models_dir, pair["primary"]),
                         os.path.join(models_dir, pair["alt"])))
    return complete


def model_names(keys):
    """Archive paths for the pairs. Kept short and under data/pcchr/pl, where the model loader expects them."""
    paths = {}
    for key in keys:
        safe = re.sub(r"[^A-Za-z0-9_-]+", "_", key).strip("_").lower()
        if not safe:
            fail(f"'{key}' cannot be used as a model name")
        if safe in paths:
            fail(f"two costumes share the name '{safe}'")
        paths[key] = (f"{PATH_PREFIX}/ps2_{safe}.mdl", f"{PATH_PREFIX}/ps2_{safe}_chhaa.mdl")
    return list(paths.values())


# ---- data/chrpl.arc -------------------------------------------------------------------------------------

def read_arc(data):
    """Header and directory of an uncompressed chrpl archive. Compressed entries are refused: appending to them
    would mean rebuilding their block tables, which this tool does not do."""
    if len(data) < 16 or struct.unpack_from("<I", data, 0)[0] != ARC_SIG:
        fail("data/chrpl.arc is not the archive this port expects (bad signature)")
    count, extra = struct.unpack_from("<II", data, 4)
    table_end = 16 + count * 16
    if count == 0 or table_end + extra > len(data):
        fail("data/chrpl.arc has a damaged directory")
    entries = []
    for i in range(count):
        offset, block_table, size, size2 = struct.unpack_from("<IIII", data, 16 + i * 16)
        if offset < table_end + extra or offset + size > len(data) or size != size2:
            fail(f"data/chrpl.arc entry {i} is compressed or damaged; this tool only appends to a plain archive")
        entries.append((offset, block_table, size, size2))
    return entries, extra


def append_arc(data, additions):
    """Returns data with `additions` ([(name, bytes)]) appended as uncompressed entries.

    The directory grows by one 16-byte entry per file, and every stored offset moves by that much. The added
    entries share one empty block table, which is what the game's reader uses for an uncompressed entry.
    """
    entries, extra = read_arc(data)
    count = len(entries)
    if count + len(additions) > 1000:
        fail("data/chrpl.arc already has as many entries as the format allows")

    n = len(additions)
    table_shift = 16 * n
    block_bytes = align16(max(4, -(-max(len(d) for _, d in additions) // 65536) * 4))
    new_extra = extra + block_bytes

    old_table_end = 16 + count * 16
    old_dir_end = old_table_end + extra
    new_table_end = 16 + (count + n) * 16
    new_dir_end = new_table_end + new_extra
    shift = new_dir_end - old_dir_end

    cursor = align16(len(data) + shift)
    placed = []
    for name, blob in additions:
        cursor = align16(cursor)
        placed.append((cursor, len(blob)))
        cursor += len(blob)

    out = bytearray(cursor)
    struct.pack_into("<IIII", out, 0, ARC_SIG, count + n, new_extra, struct.unpack_from("<I", data, 12)[0])
    out[new_table_end:new_table_end + extra] = data[old_table_end:old_dir_end]
    out[new_dir_end:new_dir_end + (len(data) - old_dir_end)] = data[old_dir_end:]

    for i, (offset, block_table, size, size2) in enumerate(entries):
        struct.pack_into("<IIII", out, 16 + i * 16, offset + shift,
                         block_table + table_shift if block_table else 0, size, size2)

    # One shared block table for the new entries, zeroed: they are stored, not compressed.
    shared_table = new_table_end + extra
    for i, ((offset, size), (_, blob)) in enumerate(zip(placed, additions)):
        struct.pack_into("<IIII", out, 16 + (count + i) * 16, offset, shared_table, size, size)
        out[offset:offset + size] = blob
    return bytes(out)


# ---- data/arc.arc ---------------------------------------------------------------------------------------

def read_catalog(raw):
    """The gzipped catalog as (records, counts offset and record, cluster index)."""
    try:
        data = gzip.decompress(raw)
    except (OSError, EOFError):
        fail("data/arc.arc is not gzip data")
    if len(data) < 16 or struct.unpack_from("<I", data, 0)[0] != CAT_SIG:
        fail("data/arc.arc is not the master catalog this port expects (bad signature)")

    records, counts, clusters, files = [], None, [], []
    offset = 16
    while offset < len(data):
        if offset + 4 > len(data):
            fail("data/arc.arc ends in the middle of a record")
        kind, size = struct.unpack_from("<HH", data, offset)
        if size < 4 or offset + size > len(data):
            fail(f"data/arc.arc record at {offset:#x} has an invalid size")
        record = bytearray(data[offset:offset + size])
        records.append((kind, record))
        if kind == 1:
            counts = record
        elif kind == 2:
            clusters.append(record)
        elif kind == 3:
            files.append(record)
        else:
            fail(f"data/arc.arc has an unknown record type {kind}")
        offset += size

    if counts is None:
        fail("data/arc.arc has no record counts")
    if struct.unpack_from("<I", counts, 4)[0] != len(clusters) or struct.unpack_from("<I", counts, 8)[0] != len(files):
        fail("data/arc.arc's record counts do not match its records")
    return data, records, counts, clusters, files


def add_to_catalog(raw, additions):
    data, records, counts, clusters, files = read_catalog(raw)
    index = next((i for i, c in enumerate(clusters)
                  if bytes(c[8:]).split(b"\0", 1)[0].decode("utf-8", "replace").lower() == "chrpl"), None)
    if index is None:
        fail("data/arc.arc has no chrpl cluster")

    cluster = clusters[index]
    cluster_count = struct.unpack_from("<I", cluster, 4)[0]
    mine = sorted((struct.unpack_from("<H", f, 4)[0], f) for f in files
                  if struct.unpack_from("<H", f, 6)[0] == index)
    if [i for i, _ in mine] != list(range(cluster_count)):
        fail("data/arc.arc's chrpl entries are not a contiguous archive index")

    taken = {bytes(f[8:]).split(b"\0", 1)[0].decode("utf-8", "replace").lower() for f in files}
    for name, _ in additions:
        if name.lower() in taken:
            fail(f"'{name}' is already in data/arc.arc; this mod is installed, or the models were added before")

    struct.pack_into("<I", counts, 8, len(files) + len(additions))
    struct.pack_into("<I", cluster, 4, cluster_count + len(additions))

    added = []
    for i, (name, _) in enumerate(additions):
        encoded = name.encode("utf-8") + b"\0"
        record = bytearray(8 + len(encoded))
        struct.pack_into("<HHH", record, 0, 3, len(record), cluster_count + i)
        struct.pack_into("<H", record, 6, index)
        record[8:] = encoded
        added.append(record)

    out = bytearray(16)  # the catalog header is the signature and three zero fields
    struct.pack_into("<I", out, 0, CAT_SIG)
    for kind, record in records:
        out += record
    for record in added:
        out += record
    packed = io.BytesIO()
    with gzip.GzipFile(fileobj=packed, mode="wb", compresslevel=9, mtime=0) as f:
        f.write(bytes(out))
    return packed.getvalue()


# ---- output ---------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", required=True, help="folder with sh3.exe, data/chrpl.arc and data/arc.arc")
    ap.add_argument("--models", required=True, help="folder with the converted model pairs")
    ap.add_argument("--out", help="mods folder (default: <game>/mods)")
    ap.add_argument("--name", default=MOD_NAME, help=f"mod folder name (default: {MOD_NAME})")
    ap.add_argument("--zip", action="store_true", help="also write <out>/<name>.zip for Options > Install a mod")
    ap.add_argument("--force", action="store_true", help="replace a mod of the same name")
    args = ap.parse_args()

    pairs = discover_pairs(args.models)
    names = model_names([key for key, _, _ in pairs])
    additions = []
    for (key, primary, alt), (primary_name, alt_name) in zip(pairs, names):
        for name, path in ((primary_name, primary), (alt_name, alt)):
            if not os.path.isfile(path):
                fail(f"missing converted model: {path}")
            blob = open(path, "rb").read()
            if not blob:
                fail(f"converted model is empty: {path}")
            additions.append((name, blob))

    arc_path = os.path.join(args.game, "data", "chrpl.arc")
    cat_path = os.path.join(args.game, "data", "arc.arc")
    for path in (arc_path, cat_path):
        if not os.path.isfile(path):
            fail(f"'{path}' is missing: --game must name the folder with sh3.exe and data/")

    archive = append_arc(open(arc_path, "rb").read(), additions)
    catalog = add_to_catalog(open(cat_path, "rb").read(), additions)

    out = args.out or os.path.join(args.game, "mods")
    mod = os.path.join(out, args.name)
    if os.path.exists(mod) and not args.force:
        fail(f"'{mod}' already exists; pass --force to replace it, or --name to keep both")
    files = os.path.join(mod, "files", "data")
    os.makedirs(files, exist_ok=True)
    open(os.path.join(files, "chrpl.arc"), "wb").write(archive)
    open(os.path.join(files, "arc.arc"), "wb").write(catalog)
    with open(os.path.join(mod, "mod.ini"), "w", encoding="ascii", newline="\n") as f:
        f.write("name=PS2 exclusive costume models\n")
        f.write("priority=40\n")

    if args.zip:
        zip_path = os.path.join(out, args.name + ".zip")
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(os.path.join(mod, "files", "data", "chrpl.arc"), "files/data/chrpl.arc")
            z.write(os.path.join(mod, "files", "data", "arc.arc"), "files/data/arc.arc")
            z.write(os.path.join(mod, "mod.ini"), "mod.ini")
        print(f"zip: {zip_path}")

    print(f"{len(pairs)} costumes ({len(additions)} models) -> {mod}")
    for name, blob in additions:
        print(f"  {name}  ({len(blob):,} bytes)")
    print(f"Only {out} was written; the game's own files are untouched. Start the game with this mod enabled; "
          "the log lists it as \"PS2 exclusive costume models\".")
    print("The executable's outfit tables are the other half: see MODDING.md, 'PS2-exclusive costumes'.")


if __name__ == "__main__":
    main()
