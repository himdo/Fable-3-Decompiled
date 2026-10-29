#!/usr/bin/env python3
"""Extract the icon assets from the original FableLauncher.exe.

The icon bitmaps are a copyrighted Microsoft/Lionhead asset, so they are NOT
committed to the repo. Instead they are re-extracted from the original binary
at build time. This script (re)generates, in the output dir (res/ by default):

  - fable.ico      the 5-image .ico that rc.exe embeds (256/64/48/16/32 px)
  - icon1..5.bin   raw RT_ICON resource data (id 1..5)  [reference/verify]
  - groupicon.bin  raw RT_GROUP_ICON resource data      [reference/verify]

The .ico is rebuilt byte-for-byte from the RT_ICON images: a 6-byte ICONDIR,
one 16-byte ICONDIRENTRY per image (ordered by resource id), then the image
blobs concatenated. Each blob's BITMAPINFOHEADER supplies width/height/bpp.

Usage:
    extract_icons.py [original.exe] [output_dir]
    (defaults: the known original path, and <script_dir>/res)
"""
import os
import struct
import sys
import pefile

DEFAULT_ORIG = r"D:\projects\hacking\Windows\Fable 3\FableLauncher.exe"

RT_ICON = 3
RT_GROUP_ICON = 14


def collect_resources(pe):
    """Return {(type, id, lang): data_bytes} for every resource in the PE."""
    out = {}
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        ttype = t.id
        for n in t.directory.entries:
            nid = n.id
            for l in n.directory.entries:
                d = l.data
                if isinstance(d, list):
                    d = d[0]
                out[(ttype, nid, l.id)] = pe.get_data(d.struct.OffsetToData, d.struct.Size)
    return out


def bmp_info(blob):
    """Parse (width, height, planes, bpp) from a BITMAPINFOHEADER (blob start)."""
    _, w, h, planes, bpp = struct.unpack_from('<IiiHH', blob, 0)
    return w, h, planes, bpp


def ico_dim(value):
    """Encode a pixel dimension for ICONDIRENTRY (0 stands in for 256)."""
    return value % 256


def build_ico(icons):
    """icons: list of (id, blob) ordered by id. Return the .ico file bytes."""
    n = len(icons)
    header = 6 + n * 16
    entries = b""
    blobs = b""
    offset = header
    for _, blob in icons:
        w, h, _planes, bpp = bmp_info(blob)
        entries += struct.pack('<BBBBHHII',
                               ico_dim(w), ico_dim(h // 2), 0, 0, 1, bpp,
                               len(blob), offset)
        blobs += blob
        offset += len(blob)
    return struct.pack('<HHH', 0, 1, n) + entries + blobs


def main():
    orig = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_ORIG
    outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "res")
    if not os.path.isfile(orig):
        print(f"ERROR: original not found: {orig}", file=sys.stderr)
        return 1
    os.makedirs(outdir, exist_ok=True)

    pe = pefile.PE(orig)
    res = collect_resources(pe)

    # RT_ICON images, ordered by resource id (1..N)
    icons = sorted(
        ((nid, data) for (t, nid, _lang), data in res.items() if t == RT_ICON),
        key=lambda x: x[0])
    if not icons:
        print("ERROR: no RT_ICON (type 3) resources found", file=sys.stderr)
        return 1

    # raw icon bins
    for i, (_nid, blob) in enumerate(icons, 1):
        path = os.path.join(outdir, f"icon{i}.bin")
        with open(path, "wb") as f:
            f.write(blob)
        w, h, _p, bpp = bmp_info(blob)
        print(f"  icon{i}.bin  id={_nid}  {w}x{h // 2}  {bpp}bpp  {len(blob)} bytes")

    # raw group icon bin
    groups = [data for (t, _n, _l), data in res.items() if t == RT_GROUP_ICON]
    if groups:
        g = groups[0]
        path = os.path.join(outdir, "groupicon.bin")
        with open(path, "wb") as f:
            f.write(g)
        print(f"  groupicon.bin  {len(g)} bytes")
    else:
        print("WARNING: no RT_GROUP_ICON (type 14) found", file=sys.stderr)

    # fable.ico
    ico = build_ico(icons)
    path = os.path.join(outdir, "fable.ico")
    with open(path, "wb") as f:
        f.write(ico)
    print(f"  fable.ico  {len(icons)} images  {len(ico)} bytes")

    print(f"extracted {len(icons) + 2} files to {outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
