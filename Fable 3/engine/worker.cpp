// Fable 3 — CWorkerWin32 module implementation (the 664-byte main worker
// object, its deleting destructor, and the current-worker registry).

#include "worker.h"
#include "common/common.h"
#include "manager.h"
#include "gamemain.h"       // MakeAbnormalShutdownPath

// g_Addend (0x2075C48, the live-slot counter) now lives in common/common.cpp
// alongside InitSharedSlot / ReleaseSharedSlot, its primary users.

// dword_20FC8F0 / dword_20FC8F4: the current worker + its SEH cleanup block.
static F3ObjReg g_WorkerPair;

// ============================================================
//  CWorkerWin32_DtorBody  (0x55D930 - 0x55D9B3)  -- the worker destructor body
//  [NOT byte-identical] placement-dependent (relative call to SharedBufferFree).
//
//  Releases the shared member at +596 (decrementing its refcount and freeing
//  the buffer when it reaches zero), decrements the worker refcount, closes
//  the four auto-reset events, deletes the critical section, and resets the
//  vtable (to the base-class vftable in the original).
// ============================================================
static void CWorkerWin32_DtorBody(CWorkerWin32 *w)
{
    char *base = (char *)w;

    void *block = *(void **)(base + 596);
    if (block != 0)
    {
        if (InterlockedDecrement((volatile long *)((char *)block + 12)) <= 0)
        {
            void *b2 = *(void **)(base + 596);
            if (b2 != 0)
                SharedBufferFree((F3SharedBuffer *)b2);
        }
        *(void **)(base + 596) = 0;
    }

    InterlockedDecrement(&g_Addend);
    *(void **)(base + 596) = 0;   // redundant clear (matches the original)
    CloseHandle(*(HANDLE *)(base + 552));   // event4
    CloseHandle(*(HANDLE *)(base + 420));   // event3
    CloseHandle(*(HANDLE *)(base + 288));   // event2
    CloseHandle(*(HANDLE *)(base + 156));   // event1
    DeleteCriticalSection((LPCRITICAL_SECTION)(base + 4));   // the critical section
    *(void **)base = 0;   // reset vtable (base-class vftable in the original)
}

// ============================================================
//  CWorkerWin32_Dtor  (0x55D8F0 - 0x55D913)  -- the worker deleting destructor
//
//  Functional (not byte-identical; it has relative calls). __thiscall (worker
//  in ECX) and cleans up its own flag arg (retn 4), matching the original
//  sub_55D8F0's convention so the byte-identical DisposeWorker can dispatch
//  through it. Runs the dtor body, then frees via the heap if the flag is set.
// ============================================================
static __declspec(naked) void *CWorkerWin32_Dtor(void)
{
    __asm {
        push   edi
        mov    edi, ecx            ; edi = worker
        push   ecx                 ; pass worker on the stack to the body
        call   CWorkerWin32_DtorBody
        add    esp, 4
        test   [esp+8], 1          ; the flag arg
        jz     short f3_cwd_nofree
        push   edi
        call   LHFree
        add    esp, 4
    f3_cwd_nofree:
        mov    eax, edi            ; return the worker
        pop   edi
        retn   4                   ; clean up the flag arg
    }
}

// NSystem::CWorkerWin32 vtable — slot 0 is the deleting destructor (the one
// the dispose handler calls); the remaining slots are not implemented.
static void *g_CWorkerWin32Vtable[8] = { (void *)CWorkerWin32_Dtor, 0, 0, 0, 0, 0, 0, 0 };

// ============================================================
//  Engine-time globals (original IDs kept)
//
//  qword_20B5FC0: the QPC counter captured at engine start (64-bit).
//  dbl_20B5FB8:   the QPC frequency as a double.
//  byte_2123048:  non-zero once InitEngineTime has captured them.
//  Written by InitEngineTime (0x18A89A0), read by GetEngineTime (0x18A88E0).
// ============================================================
static LARGE_INTEGER g_qword_20B5FC0;   // 0x20B5FC0 engine-start QPC counter
static double g_dbl_20B5FB8 = 0.0;      // 0x20B5FB8 QPC frequency (double)
static BYTE   g_byte_2123048 = 0;       // 0x2123048 engine-time initialized flag

// ============================================================
//  GetEngineTime  (0x18A88E0)  -- seconds since engine start
//  [NOT byte-identical] placement-independent but references the QueryPerformanceCounter
//  IAT slot (0x01AD50C8) and start globals (0x020B5FC0/0x020B5FB8) at original high VAs not present in this build.
//
//  (QueryPerformanceCounter() - qword_20B5FC0) / dbl_20B5FB8. The original
//  reads the globals captured by InitEngineTime; if that has not run yet we
//  lazily capture them here so the first call still returns valid seconds.
// ============================================================
static double GetEngineTime(void)
{
    LARGE_INTEGER now;

    if (g_dbl_20B5FB8 == 0.0)
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_dbl_20B5FB8 = (double)(unsigned __int64)f.QuadPart;
        QueryPerformanceCounter(&g_qword_20B5FC0);
    }
    QueryPerformanceCounter(&now);
    return (double)(unsigned __int64)(now.QuadPart - g_qword_20B5FC0.QuadPart) / g_dbl_20B5FB8;
}

// ============================================================
//  InitEngineTime  (0x18A89A0 - 0x18A8A2F)  -- one-time engine-time init
//  [NOT byte-identical] refs high-VA globals (qword_20B5FC0 / dbl_20B5FB8 /
//  byte_2123048) and the QueryPerformanceCounter / QueryPerformanceFrequency
//  IAT slots (0x01AD50C8 / 0x01AD5098) at their original addresses.
//
//  On the first call captures the QPC counter into qword_20B5FC0 and the QPC
//  frequency (converted to a positive double from the full 64-bit value) into
//  dbl_20B5FB8, sets the byte_2123048 flag, and returns the captured counter's
//  high 32 bits & 0x7FFFFFFF. Later calls return 0. Both callers (RunFrontEnd
//  and the LHCOM request-manager ctor sub_18A45D0) discard the return value.
// ============================================================
int InitEngineTime(void)
{
    if (g_byte_2123048 == 0)
    {
        LARGE_INTEGER freq;
        QueryPerformanceCounter(&g_qword_20B5FC0);
        QueryPerformanceFrequency(&freq);
        g_byte_2123048 = 1;
        g_dbl_20B5FB8 = (double)(unsigned __int64)freq.QuadPart;
        return (int)(g_qword_20B5FC0.HighPart & 0x7FFFFFFF);
    }
    return 0;
}

// ============================================================
//  CWorkerWin32Ctor  (0x18F7040 - 0x18F7152)  -- construct the main worker
//  [NOT byte-identical] placement-dependent (relative call to GetEngineTime).
//
//  void *CWorkerWin32Ctor(void *obj, int hInstance, int nShowCmd)
//
//  Zeroes the 664-byte object, sets the vtable, initializes the critical
//  section, creates four auto-reset events, stores hInstance/nShowCmd, seeds
//  the timing field, and bumps the worker refcount. Returns this.
// ============================================================
void *CWorkerWin32Ctor(void *obj, int hInstance, int nShowCmd)
{
    CWorkerWin32 *w = (CWorkerWin32 *)obj;
    memset(w, 0, sizeof(CWorkerWin32));

    w->vtable   = g_CWorkerWin32Vtable;
    InitializeCriticalSection(&w->cs);
    w->event1   = CreateEventA(0, FALSE, FALSE, 0);
    w->event2   = CreateEventA(0, FALSE, FALSE, 0);
    w->event3   = CreateEventA(0, FALSE, FALSE, 0);
    w->event4   = CreateEventA(0, FALSE, FALSE, 0);
    w->hInstance = hInstance;
    w->nShowCmd  = nShowCmd;
    w->timing    = GetEngineTime();
    InterlockedIncrement(&g_Addend);
    return obj;
}

// ============================================================
//  [BYTE-IDENTICAL]  DisposeWorker  (0x00615880 - 0x00615891)  -- SEH handler
//
//  Invoked by the SEH block when its refcount reaches zero. It calls the
//  worker's first virtual method (vtable[0]) with argument 1:
//      if (worker != 0) return worker->vtable[0](worker, 1);
//  Emits the original 17 bytes exactly (placement-independent: no absolute
//  refs, and the vtable call is indirect via EDX).
// ============================================================
int __declspec(naked) DisposeWorker(int)
{
    __asm {
        mov    ecx, [esp+4]
        test   ecx, ecx
        jz     short f3_dw_done
        mov    eax, [ecx]
        mov    edx, [eax]
        push   1
        call   edx
    f3_dw_done:
        retn
    }
}

// ============================================================
//  ReleaseWorkerPair  (0x0055DB10 - 0x0055DB52)  -- release a worker pair
//  [NOT byte-identical] placement-dependent (relative call to the heap free).
//
//  void ReleaseWorkerPair(void *pair)  (pair = &F3ObjReg)
//
//  Decrement the SEH block's refcount; when it reaches zero invoke
//  handler(arg) and free the block via the game heap. Then clear both words.
// ============================================================
static void ReleaseWorkerPair(void *pair)
{
    F3ObjReg *p = (F3ObjReg *)pair;
    F3SehBlock *sb = (F3SehBlock *)p->sehBlock;

    if (sb != 0 && InterlockedDecrement(&sb->refcount) == 0)
    {
        sb->handler(sb->arg);
        LHFree(sb);
    }
    p->object = 0;
    p->sehBlock = 0;
}

// ============================================================
//  SetWorker  (0x18F6FC0)  -- install the current worker + SEH cleanup block
//  [NOT byte-identical] placement-dependent (relative calls to
//  ReleaseWorkerPair and InterlockedIncrement).
//
//  void SetWorker(F3ObjReg *reg)
//
//  If reg->sehBlock already matches the current global, no-op. Otherwise
//  release the current pair (dropping the old SEH block's reference), store
//  (reg->object, reg->sehBlock) in the global pair, and take a reference on
//  the new SEH block so it outlives RunGame.
// ============================================================
void SetWorker(F3ObjReg *reg)
{
    if (g_WorkerPair.sehBlock == reg->sehBlock)
        return;

    ReleaseWorkerPair(&g_WorkerPair);
    g_WorkerPair.object = reg->object;
    g_WorkerPair.sehBlock = reg->sehBlock;
    if (reg->sehBlock != 0)
        InterlockedIncrement(&((F3SehBlock *)reg->sehBlock)->refcount);
}

// ============================================================
//  GetWorker  (0x18F6A40 - 0x18F6A46)  -- return the current worker
//  [NOT byte-identical] placement-independent but references the high-VA
//  global dword_20FC8F0 (0x020FC8F0) not present in this build.
//
//  A tiny getter (mov eax, dword_20FC8F0; retn) used pervasively by the
//  engine to reach the current CWorkerWin32.
// ============================================================
void *GetWorker(void)
{
    return g_WorkerPair.object;
}

// ============================================================
//  SetOnCreateDevice  (0x18F6CA0 - 0x18F6CAD)  -- set the worker's
//  on-create-device callback (the worker field at +0x258)
//
//  [BYTE-IDENTICAL]  A __thiscall + retn 4 hybrid: the worker is in ECX and
//  the callback is a stack arg the callee cleans (ret 4). It stores the
//  callback in the worker's onCreateDevice field and returns it. The caller
//  must dispatch it as a __thiscall function (see the F3ThiscallSetFn cast
//  in RunGame) so that it cleans the arg itself.
//
//  F3CallbackFn SetOnCreateDevice(CWorkerWin32 *worker, F3CallbackFn cb)
// ============================================================
__declspec(naked) F3CallbackFn SetOnCreateDevice(void *worker, F3CallbackFn cb)
{
    __asm {
        mov    eax, [esp+4]      ; eax = cb
        mov    [ecx+258h], eax   ; worker->onCreateDevice = cb
        ret    4                 ; return cb, clean up the stack arg
    }
}

// ============================================================
//  Device-shutdown state (0x1E2E / 0x2075)  -- touched by ShutdownDevice
//  (0x18F17B0) and SetHWndParent (0x18F1AE0). The original keeps these at the
//  VAs noted; here they live as static globals in this module.
// ============================================================
static unsigned char g_byte_2075DDB = 0;   // 0x2075DDB  device-active flag
static long          g_dword_1E2ED70 = 0;  // 0x1E2ED70  reset to -1 on shutdown
static long          g_dword_1E2ED6C = 0;  // 0x1E2ED6C  reset to -1 on shutdown

// 0x2075B28: the current parent/owner window for device creation (set by
// SetHWndParent, 0x18F1AE0). High-VA global; lives here as a static.
static HWND g_hWndParent = 0;

