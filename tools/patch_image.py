#!/usr/bin/env python3
"""Applies build-time byte patches to sh3.exe before it is recompiled.

Some of the game's data is not code and cannot be reached from src/game: the outfit tables, the password
lookup, the model resource list. They live in sh3.exe, so they are edited there, before tools/regen.sh lifts
it. A patch file is plain text:

    # comments start with #
    replace 0x310C90 00000000 901F7100
    string  0x6A0000 "data/pcchr/pl/ps2_egm.mdl"

`replace <rva> <old> <new>` writes <new> at <rva> only when the bytes there are exactly <old>, so a patch
cannot silently land on the wrong executable. Both are contiguous hex and must be the same length.
`string <rva> <text>` writes a NUL-terminated string, and only into a run of zero bytes, so it cannot
overwrite something that is already there.

    python tools/patch_image.py sh3.exe -o out.exe patches/*.patch
    python tools/patch_image.py sh3.exe --check patches/*.patch

tools/regen.sh applies patches/*.patch automatically. What the outfit tables need is described in MODDING.md.
"""
import argparse
import glob
import struct
import sys


class PE:
    """Just enough PE to turn an RVA into a file offset."""

    def __init__(self, data):
        if len(data) < 0x40 or data[:2] != b"MZ":
            sys.exit("not a PE executable (no MZ header)")
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            sys.exit("not a PE executable (no PE header)")
        coff = e_lfanew + 4
        nsections = struct.unpack_from("<H", data, coff + 2)[0]
        opt_size = struct.unpack_from("<H", data, coff + 16)[0]
        opt = coff + 20
        magic = struct.unpack_from("<H", data, opt)[0]
        if magic not in (0x10B, 0x20B):
            sys.exit(f"unknown optional header magic {magic:#x}")
        self.size_of_image = struct.unpack_from("<I", data, opt + 56)[0]
        self.sections = []
        table = opt + opt_size
        for i in range(nsections):
            at = table + i * 40
            name = data[at:at + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, raw_size, raw_ptr = struct.unpack_from("<IIII", data, at + 8)
            self.sections.append((name, va, vsize, raw_ptr, raw_size))
        self.data = data

    def offset(self, rva, length):
        if rva + length > self.size_of_image:
            sys.exit(f"RVA {rva:#x} is past the image ({self.size_of_image:#x} bytes)")
        for name, va, vsize, raw_ptr, raw_size in self.sections:
            if va <= rva < va + max(vsize, raw_size):
                delta = rva - va
                if delta + length > raw_size:
                    sys.exit(f"RVA {rva:#x} is in section {name} but not backed by file data")
                return raw_ptr + delta
        if rva < self.sections[0][1]:
            return rva  # headers
        sys.exit(f"RVA {rva:#x} is not inside a section")


def parse_patch(path):
    patches = []
    with open(path, "r", encoding="utf-8-sig") as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line or line[0] in "#;":
                continue
            kind, _, rest = line.partition(" ")
            rest = rest.strip()
            try:
                if kind == "replace":
                    rva_s, old_s, new_s = rest.split()
                    rva = int(rva_s, 0)
                    old, new = bytes.fromhex(old_s), bytes.fromhex(new_s)
                    if not old or len(old) != len(new):
                        raise ValueError("old and new must be the same non-zero length")
                    patches.append((path, number, "bytes", rva, old, new))
                elif kind == "string":
                    rva_s, text = rest.split(None, 1)
                    text = text.strip()
                    if len(text) < 2 or text[0] not in "\"'" or text[-1] != text[0]:
                        raise ValueError('the text must be quoted, like "data/x.mdl"')
                    rva = int(rva_s, 0)
                    raw = text[1:-1].encode("utf-8") + b"\0"
                    patches.append((path, number, "string", rva, b"\0" * len(raw), raw))
                else:
                    raise ValueError(f"unknown patch '{kind}'")
            except ValueError as e:
                sys.exit(f"{path}:{number}: {e}")
    return patches


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="sh3.exe (or another PE)")
    ap.add_argument("patches", nargs="+", help="patch files")
    ap.add_argument("-o", "--out", help="where to write the patched copy")
    ap.add_argument("--check", action="store_true", help="verify every patch applies, write nothing")
    args = ap.parse_args()

    files = []
    for pattern in args.patches:
        files.extend(glob.glob(pattern) or [pattern])
    files = sorted({f for f in files if f.lower().endswith(".patch")})
    if not files:
        sys.exit("no .patch files given")
    if not args.check and not args.out:
        sys.exit("an output path (-o) is required unless --check is used")

    data = bytearray(open(args.input, "rb").read())
    pe = PE(data)
    applied = 0
    for path, number, kind, rva, old, new in [p for f in files for p in parse_patch(f)]:
        at = pe.offset(rva, len(old))
        found = bytes(data[at:at + len(old)])
        if kind == "string" and found != old:
            sys.exit(f"{path}:{number}: {rva:#x} is not a run of zero bytes; choose empty space "
                     f"for the string")
        if found != old:
            sys.exit(f"{path}:{number}: {rva:#x} expected {old.hex()}, found {found.hex()} "
                     f"(wrong sh3.exe version?)")
        data[at:at + len(new)] = new
        applied += 1
        print(f"  {rva:#x} -> file {at:#x}: {len(new)} bytes  ({kind})")

    if args.check:
        print(f"{applied} patches apply to {args.input}")
        return
    open(args.out, "wb").write(data)
    print(f"{applied} patches -> {args.out}")


if __name__ == "__main__":
    main()
