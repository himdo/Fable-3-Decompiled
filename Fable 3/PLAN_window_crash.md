# Plan: Fix the eip=0 crash (window never shows)

## Goal
Get the Fable 3 reconstruction window to actually appear and stay up, after
blockers #1 (debug wait), #2 (SMS shared-memory), and #3 (empty window class
name) were fixed. The window is now *created* (`CreateWindowExA` returns a valid
HWND), but the process then crashes with `c0000005` at `eip=0` before
`ShowWindow`/rendering.

## What we know (confirmed via cdb runtime tracing)

### The crash
- Second-chance access violation `c0000005` at `eip=0` (a jump/call to NULL).
- On the **main thread** (thread 0). The GameThread is still blocked on
  `event1` in `CWorkerWin32_RunGame`, so the main thread crashed during
  `EnsureWorkerWindow` → display setup, before signaling `event1`.
- Registers at the crash: `ecx=2 esi=1 ebx=<heap> edx=<ptr> ebp=0` — a
  vtable-style dispatch (`call [reg]` / `jmp [reg]`) where the target pointer
  is NULL.

### The exact call sequence (breakpoint traces)
```
EnsureWorkerWindow (EW)
  -> SetHWndParent (SHP)
  -> SetDisplayMode (SDM)
  -> CreateD3D9DisplayObject (CD3D)
  -> d3d9!Direct3DCreate9 (D3DCREATE)   <- factory IS created (non-NULL)
  -> AV (eip=0)
```
- `VT8` (the factory `vtable[8]` display-mode query in `SetDisplayMode`) is
  **NOT** entered before the AV.
- `FEAM` (`F3EnumAdapterModes`, last call in CD3D before it returns) is
  **NOT** entered before the AV.
- So the crash is **inside `CreateD3D9DisplayObject`**, *after*
  `Direct3DCreate9` returns and *before* the `vtable[14]` "adapter-identifier
  query" dispatch and before `F3EnumAdapterModes`.

### State at the crash
- `g_dword_1E2EEF8` (D3D9 factory) = **non-NULL**, a real d3d9.dll
  `IDirect3D9` (created by `Direct3DCreate9`). Its vtable lives in d3d9.dll
  and is the **standard** `IDirect3D9` layout:
  `[3]=GetAdapterIdentifiers, [4]=GetAdapterCount, [8]=EnumAdapterModes,
  [12]=GetAdapterDisplayMode, [13]=CreateDevice`.
- `g_dword_1E2EEF4` (device) = **NULL** (device not created yet).

### Original's factory vtable slots (from IDA decompile of the real binary)
- `CreateD3D9DisplayObject` (0x18F21A0):
  - `GetAdapterCount` via standard `lpVtbl` (slot 4). OK.
  - adapter-identifier query via `*(_DWORD*)dword_1E2EEF8 + 56` = **slot 14**,
    called as `(factory, adapterIndex, behavior, &dword_1E2EDB8)` -> `int hr`.
  - on S_OK: `F3EnumAdapterModes(dword_1E2EDBC, flags)` (thiscall, `this` =
    `&dword_1E2EDB8`), `F3ReserveModeList`, `F3DestroyModeListContainer`,
    `F3InitAdapterState`.
- `SetDisplayMode` (0x18F2B30), no-device path, after `CreateD3D9DisplayObject`:
  - display-mode query via `*(_DWORD*)dword_1E2EEF8 + 32` = **slot 8**, called
    as `(factory, dword_1E2EDBC, mode[16 bytes])` -> void.
  - then format-table scan (bounded by `off_1D9DF80`), `PickBestDisplayMode`,
    `BuildXloDeviceFromRequest`.

### Key discrepancy
The original dispatches the **factory** (an `IDirect3D9`) at **slot 14**
(adapter-identifier query, 4 args) and **slot 8** (display-mode query, void).
In the *standard* d3d9.dll `IDirect3D9` vtable those slots are a
private/unknown method (14) and `EnumAdapterModes` (8) respectively — NOT the
functions the game expects. The original therefore does **not** rely on the
stock d3d9 vtable at those offsets; in the real game the object at
`dword_1E2EEF8` exposes those methods at those slots.

In the reconstruction, `CreateD3D9DisplayObject` creates a **stock** d3d9
`IDirect3D9` and then calls its slot 14 / slot 8 — which in stock d3d9 are
not the intended entry points, and the slot-14 call (or the subsequent
path) ends up jumping to NULL.

## Root-cause hypothesis (most likely)
`CreateD3D9DisplayObject`'s "adapter-identifier query" at factory **slot 14**
(4-arg call) hits a non-functional/NULL target in the stock d3d9 vtable,
producing the `eip=0` jump. The stock d3d9 factory does not present the
original's expected method set at slots 8/14.

## Plan of work
1. **Confirm the exact jump site** — breakpoint on the factory slot-14
   function at runtime (after the factory exists) and on the AV; verify the
   slot-14 call is the last thing executed. (Validation step.)

2. **Make the factory query calls safe/correct** in
   `CreateD3D9DisplayObject` (worker.cpp ~1281) and the `SetDisplayMode`
   display-mode query (worker.cpp ~1466):
   - Option A (minimal, unblocks the window): treat the adapter-identifier
     query and display-mode query as best-effort. Guard the slot-14 and
     slot-8 factory dispatches so a bad/NULL target can't jump to NULL
     (validate the vtable pointer + slot before calling), and default
     `g_dword_1E2EDB8[1]` (adapter id = 0) and the display-mode block
     (width/height/refresh/pixelFormat) to sane values so the rest of the
     pipeline proceeds.
   - Option B (faithful): create the D3D9 factory as the original expects —
     i.e., ensure the object at `g_dword_1E2EEF8` exposes the intended methods
     at slots 8/14 (possibly via `IDirect3D9Ex` / the right SDK version, or a
     thin wrapper) so the slot-14 adapter-identifier query and slot-8
     display-mode query hit real functions.

3. **Continue the pipeline to a visible window** — once CD3D no longer
   crashes, ensure:
   - `PickBestDisplayMode` returns a non-zero picked mode (so
     `BuildXloDeviceFromRequest` runs).
   - `BuildXloDeviceFromRequest` creates a real D3D9 device on the factory
     and writes `g_dword_1E2EEF4` (non-NULL).
   - `ShowWindow`/`UpdateWindow` are reached and the window is visible.

4. **Verify** — rebuild debug, launch under cdb with the SMS object held,
   confirm no `eip=0` crash and that `ShowWindow` is reached / a window is
   enumerable.

## Files to touch
- `Fable 3/engine/worker.cpp` — `CreateD3D9DisplayObject` (~1281),
  `SetDisplayMode` (~1449-1500), `F3FindFormatRow`, `PickBestDisplayMode`,
  `BuildXloDeviceFromRequest` (~2777), `CreateXloDeviceFromAdapter` (~2410).
- Possibly `Fable 3/common/common.cpp` / `common.h` if new globals are needed.

## Open questions
- Is the original's factory a stock `IDirect3D9`, or a wrapper/`IDirect3D9Ex`
  that presents the intended methods at slots 8/14? (Decide A vs B.)
- What does the slot-14 adapter-identifier query write into
  `g_dword_1E2EDB8[0..]` (the `D3DADAPTER_IDENTIFIER9`), and what does
  `g_dword_1E2EDB8[1]` (adapter id) need to be for the device path?
- Does `PickBestDisplayMode` need real adapter modes (from
  `F3EnumAdapterModes`, currently a stub) to return a non-zero pick?
