# Fable 3 (Fable3.exe) — decompilation

Functional reconstruction of `Fable3.exe`, built to behave the same as the
original. Where a function can be made **byte-identical** to the original
without padding, it is (labeled `[BYTE-IDENTICAL]` in the source and tracked in
the chaos-viewer atlas); otherwise it carries a `[NOT byte-identical]` note
explaining why (see **Byte-identity status** below). Organized into **module
folders** so it can be filled in one subsystem at a time.

- **Toolchain:** VS2008 SP1 (cl 15.00.30729 / link 9.00.30729), Win SDK 7.1,
  flags `/MT /GL /O2 /Ob2`, image base `0x400000`, GUI subsystem.
- **Build:** `build_vs2008.bat` → `Release/Win32/Fable3.exe`.
- **Ground truth:** IDA database `../Fable3.exe.i64` (symbols stripped). Every
  function keeps its IDA address-range annotation
  (`// name  (0x<start> - 0x<end>)`) so it maps back to the original binary.

## Layout

```
main.cpp                    application entry point (WinMain)
build_vs2008.bat            compiles + links every module .cpp

framework/                  refcounted framework init / shutdown
  framework.h
  framework.cpp             FrameworkInit  (0x1A02B50)
                            FrameworkShutdown (0x1A02E70)
                            ComputeExeDirectory (0x1A02870) + ExeDirectory()
                            ReleaseGlobalObject (0x4017B0)

common/                     engine-wide memory / buffer helpers
  common.h
  common.cpp                LHAlloc (heap alloc), LHFree (heap free),
                            SharedBufferAssign (buffer assign),
                            SharedBufferFree (buffer free), F3SharedBuffer struct

engine/                     game engine init + main run loop  [growing]
  engine.h
  engine.cpp                RunGame (0x615740) — implemented (run loop + thread)
                            CWorkerWin32Ctor (0x18F7040) — main-worker ctor
                            SetWorker (0x18F6FC0) — install current worker + SEH block
                            GetWorker (0x18F6A40) — current-worker getter
                            ReleaseWorkerPair (0x55DB10) — release a worker pair
                            DisposeWorker (0x615880) — SEH dispose handler
                            CWorkerWin32_Dtor (0x55D8F0) — worker deleting destructor
                            CWorkerWin32_DtorBody (0x55D930) — worker dtor body
                            (+ stub callees: sub_615380, sub_18F6CA0, sub_18F6C50, sub_18F8600, ...)

save/                       save-game loading  [growing]
  save.h
  save.cpp                  SetSaveGamePath (0x1A019B0) — implemented (refcounted setter)
```

## Conventions

- One folder per subsystem, each with a `*.h` (public declarations) and a
  `*.cpp` (implementation). `main.cpp` only holds `WinMain` and includes the
  module headers.
- Public functions are declared in the module header and called by name;
  module-private helpers are `static` in the `.cpp`.
- Functions are named after their IDA symbol once identified (e.g.
  `sub_1A02B50` → `FrameworkInit`); unknown ones keep the `sub_XXXXXX` name.
- Stubs carry the IDA decompile (or a summary) in a comment block marked
  `[STUB]` so the real body can be slotted in later.
- To add a module: create `folder/folder.h` + `folder/folder.cpp`, then add
  `..\..\folder\folder.cpp` to `SRCS` and the `.obj` to the `link` line in
  `build_vs2008.bat`.

## Byte-identity status

A function byte-matches the original only if it contains **no** relative
`E8` call, `E9` jmp, or `0F 8x` long conditional jump — those depend on the
link-time layout. Short jumps and absolute-address memory ops are placement-
independent. The current state of the implemented functions:

| Function | Bytes | Byte-identical? | Why |
|----------|------|-----------------|-----|
| `DisposeWorker` (0x615880) | 17 | **YES** | Pure stack/register ops, no absolute refs or relative calls. `__declspec(naked)` inline asm. |
| `GetEngineTime` (0x18A88E0) | 99 | no | Placement-independent, but refs the `QueryPerformanceCounter` IAT slot (0x01AD50C8) and start globals (0x020B5FC0/0x020B5FB8) at original high VAs not present in the build. |
| `SharedBufferFree` (0x403820) | 58 | no | Placement-independent, but refs the heap table (0x02075B24) at the original high VA. |
| `ReleaseGlobalObject` (0x4017B0) | 71 | no | Placement-independent, but refs the heap table (0x02075B24), `lpAddend` (0x020B6008), and the `InterlockedDecrement` IAT slot (0x01AD52FC) at original high VAs. |
| `GetWorker` (0x18F6A40) | 6 | no | Placement-independent, but refs the high-VA global `dword_20FC8F0` (0x020FC8F0) not present in the build. |
| `FrameworkInit`, `FrameworkShutdown`, `ComputeExeDirectory`, `LHAlloc`, `SharedBufferAssign`, `OutOfMemory`, `RunGame`, `CWorkerWin32Ctor`, `CWorkerWin32_Dtor`, `CWorkerWin32_DtorBody`, `SetSaveGamePath`, `SetWorker` (0x18F6FC0), `ReleaseWorkerPair` (0x55DB10) | — | no | Placement-dependent (relative calls to callees/CRT). |

To byte-match the placement-independent functions that reference IAT/data
globals, those globals must be placed at their original VAs (a PE
post-processor that adds high-RVA sections + reference patching, growing the
image toward the original's ~30 MB). Deferred until the module grows enough
that it is worth doing.

## Plan — modules still to build out

| Module    | Entry (IDA)          | What to add |
|-----------|----------------------|-------------|
| `engine/` | `RunGame` (0x615740), `CWorkerWin32Ctor` (0x18F7040), `SetWorker` (0x18F6FC0), `GetWorker` (0x18F6A40), `ReleaseWorkerPair` (0x55DB10), `DisposeWorker` (0x615880), `CWorkerWin32_Dtor`/`CWorkerWin32_DtorBody` (0x55D8F0/0x55D930) | (done) run loop + main-worker ctor + current-worker pair (install/get/release with SEH refcount) + SEH dispose path (worker deleting destructor + dtor body); next: fill in the remaining stub callees (sub_18F6CA0, sub_18F6C50, sub_18F8600, sub_615380) |
| `save/`   | `SetSaveGamePath` (0x1A019B0) | (done) store the save path refcounted into `dword_1DBD06C` + getter; next: the actual load logic |
| `framework/` | `ComputeExeDirectory` (0x1A02870), `ReleaseGlobalObject` (0x4017B0) | (done) exe-directory compute + `ExeDirectory()` getter + global shared-object release; the framework init/shutdown pair is complete |
| `common/` | `LHAlloc`, `LHFree`, `SharedBufferAssign`, `SharedBufferFree`, `OutOfMemory` | (done) heap alloc/free + `F3SharedBuffer` counted buffer + OOM report handler; next: cross-module globals `byte_1DBD01B`, `byte_1C86BD9` |
| (future)  | render / audio / input / network | as their code is decompiled |

## Original entry chain (for reference)

```
entry 0x1523B39:   call ___security_init_cookie; jmp ___tmainCRTStartup
0x15239BB:         __tmainCRTStartup  ->  call _WinMain@16
0x005CF170:        WinMain  (369 bytes)
```

`WinMain`: single-instance mutex `Local\FableIII` (focus the existing window
on a second launch) → `FrameworkInit` → parse `-loadsavegame <path>` →
`RunGame` (run the game) → `FrameworkShutdown` → return the exit code.
