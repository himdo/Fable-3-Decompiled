#!/usr/bin/env python3
"""Generate orig/functions.json from the freshly built base main.obj.

objdiff matches functions between the target (orig/main.obj) and the base
(Release/Win32/main.obj) by symbol NAME. The rebuilt base object carries the
user's functions as MSVC-mangled names such as ``?sub_401000@@YAXPAX@Z`` --
the decompiled function's IDA address is encoded in the name (``sub_401000``
== the function at 0x401000).

This script:
  1. Scans the base main.obj for null-terminated ``?sub_XXXXXX@@...`` symbols
     (robust to the file being a COFF bigobj, which plain parsers choke on).
  2. Decodes each function's address from its name.
  3. Looks up the function size in ida_functions.json (a one-time export of the
     original exe's function table: address -> size, from IDA).
  4. Writes orig/functions.json: a list of {name, rva, size} that
     make_target_obj.py uses to stamp matching symbols into the target object.

Because the names are copied verbatim from the base object and the sizes come
from the original binary, objdiff gets a true per-function diff.

Usage:
    python gen_functions.py [base_obj] [ida_functions.json] [out_functions.json]
"""

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# A mangled name: '?' + 'sub_' + hex address + '@@' + the rest of the mangling.
SUB_RE = re.compile(rb"(\?sub_([0-9A-Fa-f]{4,8})@@[A-Za-z0-9_@`$]+)\x00")


def extract_sub_names(obj_bytes: bytes) -> list[tuple[str, int]]:
    """Return (mangled_name, absolute_ea) for each unique sub_XXXXXX symbol,
    in first-seen order."""
    seen = set()
    out: list[tuple[str, int]] = []
    for m in SUB_RE.finditer(obj_bytes):
        name = m.group(1).decode("latin1")
        ea = int(m.group(2), 16)
        if name in seen:
            continue
        seen.add(name)
        out.append((name, ea))
    return out


def main() -> int:
    base_obj = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "Release" / "Win32" / "main.obj"
    ida_map = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "ida_functions.json"
    out = Path(sys.argv[3]) if len(sys.argv) > 3 else HERE / "orig" / "functions.json"

    if not base_obj.exists():
        print(f"gen_functions: missing {base_obj}", file=sys.stderr)
        return 1
    if not ida_map.exists():
        print(f"gen_functions: missing {ida_map} "
              f"(export it once from the original exe's IDA database)", file=sys.stderr)
        return 1

    imagebase = int(json.loads(ida_map.read_text(encoding="utf-8"))["imagebase"], 16)
    funcs = json.loads(ida_map.read_text(encoding="utf-8"))["functions"]

    names = extract_sub_names(base_obj.read_bytes())
    records = []
    missing = []
    for name, ea in names:
        key = hex(ea)
        if key not in funcs:
            missing.append((name, key))
            continue
        records.append({
            "name": name,
            "rva": ea - imagebase,
            "size": funcs[key]["size"],
        })

    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(records, f, indent=0)
        f.write("\n")

    print(f"gen_functions: {len(names)} sub_ symbols in {base_obj.name}; "
          f"wrote {len(records)} to {out.relative_to(HERE) if out.is_relative_to(HERE) else out}")
    if missing:
        print(f"gen_functions: WARNING {len(missing)} symbols not in ida_functions.json "
              f"(first: {missing[0][0]} @ {missing[0][1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
