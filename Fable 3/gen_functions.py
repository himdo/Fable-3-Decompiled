#!/usr/bin/env python3
"""Generate orig/functions.json for the Fable 3 (Fable3.exe) decomp project.

The "Fable 3" build compiles 14 source files, each to its own object. For
objdiff we build a parallel set of *native* (no /GL) objects -- one per source
file -- named ``<source>_objdiff.obj`` in Release/Win32. Each decompiled
function is named ``sub_XXXXXX`` where XXXXXX is the function's RVA in the
original Fable3.exe (image base 0x16d5000, so the RVA is NOT the VA).

This script:
  1. Scans every ``*_objdiff.obj`` for null-terminated ``?sub_XXXXXX@@...``
     symbols (robust to bigobj; the names live in the string table).
  2. Decodes each function's RVA from its name and records which source file
     (base object) it came from.
  3. Looks up the function size in ida_functions.json (keyed by RVA).
  4. Writes orig/functions.json: a list of {name, rva, size, source}.

``source`` is the object basename without the ``_objdiff.obj`` suffix, e.g.
``common`` for common_objdiff.obj. gen_objdiff.py groups functions by source so
it can emit one objdiff unit per source file (base .obj vs the shared target).

Usage:
    python gen_functions.py <ida_functions.json> <out_functions.json> [Release/Win32 dir]
"""

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Mangled C++ name for ``sub_XXXXXX``: '?' + 'sub_' + hex + '@@' + rest, NUL-terminated.
SUB_RE = re.compile(rb"(\?sub_([0-9A-Fa-f]{4,8})@@[A-Za-z0-9_@`$]+)\x00")


def extract_sub_names(obj_bytes: bytes):
    """Return (mangled_name, rva) for each unique sub_XXXXXX symbol, in order."""
    seen = set()
    out = []
    for m in SUB_RE.finditer(obj_bytes):
        name = m.group(1).decode("latin1")
        rva = int(m.group(2), 16)
        if name in seen:
            continue
        seen.add(name)
        out.append((name, rva))
    return out


def main() -> int:
    ida_map = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "ida_functions.json"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "orig" / "functions.json"
    objdir = Path(sys.argv[3]) if len(sys.argv) > 3 else HERE / "Release" / "Win32"

    if not ida_map.exists():
        print(f"gen_functions: missing {ida_map} "
              f"(export it once via ida_export.py)", file=sys.stderr)
        return 1
    if not objdir.is_dir():
        print(f"gen_functions: missing {objdir}", file=sys.stderr)
        return 1

    data = json.loads(ida_map.read_text(encoding="utf-8"))
    funcs = data["functions"]
    # The export is keyed by the function's address in the IDA database (the
    # decompiled name sub_XXXXXX decodes to that same address).
    # The database was loaded with image base 0x400000, but the original Fable3.exe
    # has image base 0x16d5000. The original RVA (used to place the symbol in the
    # copied .text section) is (db_address - 0x400000).
    DB_BASE = 0x400000

    base_objs = sorted(objdir.glob("*_objdiff.obj"))
    if not base_objs:
        print(f"gen_functions: no *_objdiff.obj in {objdir}", file=sys.stderr)
        return 1

    records = []
    missing = []
    dup = []
    seen_names = set()
    for obj in base_objs:
        source = obj.name[: -len("_objdiff.obj")]
        names = extract_sub_names(obj.read_bytes())
        for name, db_addr in names:
            if name in seen_names:
                dup.append((name, source))
                continue
            seen_names.add(name)
            key = "0x%08x" % db_addr
            entry = funcs.get(key)
            if entry is None:
                missing.append((name, key))
                continue
            records.append({
                "name": name,
                "rva": db_addr - DB_BASE,  # original RVA (offset from image base)
                "size": entry["size"],
                "source": source,
            })

    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(records, f, indent=0)
        f.write("\n")

    nsrc = len({r["source"] for r in records})
    print(f"gen_functions: {len(records)} sub_ symbols across {len(base_objs)} objects "
          f"({nsrc} sources with functions); wrote {out.name}")
    if missing:
        print(f"gen_functions: WARNING {len(missing)} symbols not in ida_functions.json "
              f"(first: {missing[0][0]} @ {missing[0][1]})")
    if dup:
        print(f"gen_functions: WARNING {len(dup)} duplicate symbols across objects "
              f"(first: {dup[0][0]} in {dup[0][1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
