#!/usr/bin/env python3
"""Generate objdiff.json for the Fable 3 Launcher decomp project.

Called automatically at the end of build_vs2008.bat, so objdiff.json is
regenerated after every build. Can also be run manually:

    python gen_objdiff.py [--original PATH] [--out PATH]

What it does:
  1. Locates the freshly built Release\\Win32\\FableLauncher.exe / main.obj.
  2. Copies the original FableLauncher.exe into this folder (orig\\) when it
     is missing or out of date, so objdiff only needs project-local files.
  3. Runs gen_functions.py to read the freshly built main_objdiff.obj (a
     native, machine-code object; see below) and, using the one-time IDA
     export ida_functions.json (address -> size for the original exe), write
     orig/functions.json -- the set of {name, rva, size} symbols. The base
     object's function names are MSVC-mangled ``?sub_XXXXXX@@...`` where the
     address is encoded in the name, so this yields exact names.
  4. Runs make_target_obj.py to (re)build orig\\main.obj, a COFF object file
     synthesized from the original exe's sections plus those function symbols
     (objdiff cannot read PE executables directly, hence "Unknown file magic").
  5. Writes objdiff.json (encounter/objdiff schema) with one unit:
        - main : built main_objdiff.obj vs orig\\main.obj
     Because the target carries the same function names as the base, objdiff
     produces a per-function diff.

Note on the base object: build_vs2008.bat compiles main.cpp twice. The first
pass uses /GL /LTCG and feeds the linker (matching the original's codegen). The
second pass omits /GL so it emits a plain machine-code .obj (main_objdiff.obj)
that objdiff can disassemble -- a /GL .obj holds MSObj IR in .cil$ sections and
cannot be read by objdiff.

One-time setup: ida_functions.json is an export of the original exe's function
list (name + size per address) from its IDA database. Regenerate it only if the
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
DEFAULT_ORIG = Path(r"D:\projects\hacking\Windows\Fable 3\FableLauncher.exe")
SCHEMA = "https://raw.githubusercontent.com/encounter/objdiff/main/config.schema.json"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sync_original(orig: Path, dest: Path) -> bool:
    """Copy orig -> dest (preserving mtime) unless already identical.

    Returns True if the file was (re)copied.
    """
    need_copy = not dest.exists() or sha256(dest) != sha256(orig)
    if need_copy:
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(orig, dest)
    return need_copy


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--original", type=Path, default=DEFAULT_ORIG,
                    help=f"original FableLauncher.exe (default: {DEFAULT_ORIG})")
    ap.add_argument("--out", type=Path, default=HERE / "objdiff.json",
                    help="output config path (default: objdiff.json next to this script)")
    args = ap.parse_args()

    rel = HERE / "Release" / "Win32"
    built_exe = rel / "FableLauncher.exe"
    base_obj = rel / "main_objdiff.obj"  # native (no /GL) object for objdiff

    missing = [p for p in (args.original, built_exe, base_obj) if not p.exists()]
    if missing:
        print("gen_objdiff: missing file(s):")
        for p in missing:
            print(f"  - {p}")
        return 1

    dest_orig = HERE / "orig" / "FableLauncher.exe"
    if sync_original(args.original, dest_orig):
        print(f"gen_objdiff: synced original -> {dest_orig.relative_to(HERE)}")
    else:
        print("gen_objdiff: original already up to date")

    # Extract the base object's function names (with sizes from the IDA map)
    # so the target object carries matching symbols for a per-function diff.
    r = subprocess.run(
        [sys.executable, str(HERE / "gen_functions.py"), str(base_obj),
         str(HERE / "ida_functions.json"), str(HERE / "orig" / "functions.json")],
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

    cfg = {
        "$schema": SCHEMA,
        # The .bat does all building; objdiff should just re-diff, not rebuild.
        "build_base": False,
        "build_target": False,
        "watch_patterns": ["*.c", "*.cc", "*.cpp", "*.h", "*.hpp", "*.rc", "*.py"],
        "ignore_patterns": ["Release/**/*", "Debug/**/*", "orig/**/*", "res/**/*", "*.json"],
        "units": [
            {
                "name": "main (obj)",
                "target_path": "orig/main.obj",
                "base_path": "Release/Win32/main_objdiff.obj",
                "metadata": {"source_path": "main.cpp"},
            },
        ],
    }

    # Atomic write so objdiff never sees a half-written file.
    tmp = args.out.with_suffix(".json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
        f.write("\n")
    tmp.replace(args.out)

    print(f"gen_objdiff: wrote {args.out}")
    print("gen_objdiff: units:")
    for u in cfg["units"]:
        print(f"  - {u['name']}: {u['base_path']}  vs  {u['target_path']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
