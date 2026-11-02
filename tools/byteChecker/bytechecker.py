#!/usr/bin/env python3
"""
byteChecker — byte-identical function matcher between two PE executables.

Opens each EXE in IDA (headless, via the public `ida-domain` / `idapro`
packages), enumerates every function, hashes its raw bytes, and reports
every pair of functions (one from each EXE) whose byte code is 100%
identical. The result is written as JSON, ready to feed into Chaos Viewer.

No LLM logic — pure deterministic code.

Subcommands
  compare-exe  EXE_A EXE_B [-o out.json]     Full pipeline: dump both EXEs,
                                             then compare the dumps.
  dump         EXE [-o dump.json]            Dump one EXE's function bytes
                                             (hashed) to a reusable JSON cache.
  compare      DUMP_A DUMP_B [-o out.json]   Compare two existing dumps
                                             (no IDA needed).

Notes
  * First `dump` of an EXE runs a full IDA auto-analysis and is slow.
    The EXE is copied into a cache dir and its .i64 is kept, so later
    runs of the same EXE are fast.
  * Dump JSON files are self-contained (function EAs, names, sizes and
    SHA-256 of the function bytes), so comparison afterwards is O(n).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent
DEFAULT_CACHE_DIR = TOOL_DIR / ".cache"
DUMP_FORMAT_VERSION = 1


def path_key(exe_path: Path) -> str:
    """Stable, collision-free cache key for a source EXE path."""
    h = hashlib.sha256(str(exe_path.resolve()).lower().encode("utf-8")).hexdigest()[:10]
    return f"{exe_path.stem}-{h}"


# --------------------------------------------------------------------------
# IDA helpers
# --------------------------------------------------------------------------

def _load_ida_domain():
    try:
        import ida_domain  # noqa: F401
    except Exception as e:
        sys.exit(
            "error: cannot import 'ida_domain' (IDA is required for dumping EXEs).\n"
            f"  import error : {e}\n"
            "  fix          : pip install ida-domain   (requires an IDA Pro/Free\n"
            "                installation; it is located via %APPDATA%\\Hex-Rays\\IDA Pro\n"
            "                ida-config.json, Paths.ida-install-dir)\n"
            "  tip          : if you already have dump JSON files, use the 'compare'\n"
            "                 subcommand which does not need IDA at all."
        )
    import ida_domain
    return ida_domain


def dump_exe(exe_path: Path, out_json: Path, cache_dir: Path, fresh: bool = False) -> dict:
    """Open `exe_path` in headless IDA and dump every function's bytes/hashes."""
    ida_domain = _load_ida_domain()

    if not exe_path.is_file():
        sys.exit(f"error: EXE not found: {exe_path}")

    # Work on a private copy so IDA never creates .i64/.id* files next to the
    # original binary. Reusing the copy + its .i64 makes re-dumps fast.
    workdir = cache_dir / path_key(exe_path)
    if fresh:
        shutil.rmtree(workdir, ignore_errors=True)
    workdir.mkdir(parents=True, exist_ok=True)
    dst = workdir / exe_path.name
    src_size = exe_path.stat().st_size
    if not dst.exists() or dst.stat().st_size != src_size:
        print(f"[dump] copying {exe_path} -> {dst}")
        shutil.copy2(exe_path, dst)

    t0 = time.time()
    db = ida_domain.Database()
    db.save_on_close = False
    has_idb = any(dst.with_suffix(s).exists() for s in (".i64", ".idb"))
    print(
        f"[dump] opening {dst} in IDA "
        f"({'reusing existing idb' if has_idb else 'full auto-analysis, first run is slow'})..."
    )
    db.open(str(dst))

    functions: list[dict] = []
    try:
        for f in db.functions.get_all():
            start = int(f.start_ea)
            end = int(f.end_ea)
            if end <= start:
                continue
            data = db.bytes.get_bytes_at(start, end - start)
            if not data:
                continue  # unreadable / uninitialized range — skip
            functions.append(
                {
                    "ea": f"0x{start:08x}",
                    "name": db.functions.get_name(f) or "",
                    "size": end - start,
                    "sha256": hashlib.sha256(data).hexdigest(),
                }
            )
    finally:
        db.close()

    functions.sort(key=lambda x: int(x["ea"], 16))
    dump = {
        "tool": "byteChecker-dump",
        "version": DUMP_FORMAT_VERSION,
        "path": str(exe_path),
        "file_sha256": hashlib.sha256(dst.read_bytes()).hexdigest(),
        "function_count": len(functions),
        "dumped_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "elapsed_seconds": round(time.time() - t0, 1),
        "functions": functions,
    }
    out_json.parent.mkdir(parents=True, exist_ok=True)
    with open(out_json, "w", encoding="utf-8") as fh:
        json.dump(dump, fh)
    print(
        f"[dump] {len(functions):>8d} functions dumped in {dump['elapsed_seconds']}s "
        f"-> {out_json}"
    )
    return dump


# --------------------------------------------------------------------------
# Comparison
# --------------------------------------------------------------------------