// 0x20FCA50: critical section guarding the device-slot globals (0x1DB1220 / 0x1DB12B4).
// In the image it is all-0xFF poison; the game's device-subsystem init calls
// InitializeCriticalSection on it before any Enter. We reproduce that with a
// one-time lazy init (a raw zero/poison CS faults on Enter).
static CRITICAL_SECTION g_stru_20FCA50;
static int g_stru_20FCA50Init = 0;
static void F3EnterDeviceSlot(void)
{
    if (!g_stru_20FCA50Init)
    {
        InitializeCriticalSection(&g_stru_20FCA50);
        g_stru_20FCA50Init = 1;
    }
    EnterCriticalSection(&g_stru_20FCA50);
}
static DWORD g_dword_1DB1220 = 0xFF000000;  // 0x1DB1220  device-slot state
static DWORD g_unk_1DB12B4   = 0xFF000000;  // 0x1DB12B4  device-slot state

// 0x1DB12xx: vtable-backed device objects released by ReleaseDeviceObjects
// (0x18F1220); the companion DWORDs (1DB1248 / 1DB1268) are cleared
// alongside their objects. Zero in the image.
static DWORD g_dword_1DB1228 = 0;
static DWORD g_dword_1DB1244 = 0;
static DWORD g_dword_1DB1248 = 0;
static DWORD g_dword_1DB1264 = 0;
static DWORD g_dword_1DB1268 = 0;

// 0x20FC8EC / 0x20FC8E8: locale keys for the display-error MessageBoxW text
// and caption (GetLocaleEntry keys; filled in at runtime in the original).
static int g_dword_20FC8EC = 0;
static int g_dword_20FC8E8 = 0;

// ============================================================
//  F3DisplayMode  -- the 44-byte display-mode request struct passed to the
//  display-mode setters (TrySetDisplayMode 0x18F28C0 / SetDisplayMode 0x18F2B30)
// ============================================================
struct F3DisplayMode
{
    int f00;      // +0
    int width;    // +4
    int height;   // +8
    int f12;      // +12
    int f16;      // +16
    int f20;      // +20
    int f24;      // +24
    int f28;      // +28
    int f32;      // +32
    int f36;      // +36
    int f40;      // +40
};

// 0x2122F18: window-message dispatch target. WorkerWndProc stores the main
// window's lpCreateParams (the worker) here on WM_CREATE and forwards
// messages to sub_18F7460 while the worker is in message-pump state (2).
static int g_dword_2122F18 = 0;

// 0x1BD44D7: the default window class name ("Str1" in IDA) -- an empty string
// in the original image (the class name is normally supplied through the
// worker's shared block at +596).
static const char g_DefaultClassName[] = "";

// ============================================================
//  sub_18F7460 (DispatchWorkerMessage)  -- window-message dispatch
//
//  The real main-window message handler, invoked by WorkerWndProc while the
//  worker is in message-pump state (2). The original is a 3-arg stdcall that
//  also reads lParam from ECX (retn 0Ch); here it is written as a clean
//  4-argument WNDPROC. It enters the worker critical section (worker +4),
//  dispatches on Msg, and either forwards to DefWindowProcA (unhandled) or
//  returns 0 (handled). The WM_CLOSE (0x10) path saves settings and calls
//  ExitProcess(0), so it never returns.
//
//  Globals referenced (high-VA, kept under their original IDA names):
//    0x20FC8CC  hInstance     -- the process HINSTANCE
//    0x20FC8D0  dword_20FC8D0 -- default cursor fallback (wParam==0 of 0x8003)
//    0x20FC8D4  hCursor       -- the current worker cursor (HCURSOR)
//    0x2122F1C  byte_2122F1C  -- cursor-shown tracking flag
// ============================================================
static int     g_hInstance     = 0;   // 0x20FC8CC
static int     g_dword_20FC8D0 = 0;   // 0x20FC8D0
static HCURSOR g_hCursor       = 0;   // 0x20FC8D4
static char    g_byte_2122F1C  = 0;   // 0x2122F1C

// 0x1D9E18C / 0x1D9E194 / 0x1D9E19C: the system-parameter restore values used
// by SaveSettingsBeforeExit (sub_18F71E0).
static DWORD g_dword_1D9E18C = 0;
static DWORD g_dword_1D9E194 = 0;
static BYTE  g_byte_1D9E19C  = 0;

// Forward declaration for a helper defined later in this file (used by the
// WM_SIZE path).
static int ResizeWorkerWindow(CWorkerWin32 *obj);

// 0x1E2EEF4: the current XLO device object (a vtable-backed pointer; defined
// here ahead of IsDisplayReady, grouped conceptually with the device-global
// block further down).
static void *g_dword_1E2EEF4 = 0;

// 0x1E2ED80: the 56-byte (14 DWORD) XLO device-caps block (xlo_device.cpp
// carries an independent mirror of this global for the XLO import paths).
static DWORD g_dword_1E2ED80[14];

// 0x1E2EDCC: device capability byte gating the render-mode-class selection
// (GetDisplayModeClass, sub_18EFF00). Bit 2 selects class 2 vs 1; bit 4
// selects class 4 vs 1.
static unsigned char g_byte_1E2EDCC = 0;

// 0x1E2EDB8: the 16-byte D3D9 adapter-identifier out block, filled by the
// GetAdapterIdentifier query (CreateD3D9DisplayObject, sub_18F21A0). Its
// second word, dword_1E2EDBC (0x1E2EDBC) == g_dword_1E2EDB8[1], is the adapter
// index handed to the mode-enumeration helper (sub_18F1FC0).
static DWORD g_dword_1E2EDB8[4];   // 0x1E2EDB8 .. 0x1E2EDC7

// 0x1E2EEF8: the current D3D9 adapter object (a vtable-backed pointer; also
// read/cleared by ShutdownDevice further down).
static DWORD g_dword_1E2EEF8 = 0;   // 0x1E2EEF8  D3D9 adapter object (vtable-backed)

// 0x20FC948: the device critical section guarding the XLO device state
// (dword_1E2EEF4 / dword_1E2ED80 / dword_1E2ED74). In the image it is
// all-0xFF poison, as with stru_20FCA50; the game's device-subsystem init
// calls InitializeCriticalSection on it before any Enter. Reproduce that with
// a one-time lazy init (a raw poison CS faults on Enter).
static CRITICAL_SECTION g_stru_20FC948;
static int g_stru_20FC948Init = 0;
static void F3EnterDisplayCs(void)
{
    if (!g_stru_20FC948Init)
    {
        InitializeCriticalSection(&g_stru_20FC948);
        g_stru_20FC948Init = 1;
    }
    EnterCriticalSection(&g_stru_20FC948);
}

// ============================================================
//  [STUB]  ResetXloDevice  (0x18F2310 - 0x18F2850)  -- reset/recreate the XLO
//  device from a fresh 56-byte caps block
//
//  int ResetXloDevice(const DWORD *caps56)   -- 1 = reset succeeded, 0 = failed
//
//  Original semantics (nested under the device CS stru_20FC948 already held
//  by the caller): if the device object (dword_1E2EEF4) reports status
//  0x88760868 (already reset) return 0. Otherwise, unless the device has
//  been built once (byte_2075F04), release the per-device resources first
//  (the LHAlloc-allocated container, the 0x20FCA0C/0x20FC9CC object-list
//  callback walks, ReleaseDeviceObjects + sub_566CB0). Then recreate the
//  device through its vtable[16] (offset 0x40) with the new caps, record the
//  caps into dword_1E2ED80 and the mode class into dword_1E2ED74 (selected
//  from the last caps dword: bit 0x80000001/low bit -> 1, bit 2 -> 2, bit 4
//  -> 3, bit 8 -> 4), run sub_18F1310, re-notify the 0x20FCA0C list (vtable[2])
//  and the 0x20FC9EC list, clear byte_2075F04, and return 1. The full
//  device-rebuild subsystem (object-list management, sub_18E65F0,
//  sub_18F1310, sub_56xxxx helpers) is out of scope. Returns 0 (reset failed)
//  so IsDisplayReady stays conservative.
// ============================================================
static int ResetXloDevice(const DWORD *caps56)
{
    (void)caps56;
    return 0;
}

//  IsDisplayReady  (0x18F2850 - 0x18F28B5)  -- is the XLO display ready to
//  present a frame?
//  [NOT byte-identical] placement-dependent (relative call to
//  ResetXloDevice) and refs the high-VA globals stru_20FC948 (0x020FC948),
//  dword_1E2EEF4 (0x01E2EEF4), dword_1E2ED80 (0x01E2ED80) plus the
//  EnterCriticalSection / LeaveCriticalSection IAT slots.
//
//  char IsDisplayReady(void)   -- 1 = ready, 0 = not ready
//
//  Under the device CS (stru_20FC948): query the device object
//  (dword_1E2EEF4) through its vtable[3] (offset 0x0C), passing the device
//  pointer as the (single, stdcall) argument. If the status is 0x88760869
//  (device lost), attempt a device reset (ResetXloDevice, 0x18F2310) with the
//  56-byte caps block at dword_1E2ED80; a failed reset reports "not ready".
//  Any other nonzero status also reports "not ready". Otherwise the display
//  is ready. The original faults on a null device slot; the guard below
//  reports "not ready" instead so this build is safe pre-device-init.
static char IsDisplayReady(void)
{
    if (g_dword_1E2EEF4 == 0)
        return 0;

    F3EnterDisplayCs();
    void *devObj = g_dword_1E2EEF4;
    void **vt    = *(void ***)devObj;
    int status   = (*(int (**)(void *, void *))(vt[3]))(devObj, devObj);
    if (status == 0x88760869)
    {
        if (ResetXloDevice(g_dword_1E2ED80) == 0)
        {
            LeaveCriticalSection(&g_stru_20FC948);
            return 0;
        }
    }
    else if (status != 0)
    {
        LeaveCriticalSection(&g_stru_20FC948);
        return 0;
    }
    LeaveCriticalSection(&g_stru_20FC948);
    return 1;
}

// 0x188EDF0 -- input-manager "set active" (obj in the caller's ECX/esi, a
// state byte on the stack). Sets obj[72]=state and, when the state changes to
// 0 (deactivate), walks the observer list at obj[12..16) invoking each
// observer's vtable[4].
static void SetInputActive(void *obj, unsigned char state)
{
    if (obj == 0)
        return;
    char *o = (char *)obj;
    EnterCriticalSection((LPCRITICAL_SECTION)(o + 76));
    int changed = (o[72] != (char)state);
    o[72] = (char)state;
    if (changed && state == 0)
    {
        void **i = (void **)(*(int *)(o + 12));   // list start
        void **end = (void **)(*(int *)(o + 16)); // list end
        while (i != end)
        {
            void *obs = *i;
            void *vt = *(void **)obs;
            if (vt)
                (*(void (**)(void *))((char *)vt + 16))(obs);   // vtable[4](obs)
            i = (void **)((char *)i + 8);
        }
    }
    LeaveCriticalSection((LPCRITICAL_SECTION)(o + 76));
}

// 0x188EC90 -- find the active input device. Walks the device list at
// obj[12..16); returns the first device whose vtable[1] returns 3, else 0.
void *FindActiveInputDevice(void *obj)
{
    if (obj == 0)
        return 0;
    DWORD *self = (DWORD *)obj;
    void **i = (void **)(self[3]);   // offset 12 = list start
    void **end = (void **)(self[4]); // offset 16 = list end
    if (i == end)
        return 0;
    for (;;)
    {
        void *obs = *i;
        void *vt = *(void **)obs;
        int res = (vt != 0) ? (*(int (**)(void *))((char *)vt + 4))(obs) : 0; // vtable[1]
        if (res == 3)
            return obs;
        i = (void **)((char *)i + 8);
        if (i == end)
            return 0;
    }
}

// ============================================================
//  InitAccessibilitySettings  (0x18F72B0 - 0x18F7455)  -- [remaining stub]
//
//  The accessibility-settings start-up load: applies the SystemParametersInfoA
//  overrides (SPI_GETHIDCONFIRM 0x3A / GETKEYBOARDSPEED 0x34 / GETKEYBOARDPREECHO
//  0x32 into dword_1D9E18C / dword_1D9E194 / byte_1D9E19C), and when
//  "AccessibilitySettings.xml" exists in the settings directory (the
//  0x1DBE2F0 slot + "AccessibilitySettings.xml", SharedFileExists), opens it
//  through CXMLReadStream + FindXmlSection and loads the three accessibility
//  values into dword_20FC8DC / dword_20FC8E0 / dword_20FC8E4 / byte_1D9E19C,
//  then runs SaveSettingsBeforeExit. The LHXML engine and the settings-value
//  consumers are out of scope here, so this remains a no-op stub (the
//  original's behavior with no settings file: the defaults stand).
// ============================================================
static void InitAccessibilitySettings(void)
{
}

