#!/usr/bin/env python3
"""Generate Chaos Viewer data for the Fable 3 Launcher decomp.

The function universe is the single source of truth in
'Fable 3 Launcher/main.cpp': every stub carries an annotation

    // <name>  (0x<start> - 0x<end>)

Matched / near-miss status comes from (optional, in this directory):
  matched.json   - list of names or {"name", "author", "srcPath", "addrHex"}
  nearmiss.json  - list of {"name", "div", "cat", "floor"?}

Outputs:
  <repo root>/chaos-db.json  lean index, at the location the hosted viewer
                             probes (<branch>/chaos-db.json)
  (optionally synced into a local Chaos Viewer clone via --viewer DIR:
   DIR/data/chaos-db.json and DIR/public/details/*)

Committed alongside on the branch (regenerate after IDA renames, see README):
  <repo root>/details/fable3-launcher.json   per-function IDA detail chunk

Usage:
  python chaos-viewer/scripts/generate_chaos_db.py [--repo <decomp root>]
      [--viewer <chaos-viewer clone>]
"""
import argparse
import json
import pathlib
import re
import shutil
import sys
import time

MODULE = "fable3-launcher"
FUNC_RE = re.compile(r"^//\s+(\S+)\s+\(0x([0-9A-Fa-f]+)\s*-\s*0x([0-9A-Fa-f]+)\)")


def load_list(path):
    if not path.exists():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    return data if isinstance(data, list) else []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(pathlib.Path(__file__).resolve().parents[2]))
    ap.add_argument("--main-cpp", default=None)
    ap.add_argument("--viewer", default=r"D:\projects\hacking\Windows\chaos-viewer",
                    help="local Chaos Viewer clone to sync into ('' to skip)")
    args = ap.parse_args()

    here = pathlib.Path(__file__).resolve().parent.parent
    root = pathlib.Path(args.repo)
    main_cpp = pathlib.Path(args.main_cpp) if args.main_cpp else root / "Fable 3 Launcher" / "main.cpp"
    if not main_cpp.exists():
        print(f"ERROR: {main_cpp} not found (pass --main-cpp)", file=sys.stderr)
        sys.exit(2)

    # ---- universe from main.cpp annotations -------------------------------
    universe, seen = [], set()
    for line in main_cpp.read_text(encoding="utf-8").splitlines():
        m = FUNC_RE.match(line)
        if not m:
            continue
        name, start, end = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
        if name in seen:
            continue
        seen.add(name)
        universe.append((name, start, end - start))
    universe.sort(key=lambda u: u[1])
    print(f"universe: {len(universe)} functions from {main_cpp.name}")

    by_name = {u[0]: u for u in universe}

    # ---- matched / near-miss status ----------------------------------------
    matched = {}
    for e in load_list(here / "matched.json"):
        rec = e if isinstance(e, dict) else {"name": e}
        name = rec["name"]
        if name in by_name:
            matched[name] = rec
        else:
            print(f"warning: matched entry not in universe: {name}", file=sys.stderr)
    nearmiss = {}
    for e in load_list(here / "nearmiss.json"):
        rec = e if isinstance(e, dict) else {"name": e, "div": 1}
        name = rec["name"]
        if name in by_name:
            nearmiss[name] = rec
        else:
            print(f"warning: near-miss entry not in universe: {name}", file=sys.stderr)

    # ---- emit chaos-db.json -------------------------------------------------
    functions = []
    total_bytes = matched_bytes = matched_n = 0
    for name, addr, size in universe:
        total_bytes += size
        is_matched = name in matched
        if is_matched:
            matched_bytes += size
            matched_n += 1
        rec = {"id": f"{MODULE}:0x{addr:06x}", "module": MODULE, "name": name,
               "addr": addr, "size": size, "matched": is_matched}
        m = matched.get(name)
        if m and m.get("srcPath"):
            rec["srcPath"] = m["srcPath"]
        if m and m.get("author"):
            rec["author"] = m["author"]
        if m and m.get("matchProvenance"):
            rec["matchProvenance"] = m["matchProvenance"]
        nm = nearmiss.get(name)
        if nm and not is_matched:
            rec["div"] = nm.get("div", 1)
            if nm.get("cat"):
                rec["cat"] = nm["cat"]
            if nm.get("floor"):
                rec["floor"] = str(nm["floor"])[:60]
        functions.append(rec)

    pc_path = here / "project.config.json"
    project = json.loads(pc_path.read_text(encoding="utf-8")) if pc_path.exists() else None

    db = {
        "generatedAt": time.strftime("%Y-%m-%d %H:%M"),
        "project": project,
        "stats": {
            "totalFunctions": len(universe),
            "matchedFunctions": matched_n,
            "totalBytes": total_bytes,
            "matchedBytes": matched_bytes,
            "moduleCount": 1,
        },
        "functions": functions,
    }
    out = root / "chaos-db.json"
    out.write_text(json.dumps(db), encoding="utf-8")
    print(f"wrote {out} ({out.stat().st_size // 1024} KB)")
    print(f"stats: {matched_n}/{len(universe)} funcs, {matched_bytes}/{total_bytes} bytes")

    # ---- sync into a local viewer clone ------------------------------------
    if args.viewer:
        viewer = pathlib.Path(args.viewer)
        if (viewer / "package.json").exists():
            (viewer / "data").mkdir(exist_ok=True)
            shutil.copy2(out, viewer / "data" / "chaos-db.json")
            dsrc, ddst = root / "details", viewer / "public" / "details"
            if dsrc.is_dir():
                ddst.mkdir(parents=True, exist_ok=True)
                for f in dsrc.glob("*.json"):
                    shutil.copy2(f, ddst / f.name)
            print(f"synced to viewer clone: {viewer}")
        else:
            print(f"note: no viewer clone at {args.viewer}, skipped sync")


if __name__ == "__main__":
    main()
