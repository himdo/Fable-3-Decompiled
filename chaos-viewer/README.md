# Chaos Viewer — Fable 3 Decomp

Progress atlas for the byte-identical decompilation of Fable 3, powered by
[Chaos Viewer](https://github.com/tangosdev/chaos-viewer). It tracks **two
modules**:

| Module | Binary | Universe source | Functions |
|---|---|---|---|
| `fable3-launcher` | `FableLauncher.exe` | `fable3-launcher/functions.json` (snapshot of the 97 launcher functions) | 97 |
| `fable3-game` | `Fable3.exe` | `fable3-game/functions.json` (IDA export of the `.text` functions) | 179,837 |

## Files

| File | Purpose |
|---|---|
| `project.config.json` | Project branding + prompt text embedded into the atlas (compiler line, verify command, rules) |
| `modules.json` | **Module registry** — one entry per module: `label`, `universe` (`{"type":"json","path":...}` or `{"type":"main_cpp","path":...}`), `matched`, `nearmiss`, `details` |
| `<module>/functions.json` | The module's function universe: `[{"name","addr","size"}]` (addr/size as hex strings), sorted by address |
| `<module>/matched.json` | Functions verified byte-identical: `[]` or `[{"name","author","srcPath","matchProvenance"}]` |
| `<module>/nearmiss.json` | Close-but-not-matched functions: `[{"name","div","cat","floor"?}]` |
| `scripts/generate_chaos_db.py` | Reads `modules.json`, merges every module, emits the repo-root `chaos-db.json`, and syncs into the local viewer clone |
| `scripts/export_game_functions.py` | Run **inside IDA** (with Fable3.exe open) to (re)export `fable3-game/functions.json` — re-run after IDA renames |
| `chaos-db.json` (repo root) | Generated lean index, at the location the hosted viewer probes |
| `details/<module>.json` (repo root) | Per-function detail chunk: IDA disassembly + callees/callers |

## Workflow

```powershell
# after marking functions matched / near-miss, regenerate + sync:
python chaos-viewer/scripts/generate_chaos_db.py

# (re)export the game function list from IDA (File > Script file):
python chaos-viewer/scripts/export_game_functions.py

# run the viewer locally (from the clone):
cd D:\projects\hacking\Windows\chaos-viewer
npm run dev          # -> http://localhost:5173/
```

The treemap shows every function across both modules; matched ones go green,
near-misses get badges and rank in "Nearly done", and the Prompt Builder
produces a matching task for any one.

> The atlas is large (~180k functions, ~23 MB `chaos-db.json`) because it
> tracks the whole game `.text`. For hosting, consider the orphan `chaos-data`
> branch described in the Chaos Viewer `ADAPTING.md` to keep the data out of
> main history.

## Hosted viewer

The published data (`chaos-db.json` + `details/`) sits at the root of this
branch, which is what the hosted viewer probes. Open
https://tangosdev.github.io/chaos-viewer/ and enter:

- repo: `https://github.com/himdo/Fable-3-Decompiled`
- branch: `chaos-viewer-launcher`

Shareable link: `https://tangosdev.github.io/chaos-viewer/?repo=https://github.com/himdo/Fable-3-Decompiled&branch=chaos-viewer-launcher`

Push this branch (`git push -u origin chaos-viewer-launcher`) after each
regen so the hosted viewer sees the new data.