// ============================================================
//  CWorkerWin32_RunGame  (0x18F7C50 - 0x18F7D21)  -- the worker's "run the
//  game" step (the manager's run dispatch delegates to this with the worker
//  in ECX/EDI)
//  [NOT byte-identical] placement-dependent (relative calls to
//  AnsiFromWideSlot / CopySharedSlot / SharedBufferFree / InitAccessibilitySettings and the
//  Enter / LeaveCriticalSection / InterlockedDecrement / WaitForSingleObjectEx
//  IAT slots) plus the high-VA live-slot counter g_Addend (0x02075C48).
//
//  __usercall in the original (worker in EDI, seven stack args, retn 0x1C;
//  the manager's a7 is not forwarded). Reimplemented as a plain thiscall
//  free function (worker as the first parameter).
//
//  char CWorkerWin32_RunGame(CWorkerWin32 *worker, int a2, int a3, int a4,
//                            int a5, int a6, int a8, F3SharedBuffer *a9)
//
//  Under the worker CS: stores the parsed start-up parameters in the
//  worker's display-mode / requested-size fields (+0x240 a2, +0x244 a3,
//  +0x248 a4, +0x24C a5 byte, +0x24D a6 byte, +0x250 a8), converts the
//  wide locale slot (a9) into a fresh ANSI shared slot (AnsiFromWideSlot,
//  0x19FFAB0), assigns it into the worker's sharedBlock slot (+0x254,
//  CopySharedSlot, 0x1A01270), then releases the temporary local slot
//  (SharedBufferFree at zero buffer refcount + the g_Addend decrement). It
//  sets the pump state to 1 (window create), leaves the CS, runs the
//  accessibility-settings load (sub_18F72B0, stubbed), blocks on event1
//  (+0x9C, INFINITE), and returns (hwnd != 0) -- nonzero only once the
//  main thread's pump (PumpWorker state 1) has created the worker window
//  and signalled event1. Null-guarded (the original assumes the worker
//  global is live; the standalone test has no worker).
// ============================================================
char CWorkerWin32_RunGame(void *worker, int a2, int a3, int a4, int a5, int a6,
                          int a8, F3SharedBuffer *wideSlot)
{
    if (worker == 0)
        return 0;
    CWorkerWin32 *w = (CWorkerWin32 *)worker;
    char *c = (char *)w;

    EnterCriticalSection(&w->cs);
    w->f_0240       = a2;     // +0x240
    w->windowWidth  = a3;     // +0x244
    w->windowHeight = a4;     // +0x248
    c[0x24C]        = (char)a5;   // fullscreen byte
    c[0x24D]        = (char)a6;   // refresh byte
    *(int *)(c + 0x250) = a8;     // +0x250

    F3SharedBuffer *local = 0;
    AnsiFromWideSlot(wideSlot, &local);   // 0x19FFAB0
    CopySharedSlot((F3SharedBuffer **)(c + 0x254), &local);   // 0x1A01270
    if (local != 0)   // release the temporary local slot
    {
        if (InterlockedDecrement((volatile long *)((char *)local + 12)) <= 0)
            SharedBufferFree(local);
    }
    InterlockedDecrement(&g_Addend);

    w->workerState = 1;      // +0x22C: request the window create
    LeaveCriticalSection(&w->cs);

    InitAccessibilitySettings();   // sub_18F72B0 (stub)

    WaitForSingleObjectEx(w->event1, INFINITE, FALSE);   // +0x9C
    return (w->hwnd != 0);
}

// 0x188D710 -- handle a raw-input report (obj, HRAWINPUT). The original calls
// GetRawInputData and feeds the gamepad axis/button state into the input
// subsystem (sub_412AB0 / sub_412B00 / sub_417C50 / sub_5CCC90 / ...), which
// is out of scope here. [remaining stub]
static UINT HandleRawInput(void *obj, void *hRawInput)
{
    (void)obj;
    (void)hRawInput;
    return 0;
}

// 0x188F140 -- record an input event (obj, WORD code, BYTE state). The
// original takes obj's CS and calls sub_5C9F50(&code, obj+24). The event
// recorder is out of scope here. [remaining stub]
static void RecordInputEvent(void *obj, unsigned short code, char state)
{
    (void)code;
    (void)state;
    if (obj == 0)
        return;
    char *o = (char *)obj;
    EnterCriticalSection((LPCRITICAL_SECTION)(o + 76));
    LeaveCriticalSection((LPCRITICAL_SECTION)(o + 76));
}

// 0x18F7000 -- set the worker's activation/cursor state (and mirror it to the
// input manager). Original is __usercall (state in EAX, worker in ECX).
static int SetWorkerState(void *worker, int state)
{
    char *w = (char *)worker;
    if (w[0x23C] != (BYTE)state)
    {
        w[0x23C] = (BYTE)state;
        w[0x287] = 1;
        void *mgr = GetManager();
        if (mgr != 0)
            SetInputActive(*(void **)((char *)mgr + 0x20), (BYTE)state);
    }
    return state;
}

// 0x18F7190 -- flush a pending worker frame (worker in the caller's ESI). If
// the render-pending flag (worker[0x280]) is set and the display is ready,
// invoke the render callback (worker +0x26C), set the frame event (worker
// +0x1A4), clear the pending flag, and propagate the reflow flag (worker
// +0x283 -> worker +0x285). Otherwise keep the pending flag set.
static void WorkerFlushFrame(void *worker)
{
    char *w = (char *)worker;
    if (w[0x280] != 0)
    {
        if (IsDisplayReady() != 0)
        {
            (*(int (**)(void))(w + 0x26C))();   // onResetDevice callback
            SetEvent(*(HANDLE *)(w + 0x1A4));   // event3
            w[0x280] = 0;
            if (w[0x283] != 0)
                w[0x285] = 1;
        }
        else
        {
            w[0x280] = 1;
        }
    }
}

// 0x18F71E0 -- save settings before exit. Restores the three system
// parameters the game overrode; the original also re-saves
// AccessibilitySettings.xml through shared buffers (out of scope). Returns the
// save status.
static LONG SaveSettingsBeforeExit(void)
{
    SystemParametersInfoA(0x3B, 8, &g_dword_1D9E18C, 0);
    SystemParametersInfoA(0x35, 8, &g_dword_1D9E194, 0);
    SystemParametersInfoA(0x33, 0x18, (PVOID)(unsigned long)g_byte_1D9E19C, 0);
    return 0;
}

// 0x18B0420 -- delete the file resolved from a shared-buffer slot. The
// original resolves the path via sub_18AFB90 and DeleteFileA's it, releasing
// the refcount; the path resolution is out of scope. [remaining stub]
static int DeleteSharedFile(void *slot)
{
    (void)slot;
    return 0;
}

// 0xBE37E0 -- release the LFX module (the loaded module at 0x1DD3218).
// Looks up "LFX_Release" and calls it, clearing the handle.
static int ReleaseLfxModule(void)
{
    static HMODULE g_hModule = 0;   // 0x1DD3218
    int result = (int)g_hModule;
    if (g_hModule != 0)
    {
        FARPROC proc = GetProcAddress(g_hModule, "LFX_Release");
        result = 1;
        if (proc != 0)
        {
            result = ((int (*)(void))proc)();
            g_hModule = 0;
        }
    }
    return result;
}

// 0x18F7460 -- DispatchWorkerMessage (see header comment above).
static LRESULT DispatchWorkerMessage(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    CWorkerWin32 *worker = (CWorkerWin32 *)g_dword_2122F18;
    if (worker == 0)
        return DefWindowProcA(hWnd, Msg, wParam, lParam);

    char *w = (char *)worker;
    CRITICAL_SECTION *cs = &worker->cs;   // worker +4

    EnterCriticalSection(cs);

    if (Msg <= 0x1C)
    {
        if (Msg != 0x1C)
        {
            switch (Msg)
            {
            case 5:   // WM_SIZE
                if (wParam == 1)   // SIZE_MINIMIZED -- show the cursor if hidden
                {
                    if (w[0x286] == 0)
                    {
                        w[0x286] = 1;
                        w[0x284] = 1;
                        SetWorkerState(worker, 0);
                    }
                }
                else if (wParam == 0 || wParam == 2)   // SIZE_RESTORED / SIZE_MAXIMIZED -- hide the cursor if shown
                {
                    if (w[0x286] != 0)
                    {
                        w[0x286] = 0;
                        w[0x285] = 1;
                        SetWorkerState(worker, 1);
                        g_byte_2122F1C = 0;
                        WorkerFlushFrame(worker);
                    }
                }
                if (w[0x24C] != 0)
                {
                    LeaveCriticalSection(cs);
                    return DefWindowProcA(hWnd, Msg, wParam, lParam);
                }
                ResizeWorkerWindow(worker);
                LeaveCriticalSection(cs);
                return DefWindowProcA(hWnd, Msg, wParam, lParam);
            case 6:   // WM_ACTIVATE
            {
                int bl = ((WORD)wParam != 0);
                SetWorkerState(worker, bl);
                if (bl != 0 && g_byte_2122F1C != 0)
                {
                    g_byte_2122F1C = 0;
                    if (w[0x286] == 0)
                        w[0x285] = 1;
                }
                LeaveCriticalSection(cs);
                return DefWindowProcA(hWnd, Msg, wParam, lParam);
            }
            case 7:   // WM_SETFOCUS
            case 8:   // WM_KILLFOCUS
                SetWorkerState(worker, 1);
                LeaveCriticalSection(cs);
                return DefWindowProcA(hWnd, Msg, wParam, lParam);
            case 0xF:   // WM_PAINT
                w[0x289] = 1;   // ready
                if (g_byte_2122F1C != 0)
                {
                    g_byte_2122F1C = 0;
                    if (w[0x286] == 0)
                        w[0x285] = 1;
                }
                LeaveCriticalSection(cs);
                return DefWindowProcA(hWnd, Msg, wParam, lParam);
            case 0x10:   // WM_CLOSE -- hard process exit
                SaveSettingsBeforeExit();
                if (wParam == 0)
                {
                    F3SharedBuffer *slot = 0;
                    MakeAbnormalShutdownPath(&slot);   // 0x67C930: settings dir + "abnormal_shutdown.tmp"
                    DeleteSharedFile(slot);
                    ReleaseSharedSlot(&slot);
                }
                ReleaseLfxModule();
                ExitProcess(0);
                break;
            default:
                LeaveCriticalSection(cs);
                return DefWindowProcA(hWnd, Msg, wParam, lParam);
            }
        }
        else   // Msg == 0x1C (WM_ACTIVATEAPP)
        {
            char bl = (wParam != 0);
            if (w[0x23C] != bl)
            {
                w[0x23C] = bl;
                w[0x287] = 1;
                void *mgr = GetManager();
                if (mgr != 0)
                    SetInputActive(*(void **)((char *)mgr + 0x20), (BYTE)bl);
            }
            if (bl != 0)
                WorkerFlushFrame(worker);
            LeaveCriticalSection(cs);
            return DefWindowProcA(hWnd, Msg, wParam, lParam);
        }
    }
    else if (Msg <= 0x113)
    {
        if (Msg == 0x113)   // custom: refresh / repaint request
        {
            if (wParam == 0x1000 && w[0x23C] != 0)
            {
                WorkerFlushFrame(worker);
                if (w[0x288] != 0)   // shown
                {
                    if (w[0x289] != 0)   // ready
                    {
                        InvalidateRect(*(HWND *)(w + 0x238), 0, 0);
                        w[0x289] = 0;
                    }
                    else
                    {
                        g_byte_2122F1C = 1;
                        w[0x288] = 0;
                        w[0x284] = 1;
                        SetWorkerState(worker, 0);
                    }
                }
            }
            LeaveCriticalSection(cs);
            return DefWindowProcA(hWnd, Msg, wParam, lParam);
        }
        switch (Msg)
        {
        case 0x20:   // WM_SETCURSOR
            SetCursor(g_hCursor);
            LeaveCriticalSection(cs);
            return 0;
        case 0xFF:   // WM_INPUT (raw input)
            if (wParam == 0)
            {
                void *mgr = GetManager();
                void *device = (mgr != 0)
                    ? FindActiveInputDevice(*(void **)((char *)mgr + 0x20))
                    : 0;
                if (device != 0)
                {
                    void *vt = *(void **)device;
                    if (vt && (*(int (**)(void *))((char *)vt + 7 * 4))(device) == 1)
                        HandleRawInput(device, (void *)lParam);
                }
            }
            LeaveCriticalSection(cs);
            return DefWindowProcA(hWnd, Msg, wParam, lParam);
        case 0x102:   // WM_KEYUP
        {
            void *mgr = GetManager();
            void *obj = (mgr != 0) ? *(void **)((char *)mgr + 0x20) : 0;
            char state = (lParam >= 0) ? 1 : 0;
            RecordInputEvent(obj, (WORD)wParam, state);
            LeaveCriticalSection(cs);
            return DefWindowProcA(hWnd, Msg, wParam, lParam);
        }
        case 0x104:   // custom
        case 0x105:   // custom
            if (wParam == 0x12)
            {
                LeaveCriticalSection(cs);
                return 0;
            }
            LeaveCriticalSection(cs);
            return DefWindowProcA(hWnd, Msg, wParam, lParam);
        case 0x112:   // custom (scan-code / gamepad range)
        {
            int v = (int)wParam & 0xFFF0;
            if (v == 0xF120)
            {
                if (w[0x286] != 0)
                {
                    w[0x286] = 0;
                    w[0x285] = 1;
                    SetWorkerState(worker, 1);
                    g_byte_2122F1C = 0;
                    WorkerFlushFrame(worker);
                }
                LeaveCriticalSection(cs);
                return DefWindowProcA(hWnd, Msg, wParam, lParam);
            }
            else if (v == 0xF020)
            {
                if (w[0x286] == 0)
                {
                    w[0x286] = 1;
                    w[0x284] = 1;
                    SetWorkerState(worker, 0);
                }
                LeaveCriticalSection(cs);
                return DefWindowProcA(hWnd, Msg, wParam, lParam);
            }
            else if (v == 0xF100 || v == 0xF140 || v == 0xF170)
            {
                LeaveCriticalSection(cs);
                return 0;
            }
            LeaveCriticalSection(cs);
            return DefWindowProcA(hWnd, Msg, wParam, lParam);
        }
        default:
            LeaveCriticalSection(cs);
            return DefWindowProcA(hWnd, Msg, wParam, lParam);
        }
    }
    else
    {
        // Msg > 0x113
        if (Msg > 0x8002)
        {
            if (Msg == 0x8003)   // custom: set cursor
            {
                HCURSOR cur = (HCURSOR)g_hInstance;
                if (wParam == 0)
                    cur = (HCURSOR)g_dword_20FC8D0;
                g_hCursor = cur;
                if (cur == 0)
                {
                    cur = LoadCursorA(0, (LPCSTR)0x7F00);
                    g_hCursor = cur;
                }
                SetCursor(g_hCursor);
                LeaveCriticalSection(cs);
                return 0;
            }
            LeaveCriticalSection(cs);
            return DefWindowProcA(hWnd, Msg, wParam, lParam);
        }
        else
        {
            // 0x114 < Msg <= 0x8002
            if (Msg == 0x8002)   // custom: show / hide cursor loop
            {
                if (wParam != 0)
                {
                    while (ShowCursor(1) < 0) { }
                }
                else
                {
                    while (ShowCursor(0) >= 0) { }
                }
                LeaveCriticalSection(cs);
                return 0;
            }
            else if (Msg == 0x281)   // custom: forward with lParam cleared
            {
                LeaveCriticalSection(cs);
                return DefWindowProcA(hWnd, Msg, wParam, 0);
            }
            else if (Msg == 0x8000)   // custom: render request
            {
                w[0x284] = 1;
                w[0x283] = 1;
                w[0x280] = 1;
                WorkerFlushFrame(worker);
                LeaveCriticalSection(cs);
                return 0;
            }
            LeaveCriticalSection(cs);
            return DefWindowProcA(hWnd, Msg, wParam, lParam);
        }
    }
}

