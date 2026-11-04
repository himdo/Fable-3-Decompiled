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
                            SharedBufferCreate (0x1A01360) — allocate + fill a counted buffer from (src, size)
                            InitSharedSlot (0x1A00C30) — shared-slot ctor (+ g_Addend counter)
                            ReleaseSharedSlot (0x1A01320) — shared-slot dtor (refcount + free)
                            SetSharedSlot (0x1A01CC0) — general shared-slot setter (thiscall, retn 8)
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
                            InitEngineTime (0x18A89A0) — one-time QPC start-counter /
                            frequency capture (globals 0x20B5FC0 / 0x20B5FB8 / flag 0x2123048)
                            CWorkerWin32_Dtor (0x55D8F0) / _DtorBody (0x55D930) — dtor
                            DisposeWorker (0x615880) — SEH dispose handler [BYTE-IDENTICAL]
                            GetWorker (0x18F6A40) / SetWorker (0x18F6FC0) / ReleaseWorkerPair (0x55DB10)
                            SetOnCreateDevice (0x18F6CA0) / SetOnResetDevice (0x18F6C50) [BYTE-IDENTICAL]
                            SetPreTranslate (0x18F6F20) — pre-translate hook setter (CS-protected)
                            PumpWorker (0x18F8600) — one step of the worker state machine
                            PumpWorkerMessages (0x18F8530) — drain the worker's message queue (state 2)
                            EnsureWorkerWindow (0x18F82E0) — create/restore the worker window
                             (state 1 step): CreateWorkerWindow (0x18F7AE0) + WorkerWndProc
                             (0x18F7A70) + SetHWndParent (0x18F1AE0) + InitDisplayMode
                             (+ DispatchWorkerMessage (0x18F7460) — the main-window
                               message handler WorkerWndProc forwards to in pump state 2;
                               WM_CLOSE saves settings then ExitProcess)
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
                            IsDisplayReady (0x18F2850) — is the XLO display ready to
                             present? (device CS 0x20FC948 + vtable[3] status query;
                             on 0x88760869 device-lost, resets via ResetXloDevice,
                             0x18F2310 [STUB])
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
                            + the current-directory path cluster: the CWD global
                              (g_cwdBlock 0x2122DE4, wide→ANSI helpers), the path
                              resolver (ResolvePathSlot, 0x18AFB90), SharedFileExists
                              (0x18B0F30), SharedPathExists (0x18B1000),
                              EnsureParentDirectory (0x18B1360) — resolve the path
                              and, when the (parent) directory is missing, create it
                            + the video-options option-map cluster: LoadVideoOptions
                              (0x775EE0) builds the 96-byte Int / Float / Detail
                              option-map triple from VideoOptionsConfig.xml (the
                              32-byte maps, the 24-byte red-black tree nodes, the
                              24-byte CXMLReadStream stack object, the map
                              copy / destroy machinery, and the Int / Float /
                              Detail section readers; the LHXML parse engine is a
                              remaining stub, so the maps stay empty as in the
                              original when the file is missing) + DestroyOptionMaps
                              (0x40E1F0) — the triple's destructor
                              + the settings-file-path cluster: the shared-slot
                              string builders — the COW detach (DetachSharedSlot
                              0x1A01500), the buffer append (SharedBufferAppend
                              0x403D10) and capacity grow (EnsureBufferCapacity
                              0x403DB0 + MSB index TopBitIndex 0x4040C0), and the
                              shared-slot append/create (AppendSharedSlot
                              0x1A01880) — plus the per-file builders
                              (MakeSettingsFilePath 0x1A02230 = base slot + literal;
                              MakeVideoOptionsPath 0x72F7B0 appends
                              "VideoOptions.xml" to the settings-directory slot
                              0x1DBE2F0)
                              + the saved video-options values cluster:
                              LoadVideoOptionsValues (0x775D10) loads the
                              user's saved video settings from VideoOptions.xml
                              into the 52-byte record (InitVideoOptionsDefaults
                              0x19E1380 = 1280x720 defaults, then
                              ReadVideoOptionsValues 0x775A60 reads the
                              "VideoOptions" element's Version / AdapterOrdinal /
                              ScreenResolutionX / Y / Fullscreen / VSync /
                              TextureDetail .. DrawDistance / Calibrated through
                              the CXMLReadStream vtable; a silent LHXML report
                              handler (XmlReportNoop 0x775A50, via
                              Get/SetXmlReportHandler 0x17E4F80 / 0x17E4F90 on
                              off_1D94CA0) is installed for the duration of the
                              read; the LHXML parse stub leaves the defaults in
                              place, as in the original when the file is
                              missing)
                              + GetDefaultResolution (0x61A000) — picks the
                              default resolution from the desktop aspect ratio
                              (closest of 16:9 -> 1280x720, 4:3 -> 1152x864,
                              16:10 -> 1280x800; the rdata aspect floats
                              0x1BF95A4 / 0x1AE9B04 / 0x1AE9B00), written to
                              the out {width, height} pair and returned
                              recursively (CreatePathTree, 0x18B0730), plus the
                              splitpath/makepath slot helpers (SplitPathIntoSlots
                              0x18ABFA0, MakePathSlot 0x18ABF00, component walker
                              NextPathComponent 0x18ABE80 / ExtractPathComponent
                              0x18ABC00, CWD-global ops RefCwdSlot 0x18AEE10 /
                              ChdirSlot 0x18AFA80, AnsiFromWideSlot 0x19FFAB0)
  netinit.h / netinit.cpp   InitNetworking (0x6A7210) — network-subsystem bootstrap
                            (XLive settings + XLiveInitializeEx, worker pre-translate
                            hook via SetPreTranslate, LIONHEAD::StartupNetworking
                            (0x181F4A0: XNet/XOnline + Winsock 2.2), one-time
                            CNetLiveSystem construction (CNetLiveSystemCtor 0xB76DF0
                            [STUB] + CNetLiveSystemAtExit 0x1AC2490) + presence /
                            notification init (CNetLiveSystemNotifyInit 0xB73150),
                            CNetManager alloc + ctor (CNetManagerCtor 0x6A6C10) +
                            publish (SetNetManager 0x6A7A30 + DisposeNetManager
                            0x6A76C0 [BYTE-IDENTICAL]) + CNetManagerStart (0x6A6DB0 [STUB]))
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
| `DisposeNetManager` (0x6A76C0) | 17 | **YES** | Deleting-destructor dispatch (`vtable[0](arg, 1)`) — the SEH handler of the global net-manager pair (dword_1DC2550 / dword_1DC2554). Pure register/stack ops, no absolute refs or relative calls. `__declspec(naked)` inline asm. |
| `SetPreTranslate` (0x18F6F20) | 36 | no | Placement-dependent: relative calls to the `EnterCriticalSection` / `LeaveCriticalSection` IAT slots. `__thiscall` + `retn 4` hybrid (worker in ECX, callback on the stack, callee cleans via `ret 4`); stores the callback in the worker's `preTranslate` field (+0x270) under the worker CS and returns it. The C++ version takes the worker as a plain first parameter. |
| `CNetManagerCtor` (0x6A6C10) | 91 | no | Placement-dependent (relative calls to the identity no-op `sub_18984D0`, `DNameNode::DNameNode`, and the two class vftables — CNetManager / NPacketCallback::CNetPacketCallback — which are placeholder vtables in this build). Constructs the 80-byte CNetManager in place: vtable + 14 zeroed DWORDs (+0x04..+0x38), the packet-callback sub-object at +0x3C, and +0x44..+0x4C zeroed. |
| `SetNetManager` (0x6A7A30) | 110 | no | Placement-dependent (relative call to `DisposeNetManager`, `InterlockedDecrement` / `LHFree` IAT slots, and the high-VA net-manager pair globals). The `__thiscall` + `retn 4` setter for dword_1DC2550 / dword_1DC2554: releases the old pair's SEH block (handler + `LHFree` on zero), stores the new manager, and wraps it in a fresh 12-byte SEH block (refcount 1, `DisposeNetManager`, arg). |
| `InitNetworking` (0x6A7210) | 223 | no | Placement-dependent (relative calls to the engine callees, the xlive / kernel32 IAT slots) and refs the high-VA globals `byte_1C86C17` / `dword_1DBE280` / `unk_1DBDDA8` / `dword_1DC2550`. The network-subsystem bootstrap RunFrontEnd runs first (after `InitEngineTime`): 32-byte XLive settings (dwSize 28, wLanguageId) + `XLiveInitializeEx(…, 0x32000300)`, installs `XLivePreTranslateMessage` as the worker pre-translate hook (skipped in the standalone test, which runs `GameThread` with no worker), `LIONHEAD::StartupNetworking` (0x181F4A0, once-counted XNet/XOnline + WSAStartup 2.2 bring-up), one-time CNetLiveSystem ctor (0xB76DF0, stubbed) + `atexit` teardown + `CNetLiveSystemNotifyInit` (0xB73150), then CNetManager `LHAlloc(80)` + ctor, `SetNetManager`, and `CNetManagerStart` (0x6A6DB0, stubbed). The xlive imports are stubbed to success; `XLivePreTranslateMessage` returns FALSE (unhandled). |
| `OnResetDevice` (0x615340) | 61 | no | Placement-dependent (relative calls to the `sub_18F01A0` caps getter, the `XLiveOnResetDevice` IAT thunk, `__security_check_cookie`) and reads the original security-cookie at its high VA. Copies the 56-byte device-caps block (`dword_1E2ED80`) onto the stack and forwards it to the stubbed XDK `XLiveOnResetDevice` import. |
| `GetEngineTime` (0x18A88E0) | 99 | no | Placement-independent, but refs the `QueryPerformanceCounter` IAT slot (0x01AD50C8) and start globals (0x020B5FC0/0x020B5FB8) at original high VAs not present in the build. |
| `InitEngineTime` (0x18A89A0) | 143 | no | Placement-independent (short jumps only), but refs the high-VA globals `qword_20B5FC0` / `dbl_20B5FB8` / `byte_2123048` and the `QueryPerformanceCounter` / `QueryPerformanceFrequency` IAT slots (0x01AD50C8 / 0x01AD5098) at their original addresses. One-time init: captures the QPC counter + frequency (as a positive double of the full 64-bit value) that `GetEngineTime` reads, sets the `byte_2123048` flag, and returns the start counter's high 32 bits & 0x7FFFFFFF (0 on later calls); both callers (`RunFrontEnd`, the LHCOM request-manager ctor `sub_18A45D0`) discard it. |
| `LHFree` (0x1A032C0) | 11 | no | Placement-independent, but refs the heap-table pointer global (0x02075B24) at the original high VA. |
| `LHFreeHandler` (0x401D20) | 11 | no | Byte-identical to `LHFree`; placement-independent, but refs the heap table (0x02075B24) at the original high VA. |
| `JmpLHFree` (0x615870) | 5 | no | Placement-dependent: the near `jmp LHFree` is rebased to the build's `LHFree` (the original displacement targeted 0x1A032C0). `__declspec(naked)` inline asm; the SEH release code pushes the object pointer and calls the handler, so the tail jump passes it straight into `LHFree`'s (cdecl) parameter. |
| `SetThreadName` (0x18C5840) | 131 | no | Placement-dependent: refs the compiler-generated SEH scope table (`stru_1C7F160`) + EH4 scope handler (0x152ECE0), the original /GS security cookie, and the `RaiseException` IAT slot (0x01AD51B0) at their original high VAs. |
| `SharedBufferFree` (0x403820) | 58 | no | Placement-independent, but refs the heap table (0x02075B24) at the original high VA. |
| `ReleaseGlobalObject` (0x4017B0) | 71 | no | Placement-independent, but refs the heap table (0x02075B24), `lpAddend` (0x020B6008), and the `InterlockedDecrement` IAT slot (0x01AD52FC) at original high VAs. |
| `InitSharedSlot` (0x1A00C30) | 24 | no | Placement-independent (no relative calls / long jumps), but refs the live-slot counter `g_Addend` (0x02075C48) and the `InterlockedIncrement` IAT slot (0x01AD52F8) at original high VAs. |
| `ReleaseSharedSlot` (0x1A01320) | 61 | no | Placement-dependent: relative calls to the `InterlockedDecrement` IAT slot and to `SharedBufferFree` (0x403820); refs the live-slot counter `g_Addend` (0x02075C48) at its original high VA. The shared-slot dtor (counterpart to `InitSharedSlot`). |
| `SharedBufferCreate` (0x1A01360) | 115 | no | Placement-dependent (relative calls to `LHAlloc` and `SharedBufferAssign`). userpurge in the original (src in EDI, size on the stack); the allocate-and-fill helper for the shared-slot setter: `LHAlloc(16)`, zero data/length/capacity, `SharedBufferAssign(src, len)` (len = size, or the null-terminated length when size == -1), refcount 1, return the block. |
| `SetSharedSlot` (0x1A01CC0) | 102 | no | Placement-dependent (relative calls to `SharedBufferCreate` and `SharedBufferFree`), plus the `InterlockedIncrement` / `InterlockedDecrement` IAT slots and the high-VA live-slot counter `g_Addend` (0x02075C48). The engine's general shared-slot setter: thiscall in the original (slot in ECX, src + size on the stack, `retn 8`) with ~18,460 call sites; nulls the slot, bumps `g_Addend`, builds a fresh counted buffer from (src, size) via `SharedBufferCreate` when src is non-empty, releases any old block the slot still holds, stores the new one, and returns the slot pointer. |
| `LoadVideoOptionsValues` (0x775D10) | 139 | no | Placement-dependent (relative calls to `Get/SetXmlReportHandler` (0x17E4F80 / 0x17E4F90), `InitVideoOptionsDefaults` (0x19E1380), the CXMLReadStream ctor / dtor, `OpenXmlFile`, `ReadVideoOptionsValues` (0x775A60), `ReleaseSharedSlot`, and `__security_check_cookie`; refs the high-VA security cookie + the `XmlReportNoop` literal (0x775A50)). Load the user's saved video settings into the 52-byte record (adapter ordinal, resolution X / Y, fullscreen, the eight TextureDetail .. DrawDistance values, vsync, calibrated): install the silent report handler on `off_1D94CA0` (the saved one restored after), write the 1280x720 defaults, open the settings `VideoOptions.xml` (the path slot from `MakeVideoOptionsPath`) in a 24-byte stack `CXMLReadStream`, read the "VideoOptions" element's fields in (`ReadVideoOptionsValues`, 0x775A60), destroy the stream, release the path slot, and return the record pointer. In this build the LHXML parse is a remaining stub, so the element search never finds the section and the record keeps its defaults (exactly the original's behavior when the file is missing); replaces the `sub_775D10` stub in `RunFrontEnd` (the v103..v106 frontend local). |
| `GetDefaultResolution` (0x61A000) | 256 | no | Refs the `__imp_GetSystemMetrics` IAT slot and the high-VA rdata floats at their original addresses (16:9 `dword_1BF95A4` 0x3FE38E39, 4:3 `dword_1AE9B04` 0x3FAAAAB, 16:10 `dword_1AE9B00` 0x3FCCCCCC, the -1.0f fabs sign mask `dword_1AE9AF0`). Picks the default video resolution from the desktop's aspect ratio: `SM_CXSCREEN / SM_CYSCREEN` via `GetSystemMetrics(0/1)`, the candidate aspect ratio with the strictly-smallest `|aspect - candidate|` difference (ties keep the earlier index; the 0.0 sentinel ends the scan), and the matching `{width, height}` pair — 1280x720 / 1152x864 / 1280x800 — is written to the out pointer and returned. Sole call site: `RunFrontEnd` (0x61CC1D), which seeds the frontend resolution globals `dword_1C88DE4` / `dword_1C88DE8` and, when the saved settings record is uncalibrated, overwrites the saved-resolution words with the same pair; replaces the `sub_61A000` stub in `RunFrontEnd`. |
| `GetWorker` (0x18F6A40) | 6 | no | Placement-independent, but refs the high-VA global `dword_20FC8F0` (0x020FC8F0) not present in the build. |
| `GetManager` (0x18F8750) | 6 | no | Placement-independent, but refs the high-VA global `dword_20FC8C4` (0x020FC8C4) not present in the build. |
| `CManagerWin32Ctor` (0x18F7160) | 46 | no | Placement-independent, but refs the two class vftables (CManagerBase 0x01BE7340 / CManagerWin32 0x01BE737C) and the `InitializeCriticalSection` IAT slot (0x01AD531C) at original high VAs not present in the build. |
| `GetGameBuffer` (0x619AA0) | 6 | no | Placement-independent (no calls / relative jumps), but refs the game buffer (0x01DBD318) at its original high VA not present in the build. |
| `GetLocalizedString` (0x19FF570) | 15 | no | Placement-independent (no calls / relative jumps), but refs the default "Path" wide-string global (0x01BDFD28) at its original high VA not present in the build. |
| `GetLocaleEntry` (0x6195C0) | 656 | no | Placement-dependent: an indirect IAT call to `GetUserDefaultLCID` (0x01AD...), `mov eax, offset dword_20B6078`, MSVC switch jump tables, and the .data locale/key tables (0x01DBD120..0x01DBD2F4) + not-found default table (0x020B6078) at their original high VAs not present in the build. |
| `PumpWorker` (0x18F8600) | 169 | no | Placement-dependent (relative calls to `EnsureWorkerWindow` / `PumpWorkerMessages` / `ShutdownDevice`) and refs the `EnterCriticalSection` / `LeaveCriticalSection` / `SetEvent` / `WaitForSingleObjectEx` / `WaitMessage` IAT slots at their original high VAs. |
| `PumpWorkerMessages` (0x18F8530) | 203 | no | Placement-dependent (relative calls to `HandleDisplayModeChange` and `__security_check_cookie`) and refs the `EnterCriticalSection` / `LeaveCriticalSection` / `PeekMessageA` / `TranslateMessage` / `DispatchMessageA` IAT slots at their original high VAs. Worker message pump (state 2): peeks the worker window's queue outside the CS, runs the pre-translate hook (+0x270, XLive) on each message, and if unhandled, translates it, dispatches it outside the CS, and (if the display-mode-pending flag +0x281 is set) runs `HandleDisplayModeChange` (stubbed) under a nested CS entry; exits holding the worker CS. |
| `EnsureWorkerWindow` (0x18F82E0) | 589 | no | Placement-dependent (relative calls to `CreateWorkerWindow` / `SetHWndParent` / `InitDisplayMode` / `TrySetDisplayMode` / `SetDisplayMode` / `ResizeWorkerWindow` / `ApplyDisplaySettings` and the `SetWindowLongA` / `SetWindowPos` / `MessageBoxW` / `PostMessageA` / `ShowWindow` / `UpdateWindow` IAT slots) and refs the high-VA locale-key globals `dword_20FC8EC` / `dword_20FC8E8`. Creates the worker window (state 1) then runs the display-mode dance; the deep display-mode callees are stubbed. |
| `DispatchWorkerMessage` (0x18F7460) | 1225 | no | Placement-dependent (relative calls to `SetWorkerState` / `WorkerFlushFrame` / `SaveSettingsBeforeExit` / `SetInputActive` / `FindActiveInputDevice` / `HandleRawInput` / `RecordInputEvent` / `IsDisplayReady` / `DeleteSharedFile` / `ReleaseLfxModule` / `ResizeWorkerWindow` / `MakeAbnormalShutdownPath` / `sub_1A22460` and the `EnterCriticalSection` / `LeaveCriticalSection` / `DefWindowProcA` / `SystemParametersInfoA` / `ShowCursor` / `SetCursor` / `LoadCursorA` / `InvalidateRect` / `SetEvent` / `ExitProcess` IAT slots) and refs the high-VA globals `hCursor` (0x020FC8D4), `hInstance` (0x020FC8CC), `dword_20FC8D0` (0x020FC8D0), `byte_2122F1C` (0x02122F1C), `dword_1D9E18C`/`dword_1D9E194`/`byte_1D9E19C`, `hModule` (0x01DD3218). The real main-window message handler (the original is a 3-arg stdcall that also reads lParam from ECX, `retn 0Ch`; reimplemented as a clean 4-arg WNDPROC): enters the worker CS (worker +4), dispatches on Msg, and either forwards to `DefWindowProcA` (unhandled) or returns 0 (handled); the `WM_CLOSE` (0x10) path saves settings and calls `ExitProcess(0)`. Deep out-of-scope callees are stubbed (`HandleRawInput` gamepad processing, `RecordInputEvent`'s `sub_5C9F50`, `DeleteSharedFile` path resolution). `IsDisplayReady` (called from `WorkerFlushFrame`) is implemented; its deep callee `ResetXloDevice` (0x18F2310) is stubbed, so the device-lost path (status 0x88760869) reports "not ready" and `WorkerFlushFrame` keeps its pending flag set. |
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
| `IsDisplayReady` (0x18F2850) | 101 | no | Placement-dependent (relative call to `ResetXloDevice`) and refs the high-VA globals `stru_20FC948` (0x020FC948), `dword_1E2EEF4` (0x01E2EEF4), `dword_1E2ED80` (0x01E2ED80) plus the `EnterCriticalSection` / `LeaveCriticalSection` IAT slots. Display-readiness check (called by `WorkerFlushFrame`, 0x18F7190): under the device CS (0x20FC948, lazy-initialized like 0x20FCA50), queries the device object through its vtable[3] (offset 0x0C) as `(device, device)`; if the status is 0x88760869 (device lost), attempts `ResetXloDevice` (0x18F2310, stubbed → 0) with the 56-byte caps block at `dword_1E2ED80` and a failed reset reports "not ready"; any other nonzero status also reports "not ready". A null-guard reports "not ready" for a pre-device-init slot (the original would fault). |
| `ResetXloDevice` (0x18F2310) | 1344 | no | [STUB] Returns 0 (reset failed). Real function (nested under the device CS 0x20FC948): if the device object (0x1E2EEF4) reports status 0x88760868 (already reset) return 0; otherwise, on first use (byte_2075F04), releases the per-device object lists (`LHAlloc` container, 0x20FCA0C/0x20FC9CC callback walks, `ReleaseDeviceObjects` + `sub_566CB0`), recreates the device through its vtable[16] (offset 0x40) with the new 56-byte caps, records the caps into 0x1E2ED80 and the mode class into `dword_1E2ED74` (selected from the last caps dword: low bit/0x80000001 → 1, bit 2 → 2, bit 4 → 3, bit 8 → 4), runs `sub_18F1310`, re-notifies the 0x20FCA0C (vtable[2]) and 0x20FC9EC callback lists, clears `byte_2075F04`, and returns 1. The device-rebuild subsystem (`sub_18E65F0`, `sub_18F1310`, `sub_56xxxx` object-list helpers) is out of scope. |
| `NvApiTeardownUnload` (0x17D1E60) | 209 | no | Placement-dependent (long jumps to the common exit) and refs `hLibModule` (0x01E2C704), the nvapi globals `dword_1E2C708`/`dword_1E2C70C`/`dword_1E2C710`, the resolved-interface table (0x01D941F4), and the `FreeLibrary` IAT slot (0x01AD5154) at their original high VAs. Release the device library: run the NVAPI teardown entry (interface 0xD22BDD7E), clear all 178 resolved-interface slots, `FreeLibrary(hLibModule)`, and clear the resolver + before/after hooks (returns -4/-3/-1/0). |
| `NvApiTeardownFlush` (0x17D6510) | 175 | no | Placement-independent body (no relative calls / long jumps), but refs the nvapi.dll interface globals `dword_1E2C708`/`dword_1E2C70C`/`dword_1E2C710` and the interface-table slot `dword_1D94644`/`byte_1D94648` at their original high VAs. Device teardown flush: lazily resolves NVAPI interface 0x2EC50C2B via `nvapi_QueryInterface`, runs the entry bracketed by the before/after hooks, and returns its result (-3 if unavailable). |
| `SteamAPI_Init` (0x17DA5EA) | 6 | no | `steam_api.dll` import thunk — the original is a single 6-byte `FF 25 C8 55 AD 01` (`jmp ds:__imp_SteamAPI_Init`), with no in-image body; the real `bool SteamAPI_Init(void)` lives in steam_api.dll (the bundled Steamworks proxy). `RunFrontEnd` calls it only after confirming `"Data/ThisIsSteam"` is present and sets `byte_1DBDC4A` ("SteamAPI initialized") on a nonzero return. The self-contained build does not link steam_api.dll, so it resolves the import dynamically (`LoadLibraryA("steam_api.dll")` + `GetProcAddress`) and forwards the call, returning 0 (init failed) when the DLL is absent — the "no Steam" path the frontend takes. [NOT byte-identical] — no steam_api IAT slot in the build to `jmp` to. (Sibling thunks `SteamFriends` 0x17DA5F0 / `SteamUtils` 0x17DA5F6 have no implemented caller yet.) |
| `FrameworkInit`, `FrameworkShutdown`, `ComputeExeDirectory`, `LHAlloc`, `SharedBufferAssign`, `InitSharedSlot` (0x1A00C30), `OutOfMemory`, `RunGame`, `GameThread` (0x615380), `CWorkerWin32Ctor`, `CWorkerWin32_Dtor`, `CWorkerWin32_DtorBody`, `CManagerWin32_Dtor` (0x55D8B0), `CManagerWin32_DtorBody` (0x55D5F0), `SetSaveGamePath`, `SetWorker` (0x18F6FC0), `ReleaseWorkerPair` (0x55DB10), `SetManager` (0x18F87E0), `ReleaseManagerPair` (0x55D7C0), `SetDebugTrace` (0x1A02E20), `SetDebugHandler` (0x1A02DD0), `ReleaseObjectPair` (0x403220), `HeapStressTest` (0x18986E0), `CGameEngineCtor` (0x61C530), `CGameEngineSubCtor` (0x623360), `CGameVersionCtor` (0x72DD10), `CGameVersionReset` (0x72DC80), `RunFrontEnd` (0x61CB70), `EnsureParentDirectory` (0x18B1360), `SharedPathExists` (0x18B1000), `CreatePathTree` (0x18B0730), `SplitPathIntoSlots` (0x18ABFA0), `MakePathSlot` (0x18ABF00), `NextPathComponent` (0x18ABE80), `ExtractPathComponent` (0x18ABC00), `RefCwdSlot` (0x18AEE10), `AnsiFromWideSlot` (0x19FFAB0), `ChdirSlot` (0x18AFA80), `LoadVideoOptions` (0x775EE0), `DestroyOptionMaps` (0x40E1F0), `MakeVideoOptionsPath` (0x72F7B0), `MakeAbnormalShutdownPath` (0x67C930), `MakeSettingsFilePath` (0x1A02230), `AppendSharedSlot` (0x1A01880), `DetachSharedSlot` (0x1A01500), `SharedBufferAppend` (0x403D10), `EnsureBufferCapacity` (0x403DB0), `TopBitIndex` (0x4040C0), `ReadVideoOptionsValues` (0x775A60), `InitVideoOptionsDefaults` (0x19E1380), `SetDetailOptionSlotName` (0x19E1920), `GetXmlReportHandler` (0x17E4F80), `SetXmlReportHandler` (0x17E4F90), `XmlReportNoop` (0x775A50) | — | no | Placement-dependent (relative calls to callees/CRT). `CGameEngineCtor` also writes the high-VA global `dword_1DBD9DC`. `RunFrontEnd` is the 9.3 KB frontend start-up driver — hundreds of relative calls to engine callees, the xlive/steam import stubs, and Win32, plus references to many high-VA data globals. `LIONHEAD::StartupNetworking` (0x181F4A0) / `CNetLiveSystemNotifyInit` (0xB73150) are placement-dependent (IAT calls) and ref the high-VA globals `dword_1E2D8C0` / `unk_1DBDDA8`. The settings-file-path builders are placement-dependent: `MakeVideoOptionsPath` (0x72F7B0) → `MakeSettingsFilePath` (0x1A02230) refs the settings-directory shared slot (`dword_1DBE2F0`, 0x1DBE2F0) via `RefSharedSlot` (0x1A012C0) and appends a literal filename with `AppendSharedSlot` (0x1A01880; when the slot is empty it creates the buffer via `SharedBufferCreate`, 0x1A01360, otherwise it COW-detaches through `DetachSharedSlot` 0x1A01500 and appends with `SharedBufferAppend` 0x403D10, which grows capacity through `EnsureBufferCapacity` 0x403DB0 / `TopBitIndex` 0x4040C0); the per-file builders share the settings-dir slot with `MakeAbnormalShutdownPath` (0x67C930, "abnormal_shutdown.tmp" — implemented), `sub_67C910` ("Controls\\") and the AccessibilitySettings.xml builders.

To byte-match the placement-independent functions that reference IAT/data
globals, those globals must be placed at their original VAs (a PE
post-processor that adds high-RVA sections + reference patching, growing the
image toward the original's ~30 MB). Deferred until the module grows enough
that it is worth doing.

## Plan — modules still to build out

| Module    | Entry (IDA)          | What to add |
|-----------|----------------------|-------------|
| `engine/` | `RunGame` (0x615740), `GameThread` (0x615380), `CWorkerWin32Ctor` (0x18F7040), `CManagerWin32Ctor` (0x18F7160), `SetWorker`/`GetWorker`/`ReleaseWorkerPair` (0x18F6FC0/0x18F6A40/0x55DB10), `SetManager`/`GetManager`/`ReleaseManagerPair` (0x18F87E0/0x18F8750/0x55D7C0), `SetDebugTrace` (0x1A02E20), `SetDebugHandler` (0x1A02DD0), `ReleaseObjectPair` (0x403220), `HeapStressTest` (0x18986E0), `DisposeWorker` (0x615880), `DisposeManager` (0x615850), `CWorkerWin32_Dtor`/`CWorkerWin32_DtorBody` (0x55D8F0/0x55D930), `CManagerWin32_Dtor`/`CManagerWin32_DtorBody` (0x55D8B0/0x55D5F0), `CGameEngineCtor` (0x61C530), `CGameEngineSubCtor` (0x623360), `CGameVersionCtor` (0x72DD10), `CGameVersionReset` (0x72DC80), `SetOnCreateDevice` (0x18F6CA0), `OnCreateDevice` (0x6152F0), `SetOnResetDevice` (0x18F6C50), `OnResetDevice` (0x615340), `LHFreeHandler` (0x401D20), `JmpLHFree` (0x615870), `SetThreadName` (0x18C5840), `PumpWorker` (0x18F8600), `PumpWorkerMessages` (0x18F8530), `EnsureWorkerWindow` (0x18F82E0), `ShutdownDevice` (0x18F17B0), `NvApiTeardownFlush` (0x17D6510), `NvApiTeardownUnload` (0x17D1E60), `TeardownDeviceSlot` (0x18F0E00), `TeardownDeviceBase` (0x18F0C10), `GetSlotClientRect` (0x18F0710), `DestroyDevice` (0x18F1760), `ReleaseDeviceObjects` (0x18F1220), `FreeObjectListContainer` (0x18CB3D0), `F3ListRemoveNode` (0x5A4840), `FreeDeviceFlushRequest` (0x5639E0), `NvApiDestroyObject` (0x17D6740), `IsDisplayReady` (0x18F2850), `ResetXloDevice` (0x18F2310, [STUB]), `GetGameBuffer` (0x619AA0), `RunFrontEnd` (0x61CB70), `InitEngineTime` (0x18A89A0), `InitNetworking` (0x6A7210), `SetPreTranslate` (0x18F6F20), `CNetManagerCtor` (0x6A6C10), `SetNetManager` (0x6A7A30), `DisposeNetManager` (0x6A76C0), `CNetManagerStart` (0x6A6DB0, [STUB]), `LIONHEAD::StartupNetworking` (0x181F4A0), `CNetLiveSystemCtor` (0xB76DF0, [STUB]), `CNetLiveSystemDtor` (0xB76AD0, [STUB]), `CNetLiveSystemAtExit` (0x1AC2490), `CNetLiveSystemNotifyInit` (0xB73150), `MakeVideoOptionsPath` (0x72F7B0), `MakeAbnormalShutdownPath` (0x67C930), `LoadVideoOptionsValues` (0x775D10), `GetDefaultResolution` (0x61A000) | (done) run loop + game thread + main-worker ctor + worker & manager pairs + CDebugTraceFable + CFableWindowsDebugHandler registration (CS-protected) + heap sanity check + SEH dispose path + 356-byte game-engine ctor (with CGameVersion sub-object) + worker on-create-device callback setter (byte-identical) + both XLO device callbacks (on-create and on-reset, setters byte-identical) + CDebugTraceFable SEH handler thunk (JmpLHFree) + worker pump step (PumpWorker; device-shutdown callee stubbed at that time) + worker message pump (PumpWorkerMessages, 0x18F8530; pre-translate hook (XLive, +0x270) + display-mode-pending flag (+0x281); display-mode change handler HandleDisplayModeChange, 0x18F8060, stubbed) + worker window create/restore step (EnsureWorkerWindow, 0x18F82E0; window class / wndproc / parent / display-mode-request init / resize implemented, deep display-mode callees stubbed) + frontend start-up driver (RunFrontEnd, 0x61CB70; XNA sign-in / device / engine sub-objects / worker callbacks / intro video / thread start, with ~60 stubbed engine callees and xlive import stubs; the steam_api import thunk SteamAPI_Init (0x17DA5EA) is a dynamic forwarder to steam_api.dll, degrading to the no-Steam path (return 0) when the DLL is absent) + network-subsystem bootstrap (InitNetworking, 0x6A7210; XLiveInitializeEx + worker pre-translate hook (SetPreTranslate, 0x18F6F20) + LIONHEAD::StartupNetworking (0x181F4A0, XNet/XOnline + Winsock 2.2) + one-time CNetLiveSystem (ctor/dtor stubbed, atexit thunk + CNetLiveSystemNotifyInit 0xB73150 implemented) + 80-byte CNetManager ctor (0x6A6C10) + global pair publish (SetNetManager 0x6A7A30 + byte-identical DisposeNetManager 0x6A76C0) + CNetManagerStart (0x6A6DB0, stubbed)) + the video-options option-map cluster (LoadVideoOptions 0x775EE0: the 96-byte Int / Float / Detail option-map triple from VideoOptionsConfig.xml — 32-byte maps (4-byte { &obj } wrapper + 24-byte red-black tree of 24-byte nodes + count), the 24-byte CXMLReadStream stack object (file-block open / close, section search), the Int / Float / Detail section readers (FindInt/Float/DetailOptionValue 0x777150 / 0x777220 / 0x75FE80, the tree key lookup stubbed), the map copy-ctors + recursive subtree copies, and the triple destructor DestroyOptionMaps 0x40E1F0; the LHXML parse engine is a remaining stub, so no section is ever found and the maps stay empty — exactly the original's behavior when the file is missing; replaces the sub_775EE0 / sub_40E1F0 stubs in RunFrontEnd) + release the device library (NvApiTeardownUnload, 0x17D1E60) + per-slot device teardown (TeardownDeviceSlot, 0x18F0E00; notifies device vtable[37] + releases the sub-record objects, falls through to sub_18F0850) + per-slot base-device teardown (TeardownDeviceBase, 0x18F0C10; notifies device vtable[39] + releases the base-device object, tail-calls GetSlotClientRect, 0x18F0710; stores the device client rect {x, y, x+w, y+h} in the slot row) + final device teardown (DestroyDevice, 0x18F1760; frees the flush request (FreeDeviceFlushRequest, 0x5639E0; wraps NvApiDestroyObject, 0x17D6740, NVAPI interface 0x3A153134), releases the device's vtable objects + per-slot tables (ReleaseDeviceObjects, 0x18F1220), and frees the 0x20FC988 object-list container (FreeObjectListContainer, 0x18CB3D0 + F3ListRemoveNode, 0x5A4840)) + display-readiness check (IsDisplayReady, 0x18F2850; device CS 0x20FC948 lazy-initialized, device vtable[3] status query, device-lost → ResetXloDevice (0x18F2310, stubbed) with the 56-byte caps block at 0x1E2ED80) + one-time engine-time init (InitEngineTime, 0x18A89A0; captures the QPC start counter / frequency into 0x20B5FC0 / 0x20B5FB8 that GetEngineTime reads, replaces the sub_18A89A0 stub in RunFrontEnd) + the current-directory path cluster (the CWD global g_cwdBlock 0x2122DE4 and its wide→ANSI helpers, the path resolver sub_18AFB90, SharedFileExists 0x18B0F30, SharedPathExists 0x18B1000, EnsureParentDirectory 0x18B1360 — resolve a path slot and, when the directory part is missing, create the directory tree recursively (CreatePathTree 0x18B0730: drive switch + chdir/mkdir component walk, then chdir back to the CWD, updating the global); replaces the sub_18B1360 stub in RunFrontEnd) + the settings-file-path cluster (MakeVideoOptionsPath 0x72F7B0: ref the settings-directory shared slot (0x1DBE2F0) and append "VideoOptions.xml" — built on the shared-slot string builders MakeSettingsFilePath 0x1A02230 (base slot + literal), AppendSharedSlot 0x1A01880 (append/create; empty slot → SharedBufferCreate 0x1A01360, else COW DetachSharedSlot 0x1A01500 + SharedBufferAppend 0x403D10, which grows capacity via EnsureBufferCapacity 0x403DB0 / TopBitIndex 0x4040C0); replaces the sub_72F7B0 stub in RunFrontEnd) + the abnormal-shutdown marker path builder (MakeAbnormalShutdownPath 0x67C930: the same MakeSettingsFilePath build for "abnormal_shutdown.tmp" — RunFrontEnd shows the info box when the marker already exists, the worker WM_CLOSE path deletes the marker on a clean exit (DeleteSharedFile's path resolution still stubbed); replaces the sub_67C930 stubs in RunFrontEnd and DispatchWorkerMessage) + the saved video-options values cluster (LoadVideoOptionsValues 0x775D10: the 52-byte saved video settings record (adapter ordinal, resolution, fullscreen, the eight detail values, vsync, calibrated) — 1280x720 defaults (InitVideoOptionsDefaults 0x19E1380) + the "VideoOptions" element reader (ReadVideoOptionsValues 0x775A60: Version / AdapterOrdinal / ScreenResolutionX / Y / Fullscreen / VSync / TextureDetail .. DrawDistance / Calibrated, one reused 4-byte key slot per element, the CXMLReadStream vtable dispatches, SetDetailOptionSlotName 0x19E1920 for the eight detail names) under a silent LHXML report handler (off_1D94CA0 swapped to XmlReportNoop 0x775A50 via Get/SetXmlReportHandler 0x17E4F80 / 0x17E4F90 for the duration of the read; the CXMLReadStream placeholder vtable's enterElement slot is wired to FindXmlSection 0x17DC6D0 so the dispatch is live but the parse stub finds nothing — the record keeps its defaults, as the original does when the file is missing); replaces the sub_775D10 stub in RunFrontEnd) + default-resolution pick (GetDefaultResolution 0x61A000: the desktop aspect ratio (GetSystemMetrics 0/1) is matched against the 16:9 / 4:3 / 16:10 rdata candidates (strictly-smallest difference, ties keep the earlier index), and the matching 1280x720 / 1152x864 / 1280x800 pair is written to the out pointer and returned; replaces the sub_61A000 stub in RunFrontEnd); next: fill in the remaining stub callees (sub_18F0850, HandleDisplayModeChange, ...) |
| `save/`   | `SetSaveGamePath` (0x1A019B0) | (done) store the save path refcounted into `dword_1DBD06C` + getter; next: the actual load logic |
| `framework/` | `ComputeExeDirectory` (0x1A02870), `ReleaseGlobalObject` (0x4017B0) | (done) exe-directory compute + `ExeDirectory()` getter + global shared-object release; the framework init/shutdown pair is complete |
| `common/` | `LHAlloc` (0x1A035C0), `LHFree` (0x1A032C0), `SharedBufferAssign` (0x403FE0), `SharedBufferFree` (0x403820), `SharedBufferCreate` (0x1A01360), `OutOfMemory` (0x1A034F0), `GetLocaleEntry` (0x6195C0), `GetLocalizedString` (0x19FF570), `SetSharedSlot` (0x1A01CC0) | (done) heap alloc/free + `F3SharedBuffer` counted buffer + OOM report handler + per-LCID localized-string table lookup (GetLocaleEntry) + localized-string getter + shared-slot init/destroy/setter (InitSharedSlot 0x1A00C30, ReleaseSharedSlot 0x1A01320, SetSharedSlot 0x1A01CC0 + its allocate-and-fill helper SharedBufferCreate 0x1A01360); next: cross-module globals `byte_1DBD01B`, `byte_1C86BD9` |
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
