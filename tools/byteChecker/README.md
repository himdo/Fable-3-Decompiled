# byteChecker

Finds every function that is **100% byte-identical** between two PE executables
(e.g. the original `Fable3.exe` and your decomp build `Fable3.exe`) and writes
the results to a JSON file you can feed into Chaos Viewer.

Pure deterministic code — no LLM logic. Function boundaries and bytes come from
IDA (headless, via the public `ida-domain` + `idapro` pip packages); matching is
just a hash comparison.

## How it works

1. `dump` — copy the EXE into a private cache dir, open it in headless IDA with
   auto-analysis, iterate every function, and record for each:
   `ea`, `name`, `size`, and `sha256` of the full function bytes.
2. `compare` — index one dump by `(size, sha256)` and look up every function of
   the other dump. A match means the entire function byte code is identical.

The first dump of an EXE runs a full IDA auto-analysis (slow). The cache keeps
the EXE copy and its `.i64`, so re-runs and re-comparisons are fast.

## Setup

```bat
pip install ida-domain
```

Requires an IDA Pro/Free installation. IDA is located via
`%APPDATA%\Hex-Rays\IDA Pro\ida-config.json` → `Paths.ida-install-dir`
(create it if missing and point it at your IDA folder).

## Usage

Full pipeline (two EXEs → matches JSON):

```bat
python tools\byteChecker\bytechecker.py compare-exe ^
    "D:\projects\hacking\Windows\Fable 3\Fable3.exe" ^
    "D:\projects\hacking\Windows\Fable 3\Fable 3 Decomp Project\Fable 3\Release\Win32\Fable3.exe" ^
    -o matches.json
```

Other subcommands:

```bat
:: dump a single EXE to a reusable JSON (no comparison)
python bytechecker.py dump <exe> -o <stem>.funcdump.json

:: compare two existing dump files (no IDA needed)
python bytechecker.py compare <a>.funcdump.json <b>.funcdump.json -o matches.json
```

Useful flags:
- `--cache-dir DIR` — relocate the EXE-copy/.i64/dump cache
  (default: `tools/byteChecker/.cache`)
- `--fresh` — force a re-dump even if a dump file exists
- `--label-a` / `--label-b` — names shown in the report (e.g. `original`, `decomp`)
- `--min-size N` — ignore functions smaller than N bytes (useful to hide
  trivial 1-byte nullsub stubs that match in thousands of places)

## Output format

```jsonc
{
  "tool": "byteChecker",
  "match_rule": "functions are matched when size and SHA-256 of the full function bytes are equal",
  "a": { "label": "original", "path": "...", "file_sha256": "...", "function_count": 1234 },
  "b": { "label": "decomp",   "path": "...", "file_sha256": "...", "function_count": 100 },
  "stats": {
    "min_size": 32,
    "matching_pairs": 42,
    "distinct_code_groups": 40,
    "ea_list_cap_per_side": 256,
    "size_histogram": { "512": 2, "48": 1 },
    "matched_functions_a": 40,
    "matched_functions_b": 42,
    "matched_ratio_a": 0.0324,
    "matched_bytes_a": 987654,
    "matched_bytes_b": 990000,
    "total_bytes_a": 30412345,
    "total_bytes_b": 250000
  },
  "matches": [
    {
      "size": 512,
      "sha256": "...",
      "occurrences_a": 2,
      "occurrences_b": 1,
      "a_names": ["sub_401234"],
      "b_names": ["SomeNamespace::foo"],
      "a_eas": ["0x00401234", "0x0045abcd"],
      "b_eas": ["0x00401000"],
      "ea_lists_truncated": false
    }
  ]
}
```

`matches` contains one entry per **distinct byte-identical code body**.
Identical code that appears at multiple EAs (inlinings, duplicated stubs,
per-character write helpers, …) is collapsed into a single group with
occurrence counts and EA lists, so a 1-byte nullsub present at 5,000 EAs is one
line instead of thousands. EA lists are capped at `ea_list_cap_per_side` (256)
per side for readability; `occurrences_*` and the stats are always exact.

EAs are image addresses as reported by IDA for each binary (the two EXEs may
have different base addresses; only code bytes are compared).

## Caveats

- Matching depends on IDA's function splitting being the same in both binaries
  (it will be, when the code is byte-identical).
- The two EXEs may have different base addresses; the tool only compares
  function bytes, never absolute addresses.
- Data-only "functions" (thunks, import stubs) are included, since they are
  plain functions as far as IDA is concerned.