// ============================================================
//  WorkerWndProc  (0x18F7A70 - 0x18F7AD4)  -- the main window's wndproc
//  [NOT byte-identical] placement-dependent (relative call to sub_18F7460)
//  and refs the high-VA globals g_dword_2122F18 (0x02122F18) and the
//  DefWindowProcA IAT slot (0x01AD53DC).
//
//  LRESULT CALLBACK WorkerWndProc(HWND, UINT, WPARAM, LPARAM)
//
//  WM_CREATE: store the CREATESTRUCT lpCreateParams (the worker) in
//  g_dword_2122F18, then fall through. WM_GETMINMAXINFO (0x24): write
//  0x7FFFFFFF to [lParam+0x20] and [lParam+0x24] (the MINMAXINFO
//  ptMaxTrackSize x/y) and return 0 -- the original allows the window to
//  grow without a max track size. Otherwise, while the stored target is
//  alive and its pump state (+0x22C) is 2, forward (hWnd, msg, wParam, lParam)
//  to DispatchWorkerMessage (sub_18F7460); else DefWindowProcA.
// ============================================================
static LRESULT CALLBACK WorkerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CREATE)
    {
        g_dword_2122F18 = *(int *)lParam;   // CREATESTRUCT->lpCreateParams
    }

    if (msg == WM_GETMINMAXINFO)
    {
        *(int *)((char *)lParam + 0x20) = 0x7FFFFFFF;
        *(int *)((char *)lParam + 0x24) = 0x7FFFFFFF;
        return 0;
    }

    if (g_dword_2122F18 != 0 && *(int *)((char *)g_dword_2122F18 + 0x22C) == 2)
        return DispatchWorkerMessage(hWnd, msg, wParam, lParam);

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// ============================================================
//  CreateWorkerWindow  (0x18F7AE0 - 0x18F7C48)  -- register the window class
//  and create the worker's main window
//  [NOT byte-identical] placement-dependent (relative calls to LoadCursorA /
//  GetStockObject / LoadIconA / RegisterClassExA / AdjustWindowRect /
//  CreateWindowExA / SetTimer IAT slots) and refs the high-VA default class
//  name ("Str1", 0x01BD44D7).
//
//  int CreateWorkerWindow(CWorkerWin32 *obj)   -- __thiscall (worker in ECX)
//
//  RegisterClassExA (class name from the worker's shared block at +596, or
//  the default; WorkerWndProc; arrow cursor; white brush; application icon),
//  AdjustWindowRect the client size (width/height at +580/+584) with style
//  0xCA0000 | (fullscreen ? 0x7F3E0000 : 0), CreateWindowExA it (lpParam =
//  the worker, CW_USEDEFAULT position), store the handle in +568, and start
//  the 1-second id-0x1000 timer. Returns hwnd != 0.
// ============================================================
static int CreateWorkerWindow(CWorkerWin32 *obj)
{
    const char *className;
    void *block = obj->sharedBlock;   // +596 (0x254)
    className = (block != 0) ? *(const char **)block : g_DefaultClassName;

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = 48;
    wc.style = 0;
    wc.lpfnWndProc = WorkerWndProc;   // sub_18F7A70
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = (HINSTANCE)obj->hInstance;
    wc.hCursor = LoadCursorA(0, (LPCSTR)0x7F00);          // IDC_ARROW
    wc.hbrBackground = (HBRUSH)GetStockObject(4);         // WHITE_BRUSH
    wc.lpszMenuName = 0;
    wc.lpszClassName = className;
    wc.hIconSm = LoadIconA((HINSTANCE)obj->hInstance, (LPCSTR)0x65);   // IDI_APPLICATION
    wc.hIcon   = LoadIconA((HINSTANCE)obj->hInstance, (LPCSTR)0x65);
    RegisterClassExA(&wc);

    DWORD style = 0xCA0000 | ((obj->windowFlags & 0xFF) ? 0x7F3E0000 : 0);
    RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = obj->windowWidth;
    rect.bottom = obj->windowHeight;
    AdjustWindowRect(&rect, style, FALSE);

    HWND hwnd = CreateWindowExA(
        0, className, className, style,
        0x80000000 /*CW_USEDEFAULT*/, 0,
        rect.right - rect.left, rect.bottom - rect.top,
        0, 0, (HINSTANCE)obj->hInstance, obj);
    obj->hwnd = hwnd;
    if (hwnd != 0)
        SetTimer(hwnd, 0x1000, 0x3E8, 0);
    return hwnd != 0;
}

// ============================================================
//  SetHWndParent  (0x18F1AE0 - 0x18F1B21)  -- record the parent window for
//  device creation and seed the device-slot state
//  [NOT byte-identical] refs the high-VA globals g_hWndParent (0x02075B28),
//  g_dword_1E2ED6C / g_dword_1E2ED70 (0x01E2ED6C / 0x01E2ED70), g_stru_20FCA50
//  (0x020FCA50), g_dword_1DB1220 / g_unk_1DB12B4 (0x01DB1220 / 0x01DB12B4).
//
//  void SetHWndParent(HWND hwnd)   -- __cdecl
// ============================================================
static void SetHWndParent(HWND hwnd)
{
    g_hWndParent = hwnd;
    g_dword_1E2ED6C = -1;
    g_dword_1E2ED70 = GetCurrentThreadId();
    F3EnterDeviceSlot();
    g_dword_1DB1220 = g_unk_1DB12B4;
    LeaveCriticalSection(&g_stru_20FCA50);
}

// ============================================================
//  InitDisplayMode  (0x55D850 - ...)  -- seed a default display-mode request
//  (the 800x600x32@60 fallback)
//  [NOT byte-identical] none -- no absolute refs, but it is only 44 bytes of
//  stores and stays a plain C function; see the original for the exact body.
//
//  F3DisplayMode * InitDisplayMode(F3DisplayMode *dm)   -- returns dm
// ============================================================
static F3DisplayMode *InitDisplayMode(F3DisplayMode *dm)
{
    memset(dm, 0, sizeof(*dm));
    dm->width  = 800;
    dm->height = 600;
    dm->f12 = 32;
    dm->f16 = 24;
    dm->f20 = 8;
    dm->f24 = 60;
    dm->f32 = 100;
    dm->f36 = 1;
    return dm;
}

// ============================================================
//  [STUB]  TrySetDisplayMode  (0x18F28C0 - ...)  -- try to make the requested
//  display mode current
//
//  int TrySetDisplayMode(const F3DisplayMode *dm)
//
//  Original semantics (under the device critical section stru_20FC948): 0 =
//  the requested mode is / became current, 1 = the mode was changed, 2 = the
//  restore-to-current failed. Deep display-mode subsystem (sub_18F1510 /
//  ResetXloDevice (0x18F2310) / sub_18EFF00 / sub_18F21A0 / sub_18F05D0 / sub_18F1B30); out
//  of scope here. Returns 0 (mode already current) so callers take the
//  success path.
// ============================================================
static int TrySetDisplayMode(const F3DisplayMode *dm) { (void)dm; return 0; }

// ============================================================
//  GetDisplayModeClass  (sub_18EFF00, 0x18EFF00-0x18EFF49)  -- map a
//  render-mode-class tier (1..3) to the device-class code actually requested
//  from the XLO device, gated by the capability byte byte_1E2EDCC.
//
//  int GetDisplayModeClass(int tier)   -- __usercall (tier in EAX)
//
//    tier 1 -> 1
//    tier 2 -> (byte_1E2EDCC & 2) ? 2 : 1
//    tier 3 -> (byte_1E2EDCC & 4) ? 4 : 1
//    else   -> 0x80000000
//
//  [NOT byte-identical] refs the high-VA global byte_1E2EDCC. The __usercall
//  (arg in EAX) collapses to a plain int arg here.
// ============================================================
static int GetDisplayModeClass(int tier)
{
    if (tier == 1)
        return 1;
    if (tier == 2)
        return (g_byte_1E2EDCC & 2) ? 2 : 1;
    if (tier == 3)
        return (g_byte_1E2EDCC & 4) ? 4 : 1;
    return 0x80000000;
}

// d3d9.dll import (bound through the generated d3d9.lib): the D3D9 factory
// the original pulls to (re)create the adapter object. Returns an
// IDirect3D9* (a vtable-backed object) or NULL.
extern "C" void *Direct3DCreate9(UINT SDKVersion);

// ============================================================
//  [STUB]  F3EnumAdapterModes  (sub_18F1FC0)  -- enumerate the selected
//  adapter's display modes into a heap list
//
//  void *F3EnumAdapterModes(void *container, DWORD adapterId, int flags)
//
//  Original: thiscall (the 20-byte local `container` in ECX, adapterId and
//  flags on the stack, caller cleans 8). Builds a per-adapter mode list from
//  the D3D9 object (dword_1E2EEF8) using the rdata adapter table at
//  unk_1BD3020 and returns the list head. The D3D9/XLO enumeration is out of
//  scope; the container is left unpopulated so the caller's free is a no-op.
// ============================================================
static void *F3EnumAdapterModes(void *container, DWORD adapterId, int flags)
{
    (void)container; (void)adapterId; (void)flags;
    return 0;
}

// ============================================================
//  [STUB]  F3ReserveModeList  (sub_563AE0)  -- size the shared mode buffer
//  to hold the enumerated-mode list
//
//  void F3ReserveModeList(void *list)
//
//  Original: stdcall (list pointer on the stack); grows the shared mode
//  buffer (dword_20FC978 / dword_20FC97C / dword_20FC980) to fit the list's
//  element count (list+8 .. list+12). Out of scope here (no-op).
// ============================================================
static void F3ReserveModeList(void *list) { (void)list; }

