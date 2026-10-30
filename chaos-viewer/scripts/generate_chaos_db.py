#!/usr/bin/env python3
"""Generate Chaos Viewer atlas data for the Fable 3 Decomp project (multi-module).

The atlas covers every module listed in 'chaos-viewer/modules.json'. Each module
declares where its function universe comes from:

    {"type": "main_cpp", "path": "Fable 3 Launcher/main.cpp"}
        -> parse '// <name>  (0x<start> - 0x<end>)' annotations
    {"type": "json",     "path": "fable3-game/functions.json"}
        -> read a list of {"name": ..., "addr": <int>, "size": <int>}

Matched / near-miss status comes from per-module files (relative to this dir):
    <module>/matched.json    - list of names or {"name","author","srcPath","matchProvenance"}
    <module>/nearmiss.json   - list of {"name","div","cat","floor"?}

Outputs:
    <repo root>/chaos-db.json   combined lean index, at the location the hosted
                                viewer probes (<branch>/chaos-db.json)
    (optionally synced into a local Chaos Viewer clone via --viewer DIR:
     DIR/data/chaos-db.json and DIR/public/details/*)

Committed alongside on the branch (regenerate after IDA renames, see README):
    <repo root>/details/<module>.json   per-function IDA detail chunk

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

FUNC_RE = re.compile(r"^//\s+(\S+)\s+\(0x([0-9A-Fa-f]+)\s*-\s*0x([0-9A-Fa-f]+)\)")


def load_list(path):
    if not path.exists():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    return data if isinstance(data, list) else []


def universe_from_main_cpp(path):
    """Parse '// <name>  (0x<start> - 0x<end>)' annotations into (name, addr, size)."""
    universe, seen = [], set()
    for line in path.read_text(encoding="utf-8").splitlines():
        m = FUNC_RE.match(line)
        if not m:
            continue
        name, start, end = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
        if name in seen:
            continue
        seen.add(name)
        universe.append((name, start, end - start))
    universe.sort(key=lambda u: u[1])
    return universe


def universe_from_json(path):
    """Read [{'name','addr','size'}] (addr/size also accepted as '0x...' strings)."""
    raw = load_list(path)
    universe, seen = [], set()
    for e in raw:
        name = e["name"]
        if name in seen:
            continue
        seen.add(name)
        addr = int(e["addr"], 0) if isinstance(e["addr"], str) else int(e["addr"])
        size = int(e["size"], 0) if isinstance(e["size"], str) else int(e["size"])
        universe.append((name, addr, size))
    universe.sort(key=lambda u: u[1])
    return universe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(pathlib.Path(__file__).resolve().parents[2]))
    ap.add_argument("--viewer", default=r"D:\projects\hacking\Windows\chaos-viewer",
                    help="local Chaos Viewer clone to sync into ('' to skip)")
    args = ap.parse_args()

    here = pathlib.Path(__file__).resolve().parent.parent   # chaos-viewer/
    root = pathlib.Path(args.repo)

    registry = json.loads((here / "modules.json").read_text(encoding="utf-8"))
    modules = registry.get("modules", [])
    if not modules:
        print("ERROR: no modules in modules.json", file=sys.stderr)
        sys.exit(2)

    functions = []
    total_bytes = matched_bytes = matched_n = total_n = 0
    modules_seen = []

    for mod in modules:
        label = mod["label"]
        src = mod["universe"]
        if src["type"] == "main_cpp":
            spath = root / src["path"]
            if not spath.exists():
                print(f"warning: module {label} main_cpp not found: {spath}", file=sys.stderr)
                continue
            universe = universe_from_main_cpp(spath)
        elif src["type"] == "json":
            jpath = here / src["path"]
            if not jpath.exists():
                print(f"warning: module {label} functions.json not found: {jpath} (module skipped)", file=sys.stderr)
                continue
            universe = universe_from_json(jpath)
        else:
            print(f"warning: unknown universe type for {label}: {src['type']}", file=sys.stderr)
            continue

        by_name = {u[0]: u for u in universe}

        matched = {}
        for e in load_list(here / mod["matched"]):
            rec = e if isinstance(e, dict) else {"name": e}
            name = rec["name"]
            if name in by_name:
                matched[name] = rec
            else:
                print(f"warning: [{label}] matched entry not in universe: {name}", file=sys.stderr)

        nearmiss = {}
        for e in load_list(here / mod["nearmiss"]):
            rec = e if isinstance(e, dict) else {"name": e, "div": 1}
            name = rec["name"]
            if name in by_name:
                nearmiss[name] = rec
            else:
                print(f"warning: [{label}] near-miss entry not in universe: {name}", file=sys.stderr)

        for name, addr, size in universe:
            total_bytes += size
            total_n += 1
            is_matched = name in matched
            if is_matched:
                matched_bytes += size
                matched_n += 1
            rec = {"id": f"{label}:0x{addr:06x}", "module": label, "name": name,
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
        modules_seen.append(label)
        print(f"module {label}: {len(universe)} funcs, {sum(1 for n,_,_ in universe if n in matched)} matched")

    functions.sort(key=lambda r: (r["module"], r["addr"]))

    pc_path = here / "project.config.json"
    project = json.loads(pc_path.read_text(encoding="utf-8")) if pc_path.exists() else None

    db = {
        "generatedAt": time.strftime("%Y-%m-%d %H:%M"),
        "project": project,
        "stats": {
            "totalFunctions": total_n,
            "matchedFunctions": matched_n,
            "totalBytes": total_bytes,
            "matchedBytes": matched_bytes,
            "moduleCount": len(modules_seen),
            "modules": modules_seen,
        },
        "functions": functions,
    }
    out = root / "chaos-db.json"
    out.write_text(json.dumps(db), encoding="utf-8")
    print(f"wrote {out} ({out.stat().st_size // 1024} KB)")
    print(f"stats: {matched_n}/{total_n} funcs across {len(modules_seen)} modules, "
          f"{matched_bytes}/{total_bytes} bytes")

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
