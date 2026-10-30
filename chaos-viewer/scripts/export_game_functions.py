#!/usr/bin/env python3
"""Export the fable3-game function universe from an open IDA database.

Run this INSIDE IDA (File > Script file, or idapysmall) with Fable3.exe open.
It writes every function in the .text section to:

    chaos-viewer/fable3-game/functions.json

as a list of {"name", "addr", "size"} (addr/size as hex strings for compactness),
sorted by address. Re-run after IDA renames to refresh names, then re-run
chaos-viewer/scripts/generate_chaos_db.py to rebuild the atlas.

Usage:
    python export_game_functions.py [path/to/functions.json]
        (default: <repo>/chaos-viewer/fable3-game/functions.json)
"""
import json
import os
import sys

import ida_funcs
import idautils


def main():
    # .text range; fall back to auto-detecting the largest .text segment.
    lo = hi = None
    for seg in db.segments.get_all():
        nm = (db.segments.get_name(seg) or "").lower()
        if nm == ".text":
            if lo is None or seg.start_ea < lo:
                lo = seg.start_ea
            if seg.end_ea > (hi or 0):
                hi = seg.end_ea
    if lo is None:
        print("ERROR: no .text segment found")
        sys.exit(2)

    out = []
    n = 0
    for ea in idautils.Functions():
        if not (lo <= ea < hi):
            continue
        f = ida_funcs.get_func(ea)
        if f is None:
            continue
        out.append({"name": f.name, "addr": "0x%06x" % ea, "size": "0x%x" % (f.end_ea - ea)})
        n += 1
    out.sort(key=lambda r: int(r["addr"], 16))

    if len(sys.argv) > 1:
        outpath = sys.argv[1]
    else:
        here = os.path.dirname(os.path.abspath(__file__))
        outpath = os.path.normpath(os.path.join(here, "..", "fable3-game", "functions.json"))
    with open(outpath, "w", encoding="utf-8") as fh:
        json.dump(out, fh)
    print("exported %d functions (0x%X - 0x%X) -> %s" % (n, lo, hi, outpath))
    print("file size: %.1f KB" % (os.path.getsize(outpath) / 1024.0))


if __name__ == "__main__":
    main()