def load_dump(path: Path) -> dict:
    if not path.is_file():
        sys.exit(f"error: dump file not found: {path}")
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def compare_dumps(dump_a: dict, dump_b: dict, label_a: str, label_b: str, min_size: int = 0) -> dict:
    """Match functions between two dumps by (size, sha256) of their bytes."""
    index_b: dict[tuple[int, str], list[int]] = {}
    for i, fb in enumerate(dump_b["functions"]):
        index_b.setdefault((fb["size"], fb["sha256"]), []).append(i)

    # Group by identical code body: all functions sharing the same bytes on
    # each side form one group, so a 1-byte stub present at 5000 EAs is one
    # entry with occurrence lists, not 5000 lines.
    ea_cap = 256  # cap on EAs listed per side per group (counts stay exact)
    grouped: dict[tuple[int, str], dict] = {}
    for fa in dump_a["functions"]:
        if fa["size"] < min_size:
            continue
        hits = index_b.get((fa["size"], fa["sha256"]))
        if not hits:
            continue
        g = grouped.setdefault(
            (fa["size"], fa["sha256"]),
            {
                "size": fa["size"],
                "sha256": fa["sha256"],
                "a": [],
                "b": [],
                "a_count": 0,
                "b_count": 0,
            },
        )
        g["a"].append({"ea": fa["ea"], "name": fa["name"]})
        g["a_count"] += 1
        for j in hits:
            fb = dump_b["functions"][j]
            g["b"].append({"ea": fb["ea"], "name": fb["name"]})
            g["b_count"] += 1

    matched_a_eas: set[str] = set()
    matched_b_eas: set[str] = set()
    size_histogram: dict[str, int] = {}
    matching_pairs = 0
    for g in grouped.values():
        matching_pairs += g["a_count"] * g["b_count"]
        size_histogram[str(g["size"])] = (
            size_histogram.get(str(g["size"]), 0) + g["a_count"] * g["b_count"]
        )
        matched_a_eas.update(x["ea"] for x in g["a"])
        matched_b_eas.update(x["ea"] for x in g["b"])

    grouped_matches: list[dict] = []
    for g in grouped.values():
        a_list = sorted(g["a"], key=lambda x: int(x["ea"], 16))
        b_list = sorted(g["b"], key=lambda x: int(x["ea"], 16))
        truncated = len(a_list) > ea_cap or len(b_list) > ea_cap
        a_list, b_list = a_list[:ea_cap], b_list[:ea_cap]
        grouped_matches.append(
            {
                "size": g["size"],
                "sha256": g["sha256"],
                "occurrences_a": g["a_count"],
                "occurrences_b": g["b_count"],
                "a_names": sorted({x["name"] for x in g["a"]}),
                "b_names": sorted({x["name"] for x in g["b"]}),
                "a_eas": [x["ea"] for x in a_list],
                "b_eas": [x["ea"] for x in b_list],
                "ea_lists_truncated": truncated,
            }
        )
    grouped_matches.sort(key=lambda g: (g["size"], g["sha256"]))
    matched_b_bytes = sum(fb["size"] for fb in dump_b["functions"] if fb["ea"] in matched_b_eas)

    return {
        "tool": "byteChecker",
        "version": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "match_rule": "functions are matched when size and SHA-256 of the full function bytes are equal (100% byte identical)",
        "a": {
            "label": label_a,
            "path": dump_a["path"],
            "file_sha256": dump_a.get("file_sha256"),
            "function_count": dump_a["function_count"],
        },
        "b": {
            "label": label_b,
            "path": dump_b["path"],
            "file_sha256": dump_b.get("file_sha256"),
            "function_count": dump_b["function_count"],
        },
        "stats": {
            "min_size": min_size,
            "matching_pairs": matching_pairs,
            "distinct_code_groups": len(grouped_matches),
            "ea_list_cap_per_side": ea_cap,
            "size_histogram": dict(sorted(size_histogram.items(), key=lambda kv: int(kv[0]))),
            "matched_functions_a": len(matched_a_eas),
            "matched_functions_b": len(matched_b_eas),
            "matched_ratio_a": round(len(matched_a_eas) / dump_a["function_count"], 6)
            if dump_a["function_count"]
            else 0.0,
            "matched_bytes_a": sum(
                fa["size"] for fa in dump_a["functions"] if fa["size"] >= min_size and fa["ea"] in matched_a_eas
            ),
            "matched_bytes_b": sum(
                fb["size"] for fb in dump_b["functions"] if fb["size"] >= min_size and fb["ea"] in matched_b_eas
            ),
            "total_bytes_a": sum(fa["size"] for fa in dump_a["functions"]),
            "total_bytes_b": sum(fb["size"] for fb in dump_b["functions"]),
        },
        # One entry per distinct byte-identical code body. Each group lists
        # every function EA (up to ea_list_cap_per_side per side; counts are
        # always exact) plus the function names observed on each side.
        "matches": grouped_matches,
    }