// ============================================================
//  [STUB]  F3DestroyModeListContainer  (sub_563770)  -- destroy the local
//  20-byte mode-list container built by F3EnumAdapterModes
//
//  void F3DestroyModeListContainer(void *container)
//
//  Original: thiscall (container in ECX); releases container[2] through the
//  operator-delete thunk and zeroes words 2..4. Out of scope here (no-op).
// ============================================================
static void F3DestroyModeListContainer(void *container) { (void)container; }

// ============================================================
//  [STUB]  F3InitAdapterState  (sub_18F1150)  -- initialize the adapter state
//
//  void F3InitAdapterState(void)
//
//  Original: seeds a one-word state and calls sub_1887790(dword_1E2EDBC, &w)
//  (dword_1D9E184 = the result). Out of scope here (no-op).
// ============================================================
static void F3InitAdapterState(void) {}

// ============================================================
//  CreateD3D9DisplayObject  (0x18F21A0 - 0x18F230F)  -- (re)create the D3D9
//  adapter object and query the selected adapter
//
//  bool CreateD3D9DisplayObject(int adapterIndex, DWORD *outBehavior, int flags)
//      (original is a __usercall: adapterIndex in ECX, outBehavior in EDI,
//       flags as the first stack arg; returns a bool in AL)
//
//  Releases any live D3D9 object (dword_1E2EEF8, vtable[2]), then loops:
//  create a new object (Direct3DCreate9Ex(0x20) when (flags & 0x10) and the
//  symbol resolves from d3d9.dll, else Direct3DCreate9(0x20)) and store it in
//  dword_1E2EEF8; a NULL object breaks out (returns false). On a live object
//  set *outBehavior = (flags & 2) ? 2 : 1, clamp adapterIndex to
//  GetAdapterCount()-1 (vtable[4]), and run the adapter query (vtable[14],
//  offset 0x38) into the 16-byte identifier block g_dword_1E2EDB8:
//    - S_OK: enumerate the adapter modes (F3EnumAdapterModes / sub_18F1FC0)
//      into a 20-byte local container, free the returned list (sub_563AE0),
//      destroy the container (sub_563770), init the adapter state
//      (sub_18F1150) and return true (the object is kept alive).
//    - D3DERR_NOTAVAILABLE (0x8876086A) with (flags & 4): force *outBehavior
//      to 2 and re-run the query once, returning whether it now succeeds
//      (object kept alive).
//    - otherwise: release the object (dword_1E2EEF8 = 0) and, when (flags & 1)
//      is clear, break (return false); else Sleep(0x64) and retry.
//
//  [NOT byte-identical] refs the high-VA device globals dword_1E2EEF8 /
//  g_dword_1E2EDB8 (0x01E2EEF8 / 0x01E2EDB8) and dispatches into the deep D3D9
//  / XLO mode-enumeration subsystem (sub_18F1FC0 / sub_563AE0 / sub_563770 /
//  sub_18F1150), which is stubbed out.
// ============================================================
static bool CreateD3D9DisplayObject(int adapterIndex, DWORD *outBehavior, int flags)
{
    // Release any live D3D9 object (vtable[2] = Release).
    if (g_dword_1E2EEF8 != 0)
    {
        void **vtable = *(void ***)g_dword_1E2EEF8;
        typedef void (__stdcall *F3D3D9ReleaseFn)(DWORD);
        ((F3D3D9ReleaseFn)vtable[2])(g_dword_1E2EEF8);
        g_dword_1E2EEF8 = 0;
    }

    const int useEx = (flags & 0x10) != 0;   // constant across the loop
    for (;;)
    {
        DWORD d3d = 0;
        if (useEx)
        {
            HMODULE h = GetModuleHandleA("d3d9.dll");
            FARPROC p = (h != 0) ? (FARPROC)GetProcAddress(h, "Direct3DCreate9Ex") : 0;
            if (p != 0)
            {
                typedef int (__stdcall *F3D3D9Create9ExFn)(UINT, void **);
                ((F3D3D9Create9ExFn)p)(0x20u, (void **)&d3d);
            }
        }
        if (d3d == 0)
            d3d = (DWORD)Direct3DCreate9(0x20u);
        g_dword_1E2EEF8 = d3d;
        if (d3d == 0)
            break;

        *outBehavior = ((flags & 2) != 0) + 1;   // (flags & 2) ? 2 : 1

        void **vtable = *(void ***)d3d;
        typedef DWORD (__stdcall *F3D3D9GetCountFn)(DWORD);
        DWORD maxAdapter = ((F3D3D9GetCountFn)vtable[4])(d3d) - 1;
        if ((DWORD)adapterIndex >= maxAdapter)
            adapterIndex = (int)maxAdapter;

        typedef int (__stdcall *F3D3D9GetIdFn)(DWORD, int, DWORD, void *);
        F3D3D9GetIdFn query = (F3D3D9GetIdFn)vtable[14];
        int hr = query(d3d, adapterIndex, *outBehavior, &g_dword_1E2EDB8[0]);

        if (hr == 0)   // S_OK: enumerate the modes and keep the object live.
        {
            DWORD modeList[5];   // the 20-byte local container (var_1C)
            void *modes = F3EnumAdapterModes(modeList, g_dword_1E2EDB8[1], flags);
            F3ReserveModeList(modes);
            F3DestroyModeListContainer(modeList);
            F3InitAdapterState();
            return true;
        }
        if (hr == 0x8876086A /* D3DERR_NOTAVAILABLE */ && (flags & 4) != 0)
        {
            // Retry the query once with behavior 2 (object kept live).
            *outBehavior = 2;
            return (query(d3d, adapterIndex, 2, &g_dword_1E2EDB8[0]) == 0);
        }
        // Other failure: release the object, then retry after a short sleep
        // when (flags & 1) is set, else give up.
        typedef void (__stdcall *F3D3D9ReleaseFn2)(DWORD);
        ((F3D3D9ReleaseFn2)vtable[2])(d3d);
        g_dword_1E2EEF8 = 0;
        if ((flags & 1) == 0)
            break;
        Sleep(0x64u);
    }
    return false;
}

// ============================================================
//  [STUB]  BuildXloDeviceFromRequest  (sub_18F1B30 + its query/pick helpers,
//  0x18F1B30-0x18F1E60)  -- build the XLO device from the display-mode request
//
//  int BuildXloDeviceFromRequest(const F3DisplayMode *dm)   -- 1 = success
//
//  Original (only reached once CreateD3D9DisplayObject has produced a live
//  D3D9 object in dword_1E2EEF8): query the adapter's display-mode descriptor
//  (device vtable slot 8, offset 0x20) into a 16-byte out block, walk the
//  texture-source-descriptor table (dword_1D9AF60, 0x70-byte rows) for the row
//  whose key matches the descriptor, pick the closest matching mode
//  (sub_18F05D0), assemble the 56-byte display-mode request, and build the
//  device (sub_18F1B30) -- returning its build status. The D3D9 / XLO
//  device-rebuild subsystem is out of scope. Returns 1 (build succeeded).
// ============================================================
static int BuildXloDeviceFromRequest(const F3DisplayMode *dm)
{
    (void)dm;
    return 1;
}

// ============================================================
//  SetDisplayMode  (0x18F2B30 - 0x18F2D66)  -- set the display mode
//
//  int SetDisplayMode(const F3DisplayMode *dm)   -- 0 = success, nonzero = error
//
//  Original semantics (nested under the device CS stru_20FC948 already held by
//  the caller): if the device object (dword_1E2EEF4) is present, copy the
//  56-byte caps block (dword_1E2ED80), overwrite width/height from the request,
//  force caps[8]=1 and caps[12]=0, and set the render-mode class (caps[13])
//  from byte 20 / dword 24 via GetDisplayModeClass (sub_18EFF00), or
//  0x80000000 when byte 20 is 0; reset the XLO device (ResetXloDevice) and
//  return 0 on success, or restore the original caps (second ResetXloDevice)
//  and return 1, or return 2 if both fail. If the device is absent, create the
//  D3D9 device object (CreateD3D9DisplayObject / sub_18F21A0); on success build
//  the device from the request's picked mode (BuildXloDeviceFromRequest /
//  sub_18F1B30) and return its build status, else return 1 (no device).
//
//  [NOT byte-identical] refs the high-VA device globals and the deep D3D9 / XLO
//  device-rebuild subsystem, which is stubbed out.
// ============================================================
static int SetDisplayMode(const F3DisplayMode *dm)
{
    F3EnterDisplayCs();
    if (g_dword_1E2EEF4 != 0)
    {
        // Device present: rebuild the XLO device with the requested size and
        // render-mode class, restoring the original caps if that fails.
        DWORD caps[14];
        memcpy(caps, g_dword_1E2ED80, sizeof(caps));
        caps[0]  = (DWORD)dm->width;             // byte offset 0
        caps[1]  = (DWORD)dm->height;            // byte offset 4
        caps[8]  = 1;                            // byte offset 32
        caps[12] = 0;                            // byte offset 48
        caps[13] = (dm->f20 != 0) ? (DWORD)GetDisplayModeClass(dm->f24)
                                  : 0x80000000;   // byte offset 52
        if (ResetXloDevice(caps) != 0)
        {
            LeaveCriticalSection(&g_stru_20FC948);
            return 0;
        }
        if (ResetXloDevice(g_dword_1E2ED80) != 0)
        {
            LeaveCriticalSection(&g_stru_20FC948);
            return 1;
        }
        LeaveCriticalSection(&g_stru_20FC948);
        return 2;
    }
    else
    {
        // No device yet: create the D3D9 object and, on success, build the
        // device from the request. Without a live D3D9 object this reports the
        // "no device" status (1).
        int v5 = 1;
        DWORD behavior = 1;   // the original seeds the out behavior to 1
        if (CreateD3D9DisplayObject(dm->f00, &behavior, dm->f28) != 0)
            v5 = (BuildXloDeviceFromRequest(dm) == 0);
        LeaveCriticalSection(&g_stru_20FC948);
        return v5;
    }
}

// ============================================================
//  ResizeWorkerWindow  (0x18F6A70 - 0x18F6B77)  -- resize the main window so
//  its client area matches the worker's requested width/height
//  [NOT byte-identical] placement-dependent (relative calls to the
//  GetClientRect / GetWindowRect / AdjustWindowRect / MoveWindow IAT slots).
//
//  int ResizeWorkerWindow(CWorkerWin32 *obj)   -- __thiscall (worker in ECX)
//
//  If the client area already equals (windowWidth, windowHeight) at
//  +580/+584, no-op (the original returns the client height in eax in that
//  case). Otherwise keep the window's top-left corner and MoveWindow it to
//  the adjusted size for style 0xCA0000 | (fullscreen ? 0x7F3E0000 : 0).
//  Returns the MoveWindow result.
// ============================================================
static int ResizeWorkerWindow(CWorkerWin32 *obj)
{
    RECT client;
    GetClientRect(obj->hwnd, &client);
    int cw = client.right - client.left;
    int ch = client.bottom - client.top;
    if (cw == obj->windowWidth && ch == obj->windowHeight)
        return ch;   // original returns the eax holding the client height

    RECT rect;
    GetWindowRect(obj->hwnd, &rect);
    int x = rect.left;
    int y = rect.top;
    rect.right = x + obj->windowWidth;
    rect.bottom = y + obj->windowHeight;
    DWORD style = 0xCA0000 | ((obj->windowFlags & 0xFF) ? 0x7F3E0000 : 0);
    AdjustWindowRect(&rect, style, FALSE);
    rect.bottom += y - rect.top;
    rect.right  += x - rect.left;
    rect.left = x;
    rect.top = y;
    return MoveWindow(obj->hwnd, x, y, rect.right - x, rect.bottom - y, TRUE);
}

// ============================================================
//  [STUB]  ApplyDisplaySettings  (0x18F7E60 - ...)  -- apply / restore the
//  system display settings on a windowed <-> fullscreen transition
//
//  void __stdcall ApplyDisplaySettings(int windowed)
//
//  Deep system-settings path (SystemParametersInfoA + accessibility-settings
//  save/load + locale strings); out of scope here. No-op.
// ============================================================
static void __stdcall ApplyDisplaySettings(int windowed) { (void)windowed; }

//  The manager's GetWindow virtual method (vtable slot 8, 0x20): the manager
//  window used for error boxes (see EnsureWorkerWindow's failure path).
typedef void *(__thiscall *F3ThiscallFn0)(void *);
static void *F3ManagerGetWindow(void *mgr)
{
    void **vtable = *(void ***)mgr;
    return ((F3ThiscallFn0)vtable[8])(mgr);
}

