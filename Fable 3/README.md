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
                            PumpWorkerMessages (0x18F8530) — drain the worker's message queue (state 2)
                            EnsureWorkerWindow (0x18F82E0) — create/restore the worker window
                             (state 1 step): CreateWorkerWindow (0x18F7AE0) + WorkerWndProc
                             (0x18F7A70) + SetHWndParent (0x18F1AE0) + InitDisplayMode
                             (0x55D850) + ResizeWorkerWindow (0x18F6A70); display-mode
                             callees TrySetDisplayMode (0x18F28C0) / SetDisplayMode (0x18F2B30)
                             / ApplyDisplaySettings (0x18F7E60) stubbed
                            ShutdownDevice (0x18F17B0) — device-shutdown (state 3) step
                            NvApiTeardownFlush (0x17D6510) — device teardown flush (NVAPI
                             interface 0x2EC50C2B, lazy-resolved via nvapi_QueryInterface)
                            NvApiTeardownUnload (0x17D1E60) — release the device library
                             (run the NVAPI teardown entry, clear the 178-entry
                             resolved-interface table, FreeLibrary(hLibModule))
                            TeardownDeviceSlot (0x18F0E00) — per-slot device teardown
                             (notify device vtable[37] + release the sub-record objects)
                            TeardownDeviceBase (0x18F0C10) — per-slot base-device teardown
                             (notify device vtable[39] + release the base-device object,
                             tail-calls GetSlotClientRect (0x18F0710): store the device
                             client rect {x, y, x+w, y+h} in the slot row)
                            DestroyDevice (0x18F1760) — destroy the current device
                             (free the flush request + release device objects + vtable[2])
                            ReleaseDeviceObjects (0x18F1220) — release the device's
                             vtable objects + object-list container (0x20FC988)
                            FreeObjectListContainer (0x18CB3D0) / F3ListRemoveNode
                             (0x5A4840) — free the container's two vtable-object lists
                            FreeDeviceFlushRequest (0x5639E0) — free the 20-byte
                             device flush-request object (dword_2075F00)
                            NvApiDestroyObject (0x17D6740) — destroy an NVAPI object
                             (interface 0x3A153134, lazy-resolved via nvapi_QueryInterface)
                            (+ stub callees: HandleDisplayModeChange (0x18F8060), sub_18F0850)
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
| `PumpWorker` (0x18F8600) | 169 | no | Placement-dependent (relative calls to `EnsureWorkerWindow` / `PumpWorkerMessages` / `ShutdownDevice`) and refs the `EnterCriticalSection` / `LeaveCriticalSection` / `SetEvent` / `WaitForSingleObjectEx` / `WaitMessage` IAT slots at their original high VAs. |
| `PumpWorkerMessages` (0x18F8530) | 203 | no | Placement-dependent (relative calls to `HandleDisplayModeChange` and `__security_check_cookie`) and refs the `EnterCriticalSection` / `LeaveCriticalSection` / `PeekMessageA` / `TranslateMessage` / `DispatchMessageA` IAT slots at their original high VAs. Worker message pump (state 2): peeks the worker window's queue outside the CS, runs the pre-translate hook (+0x270, XLive) on each message, and if unhandled, translates it, dispatches it outside the CS, and (if the display-mode-pending flag +0x281 is set) runs `HandleDisplayModeChange` (stubbed) under a nested CS entry; exits holding the worker CS. |
| `EnsureWorkerWindow` (0x18F82E0) | 589 | no | Placement-dependent (relative calls to `CreateWorkerWindow` / `SetHWndParent` / `InitDisplayMode` / `TrySetDisplayMode` / `SetDisplayMode` / `ResizeWorkerWindow` / `ApplyDisplaySettings` and the `SetWindowLongA` / `SetWindowPos` / `MessageBoxW` / `PostMessageA` / `ShowWindow` / `UpdateWindow` IAT slots) and refs the high-VA locale-key globals `dword_20FC8EC` / `dword_20FC8E8`. Creates the worker window (state 1) then runs the display-mode dance; the deep display-mode callees are stubbed. |
| `ShutdownDevice` (0x18F17B0) | 84 | no | Placement-dependent (relative calls to `NvApiTeardownFlush` / `NvApiTeardownUnload` / `TeardownDeviceSlot` / `TeardownDeviceBase` / `DestroyDevice`) and refs the device globals `byte_2075DDB` (0x02075DDB), `dword_1E2ED70`/`dword_1E2ED6C`/`dword_1E2EEF8` (0x01E2EDxx/0x01E2EEF8) at their original high VAs. Worker device-shutdown step (PumpWorker state 3); the per-slot teardown callees (`TeardownDeviceSlot`, `TeardownDeviceBase` + `GetSlotClientRect`) and the final device teardown (`DestroyDevice` + `ReleaseDeviceObjects` + `FreeObjectListContainer`/`F3ListRemoveNode` + `FreeDeviceFlushRequest` + `NvApiDestroyObject`) are implemented. |
| `TeardownDeviceSlot` (0x18F0E00) | 155 | no | Placement-dependent (relative fall-through to `sub_18F0850`) and refs the slot index `dword_2075F10` (0x02075F10), the device object `dword_1E2EEF4` (0x01E2EEF4), and the per-slot record table `0x20FCA68` at their original high VAs. Per-slot device teardown: for the current slot it walks three sub-records (row offsets 8/16/24), notifies the device object through its vtable[37] (offset 0x94) as `(device, n, 0)`, releases each sub-record object (vtable[2]) and clears its two DWORDs; falls through to `sub_18F0850` (stubbed) if the slot trailer flag (row offset 33) is set. |
| `TeardownDeviceBase` (0x18F0C10) | 92 | no | Placement-dependent (relative tail-jmp to `GetSlotClientRect`) and refs the slot index `dword_2075F10` (0x02075F10), the device object `dword_1E2EEF4` (0x01E2EEF4), and the per-slot base-device table `0x20FCAE8` at their original high VAs. Per-slot base-device teardown: for the current slot, if the base-device row's object (row DWORD 1) is set, notifies the device object through its vtable[39] (offset 0x9C) as `(device, 0)`, re-reads the slot, then releases the base object (vtable[2]) and clears row DWORDs 1 and 2; tail-calls `GetSlotClientRect()`. |
| `DestroyDevice` (0x18F1760) | 65 | no | Placement-dependent (relative calls to `FreeDeviceFlushRequest` / `ReleaseDeviceObjects`) and refs the device object `dword_1E2EEF4` (0x01E2EEF4) and the flush-request object `dword_2075F00` (0x02075F00) at their original high VAs. Final device teardown: if the device object is set, frees the pending flush request (`FreeDeviceFlushRequest` + clear `dword_2075F00`), runs `ReleaseDeviceObjects()`, destroys the device through its vtable[2] (offset 0x08), and clears the pointer. |
| `ReleaseDeviceObjects` (0x18F1220) | 230 | no | Placement-dependent (relative calls) and refs the device objects `dword_2075F14`/`dword_1DB1228`/`dword_1DB1264`/`dword_1DB1244`, the slot index `dword_2075F10`, the per-slot tables `0x20FCA68`/`0x20FCAE8`, and the object-list container `0x20FC988` at their original high VAs. Releases (vtable[2]) and clears the named device objects, zeroes the container's first DWORD, then walks the current slot's four `0x20FCA68` sub-records (row bytes 0/32/64/96) and base-device `0x20FCAE8` row, releasing + clearing each occupied object (re-reading the slot after each release); finally `FreeObjectListContainer()`. |
| `FreeObjectListContainer` (0x18CB3D0) | 318 | no | Placement-dependent (relative call to `F3ListRemoveNode`). Frees both vtable-object lists of the 64-byte container (`0x20FC988`): for the +0x20 list then the +0x04 list, while the count is non-zero, releases the last real node's wrapped object (vtable[2]) and removes + frees that node. |
| `F3ListRemoveNode` (0x5A4840) | 133 | no | Placement-independent, but refs the heap table (0x02075B24) at the original high VA. Unlinks + `LHFree`s one list node (skipped for the sentinel) and decrements the list count; out receives `{ list->head, head }`. |
| `FreeDeviceFlushRequest` (0x5639E0) | 40 | no | Placement-dependent (relative call to `NvApiDestroyObject`) and refs the heap table (0x02075B24) at the original high VA. Frees the 20-byte device flush-request object: releases its wrapped NVAPI resource (offset +12) via `NvApiDestroyObject` + clears the field, then `LHFree`s the object and returns it. |
| `NvApiDestroyObject` (0x17D6740) | 182 | no | Placement-independent body, but refs the nvapi.dll interface globals `dword_1E2C708`/`dword_1E2C70C`/`dword_1E2C710` and the interface-table slot `dword_1D94668`/`byte_1D9466C` at their original high VAs. Destroy an NVAPI object: lazily resolves NVAPI interface 0x3A153134, runs the entry with the object bracketed by the before/after hooks, and returns its result (-3 if unavailable). |
| `GetSlotClientRect` (0x18F0710) | 117 | no | Refs the device object `dword_1E2EEF4` (0x01E2EEF4), the slot index `dword_2075F10` (0x02075F10), the per-slot client-rect table `0x20FCB08`, and the `__security_check_cookie` IAT slot at their original high VAs. If the device object is set, queries it through its vtable[48] (offset 0xC0, stdcall) into a 6-DWORD out-buffer and stores `{x, y, x + w, y + h}` in the current slot's 176-byte rect row (DWORDs 0-3); returns the slot byte offset (176 * slot), or 0 when the device object is not set. |
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
| `engine/` | `RunGame` (0x615740), `GameThread` (0x615380), `CWorkerWin32Ctor` (0x18F7040), `CManagerWin32Ctor` (0x18F7160), `SetWorker`/`GetWorker`/`ReleaseWorkerPair` (0x18F6FC0/0x18F6A40/0x55DB10), `SetManager`/`GetManager`/`ReleaseManagerPair` (0x18F87E0/0x18F8750/0x55D7C0), `SetDebugTrace` (0x1A02E20), `SetDebugHandler` (0x1A02DD0), `ReleaseObjectPair` (0x403220), `HeapStressTest` (0x18986E0), `DisposeWorker` (0x615880), `DisposeManager` (0x615850), `CWorkerWin32_Dtor`/`CWorkerWin32_DtorBody` (0x55D8F0/0x55D930), `CManagerWin32_Dtor`/`CManagerWin32_DtorBody` (0x55D8B0/0x55D5F0), `CGameEngineCtor` (0x61C530), `CGameEngineSubCtor` (0x623360), `CGameVersionCtor` (0x72DD10), `CGameVersionReset` (0x72DC80), `SetOnCreateDevice` (0x18F6CA0), `OnCreateDevice` (0x6152F0), `SetOnResetDevice` (0x18F6C50), `OnResetDevice` (0x615340), `LHFreeHandler` (0x401D20), `JmpLHFree` (0x615870), `SetThreadName` (0x18C5840), `PumpWorker` (0x18F8600), `PumpWorkerMessages` (0x18F8530), `EnsureWorkerWindow` (0x18F82E0), `ShutdownDevice` (0x18F17B0), `NvApiTeardownFlush` (0x17D6510), `NvApiTeardownUnload` (0x17D1E60), `TeardownDeviceSlot` (0x18F0E00), `TeardownDeviceBase` (0x18F0C10), `GetSlotClientRect` (0x18F0710), `DestroyDevice` (0x18F1760), `ReleaseDeviceObjects` (0x18F1220), `FreeObjectListContainer` (0x18CB3D0), `F3ListRemoveNode` (0x5A4840), `FreeDeviceFlushRequest` (0x5639E0), `NvApiDestroyObject` (0x17D6740), `GetGameBuffer` (0x619AA0), `RunFrontEnd` (0x61CB70) | (done) run loop + game thread + main-worker ctor + worker & manager pairs + CDebugTraceFable + CFableWindowsDebugHandler registration (CS-protected) + heap sanity check + SEH dispose path + 356-byte game-engine ctor (with CGameVersion sub-object) + worker on-create-device callback setter (byte-identical) + both XLO device callbacks (on-create and on-reset, setters byte-identical) + CDebugTraceFable SEH handler thunk (JmpLHFree) + worker pump step (PumpWorker; device-shutdown callee stubbed at that time) + worker message pump (PumpWorkerMessages, 0x18F8530; pre-translate hook (XLive, +0x270) + display-mode-pending flag (+0x281); display-mode change handler HandleDisplayModeChange, 0x18F8060, stubbed) + worker window create/restore step (EnsureWorkerWindow, 0x18F82E0; window class / wndproc / parent / display-mode-request init / resize implemented, deep display-mode callees stubbed) + frontend start-up driver (RunFrontEnd, 0x61CB70; XNA sign-in / device / engine sub-objects / worker callbacks / intro video / thread start, with ~60 stubbed engine callees and xlive/steam import stubs) + release the device library (NvApiTeardownUnload, 0x17D1E60) + per-slot device teardown (TeardownDeviceSlot, 0x18F0E00; notifies device vtable[37] + releases the sub-record objects, falls through to sub_18F0850) + per-slot base-device teardown (TeardownDeviceBase, 0x18F0C10; notifies device vtable[39] + releases the base-device object, tail-calls GetSlotClientRect, 0x18F0710; stores the device client rect {x, y, x+w, y+h} in the slot row) + final device teardown (DestroyDevice, 0x18F1760; frees the flush request (FreeDeviceFlushRequest, 0x5639E0; wraps NvApiDestroyObject, 0x17D6740, NVAPI interface 0x3A153134), releases the device's vtable objects + per-slot tables (ReleaseDeviceObjects, 0x18F1220), and frees the 0x20FC988 object-list container (FreeObjectListContainer, 0x18CB3D0 + F3ListRemoveNode, 0x5A4840)); next: fill in the remaining stub callees (sub_18F0850, HandleDisplayModeChange, ...) |
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