def print_summary(result: dict) -> None:
    s = result["stats"]
    a, b = result["a"], result["b"]
    print()
    print("=" * 72)
    print(f"A: {a['label']}  ({a['path']})")
    print(f"B: {b['label']}  ({b['path']})")
    print("-" * 72)
    print(f"functions in A            : {a['function_count']:>10d}")
    print(f"functions in B            : {b['function_count']:>10d}")
    print(f"byte-identical pairs      : {s['matching_pairs']:>10d}")
    print(f"matched functions (A)     : {s['matched_functions_a']:>10d}  ({s['matched_ratio_a'] * 100:.2f}%)")
    print(f"matched functions (B)     : {s['matched_functions_b']:>10d}")
    print(f"matched bytes (A)         : {s['matched_bytes_a']:>10d} / {s['total_bytes_a']}")
    print(f"matched bytes (B)         : {s['matched_bytes_b']:>10d} / {s['total_bytes_b']}")
    print("=" * 72)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def _default_output(label_a: str, label_b: str, out: str | None) -> Path:
    if out:
        return Path(out)
    return Path.cwd() / "bytechecker_matches.json"


def cmd_compare_exe(args: argparse.Namespace) -> None:
    cache_dir = Path(args.cache_dir)
    exe_a = Path(args.exe_a).resolve()
    exe_b = Path(args.exe_b).resolve()

    dumps = {"a": cache_dir / f"{path_key(exe_a)}.funcdump.json", "b": cache_dir / f"{path_key(exe_b)}.funcdump.json"}
    for name, exe in (("a", exe_a), ("b", exe_b)):
        if not dumps[name].exists() or args.fresh:
            dump_exe(exe, dumps[name], cache_dir / "work", fresh=args.fresh)
        else:
            print(f"[dump] reusing existing dump {dumps[name]}")

    dump_a = load_dump(dumps["a"])
    dump_b = load_dump(dumps["b"])
    result = compare_dumps(
        dump_a, dump_b, args.label_a or exe_a.stem, args.label_b or exe_b.stem, min_size=args.min_size
    )

    out = _default_output(result["a"]["label"], result["b"]["label"], args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(result, fh, indent=args.indent)
    print_summary(result)
    print(f"\nresults written to: {out}")


def cmd_dump(args: argparse.Namespace) -> None:
    exe = Path(args.exe).resolve()
    out = (
        Path(args.output).resolve()
        if args.output
        else Path(args.cache_dir) / f"{path_key(exe)}.funcdump.json"
    )
    dump_exe(exe, out, Path(args.cache_dir) / "work", fresh=args.fresh)


def cmd_compare(args: argparse.Namespace) -> None:
    dump_a = load_dump(Path(args.dump_a).resolve())
    dump_b = load_dump(Path(args.dump_b).resolve())
    label_a = args.label_a or Path(dump_a["path"]).stem
    label_b = args.label_b or Path(dump_b["path"]).stem
    result = compare_dumps(dump_a, dump_b, label_a, label_b, min_size=args.min_size)
    out = _default_output(label_a, label_b, args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(result, fh, indent=args.indent)
    print_summary(result)
    print(f"\nresults written to: {out}")


def main() -> None:
    ap = argparse.ArgumentParser(
        prog="bytechecker",
        description="Find 100% byte-identical functions between two executables (via IDA).",
    )
    ap.add_argument(
        "--indent", type=int, default=2, help="JSON indent for output files (default 2)"
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("compare-exe", help="dump both EXEs with IDA, then compare")
    p.add_argument("exe_a", help="first EXE (e.g. original Fable3.exe)")
    p.add_argument("exe_b", help="second EXE (e.g. decomp build Fable3.exe)")
    p.add_argument("-o", "--output", help="output JSON path (default: ./bytechecker_matches.json)")
    p.add_argument("--cache-dir", default=str(DEFAULT_CACHE_DIR), help="cache dir for EXE copies + dumps")
    p.add_argument("--fresh", action="store_true", help="force re-dump even if a dump file exists")
    p.add_argument("--label-a", help="label for EXE A in the report")
    p.add_argument("--label-b", help="label for EXE B in the report")
    p.add_argument("--min-size", type=int, default=0, help="ignore functions smaller than N bytes")
    p.set_defaults(func=cmd_compare_exe)

    p = sub.add_parser("dump", help="dump one EXE's function bytes to a reusable JSON file")
    p.add_argument("exe")
    p.add_argument("-o", "--output", help="output dump JSON path (default: <cache-dir>/<stem>.funcdump.json)")
    p.add_argument("--cache-dir", default=str(DEFAULT_CACHE_DIR))
    p.add_argument("--fresh", action="store_true")
    p.set_defaults(func=cmd_dump)

    p = sub.add_parser("compare", help="compare two existing dump JSON files (no IDA needed)")
    p.add_argument("dump_a")
    p.add_argument("dump_b")
    p.add_argument("-o", "--output", help="output JSON path")
    p.add_argument("--label-a")
    p.add_argument("--label-b")
    p.add_argument("--min-size", type=int, default=0, help="ignore functions smaller than N bytes")
    p.set_defaults(func=cmd_compare)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