// ============================================================
//  EnsureWorkerWindow  (0x18F82E0 - 0x18F852D)  -- create / restore the
//  worker window (the "window state 1" step of PumpWorker)
//  [NOT byte-identical] placement-dependent (relative calls to
//  CreateWorkerWindow / SetHWndParent / InitDisplayMode / TrySetDisplayMode /
//  SetDisplayMode / ResizeWorkerWindow / ApplyDisplaySettings and the
//  SetWindowLongA / SetWindowPos / MessageBoxW / PostMessageA / ShowWindow /
//  UpdateWindow IAT slots) and refs the high-VA locale-key globals
//  g_dword_20FC8EC / g_dword_20FC8E8.
//
//  int EnsureWorkerWindow(CWorkerWin32 *obj)   -- __thiscall (worker in ECX)
//
//  Creates the main window (CreateWorkerWindow into +568) and returns 0 if
//  that fails. Otherwise records the parent window (SetHWndParent), then the
//  display-mode dance: if the fullscreen byte (+0x24C) is set, seed a default
//  mode (InitDisplayMode) from the worker's cached fields (+576/+580/+584,
//  +0x24D, +592, 32) and TrySetDisplayMode it; on failure retry at 800x600,
//  and if that also fails drop out of fullscreen (clear +0x24C, swap the
//  wndproc to the class procedure, clear GWLP_HINSTANCE, SetWindowPos
//  HWND_TOP SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW, ResizeWorkerWindow); on
//  success store 800x600 back. If the fullscreen byte is (now) clear, run
//  SetDisplayMode with a second request ({+576, +580, +584, 24, 8, +0x24D,
//  1, 32}). If that fails, show an error MessageBoxW on the manager window
//  (locale keys g_dword_20FC8EC / g_dword_20FC8E8, type 0x10) and post
//  WM_CLOSE (0x10, wParam 1) to it. Then ApplyDisplaySettings(!fullscreen),
//  ShowWindow(hwnd, nShowCmd), UpdateWindow, invoke the +600 callback if set
//  (its result becomes the return), and set the shown/ready bytes at
//  +648/+649.
// ============================================================
static int EnsureWorkerWindow(CWorkerWin32 *obj)
{
    int result;
    int ok;
    F3DisplayMode dm;

    if (!CreateWorkerWindow(obj))
        return 0;

    SetHWndParent(obj->hwnd);

    if (obj->windowFlags & 0xFF)   // +0x24C fullscreen byte
    {
        InitDisplayMode(&dm);
        dm.f00  = obj->f_0240;
        dm.width  = obj->windowWidth;
        dm.height = obj->windowHeight;
        ((char *)&dm.f28)[1] = (char)(obj->windowFlags >> 8);   // +0x24D byte
        dm.f36  = obj->f_0250;
        dm.f40  = 32;
        ok = TrySetDisplayMode(&dm);
        if (ok != 0)
        {
            dm.width = 800;
            dm.height = 600;
            ok = TrySetDisplayMode(&dm);
            if (ok != 0)
            {
                obj->windowFlags &= 0xFF00;   // clear the +0x24C fullscreen byte
                // The original installs the class procedure at 0xCA0000 here;
                // keep the module's own wndproc (sub_18F7A70) in this build.
                SetWindowLongA(obj->hwnd, GWLP_WNDPROC, (LONG)WorkerWndProc);
                SetWindowLongA(obj->hwnd, GWLP_HINSTANCE, 0);
                SetWindowPos(obj->hwnd, HWND_TOP, 0, 0, 0, 0, 0x23u /*SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW*/);
                ResizeWorkerWindow(obj);
            }
            else
            {
                obj->windowWidth = dm.width;
                obj->windowHeight = dm.height;
            }
        }
        if ((obj->windowFlags & 0xFF) != 0)
            goto shown;
    }

    dm.f00  = obj->f_0240;
    dm.width  = obj->windowWidth;
    dm.height = obj->windowHeight;
    dm.f12  = 24;
    dm.f16  = 8;
    dm.f20  = (unsigned char)(obj->windowFlags >> 8);   // +0x24D byte
    dm.f24  = 1;
    dm.f28  = 32;
    ok = SetDisplayMode(&dm);

shown:
    if (ok != 0)
    {
        void *mgr = GetManager();
        const WCHAR *caption = GetLocalizedString((const WCHAR ***)GetLocaleEntry(g_dword_20FC8EC));
        const WCHAR *text    = GetLocalizedString((const WCHAR ***)GetLocaleEntry(g_dword_20FC8E8));
        HWND mhwnd = (HWND)F3ManagerGetWindow(mgr);
        MessageBoxW(mhwnd, text, caption, 0x10u /*MB_ICONSTOP*/);
        PostMessageA((HWND)F3ManagerGetWindow(mgr), 0x10u /*WM_CLOSE*/, 1u, 0);
    }

    ApplyDisplaySettings((obj->windowFlags & 0xFF) == 0);

    ShowWindow(obj->hwnd, obj->nShowCmd);
    UpdateWindow(obj->hwnd);

    result = 0;
    if (obj->onCreateDevice != 0)
    {
        result = obj->onCreateDevice();
    }

    obj->shown = 1;
    obj->ready = 1;
    return result;
}

// ============================================================
//  [STUB]  HandleDisplayModeChange  (0x18F8060 - 0x18F82D6)  -- apply a
//  pending display-mode change (the request stashed by sub_18F6BA0 at
//  +0x274..+0x27D)
//
//  int __thiscall HandleDisplayModeChange(CWorkerWin32 *obj)   -- worker in ECX
//
//  Called from PumpWorkerMessages under the worker CS (nested entry) when
//  the display-mode-pending flag (+0x281) is set. Applies the requested mode
//  (the TrySetDisplayMode fullscreen path when +0x27C is set, else the
//  SetDisplayMode windowed path); on success it calls the +0x26C callback,
//  ApplyDisplaySettings(!windowed), ResizeWorkerWindow, ShowWindow(hwnd,
//  SW_SHOW), updates the manager's display record (dword_20FC8C4 pair) via
//  its vtable, clears the pending/done flags, and SetEvents event4 (+0x228).
//  Returns 0 (unhandled) for now.
// ============================================================
static int HandleDisplayModeChange(CWorkerWin32 *obj) { (void)obj; return 0; }

// ============================================================
//  PumpWorkerMessages  (0x18F8530 - 0x18F85FB)  -- drain the worker's
//  pending message queue (the "message state 2" step of PumpWorker)
//  [NOT byte-identical] placement-dependent (relative calls to
//  HandleDisplayModeChange and __security_check_cookie) and refs the
//  Enter/LeaveCriticalSection / PeekMessageA / TranslateMessage /
//  DispatchMessageA IAT slots at their original high VAs.
//
//  void PumpWorkerMessages(CWorkerWin32 *obj)   -- thiscall (worker in EDI)
//
//  Runs with the worker CS held by PumpWorker and exits holding one
//  reference. First peeks the queue (the worker window, +568) outside the
//  CS. For each message (PeekMessageA, PM_REMOVE): if the pre-translate hook
//  (+624, the XLive one) is absent or returns 0, TranslateMessage, leave the
//  CS, DispatchMessageA, re-enter the CS; if a display-mode change is
//  pending (+641), re-enter the CS (nested) and run
//  HandleDisplayModeChange under it. Repeats until the queue is empty --
//  the final peek + enter leaves the CS held.
// ============================================================
static void PumpWorkerMessages(CWorkerWin32 *obj)
{
    MSG msg;
    int hasMsg;

    LeaveCriticalSection(&obj->cs);
    hasMsg = PeekMessageA(&msg, obj->hwnd, 0, 0, 1);
    EnterCriticalSection(&obj->cs);

    while (hasMsg)
    {
        if (obj->preTranslate == 0 || obj->preTranslate(&msg) == 0)
        {
            TranslateMessage(&msg);
            LeaveCriticalSection(&obj->cs);
            DispatchMessageA(&msg);
            EnterCriticalSection(&obj->cs);

            if (obj->displayModePending != 0)
            {
                EnterCriticalSection(&obj->cs);
                HandleDisplayModeChange(obj);
                LeaveCriticalSection(&obj->cs);
            }
        }

        LeaveCriticalSection(&obj->cs);
        hasMsg = PeekMessageA(&msg, obj->hwnd, 0, 0, 1);
        EnterCriticalSection(&obj->cs);
    }
    // Exits holding the worker CS (one reference).
}

// ============================================================
//  nvapi.dll interface subsystem state (0x1E2C / 0x1D94)  -- the init
//  (sub_17D1D10) loads nvapi.dll, takes nvapi_QueryInterface, and resolves the
//  before/after hooks; the unload (NvApiTeardownUnload) clears the table and
//  frees the library. The original keeps these at the VAs noted; here they
//  live as static globals in this module.
// ============================================================
static HMODULE g_hLibModule = 0;                                // 0x1E2C704  nvapi.dll module handle (hLibModule)
static int (__cdecl *g_dword_1E2C708)(DWORD) = 0;               // 0x1E2C708  nvapi_QueryInterface resolver
static int (__cdecl *g_dword_1E2C70C)(DWORD, int *) = 0;        // 0x1E2C70C  before-call hook
static int (__cdecl *g_dword_1E2C710)(DWORD, int, int, int) = 0; // 0x1E2C710  after-call hook

//  178-entry resolved-interface table (0x1D941F4 - 0x1D94A08), 12 bytes per
//  slot: resolved entry pointer at +0, resolved flag byte at +4. Slot 112 is
//  dword_1D94644 / byte_1D94648 (NVAPI interface 0x2EC50C2B).
struct F3NvApiSlot
{
    int (__cdecl *entry)(void);   // +0  resolved interface entry
    unsigned char resolved;       // +4  slot-resolved flag
    char pad[7];                  // +5  untouched by the game
};
static F3NvApiSlot g_dword_1D941F4[178];                         // 0x1D941F4  resolved-interface table

// ============================================================
//  NvApiTeardownFlush  (0x17D6510 - 0x17D65BF)  -- device teardown flush
//  (NVAPI interface 0x2EC50C2B)
//  [NOT byte-identical] placement-independent body, but refs the nvapi.dll
//  interface globals (dword_1E2C708 / dword_1E2C70C / dword_1E2C710) and the
//  interface-table slot (dword_1D94644 / byte_1D94648) at their original
//  high VAs.
//
//  int NvApiTeardownFlush(void)   -- no args
//
//  Lazily resolves NVAPI interface 0x2EC50C2B through the nvapi_QueryInterface
//  resolver (dword_1E2C708) when the slot is empty and unresolved, caching the
//  entry in the slot and setting the flag (even if the resolver returns NULL).
//  Returns -3 if no entry is available. Otherwise runs the resolved entry,
//  bracketed by the before-hook dword_1E2C70C(0x2EC50C2B, &pre) and the
//  after-hook dword_1E2C710(0x2EC50C2B, pre, 0, result), and returns the
//  entry's result. Called on the device-teardown path (ShutdownDevice) just
//  before the nvapi.dll library is released (NvApiTeardownUnload).
// ============================================================
static int NvApiTeardownFlush(void)
{
    int pre = 0;
    int result;
    F3NvApiSlot *slot = &g_dword_1D941F4[112];   // 0x1D94644 / 0x1D94648, interface 0x2EC50C2B

    if (g_dword_1E2C708 != 0 && slot->entry == 0 && slot->resolved == 0)
    {
        slot->entry = (int (__cdecl *)(void))g_dword_1E2C708(0x2EC50C2B);
        slot->resolved = 1;
    }
    if (slot->entry == 0)
        return -3;
    if (g_dword_1E2C70C != 0)
        g_dword_1E2C70C(0x2EC50C2B, &pre);
    result = slot->entry();
    if (g_dword_1E2C710 != 0)
        g_dword_1E2C710(0x2EC50C2B, pre, 0, result);
    return result;
}

// ============================================================
//  NvApiTeardownUnload  (0x17D1E60 - 0x17D1F31)  -- release the device library
//  [NOT byte-identical] placement-dependent (long jumps to the common exit)
//  and refs hLibModule (0x01E2C704), the nvapi globals (dword_1E2C708 /
//  dword_1E2C70C / dword_1E2C710), the interface table (0x01D941F4), and the
//  FreeLibrary IAT slot (0x01AD5154) at their original high VAs.
//
//  int NvApiTeardownUnload(void)   -- no args
//
//  Resolves the teardown entry (interface 0xD22BDD7E) through the
//  nvapi_QueryInterface resolver and runs it; returns -4 if the library or
//  resolver is gone, -3 if the entry will not resolve, and -1 if the entry
//  reports failure. On success clears the resolver, zeroes every slot of the
//  178-entry resolved-interface table (entry pointer + flag byte),
//  FreeLibrary(hLibModule), and clears hLibModule plus the before/after hooks
//  (dword_1E2C70C / dword_1E2C710). Returns 0 on success.
// ============================================================
static int NvApiTeardownUnload(void)
{
    int i;
    int (__cdecl *teardown)(void);

    if (g_hLibModule == 0 || g_dword_1E2C708 == 0)
        return -4;

    teardown = (int (__cdecl *)(void))g_dword_1E2C708(0xD22BDD7E);
    if (teardown == 0)
        return -3;
    if (teardown() != 0)
        return -1;

    g_dword_1E2C708 = 0;
    for (i = 0; i < 178; ++i)
    {
        g_dword_1D941F4[i].entry = 0;
        g_dword_1D941F4[i].resolved = 0;
    }

    FreeLibrary(g_hLibModule);
    g_hLibModule = 0;
    g_dword_1E2C70C = 0;
    g_dword_1E2C710 = 0;
    return 0;
}

