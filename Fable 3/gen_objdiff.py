#!/usr/bin/env python3
"""Generate objdiff.json for the Fable 3 (Fable3.exe) decomp project.

Called automatically at the end of build_vs2008.bat, so objdiff.json is
regenerated after every build. Can also be run manually:

    python gen_objdiff.py [--original PATH] [--out PATH]

The "Fable 3" build compiles 14 source files, each to its own object. build_
vs2008.bat compiles every source twice: the first pass uses /GL /LTCG and feeds
the linker (matching the original's codegen); the second pass omits /GL so it
emits a plain machine-code object (<name>_objdiff.obj) that objdiff can
disassemble -- a /GL .obj holds MSObj IR in .cil$ sections and cannot be read.

What it does:
  1. Copies the original Fable3.exe into orig\\ (when missing/out of date).
  2. Runs gen_functions.py to read every *_objdiff.obj and, using the one-time
     IDA export ida_functions.json (address -> size), write orig/functions.json
     -- the set of {name, rva, size, source} symbols. The base objects' function
     names are MSVC-mangled ``?sub_XXXXXX@@...`` where XXXXXX is the function's
     address in the IDA database, so this yields exact names.
  3. Runs make_target_obj.py to (re)build orig\\main.obj, a COFF object with the
     whole original .text section plus every decompiled function's symbol.
  4. Writes objdiff.json (encounter/objdiff schema) with one unit per source
     file that has functions, each comparing that source's *_objdiff.obj (base)
     against the shared orig\\main.obj (target).

One-time setup: ida_functions.json is an export of the original exe's function
list (size per address) from its IDA database. Regenerate it only if the
original binary itself changes.
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_ORIG = HERE.parent.parent / "Fable3.exe"
SCHEMA = "https://raw.githubusercontent.com/encounter/objdiff/main/config.schema.json"

# Source object basename -> source .cpp path (relative to the project root),
# used for the objdiff "source_path" metadata.
SOURCE_PATHS = {
    "main": "main.cpp",
    "framework": "framework/framework.cpp",
    "common": "common/common.cpp",
    "engine": "engine/engine.cpp",
    "worker": "engine/worker.cpp",
    "manager": "engine/manager.cpp",
    "object_registry": "engine/object_registry.cpp",
    "xlo_device": "engine/xlo_device.cpp",
    "gameengine": "engine/gameengine.cpp",
    "gamethread": "engine/gamethread.cpp",
    "gamemain": "engine/gamemain.cpp",
    "netinit": "engine/netinit.cpp",
    "thread": "engine/thread.cpp",
    "save": "save/save.cpp",
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sync_original(orig: Path, dest: Path) -> bool:
    """Copy orig -> dest (preserving mtime) unless already identical."""
    need_copy = not dest.exists() or sha256(dest) != sha256(orig)
    if need_copy:
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(orig, dest)
    return need_copy


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--original", type=Path, default=DEFAULT_ORIG,
                    help=f"original Fable3.exe (default: {DEFAULT_ORIG})")
    ap.add_argument("--out", type=Path, default=HERE / "objdiff.json",
                    help="output config path (default: objdiff.json next to this script)")
    args = ap.parse_args()

    rel = HERE / "Release" / "Win32"
    base_objs = sorted(rel.glob("*_objdiff.obj"))
    if not base_objs:
        print(f"gen_objdiff: no *_objdiff.obj in {rel} (build first)", file=sys.stderr)
        return 1
    if not args.original.exists():
        print(f"gen_objdiff: missing original {args.original}", file=sys.stderr)
        return 1

    dest_orig = HERE / "orig" / "Fable3.exe"
    if sync_original(args.original, dest_orig):
        print(f"gen_objdiff: synced original -> {dest_orig.relative_to(HERE)}")
    else:
        print("gen_objdiff: original already up to date")

    # Extract every base object's function names (with sizes from the IDA map)
    # so the target object carries matching symbols for a per-function diff.
    r = subprocess.run(
        [sys.executable, str(HERE / "gen_functions.py"),
         str(HERE / "ida_functions.json"), str(HERE / "orig" / "functions.json"),
         str(rel)],
        cwd=HERE,
    )
    if r.returncode != 0:
        print("gen_objdiff: gen_functions.py failed", file=sys.stderr)
        return 1

    # Build the COFF target object from the original exe (objdiff can't read PE).
    r = subprocess.run(
        [sys.executable, str(HERE / "make_target_obj.py"), str(dest_orig),
         str(HERE / "orig" / "main.obj")],
        cwd=HERE,
    )
    if r.returncode != 0:
        print("gen_objdiff: make_target_obj.py failed", file=sys.stderr)
        return 1

    # One unit per source file that actually has decompiled functions.
    fns = json.loads((HERE / "orig" / "functions.json").read_text(encoding="utf-8"))
    sources = []
    for fn in fns:
        s = fn["source"]
        if s not in sources:
            sources.append(s)
    sources.sort()

    units = []
    for s in sources:
        base = rel / f"{s}_objdiff.obj"
        if not base.exists():
            continue
        units.append({
            "name": s,
            "target_path": "orig/main.obj",
            "base_path": f"Release/Win32/{s}_objdiff.obj",
            "metadata": {"source_path": SOURCE_PATHS.get(s, f"{s}.cpp")},
        })

    cfg = {
        "$schema": SCHEMA,
        # The .bat does all building; objdiff should just re-diff, not rebuild.
        "build_base": False,
        "build_target": False,
        "watch_patterns": ["*.c", "*.cc", "*.cpp", "*.h", "*.hpp", "*.rc", "*.py"],
        "ignore_patterns": ["Release/**/*", "Debug/**/*", "orig/**/*", "res/**/*", "*.json"],
        "units": units,
    }

    # Atomic write so objdiff never sees a half-written file.
    tmp = args.out.with_suffix(".json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")
    tmp.replace(args.out)

    print(f"gen_objdiff: wrote {args.out} ({len(units)} units)")
    print("gen_objdiff: units:")
    for u in units:
        print(f"  - {u['name']}: {u['base_path']}  vs  {u['target_path']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
