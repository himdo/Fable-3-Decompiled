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
                            ReleaseGlobalObject (0x4017B0) — release the shared debug-handler pair

common/                     engine-wide memory / buffer helpers
  common.h
  common.cpp                LHAlloc (0x1A035C0, heap alloc),
                            LHFree (0x1A032C0, heap free),
                            SharedBufferAssign (0x403FE0, buffer assign),
                            SharedBufferFree (0x403820, buffer free),
                            InitSharedSlot (0x1A00C30) — shared-slot ctor (+ g_Addend counter)
                            ReleaseSharedSlot (0x1A01320) — shared-slot dtor (refcount + free)
                            GetLocaleEntry (0x6195C0) — per-LCID localized-string table entry lookup
                            GetLocalizedString (0x19FF570) — per-LCID localized-string getter
                            F3SharedBuffer struct

engine/                     game engine init + main run loop  [growing]
  engine.h                  public engine API (RunGame + the module headers below)
  engine.cpp                RunGame (0x615740) — implemented (run loop + thread)
  worker.h / worker.cpp     CWorkerWin32Ctor (0x18F7040) — main-worker ctor
                            (+ the live-slot counter g_Addend (0x2075C48) now lives in common/,
                             with InitSharedSlot / ReleaseSharedSlot, its primary users)
                            GetEngineTime (0x18A88E0) — seconds since engine start
                            CWorkerWin32_Dtor (0x55D8F0) / _DtorBody (0x55D930) — dtor
                            DisposeWorker (0x615880) — SEH dispose handler [BYTE-IDENTICAL]
                            GetWorker (0x18F6A40) / SetWorker (0x18F6FC0) / ReleaseWorkerPair (0x55DB10)
                            SetOnCreateDevice (0x18F6CA0) / SetOnResetDevice (0x18F6C50) [BYTE-IDENTICAL]
                            PumpWorker (0x18F8600) — one step of the worker state machine
                            ShutdownDevice (0x18F17B0) — device-shutdown (state 3) step
                            NvApiTeardownFlush (0x17D6510) — device teardown flush (NVAPI
                             interface 0x2EC50C2B, lazy-resolved via nvapi_QueryInterface)
                            NvApiTeardownUnload (0x17D1E60) — release the device library
                             (run the NVAPI teardown entry, clear the 178-entry
                             resolved-interface table, FreeLibrary(hLibModule))
                            (+ stub callees: sub_18F82E0 window, sub_18F8530 messages,
                             sub_18F0E00/sub_18F0C10/sub_18F1760 device teardown)
  manager.h / manager.cpp   CManagerWin32_Dtor (0x55D8B0) / _DtorBody (0x55D5F0) — dtor
                            CManagerWin32Ctor (0x18F7160) — CManagerWin32 ctor (48-byte object)
                            DisposeManager (0x615850) — SEH dispose handler [BYTE-IDENTICAL]
                            GetManager (0x18F8750) / SetManager (0x18F87E0) / ReleaseManagerPair (0x55D7C0)
  object_registry.h/.cpp    F3SehBlock / F3ObjReg types + registry critical section
                            ReleaseObjectPair (0x403220) — release a global object pair
                            SetDebugTrace (0x1A02E20) — register CDebugTraceFable (CS-protected)
                            SetDebugHandler (0x1A02DD0) — register CFableWindowsDebugHandler (CS-protected)
                            GetDebugHandlerPair — the shared CFableWindowsDebugHandler pair
  xlo_device.h / xlo_device.cpp
                            OnCreateDevice (0x6152F0) / OnResetDevice (0x615340) — XLO
                            device callbacks (+ stubbed XLiveOn* XDK imports)
  gameengine.h / gameengine.cpp
                            CGameEngineCtor (0x61C530) — construct the 356-byte game engine
                            CGameEngineSubCtor (0x623360) — ctor of the engine's +0xC8 sub-object
                            CGameVersionCtor (0x72DD10) — 92-byte CGameVersion sub-object
                            CGameVersionReset (0x72DC80) — reset CGameVersion's node lists
  gamethread.h / gamethread.cpp
                            GameThread (0x615380) — game thread entry point (implemented)
                            GetGameBuffer (0x619AA0) — getter for the 0x6C4-byte
                            game buffer (0x1DBD318) holding the shared-memory blob
                            HeapStressTest (0x18986E0) — engine heap sanity check
                            LHFreeHandler (0x401D20) — SEH cleanup handler (LHFree)
                            JmpLHFree (0x615870) — `jmp LHFree` thunk SEH handler
                            (+ GameThread's stub callees: sub_61BE00, sub_61AE70, ...)
  gamemain.h / gamemain.cpp
                            RunFrontEnd (0x61CB70) — the game start-up / frontend driver
                            (XNA sign-in, device + engine sub-objects, worker callbacks,
                            intro video, thread start) + its xlive/steam import stubs
                            and ~60 stubbed engine callees
  thread.cpp                SetThreadName (0x18C5840) — announce the thread name via a
                            first-chance SEH raise

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
| `DisposeManager` (0x615850) | 17 | **YES** | Byte-identical to `DisposeWorker` (same 17 bytes); disposes the 48-byte `CManagerWin32` via `vtable[0](obj,1)`. `__declspec(naked)` inline asm. |
| `SetOnCreateDevice` (0x18F6CA0) | 13 | **YES** | `__thiscall` + `retn 4` hybrid (worker in ECX, callback on the stack, callee cleans via `ret 4`). Pure register/stack ops, no absolute refs or relative calls. `__declspec(naked)` inline asm; the caller dispatches it through a `__thiscall` function pointer (`F3ThiscallSetFn`). |
| `OnCreateDevice` (0x6152F0) | 70 | no | Placement-dependent (relative calls to the `sub_18F01A0` caps getter, the `XLiveOnCreateDevice` IAT thunk, `__security_check_cookie`) and reads the original security-cookie + data globals (`dword_1E2EEF4`, `dword_1E2ED80`) at high VAs. The XDK `XLiveOnCreateDevice` import is stubbed for the PC build. |
| `SetOnResetDevice` (0x18F6C50) | 13 | **YES** | `__thiscall` + `retn 4` hybrid, the sibling of `SetOnCreateDevice` (worker in ECX, callback on the stack, callee cleans via `ret 4`). Stores the callback in the worker's `onResetDevice` field (+0x26C). `__declspec(naked)` inline asm; the caller dispatches it through a `__thiscall` function pointer (`F3ThiscallSetFn`). |
| `OnResetDevice` (0x615340) | 61 | no | Placement-dependent (relative calls to the `sub_18F01A0` caps getter, the `XLiveOnResetDevice` IAT thunk, `__security_check_cookie`) and reads the original security-cookie at its high VA. Copies the 56-byte device-caps block (`dword_1E2ED80`) onto the stack and forwards it to the stubbed XDK `XLiveOnResetDevice` import. |
| `GetEngineTime` (0x18A88E0) | 99 | no | Placement-independent, but refs the `QueryPerformanceCounter` IAT slot (0x01AD50C8) and start globals (0x020B5FC0/0x020B5FB8) at original high VAs not present in the build. |
| `LHFree` (0x1A032C0) | 11 | no | Placement-independent, but refs the heap-table pointer global (0x02075B24) at the original high VA. |
| `LHFreeHandler` (0x401D20) | 11 | no | Byte-identical to `LHFree`; placement-independent, but refs the heap table (0x02075B24) at the original high VA. |
| `JmpLHFree` (0x615870) | 5 | no | Placement-dependent: the near `jmp LHFree` is rebased to the build's `LHFree` (the original displacement targeted 0x1A032C0). `__declspec(naked)` inline asm; the SEH release code pushes the object pointer and calls the handler, so the tail jump passes it straight into `LHFree`'s (cdecl) parameter. |
| `SetThreadName` (0x18C5840) | 131 | no | Placement-dependent: refs the compiler-generated SEH scope table (`stru_1C7F160`) + EH4 scope handler (0x152ECE0), the original /GS security cookie, and the `RaiseException` IAT slot (0x01AD51B0) at their original high VAs. |
| `SharedBufferFree` (0x403820) | 58 | no | Placement-independent, but refs the heap table (0x02075B24) at the original high VA. |
| `ReleaseGlobalObject` (0x4017B0) | 71 | no | Placement-independent, but refs the heap table (0x02075B24), `lpAddend` (0x020B6008), and the `InterlockedDecrement` IAT slot (0x01AD52FC) at original high VAs. |
| `InitSharedSlot` (0x1A00C30) | 24 | no | Placement-independent (no relative calls / long jumps), but refs the live-slot counter `g_Addend` (0x02075C48) and the `InterlockedIncrement` IAT slot (0x01AD52F8) at original high VAs. |
| `ReleaseSharedSlot` (0x1A01320) | 61 | no | Placement-dependent: relative calls to the `InterlockedDecrement` IAT slot and to `SharedBufferFree` (0x403820); refs the live-slot counter `g_Addend` (0x02075C48) at its original high VA. The shared-slot dtor (counterpart to `InitSharedSlot`). |
| `GetWorker` (0x18F6A40) | 6 | no | Placement-independent, but refs the high-VA global `dword_20FC8F0` (0x020FC8F0) not present in the build. |
| `GetManager` (0x18F8750) | 6 | no | Placement-independent, but refs the high-VA global `dword_20FC8C4` (0x020FC8C4) not present in the build. |
| `CManagerWin32Ctor` (0x18F7160) | 46 | no | Placement-independent, but refs the two class vftables (CManagerBase 0x01BE7340 / CManagerWin32 0x01BE737C) and the `InitializeCriticalSection` IAT slot (0x01AD531C) at original high VAs not present in the build. |
| `GetGameBuffer` (0x619AA0) | 6 | no | Placement-independent (no calls / relative jumps), but refs the game buffer (0x01DBD318) at its original high VA not present in the build. |
| `GetLocalizedString` (0x19FF570) | 15 | no | Placement-independent (no calls / relative jumps), but refs the default "Path" wide-string global (0x01BDFD28) at its original high VA not present in the build. |
| `GetLocaleEntry` (0x6195C0) | 656 | no | Placement-dependent: an indirect IAT call to `GetUserDefaultLCID` (0x01AD...), `mov eax, offset dword_20B6078`, MSVC switch jump tables, and the .data locale/key tables (0x01DBD120..0x01DBD2F4) + not-found default table (0x020B6078) at their original high VAs not present in the build. |
| `PumpWorker` (0x18F8600) | 169 | no | Placement-dependent (relative calls to `sub_18F82E0` / `sub_18F8530` / `ShutdownDevice`) and refs the `EnterCriticalSection` / `LeaveCriticalSection` / `SetEvent` / `WaitForSingleObjectEx` / `WaitMessage` IAT slots at their original high VAs. |
| `ShutdownDevice` (0x18F17B0) | 84 | no | Placement-dependent (relative calls to `NvApiTeardownFlush` / `NvApiTeardownUnload` / `sub_18F0E00` / `sub_18F0C10` / `sub_18F1760`) and refs the device globals `byte_2075DDB` (0x02075DDB), `dword_1E2ED70`/`dword_1E2ED6C`/`dword_1E2EEF8` (0x01E2EDxx/0x01E2EEF8) at their original high VAs. Worker device-shutdown step (PumpWorker state 3); the three remaining per-slot teardown callees are stubbed. |
| `NvApiTeardownUnload` (0x17D1E60) | 209 | no | Placement-dependent (long jumps to the common exit) and refs `hLibModule` (0x01E2C704), the nvapi globals `dword_1E2C708`/`dword_1E2C70C`/`dword_1E2C710`, the resolved-interface table (0x01D941F4), and the `FreeLibrary` IAT slot (0x01AD5154) at their original high VAs. Release the device library: run the NVAPI teardown entry (interface 0xD22BDD7E), clear all 178 resolved-interface slots, `FreeLibrary(hLibModule)`, and clear the resolver + before/after hooks (returns -4/-3/-1/0). |
| `NvApiTeardownFlush` (0x17D6510) | 175 | no | Placement-independent body (no relative calls / long jumps), but refs the nvapi.dll interface globals `dword_1E2C708`/`dword_1E2C70C`/`dword_1E2C710` and the interface-table slot `dword_1D94644`/`byte_1D94648` at their original high VAs. Device teardown flush: lazily resolves NVAPI interface 0x2EC50C2B via `nvapi_QueryInterface`, runs the entry bracketed by the before/after hooks, and returns its result (-3 if unavailable). |
| `FrameworkInit`, `FrameworkShutdown`, `ComputeExeDirectory`, `LHAlloc`, `SharedBufferAssign`, `InitSharedSlot` (0x1A00C30), `OutOfMemory`, `RunGame`, `GameThread` (0x615380), `CWorkerWin32Ctor`, `CWorkerWin32_Dtor`, `CWorkerWin32_DtorBody`, `CManagerWin32_Dtor` (0x55D8B0), `CManagerWin32_DtorBody` (0x55D5F0), `SetSaveGamePath`, `SetWorker` (0x18F6FC0), `ReleaseWorkerPair` (0x55DB10), `SetManager` (0x18F87E0), `ReleaseManagerPair` (0x55D7C0), `SetDebugTrace` (0x1A02E20), `SetDebugHandler` (0x1A02DD0), `ReleaseObjectPair` (0x403220), `HeapStressTest` (0x18986E0), `CGameEngineCtor` (0x61C530), `CGameEngineSubCtor` (0x623360), `CGameVersionCtor` (0x72DD10), `CGameVersionReset` (0x72DC80), `RunFrontEnd` (0x61CB70) | — | no | Placement-dependent (relative calls to callees/CRT). `CGameEngineCtor` also writes the high-VA global `dword_1DBD9DC`. `RunFrontEnd` is the 9.3 KB frontend start-up driver — hundreds of relative calls to engine callees, the xlive/steam import stubs, and Win32, plus references to many high-VA data globals. |

To byte-match the placement-independent functions that reference IAT/data
globals, those globals must be placed at their original VAs (a PE
post-processor that adds high-RVA sections + reference patching, growing the
image toward the original's ~30 MB). Deferred until the module grows enough
that it is worth doing.

## Plan — modules still to build out

| Module    | Entry (IDA)          | What to add |
|-----------|----------------------|-------------|
| `engine/` | `RunGame` (0x615740), `GameThread` (0x615380), `CWorkerWin32Ctor` (0x18F7040), `CManagerWin32Ctor` (0x18F7160), `SetWorker`/`GetWorker`/`ReleaseWorkerPair` (0x18F6FC0/0x18F6A40/0x55DB10), `SetManager`/`GetManager`/`ReleaseManagerPair` (0x18F87E0/0x18F8750/0x55D7C0), `SetDebugTrace` (0x1A02E20), `SetDebugHandler` (0x1A02DD0), `ReleaseObjectPair` (0x403220), `HeapStressTest` (0x18986E0), `DisposeWorker` (0x615880), `DisposeManager` (0x615850), `CWorkerWin32_Dtor`/`CWorkerWin32_DtorBody` (0x55D8F0/0x55D930), `CManagerWin32_Dtor`/`CManagerWin32_DtorBody` (0x55D8B0/0x55D5F0), `CGameEngineCtor` (0x61C530), `CGameEngineSubCtor` (0x623360), `CGameVersionCtor` (0x72DD10), `CGameVersionReset` (0x72DC80), `SetOnCreateDevice` (0x18F6CA0), `OnCreateDevice` (0x6152F0), `SetOnResetDevice` (0x18F6C50), `OnResetDevice` (0x615340), `LHFreeHandler` (0x401D20), `JmpLHFree` (0x615870), `SetThreadName` (0x18C5840), `PumpWorker` (0x18F8600), `ShutdownDevice` (0x18F17B0), `NvApiTeardownFlush` (0x17D6510), `NvApiTeardownUnload` (0x17D1E60), `GetGameBuffer` (0x619AA0), `RunFrontEnd` (0x61CB70) | (done) run loop + game thread + main-worker ctor + worker & manager pairs + CDebugTraceFable + CFableWindowsDebugHandler registration (CS-protected) + heap sanity check + SEH dispose path + 356-byte game-engine ctor (with CGameVersion sub-object) + worker on-create-device callback setter (byte-identical) + both XLO device callbacks (on-create and on-reset, setters byte-identical) + CDebugTraceFable SEH handler thunk (JmpLHFree) + worker pump step (PumpWorker; window / message / device-shutdown callees stubbed) + frontend start-up driver (RunFrontEnd, 0x61CB70; XNA sign-in / device / engine sub-objects / worker callbacks / intro video / thread start, with ~60 stubbed engine callees and xlive/steam import stubs) + release the device library (NvApiTeardownUnload, 0x17D1E60); next: fill in the remaining stub callees (sub_18F82E0, sub_18F8530, sub_18F0E00/sub_18F0C10/sub_18F1760, ...) |
| `save/`   | `SetSaveGamePath` (0x1A019B0) | (done) store the save path refcounted into `dword_1DBD06C` + getter; next: the actual load logic |
| `framework/` | `ComputeExeDirectory` (0x1A02870), `ReleaseGlobalObject` (0x4017B0) | (done) exe-directory compute + `ExeDirectory()` getter + global shared-object release; the framework init/shutdown pair is complete |
| `common/` | `LHAlloc` (0x1A035C0), `LHFree` (0x1A032C0), `SharedBufferAssign` (0x403FE0), `SharedBufferFree` (0x403820), `OutOfMemory` (0x1A034F0), `GetLocaleEntry` (0x6195C0), `GetLocalizedString` (0x19FF570) | (done) heap alloc/free + `F3SharedBuffer` counted buffer + OOM report handler + per-LCID localized-string table lookup (GetLocaleEntry) + localized-string getter + shared-slot init/destroy (InitSharedSlot 0x1A00C30, ReleaseSharedSlot 0x1A01320); next: cross-module globals `byte_1DBD01B`, `byte_1C86BD9` |
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