// ============================================================
//  NvApiDestroyObject  (0x17D6740 - 0x17D67F5)  -- destroy an NVAPI object
//  (NVAPI interface 0x3A153134)
//  [NOT byte-identical] placement-independent body, but refs the nvapi.dll
//  interface globals (dword_1E2C708 / dword_1E2C70C / dword_1E2C710) and the
//  interface-table slot (dword_1D94668 / byte_1D9466C) at their original
//  high VAs.
//
//  int NvApiDestroyObject(void *obj)   -- one arg (cdecl), returns int
//
//  Lazily resolves NVAPI interface 0x3A153134 through the nvapi_QueryInterface
//  resolver (dword_1E2C708) when the slot is empty and unresolved, caching the
//  entry in the slot and setting the flag (even if the resolver returns
//  NULL). Returns -3 if no entry is available. Otherwise runs the resolved
//  entry with the object, bracketed by the before-hook
//  dword_1E2C70C(0x3A153134, &pre) and the after-hook
//  dword_1E2C710(0x3A153134, pre, 0, result), and returns the entry's result.
// ============================================================
static int NvApiDestroyObject(void *obj)
{
    int pre = 0;
    int result;
    F3NvApiSlot *slot = (F3NvApiSlot *)((char *)g_dword_1D941F4 + (0x1D94668 - 0x1D941F4));

    if (g_dword_1E2C708 != 0 && slot->entry == 0 && slot->resolved == 0)
    {
        slot->entry = (int (__cdecl *)(void))g_dword_1E2C708(0x3A153134);
        slot->resolved = 1;
    }
    if (slot->entry == 0)
        return -3;
    if (g_dword_1E2C70C != 0)
        g_dword_1E2C70C(0x3A153134, &pre);
    result = ((int (__cdecl *)(void *))slot->entry)(obj);
    if (g_dword_1E2C710 != 0)
        g_dword_1E2C710(0x3A153134, pre, 0, result);
    return result;
}

// 0x2075F10: the current per-device slot index (which XLO device slot).
static DWORD g_dword_2075F10 = 0;

// 0x1E2EEF4 (the current XLO device object) is defined earlier, ahead of
// IsDisplayReady.

// 0x2075F00: the pending device flush-request object -- a 20-byte struct
// (three floats, a wrapped NVAPI resource at +12, three flag bytes at
// +16..18). Set by the device init (CreateXloDevice, sub_18F1B30), freed by DestroyDevice.
static void *g_dword_2075F00 = 0;

// 0x2075F14: a vtable-backed device object released by ReleaseDeviceObjects.
static void *g_dword_2075F14 = 0;

// 0x20FCA68: the per-slot device record table. Each slot row is 176 bytes
// (44 DWORDs); slot i occupies DWORDs [i*44, i*44+44). Sized generously for the
// slot count (bounds unknown in the original).
static DWORD g_dword_20FCA68[44 * 64];

// 0x20FCAE8: the per-slot base-device table. Each slot row is 176 bytes
// (44 DWORDs); row DWORD 1 is the base device object pointer (also the
// occupied flag) and row DWORD 2 is a companion pointer.
static DWORD g_dword_20FCAE8[44 * 64];

// 0x20FCB08: the per-slot client-rect table. Each slot row is 176 bytes
// (44 DWORDs); row DWORDs 0-3 hold {left, top, right, bottom}.
static DWORD g_dword_20FCB08[44 * 64];

// ============================================================
//  [STUB]  sub_18F0850  (0x18F0850 - 0x18F08C7)  -- device teardown (per-slot)
//
//  Releases the base per-slot device object for the current slot
//  (dword_2075F10) from the 0x20FCA68 record table and calls sub_18F07B0().
//  Out of scope here; reached as the fall-through of TeardownDeviceSlot.
// ============================================================
static int sub_18F0850(void) { return 0; }

// ============================================================
//  TeardownDeviceSlot  (0x18F0E00 - 0x18F0E9B)  -- device teardown (per-slot)
//  [NOT byte-identical] placement-dependent (relative fall-through to
//  sub_18F0850) and refs the slot index (dword_2075F10), the device object
//  (dword_1E2EEF4) and the per-slot record table (0x20FCA68) at their
//  original high VAs.
//
//  int TeardownDeviceSlot(void)   -- no args (cdecl), returns int
//
//  For the current slot (dword_2075F10) walks the three per-slot sub-records
//  whose base DWORDs sit at 8/16/24 within the 44-DWORD slot row. For each
//  occupied sub-record (flag DWORD at row offset 11/19/27) it notifies the
//  device object (dword_1E2EEF4) through its vtable[37] (offset 0x94) as
//  (device, n, 0) where n is 1/2/3, then releases the sub-record object
//  (vtable[2]) and clears the two DWORDs at row offsets (9,10)/(17,18)/(25,26).
//  If the slot's trailer flag (row offset 33) is set, falls through to
//  sub_18F0850(); otherwise returns the last release result (or 0).
// ============================================================
static int TeardownDeviceSlot(void)
{
    int result = 0;
    int n = 1;

    for (int rec = 8; rec < 32; rec += 8, ++n)
    {
        int slot = (int)g_dword_2075F10;
        if (g_dword_20FCA68[slot * 44 + rec + 3] != 0)
        {
            void *dev = g_dword_1E2EEF4;
            void **vt = *(void ***)dev;
            void (__stdcall *notify)(void *, int, int) =
                (void (__stdcall *)(void *, int, int))vt[37];
            notify(dev, n, 0);

            DWORD *record = &g_dword_20FCA68[slot * 44 + rec];
            void *obj = (void *)record[1];
            if (obj != 0)
            {
                void **ovt = *(void ***)obj;
                int (__stdcall *release)(void *) =
                    (int (__stdcall *)(void *))ovt[2];
                result = release(obj);
                record[1] = 0;
                record[2] = 0;
            }
        }
    }

    if (g_dword_20FCA68[g_dword_2075F10 * 44 + 33] != 0)
        return sub_18F0850();
    return result;
}

// ============================================================
//  GetSlotClientRect  (0x18F0710 - 0x18F0785)  -- query the device's client rect
//  [NOT byte-identical] refs the device object (dword_1E2EEF4), the slot index
//  (dword_2075F10) and the per-slot client-rect table (0x20FCB08) at their
//  original high VAs, plus the __security_check_cookie IAT slot.
//
//  int GetSlotClientRect(void)   -- no args (cdecl), returns int
//
//  If the device object (dword_1E2EEF4) is set, queries it through its
//  vtable[48] (offset 0xC0, stdcall with the device pointer first) into a
//  6-DWORD rect out-buffer {x, y, w, h, ?, ?}, then stores the four values
//  {x, y, x + w, y + h} in the current slot's (dword_2075F10) 176-byte rect row
//  (0x20FCB08, DWORDs 0-3). Returns the slot's byte offset (176 * slot), or 0
//  when the device object is not set.
// ============================================================
static int GetSlotClientRect(void)
{
    int result = (int)g_dword_1E2EEF4;
    if (g_dword_1E2EEF4 != 0)
    {
        void **vt = *(void ***)g_dword_1E2EEF4;
        typedef void (__stdcall *RectFn)(void *, DWORD *);
        RectFn fn = (RectFn)vt[48];
        DWORD rect[6] = { 0, 0, 0, 0, 0, 0 };
        fn(g_dword_1E2EEF4, rect);
        DWORD *row = &g_dword_20FCB08[g_dword_2075F10 * 44];
        row[0] = rect[0];
        row[1] = rect[1];
        row[2] = rect[2] + row[0];
        row[3] = rect[3] + row[1];
        result = (int)g_dword_2075F10 * 176;
    }
    return result;
}

// ============================================================
//  TeardownDeviceBase  (0x18F0C10 - 0x18F0C6C)  -- device teardown (per-slot base)
//  [NOT byte-identical] placement-dependent (relative tail-jmp to
//  GetSlotClientRect) and refs the slot index (dword_2075F10), the device
//  object (dword_1E2EEF4) and the per-slot base-device table (0x20FCAE8) at
//  their original high VAs.
//
//  int TeardownDeviceBase(void)   -- no args (cdecl), returns int
//
//  For the current slot (dword_2075F10) releases the base per-slot device: if
//  the slot row's base object (0x20FCAE8 row, DWORD 1) is set, it notifies the
//  device object (dword_1E2EEF4) through its vtable[39] (offset 0x9C) as
//  (device, 0), re-reads the slot (the notify may change it) and, if the base
//  object is still set, releases it (vtable[2], stdcall with the object on the
//  stack) and clears row DWORDs 1 and 2. Tail-calls GetSlotClientRect() and
//  returns its result.
// ============================================================
static int TeardownDeviceBase(void)
{
    DWORD *row = &g_dword_20FCAE8[g_dword_2075F10 * 44];
    if (row[1] != 0)
    {
        void *dev = g_dword_1E2EEF4;
        void **vt = *(void ***)dev;
        void (__stdcall *notify)(void *, int) =
            (void (__stdcall *)(void *, int))vt[39];
        notify(dev, 0);

        row = &g_dword_20FCAE8[g_dword_2075F10 * 44];
        if (row[1] != 0)
        {
            void *obj = (void *)row[1];
            void **ovt = *(void ***)obj;
            int (__stdcall *release)(void *) =
                (int (__stdcall *)(void *))ovt[2];
            release(obj);
            row[1] = 0;
            row[2] = 0;
        }
    }
    return GetSlotClientRect();
}

// ============================================================
//  F3ObjectListNode / F3ObjectList / F3ObjectListContainer  -- the vtable-
//  object linked lists of the device subsystem (container at 0x20FC988).
//
//  A list is a circular doubly-linked list of F3ObjectListNode nodes around a
//  sentinel node (list->tail). head points at the first real node (0 when
//  empty) and count holds the number of real nodes. Each node wraps a
//  vtable-backed object whose release method is vtable[2] (byte offset +8,
//  stdcall with the object on the stack).
// ============================================================
struct F3ObjectListNode
{
    F3ObjectListNode *next;   // +0
    F3ObjectListNode *prev;   // +4
    void *obj;                // +8  wrapped vtable object
};

struct F3ObjectList
{
    F3ObjectListNode *head;   // +0
    DWORD f04[4];             // +4  (untouched)
    F3ObjectListNode *tail;   // +0x14  sentinel node
    DWORD count;              // +0x18  number of real nodes
};

struct F3ObjectListContainer   // 64 bytes
{
    DWORD f00;                // +0x00
    F3ObjectList l04;         // +0x04
    F3ObjectList l20;         // +0x20
    DWORD f3C[3];             // +0x3C
};

// 0x20FC988: the device object-list container (64 bytes; 0xFF-poison in the
// image, initialized by the device subsystem at device creation).
static F3ObjectListContainer g_stru_20FC988;

// ============================================================
//  F3ListRemoveNode  (0x5A4840 - 0x5A48C3)  -- unlink + free one list node
//  [NOT byte-identical] refs the heap table (dword_2075B24) at its original
//  high VA.
//
//  void F3ListRemoveNode(F3ObjectList *list, DWORD *out, F3ObjectListNode *head,
//                        F3ObjectListNode *node)
//
//  Removes `node` from the list (the device code always passes the last real
//  node): if node != the sentinel, splice it out (prev->next = node->next,
//  next->prev = node->prev), LHFree it and decrement the count. (The original
//  is a userpurge function -- its first arg arrives in EDI and the caller
//  cleans the three stack args -- and carries __invalid_parameter_noinfo debug
//  asserts that are no-ops in a healthy build.) out receives
//  { list->head, head }.
// ============================================================
static void F3ListRemoveNode(F3ObjectList *list, DWORD *out,
                             F3ObjectListNode *head, F3ObjectListNode *node)
{
    if (node != list->tail)
    {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        LHFree(node);
        list->count--;
    }
    out[0] = (DWORD)list->head;
    out[1] = (DWORD)head;
}

