# Chaos Viewer — Fable 3 Launcher

Progress atlas for the Fable 3 Launcher byte-identical decomp, powered by
[Chaos Viewer](https://github.com/tangosdev/chaos-viewer).

## Files

| File | Purpose |
|---|---|
| `project.config.json` | Branding + prompt text embedded into the atlas (compiler line, verify command, rules) |
| `matched.json` | Functions verified byte-identical: `[]` or `["sub_401000", {"name": "...", "author": "himdo", "srcPath": "Fable 3 Launcher/main.cpp"}]` |
| `nearmiss.json` | Close-but-not-matched functions: `[{"name": "_WinMain@16", "div": 3, "cat": "stack alignment"}]` |
| `scripts/generate_chaos_db.py` | Emits `data/chaos-db.json` from `Fable 3 Launcher/main.cpp` + the two lists, and syncs into the local viewer clone |
| `chaos-db.json` (repo root) | Generated lean index, at the location the hosted viewer probes |
| `details/fable3-launcher.json` (repo root) | Per-function detail chunk: IDA disassembly + callees/callers (regenerate after IDA renames) |

## Workflow

```powershell
# after marking functions matched / near-miss, regenerate + sync:
python chaos-viewer/scripts/generate_chaos_db.py

# run the viewer locally (from the clone):
cd D:\projects\hacking\Windows\chaos-viewer
npm run dev          # -> http://localhost:5173/
```

The treemap shows all 97 launcher functions; matched ones go green,
near-misses get badges and rank in "Nearly done", and the Prompt Builder
produces a matching task for any one.

The function universe is `Fable 3 Launcher/main.cpp` — add new stub
annotations there (`// name  (0x<start> - 0x<end>)`) and re-run the generator.

## Hosted viewer

The published data (`chaos-db.json` + `details/`) sits at the root of this
branch, which is what the hosted viewer probes. Open
https://tangosdev.github.io/chaos-viewer/ and enter:

- repo: `https://github.com/himdo/Fable-3-Decompiled`
- branch: `chaos-viewer-launcher`

Shareable link: `https://tangosdev.github.io/chaos-viewer/?repo=https://github.com/himdo/Fable-3-Decompiled&branch=chaos-viewer-launcher`

Push this branch (`git push -u origin chaos-viewer-launcher`) after each
regen so the hosted viewer sees the new data.