// ============================================================
//  FreeObjectListContainer  (0x18CB3D0 - 0x18CB50C)  -- free both object
//  lists of the 64-byte container
//  [NOT byte-identical] placement-dependent (relative call to
//  F3ListRemoveNode).
//
//  void __stdcall FreeObjectListContainer(F3ObjectListContainer *container)
//
//  For each of the container's two lists (the +0x20 list first, then the
//  +0x04 list), while the count is non-zero: release the last real node's
//  wrapped object (vtable[2], stdcall with the object on the stack) and
//  remove + free that node (F3ListRemoveNode).
// ============================================================
static void __stdcall FreeObjectListContainer(F3ObjectListContainer *c)
{
    F3ObjectList *lists[2] = { &c->l20, &c->l04 };

    for (int li = 0; li < 2; ++li)
    {
        F3ObjectList *list = lists[li];
        while (list->count > 0)
        {
            F3ObjectListNode *head = list->head;
            F3ObjectListNode *last = list->tail->prev;
            void *obj = last->obj;
            void **ovt = *(void ***)obj;
            void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
            release(obj);

            DWORD out[2] = { 0, 0 };
            F3ListRemoveNode(list, out, head, last);
        }
    }
}

// ============================================================
//  FreeDeviceFlushRequest  (0x5639E0 - 0x563A08)  -- free the 20-byte device
//  flush-request object (dword_2075F00)
//  [NOT byte-identical] placement-dependent (relative call to
//  NvApiDestroyObject) and refs the heap table (dword_2075B24) at its
//  original high VA.
//
//  void *FreeDeviceFlushRequest(void *obj)   -- usercall (obj in ESI),
//  returns obj
//
//  If the request's wrapped NVAPI resource (offset +12) is set, releases it
//  via NvApiDestroyObject and clears the field, then frees the request itself
//  through the game heap (LHFree) and returns the pointer.
// ============================================================
static void *FreeDeviceFlushRequest(void *obj)
{
    void *resource = *(void **)((char *)obj + 12);
    if (resource != 0)
    {
        NvApiDestroyObject(resource);
        *(void **)((char *)obj + 12) = 0;
    }
    LHFree(obj);
    return obj;
}

// ============================================================
//  ReleaseDeviceObjects  (0x18F1220 - 0x18F1306)  -- release the device's
//  vtable objects before the device itself is destroyed
//  [NOT byte-identical] placement-dependent (relative calls) and refs the
//  device objects (dword_2075F14 / dword_1DB1228 / dword_1DB1264 /
//  dword_1DB1244), the slot index (dword_2075F10), the per-slot tables
//  (0x20FCA68 / 0x20FCAE8) and the object-list container (0x20FC988) at
//  their original high VAs.
//
//  int ReleaseDeviceObjects(void)   -- no args (cdecl), returns int
//
//  Releases (vtable[2], stdcall with the object on the stack) and clears:
//  dword_2075F14; then zeroes the first DWORD of the 0x20FC988 container;
//  then dword_1DB1228, dword_1DB1264 (+ 1DB1268), dword_1DB1244 (+ 1DB1248).
//  Then, for the current slot (dword_2075F10), releases and clears the
//  objects in the four 0x20FCA68 sub-records (row bytes 0/32/64/96, row
//  DWORDs 1 and 2) -- re-reading the slot after each release, since the
//  release may change it -- and the base-device object in the current
//  slot's 0x20FCAE8 row (row DWORDs 1 and 2, using the slot as last read).
//  Finally frees the container's two object lists (FreeObjectListContainer).
// ============================================================
static int ReleaseDeviceObjects(void)
{
    if (g_dword_2075F14 != 0)
    {
        void **ovt = *(void ***)g_dword_2075F14;
        void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
        release(g_dword_2075F14);
        g_dword_2075F14 = 0;
    }

    g_stru_20FC988.f00 = 0;

    if (g_dword_1DB1228 != 0)
    {
        void **ovt = *(void ***)g_dword_1DB1228;
        void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
        release((void *)g_dword_1DB1228);
        g_dword_1DB1228 = 0;
    }
    if (g_dword_1DB1264 != 0)
    {
        void **ovt = *(void ***)g_dword_1DB1264;
        void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
        release((void *)g_dword_1DB1264);
        g_dword_1DB1264 = 0;
        g_dword_1DB1268 = 0;
    }
    if (g_dword_1DB1244 != 0)
    {
        void **ovt = *(void ***)g_dword_1DB1244;
        void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
        release((void *)g_dword_1DB1244);
        g_dword_1DB1244 = 0;
        g_dword_1DB1248 = 0;
    }

    DWORD slot = g_dword_2075F10;
    for (int i = 0; i < 128; i += 32)
    {
        char *row = (char *)&g_dword_20FCA68[slot * 44] + i;
        if (*(DWORD *)(row + 4) != 0)
        {
            void *obj = *(void **)(row + 4);
            void **ovt = *(void ***)obj;
            void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
            release(obj);
            slot = g_dword_2075F10;    // re-read (the release may change it)
            *(DWORD *)(row + 4) = 0;   // clear the original row
            *(DWORD *)(row + 8) = 0;
        }
    }

    DWORD *base = &g_dword_20FCAE8[slot * 44];
    if (base[1] != 0)
    {
        void *obj = (void *)base[1];
        void **ovt = *(void ***)obj;
        void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
        release(obj);
        base[1] = 0;
        base[2] = 0;
    }

    FreeObjectListContainer(&g_stru_20FC988);
    return 0;
}

// ============================================================
//  DestroyDevice  (0x18F1760 - 0x18F17A1)  -- destroy the current device
//  [NOT byte-identical] placement-dependent (relative calls to
//  FreeDeviceFlushRequest / ReleaseDeviceObjects) and refs the device object
//  (dword_1E2EEF4) and the flush-request object (dword_2075F00) at their
//  original high VAs.
//
//  int DestroyDevice(void)   -- no args (cdecl), returns int
//
//  If the device object (dword_1E2EEF4) is set: free the pending device
//  flush request (FreeDeviceFlushRequest + clear dword_2075F00), release the
//  device's vtable objects and object lists (ReleaseDeviceObjects), invoke
//  the device object's vtable[2] (byte offset +8, stdcall with the object on
//  the stack) to destroy it, and clear the pointer. Returns the vtable[2]
//  result (0 when no device is set; the original returns a stale EAX in that
//  case). Called from ShutdownDevice and at the top of the device init
//  (CreateXloDevice, sub_18F1B30) to destroy any previous device.
// ============================================================
static int DestroyDevice(void)
{
    if (g_dword_1E2EEF4 != 0)
    {
        if (g_dword_2075F00 != 0)
        {
            FreeDeviceFlushRequest(g_dword_2075F00);
            g_dword_2075F00 = 0;
        }
        ReleaseDeviceObjects();
        void **vtable = *(void ***)g_dword_1E2EEF4;
        int (__stdcall *destroy)(void *) = (int (__stdcall *)(void *))vtable[2];
        int result = destroy(g_dword_1E2EEF4);
        g_dword_1E2EEF4 = 0;
        return result;
    }
    return 0;
}

// ============================================================
//  ShutdownDevice  (0x18F17B0 - 0x18F1804)  -- shut the device down
//  (the "shutdown state 3" step of PumpWorker)
//  [NOT byte-identical] placement-dependent (relative calls to NvApiTeardownFlush /
//  NvApiTeardownUnload / TeardownDeviceSlot / TeardownDeviceBase / DestroyDevice) and refs the device
//  globals byte_2075DDB, dword_1E2ED70, dword_1E2ED6C, dword_1E2EEF8 at their
//  original high VAs.
//
//  int ShutdownDevice(CWorkerWin32 *obj)   -- __thiscall (worker in ECX)
//
//  If byte_2075DDB (device-active flag) is set: NvApiTeardownFlush() (device
//  teardown flush), NvApiTeardownUnload() (release the device library), clear the
//  flag. Then dword_1E2ED70 = dword_1E2ED6C = -1, TeardownDeviceSlot(),
//  TeardownDeviceBase(), DestroyDevice() (final device teardown), and if the
//  device object dword_1E2EEF8 is set, invokes its third virtual method
//  (vtable[2], stdcall with the object on the stack) and clears it. Returns
//  the vtable[2] result (or 0).
// ============================================================
static int ShutdownDevice(CWorkerWin32 *obj)
{
    int result;

    if (g_byte_2075DDB != 0)
    {
        NvApiTeardownFlush();
        NvApiTeardownUnload();
        g_byte_2075DDB = 0;
    }

    g_dword_1E2ED70 = -1;
    g_dword_1E2ED6C = -1;
    TeardownDeviceSlot();
    TeardownDeviceBase();
    DestroyDevice();

    result = (int)g_dword_1E2EEF8;
    if (g_dword_1E2EEF8 != 0)
    {
        void **vtable = *(void ***)g_dword_1E2EEF8;
        typedef int (__stdcall *F3DeviceShutdownFn)(DWORD);
        F3DeviceShutdownFn fn = (F3DeviceShutdownFn)vtable[2];
        result = fn(g_dword_1E2EEF8);
    }
    g_dword_1E2EEF8 = 0;
    return result;
}

// ============================================================
//  PumpWorker  (0x18F8600 - 0x18F86A9)  -- one step of the worker's state
//  machine, called on every iteration of RunGame's main pump loop
//  [NOT byte-identical] placement-dependent (relative calls to
//  EnsureWorkerWindow / PumpWorkerMessages / ShutdownDevice) and refs the
//  EnterCriticalSection /
//  LeaveCriticalSection / SetEvent / WaitForSingleObjectEx / WaitMessage IAT
//  slots at their original high VAs.
//
//  __thiscall-style (worker in ECX, no stack args). Reads the pump state
//  word at +556 (workerState) under the worker's critical section (+4):
//    state 1  create/restore the worker window (EnsureWorkerWindow), set the state
//             to 2, signal event1 (+156), leave the CS, block on
//             WaitForSingleObjectEx(event2, INFINITE, false), re-enter the CS
//    state 2  leave the CS, WaitMessage() (sleep until a message arrives),
//             re-enter the CS, drain the message queue (PumpWorkerMessages), leave
//    state 3  shut the device down (ShutdownDevice), reset the state to 0, leave
//    state 0  leave the CS (idle no-op)
// ============================================================
void PumpWorker(CWorkerWin32 *obj)
{
    LPCRITICAL_SECTION cs = (LPCRITICAL_SECTION)&obj->cs;

    EnterCriticalSection(cs);
    switch (obj->workerState)
    {
    case 1:
        EnsureWorkerWindow(obj);
        obj->workerState = 2;
        SetEvent(obj->event1);
        LeaveCriticalSection(cs);
        WaitForSingleObjectEx(obj->event2, INFINITE, FALSE);
        EnterCriticalSection(cs);
        break;
    case 2:
        LeaveCriticalSection(cs);
        WaitMessage();
        EnterCriticalSection(cs);
        PumpWorkerMessages(obj);
        LeaveCriticalSection(cs);
        return;
    case 3:
        ShutdownDevice(obj);
        obj->workerState = 0;
        LeaveCriticalSection(cs);
        return;
    default:
        break;
    }
    LeaveCriticalSection(cs);
}

// ============================================================
//  SetOnResetDevice  (0x18F6C50 - 0x18F6C5D)  -- set the worker's
//  on-reset-device callback (the worker field at +0x26C)
//
//  [BYTE-IDENTICAL]  A __thiscall + retn 4 hybrid, the sibling of
//  SetOnCreateDevice (0x18F6CA0): the worker is in ECX and the callback is
//  a stack arg the callee cleans (ret 4). It stores the callback in the
//  worker's onResetDevice field and returns it. The caller must dispatch
//  it as a __thiscall function (see the F3ThiscallSetFn cast in RunGame).
//
//  F3CallbackFn SetOnResetDevice(CWorkerWin32 *worker, F3CallbackFn cb)
// ============================================================
__declspec(naked) F3CallbackFn SetOnResetDevice(void *worker, F3CallbackFn cb)
{
    __asm {
        mov    eax, [esp+4]      ; eax = cb
        mov    [ecx+26Ch], eax   ; worker->onResetDevice = cb
        ret    4                 ; return cb, clean up the stack arg
    }
}

// ============================================================
//  SetPreTranslate  (0x18F6F20 - 0x18F6F44)  -- set the worker's
//  pre-translate hook (the field at +0x270)
//
//  [NOT byte-identical] refs the EnterCriticalSection / LeaveCriticalSection
//  IAT slots.
//
//  A __thiscall + retn 4 hybrid, the sibling of SetOnCreateDevice: the
//  worker is in ECX and the callback is a stack arg the callee cleans
//  (ret 4). It stores the callback under the worker's critical section
//  (the original holds the CS across the store) and returns it. Installed
//  with XLivePreTranslateMessage by InitNetworking (0x6A7210).
//
//  F3PreTranslateFn SetPreTranslate(CWorkerWin32 *worker, F3PreTranslateFn cb)
// ============================================================
F3PreTranslateFn SetPreTranslate(void *worker, F3PreTranslateFn cb)
{
    CWorkerWin32 *w = (CWorkerWin32 *)worker;
    EnterCriticalSection(&w->cs);
    w->preTranslate = cb;
    LeaveCriticalSection(&w->cs);
    return cb;
}
