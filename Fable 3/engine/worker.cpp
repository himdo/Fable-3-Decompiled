// Fable 3 — CWorkerWin32 module implementation (the 664-byte main worker
// object, its deleting destructor, and the current-worker registry).

#include <stdio.h>
#include <string.h>   // _invalid_parameter_noinfo
#include <math.h>    // fabsf
#include <new>       // std::bad_alloc (InputQueueAllocBuffer overflow path)
#include <stdexcept> // std::length_error (ProcessInputQueue overflow path)

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

//  ClearCurrentThreadId  (0x18F0180 - 0x18F018B)  -- clear the current-thread-id
//  marker (dword_1E2ED70) to -1 (the "reset to -1 on shutdown" step).
//  [NOT byte-identical] refs the high-VA global g_dword_1E2ED70 not placed at
//  its original VA in this build, so it is implemented in C++ over a static.
void ClearCurrentThreadId(void)
{
    g_dword_1E2ED70 = -1;
}

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
// 0x1DB1240 .. 0x1DB125C: the device surface-record block (8 DWORDs / 32 bytes).
//   [0] (0x1DB1240)               record header
//   [1] (0x1DB1244)               device backbuffer surface object
//   [2] (0x1DB1248)               backbuffer present flag
//   [3..7] (0x1DB124C..0x1DB125C) 5-word format record (QueryCsurfaceFormat out)
// Modeled as one contiguous array so CopySlotRecord (sub_18D1B70) can copy the
// 28-byte payload ([1..7]) from the record base, matching the image layout.
static DWORD g_dword_1DB1240[8] = { 0 };
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
// by SaveSettingsBeforeExit (sub_18F71E0) now live in gamemain.cpp next to the
// settings cluster (InitAccessibilitySettings reads them too); see gamemain.h.

// Forward declaration for a helper defined later in this file (used by the
// WM_SIZE path).
static int ResizeWorkerWindow(CWorkerWin32 *obj);

// 0x1E2EEF4 (the current XLO device object) and 0x1E2ED80 (the 56-byte
// 14-DWORD XLO device-caps block) are engine-wide shared globals now defined
// once in common/common.cpp and declared in common/common.h (included above),
// so worker.cpp / gamemain.cpp / xlo_device.cpp all see the same device.

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

// 0x1D9E184: the one-word adapter state stored by F3InitAdapterState (the
// probe result). The legacy probe that returns 0 or a negative-int out
// value also writes 1 here, forcing the success-shaped tail.
static DWORD g_dword_1D9E184 = 0;   // 0x1D9E184  adapter state word

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
//  XLO device-object containers  +  ResetXloDevice
//  (0x18F2310 - 0x18F2850 and its transitive sub_56xxxx / sub_18E6xxx helpers)
//
//  The device keeps three 20-byte red-black "list" containers (device objects
//  walked for teardown / re-notification) and one 28-byte red-black "map"
//  container (object -> SEH slot).  Each container is a self-referential
//  sentinel node (an empty tree) plus a 4-byte wrapper pointer and a count.
//  In the image every container is empty (poison 0xFFFFFFFF at image load;
//  the subsystem init that would populate them is not executed in this build),
//  so the wrapper / sentinel pairs below are the valid empty-tree layout and
//  all container walks are no-ops.
//
//  ResetXloDevice passes the 56-byte device caps block BY VALUE (14 DWORDs on
//  the stack), matching the original's rep-movsd by-value call convention.
//
//  [NOT byte-identical] throughout: the original helpers use userpurge
//  register passing and high-VA global references, and the RB rebalancing is
//  transcribed from IDA pseudocode; behavioral fidelity is the goal (the
//  containers are always empty in this build).
// ============================================================

// Forward declarations of device-state functions defined further below in this
// module (ResetXloDevice calls them before their definitions).
static int   ReleaseDeviceObjects(void);
static DWORD InitXloDeviceState(void);

// 0x1E2ED74: device render-mode class (1..4).  Defined here (moved up from the
// device-build globals block below) so ResetXloDevice can store it.
static DWORD g_dword_1E2ED74 = 0;

// 0x2075F04: the "device built once" flag.  Set on the first reset so that
// subsequent resets skip the one-time release phase; cleared at the end of
// every reset.
static BYTE  g_byte_2075F04 = 0;

// The 56-byte (14-DWORD) device caps block, passed by value to
// ResetXloDevice (the original does a rep-movsd of 14 DWORDs onto the stack).
// (This is the same F3XloCaps consumed by BuildXloDeviceFromRequest; the
// definition lives here so ResetXloDevice can take it by value.)
struct F3XloCaps
{
    DWORD field[14];
};
typedef char F3XloCaps_SizeCheck[(sizeof(F3XloCaps) == 56) ? 1 : -1];

// ---- container node layouts ---------------------------------------------
// 20-byte node used by the three device-object lists.
typedef struct F3ObjNode20
{
    DWORD left;      // +0x00
    DWORD parent;    // +0x04
    DWORD right;     // +0x08
    DWORD key;       // +0x0C  (object pointer / callback)
    BYTE  flag;      // +0x10  (delete flag)
    BYTE  terminus;  // +0x11  (sentinel / end flag)
    char  pad[2];
} F3ObjNode20;
typedef char F3ObjNode20_SizeCheck[(sizeof(F3ObjNode20) == 20) ? 1 : -1];

// 28-byte node used by the device-object map.
typedef struct F3ObjNode28
{
    DWORD left;      // +0x00
    DWORD parent;    // +0x04
    DWORD right;     // +0x08
    DWORD key;       // +0x0C  (object pointer)
    DWORD aux;       // +0x10  (SEH slot word 0)
    DWORD block;     // +0x14  (SEH slot word 1, refcounted)
    BYTE  color;     // +0x18  (RB color)
    BYTE  terminus;  // +0x19  (sentinel / end flag)
    char  pad[2];
} F3ObjNode28;
typedef char F3ObjNode28_SizeCheck[(sizeof(F3ObjNode28) == 28) ? 1 : -1];

// 32-byte container header.  The wrapper points to a 4-byte block whose
// content is the address of this header, so a non-null dereference of the
// wrapper lands on the header and [wrapper + 0x18] is the sentinel pointer
// (matching the original layout: wrapper at +0x00, sentinel at +0x18, count
// at +0x1C).
typedef struct F3ObjContainer
{
    DWORD wrapper;   // +0x00
    char  pad[20];   // +0x04 .. +0x17
    DWORD sentinel;  // +0x18
    DWORD count;     // +0x1C
} F3ObjContainer;
typedef char F3ObjContainer_SizeCheck[(sizeof(F3ObjContainer) == 32) ? 1 : -1];

// The original container helpers call the CRT invalid-parameter handler on
// invariant violations (these are never reached for the empty containers in
// this build).  Use a project-local no-op stand-in (the original aborts /
// reports; no-op is fine for the empty case).  A local _invalid_parameter_noinfo
// definition emits the CRT symbol __invalid_parameter_noinfo at the /LTCG link
// and collides with libcmt.lib(invarg.obj) (LNK2005), so it keeps a name of
// its own.
static void F3InvalidParameterNoop(void)
{
}

// ---- container sentinels + wrapper blocks + headers ---------------------
static F3ObjNode20 g_sentList1;   // 0x20FCA24
static F3ObjNode20 g_sentList2;   // 0x20FC9E4
static F3ObjNode20 g_sentList3;   // 0x20FCA04
static F3ObjNode28 g_sentMap;     // 0x20FCA44
static DWORD       g_wrapList1;   // 0x20FCA0C block
static DWORD       g_wrapList2;   // 0x20FC9CC block
static DWORD       g_wrapList3;   // 0x20FC9EC block
static DWORD       g_wrapMap;     // 0x20FCA2C block
static F3ObjContainer g_contList1;   // base 0x20FCA0C
static F3ObjContainer g_contList2;   // base 0x20FC9CC
static F3ObjContainer g_contList3;   // base 0x20FC9EC
static F3ObjContainer g_contMap;     // base 0x20FCA2C

static void F3InitDeviceObjContainers(void)
{
    // List 1 (20-byte nodes, 0x20FCA0C / 0x20FCA24 / 0x20FCA28).
    F3ObjNode20 *s1 = &g_sentList1;
    s1->left = s1->right = s1->parent = (DWORD)s1;
    s1->key = 0; s1->flag = 1; s1->terminus = 1;
    g_wrapList1  = (DWORD)&g_contList1;   // wrapper content = container struct
    g_contList1.wrapper  = (DWORD)&g_wrapList1;
    g_contList1.sentinel = (DWORD)s1;
    g_contList1.count    = 0;

    // List 2 (20-byte nodes, 0x20FC9CC / 0x20FC9E4 / 0x20FC9E8).
    F3ObjNode20 *s2 = &g_sentList2;
    s2->left = s2->right = s2->parent = (DWORD)s2;
    s2->key = 0; s2->flag = 1; s2->terminus = 1;
    g_wrapList2  = (DWORD)&g_contList2;
    g_contList2.wrapper  = (DWORD)&g_wrapList2;
    g_contList2.sentinel = (DWORD)s2;
    g_contList2.count    = 0;

    // List 3 (20-byte nodes, 0x20FC9EC / 0x20FCA04 / 0x20FCA08).
    F3ObjNode20 *s3 = &g_sentList3;
    s3->left = s3->right = s3->parent = (DWORD)s3;
    s3->key = 0; s3->flag = 1; s3->terminus = 1;
    g_wrapList3  = (DWORD)&g_contList3;
    g_contList3.wrapper  = (DWORD)&g_wrapList3;
    g_contList3.sentinel = (DWORD)s3;
    g_contList3.count    = 0;

    // Map (28-byte nodes, 0x20FCA2C / 0x20FCA44 / 0x20FCA48).
    F3ObjNode28 *sm = &g_sentMap;
    sm->left = sm->right = sm->parent = (DWORD)sm;
    sm->key = 0; sm->aux = 0; sm->block = 0;
    sm->color = 0; sm->terminus = 1;
    g_wrapMap     = (DWORD)&g_contMap;
    g_contMap.wrapper  = (DWORD)&g_wrapMap;
    g_contMap.sentinel = (DWORD)sm;
    g_contMap.count    = 0;
}

// Static-constructor guard so the containers are valid from program start
// (matches the original subsystem init running before main).
struct F3DeviceObjContainersInit
{
    F3DeviceObjContainersInit() { F3InitDeviceObjContainers(); }
};
static F3DeviceObjContainersInit g_deviceObjContainersInit;

// ============================================================
//  F3ObjTreeStep20  (sub_568420 / sub_568330, byte-identical twins)
//  -- advance a 2-word in-order iterator {container, node} over a 20-byte
//  node tree to the in-order successor.  [NOT byte-identical] userpurge.
// ============================================================
static void F3ObjTreeStep20(DWORD *iter)
{
    if (iter[0] == 0)
        F3InvalidParameterNoop();
    F3ObjNode20 *node = (F3ObjNode20 *)iter[1];
    if (node->terminus != 0)
        F3InvalidParameterNoop();
    F3ObjNode20 *parent = (F3ObjNode20 *)node->parent;
    if (parent->terminus != 0)
    {
        // node is the root: walk node->right while the right child's parent
        // is still node.
        F3ObjNode20 *r = (F3ObjNode20 *)node->right;
        for (;;)
        {
            if (r->terminus != 0)
                break;
            if ((DWORD)node != r->parent)
                break;
            iter[1] = (DWORD)r;
            r = (F3ObjNode20 *)r->right;
        }
        iter[1] = (DWORD)r;
        return;
    }
    F3ObjNode20 *l = (F3ObjNode20 *)parent->left;
    if (l->terminus != 0)
    {
        iter[1] = (DWORD)parent;
        return;
    }
    F3ObjNode20 *cur = l;
    for (;;)
    {
        F3ObjNode20 *next = (F3ObjNode20 *)l->left;
        if (next->terminus != 0)
            break;
        cur = l;
        l = next;
    }
    iter[1] = (DWORD)cur;
}

// ============================================================
//  F3ObjTreeStep28  (sub_568540)  -- in-order successor over a 28-byte node
//  tree (the terminus flag lives at byte +0x19 instead of +0x11).
//  [NOT byte-identical] userpurge.
// ============================================================
static void F3ObjTreeStep28(DWORD *iter)
{
    if (iter[0] == 0)
        F3InvalidParameterNoop();
    F3ObjNode28 *node = (F3ObjNode28 *)iter[1];
    if (node->terminus != 0)
        F3InvalidParameterNoop();
    F3ObjNode28 *parent = (F3ObjNode28 *)node->parent;
    if (parent->terminus != 0)
    {
        F3ObjNode28 *r = (F3ObjNode28 *)node->right;
        for (;;)
        {
            if (r->terminus != 0)
                break;
            if ((DWORD)node != r->parent)
                break;
            iter[1] = (DWORD)r;
            r = (F3ObjNode28 *)r->right;
        }
        iter[1] = (DWORD)r;
        return;
    }
    F3ObjNode28 *l = (F3ObjNode28 *)parent->left;
    if (l->terminus != 0)
    {
        iter[1] = (DWORD)parent;
        return;
    }
    F3ObjNode28 *cur = l;
    for (;;)
    {
        F3ObjNode28 *next = (F3ObjNode28 *)l->left;
        if (next->terminus != 0)
            break;
        cur = l;
        l = next;
    }
    iter[1] = (DWORD)cur;
}

// ============================================================
//  F3ObjTreePrev28  (sub_5684C0)  -- in-order predecessor over a 28-byte node
//  tree.  [NOT byte-identical] userpurge.
// ============================================================
static void F3ObjTreePrev28(DWORD *iter)
{
    if (iter[0] == 0)
        F3InvalidParameterNoop();
    F3ObjNode28 *node = (F3ObjNode28 *)iter[1];
    if (node->terminus != 0)
    {
        node = (F3ObjNode28 *)node->parent;
        iter[1] = (DWORD)node;
        if (node->terminus != 0)
            F3InvalidParameterNoop();
        return;
    }
    F3ObjNode28 *l = (F3ObjNode28 *)node->left;
    if (l->terminus != 0)
    {
        // node->left is terminal: climb node->right while it is the parent's
        // left child.
        F3ObjNode28 *nr = (F3ObjNode28 *)node->right;
        for (;;)
        {
            if (nr->terminus != 0)
                break;
            if (iter[1] != nr->left)
                break;
            iter[1] = (DWORD)nr;
            nr = (F3ObjNode28 *)nr->right;
        }
        if (((F3ObjNode28 *)iter[1])->terminus != 0)
            F3InvalidParameterNoop();
        iter[1] = (DWORD)nr;
        return;
    }
    F3ObjNode28 *r = (F3ObjNode28 *)l->parent;
    if (r->terminus != 0)
    {
        iter[1] = (DWORD)l;
        return;
    }
    F3ObjNode28 *cur = r;
    for (;;)
    {
        F3ObjNode28 *next = (F3ObjNode28 *)cur->parent;
        if (next->terminus != 0)
            break;
        cur = next;
    }
    iter[1] = (DWORD)cur;
}

// ============================================================
//  F3ObjTreeSearch28  (sub_565450)  -- BST search in the 28-byte map by key;
//  returns a 2-word result {wrapper, node-or-sentinel}.
//  [NOT byte-identical] userpurge (the result was returned in EAX).
// ============================================================
static DWORD *F3ObjTreeSearch28(DWORD *out, DWORD *keyWord)
{
    F3ObjNode28 *sentinel = (F3ObjNode28 *)g_contMap.sentinel;
    F3ObjNode28 *v2 = sentinel;
    F3ObjNode28 *v3 = (F3ObjNode28 *)sentinel->parent;
    while (v3->terminus == 0)
    {
        if (v3->key >= *keyWord)
        {
            v2 = v3;
            v3 = (F3ObjNode28 *)v3->left;
        }
        else
        {
            v3 = (F3ObjNode28 *)v3->right;
        }
    }
    out[1] = (DWORD)v2;
    out[0] = g_contMap.wrapper;
    return out;
}

// ============================================================
//  F3ObjNodeAlloc28  (sub_567D30)  -- allocate and initialize a 28-byte map
//  node from a 3-DWORD key block {key, aux, block}.
//  [NOT byte-identical] userpurge + LHAlloc.
// ============================================================
static DWORD *F3ObjNodeAlloc28(DWORD left, DWORD right, DWORD parent,
                               const DWORD *key3)
{
    DWORD *n = (DWORD *)LHAlloc(28);
    if (n == 0)
        return 0;
    n[0] = left;
    n[1] = right;
    n[2] = parent;
    n[3] = key3[0];
    n[4] = key3[1];
    n[5] = key3[2];
    if (n[5] != 0)
        InterlockedIncrement((volatile LONG *)n[5]);
    ((BYTE *)n)[0x18] = 0;
    ((BYTE *)n)[0x19] = 0;
    return n;
}

// ============================================================
//  F3ObjTreeRotateLeft28 / F3ObjTreeRotateRight28
//  (sub_567C50 / sub_567CA0)  -- RB rotations; `headerAddr` is the address of
//  the container header so [headerAddr + 0x18] is the sentinel node.
//  [NOT byte-identical] userpurge + high-VA header reference.
// ============================================================
static void F3ObjTreeRotateLeft28(DWORD x, DWORD headerAddr)
{
    DWORD y  = *(DWORD *)(x + 8);
    DWORD yl = *(DWORD *)y;
    *(DWORD *)(x + 8) = yl;
    if (*(BYTE *)(yl + 0x19) == 0)
        *(DWORD *)(yl + 4) = x;
    DWORD xparent = *(DWORD *)(x + 4);
    *(DWORD *)(y + 4) = xparent;
    DWORD header = *(DWORD *)(headerAddr + 0x18);
    if (x == *(DWORD *)(header + 4))
    {
        *(DWORD *)(header + 4) = y;
        *(DWORD *)y  = x;
        *(DWORD *)(x + 4) = y;
    }
    else
    {
        xparent = *(DWORD *)(x + 4);
        if (x == *(DWORD *)xparent)
        {
            *(DWORD *)xparent = y;
            *(DWORD *)y  = x;
            *(DWORD *)(x + 4) = y;
        }
        else
        {
            *(DWORD *)(xparent + 8) = y;
            *(DWORD *)y  = x;
            *(DWORD *)(x + 4) = y;
        }
    }
}

static void F3ObjTreeRotateRight28(DWORD x, DWORD headerAddr)
{
    DWORD y  = *(DWORD *)(x + 8);
    DWORD yl = *(DWORD *)y;
    *(DWORD *)(x + 8) = yl;
    if (*(BYTE *)(yl + 0x19) == 0)
        *(DWORD *)(yl + 4) = x;
    DWORD xparent = *(DWORD *)(x + 4);
    *(DWORD *)(y + 4) = xparent;
    DWORD header = *(DWORD *)(headerAddr + 0x18);
    if (x == *(DWORD *)(header + 4))
    {
        *(DWORD *)(header + 4) = y;
        *(DWORD *)(y + 8) = x;
        *(DWORD *)(x + 4) = y;
    }
    else
    {
        xparent = *(DWORD *)(x + 4);
        if (x == *(DWORD *)(xparent + 8))
            *(DWORD *)(xparent + 8) = y;
        else
            *(DWORD *)xparent = y;
        *(DWORD *)(y + 8) = x;
        *(DWORD *)(x + 4) = y;
    }
}

// ============================================================
//  F3ObjTreeInsert28  (sub_566DF0)  -- insert a 28-byte node below `pos`
//  (flag 0 = right child, flag 1 = left child) and rebalance; returns a
//  2-word iterator {wrapper, node}.
//  [NOT byte-identical] transcribed from IDA pseudocode.
// ============================================================
static DWORD *F3ObjTreeInsert28(DWORD pos, DWORD *out2, int flag,
                                const DWORD *key3)
{
    if (g_contMap.count >= 0x15555554)
        return 0;   // "map/set<T> too long" (the original throws)
    DWORD sentinel = g_contMap.sentinel;
    DWORD node = (DWORD)F3ObjNodeAlloc28(sentinel, sentinel, pos, key3);
    ++g_contMap.count;
    if (pos == sentinel)
    {
        *(DWORD *)(sentinel + 4) = node;
        *(DWORD *)sentinel       = node;
        *(DWORD *)(sentinel + 8) = node;
    }
    else if (flag == 0)
    {
        *(DWORD *)(pos + 8) = node;
        if (pos == *(DWORD *)(sentinel + 8))
            *(DWORD *)(sentinel + 8) = node;
    }
    else
    {
        *(DWORD *)pos = node;
        if (pos == *(DWORD *)sentinel)
            *(DWORD *)sentinel = node;
    }
    // RB fixup: walk the parent chain, recoloring / rotating as needed.
    DWORD i = node;
    for (;;)
    {
        DWORD p = *(DWORD *)(i + 4);          // parent
        if (*(BYTE *)(p + 0x18) != 0)         // parent is black
            break;
        DWORD g = *(DWORD *)(p + 4);          // grandparent
        DWORD uncle = (p == *(DWORD *)g) ? *(DWORD *)(g + 8) : *(DWORD *)g;
        if (*(BYTE *)(uncle + 0x18) != 0)     // uncle is black -> rotate
        {
            if (p == *(DWORD *)g)             // p is the left child
            {
                if (i == *(DWORD *)(p + 8))   // i is the right child
                {
                    i = p;
                    F3ObjTreeRotateLeft28(i, (DWORD)&g_contMap);
                }
                *(BYTE *)(*(DWORD *)(i + 4) + 0x18) = 1;
                *(BYTE *)(*(DWORD *)(*(DWORD *)(i + 4) + 4) + 0x18) = 0;
                F3ObjTreeRotateRight28(i, (DWORD)&g_contMap);
                continue;
            }
            else                              // p is the right child
            {
                if (i == *(DWORD *)p)         // i is the left child
                {
                    i = p;
                    F3ObjTreeRotateRight28(i, (DWORD)&g_contMap);
                }
                *(BYTE *)(*(DWORD *)(i + 4) + 0x18) = 1;
                *(BYTE *)(*(DWORD *)(*(DWORD *)(i + 4) + 4) + 0x18) = 0;
                F3ObjTreeRotateLeft28(i, (DWORD)&g_contMap);
                continue;
            }
        }
        // uncle is red: recolor parent / uncle black, grandparent red, climb.
        *(BYTE *)(p + 0x18) = 1;
        *(BYTE *)(uncle + 0x18) = 1;
        *(BYTE *)(g + 0x18) = 0;
        i = g;
    }
    *(BYTE *)(*(DWORD *)(sentinel + 4) + 0x18) = 1;   // root is black
    out2[1] = node;
    out2[0] = g_contMap.wrapper;
    return out2;
}

// ============================================================
//  F3ObjTreeFindOrInsertMin28  (sub_566BE0)  -- BST search for the key; if
//  absent, insert it as the left child of the found node.  Returns a 3-word
//  result {wrapper, node, inserted}.
//  [NOT byte-identical] transcribed from IDA pseudocode.
// ============================================================
static DWORD *F3ObjTreeFindOrInsertMin28(DWORD *out3, const DWORD *key3)
{
    DWORD sentinel = g_contMap.sentinel;
    DWORD v2 = *(DWORD *)(sentinel + 4);
    BOOL inserted = TRUE;
    DWORD v4 = sentinel;
    if (*(BYTE *)(v2 + 0x19) == 0)
    {
        DWORD key = key3[0];
        for (;;)
        {
            v4 = v2;
            inserted = (key < *(DWORD *)(v2 + 0xC));
            if (key >= *(DWORD *)(v2 + 0xC))
                v2 = *(DWORD *)(v2 + 8);
            else
                v2 = *(DWORD *)v2;
            if (*(BYTE *)(v2 + 0x19) != 0)
                break;
        }
    }
    DWORD v6 = g_contMap.wrapper;
    DWORD v7 = v4;
    DWORD v12 = g_contMap.wrapper;
    if (inserted)
    {
        DWORD *node;
        if (v4 == *(DWORD *)sentinel)
            node = (DWORD *)F3ObjTreeInsert28(v4, &v12, 1, key3);
        else
        {
            F3ObjTreePrev28(&v12);
            node = (DWORD *)F3ObjTreeInsert28(v4, &v12, inserted, key3);
        }
        out3[0] = node ? node[0] : 0;
        v7 = node ? node[1] : 0;
        out3[2] = 1;
    }
    else
    {
        if (*(DWORD *)(v7 + 0xC) >= key3[0])
        {
            out3[0] = v6;
            out3[2] = 0;
            v7 = v4;
        }
    }
    out3[1] = v7;
    return out3;
}

// ============================================================
//  F3ObjTreeFindOrInsertCached28  (sub_5652C0)  -- cached BST find-or-insert
//  in the 28-byte map (uses the hint node to speed up the search).
//  [NOT byte-identical] transcribed from IDA pseudocode.
// ============================================================
static DWORD *F3ObjTreeFindOrInsertCached28(DWORD *out3, const DWORD *key3,
                                            int wrapper, int hint)
{
    if (g_contMap.count == 0)
    {
        F3ObjTreeInsert28(g_contMap.sentinel, out3, 1, key3);
        return out3;
    }
    DWORD leftmost = *(DWORD *)g_contMap.sentinel;
    if (wrapper == 0 || wrapper != g_contMap.wrapper)
        F3InvalidParameterNoop();
    if (hint == leftmost)
    {
        if (key3[0] < *(DWORD *)(hint + 0xC))
        {
            F3ObjTreeInsert28(hint, out3, 1, key3);
            return out3;
        }
        DWORD *r = F3ObjTreeFindOrInsertMin28(out3, key3);
        out3[0] = r[0];
        out3[1] = r[1];
        return out3;
    }
    if (wrapper == 0 || wrapper != g_contMap.wrapper)
        F3InvalidParameterNoop();
    if (hint == g_contMap.sentinel)
    {
        if (*(DWORD *)(*(DWORD *)(g_contMap.sentinel + 8) + 0xC) >= key3[0])
        {
            DWORD *r = F3ObjTreeFindOrInsertMin28(out3, key3);
            out3[0] = r[0];
            out3[1] = r[1];
            return out3;
        }
        F3ObjTreeInsert28(hint, out3, 1, key3);
        return out3;
    }
    // General case: compare against the hint and its predecessor/successor.
    int less = (*(DWORD *)(hint + 0xC) < key3[0]);
    if (*(DWORD *)(hint + 0xC) > key3[0])
    {
        DWORD v12 = hint;
        F3ObjTreePrev28(&v12);
        if (*(DWORD *)(v12 + 0xC) < key3[0])
        {
            if (*(BYTE *)(*(DWORD *)(v12 + 8) + 0x19) == 0)
            {
                F3ObjTreeInsert28(v12, out3, 1, key3);
                return out3;
            }
            F3ObjTreeInsert28(v12, out3, 0, key3);
            return out3;
        }
    }
    if (!less)
    {
        DWORD *r = F3ObjTreeFindOrInsertMin28(out3, key3);
        out3[0] = r[0];
        out3[1] = r[1];
        return out3;
    }
    F3ObjTreeInsert28(hint, out3, 1, key3);
    return out3;
}

// ============================================================
//  F3ObjMapInsert28  (sub_5640A0)  -- search-or-insert the object key in the
//  28-byte map; returns a pointer to the resulting node's {aux, block} pair
//  (node + 0x10).
//  [NOT byte-identical] thiscall + userpurge + high-VA globals.
// ============================================================
static DWORD *F3ObjMapInsert28(DWORD *objSlot)
{
    DWORD out3[2];
    F3ObjTreeSearch28(out3, objSlot);
    if (out3[0] == 0 || out3[0] != g_contMap.wrapper)
        F3InvalidParameterNoop();
    DWORD v4 = out3[1];
    if (v4 == g_contMap.sentinel || *objSlot < *(DWORD *)(v4 + 0xC))
    {
        DWORD v8[2];
        v8[0] = *objSlot;
        v8[1] = 0;
        DWORD *r = F3ObjTreeFindOrInsertCached28(out3, v8, (int)out3[0],
                                                 (int)out3[1]);
        v4 = r[1];
    }
    DWORD head = (out3[0] != 0) ? *(DWORD *)out3[0] : 0;
    if (out3[0] == 0)
        F3InvalidParameterNoop();
    if (v4 == *(DWORD *)(head + 0x18))
        F3InvalidParameterNoop();
    return (DWORD *)(v4 + 16);
}

// ============================================================
//  F3ObjIterInit28  (sub_564200)  -- initialize a 2-word iterator {wrapper,
//  node} for the object key: the found node if present, else the sentinel.
//  [NOT byte-identical] userpurge (both args were in EDI / ESI).
// ============================================================
static DWORD *F3ObjIterInit28(DWORD *out, DWORD *objSlot)
{
    DWORD out3[2];
    F3ObjTreeSearch28(out3, objSlot);
    if (out3[0] == 0 || out3[0] != g_contMap.wrapper)
        F3InvalidParameterNoop();
    if (out3[1] == g_contMap.sentinel ||
        *objSlot < *(DWORD *)(out3[1] + 0xC))
    {
        out[0] = g_contMap.wrapper;
        out[1] = g_contMap.sentinel;
    }
    else
    {
        out[0] = out3[0];
        out[1] = out3[1];
    }
    return out;
}

// ============================================================
//  F3ObjNodeFree28  (sub_565490)  -- free a 28-byte map subtree: walk the
//  left chain, recursing on the parent field, releasing the refcounted SEH
//  block and freeing each node.  A no-op on an empty (sentinel) tree.
//  [NOT byte-identical] stdcall + high-VA heap-table reference.
// ============================================================
static void F3ObjNodeFree28(DWORD *root)
{
    DWORD *v1 = root;
    for (DWORD *i = root; ((BYTE *)i)[0x19] == 0; v1 = i)
    {
        F3ObjNodeFree28((DWORD *)i[2]);
        i = (DWORD *)*i;
        if (v1[5] != 0 && InterlockedDecrement((volatile LONG *)v1[5]) == 0)
        {
            DWORD *blk = (DWORD *)v1[5];
            ((void (*)(DWORD))blk[1])(blk[2]);
            LHFree(blk);
        }
        v1[4] = 0;
        v1[5] = 0;
        LHFree(v1);
    }
}

// Forward declarations (defined below): F3ObjRangeClear28 erases from the
// map, and F3ObjTreeErase28 climbs to the rightmost descendant.
static DWORD *F3ObjTreeErase28(DWORD a1, DWORD *a2, char a3, int a4);
static DWORD  F3ObjTreeRoot28(DWORD node);

// ============================================================
//  F3ObjRangeClear28  (sub_566CB0)  -- clear a range of the 28-byte map
//  (or, for an empty container, re-establish the sentinel self-links and
//  return {wrapper, sentinel}).
//  [NOT byte-identical] fastcall + high-VA references.
// ============================================================
static DWORD *F3ObjRangeClear28(DWORD a1, DWORD *a2, int a3, int a4,
                                int a5, int a6)
{
    int v6 = a3;
    int v8 = **(DWORD **)(a1 + 24);
    DWORD *v9 = a2;
    if (a3 == 0 || a3 != *(DWORD *)a1)
    {
        F3InvalidParameterNoop();
        v6 = a3;
    }
    if (a4 != v8)
        goto LABEL_13;
    int v10 = *(DWORD *)(a1 + 24);
    if (a5 == 0 || a5 != *(DWORD *)a1)
    {
        F3InvalidParameterNoop();
        v6 = a3;
    }
    if (a6 == v10)
    {
        F3ObjNodeFree28((DWORD *)*(DWORD *)(v10 + 4));
        *(DWORD **)(v10 + 4) = (DWORD *)v10;
        *(DWORD *)(a1 + 28) = 0;
        *(DWORD *)v10 = v10;
        *(DWORD **)(v10 + 8) = (DWORD *)v10;
        DWORD v12 = *(DWORD *)v10;
        v9[0] = 0;
        v9[1] = v12;
        v9[0] = *(DWORD *)a1;
        return v9;
    }
LABEL_13:
    for (;;)
    {
        if (v6 == 0 || v6 != a5)
        {
            F3InvalidParameterNoop();
            v6 = a3;
        }
        if (a4 == a6)
            break;
        F3ObjTreeStep28(v9);
        F3ObjTreeErase28(a1, v9, (char)v6, a4);
        v6 = a3;
        v9 = a2;
    }
    v9[0] = *(DWORD *)a1;
    v9[1] = a4;
    return v9;
}

// ============================================================
//  F3ObjTreeErase28  (sub_5678C0)  -- erase a node from the 28-byte map with
//  RB rebalancing, release its SEH block, free the node, and decrement the
//  container count; returns a 2-word iterator {wrapper, node}.
//  [NOT byte-identical] transcribed from IDA pseudocode; only reachable for a
//  non-empty map (never in this build).
// ============================================================
static DWORD *F3ObjTreeErase28(DWORD a1, DWORD *a2, char a3, int a4)
{
    (void)a3;
    int *node = (int *)a4;
    if (*(BYTE *)(node + 0x19) != 0)
        return a2;   // "invalid map/set<T> iterator" (the original throws)
    F3ObjTreeStep28(a2);
    // Splice the node out of the tree: replace it with its non-terminal
    // child and fix up the sentinel's leftmost / rightmost pointers. (RB
    // delete rebalancing omitted: the map is always empty in this build, so
    // this path is never reached.)
    int *v4;
    if (*(BYTE *)(*(int **)node + 0x19) != 0)
        v4 = (int *)node[2];
    else if (*(BYTE *)(node[2] + 0x19) != 0)
        v4 = (int *)*node;
    else
        v4 = (int *)node[2];
    int *v5 = (int *)node[1];
    if (*(BYTE *)v4 == 0)
        v4[1] = (int)v5;
    int sentinel = *(DWORD *)(a1 + 24);
    if (*(int **)(sentinel + 4) == node)
        *(int **)(sentinel + 4) = v4;
    else if (*(int **)v5 == node)
        *v5 = (int)v4;
    else
        v5[2] = (int)v4;
    if ((int)*(int *)sentinel == (int)node)
    {
        if (*(BYTE *)v4 != 0)
            *(int *)sentinel = (int)v5;
        else
        {
            int *i = v4;
            int *r = (int *)v4[0];
            for (; *(BYTE *)r == 0; r = (int *)r[0])
                i = r;
            *(int *)sentinel = (int)i;
        }
    }
    if ((int)*(int *)(sentinel + 8) == (int)node)
        *(int *)(sentinel + 8) = (int)((*(BYTE *)v4 != 0)
                                          ? (DWORD)v5
                                          : F3ObjTreeRoot28((DWORD)v4));
    // Release the SEH block and free the node.
    if (node[5] != 0 && InterlockedDecrement((volatile LONG *)node[5]) == 0)
    {
        DWORD *blk = (DWORD *)node[5];
        ((void (*)(DWORD))blk[1])(blk[2]);
        LHFree(blk);
    }
    node[4] = 0;
    node[5] = 0;
    LHFree(node);
    if (*(DWORD *)(a1 + 28) != 0)
        *(DWORD *)(a1 + 28) = *(DWORD *)(a1 + 28) - 1;
    a2[1] = (DWORD)node;
    a2[0] = *(DWORD *)a1;
    return a2;
}

// ============================================================
//  F3ObjTreeRoot28  (sub_568710)  -- climb the parent chain from a node to
//  its rightmost / deepest descendant (returns the input node when it has no
//  non-terminal parent).
//  [NOT byte-identical] userpurge (the node was passed in EAX).
// ============================================================
static DWORD F3ObjTreeRoot28(DWORD node)
{
    for (DWORD i = *(DWORD *)(node + 8); *(BYTE *)(i + 0x19) == 0;
         i = *(DWORD *)(i + 8))
        node = i;
    return node;
}

// ============================================================
//  F3SehSlotRelease  (sub_414850)  -- release-and-clear a 2-word {aux, block}
//  SEH slot: decrement the block refcount and, when it reaches zero, call the
//  handler (block + 4) with the arg (block + 8) and free the block.
//  [NOT byte-identical] userpurge + high-VA heap-table reference.
// ============================================================
static void F3SehSlotRelease(DWORD *slot)
{
    if (slot[1] != 0)
    {
        if (InterlockedDecrement((volatile LONG *)slot[1]) == 0)
        {
            DWORD *blk = (DWORD *)slot[1];
            ((void (*)(DWORD))blk[1])(blk[2]);
            LHFree(blk);
        }
    }
    slot[0] = 0;
    slot[1] = 0;
}

// ---- device slot tables (worker.cpp-local copies; the device slot init is
//      never run in this build because no device is created) -------------
// 0x1E2EF10: per-slot device state (16 entries); zeroed on reset.
static DWORD  g_dword_1E2EF10[16] = {0};
// 0x1DB12FC: the default device object assigned to missing slots.
static DWORD  g_dword_1DB12FC = 0;
// 0x1E2EAF8: per-slot "has object" flag (indexed by slot).
static DWORD  g_dword_1E2EAF8[0x105] = {0};
// 0x1E2EB4C: slot objects for slots >= 16 (indexed by raw slot).
static DWORD  g_dword_1E2EB4C[0x105] = {0};
// 0x1E2EF60: slot objects for slots < 16 (indexed by slot).
static DWORD  g_dword_1E2EF60[16] = {0};
// 0x1DB4AF8: per-slot registration (indexed by slot; >= 0x101 wraps by -241).
static DWORD  g_dword_1DB4AF8[0x105] = {0};

// ============================================================
//  AssignXloDeviceSlot  (sub_18E6250)  -- store a device object in slot
//  <16 -> g_dword_1E2EF60[slot] else g_dword_1E2EB4C[slot]; if the
//  registration changed, record it and call device vtable[65] (device, slot,
//  object).
//  [NOT byte-identical] userpurge + high-VA globals / vtable dispatch.
// ============================================================
static int AssignXloDeviceSlot(int slot, int obj)
{
    if (slot >= 16)
        g_dword_1E2EB4C[slot] = obj;
    else
        g_dword_1E2EF60[slot] = obj;
    if (g_dword_1DB4AF8[slot] != obj)
    {
        int v2 = slot;
        if ((unsigned)slot >= 0x101)
            v2 = slot - 241;
        g_dword_1DB4AF8[v2] = obj;
        void *dev = g_dword_1E2EEF4;
        int (*fn)(void *, int, int) =
            (int (*)(void *, int, int))(*(DWORD *)dev + 260);
        return fn(dev, slot, obj);
    }
    return slot;
}

// ============================================================
//  InitXloDeviceSlots  (sub_18E65F0)  -- fill in the missing device slots
//  with the default object (g_dword_1DB12FC) and register the per-slot
//  records for slots 257..0x104.
//  [NOT byte-identical] high-VA globals / vtable dispatch.
// ============================================================
static int InitXloDeviceSlots(void)
{
    for (unsigned i = 0; i < 0x10; ++i)
    {
        if (g_dword_1E2EF10[i] == 0)
            AssignXloDeviceSlot((int)i, (int)g_dword_1DB12FC);
    }
    int v1 = (int)g_dword_1DB12FC;
    int result = 0;
    for (unsigned j = 257; j < 0x105; ++j)
    {
        result = 4 * (int)j;
        if (g_dword_1E2EAF8[j] == 0)
        {
            if (j >= 16)
                g_dword_1E2EB4C[j] = (DWORD)v1;
            else
                g_dword_1E2EF60[j] = (DWORD)v1;
            if (g_dword_1DB4AF8[j] != (DWORD)v1)
            {
                if (j >= 0x101)
                    result -= 964;
                *(int *)((char *)g_dword_1DB4AF8 + result) = v1;
                void *dev = g_dword_1E2EEF4;
                int (*fn)(void *, int, int) =
                    (int (*)(void *, int, int))(*(DWORD *)dev + 260);
                result = fn(dev, (int)j, v1);
                v1 = (int)g_dword_1DB12FC;
            }
        }
    }
    return result;
}

// ============================================================
//  ResetXloDevice  (0x18F2310 - 0x18F2850)  -- reset / recreate the XLO device
//  from a fresh 56-byte caps block (passed by value).
//
//  int ResetXloDevice(F3XloCaps caps)   -- 1 = reset succeeded, 0 = failed
//
//  Original semantics: enter the device CS stru_20FC948.  If the device object
//  (dword_1E2EEF4) reports status 0x88760868 (already reset) leave and return
//  0.  Otherwise, unless the device has been built once (byte_2075F04), run the
//  one-time release phase: InitXloDeviceSlots, set byte_2075F04, allocate a
//  4-byte wrapper (v50) and a 28-byte self-linked sentinel node (v51), walk the
//  0x20FCA0C list (object vtable[1] teardown + SEH insert into the map) and the
//  0x20FC9CC list (raw callbacks), call ReleaseDeviceObjects, clear the local
//  container (sub_566CB0) and free v51 / v50.  Then recreate the device through
//  its vtable[16] (offset 0x40) with the new caps (a failed reset leaves the CS
//  and returns 0), record the caps into dword_1E2ED80 and the mode class into
//  dword_1E2ED74, run InitXloDeviceState (sub_18F1310), re-notify the 0x20FCA0C
//  list (object vtable[2]) and the 0x20FC9EC list (raw callbacks), free the map
//  tree and reset its sentinel, clear byte_2075F04, leave the CS and return 1.
//
//  [NOT byte-identical] placement-dependent (relative calls, the by-value
//  56-byte stack argument) and refs the high-VA device / container globals;
//  the RB-tree helpers are transcribed from IDA pseudocode.  In this build the
//  device is never created (no window), so this path is never exercised; the
//  goal is a faithful, compilable transcription that does not crash.
// ============================================================
static int ResetXloDevice(F3XloCaps caps)
{
    F3EnterDisplayCs();

    void *dev = g_dword_1E2EEF4;
    void **vt = *(void ***)dev;
    int status = ((int (*)(void *))vt[3])(dev);
    if (status == 0x88760868)
    {
        LeaveCriticalSection(&g_stru_20FC948);
        return 0;
    }

    if (g_byte_2075F04 == 0)
    {
        // ---- one-time release phase ----
        InitXloDeviceSlots();
        g_byte_2075F04 = 1;

        DWORD local_v50 = 0;
        DWORD *w50 = (DWORD *)LHAlloc(4);
        if (w50 != 0)
        {
            *w50 = (DWORD)&local_v50;   // wrapper content = address of the local
            local_v50 = (DWORD)w50;
        }

        // The local 28-byte self-referential empty-tree sentinel (v51).
        DWORD *v51 = (DWORD *)LHAlloc(0x1C);
        DWORD local_v51 = (DWORD)v51;
        if (v51 != 0)
        {
            ((BYTE *)v51)[0x18] = 1;    // color = 1
            ((BYTE *)v51)[0x19] = 1;    // terminus = 1
            v51[0] = local_v51;         // left   = self
            v51[1] = local_v51;         // parent = self
            v51[2] = local_v51;         // right  = self
        }

        // ---- walk 1 (list 1): object vtable[1] teardown + SEH insert ----
        {
            DWORD iter[2];
            iter[0] = g_contList1.wrapper;
            iter[1] = *(DWORD *)g_contList1.sentinel;
            DWORD sent1 = g_contList1.sentinel;
            while (1)
            {
                if (iter[0] == 0 || iter[0] != g_contList1.wrapper)
                    F3InvalidParameterNoop();
                if (iter[1] == sent1)
                    break;
                DWORD v48 = 0, lpAddend = 0;
                DWORD head = iter[0];
                if (head != 0)
                    head = *(DWORD *)head;
                else
                    F3InvalidParameterNoop();
                if (iter[1] == *(DWORD *)(head + 0x18))
                    F3InvalidParameterNoop();
                DWORD obj = *(DWORD *)(iter[1] + 0xC);
                void **ovt = *(void ***)obj;
                ((void (__thiscall *)(void *, DWORD *))ovt[1])((void *)obj, &v48);
                if (v48 != 0)
                {
                    DWORD head2 = iter[0];
                    if (head2 != 0)
                        head2 = *(DWORD *)head2;
                    else
                        F3InvalidParameterNoop();
                    if (iter[1] == *(DWORD *)(head2 + 0x18))
                        F3InvalidParameterNoop();
                    DWORD *r = F3ObjMapInsert28((DWORD *)(iter[1] + 0xC));
                    if (r[1] != lpAddend)
                        F3SehSlotRelease(r);
                    r[0] = v48;
                    r[1] = lpAddend;
                    if (lpAddend != 0)
                        InterlockedIncrement((volatile LONG *)lpAddend);
                }
                if (lpAddend != 0 &&
                    InterlockedDecrement((volatile LONG *)lpAddend) == 0)
                {
                    DWORD *blk = (DWORD *)lpAddend;
                    ((void (*)(DWORD))blk[1])(blk[2]);
                    LHFree(blk);
                }
                F3ObjTreeStep20(iter);
            }
        }

        // ---- walk 2 (list 2): raw callbacks ----
        {
            DWORD iter[2];
            iter[0] = g_contList2.wrapper;
            iter[1] = *(DWORD *)g_contList2.sentinel;
            DWORD sent2 = g_contList2.sentinel;
            while (1)
            {
                if (iter[0] == 0 || iter[0] != g_contList2.wrapper)
                    F3InvalidParameterNoop();
                if (iter[1] == sent2)
                    break;
                DWORD head = iter[0];
                if (head != 0)
                    head = *(DWORD *)head;
                else
                    F3InvalidParameterNoop();
                if (iter[1] == *(DWORD *)(head + 0x18))
                    F3InvalidParameterNoop();
                ((void (*)(void))(*(DWORD *)(iter[1] + 0xC)))();
                F3ObjTreeStep20(iter);
            }
        }

        // ---- release the device objects + clear the local container ----
        ReleaseDeviceObjects();
        {
            // A 32-byte local container: wrapper at +0x00, sentinel (v51) at
            // +0x18, count at +0x1C (sub_566CB0 reads [container + 0x18]).
            struct F3LocalContainer
            {
                DWORD wrapper;   // +0x00 (v50)
                char  pad[20];
                DWORD sentinel;  // +0x18 (v51)
                DWORD count;     // +0x1C
            } lc;
            lc.wrapper  = local_v50;
            lc.sentinel = local_v51;
            lc.count    = 0;
            DWORD out2[2];
            F3ObjRangeClear28((DWORD)&lc, out2, (int)local_v50,
                              (int)*(DWORD *)local_v51, (int)local_v50,
                              (int)local_v51);
        }
        LHFree((void *)local_v51);
        local_v51 = 0;
        LHFree((void *)local_v50);
    }

    // ---- recreate the device through vtable[16] with the new caps ----
    {
        int (*reset)(void *, const DWORD *) =
            (int (*)(void *, const DWORD *))vt[16];
        int r = reset(dev, caps.field);
        if (r != 0)
        {
            LeaveCriticalSection(&g_stru_20FC948);
            return 0;
        }
    }

    // ---- record the caps + derive the mode class ----
    {
        DWORD mc = caps.field[13];
        if (mc & 0x80000001)
            g_dword_1E2ED74 = 1;
        else if (mc & 2)
            g_dword_1E2ED74 = 2;
        else if (mc & 4)
            g_dword_1E2ED74 = 3;
        else
            g_dword_1E2ED74 = (mc & 8) ? 4 : 1;
    }
    memcpy(g_dword_1E2ED80, caps.field, sizeof(caps.field));

    // ---- re-initialize the device state ----
    InitXloDeviceState();

    // ---- walk 4 (list 1): object vtable[2] re-notification ----
    {
        DWORD iter[2];
        iter[0] = g_contList1.wrapper;
        iter[1] = *(DWORD *)g_contList1.sentinel;
        DWORD sent1 = g_contList1.sentinel;
        while (1)
        {
            if (iter[0] == 0 || iter[0] != g_contList1.wrapper)
                F3InvalidParameterNoop();
            if (iter[1] == sent1)
                break;
            DWORD head = iter[0];
            if (head != 0)
                head = *(DWORD *)head;
            else
                F3InvalidParameterNoop();
            if (iter[1] == *(DWORD *)(head + 0x18))
                F3InvalidParameterNoop();
            DWORD *objSlot = (DWORD *)(iter[1] + 0xC);
            DWORD out_pair[2];
            F3ObjIterInit28(out_pair, objSlot);
            if (out_pair[0] == 0 || out_pair[0] != g_contMap.wrapper)
                F3InvalidParameterNoop();
            DWORD node = out_pair[1];
            DWORD block = 0, aux = 0;
            if (node != g_contMap.sentinel)
            {
                DWORD head2 = out_pair[0];
                if (head2 != 0)
                    head2 = *(DWORD *)head2;
                else
                    F3InvalidParameterNoop();
                if (node == *(DWORD *)(head2 + 0x18))
                    F3InvalidParameterNoop();
                block = *(DWORD *)(node + 0x14);
                aux   = *(DWORD *)(node + 0x10);
                if (block != 0)
                    InterlockedIncrement((volatile LONG *)block);
            }
            DWORD head3 = iter[0];
            if (head3 != 0)
                head3 = *(DWORD *)head3;
            else
                F3InvalidParameterNoop();
            if (iter[1] == *(DWORD *)(head3 + 0x18))
                F3InvalidParameterNoop();
            DWORD obj = *objSlot;
            if (block != 0)
                InterlockedIncrement((volatile LONG *)block);
            void **ovt2 = *(void ***)obj;
            ((void (__thiscall *)(void *, DWORD, DWORD))ovt2[2])((void *)obj, aux, block);
            if (block != 0 &&
                InterlockedDecrement((volatile LONG *)block) == 0)
            {
                DWORD *blk = (DWORD *)block;
                ((void (*)(DWORD))blk[1])(blk[2]);
                LHFree(blk);
            }
            F3ObjTreeStep20(iter);
        }
    }

    // ---- free the map tree + reset its sentinel (no-op when empty) ----
    F3ObjNodeFree28((DWORD *)*(DWORD *)(g_contMap.sentinel + 4));
    *(DWORD **)(g_contMap.sentinel + 4) = (DWORD *)g_contMap.sentinel;
    g_contMap.count = 0;
    *(DWORD *)g_contMap.sentinel = g_contMap.sentinel;
    *(DWORD **)(g_contMap.sentinel + 8) = (DWORD *)g_contMap.sentinel;

    // ---- walk 3 (list 3): raw callbacks ----
    {
        DWORD iter[2];
        iter[0] = g_contList3.wrapper;
        iter[1] = *(DWORD *)g_contList3.sentinel;
        DWORD sent3 = g_contList3.sentinel;
        while (1)
        {
            if (iter[0] == 0 || iter[0] != g_contList3.wrapper)
                F3InvalidParameterNoop();
            if (iter[1] == sent3)
                break;
            DWORD head = iter[0];
            if (head != 0)
                head = *(DWORD *)head;
            else
                F3InvalidParameterNoop();
            if (iter[1] == *(DWORD *)(head + 0x18))
                F3InvalidParameterNoop();
            ((void (*)(void))(*(DWORD *)(iter[1] + 0xC)))();
            F3ObjTreeStep20(iter);
        }
    }

    g_byte_2075F04 = 0;
    LeaveCriticalSection(&g_stru_20FC948);
    return 1;
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
        F3XloCaps resetCaps;
        memcpy(resetCaps.field, g_dword_1E2ED80, sizeof(resetCaps.field));
        if (ResetXloDevice(resetCaps) == 0)
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
//                            int a5, int a6, int a8, F3SharedBuffer **a9)
//
//  Under the worker CS: stores the parsed start-up parameters in the
//  worker's display-mode / requested-size fields (+0x240 a2, +0x244 a3,
//  +0x248 a4, +0x24C a5 byte, +0x24D a6 byte, +0x250 a8), converts the
//  wide locale slot (a9) into a fresh ANSI shared slot (AnsiFromWideSlot,
//  0x19FFAB0), assigns it into the worker's sharedBlock slot (+0x254,
//  CopySharedSlot, 0x1A01270), then releases the temporary local slot
//  (SharedBufferFree at zero buffer refcount + the g_Addend decrement). It
//  sets the pump state to 1 (window create), leaves the CS, runs the
//  accessibility-settings load (InitAccessibilitySettings, 0x18F72B0), blocks on event1
//  (+0x9C, INFINITE), and returns (hwnd != 0) -- nonzero only once the
//  main thread's pump (PumpWorker state 1) has created the worker window
//  and signalled event1. Null-guarded (the original assumes the worker
//  global is live; the standalone test has no worker).
// ============================================================
char CWorkerWin32_RunGame(void *worker, int a2, int a3, int a4, int a5, int a6,
                          int a8, F3SharedBuffer **wideSlot)
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
    AnsiFromWideSlot(wideSlot, &local);   // 0x19FFAB0 (wideSlot = pointer to the wide slot)
    CopySharedSlot((F3SharedBuffer **)(c + 0x254), &local);   // 0x1A01270
    if (local != 0)   // release the temporary local slot
    {
        if (InterlockedDecrement((volatile long *)((char *)local + 12)) <= 0)
            SharedBufferFree(local);
    }
    InterlockedDecrement(&g_Addend);

    w->workerState = 1;      // +0x22C: request the window create
    LeaveCriticalSection(&w->cs);

    InitAccessibilitySettings();   // 0x18F72B0

    WaitForSingleObjectEx(w->event1, INFINITE, FALSE);   // +0x9C
    return (w->hwnd != 0);
}

// Input event record (32 bytes) pushed onto the device's input event queue.
// The original never writes +0x04 / +0x1C (stack residue is copied too).
typedef struct
{
    float time;    // +0x00  GetTickCount()/1000.0 (stored as float)
    int   pad0;    // +0x04  (never written by the producer)
    int   type;    // +0x08  1 = press, 2 = release, 6 = wheel
    int   button;  // +0x0C  button number (1-based), 0 for axis / wheel
    float value;   // +0x10  wheel delta
    int   pad1a;   // +0x14  0
    int   pad1b;   // +0x18  0
    int   pad1;    // +0x1C  (never written by the producer)
} F3InputEvent;

// Raw-input report (0x28 bytes) filled by GetRawInputData (RID_INPUT, the
// 0x10-byte RAWINPUTHEADER size). Type 0 = mouse / input device.
typedef struct
{
    DWORD type;        // +0x00  0 = mouse / input device
    DWORD dwSize;      // +0x04
    DWORD unk08;       // +0x08
    DWORD unk0C;       // +0x0C
    BYTE  flags;       // +0x10  bit0: 1 = no relative axis motion this report
    BYTE  pad11[3];
    WORD  buttons;     // +0x14  bit 2k / 2k+1 = press / release of button k+1
                       //        (ten buttons); bit 10 (0x400) = wheel active
    WORD  wheelDelta;  // +0x16  signed 16-bit wheel delta
    DWORD unk18;       // +0x18
    LONG  xDelta;      // +0x1C  relative X motion
    LONG  yDelta;      // +0x20  relative Y motion
    BYTE  pad24[4];
} F3RawInputReport;

// 0x5CD210 -- the myvector overflow guard: __debugbreak, then throw
// std::length_error("myvector<T> too long") (invoking the non-NULL callback
// at 0x01DDCFC8 first). No return.
// [NOT byte-identical] refs the std::length_error / exception machinery and
// the high-VA exception-hook global (0x01DDCFC8).
static void (*g_dword_1DDCFC8)(const void *) = 0;   // 0x01DDCFC8: exception hook (0 here)
static void InputQueueOverflowThrow(void)
{
    __debugbreak();
    std::length_error err("myvector<T> too long");
    if (g_dword_1DDCFC8 != 0)
        g_dword_1DDCFC8(&err);
    throw err;
}

// 0x5CD460 -- allocate the input-queue record buffer: LHAlloc(0) when slots
// is 0, else LHAlloc(32 * slots); throws std::bad_alloc (stdext::bad_alloc in
// 2008, through the 0x01DDCFC8 callback) when 32 * slots overflows
// (0xFFFFFFFF / slots < 0x20).
// [NOT byte-identical] relative call to LHAlloc + the bad_alloc machinery.
static void *InputQueueAllocBuffer(unsigned int slots)
{
    if (slots == 0)
        return LHAlloc(0);
    if (0xFFFFFFFFu / slots < 0x20u)
    {
        std::bad_alloc err;
        if (g_dword_1DDCFC8 != 0)
            g_dword_1DDCFC8(&err);
        throw err;
    }
    return LHAlloc(32 * slots);
}

// 0x5CD830 -- copy the 32-byte input records in [src, srcEnd) to dst, one
// record at a time (skipping the copy when dst is null). Returns the advanced
// dst (dst + 32 * n).
static void *CopyInputRecords(void *dst, const void *src, const void *srcEnd)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (s != (const char *)srcEnd)
    {
        if (d != 0)
            memcpy(d, s, 32);
        s += 32;
        d += 32;
    }
    return (void *)d;
}

// 0x5CD5D0 -- fill [dst, dstEnd) with copies of the 32-byte record at src.
// Returns the advanced dst (dstEnd).
static void *FillInputRecords(void *dst, void *dstEnd, const void *src)
{
    char *d = (char *)dst;
    while (d != (char *)dstEnd)
    {
        memcpy(d, src, 32);
        d += 32;
    }
    return (void *)d;
}

// 0x5CD560 -- copy `count` 32-byte records from src to dst (skipping the
// copies when dst is null). Returns dst + 32 * count.
static void *CopyInputRecordsN(void *dst, const void *src, int count)
{
    char *d = (char *)dst;
    unsigned int n = (unsigned int)count;
    while (n-- > 0)
    {
        if (d != 0)
            memcpy(d, src, 32);
        d += 32;
    }
    return (void *)d;
}

// 0x5CD410 + 0x5CD7E0 -- move the 32-byte records of [marker, srcEnd) up one
// slot: the new value at P is the old value at P - 32 for P in
// [marker + 32, dstEnd), copied one record at a time, backward.
static void ShiftInputRecordsUp(void *marker, void *srcEnd, void *dstEnd)
{
    char *s = (char *)srcEnd;
    char *d = (char *)dstEnd;
    while (s != (char *)marker)
    {
        memcpy(d, s - 32, 32);
        s -= 32;
        d -= 32;
    }
}

// 0x5CCF70 -- insert a 32-byte input event record into the device's input
// queue (head +8, tail +0xC, limit +0x10; 32-byte slots). The record
// is copied to the stack at entry, then placed at `marker` within
// [head, tail): [marker, tail) shifts up one
// slot and the tail extends by 32; when the capacity cannot hold the pending
// records + 1, the buffer is grown to max(3/2 * capacity, pending + 1)
// (InputQueueAllocBuffer), the records of [head, marker), the new record and
// the records of [marker, tail) are copied into the new buffer, and the old
// one is freed through the heap table (dword_2075B24 + 16; skipped when head
// is 0). Throws std::length_error("myvector<T> too long") when the pending
// count is 0x7FFFFFF. Returns marker + 32 (or the heap-free EAX / 0 after a
// grow); the sole caller discards it. stdcall (retn 10h); arg 3 is unused.
// [NOT byte-identical] placement-dependent (relative calls to the copy / grow
// helpers and __security_check_cookie) plus the high-VA heap-table free slot
// (dword_2075B24 + 16, LHFree here) and exception-hook global (0x01DDCFC8).
static int ProcessInputQueue(void *queue, const void *event, int unused, void *marker)
{
    (void)unused;
    DWORD *q = (DWORD *)queue;
    DWORD ev[8];   // the 32-byte record, copied at entry (the original's var_2C)
    memcpy(ev, event, 32);

    DWORD head = q[2];
    DWORD tail = q[3];
    DWORD cap = (head != 0) ? (DWORD)((q[4] - head) >> 5) : 0;
    DWORD pending = (DWORD)((tail - head) >> 5);
    if (0x7FFFFFFu - pending < 1u)
        InputQueueOverflowThrow();   // 0x5CD210 (no return)
    DWORD need = pending + 1;

    if (cap >= need)
    {
        if (((tail - (DWORD)marker) >> 5) != 0)
        {
            // marker is below the tail: duplicate the last pending record into
            // the new slot, shift [marker, tail) up one slot, put the record
            // at marker.
            q[3] = (DWORD)(DWORD_PTR)CopyInputRecords((void *)tail, (void *)(tail - 32), (void *)tail);   // InputQueueCopyRangeThunk -> CopyInputRecords
            ShiftInputRecordsUp(marker, (void *)(tail - 32), (void *)tail);                                // ShiftInputRecordsUp
        }
        else
        {
            // marker == tail (the append case, as used by FlushInputQueue):
            // re-copy the record at the tail, write the new record there and
            // advance the tail.
            CopyInputRecords((void *)tail, marker, (void *)((DWORD)marker + 32));                          // InputQueueCopyRangeThunk -> CopyInputRecords
            CopyInputRecordsN((void *)tail, ev, 1 - (int)((tail - (DWORD)marker) >> 5));                   // InputQueueCopyAtThunk -> CopyInputRecordsN
            q[3] += 32;
        }
        return (int)(DWORD_PTR)FillInputRecords(marker, (void *)((DWORD)marker + 32), ev);                 // FillInputRecords
    }

    // Grow: 1.5x the capacity (0 on overflow), clamped up to `need`.
    DWORD half = cap >> 1;
    DWORD newcap = (0x7FFFFFFu - half >= cap) ? cap + half : 0;
    if (newcap < need)
        newcap = need;
    DWORD newbuf = (DWORD)(DWORD_PTR)InputQueueAllocBuffer(newcap);    // InputQueueAllocBuffer
    DWORD p1 = (DWORD)(DWORD_PTR)CopyInputRecords((void *)newbuf, (void *)head, marker);                   // CopyInputRecords
    CopyInputRecordsN((void *)p1, ev, 1);                                                                      // CopyInputRecordsN
    CopyInputRecords((void *)(p1 + 32), marker, (void *)tail);                                                 // CopyInputRecords
    DWORD count = (DWORD)((tail - head) >> 5) + 1;
    int result = 0;   // the heap-free thunk's EAX (0 when head is 0); the caller discards it
    if (head != 0)
        LHFree((void *)head);                                          // dword_2075B24 + 16
    q[4] = newbuf + 32 * newcap;
    q[3] = newbuf + 32 * count;
    q[2] = newbuf;
    return result;
}

// 0x5CCDE0 -- full-queue flush: guarantee room for one more input event by
// inserting it through ProcessInputQueue (append at the tail, growing the
// buffer if needed), report {0, head + 32*count} (head re-read after the
// insert) into the 8-byte out slot, and return the slot (a stack address, as
// in the original).
#pragma warning(push)
#pragma warning(disable : 4172)   // intentionally returns the stack slot address
static void *FlushInputQueue(void *queue, void *event, DWORD tail)
{
    DWORD *q = (DWORD *)queue;
    DWORD head = q[2];
    DWORD count = (((q[3] - head) & 0xFFFFFFE0u) != 0) ? (tail - head) >> 5 : 0;
    ProcessInputQueue(queue, event, 0, (void *)tail);
    DWORD slot[2] = { 0, 0 };
    slot[1] = q[2] + 32 * count;   // head re-read after the insert
    return slot;
}
#pragma warning(pop)

// 0x5CCC90 -- push a 32-byte input event record onto the queue. When the
// queue is full (or uninitialized, head == 0) it flushes through
// FlushInputQueue (which guarantees room and inserts the event at the tail
// via ProcessInputQueue). Returns the new tail (or the flush slot on a full
// queue).
static void *PushInputEvent(void *queue, const void *event)
{
    DWORD *q = (DWORD *)queue;
    DWORD head = q[2];
    DWORD cap = (head != 0) ? (q[4] - head) >> 5 : 0;
    DWORD tail = q[3];
    if ((tail - head) >> 5 >= cap)
        return FlushInputQueue(queue, (void *)event, tail);
    if (tail != 0)
        memcpy((void *)tail, event, 32);
    q[3] = tail + 32;
    return (void *)(tail + 32);
}

// ============================================================
//  HandleRawInput  (0x188D710 - 0x188D8DF)  -- handle a raw-input report
//  (WM_INPUT) for the active input device.
//  [NOT byte-identical] placement-dependent (relative calls to the vec
//  component get / set helpers sub_412AB0 / sub_412B00 / sub_412B50 /
//  sub_4129F0 / sub_417C50 / sub_417B60, folded into direct float4
//  accesses, and to PushInputEvent / FlushInputQueue) plus the
//  GetRawInputData / GetTickCount IAT slots and the dbl_1BF9478 (1000.0)
//  / dbl_1BF9390 (127.0) data refs.
//
//  Original: UINT __stdcall HandleRawInput(device, HRAWINPUT). device is
//  the active input device object:
//    +0x5C float sensitivity
//    +0x70 float4 state {x, y, z, wheelAccum}
//    +0xA8 input event queue (see PushInputEvent)
//
//  Reads the 0x28-byte report with GetRawInputData (RID_INPUT, the 0x10-byte
//  header size). For a type-0 (mouse / input device) report it builds a
//  stack event {time = GetTickCount()/1000, ...} and, when the report's
//  flags byte has no "no motion" bit, moves the x / y state by
//  delta * sensitivity / 127 (x87 float arithmetic); when the 0x400 button
//  bit is set it records a type-6 wheel event (value = wheel delta) and
//  accumulates the wheel state (state.w += delta); then it records a press
//  (type 1) / release (type 2) event for each of the ten buttons encoded as
//  2-bit pairs in report.buttons. Returns the result of the last operation
//  (GetRawInputData, the tick count, the new y / wheel float, or the queue
//  tail after a push).
// ============================================================
static UINT HandleRawInput(void *obj, void *hRawInput)
{
    char *o = (char *)obj;
    F3RawInputReport report;
    UINT reportSize = sizeof(report);
    UINT result = GetRawInputData((HRAWINPUT)hRawInput, RID_INPUT, &report,
                                  &reportSize, (UINT)sizeof(RAWINPUTHEADER));
    if (report.type != 0)
        return result;

    float *state = (float *)(o + 0x70);      // the float4 state
    float sensitivity = *(float *)(o + 0x5C);
    void *queue = o + 0xA8;

    F3InputEvent event;
    event.type = 1;
    event.button = 0;
    event.value = 0.0f;
    event.pad1a = 0;
    event.pad1b = 0;
    result = GetTickCount();
    event.time = (float)((double)(LONG)result / 1000.0);

    if ((report.flags & 1) == 0)
    {
        // The original rounds each scaled delta to float (FSTP m32) before
        // adding it to the state (FADD m32).
        float dx = (float)((double)report.xDelta * (double)sensitivity / 127.0);
        float dy = (float)((double)report.yDelta * (double)sensitivity / 127.0);
        state[0] = (float)((double)state[0] + dx);
        state[1] = (float)((double)state[1] + dy);
        result = (UINT)state[1];   // the y-setter (sub_4129F0) returns the new y
    }
    if ((report.buttons & 0x400) != 0)
    {
        int wheelDelta = (short)report.wheelDelta;   // movsx: sign-extended
        event.type = 6;
        event.value = (float)wheelDelta;
        PushInputEvent(queue, &event);
        float wheel = (float)((double)state[2] + (float)wheelDelta);
        state[3] = wheel;
        result = (UINT)wheel;      // the wheel-setter (sub_417B60) returns the new value
    }
    for (int shift = 0, button = 1, n = 5; n != 0; shift += 2, ++button, --n)
    {
        int pair = (report.buttons >> shift) & 3;
        if (pair & 1)
        {
            event.type = 1;
            event.button = button;
            result = (UINT)(DWORD_PTR)PushInputEvent(queue, &event);
        }
        if (pair & 2)
        {
            event.type = 2;
            event.button = button;
            result = (UINT)(DWORD_PTR)PushInputEvent(queue, &event);
        }
    }
    return result;
}

// 0x5CB8C0 -- allocate the event-queue record buffer: LHAlloc(0) when slots
// is 0, else LHAlloc(4 * slots); throws std::bad_alloc (stdext::bad_alloc in
// 2008, through the 0x01DDCFC8 callback) when 4 * slots overflows (the
// 0xFFFFFFFF / slots < 4 guard). See InputQueueAllocBuffer (the 32-byte
// sibling) for the same shape.
// [NOT byte-identical] relative call to LHAlloc + the bad_alloc machinery.
static void *EventQueueAllocBuffer(unsigned int slots)
{
    if (slots == 0)
        return LHAlloc(0);
    if (0xFFFFFFFFu / slots < 4u)
    {
        std::bad_alloc err;
        if (g_dword_1DDCFC8 != 0)
            g_dword_1DDCFC8(&err);
        throw err;
    }
    return LHAlloc(4 * slots);
}

// 0x5CB1B0 -- the event-queue overflow guard: __debugbreak, then throw
// std::length_error("myvector<T> too long") (invoking the non-NULL callback
// at 0x01DDCFC8 first). No return.
// [NOT byte-identical] refs the std::length_error / exception machinery and
// the high-VA exception-hook global (0x01DDCFC8).
static void EventQueueOverflowThrow(void)
{
    __debugbreak();
    std::length_error err("myvector<T> too long");
    if (g_dword_1DDCFC8 != 0)
        g_dword_1DDCFC8(&err);
    throw err;
}

// 0x5CAAC0 -- grow the event queue to max(3/2 * capacity, pending + 1) --
// 0 slots on the 0x3FFFFFFF overflow -- copy [head, tail) into the new
// buffer, insert the record at the new tail (newbuf + 4 * pending), free
// the old buffer through the heap table (dword_2075B24 + 16 -- LHFree;
// skipped when head is 0) and rewire head / tail / limit. Throws
// std::length_error ("myvector<T> too long") when the pending count is
// 0x3FFFFFFF. The fit branch (capacity >= pending + 1) is unreachable
// from this caller: the queue is only full here, so pending >= capacity.
// [NOT byte-identical] placement-dependent (LHAlloc / LHFree / memcpy)
// plus the exception-hook global (0x01DDCFC8).
static void *EventQueueInsert(DWORD *q, const void *record)
{
    DWORD head = q[2];
    DWORD cap = (head != 0) ? (DWORD)((q[4] - head) >> 2) : 0;
    DWORD tail2 = q[3];
    DWORD pend = (DWORD)((tail2 - head) >> 2);
    if (pend == 0x3FFFFFFu)
        EventQueueOverflowThrow();
    DWORD half = cap >> 1;
    DWORD newcap = (0x3FFFFFFu - half >= cap) ? cap + half : 0;
    if (newcap < pend + 1)
        newcap = pend + 1;
    DWORD newbuf = (DWORD)(DWORD_PTR)EventQueueAllocBuffer(newcap);
    // copy the existing records [head, tail) into the new buffer
    DWORD p = newbuf;
    DWORD src = head;
    while (src != tail2)
    {
        memcpy((void *)p, (const void *)src, 4);
        p += 4;
        src += 4;
    }
    // insert the record at the new tail (newbuf + 4 * pend)
    *(DWORD *)((char *)newbuf + 4 * (int)pend) = ((const DWORD *)record)[0];
    if (head != 0)
        LHFree((void *)head);   // dword_2075B24 + 16
    q[4] = newbuf + 4 * newcap;
    q[3] = newbuf + 4 * (pend + 1);
    q[2] = newbuf;
    return (void *)newbuf;
}

// 0x5CA450 -- full-queue flush: grow the queue and insert the record at the
// new tail (via EventQueueInsert), then report {0, the record's slot} into
// the 8-byte out slot and return the slot (a stack address, as in the
// original); the sole caller (sub_5C9F50's full path) discards it.
// [NOT byte-identical] placement-dependent (EventQueueInsert / memcpy).
static void *EventQueueFlush(DWORD *q, const void *record, DWORD tailOld)
{
    DWORD head = q[2];
    DWORD ebx = 0;
    if (((q[3] - head) & ~3u) != 0)
        ebx = (tailOld - head) >> 2;   // pending (the record being pushed)
    DWORD newbuf = (DWORD)(DWORD_PTR)EventQueueInsert(q, record);
#pragma warning(push)
#pragma warning(disable : 4172)   // intentionally returns the stack slot address
    DWORD slot[2] = { 0, newbuf + 4 * ebx };
    return slot;
#pragma warning(pop)
}

// 0x5C9F50 -- push a 4-byte event record onto the event queue (head +8, tail
// +0xC, limit +0x10 -- head/tail/limit as 4-byte slots). Appends the record
// at the tail while the capacity allows; on a full queue it flushes through
// EventQueueFlush (which grows the buffer and inserts the record) and returns
// the flush out-slot. Returns the new tail in the fits path.
// [NOT byte-identical] placement-dependent (memcpy + the flush call).
static void *EventQueuePush(DWORD *q, const void *record)
{
    DWORD head = q[2];
    DWORD cap = (head != 0) ? (DWORD)((q[4] - head) >> 2) : 0;
    DWORD tail = q[3];
    if ((DWORD)((tail - head) >> 2) >= cap)
        return EventQueueFlush(q, record, tail);
    if (tail != 0)
        memcpy((void *)tail, record, 4);
    tail += 4;
    q[3] = tail;
    return (void *)tail;
}

// 0x188F140 -- record an input event (obj, WORD code, BYTE state). The
// original takes obj's CS (obj + 0x4C) and calls sub_5C9F50(&record, obj + 0x18).
// The recorder builds the 4-byte record {code (+0, WORD), state (+2, BYTE)}
// (the +3 slot holds uninitialized stack data in the original -- zeroed here)
// and pushes it through the event queue, then leaves the CS. The recorder's
// return value (the flush out-slot / the new tail) is discarded.
// [NOT byte-identical] placement-dependent (EnterCriticalSection /
// LeaveCriticalSection IAT slots + the EventQueuePush call).
static void RecordInputEvent(void *obj, unsigned short code, char state)
{
    if (obj == 0)
        return;
    char *o = (char *)obj;
    DWORD record = (DWORD)(WORD)code | ((DWORD)(BYTE)state << 16);
    EnterCriticalSection((LPCRITICAL_SECTION)(o + 76));
    EventQueuePush((DWORD *)(o + 24), &record);
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
// parameters the game overrode (SPI_SETHIDCONFIRM / SPI_SETKEYBOARDSPEED /
// SPI_SETKEYBOARDDelay), then, when AccessibilitySettings.xml exists in the
// settings directory, deletes it (a fresh ref of the settings-directory slot
// + the filename, DeleteSharedFile) and returns the release's new g_Addend
// count; otherwise returns the g_Addend count after the existence-check
// release. Non-static: InitAccessibilitySettings / ApplyDisplaySettings
// (gamemain.cpp) and the WM_CLOSE path (DispatchWorkerMessage) call it.
LONG SaveSettingsBeforeExit(void)
{
    SystemParametersInfoA(0x3B, 8, &g_dword_1D9E18C, 0);
    SystemParametersInfoA(0x35, 8, &g_dword_1D9E194, 0);
    SystemParametersInfoA(0x33, 0x18, (PVOID)g_byte_1D9E19C, 0);

    F3SharedBuffer *path = 0;
    RefSharedSlot(&path, &g_dword_1DBE2F0);
    AppendSharedSlot(&path, "AccessibilitySettings.xml");
    char exists = SharedFileExists(&path);
    if (path != 0)
    {
        if (InterlockedDecrement(&path->refcount) <= 0)
            SharedBufferFree(path);
        path = 0;
    }
    LONG result = InterlockedDecrement(&g_Addend);
    if (exists != 0)
    {
        F3SharedBuffer *path2 = 0;
        RefSharedSlot(&path2, &g_dword_1DBE2F0);
        AppendSharedSlot(&path2, "AccessibilitySettings.xml");
        DeleteSharedFile(&path2);
        if (path2 != 0)
        {
            if (InterlockedDecrement(&path2->refcount) <= 0)
                SharedBufferFree(path2);
            path2 = 0;
        }
        return InterlockedDecrement(&g_Addend);
    }
    return result;
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
                    DeleteSharedFile(&slot);           // 0x18B0420 (EAX = &slot in the original)
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
    // TODO THIS IS TEMPORARY: the original's shared slot is not guaranteed to be valid at this point, so the class name may be null/empty. The original assumes it is valid and will crash if it is not. The fallback here is to use the default class name (the original's hard-coded "Fable 3" string).
    // The class name is the ApplicationName slot (0x1DBD048, the worker window
    // title). If it is unset or empty the slot's data pointer is null/"" and
    // CreateWindowExA fails (empty class name -> NULL handle), so no window is
    // ever shown. Fall back to the original's window class/title.
    if (className == 0 || className[0] == '\0')
        className = "Fable 3";

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

// TrySetDisplayMode (0x18F28C0) and its helpers are defined below, after the
// XLO device-build forward declarations they need (PickBestDisplayMode /
// BuildXloDeviceFromRequest / F3FindFormatRow) and after F3_FORMAT_TABLE_END.
static int TrySetDisplayMode(const F3DisplayMode *dm);

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

// d3d9.dll import (bound through the SDK d3d9.lib import library): the
// D3D9 factory the original pulls to (re)create the adapter object.
// The stock d3d9.dll exports "Direct3DCreate9" with a __stdcall
// (WINAPI) convention; it returns an IDirect3D9* (a vtable-backed object)
// or NULL.
extern "C" void *WINAPI Direct3DCreate9(UINT SDKVersion);

// ============================================================
//  F3EnumAdapterModes  (sub_18F1FC0, 0x18F1FC0 - 0x18F219B, 475 bytes)
//  -- enumerate the selected adapter's display modes into a local container
//
//  void *F3EnumAdapterModes(void *container, DWORD adapterId, int flags)
//
//  Original: thiscall (the 20-byte local `container` in ECX, adapterId and
//  flags on the stack, caller cleans 8). Walks the rdata adapter table at
//  unk_1BD3020 ({0x1F, 0x15, 0x16, 0x00} -- stops at the 0 sentinel) and, for
//  each D3DFORMAT, queries the live D3D9 object (dword_1E2EEF8) through its
//  vtable: vtable[6] (offset 0x18) returns the mode count for the format and
//  vtable[7] (offset 0x1C) fills each mode's 4-DWORD descriptor
//  {width, height, refresh, pixelFormat}. Each unique {width, height} pair is
//  collected in a local red-black tree keyed on the 8-byte pair. The tree is
//  internal: it is destroyed before the function returns and the container's
//  vector is reset to empty below, so the dedup has no observable effect on
//  the returned value. The 20-byte container (8-byte header + a 3-pointer
//  8-byte-element vector) is returned with its vector words (+8/+0xC/+0x10)
//  zeroed; the 8-byte header (+0/+4) is left untouched. Returns the container.
//
//  [NOT byte-identical] refs the high-VA D3D9 object g_dword_1E2EEF8
//  (0x01E2EEF8) and the rdata format table unk_1BD3020 (0x01BD3020) at their
//  original addresses, and dispatches into the D3D9 object's vtable[6]/[7].
// ============================================================
static void *F3EnumAdapterModes(void *container, DWORD adapterId, int flags)
{
    DWORD *c = (DWORD *)container;
    const int filter = flags & 0x20;
    const DWORD d3d = g_dword_1E2EEF8;
    if (d3d != 0)
    {
        // Stock d3d9 COM dispatches (this in ECX, args after it):
        //   slot 6 = GetAdapterModeCount(Adapter, Format) -> UINT
        //   slot 7 = EnumAdapterModes(Adapter, Format, Mode, D3DDISPLAYMODE*)
        void **vt = *(void ***)d3d;
        typedef UINT (__stdcall *F3D3D9ModeCountFn)(void *, DWORD, DWORD);
        typedef void (__stdcall *F3D3D9ModeFn)(void *, DWORD, DWORD, DWORD, DWORD *);
        F3D3D9ModeCountFn countFn = vt != NULL ? (F3D3D9ModeCountFn)vt[6] : 0;
        F3D3D9ModeFn modeFn = vt != NULL ? (F3D3D9ModeFn)vt[7] : 0;
        // The rdata adapter table unk_1BD3020: {0x1F, 0x15, 0x16, 0x00}
        // (D3DFMT_X1 / D3DFMT_A8R8G8B8 / D3DFMT_A8B8G8R8, then the 0 sentinel).
        static const DWORD fmtTable[] = { 0x1F, 0x15, 0x16, 0x00 };
        if (countFn != 0 && modeFn != 0)
            for (int i = 0; fmtTable[i] != 0; ++i)
        {
            const DWORD fmt = fmtTable[i];
            const int count = (int)countFn((void *)d3d, adapterId, fmt);
            if (count <= 0)
                continue;
            for (int m = 0; m < count; ++m)
            {
                DWORD mode[4] = { 0, 0, 0, 0 };
                modeFn((void *)d3d, adapterId, fmt, (DWORD)m, mode);
                if (filter != 0 && (DWORD)mode[0] < (DWORD)mode[1])
                    continue;   // (flags & 0x20): drop landscape modes
                // (the unique {mode[0], mode[1]} pair is inserted into the
                //  local red-black tree; the tree is destroyed before the
                //  return and the vector is reset below, so this has no
                //  observable effect here.)
            }
        }
    }
    // Reset the container's 8-byte-element vector to empty (begin/end/capacity
    // at +8/+0xC/+0x10); the 8-byte header at +0/+4 is left untouched.
    c[2] = 0;
    c[3] = 0;
    c[4] = 0;
    return container;
}

// ============================================================
//  F3ReserveModeList  (0x563AE0 - 0x563BED)  -- size the shared mode
//  buffer (dword_20FC978 / dword_20FC97C / dword_20FC980) to hold the
//  enumerated-mode list
//
//  void *__stdcall F3ReserveModeList(void *list)
//
//  Original: __stdcall (list pointer on the stack); the list is the 20-byte
//  mode-list container whose vector words are +8 (begin), +12 (end) and
//  +16 (capacity-end). Grows the shared buffer to fit the list's current
//  element count (list+12 - list+8):
//    - list == &unk_20FC970 (the sentinel descriptor block): trivial return.
//    - v1 == 0 (empty list): reset the buffer to empty (sub_5648B0 -- when
//      the buffer is non-empty, set dword_20FC97C = dword_20FC978) and return
//      &unk_20FC970. The 4-byte stack descriptor sub_565BE0 writes is dead in
//      the original (the function ignores its return value) and is omitted.
//    - v1 <= used (the current fill already holds >= v1 elements): copy the
//      list into the front of the buffer (sub_568B50) and set
//      dword_20FC97C = buffer start + 8 * v1.
//    - v1 <= capacity (fits the allocated buffer): sub_568B50 copies the first
//      "used" elements of the list into the front of the buffer, then the
//      tail call sub_568B80 appends the remaining elements after the current
//      buffer end (dword_20FC978 .. dword_20FC97C); dword_20FC97C = the tail
//      call's return (the new buffer end).
//    - otherwise (needs a larger buffer): free the old buffer through the
//      heap (dword_2075B24 + 16) and, with v5 = the list's byte length, zero
//      the buffer words and allocate a fresh 8 * (v5 >> 3) byte buffer via
//      sub_568D00 (the LHAlloc path): a run longer than 0x1FFFFFFF elements
//      throws std::length_error("myvector<T> too long") (sub_565DF0), and
//      std::bad_alloc is thrown when 0xFFFFFFFF / count < 8. sub_5648E0 then
//      fills the fresh buffer's descriptor words (start/end/capacity-end)
//      and returns 1; the tail call sub_568B80 copies the list into the new
//      buffer and dword_20FC97C = its return (the new buffer end).
//  In every path the function returns &unk_20FC970 (the sentinel descriptor
//  block at 0x20FC970).
//
//  [NOT byte-identical] refs the high-VA descriptor block (0x20FC970 --
//  unk_20FC970 / dword_20FC978 / dword_20FC97C / dword_20FC980) at its original
//  address, is placement-dependent (relative E8 calls to sub_5648B0 /
//  sub_568B50 / sub_568B80 / sub_5648E0 / sub_568D00 / sub_565DF0) and refs the
//  heap-table free dispatch (dword_2075B24 + 16) and the
//  std::length_error / std::bad_alloc machinery.
// ============================================================
static DWORD g_unk_20FC970[5] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, 0 };
                                          // 0x20FC970  the 20-byte mode-list
                                          // descriptor block: +0x08 dword_20FC978
                                          // (buffer start), +0x0C dword_20FC97C
                                          // (buffer end), +0x10 dword_20FC980
                                          // (capacity-end)

// sub_568B50 -- copy the 8-byte elements [a2, a3) into [a1, ...) one element
// at a time; returns a1 + 8 * count (the new buffer end).
static void *F3ModeBufferCopy(DWORD a1, DWORD a2, DWORD a3)
{
    DWORD result = a1 + 8u * ((int)(a3 - a2) >> 3);
    if (a2 != a3)
    {
        char *dest = (char *)a1;
        for (DWORD src = a2; src != a3; src += 8u)
        {
            *(DWORD *)dest          = *(DWORD *)src;
            *(DWORD *)(dest + 4)    = *((DWORD *)src + 1);
            dest += 8;
        }
    }
    return (void *)result;
}

// sub_568B80 -- copy the 8-byte elements [a1, a2) into the buffer starting at
// a3 (the write base); returns the new buffer end. The original performs the
// same per-iteration null guard (a null write base skips the copy for the
// current stride and the loop advances -- a behaviour the game never reaches
// since the allocation only fails on OOM).
static DWORD *F3ModeBufferAppend(DWORD a1, DWORD a2, DWORD a3)
{
    DWORD *result = (DWORD *)a3;
    if (a1 != a2)
    {
        int diff = (int)(a1 - a3);   // (a1 - bufferBase) in bytes
        do
        {
            if (result != 0)
            {
                *result = *(DWORD *)((char *)result + diff);
                result[1] = *(DWORD *)((char *)result + diff + 4);
            }
            result += 2;
        }
        while ((char *)result + diff != (char *)a2);
    }
    return result;
}

// sub_568D00 -- allocate 8 * count bytes through the LHAlloc path. Throws
// std::bad_alloc when 0xFFFFFFFF / count < 8 (the run would overflow).
static void *F3ModeAllocElements(unsigned int count)
{
    if (count == 0u)
        return LHAlloc(0u);
    if (0xFFFFFFFFu / count < 8u)
    {
        std::bad_alloc err;
        if (g_dword_1DDCFC8 != 0)
            g_dword_1DDCFC8(&err);
        throw err;
    }
    return LHAlloc(8u * count);
}

// sub_5648E0 -- fill the fresh mode-list buffer's descriptor words (start =
// end = the allocation, capacity-end = start + 8 * count) and return 1.
// Throws (via sub_565DF0) std::length_error("myvector<T> too long") when
// count > 0x1FFFFFFF before any allocation happens.
static int F3ModeReserveFill(unsigned int count, DWORD *block)
{
    if (count > 0x1FFFFFFFu)
    {
        std::length_error err("myvector<T> too long");
        if (g_dword_1DDCFC8 != 0)
            g_dword_1DDCFC8(&err);
        throw err;
    }
    DWORD v2 = (DWORD)F3ModeAllocElements(count);
    block[2] = v2;
    block[3] = v2;
    block[4] = v2 + 8u * count;
    return 1;
}

static void *__stdcall F3ReserveModeList(void *list)
{
    DWORD *block = g_unk_20FC970;
    DWORD start = block[2];
    DWORD end = block[3];

    if (list == (void *)block)
        return (void *)block;

    DWORD begin = *(DWORD *)((DWORD)list + 8);
    DWORD listEnd = *(DWORD *)((DWORD)list + 12);
    unsigned int v1 = (int)(listEnd - begin) >> 3;

    if (v1 == 0u)
    {
        // sub_5648B0 -- reset the buffer to empty (start != end).
        if (start != end)
            block[3] = start;
        return (void *)block;
    }

    if (v1 <= (end - start) >> 3)
    {
        // v1 <= used: copy the list into the front of the buffer and set the
        // new end (sub_568B50; the result is ignored).
        (void)F3ModeBufferCopy(start, begin, listEnd);
        block[3] = start + 8u * v1;
        return (void *)block;
    }

    DWORD capacity = (block[4] - start) >> 3;
    DWORD used = (end - start) >> 3;
    if (v1 <= capacity)
    {
        // v1 <= capacity: sub_568B50 copies the first "used" elements of the
        // list into the front of the buffer, then the shared tail calls
        // sub_568B80 with the current buffer end as the write base.
        (void)F3ModeBufferCopy(start, begin, begin + 8u * used);
        block[3] = (DWORD)F3ModeBufferAppend(begin + 8u * used, listEnd, end);
        return (void *)block;
    }

    // v1 > capacity: free the old buffer and allocate a fresh one.
    if (start != 0u)
        LHFree((void *)start);
    int v5 = (int)(listEnd - begin);
    block[2] = 0;
    block[3] = 0;
    block[4] = 0;
    if ((v5 >> 3) == 0 || F3ModeReserveFill((unsigned int)(v5 >> 3), block) == 0)
        return (void *)block;

    // The shared tail (realloc path): copy the list into the fresh buffer.
    block[3] = (DWORD)F3ModeBufferAppend(begin, listEnd, block[2]);
    return (void *)block;
}

// ============================================================
//  F3DestroyModeListContainer  (0x563770 - 0x56379F, 47 bytes)  -- destroy the
//  local 20-byte mode-list container built by F3EnumAdapterModes
//
//  void F3DestroyModeListContainer(void *container)
//
//  Original: thiscall-like (container in ECX; retn with no stack cleanup).
//  Loads the container's begin word (+8, the 8-byte-element buffer) and, when
//  it is non-null, releases it through the heap-table free slot (the
//  dword_2075B24 + 0x10 dispatch); the result lands in EAX as the function's
//  return value. Then zeroes the vector words (+8/+0xC/+0x10) and returns.
//  (The original leaves EAX = the free-slot result, which this build cannot
//  reproduce -- the heap table's free slot is void here and the one call
//  site discards the result -- so this build returns void, matching the
//  original's observable behavior at the single call site.) The header words
//  (+0/+4) are left untouched.
//
//  [NOT byte-identical] refs the heap-table pointer global (0x02075B24) at
//  the original high VA not present in this build and dispatches through the
//  heap table's free slot.
// ============================================================
static void F3DestroyModeListContainer(void *container)
{
    DWORD *c = (DWORD *)container;
    if (c[2] != 0u)
        LHFree((void *)c[2]);
    c[2] = 0;
    c[3] = 0;
    c[4] = 0;
}

// ============================================================
//  F3IsOsMajorVersionAtLeast6  (0x1887820 - 0x188787F)  -- the OS >= 6 gate
//
//  int F3IsOsMajorVersionAtLeast6(void)
//
//  Original (sub_1887820): fills an 148-byte OSVERSIONINFOA with
//  dwOSVersionInfoSize = 148 and calls GetVersionExA; returns 1 when the
//  call succeeds and dwMajorVersion >= 6, else 0.
// ============================================================
static int F3IsOsMajorVersionAtLeast6(void)
{
    OSVERSIONINFOA vi;
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = 148;
    if (GetVersionExA(&vi))
        return vi.dwMajorVersion >= 6 ? 1 : 0;
    return 0;
}

// ============================================================
//  F3QueryAdapterViaExtEscape  (0x1887880 - 0x188793A)  -- the legacy
//  CreateDCA / ExtEscape adapter probe (OS < 6 only)
//
//  int F3QueryAdapterViaExtEscape(int *outValue)
//
//  Original (sub_1887880): zeroes a 320-byte out buffer, fills a 4-byte
//  input word with 16, runs CreateDCA("DISPLAY", NULL, NULL, NULL), then
//  ExtEscape(hdc, 0x7306, 0x50, InData, 0x140, OutData) (cjInput 80 /
//  cjOutput 320), DeleteDC(hdc). When ExtEscape returns 0 (success), the
//  2nd DWORD of the out buffer (OutData[4..7]) is written to *outValue and
//  1 is returned; otherwise 0.
// ============================================================
static int F3QueryAdapterViaExtEscape(int *outValue)
{
    BYTE OutData[320];
    BYTE InData[4];
    HDC hdc;
    int v11;

    memset(OutData, 0, sizeof(OutData));
    *(DWORD *)InData = 16;
    hdc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    v11 = ExtEscape(hdc, 29446 /* 0x7306 */, 80 /* 0x50 */, (LPCSTR)InData, 320 /* 0x140 */, (LPSTR)OutData);
    DeleteDC(hdc);
    if (v11 == 0)
    {
        *outValue = *(int *)&OutData[4];
        return 1;
    }
    return 0;
}

// ============================================================
//  [STUB]  sub_1887940  (0x1887940 - 0x1887BB6)  -- the Vista+ D3DKMT probe
//
//  The 630-byte D3DKMT adapter probe (LoadLibraryA("user32.dll") +
//  EnumDisplayDevicesA, LoadLibraryA("gdi32.dll") + D3DKMTEscape +
//  D3DKMTOpenAdapterFromHdc, then sub_1887BC0's display-device walk + the
//  D3DKMTEscape call with a 0x218-byte struct) is a documented remaining
//  stub. It returns 0 here, which sends the caller down the v5 == 0 ->
//  v4 = 1 -> return 1 path.
// ============================================================
static int sub_1887940(int *outValue)
{
    (void)outValue;
    return 0;
}

// ============================================================
//  F3QueryAdapterState  (0x1887790 - 0x1887814)  -- seed the one-word state
//  and run the OS-gated adapter probe
//
//  int F3QueryAdapterState(int adapterIndex, int *outValue)
//
//  Original (sub_1887790): checks the OS gate (F3IsOsMajorVersionAtLeast6);
//  null outValue -> return 0. When the gate is set, runs the Vista+
//  D3DKMT probe (sub_1887940, stubbed here); otherwise runs the legacy
//  CreateDCA / ExtEscape probe (F3QueryAdapterViaExtEscape). On probe
//  failure (v5 == 0) or a zero out word, forces v4 = 1. When v4 >= 1,
//  writes *outValue = v4 and returns 1; otherwise writes *outValue = 1
//  and returns 0 (the reachable case is a negative-int legacy out word,
//  which is < 1 after the forced-1 check).
// ============================================================
static int F3QueryAdapterState(int adapterIndex, int *outValue)
{
    int v3;   // F3IsOsMajorVersionAtLeast6 result
    int v5;   // probe result
    int v4 = 0;   // probe out word

    (void)adapterIndex;   // unused in the original
    v3 = F3IsOsMajorVersionAtLeast6();
    if (outValue == NULL)
        return 0;
    if (v3 == 1)
        v5 = sub_1887940(&v4);
    else
        v5 = F3QueryAdapterViaExtEscape(&v4);
    if (v5 == 0 || v4 == 0)
        v4 = 1;
    if (v4 >= 1)
    {
        *outValue = v4;
        return 1;
    }
    else
    {
        *outValue = 1;
        return 0;
    }
}

// ============================================================
//  F3InitAdapterState  (0x18F1150 - 0x18F11B3)  -- initialize the adapter state
//
//  int F3InitAdapterState(void)
//
//  Original (sub_18F1150): seeds the one-word state with 1, calls
//  F3QueryAdapterState(dword_1E2EDBC, &v1), and -- when the probe returns
//  1 -- writes the result into dword_1D9E184 and returns it; otherwise
//  writes 1 into dword_1D9E184 and returns the probe result.
//
//  [NOT byte-identical] refs the high-VA globals g_dword_1E2EDB8[1]
//  (0x01E2EDBC) and g_dword_1D9E184 (0x01D9E184) and calls F3QueryAdapterState
//  -- no byte-exact replay is possible.
// ============================================================
static int F3InitAdapterState(void)
{
    int v1 = 1;
    int result;

    result = F3QueryAdapterState(g_dword_1E2EDB8[1], &v1);   // dword_1E2EDBC
    if (result == 1)
    {
        g_dword_1D9E184 = (DWORD)v1;
        return v1;
    }
    else
    {
        g_dword_1D9E184 = 1;
    }
    return result;
}

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
//  GetAdapterCount()-1 (vtable[4]), and run the adapter-identifier query
//  (vtable[5], the stock IDirect3D9 GetAdapterIdentifier; the original used a
//  private-vtable slot 14 with a factory-first-arg convention this build does
//  not reproduce) into the 16-byte identifier block g_dword_1E2EDB8:
//    - S_OK: enumerate the adapter modes (F3EnumAdapterModes / sub_18F1FC0)
//      into a 20-byte local container, size the shared mode buffer
//      (F3ReserveModeList / sub_563AE0), destroy the container
//      (F3DestroyModeListContainer / sub_563770), init the adapter state
//      (sub_18F1150) and return true (the object is kept alive).
//    - D3DERR_NOTAVAILABLE (0x8876086A) with (flags & 4): force *outBehavior
//      to 2 and re-run the query once, returning whether it now succeeds
//      (object kept alive).
//    - otherwise: release the object (dword_1E2EEF8 = 0) and, when (flags & 1)
//      is clear, break (return false); else Sleep(0x64) and retry.
//  The stock interface does not fill the game's 16-byte adapter block; the
//  adapter index (block [1]) and device type (block [0]) are set directly.
//
//  [NOT byte-identical] refs the high-VA device globals dword_1E2EEF8 /
//  g_dword_1E2EDB8 (0x01E2EEF8 / 0x01E2EDB8) and dispatches into the deep D3D9
//  / XLO mode-enumeration subsystem (sub_18F1FC0 / sub_563AE0 / sub_563770 /
//  sub_18F1150), of which only F3EnumAdapterModes, F3ReserveModeList and
//  F3DestroyModeListContainer are implemented; F3InitAdapterState remains a
//  no-op stub.
// ============================================================
static bool CreateD3D9DisplayObject(int adapterIndex, DWORD *outBehavior, int flags)
{
    // Release any live D3D9 object (vtable[2] = Release). Stock d3d9 COM
    // dispatch: this in ECX, no args.
    if (g_dword_1E2EEF8 != 0)
    {
        void **vtable = *(void ***)g_dword_1E2EEF8;
        typedef void (__stdcall *F3D3D9ReleaseFn)(void *);
        if (vtable != NULL && (void *)vtable[2] != NULL)
            ((F3D3D9ReleaseFn)vtable[2])((void *)g_dword_1E2EEF8);
        g_dword_1E2EEF8 = 0;
    }

    // The Ex (Direct3DCreate9Ex) factory path crashes on this machine (an
    // access violation inside the stock factory's own locking on the first
    // GetAdapterCount / vtable[4] dispatch), so we always use the classic
    // Direct3DCreate9 path, which works reliably. The Ex ABI is identical
    // for the slots this build uses (0-16); only the Ex factory creation path
    // is excluded.
    const int useEx = 0;   // forced: classic path only
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

        if (outBehavior != NULL)
            *outBehavior = ((flags & 2) != 0) + 1;   // (flags & 2) ? 2 : 1

        // Stock d3d9 COM dispatches (this in ECX, args after it):
        //   slot 4 = GetAdapterCount() -> UINT
        //   slot 5 = GetAdapterIdentifier(Adapter, Flags,
        //                                D3DADAPTER_IDENTIFIER9*) -> HRESULT
        void **vtable = *(void ***)d3d;
        if (vtable == NULL || (void *)vtable[4] == NULL)
        {
            typedef void (__stdcall *F3D3D9ReleaseFn2)(void *);
            if (vtable != NULL && (void *)vtable[2] != NULL)
                ((F3D3D9ReleaseFn2)vtable[2])((void *)d3d);
            g_dword_1E2EEF8 = 0;
            break;
        }

        typedef UINT (__stdcall *F3D3D9GetCountFn)(void *);
        DWORD maxAdapter = ((F3D3D9GetCountFn)vtable[4])((void *)d3d) - 1;

        if ((DWORD)adapterIndex >= maxAdapter)
            adapterIndex = (int)maxAdapter;

        // The original fills the 16-byte identifier block at 1E2EDB8 with a
        // custom query (slot 14 in the original's private-vtable factory).
        // This build uses the stock d3d9 interface, whose adapter-identifier
        // query lives at slot 5 and writes a (much larger) D3DADAPTER_IDENTIFIER9.
        // We do not need that buffer; what the caller uses is the adapter index
        // (block [1]) and the device type (block [0]) -- set directly.
        DWORD hr = 0;
        typedef long (__stdcall *F3D3D9GetIdFn)(void *, DWORD, DWORD, void *);
        F3D3D9GetIdFn query = (vtable != NULL && (void *)vtable[5] != NULL)
                              ? (F3D3D9GetIdFn)vtable[5] : 0;
        if (query != 0)
        {
            // A stack identifier block big enough for the stock interface
            // (D3DADAPTER_IDENTIFIER9 is ~1.1 KiB); the query's HRESULT
            // decides success. The original passed the behavior word as the
            // "flags" parameter to its custom query; the stock interface
            // only expects 0 here.
            char identBuf[1100];
            hr = query((void *)d3d, (DWORD)adapterIndex, 0, identBuf);
        }
        else
        {
            hr = -1;
        }

        // Default the adapter block the caller reads: [1] = adapter index,
        // [0] = device type (D3DDEVTYPE_REF). The original's custom query
        // filled these; with the stock interface we keep the sane defaults.
        g_dword_1E2EDB8[0] = 2;
        g_dword_1E2EDB8[1] = (DWORD)adapterIndex;

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
            if (query != 0)
            {
                char identBuf[1100];
                hr = query((void *)d3d, (DWORD)adapterIndex, 0, identBuf);
            }
            else
                hr = -1;
            g_dword_1E2EDB8[0] = 2;
            g_dword_1E2EDB8[1] = (DWORD)adapterIndex;
            return hr == 0;
        }
        // Other failure: release the object, then retry after a short sleep
        // when (flags & 1) is set, else give up.
        typedef void (__stdcall *F3D3D9ReleaseFn2)(void *);
        if ((void *)vtable[2] != NULL)
            ((F3D3D9ReleaseFn2)vtable[2])((void *)d3d);
        g_dword_1E2EEF8 = 0;
        if ((flags & 1) == 0)
            break;
        Sleep(0x64u);
    }
    return false;
}

// ============================================================
//  F3XloCaps  -- the 56-byte (14-DWORD) XLO device caps / present-params
//  block built by the display-mode setters (SetDisplayMode / TrySetDisplayMode)
//  and consumed by BuildXloDeviceFromRequest (CreateXloDevice, sub_18F1B30).
//  The live copy the device vtable reads lives at g_dword_1E2ED80 / xmmword
//  1E2ED80 (copied in by CreateXloDevice).
//
//  Byte layout (as the setters build it):
//    +0   field[0]  texture source-descriptor table row index (format key row)
//    +4   field[1]  the picked display-mode index (0 = none -> no device)
//    +8   field[2]  width
//    +12  field[3]  height
//    +16  field[4]  present flag (1)
//    +20  field[5]  flags (0)
//    +24  field[6]  byte 24 gates caps[6]/caps[8]; byte 25 gates caps[13]
//    +28  field[7]  refresh rate (100)
//    +32  field[8]  render-mode-class tier (GetDisplayModeClass arg)
//    +36  field[9]  (0)
//    +40  field[10] behavior flags: bit0 sleep-retry, bit1 -> behavior 2,
//                   bit2 second-chance (behavior 2), bit4 Ex create path
//    +44  field[11] (unwritten)
//    +48  field[12] (unwritten)
//    +52  field[13] (unwritten)
// ============================================================
// (struct F3XloCaps is defined at the top of the ResetXloDevice cluster.)

// 0x1D9DF80: the end of the texture source-descriptor table (g_dword_1D9AF60).
// Distance from the table base in DWORDs: (0x1D9DF80 - 0x1D9AF60) / 4 = 3080
// (= 110 rows of 28 DWORDs).
#define F3_FORMAT_TABLE_END 3080

// Forward declarations for the XLO device-build path (definitions below, after
// DestroyDevice, so all the device globals they read are already in scope).
static int      DestroyDevice(void);
static int      F3FindFormatRow(DWORD key);
static DWORD   *PickBestDisplayMode(DWORD *out, DWORD *fmtIdx, int w, int h);
static int      CreateXloDeviceFromAdapter(DWORD adapter, const DWORD *caps,
                                           const F3XloCaps *req, DWORD deviceType);
static int      SetDeviceViewport(const DWORD *rect);
static DWORD   *CopySlotRecord(DWORD *src, DWORD *dst);
static int      RegisterDeviceSlotRecords(const DWORD *tmpl);
static char     CheckDeviceTextCaps(void);
static int      NvApiLoadInit(void);
static int      NvApiRegisterDevice(int a, int b);
static int      NvApiConfigure(int a);
static DWORD    InitXloDeviceState(void);
static int      BuildXloDeviceFromRequest(const F3XloCaps *req);

// ============================================================
//  F3InitXloCapsDefaults  (sub_563700, 0x563700 - 0x563732)  -- seed a 56-byte
//  XLO device-caps block with the default present parameters
//
//  void F3InitXloCapsDefaults(F3XloCaps *caps)
//
//  [NOT byte-identical] (the original is a usercall with the block in EAX);
//  the body is a plain store sequence. Sets the 44-byte core the original
//  writes: {0, 0, 0, 0, 1, 60, 0, 100, 0, 0, 0} (present flag 1 at byte 16,
//  refresh 60 at byte 20, refresh 100 at byte 28); the remaining 12 bytes
//  (field[11..13]) are left unwritten by the original (stack garbage there),
//  so they are zeroed for determinism.
// ============================================================
static void F3InitXloCapsDefaults(F3XloCaps *caps)
{
    caps->field[0]  = 0;
    caps->field[1]  = 0;
    caps->field[2]  = 0;
    caps->field[3]  = 0;
    caps->field[4]  = 1;     // byte 16: present flag
    caps->field[5]  = 60;    // byte 20: refresh
    caps->field[6]  = 0;     // byte 24: caps flags (2-byte field)
    caps->field[7]  = 100;   // byte 28: refresh
    caps->field[8]  = 0;     // byte 32
    caps->field[9]  = 0;     // byte 36
    caps->field[10] = 0;     // byte 40: behavior flags
    caps->field[11] = 0;     // bytes 44..55: zeroed (stack garbage in the original)
    caps->field[12] = 0;
    caps->field[13] = 0;
}

// ============================================================
//  F3PickBestModeDescriptor  (sub_18F1510, 0x18F1510 - 0x18F175B)  -- pick
//  the adapter display-mode descriptor closest to the requested mode
//
//  DWORD *F3PickBestModeDescriptor(DWORD *out16, const F3DisplayMode *dm)
//      -- stores the best 16-byte descriptor {width, height, refresh,
//         pixelFormat} into *out16 and returns out16 (both call sites read
//         the result through the returned pointer)
//
//  [NOT byte-identical] refs the high-VA device globals g_dword_1E2EEF8
//  (0x01E2EEF8, the D3D9 adapter object) / g_dword_1E2EDB8 (0x01E2EDB8, the
//  adapter index at +4) and the texture source-descriptor table
//  g_dword_1D9AF60 (0x01D9AF60) at their original addresses, refs the rdata
//  adapter-format table unk_1BD3020 (0x01BD3020, {0x1F, 0x15, 0x16, 0x00})
//  and the rdata floats (1e30f at 0x1BF9420, 10.0f at 0x1BE5700), and
//  dispatches into the D3D9 object's vtable[6] (mode count) / vtable[7]
//  (per-mode fetch). A 2-arg cdecl: (out16, dm); both call sites read the
//  result through the returned pointer.
//
//  Walks the rdata adapter-format table unk_1BD3020 (stopping at the 0
//  sentinel); for each D3DFORMAT, queries the live D3D9 object for the mode
//  count (vtable[6]) and, per mode, the 16-byte descriptor (vtable[7],
//  {width, height, refresh, pixelFormat}). A mode with width < height is
//  skipped when (dm->f28 & 0x20) is set. The descriptor's pixel format
//  selects a texture source-descriptor table row (row[0] key; the original
//  resets the index to row 0 when no row matches); when width, height AND
//  refresh all equal the request's and the row's bits-per-element ratio
//  (row[2] / row[1] / row[1], integer division) equals dm->f12, the mode is
//  an exact match: it is stored into *out16 and returned immediately. The
//  remaining modes are scored (float, the fabs of each term, the x87
//  sequence collapsed to float math):
//      size  = (|w - dmW| + |h - dmH|) / (dmW + dmH) * 10
//      score = size + |ratio - f12| / f12 * 10 + |f24 - refresh| / f24
//  keeping the strictly-smallest. If no mode scores better (or the adapter
//  enumerates nothing), the all-zero descriptor is returned (width 0).
//  Sole caller: TrySetDisplayMode (0x18F28C0).
// ============================================================
static DWORD *F3PickBestModeDescriptor(DWORD *out16, const F3DisplayMode *dm)
{
    const float F3_SCORE_INF = 1.0e30f;   // 0x1BF9420 (dword_1BF9420)
    const float F3_SCORE_SCALE = 10.0f;   // 0x1BE5700 (dword_1BE5700)
    // The rdata adapter table unk_1BD3020: {0x1F, 0x15, 0x16, 0x00}
    // (D3DFMT_A8R8G8B8 / D3DFMT_X8R8G8B8 / D3DFMT_A8B8G8R8, then the 0 sentinel).
    static const DWORD fmtTable[] = { 0x1F, 0x15, 0x16, 0x00 };

    float bestScore = F3_SCORE_INF;
    DWORD  bestDesc[4] = { 0, 0, 0, 0 };
    const DWORD d3d = g_dword_1E2EEF8;
    if (d3d != 0)   // null-guard a pre-device slot (the original would fault)
    {
        // Stock d3d9 COM dispatches (this in ECX, args after it):
        //   slot 6 = GetAdapterModeCount(Adapter, Format) -> UINT
        //   slot 7 = EnumAdapterModes(Adapter, Format, Mode, D3DDISPLAYMODE*)
        void **vt = *(void ***)d3d;
        typedef UINT (__thiscall *F3D3D9ModeCountFn)(void *, DWORD, DWORD);
        typedef void (__thiscall *F3D3D9ModeFn)(void *, DWORD, DWORD, DWORD, DWORD *);
        F3D3D9ModeCountFn countFn = vt != NULL ? (F3D3D9ModeCountFn)vt[6] : 0;
        F3D3D9ModeFn modeFn = vt != NULL ? (F3D3D9ModeFn)vt[7] : 0;

        if (countFn != 0 && modeFn != 0)
        {
            const DWORD *fmtKey = fmtTable;
            do
            {
                const int count = (int)countFn((void *)d3d, g_dword_1E2EDB8[1], *fmtKey);
                int i = 0;
                for (; i < count; ++i)
                {
                    DWORD desc[4] = { 0, 0, 0, 0 };
                    modeFn((void *)d3d, g_dword_1E2EDB8[1], *fmtKey, (DWORD)i, desc);
                    if (((dm->f28 & 0x20) != 0) && (DWORD)desc[0] < (DWORD)desc[1])
                        continue;   // (f28 & 0x20): drop portrait modes (w < h)
                    // The pixel format's texture source-descriptor table row
                    // (the original resets to row 0 when no row matches the key).
                    const int rowIdx = F3FindFormatRow(desc[3]);
                    const DWORD *row = &g_dword_1D9AF60[28 * rowIdx];
                    if (desc[0] == (DWORD)dm->width && desc[1] == (DWORD)dm->height &&
                        desc[2] == (DWORD)dm->f24)
                    {
                        // bits-per-element ratio (integer division, as the
                        // original's two cdq/idiv steps; row[1] is the element
                        // divisor, always 1 in the embedded table).
                        const int ratio = (int)(row[2] / row[1] / row[1]);
                        if (dm->f12 == ratio)
                        {
                            memcpy(out16, desc, 16);   // exact match: return immediately
                            return out16;
                        }
                    }
                    // Signed 32-bit differences, as the original's int sub +
                    // cvtsi2ss pairs (desc[] is DWORD, so cast before the
                    // subtraction).
                    const float sizeScore =
                        (fabsf((float)((int)desc[0] - dm->width)) +
                         fabsf((float)((int)desc[1] - dm->height))) /
                        (float)(dm->width + dm->height) * F3_SCORE_SCALE;
                    const int ratio = (int)(row[2] / row[1] / row[1]);
                    float score = sizeScore +
                        fabsf((float)(ratio - dm->f12)) / (float)dm->f12 * F3_SCORE_SCALE +
                        fabsf((float)(dm->f24 - (int)desc[2])) / (float)dm->f24;
                    if (score < bestScore)   // fcomip + jbe: strictly smaller only
                    {
                        bestScore = score;
                        memcpy(bestDesc, desc, 16);
                    }
                }
                ++fmtKey;
            } while (*fmtKey != 0);
        }
    }

    memcpy(out16, bestDesc, 16);
    return out16;
}

// ============================================================
//  TrySetDisplayMode  (0x18F28C0 - 0x18F2B22)  -- try to make the requested
//  display mode current
//
//  int TrySetDisplayMode(const F3DisplayMode *dm)   -- 0 = current, 1 = changed,
//                                                     2 = restore-to-current failed
//
//  [NOT byte-identical] placement-dependent (relative calls to
//  F3PickBestModeDescriptor (sub_18F1510), GetDisplayModeClass (sub_18EFF00),
//  ResetXloDevice (0x18F2310), CreateD3D9DisplayObject (0x18F21A0),
//  F3InitXloCapsDefaults (sub_563700), F3FindFormatRow (sub_435100),
//  PickBestDisplayMode (sub_18F05D0), BuildXloDeviceFromRequest (sub_18F1B30)
//  and the Enter/LeaveCriticalSection IAT slots) and refs the high-VA device
//  globals g_dword_1E2EEF4 (0x01E2EEF4), the 56-byte caps block
//  g_dword_1E2ED80 (0x01E2ED80), g_dword_1E2ED7C (0x01E2ED7C) and the device
//  critical section g_stru_20FC948 (0x020FC948).
//
//  Original semantics (nested under the device CS stru_20FC948): if the
//  device object (dword_1E2EEF4) is present, pick the adapter mode descriptor
//  closest to the request (F3PickBestModeDescriptor, sub_18F1510); when its
//  width is > 0, copy the live 56-byte caps block (dword_1E2ED80), overwrite
//  width / height (bytes 0/4), zero byte 32, set the refresh (byte 48) and
//  the render-mode class (byte 52, GetDisplayModeClass(dm->f36) when the
//  request's byte 29 is nonzero, else 0x80000000), and reset the XLO device
//  (ResetXloDevice) -- a successful reset returns 0 (the requested mode is /
//  became current). Otherwise restore the original caps (a second
//  ResetXloDevice on the live block): success returns 1 (the mode was
//  changed / restored), failure returns 2 (the restore-to-current failed).
//  If the device is absent, create the D3D9 object
//  (CreateD3D9DisplayObject / sub_18F21A0; a failure returns 1) and, when
//  the picked descriptor's width is nonzero (else 1), build the 56-byte XLO
//  device request from the seeded defaults (F3InitXloCapsDefaults, sub_563700):
//  the picked descriptor's pixel-format row (F3FindFormatRow, sub_435100)
//  at byte 0, width / height at bytes 8/12, refresh at byte 20, caps flags
//  byte 24 = 1 + the request's byte 29 at byte 25, the request's f20 /
//  f24 / f28 at bytes 28/32/40 (behavior 2 ORs 2 into byte 40); the picked
//  display mode (PickBestDisplayMode, sub_18F05D0, on the format row and the
//  request's f16 / f20) goes into byte 4 -- a zero pick returns 1. With a
//  picked mode, zero g_dword_1E2ED7C and build the device
//  (BuildXloDeviceFromRequest / sub_18F1B30), returning its negated status
//  (1 = success).
// ============================================================
static int TrySetDisplayMode(const F3DisplayMode *dm)
{
    F3EnterDisplayCs();

    if (g_dword_1E2EEF4 != 0)
    {
        // Device present: rebuild the XLO device with the picked mode.
        F3XloCaps caps;
        DWORD  picked[4];
        memcpy(caps.field, g_dword_1E2ED80, sizeof(caps.field));
        F3PickBestModeDescriptor(picked, dm);
        if ((int)picked[0] > 0)   // the original's `test eax, eax; jbe`
        {
            caps.field[0]  = picked[0];              // byte 0:  width
            caps.field[1]  = picked[1];              // byte 4:  height
            caps.field[8]  = 0;                      // byte 32
            caps.field[12] = picked[2];              // byte 48: refresh
            caps.field[13] = (((const unsigned char *)&dm->f28)[1] != 0)
                                 ? (DWORD)GetDisplayModeClass(dm->f36)
                                 : 0x80000000;       // byte 52
            if (ResetXloDevice(caps) != 0)
            {
                LeaveCriticalSection(&g_stru_20FC948);
                return 0;   // the requested mode is / became current
            }
        }
        // The reset failed (or the picked mode was unusable): restore the
        // original caps and report the outcome.
        F3XloCaps current;
        memcpy(current.field, g_dword_1E2ED80, sizeof(current.field));
        if (ResetXloDevice(current) != 0)
        {
            LeaveCriticalSection(&g_stru_20FC948);
            return 1;   // the mode was changed (restored to the original)
        }
        LeaveCriticalSection(&g_stru_20FC948);
        return 2;   // the restore-to-current failed
    }

    // No device yet: create the D3D9 object and build the device from the
    // request's picked adapter mode.
    DWORD behavior = 1;   // the original seeds the out behavior to 1
    if (CreateD3D9DisplayObject(dm->f00, &behavior, dm->f28) == 0)
    {
        LeaveCriticalSection(&g_stru_20FC948);
        return 1;
    }

    DWORD picked[4];
    F3PickBestModeDescriptor(picked, dm);
    if (picked[0] == 0)
    {
        LeaveCriticalSection(&g_stru_20FC948);
        return 1;
    }

    F3XloCaps req;
    F3InitXloCapsDefaults(&req);   // {0,0,0,0, 1, 60, 0, 100, 0, 0, 0, ...}
    DWORD fmtRow = (DWORD)F3FindFormatRow(picked[3]);   // pixel-format row
    req.field[0]  = fmtRow;                            // byte 0
    req.field[2]  = picked[0];                         // byte 8:  width
    req.field[3]  = picked[1];                         // byte 12: height
    req.field[5]  = picked[2];                         // byte 20: refresh
    req.field[6]  = 1 | ((DWORD)(((const unsigned char *)&dm->f28)[1]) << 8);  // byte 24 = 1, byte 25 = dm+29
    req.field[7]  = (DWORD)dm->f20;                    // byte 28
    req.field[8]  = (DWORD)dm->f24;                    // byte 32
    req.field[10] = (DWORD)dm->f28;                    // byte 40
    if (behavior == 2)
        req.field[10] |= 2;

    // Pick the closest display mode (the format row is the input key; the
    // original re-runs the row lookup before the pick, which is idempotent).
    DWORD pickedMode = 0;
    PickBestDisplayMode(&pickedMode, &fmtRow, dm->f16, dm->f20);
    req.field[1] = pickedMode;                         // byte 4
    if (pickedMode == 0)
    {
        LeaveCriticalSection(&g_stru_20FC948);
        return 1;
    }

    g_dword_1E2ED7C = 0;
    int result = (BuildXloDeviceFromRequest(&req) == 0);
    LeaveCriticalSection(&g_stru_20FC948);
    return result;
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
        // The original passes the 56-byte caps block BY VALUE (14 DWORDs on
        // the stack), so copy it into an F3XloCaps before each call.
        F3XloCaps capsByVal;
        memcpy(capsByVal.field, caps, sizeof(capsByVal.field));
        if (ResetXloDevice(capsByVal) != 0)
        {
            LeaveCriticalSection(&g_stru_20FC948);
            return 0;
        }
        F3XloCaps capsByVal2;
        memcpy(capsByVal2.field, g_dword_1E2ED80, sizeof(capsByVal2.field));
        if (ResetXloDevice(capsByVal2) != 0)
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
        // 56-byte XLO device request and build the device from it. Without a
        // live D3D9 object this reports the "no device" status (1).
        int v5 = 1;
        DWORD behavior = 1;   // the original seeds the out behavior to 1
        if (CreateD3D9DisplayObject(dm->f00, &behavior, dm->f28) != 0)
        {
            // Query the adapter's display-mode descriptor (d3d9 COM slot 8:
            // GetAdapterDisplayMode(Adapter, D3DDISPLAYMODE*), this in ECX,
            // 16-byte out block = {width, height, refresh, pixelFormat}).
            DWORD mode[4] = { 0, 0, 0, 0 };
            void **vt = *(void ***)g_dword_1E2EEF8;
            typedef void (__stdcall *F3AdapterModeFn)(void *, DWORD, DWORD *);
            if (vt != NULL && (void *)vt[8] != NULL)
                ((F3AdapterModeFn)vt[8])((void *)g_dword_1E2EEF8, g_dword_1E2EDB8[1], mode);
            DWORD pixelFormat = mode[3];

            // Assemble the 56-byte XLO device request (F3XloCaps).
            F3XloCaps req;
            memset(req.field, 0, sizeof(req.field));
            req.field[0]  = (DWORD)F3FindFormatRow(pixelFormat);  // byte 0
            req.field[2]  = (DWORD)dm->width;                     // byte 8
            req.field[3]  = (DWORD)dm->height;                    // byte 12
            req.field[4]  = 1;                                    // byte 16
            req.field[5]  = 0;                                    // byte 20
            req.field[6]  = ((DWORD)(dm->f20 & 0xFF)) << 8;       // byte 24=0, 25=low byte of f20
            req.field[7]  = 100;                                  // byte 28
            req.field[8]  = 0;                                    // byte 32
            req.field[9]  = 0;                                    // byte 36
            req.field[10] = (DWORD)dm->f28;                       // byte 40
            if (behavior == 2)
                req.field[10] |= 2;

            // Pick the best matching display mode and build the device from it.
            DWORD fmtIdx = (DWORD)F3FindFormatRow(pixelFormat);
            DWORD picked = 0;
            PickBestDisplayMode(&picked, &fmtIdx, dm->f12, dm->f16);
            req.field[1] = picked;                                // byte 4
            if (picked != 0)
            {
                g_dword_1E2ED7C = 0;
                v5 = (BuildXloDeviceFromRequest(&req) == 0);
            }
        }
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
//  ApplyDisplaySettings  (0x18F7E60 - 0x18F805C)  -- apply / restore the
//  system display settings on a windowed <-> fullscreen transition
//  [NOT byte-identical] placement-dependent (relative calls to
//  SaveSettingsBeforeExit / SystemParametersInfoA / CXMLWriteStreamCtor /
//  RefSharedSlot / AppendSharedSlot / EnsureParentDirectory /
//  ReleaseSharedSlot / WriteStreamEnterElement / ReadXmlNamedValue /
//  WriteXmlStreamAdvance / WriteXmlStreamToFile / CXMLWriteStreamDtor and the
//  /GS cookie / high-VA Str1) plus the high-VA accessibility globals
//  (g_dword_1D9E18C / g_dword_1D9E190 / g_dword_1D9E194 / g_dword_1D9E198 /
//  g_dword_1D9E1A0 / g_byte_1D9E19C / g_dword_1DBE2F0 / g_dword_20FC8D8 /
//  g_dword_20FC8DC / g_dword_20FC8E0 / g_dword_20FC8E4).
//
//  int __stdcall ApplyDisplaySettings(int windowed)
//
//  When `windowed` is non-zero, the settings are restored (the windowed
//  <-> fullscreen transition path) through SaveSettingsBeforeExit and its
//  result is returned. Otherwise the three snapshotted system parameters
//  are (re)applied: SPI_SETHIDCONFIRM (0x3B, 8, &g_dword_1D9E18C) when the
//  loaded HIDCONFIRM value (g_dword_1D9E190) has bit 0 clear,
//  SPI_SETKEYBOARDSPEED (0x35, 8, &g_dword_1D9E194) when the loaded
//  KEYBOARDSPEED value (g_dword_1D9E198) has bit 0 clear, and
//  SPI_SETKEYBOARDDelay (0x33, 0x18, the 24-byte g_byte_1D9E19C snapshot with
//  bits 0/1 of the loaded keyboard-delay value (g_dword_1D9E1A0, bytes 4-7 of
//  the snapshot) cleared) when that loaded value is even. The keyboard-delay
//  snapshot is copied to a stack buffer first; the two-word (bits 0/1) mask
//  is applied to the copy only. When the loaded keyboard-delay value is odd
//  and no other parameter was applied, the function returns 0 and skips the
//  XML-save path. Otherwise a CXMLWriteStream is built; EnsureParentDirectory
//  (the settings directory + "AccessibilitySettings.xml") is run and the
//  slot released; the section named by g_dword_20FC8D8 (empty = root) is
//  entered through the write stream and, on success, the three named values
//  (g_dword_20FC8DC / E0 / E4) are re-entered through the stream with the
//  loaded values passed by reference (ReadXmlNamedValue, the loaded value is
//  the in/out local), and the stream is advanced (WriteXmlStreamAdvance).
//  AccessibilitySettings.xml is then serialized (the LHXML serializer is a
//  remaining stub, so only the XML prologue reaches the file), the slot is
//  released and the stream destroyed. This is the display-transition
//  counterpart of InitAccessibilitySettings (the start-up load).
// ============================================================
static int __stdcall ApplyDisplaySettings(int windowed)
{
    if (windowed != 0)
        return SaveSettingsBeforeExit();

    DWORD pvParam = g_dword_1D9E18C;   // the SPI_GETHIDCONFIRM snapshot
    int result = 0;

    if ((g_dword_1D9E190 & 1) == 0)
    {
        SystemParametersInfoA(0x3B, 8u, (PVOID)&pvParam, 0);   // SPI_SETHIDCONFIRM
        result = 1;
    }

    DWORD v5 = g_dword_1D9E194;        // the SPI_GETKEYBOARDSPEED snapshot
    if ((g_dword_1D9E198 & 1) == 0)
    {
        SystemParametersInfoA(0x35u, 8u, (PVOID)&v5, 0);       // SPI_SETKEYBOARDSPEED
        result = 1;
    }

    BYTE v10[24];
    memcpy(v10, g_byte_1D9E19C, 24);
    if ((v10[4] & 1) != 0)             // the loaded keyboard-delay value (bytes 4-7)
    {
        if (result == 0)
            return 0;                  // return 0; skip the XML-save path
    }
    else
    {
        *(DWORD *)&v10[4] &= 0xFFFFFFF3u;                   // bits 0/1 cleared
        SystemParametersInfoA(0x33u, 0x18u, (PVOID)v10, 0);   // SPI_SETKEYBOARDDelay
    }

    F3XmlStream stream;
    CXMLWriteStreamCtor(&stream);                             // sub_17DB800

    F3SharedBuffer *path = 0;
    RefSharedSlot(&path, &g_dword_1DBE2F0);
    AppendSharedSlot(&path, "AccessibilitySettings.xml");
    EnsureParentDirectory(&path);
    ReleaseSharedSlot(&path);

    const char *sectionName = (g_dword_20FC8D8 != 0) ? g_dword_20FC8D8->data : "";
    if (WriteStreamEnterElement(&stream, sectionName) != 0)
    {
        DWORD value = g_dword_1D9E190;
        ReadXmlNamedValue(&g_dword_20FC8DC, &stream, &value);
        value = g_dword_1D9E198;
        ReadXmlNamedValue(&g_dword_20FC8E0, &stream, &value);
        value = g_dword_1D9E1A0;
        ReadXmlNamedValue(&g_dword_20FC8E4, &stream, &value);
        WriteXmlStreamAdvance(&stream);
    }

    F3SharedBuffer *path2 = 0;
    RefSharedSlot(&path2, &g_dword_1DBE2F0);
    AppendSharedSlot(&path2, "AccessibilitySettings.xml");
    const char *fileName = (path2 != 0) ? path2->data : "";
    WriteXmlStreamToFile(&stream, fileName);
    ReleaseSharedSlot(&path2);
    return CXMLWriteStreamDtor(&stream);
}

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
//  HandleDisplayModeChange  (0x18F8060 - 0x18F82D6)  -- apply a
//  pending display-mode change (the request stashed by sub_18F6BA0 at
//  +0x274..+0x27D)
//  [NOT byte-identical] placement-dependent (relative calls to
//  InitDisplayMode / TrySetDisplayMode / SetDisplayMode / ApplyDisplaySettings /
//  ResizeWorkerWindow / FindActiveInputDevice / the SetWindowLongA / SetWindowPos /
//  ShowWindow / SetEvent IAT slots) and refs the high-VA manager pair
//  dword_20FC8C4 and the input-manager dispatch (dword_20FC8C4 + 0x20).
//
//  int __thiscall HandleDisplayModeChange(CWorkerWin32 *obj)   -- worker in ECX
//
//  Called from PumpWorkerMessages under the worker CS (nested entry) when
//  the display-mode-pending flag (+0x281) is set. Reads the pending request
//  (width/height at +0x274/+0x278, fullscreen/refresh bytes at +0x27C/+0x27D,
//  render-mode field at +0x250), applies it (the TrySetDisplayMode fullscreen
//  path when +0x27C is set, else the SetDisplayMode windowed path). On
//  success (result 0) it calls the +0x26C callback, stores the requested
//  width/height + the fullscreen/refresh bytes, runs
//  ApplyDisplaySettings(!windowed), ResizeWorkerWindow, ShowWindow(hwnd,
//  10), updates the manager's display record (dword_20FC8C4 pair) via its
//  vtable[1], activates the active input device (device vtable[12]), clears
//  the pending flag, and SetEvents event4 (+0x228). On result 1 it leaves
//  the pending flag clear (+0x282 = 1) and SetEvents event4; on result 2
//  it re-arms the pending flag (+0x281) and returns unhandled.
// ============================================================
static int HandleDisplayModeChange(CWorkerWin32 *obj)
{
    int result;
    int width  = obj->displayPendingWidth;      // +0x274 requested client width
    int height = obj->displayPendingHeight;      // +0x278 requested client height

    if (obj->displayPendingFullscreen != 0)      // +0x27C requested fullscreen byte
    {
        if ((unsigned char)obj->windowFlags == 0)   // +0x24C current fullscreen byte clear
        {
            // Drop out of fullscreen before re-applying the mode. The original
            // installs the class procedure at 0xCA0000 / 0x80080000 here; keep
            // the module's own wndproc (sub_18F7A70) in this build.
            SetWindowLongA(obj->hwnd, GWLP_WNDPROC, (LONG)WorkerWndProc);
            SetWindowLongA(obj->hwnd, GWLP_HINSTANCE, 0);
            SetWindowPos(obj->hwnd, HWND_TOP, 0, 0, 0, 0, 0x23u /*SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW*/);
        }

        F3DisplayMode dm;
        InitDisplayMode(&dm);
        dm.width = width;
        dm.height = height;
        *(unsigned char *)&dm.f28 = (unsigned char)obj->displayPendingRefresh;  // low byte of f28
        dm.f36 = obj->f_0250;
        result = TrySetDisplayMode(&dm);

        if (result != 0 && (unsigned char)obj->windowFlags == 0)
        {
            // The mode was not applied and the window is not fullscreen:
            // restore the window proc / instance / position.
            SetWindowLongA(obj->hwnd, GWLP_WNDPROC, (LONG)WorkerWndProc);
            SetWindowLongA(obj->hwnd, GWLP_HINSTANCE, 0);
            SetWindowPos(obj->hwnd, HWND_TOP, 0, 0, 0, 0, 0x23u /*SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW*/);
        }
    }
    else
    {
        F3DisplayMode dm;
        memset(&dm, 0, sizeof(dm));
        dm.width = width;
        dm.height = height;
        dm.f12 = 24;
        dm.f16 = 8;
        dm.f20 = 0;
        dm.f24 = 1;
        dm.f28 = 0;
        result = SetDisplayMode(&dm);
    }

    switch (result)
    {
    case 0:
        obj->onResetDevice();   // +0x26C on-reset-device callback
        {
            int fullscreen = (unsigned char)obj->displayPendingFullscreen;  // +0x27C
            int refresh    = (unsigned char)obj->displayPendingRefresh;      // +0x27D

            *(unsigned char *)&obj->windowFlags = (unsigned char)fullscreen;        // +0x24C
            obj->displayPendingDone = 0;        // +0x282
            obj->windowWidth  = width;          // +0x244
            obj->windowHeight = height;         // +0x248
            *((unsigned char *)&obj->windowFlags + 1) = (unsigned char)refresh;      // +0x24D
            ApplyDisplaySettings(fullscreen == 0);

            if ((unsigned char)obj->windowFlags == 0)   // not fullscreen (now)
            {
                ResizeWorkerWindow(obj);
                SetWindowLongA(obj->hwnd, GWLP_WNDPROC, (LONG)WorkerWndProc);
                SetWindowLongA(obj->hwnd, GWLP_HINSTANCE, 0);
                SetWindowPos(obj->hwnd, HWND_TOP, 0, 0, 0, 0, 0x23u /*SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW*/);
            }

            ShowWindow(obj->hwnd, 10);   // SW_SHOWDEFAULT

            // Update the manager's display record (dword_20FC8C4 + 0x20).
            {
                void *mgr = GetManager();
                if (mgr != 0)
                {
                    void *inputManager = *(void **)((char *)mgr + 0x20);
                    if (inputManager != 0)
                    {
                        DWORD *io = (DWORD *)inputManager;
                        io[16] = (DWORD)width;   // +0x40 requested width
                        io[17] = (DWORD)height;  // +0x44 requested height

                        // vtable[1] (sub_188ED80): walk the device list and notify
                        // each device -- slot 1 is stubbed (0) in this build, so
                        // the notification is skipped (the device list stays empty).
                        void **vt = *(void ***)inputManager;
                        if (vt != 0 && vt[1] != 0)
                        {
                            typedef void (__stdcall *F3StdcallNotifyFn)(int);
                            ((F3StdcallNotifyFn)vt[1])(0);
                        }

                        void *activeDevice = FindActiveInputDevice(inputManager);
                        if (activeDevice != 0)
                        {
                            void **devVt = *(void ***)activeDevice;
                            if (devVt != 0 && devVt[12] != 0)
                            {
                                typedef void (__thiscall *F3ThiscallFn0)(void *);
                                ((F3ThiscallFn0)devVt[12])(activeDevice);
                            }
                        }
                    }
                }
            }

            obj->displayModePending = 0;   // +0x281
            return SetEvent(obj->event4);  // +552
        }
    case 1:
        obj->displayPendingDone = 1;      // +0x282
        obj->displayModePending = 0;      // +0x281
        return SetEvent(obj->event4);     // +552
    case 2:
        obj->displayModePending = 1;      // +0x281
        break;
    default:
        break;
    }
    return result;
}

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
//  (NvApiLoadInit, sub_17D1D10) loads nvapi.dll, takes nvapi_QueryInterface, and resolves the
//  before/after hooks; the unload (NvApiTeardownUnload) clears the table and
//  frees the library. The original keeps these at the VAs noted; here they
//  live as static globals in this module.
// ============================================================
static HMODULE g_hLibModule = 0;                                // 0x1E2C704  nvapi.dll module handle (hLibModule)
static int (__cdecl *g_dword_1E2C708)(DWORD) = 0;               // 0x1E2C708  nvapi_QueryInterface resolver
static int (__cdecl *g_dword_1E2C70C)(DWORD, int *) = 0;        // 0x1E2C70C  before-call hook
static int (__cdecl *g_dword_1E2C710)(DWORD, int, int, int) = 0; // 0x1E2C710  after-call hook

//  178-entry resolved-interface table (0x1D941F4 - 0x1D94A08), 12 bytes per
//  slot: resolved entry pointer at +0, resolved flag byte at +4. Slot 92 is
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
    F3NvApiSlot *slot = (F3NvApiSlot *)((char *)g_dword_1D941F4 + (0x1D94644 - 0x1D941F4));   // 0x1D94644 / 0x1D94648, interface 0x2EC50C2B

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
//  ApplySlotViewport  (sub_18F0850, 0x18F0850 - 0x18F08C7)  -- re-apply the
//  current slot's stored viewport rect to the device
//  [NOT byte-identical] relative call to SetDeviceViewport and refs the slot
//  index (dword_2075F10) and the per-slot record table (0x20FCA68) at their
//  original high VAs.
//
//  int ApplySlotViewport(void)   -- no args (cdecl), returns int
//
//  For the current slot's (dword_2075F10) row in the per-slot record table
//  (0x20FCA68, 44 DWORDs), picks the stored {right, bottom} pair: from the
//  row's first record (object at row DWORD 1) when that record is set
//  (values at row DWORDs 4 and 5), else from the slot trailer when its flag
//  (row DWORD 33) is set (values at row DWORDs 36 and 37). With neither
//  present, returns the slot's byte offset (176 * slot); otherwise builds the
//  rect {0, 0, right, bottom} and returns SetDeviceViewport(rect).
// ============================================================
static int ApplySlotViewport(void)
{
    DWORD slot = g_dword_2075F10;
    DWORD *row = &g_dword_20FCA68[slot * 44];

    DWORD right, bottom;
    if (row[1] != 0)
    {
        right = row[4];
        bottom = row[5];
    }
    else
    {
        if (row[33] == 0)
            return (int)(slot * 176);
        right = row[36];
        bottom = row[37];
    }

    DWORD rect[4] = { 0, 0, right, bottom };
    return SetDeviceViewport(rect);
}

// ============================================================
//  TeardownDeviceSlot  (0x18F0E00 - 0x18F0E9B)  -- device teardown (per-slot)
//  [NOT byte-identical] placement-dependent (relative fall-through to
//  ApplySlotViewport) and refs the slot index (dword_2075F10), the device object
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
//  ApplySlotViewport(); otherwise returns the last release result (or 0).
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
        return ApplySlotViewport();
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
    DWORD pad0C;              // +0x0C (opaque, copied verbatim by the flush path)
    DWORD data16_lo;          // +0x10 (the 8-byte opaque payload, low word)
    DWORD data16_hi;          // +0x14 (the 8-byte opaque payload, high word)
};
// 24 bytes, matching the LHAlloc(24) allocation of the original node layout.

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
    DWORD f3C;                // +0x3C  container counter (++ per flush)
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
//  F3ObjectListSizeAdd  (0x5A4A80)  -- increment a list's count
//  [NOT byte-identical] placement-dependent (LHAlloc exception machinery).
//
//  int F3ObjectListSizeAdd(F3ObjectList *list)
//
//  Checks the count against the 0xFFFFFFF "list too long" limit and, on
//  overflow, throws std::length_error("list<T> too long") (the original builds
//  the same exception via sub_402F00 / sub_401BD0 and throws it with
//  std::_Throw). On success increments the count and returns
//  0xFFFFFFF - count (the value before the increment).
// ============================================================
static int F3ObjectListSizeAdd(F3ObjectList *list)
{
    int result = 0xFFFFFFF - list->count;
    if (list->count == 0xFFFFFFF)
        throw std::length_error("list<T> too long");
    ++list->count;
    return result;
}

// ============================================================
//  F3ObjectListAddNode  (0x5A47D0)  -- insert a new node at the end of
//  the list
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc and
//  F3ObjectListSizeAdd) and refs the heap table (dword_2075B24) at its
//  original high VA.
//
//  void F3ObjectListAddNode(F3ObjectList *list, const void *obj,
//                           DWORD pad0C, DWORD data16_lo, DWORD data16_hi)
//
//  Allocates the 24-byte node, sets { next = sentinel, prev = sentinel->prev
//  (the old last real node), obj = obj, pad0C = pad0C, data16 =
//  {lo, hi} }, increments the count (F3ObjectListSizeAdd), and links it
//  after the old last real node (old last->next = new node,
//  sentinel->prev = new node). On allocation failure throws std::bad_alloc
//  (unreachable in practice -- a 24-byte LHAlloc).
// ============================================================
static void F3ObjectListAddNode(F3ObjectList *list, void *obj,
                                DWORD pad0C, DWORD data16_lo, DWORD data16_hi)
{
    F3ObjectListNode *sentinel = list->tail;
    F3ObjectListNode *newNode =
        (F3ObjectListNode *)LHAlloc(sizeof(F3ObjectListNode));

    if (newNode == 0)
        throw std::bad_alloc();

    F3ObjectListNode *oldLast = sentinel->prev;
    newNode->next = sentinel;
    newNode->prev = oldLast;
    newNode->obj  = obj;
    newNode->pad0C = pad0C;
    newNode->data16_lo = data16_lo;
    newNode->data16_hi = data16_hi;

    F3ObjectListSizeAdd(list);

    sentinel->prev = newNode;
    oldLast->next = newNode;
}

// ============================================================
//  F3ListRemoveTail  (0x5A4740)  -- remove + free the last real node
//  [NOT byte-identical] placement-dependent (relative call to F3ListRemoveNode).
//
//  void F3ListRemoveTail(F3ObjectList *list, F3ObjectListNode **outLast)
//
//  Reads the last real node (sentinel->prev) and removes it via
//  F3ListRemoveNode (splice + LHFree + count--). outLast receives the node
//  pointer (so the caller can release / re-wrap its wrapped object). The
//  original carries __invalid_parameter_noinfo asserts that are no-ops in a
//  healthy build.
// ============================================================
static void F3ListRemoveTail(F3ObjectList *list, F3ObjectListNode **outLast)
{
    F3ObjectListNode *last = list->tail->prev;
    DWORD out[2] = { 0, 0 };
    F3ListRemoveNode(list, out, list->head, last);
    *outLast = last;
}

// ============================================================
//  EnsureObjectListInit  (0x18CB5A0)  -- one-time list init
//  [NOT byte-identical] placement-dependent (LHAlloc).
//
//  void EnsureObjectListInit(F3ObjectList *list)
//
//  Lazily creates the 24-byte sentinel node for the list (next = prev =
//  itself, count = 0). The original allocates a 4-byte head-slot block and
//  stores the sentinel in the tail slot; this build keeps the sentinel
//  directly in the tail slot.
// ============================================================
static void EnsureObjectListInit(F3ObjectList *list)
{
    if (list->tail != 0)
        return;

    F3ObjectListNode *sentinel =
        (F3ObjectListNode *)LHAlloc(sizeof(F3ObjectListNode));
    if (sentinel == 0)
        throw std::bad_alloc();
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    sentinel->obj  = 0;
    sentinel->pad0C = 0;
    sentinel->data16_lo = 0;
    sentinel->data16_hi = 0;

    list->tail = sentinel;
    list->head = 0;
    list->count = 0;
}

// ============================================================
//  FlushDeviceOperationLists  (0x18CB6A0 - 0x18CB96F)  -- flush the pending
//  device-operation object lists
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc /
//  GetEngineTime / Sleep / FreeObjectListContainer / F3ListRemoveNode) and
//  refs the container (0x20FC988) and the FPS records (0x2074548 / 0x20B5F70)
//  at their original high VAs.
//
//  int FlushDeviceOperationLists(void)   -- no args (cdecl), returns int
//
//  Flush the pending device-operation lists of the 64-byte container:
//  - while the +0x20 list (l20) holds pending operations, ask the wrapped
//    object's vtable[7] (offset 28, stdcall with the object on the stack,
//    args 0 / 0 / 1) whether it is done. If it returns non-zero and is not
//    1 (or the flush took longer than dbl_1DBBFE0 seconds), free both lists
//    (FreeObjectListContainer) and jump to the tail. If it returns 1 and
//    the flush is still within the threshold, Sleep(0) and keep flushing.
//    If it returns 0, move the last operation node from l20 to the +0x04
//    list (l04) -- copying its wrapped object and opaque payload verbatim --
//    via F3ObjectListAddNode and remove + free the old l20 node.
//  - when l04 is empty at the end, ask the device's vtable[118] (offset
//    472) to produce a new operation into a 20-byte buffer (arg 8) and add
//    it as an l04 node if the call returns 0.
//  - if l04 is still non-empty at the end, release the last l04 node's
//    wrapped object (vtable[6], offset 24, stdcall with the object on the
//    stack, args 1) and re-add it to l20 (F3ObjectListAddNode).
//  Then bump the container counter, stamp the engine time into the current
//  FPS record, and return the record index * 3.
// ============================================================
static double g_dbl_1DBBFE0 = 1.0;       // 0x1DBBFE0  flush time threshold (seconds)
static DWORD  g_dword_20FC9C8 = 0;        // 0x20FC9C8  flush scratch (write-only, never read)
static DWORD  g_dword_20B5F74 = 0;        // 0x20B5F74  current FPS record index
static DWORD  g_dword_20B5F70 = 0;        // 0x20B5F70  global frame counter
static double g_dbl_2074580 = 0.0;        // 0x2074580  last flush time
static double g_dbl_2074578 = 0.0;        // 0x2074578  last flush time (copy)

// 24-byte FPS record: frame time (+0), previous frame time (+8), frame
// counter (+0x10), interlocked counter (+0x14).
static struct F3FpsRecord
{
    double frameTime;        // +0
    double prevFrameTime;    // +8
    DWORD  frameCounter;     // +0x10
    long   interlocked;      // +0x14
} g_FpsRecords[128];

int FlushDeviceOperationLists(void)
{
    EnsureObjectListInit(&g_stru_20FC988.l04);
    EnsureObjectListInit(&g_stru_20FC988.l20);

    double engineTime = GetEngineTime();
    int result = 0;

    while (g_stru_20FC988.l20.count != 0)
    {
        F3ObjectList *l20 = &g_stru_20FC988.l20;
        F3ObjectListNode *sentinel = l20->tail;
        F3ObjectListNode *last = sentinel->prev;   // the last real node
        void *obj = last->obj;

        void **ovt = *(void ***)obj;
        int (__stdcall *op)(void *, int, int, int) =
            (int (__stdcall *)(void *, int, int, int))ovt[7];  // offset 28
        int v4 = op(obj, 0, 0, 1);

        if (v4 != 0)
        {
            if (v4 != 1 || GetEngineTime() - engineTime > g_dbl_1DBBFE0)
            {
                FreeObjectListContainer(&g_stru_20FC988);
                goto f3_flush_done;
            }
            Sleep(0);
            continue;
        }

        // v4 == 0: move the last operation node from l20 to l04
        DWORD pad0C = last->pad0C;
        DWORD data16_lo = last->data16_lo;
        DWORD data16_hi = last->data16_hi;
        g_dword_20FC9C8 = pad0C;   // 0x20FC9C8  write-only scratch (never read)

        F3ObjectListAddNode(&g_stru_20FC988.l04, obj, pad0C, data16_lo, data16_hi);

        F3ObjectListNode *head = l20->head;
        DWORD out[2] = { 0, 0 };
        F3ListRemoveNode(l20, out, head, last);
    }

    // l20 is empty: re-check l04
    if (g_stru_20FC988.l04.count == 0)
    {
        DWORD dev = g_stru_20FC988.f00;
        if (dev != 0)
        {
            void **devVTable = *(void ***)dev;
            int (__stdcall *acq)(DWORD, int, BYTE *) =
                (int (__stdcall *)(DWORD, int, BYTE *))devVTable[118];  // offset 472

            BYTE buffer[20] = { 0 };
            if (acq(dev, 8, buffer) == 0)
            {
                void *wrappedObj = *(void **)buffer;
            DWORD pad0C = (DWORD)(buffer[4] | (buffer[5] << 8) | (buffer[6] << 16) | ((DWORD)buffer[7] << 24));
            DWORD data16_lo = (DWORD)(buffer[8] | (buffer[9] << 8) | (buffer[10] << 16) | ((DWORD)buffer[11] << 24));
            DWORD data16_hi = (DWORD)(buffer[12] | (buffer[13] << 8) | (buffer[14] << 16) | ((DWORD)buffer[15] << 24));
            F3ObjectListAddNode(&g_stru_20FC988.l04, wrappedObj, pad0C, data16_lo, data16_hi);
            }
        }
    }

    if (g_stru_20FC988.l04.count > 0)
    {
        // release the last l04 node's wrapped object via vtable[6] (offset 24)
        // and re-add it to l20
        F3ObjectList *l04 = &g_stru_20FC988.l04;
        F3ObjectListNode *last = l04->tail->prev;

        void *obj = last->obj;
        DWORD pad0C = last->pad0C;
        DWORD data16_lo = last->data16_lo;
        DWORD data16_hi = last->data16_hi;

        F3ListRemoveTail(l04, &last);

        if (obj != 0)
        {
            void **ovt = *(void ***)obj;
            void (__stdcall *release)(void *, int) =
                (void (__stdcall *)(void *, int))ovt[6];  // offset 24
            release(obj, 1);
        }

        F3ObjectListAddNode(&g_stru_20FC988.l20, obj, pad0C, data16_lo, data16_hi);
    }

    f3_flush_done:
    ;

    // counter bump
    ++g_stru_20FC988.f3C;

    // FPS record update
    g_dbl_2074580 = GetEngineTime();
    F3FpsRecord *rec = &g_FpsRecords[g_dword_20B5F74];
    rec->prevFrameTime = rec->frameTime;
    rec->frameTime = g_dbl_2074580;

    result = 3 * (int)g_dword_20B5F74;

    ++rec->frameCounter;

    g_dbl_2074578 = g_dbl_2074580;
    ++g_dword_20B5F70;

    return result;
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
    if (g_dword_1DB1240[1] != 0)
    {
        void **ovt = *(void ***)g_dword_1DB1240[1];
        void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
        release((void *)g_dword_1DB1240[1]);
        g_dword_1DB1240[1] = 0;
        g_dword_1DB1240[2] = 0;
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
//  XLO device-build state globals (sub_18F1B30 / CreateXloDevice and its
//  helpers).  All zero in the image; here they live as static globals in this
//  module (g_dword_2074540 is the one engine-wide exception - it is an
//  extern declared in common/common.h and shared with gamemain.cpp).
// ============================================================
static DWORD  g_dword_1DB11F4[11];   // 0x1DB11F4  the last 44-byte display-mode request
static DWORD  g_dword_20FC984 = 0;   // 0x20FC984  format-table index (request field[0])
// (g_dword_1E2ED74 is defined at the top of the ResetXloDevice cluster.)
static float  g_flt_1E2ED78 = 0.0f;  // 0x1E2ED78  device refresh rate
static float  g_flt_20FC968 = 0.0f;  // 0x20FC968  device refresh rate (mirror)
DWORD          g_dword_2074540 = 0;   // 0x2074540  the built device (mirror of 1E2EEF4; engine-wide extern, see common/common.h)
static DWORD  g_dword_1E2EEF0 = 0;   // 0x1E2EEF0  NVAPI device-registration scratch
static DWORD  g_dword_1E2EE7C = 0;   // 0x1E2EE7C  text-caps threshold (low byte + 10*byte1)
// 0x1BD3030: the rdata display-mode flags table (4 valid values + 0 sentinel).
static const DWORD g_dword_1BD3030[5] = { 0x47, 0x4B, 0x4D, 0x50, 0 };

// ============================================================
//  FillFpsRecordStackStruct  (0x18CB350 - 0x18CB3C6)  -- snapshot the
//  current FPS record + the globals into a 44-byte stack struct. The
//  original is a __usercall (the scratch struct in ESI, returns ESI);
//  here it is a plain free function whose pointer arg is named `out`.
//  [NOT byte-identical] placement-dependent (relative calls to the
//  InterlockedIncrement / InterlockedDecrement IAT slots) and refs the
//  high-VA globals (dbl_2074548 / dword_20B5F74 / dword_20B5F70 /
//  qword_2074580 / qword_2074578) at their original addresses.
//
//  void *FillFpsRecordStackStruct(void *out)  -- fills *out (a 44-byte
//  stack scratch record) and returns it.
//
//  Fills the scratch record from the current FPS record (frameTime at
//  record +0, prevFrameTime at +8, frameCounter at +0x10) and the
//  globals: last flush time (qword_2074580) at scratch +0, last flush
//  time copy (qword_2074578) at +8, global frame counter (dword_20B5F70)
//  at +0x10, record frameTime at +0x18, record prevFrameTime at +0x20,
//  record frameCounter at +0x28. The record's interlocked guard (record
//  +0x14) is incremented for the read and decremented on the way out
//  (net zero). Only the +0x10 / +0x18 / +0x28 fields are read back --
//  the scratch record is written for the whole struct to keep the
//  original's callee-writes-44-bytes / caller-reads-a-few structure.
// ============================================================
static void *FillFpsRecordStackStruct(void *out)
{
    F3FpsRecord *rec = &g_FpsRecords[g_dword_20B5F74];
    InterlockedIncrement((volatile LONG *)&rec->interlocked);

    double *pd = (double *)out;
    DWORD  *pi = (DWORD *)out;

    pd[0] = g_dbl_2074580;      // last flush time (qword_2074580)
    pd[1] = g_dbl_2074578;      // last flush time (copy)
    pi[4] = g_dword_20B5F70;    // global frame counter (dword_20B5F70)
    pd[3] = rec->frameTime;     // current record's frame time
    pd[4] = rec->prevFrameTime; // current record's previous frame time
    pi[10] = rec->frameCounter; // current record's frame counter

    InterlockedDecrement((volatile LONG *)&rec->interlocked);
    return out;
}

// ============================================================
//  GetInterpolatedFrameTime  (0x18F0460 - 0x18F04B0)  -- the per-frame
//  interpolated frame time reconstructed from the current FPS record and
//  the global frame counter.
//  [NOT byte-identical] placement-dependent (relative calls to the
//  __security_check_cookie IAT slot and the record-read helper) and refs
//  the high-VA globals dword_1E2ED74 (render-mode class) / flt_1E2ED78
//  (device refresh rate) / dbl_2074548 / dword_20B5F74 / dword_20B5F70 at
//  their original addresses.
//
//  double GetInterpolatedFrameTime(void)  -- no args, returns double
//
//  Fills the 44-byte scratch record through FillFpsRecordStackStruct
//  (sub_18CB350), then computes, in excess precision,
//    (dword_1E2ED74 / flt_1E2ED78) * (globalFrameCounter - recordFrameCounter + 1)
//      + recordFrameTime
//  exactly as the original's fild / fdiv / inc / fild / fmulp / fadd
//  sequence does (the security-cookie check is omitted, as everywhere in
//  this build). Sole caller FlushRenderFrame (0x18F1F60), which stores the
//  result into dbl_20FC960 under the device CS.
// ============================================================
double GetInterpolatedFrameTime(void)
{
    struct F3FpsStackSnapshot
    {
        double lastFlushTime;       // +0
        double lastFlushTimeCopy;   // +8
        DWORD  globalFrameCounter;  // +0x10
        double frameTime;           // +0x18
        double prevFrameTime;       // +0x20
        DWORD  recordFrameCounter;  // +0x28
    };

    F3FpsStackSnapshot s;
    FillFpsRecordStackStruct(&s);
    return (double)g_dword_1E2ED74 / (double)g_flt_1E2ED78
           * (double)((int)s.globalFrameCounter - (int)s.recordFrameCounter + 1)
           + s.frameTime;
}

// ============================================================
//  F3FindFormatRow  -- walk the texture source-descriptor table
//  (g_dword_1D9AF60, 0x70-byte rows) for the row whose first word (the
//  pixel-format lookup key) equals `key`; returns the row index, or 0 when no
//  row matches (the original resets to row 0 at the table end).
// ============================================================
static int F3FindFormatRow(DWORD key)
{
    int idx = 0;
    const DWORD *row = g_dword_1D9AF60;
    while (row[0] != key)
    {
        row += 28;
        ++idx;
        if ((const void *)row >= (const void *)&g_dword_1D9AF60[F3_FORMAT_TABLE_END])
        {
            idx = 0;
            break;
        }
    }
    return idx;
}

// ============================================================
//  PickBestDisplayMode  (sub_18F05D0)  -- pick the closest matching display
//  mode
//
//  DWORD *PickBestDisplayMode(DWORD *out, DWORD *fmtIdx, int targetW, int targetH)
//      -- stores the best table row index in *out and returns out
//
//  Walks the rdata flags table g_dword_1BD3030 (4 valid + 0 sentinel). For each
//  flags value, checks the D3D9 object's caps query (vtable[10], offset 40) and
//  mode check (vtable[12], offset 48) with the format row key
//  (g_dword_1D9AF60[28 * *fmtIdx]); when both succeed, finds the table row whose
//  key equals the flags value and computes the squared distance from that row's
//  width/height (row[13]/row[14]) to the target, keeping the minimum. Stops
//  early on distance 0 or at the sentinel. Returns the best row index (0 = none).
// ============================================================
static DWORD *PickBestDisplayMode(DWORD *out, DWORD *fmtIdx, int targetW, int targetH)
{
    int best = 0;
    int bestDist = 0x7FFFFFFF;
    int i = 0;
    const DWORD *flags = g_dword_1BD3030;
    // Stock d3d9 COM dispatches (this in ECX, args after it):
    //   slot 10 = CheckDeviceFormat(Adapter, DeviceType, AdapterFormat,
    //                               Usage, RType, CheckFormat) -> HRESULT
    //   slot 12 = CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat,
    //                                   RenderTargetFormat, DepthStencilFormat)
    // In the original's private-vtable factory these methods took the factory
    // pointer as their first stack argument; the stock interface does not.
    void **vt = *(void ***)g_dword_1E2EEF8;
    typedef long (__stdcall *F3CapsFn)(void *, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
    typedef long (__stdcall *F3ModeFn)(void *, DWORD, DWORD, DWORD, DWORD, DWORD);
    F3CapsFn caps = vt != NULL ? (F3CapsFn)vt[10] : 0;   // offset 40
    F3ModeFn check = vt != NULL ? (F3ModeFn)vt[12] : 0;  // offset 48
    for (;;)
    {
        DWORD fmtKey = g_dword_1D9AF60[28 * (*fmtIdx)];
        if (caps != 0 && check != 0 &&
            caps((void *)g_dword_1E2EEF8, g_dword_1E2EDB8[1], g_dword_1E2EDB8[0], fmtKey, 2, 1, flags[i]) >= 0 &&
            check((void *)g_dword_1E2EEF8, g_dword_1E2EDB8[1], g_dword_1E2EDB8[0], fmtKey, fmtKey, flags[i]) >= 0)
        {
            int idx = F3FindFormatRow(flags[i]);
            int dw = (int)g_dword_1D9AF60[28 * idx + 13] - targetW;
            int dh = (int)g_dword_1D9AF60[28 * idx + 14] - targetH;
            int dist = dw * dw + dh * dh;
            if (dist < bestDist)
            {
                best = idx;
                bestDist = dist;
            }
            if (dist == 0)
                break;
        }
        if (g_dword_1BD3030[i + 1] == 0)
            break;
        ++i;
    }
    *out = (DWORD)best;
    return out;
}

// ============================================================
//  CreateXloDeviceFromAdapter  (sub_18F04B0)  -- create the XLO device on the
//  D3D9 object
//
//  int CreateXloDeviceFromAdapter(DWORD adapter, const DWORD *caps,   // 56-byte caps block
//                                 const F3XloCaps *req, DWORD deviceType)
//
//  Call the D3D9 object's CreateDevice (stock COM slot 16, offset 64) with
//  (factory, adapter, deviceType, hWndParent, 68, caps, &dword_1E2EEF4),
//  setting byte_2075F05 = 0 (classic path). Returns the HRESULT. The original
//  game used a private factory whose CreateDevice took the factory as the
//  first stack argument and supported a CreateDeviceEx (slot 20, Ex path);
//  that is unreachable in this build (the Ex flag never appears), so the
//  classic path is used unconditionally.
// ============================================================
static int CreateXloDeviceFromAdapter(DWORD adapter, const DWORD *caps,
                                      const F3XloCaps *req, DWORD deviceType)
{
    int hr;
    // Stock d3d9 COM dispatch: slot 16 = CreateDevice(Adapter, DeviceType,
    // HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**),
    // with `this` (the factory) in ECX. The original game used a private
    // factory whose CreateDevice also took the factory as the first stack arg;
    // the stock interface does not.
    void **vt = *(void ***)g_dword_1E2EEF8;
    typedef int (__stdcall *F3CreateDeviceFn)(void *, DWORD, DWORD, HWND, DWORD, const DWORD *, DWORD *);
    if (vt == NULL || (void *)vt[16] == NULL)
        return -1;   // no usable factory: report failure (never dispatches to NULL)
    F3CreateDeviceFn fn = (F3CreateDeviceFn)vt[16];   // offset 64
    hr = fn((void *)g_dword_1E2EEF8, adapter, deviceType, g_hWndParent, 68, caps, (DWORD *)&g_dword_1E2EEF4);
    g_byte_2075F05 = 0;
    return hr;
}

// ============================================================
//  SetDeviceViewport  (sub_18F07B0)  -- set the device viewport to a rect
//
//  int SetDeviceViewport(const DWORD *rect)   -- rect = {x, y, right, bottom}
//
//  Copies the 4-DWORD rect into the current slot's row in the per-slot
//  client-rect table (g_dword_20FCB08), then builds a D3DVIEWPORT9
//  {x, y, width=right-x, height=bottom-y, minz=0, maxz=1.0f} and calls the
//  device's SetViewport (vtable[47], offset 188). Returns the result.
// ============================================================
static int SetDeviceViewport(const DWORD *rect)
{
    DWORD slot = g_dword_2075F10;
    DWORD *row = &g_dword_20FCB08[slot * 44];
    row[0] = rect[0];
    row[1] = rect[1];
    row[2] = rect[2];
    row[3] = rect[3];

    DWORD vp[6];
    vp[0] = rect[0];
    vp[1] = rect[1];
    vp[2] = rect[2] - rect[0];
    vp[3] = rect[3] - rect[1];
    vp[4] = 0;
    vp[5] = 0x3F800000;   // 1.0f (ds:dword_1BD2A1C)

    void **vt = *(void ***)g_dword_1E2EEF4;
    typedef int (__stdcall *F3ViewportFn)(DWORD, DWORD *);
    F3ViewportFn fn = (F3ViewportFn)vt[47];   // offset 188
    return fn((DWORD)g_dword_1E2EEF4, vp);
}

// ============================================================
//  CopySlotRecord  (sub_18D1B70)  -- copy a 32-byte record, releasing the
//  destination's wrapped object first
//
//  DWORD *CopySlotRecord(DWORD *src, DWORD *dst)   -- usercall (src in EDI, dst in ESI)
//
//  When src != dst: release dst[1] (the destination's wrapped vtable object,
//  offset +4) via vtable[2] (offset 8) and clear dst[1..2]; then copy
//  src[1..7] (28 bytes) into dst[1..7]; then, if the new dst[1] is non-zero,
//  call its vtable[1] (offset 4, a no-arg method taking the object). Returns
//  dst.
// ============================================================
static DWORD *CopySlotRecord(DWORD *src, DWORD *dst)
{
    if (src != dst)
    {
        if (dst[1] != 0)
        {
            void **ovt = *(void ***)dst[1];
            void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
            release((void *)dst[1]);
            dst[1] = 0;
            dst[2] = 0;
        }
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        dst[4] = src[4];
        dst[5] = src[5];
        dst[6] = src[6];
        dst[7] = src[7];
        if (dst[1] != 0)
        {
            void **ovt = *(void ***)dst[1];
            void (__stdcall *init)(void *) = (void (__stdcall *)(void *))ovt[1];
            init((void *)dst[1]);
        }
    }
    return dst;
}

// ============================================================
//  RegisterDeviceSlotRecords  (sub_18F0CB0)  -- register the device's slot
//  records (usercall; the surface-record base in EAX)
//
//  int RegisterDeviceSlotRecords(const DWORD *surfRec)
//
//  Copies the surface record (surfRec, the 0x1DB1240 block) into the current
//  slot's row in the per-slot record table (CopySlotRecord), then calls the
//  device's register method (vtable[37], offset 0x94) with (device, 0,
//  surfRec[1]). Then, for v = 1..3: call the register method with (device, v, 0)
//  and release + clear the slot row's sub-record at byte offset v*32 (row
//  DWORDs i/4+1 and i/4+2 for i = 32, 64, 96). Returns the ApplySlotViewport result (the slot's stored viewport applied to
//  the device).
// ============================================================
static int RegisterDeviceSlotRecords(const DWORD *surfRec)
{
    DWORD slot = g_dword_2075F10;
    CopySlotRecord((DWORD *)surfRec, (DWORD *)&g_dword_20FCA68[slot * 44]);
    void **vt = *(void ***)g_dword_1E2EEF4;
    typedef void (__stdcall *F3RegFn)(DWORD, DWORD, DWORD);
    F3RegFn reg = (F3RegFn)vt[37];   // offset 0x94
    reg((DWORD)g_dword_1E2EEF4, 0, surfRec[1]);
    DWORD v = 1;
    for (int i = 32; i < 128; i += 32)
    {
        reg((DWORD)g_dword_1E2EEF4, v, 0);
        DWORD *row = &g_dword_20FCA68[slot * 44] + i / 4;
        if (row[1] != 0)
        {
            void **ovt = *(void ***)row[1];
            void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
            release((void *)row[1]);
            row[1] = 0;
            row[2] = 0;
        }
        ++v;
    }
    return ApplySlotViewport();
}

// ============================================================
//  CheckDeviceTextCaps  (sub_18F08D0)  -- check the device's text caps
//
//  char CheckDeviceTextCaps(void)
//
//  If 100 * (low byte of dword_1E2EE7C + 10 * byte1 of dword_1E2EE7C) >= 0xBB8,
//  return 1. Otherwise, call the D3D9 object's caps query (vtable[10], offset
//  40) with (d3d9, adapter, 1, 22, 0, 1, 0x54657874); on success, call the
//  device's set method (vtable[57], offset 228) with (device, 154, 0x54657874)
//  and return 1. Returns 0 otherwise.
// ============================================================
static char CheckDeviceTextCaps(void)
{
    DWORD v = g_dword_1E2EE7C;
    if (100 * ((unsigned __int8)v + 10 * (unsigned int)((v >> 8) & 0xFF)) >= 0xBB8)
        return 1;
    void **vt = *(void ***)g_dword_1E2EEF8;
    typedef int (__stdcall *F3CapsFn)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
    F3CapsFn caps = (F3CapsFn)vt[10];   // offset 40
    if (caps(g_dword_1E2EEF8, g_dword_1E2EDB8[1], 1, 22, 0, 1, 0x54657874) >= 0)
    {
        void **dvt = *(void ***)g_dword_1E2EEF4;
        typedef void (__stdcall *F3SetFn)(DWORD, DWORD, DWORD);
        F3SetFn set = (F3SetFn)dvt[57];   // offset 228
        set((DWORD)g_dword_1E2EEF4, 154, 0x54657874);
        return 1;
    }
    return 0;
}

// ============================================================
//  NvApiLoadInit  (sub_17D1D10)  -- load nvapi.dll and resolve the interface
//  hooks
//
//  int NvApiLoadInit(void)
//
//  Lazily LoadLibraryA("nvapi.dll") (returns -2 on failure). Resolve
//  nvapi_QueryInterface into dword_1E2C708; if absent, FreeLibrary and return
//  -1. Call the resolver with 0x150E828 to obtain the base interface (v1); if
//  v1 is NULL or v1(v1) != 0, clear the resolver, FreeLibrary and return -1.
//  Otherwise resolve the before/after hooks (dword_1E2C70C / dword_1E2C710)
//  via the resolver with 0x33C7358C / 0x593E8644; if one is set and the other
//  is not, clear both. Returns 0 on success.
// ============================================================
static int NvApiLoadInit(void)
{
    int (__cdecl *v1)(void);

    if (g_hLibModule == 0)
    {
        g_hLibModule = LoadLibraryA("nvapi.dll");
        if (g_hLibModule == 0)
            return -2;
    }
    g_dword_1E2C708 = (int (__cdecl *)(DWORD))GetProcAddress(g_hLibModule, "nvapi_QueryInterface");
    if (g_dword_1E2C708 == 0)
    {
        FreeLibrary(g_hLibModule);
        g_hLibModule = 0;
        return -1;
    }
    v1 = (int (__cdecl *)(void))g_dword_1E2C708(0x150E828);
    if (v1 == 0 || v1() != 0)
    {
        g_dword_1E2C708 = 0;
        FreeLibrary(g_hLibModule);
        g_hLibModule = 0;
        return -1;
    }
    g_dword_1E2C70C = (int (__cdecl *)(DWORD, int *))g_dword_1E2C708(0x33C7358C);
    g_dword_1E2C710 = (int (__cdecl *)(DWORD, int, int, int))g_dword_1E2C708(0x593E8644);
    if ((g_dword_1E2C70C != 0) != (g_dword_1E2C710 != 0))
    {
        g_dword_1E2C70C = 0;
        g_dword_1E2C710 = 0;
    }
    return 0;
}

// ============================================================
//  NvApiRegisterDevice  (sub_17D6680)  -- register the device with an NVAPI
//  interface (interface id 0xAC7E37F4)
//
//  int NvApiRegisterDevice(int a, int b)   -- two args (cdecl)
//
//  Lazily resolves the interface entry (interface id 0xAC7E37F4) into the
//  interface-table slot at 0x1D9465C (flag at 0x1D94660). Returns -3 if no
//  entry is available. Otherwise runs the resolved entry with (a, b),
//  bracketed by the before-hook dword_1E2C70C(id, &pre) and the after-hook
//  dword_1E2C710(id, pre, 0, result). Returns the entry's result.
// ============================================================
static int NvApiRegisterDevice(int a, int b)
{
    F3NvApiSlot *slot = (F3NvApiSlot *)((char *)g_dword_1D941F4 + (0x1D9465C - 0x1D941F4));
    int pre = 0;
    int result;

    if (g_dword_1E2C708 != 0 && slot->entry == 0 && slot->resolved == 0)
    {
        slot->entry = (int (__cdecl *)(void))g_dword_1E2C708(0xAC7E37F4);
        slot->resolved = 1;
    }
    if (slot->entry == 0)
        return -3;
    if (g_dword_1E2C70C != 0)
        g_dword_1E2C70C(0xAC7E37F4, &pre);
    result = ((int (__cdecl *)(int, int))slot->entry)(a, b);
    if (g_dword_1E2C710 != 0)
        g_dword_1E2C710(0xAC7E37F4, pre, 0, result);
    return result;
}

// ============================================================
//  NvApiConfigure  (sub_17D6160)  -- configure the device's NVAPI flush state
//  (interface id 0xBE7692EC)
//
//  int NvApiConfigure(int a)   -- one arg (cdecl)
//
//  Lazily resolves the interface entry (interface id 0xBE7692EC) into the
//  interface-table slot at 0x1D94608 (flag at 0x1D9460C). Returns -3 if no
//  entry is available. Otherwise runs the resolved entry with (a), bracketed
//  by the before-hook dword_1E2C70C(id, &pre) and the after-hook
//  dword_1E2C710(id, pre, 0, result). Returns the entry's result.
// ============================================================
static int NvApiConfigure(int a)
{
    F3NvApiSlot *slot = (F3NvApiSlot *)((char *)g_dword_1D941F4 + (0x1D94608 - 0x1D941F4));
    int pre = 0;
    int result;

    if (g_dword_1E2C708 != 0 && slot->entry == 0 && slot->resolved == 0)
    {
        slot->entry = (int (__cdecl *)(void))g_dword_1E2C708(0xBE7692EC);
        slot->resolved = 1;
    }
    if (slot->entry == 0)
        return -3;
    if (g_dword_1E2C70C != 0)
        g_dword_1E2C70C(0xBE7692EC, &pre);
    result = ((int (__cdecl *)(int))slot->entry)(a);
    if (g_dword_1E2C710 != 0)
        g_dword_1E2C710(0xBE7692EC, pre, 0, result);
    return result;
}

// ============================================================
//  InitXloDeviceState  (sub_18F1310)  -- initialize the XLO device state
//
//  DWORD InitXloDeviceState(void)
//
//  1. Acquire the device's backbuffer surface (vtable[18], offset 72) with
//     (dev, 0, 0, 0, &backbuffer).
//  2. Release the previous backbuffer object (g_dword_1DB1240[1], vtable[2])
//     if non-zero; store the new backbuffer in g_dword_1DB1240[1] and set the
//     present flag g_dword_1DB1240[2] = (backbuffer != 0).
//  3. Refresh the 5-word format record (g_dword_1DB1240[3..7]) from the
//     backbuffer (QueryCsurfaceFormat).
//  4. Register the device's slot records (RegisterDeviceSlotRecords, the
//     0x1DB1240 surface record).
//  5. Check the device's text caps (CheckDeviceTextCaps).
//  6. Set the current resolution (g_qword_20FC928) from the caps block
//     (g_dword_1E2ED80[0..1] = width/height).
//  7. Call the device's acquire method (vtable[118], offset 0x1D8) with
//     (dev, 8, &g_dword_2075F14).
//  8. Store the device in the container's f00 (g_stru_20FC988.f00).
//  Returns the device pointer (g_dword_1E2EEF4).
// ============================================================
static DWORD InitXloDeviceState(void)
{
    DWORD dev = (DWORD)g_dword_1E2EEF4;
    void **vt = *(void ***)dev;

    DWORD backbuffer = 0;
    typedef void (__stdcall *F3BackBufFn)(DWORD, DWORD, DWORD, DWORD, DWORD *);
    ((F3BackBufFn)vt[18])(dev, 0, 0, 0, &backbuffer);   // offset 72

    if (g_dword_1DB1240[1] != 0)
    {
        void **ovt = *(void ***)g_dword_1DB1240[1];
        void (__stdcall *release)(void *) = (void (__stdcall *)(void *))ovt[2];
        release((void *)g_dword_1DB1240[1]);
    }
    g_dword_1DB1240[1] = backbuffer;
    g_dword_1DB1240[2] = (backbuffer != 0);
    QueryCsurfaceFormat((int *)&g_dword_1DB1240[3], (void *)backbuffer);

    RegisterDeviceSlotRecords(&g_dword_1DB1240[0]);
    CheckDeviceTextCaps();

    g_qword_20FC928[0] = g_dword_1E2ED80[0];
    g_qword_20FC928[1] = g_dword_1E2ED80[1];

    typedef void (__stdcall *F3AcqFn)(DWORD, DWORD, DWORD *);
    ((F3AcqFn)vt[118])(dev, 8, (DWORD *)&g_dword_2075F14);   // offset 0x1D8

    g_stru_20FC988.f00 = (DWORD)dev;
    return (DWORD)dev;
}

// ============================================================
//  BuildXloDeviceFromRequest  (CreateXloDevice, sub_18F1B30 - 0x18F1E60)  --
//  build the XLO device from the 56-byte display-mode request
//
//  int BuildXloDeviceFromRequest(const F3XloCaps *req)   -- 1 = success, 0 = fail
//
//  Only reached once CreateD3D9DisplayObject has produced a live D3D9 object in
//  dword_1E2EEF8.  Destroys any previous device, saves the 44-byte request to
//  0x1DB11F4, builds the 56-byte caps block, allocates the flush-request
//  object, creates the device on the D3D9 object (with the
//  0x8876086A/0x88760868/0x8876086C retry logic), captures the device's
//  refresh rate, copies the caps block to the live global (dword_1E2ED80),
//  sets the render-mode class (dword_1E2ED74) and current resolution
//  (qword_20FC928), sets the full-size viewport (SetDeviceViewport, sub_18F07B0),
//  initializes the device state (InitXloDeviceState, sub_18F1310), loads +
//  registers NVAPI (NvApiLoadInit sub_17D1D10 / NvApiRegisterDevice sub_17D6680),
//  and registers the flush-request resource (NvApiRegisterDevice, sub_17D6680).
//  Returns 1 on
//  success, 0 when the device cannot be created.
//
//  [NOT byte-identical] placement-dependent (relative calls to the D3D9 / XLO
//  device-rebuild subsystem + the __security_check_cookie IAT slot) and refs
//  the high-VA device globals at their original addresses.
// ============================================================
static int BuildXloDeviceFromRequest(const F3XloCaps *req)
{
    DWORD caps[14];
    DWORD out[4];
    int behavior;

    DestroyDevice();

    // Save the 44-byte request (11 DWORDs) at 0x1DB11F4..0x1DB121C.
    memcpy(g_dword_1DB11F4, req->field, 44);

    // Build the 56-byte caps block (the live copy the device vtable reads).
    {
        BYTE b24 = (BYTE)(req->field[6] & 0xFF);
        BYTE b25 = (BYTE)((req->field[6] >> 8) & 0xFF);
        memset(caps, 0, sizeof(caps));
        caps[0]  = req->field[2];                                // width
        caps[1]  = req->field[3];                                // height
        caps[2]  = g_dword_1D9AF60[28 * req->field[0]];          // format key
        caps[3]  = req->field[4];
        caps[6]  = (b24 == 0) ? 3 : 2;
        caps[7]  = (DWORD)g_hWndParent;
        caps[8]  = (b24 == 0) ? 1 : 0;
        caps[12] = req->field[5];
        caps[13] = (b25 == 0) ? 0x80000000 : (DWORD)GetDisplayModeClass(req->field[8]);
    }
    g_dword_20FC984 = req->field[0];

    behavior = ((req->field[10] & 2) != 0) + 1;

    // Allocate + initialize the flush-request object if absent.
    if (g_dword_2075F00 == 0)
    {
        void *flush = LHAlloc(20);
        if (flush != 0)
        {
            memset(flush, 0, 20);
            ((BYTE *)flush)[18] = 1;
            NvApiConfigure(1);
            g_dword_2075F00 = flush;
        }
        else
        {
            g_dword_2075F00 = 0;
        }
    }

    // Create the device on the D3D9 object (with the retry logic).
    int st = CreateXloDeviceFromAdapter(g_dword_1E2EDB8[1], caps, req, (DWORD)behavior);
    if (st != 0)
    {
        for (;;)
        {
            if (st != 0x8876086A && st != 0x88760868 && st != 0x8876086C)
                return 0;
            if ((req->field[10] & 1) == 0)
                break;
            Sleep(0x64u);
            st = CreateXloDeviceFromAdapter(g_dword_1E2EDB8[1], caps, req, (DWORD)behavior);
            if (st == 0)
                goto created;
        }
        if ((req->field[10] & 4) == 0 ||
            CreateXloDeviceFromAdapter(g_dword_1E2EDB8[1], caps, req, 2) != 0)
            return 0;
    }
created:
    // Query the device's display mode (vtable[8], offset 0x20) and capture the
    // refresh rate (the int64 at out+8; add 2^31 when the low word is negative).
    {
        void **vt = *(void ***)g_dword_1E2EEF4;
        typedef void (__stdcall *F3ModeFn)(DWORD, DWORD, DWORD *);
        F3ModeFn fn = (F3ModeFn)vt[8];
        memset(out, 0, sizeof(out));
        fn((DWORD)g_dword_1E2EEF4, 0, out);
        long long refresh64 = *(long long *)(out + 2);
        if ((int)out[2] < 0)
            refresh64 += 2147483648LL;
        g_flt_1E2ED78 = (float)refresh64;
        g_flt_20FC968 = (float)refresh64;
    }

    // Copy the caps block to the live global (g_dword_1E2ED80 / xmmword 1E2ED80).
    memcpy(g_dword_1E2ED80, caps, sizeof(caps));

    // Set the device render-mode class (dword_1E2ED74) from caps[13].
    {
        DWORD mc = caps[13];
        if ((mc & 0x80000001) != 0)
            g_dword_1E2ED74 = 1;
        else if ((mc & 0x2) != 0)
            g_dword_1E2ED74 = 2;
        else if ((mc & 0x4) != 0)
            g_dword_1E2ED74 = 3;
        else
            g_dword_1E2ED74 = ((mc & 0x8) != 0) ? 4 : 1;
    }

    // Set the current resolution (qword_20FC928) from the caps block.
    g_qword_20FC928[0] = caps[0];
    g_qword_20FC928[1] = caps[1];

    // Set the device viewport to the full width/height.
    {
        DWORD rect[4];
        rect[0] = 0;
        rect[1] = 0;
        rect[2] = caps[0];
        rect[3] = caps[1];
        SetDeviceViewport(rect);
    }

    // Initialize the device state (backbuffer, format record, slot records,
    // text caps, resolution, acquire method, container f00).
    InitXloDeviceState();

    g_dword_2074540 = (DWORD)g_dword_1E2EEF4;

    // Load + init NVAPI, then register the device.
    if (NvApiLoadInit() == 0)
    {
        if (NvApiRegisterDevice((int)g_dword_1E2EEF4, (int)&g_dword_1E2EEF0) != 0)
            NvApiTeardownUnload();
        else
            g_byte_2075DDB = 1;
    }

    // Register the device's flush-request resource with NVAPI.
    {
        void *flush = g_dword_2075F00;
        NvApiRegisterDevice((int)g_dword_1E2EEF4, (int)((char *)flush + 12));
        ((BYTE *)flush)[16] = 1;
    }
    return 1;
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
//  SetPauseGameOnVMState  (0x18F6C90 - 0x18F6C99)  -- set the worker's
//  pause-game VM-state callback (the worker field at +0x25C)
//
//  [BYTE-IDENTICAL]  A __thiscall + retn 4 hybrid, the sibling of
//  SetOnCreateDevice / SetOnResetDevice: the worker is in ECX and the
//  callback is a stack arg the callee cleans (ret 4). It stores the
//  callback in the worker's pauseGameOnVMState field and returns it.
//  The caller must dispatch it as a __thiscall function (the F3ThiscallSetFn
//  cast) so that the worker lands in ECX.
//
//  F3CallbackFn SetPauseGameOnVMState(CWorkerWin32 *worker, F3CallbackFn cb)
// ============================================================
__declspec(naked) F3CallbackFn SetPauseGameOnVMState(void *worker, F3CallbackFn cb)
{
    __asm {
        mov    eax, [esp+4]      ; eax = cb
        mov    [ecx+25Ch], eax   ; worker->pauseGameOnVMState = cb
        ret    4                 ; return cb, clean up the stack arg
    }
}

// ============================================================
//  SetMultiplayerPauseOnVMState  (0x18F6C80 - 0x18F6C8D)  -- set the
//  worker's multi-player pause VM-state callback (the worker field at +0x260)
//
//  [BYTE-IDENTICAL]  A __thiscall + retn 4 hybrid, the sibling of
//  SetOnCreateDevice / SetOnResetDevice / SetPauseGameOnVMState: the
//  worker is in ECX and the callback is a stack arg the callee cleans
//  (ret 4). It stores the callback in the worker's multiplayerPauseOnVMState
//  field and returns it. The caller must dispatch it as a __thiscall function
//  (the F3ThiscallSetFn cast) so that the worker lands in ECX.
//
//  F3CallbackFn SetMultiplayerPauseOnVMState(CWorkerWin32 *worker, F3CallbackFn cb)
// ============================================================
__declspec(naked) F3CallbackFn SetMultiplayerPauseOnVMState(void *worker, F3CallbackFn cb)
{
    __asm {
        mov    eax, [esp+4]      ; eax = cb
        mov    [ecx+260h], eax   ; worker->multiplayerPauseOnVMState = cb
        ret    4                 ; return cb, clean up the stack arg
    }
}

// ============================================================
//  SetArmedPauseOnVMState  (0x18F6C70 - 0x18F6C7D)  -- set the worker's
//  audio-armed / window-placement VM-state callback (the worker field at +0x264)
//
//  [BYTE-IDENTICAL]  A __thiscall + retn 4 hybrid, the sibling of
//  SetOnCreateDevice / SetOnResetDevice / SetPauseGameOnVMState /
//  SetMultiplayerPauseOnVMState: the worker is in ECX and the callback is a
//  stack arg the callee cleans (ret 4). It stores the callback in the worker's
//  armedPauseOnVMState field and returns it. The caller must dispatch it as a
//  __thiscall function (the F3ThiscallSetFn cast) so that the worker lands
//  in ECX.
//
//  F3CallbackFn SetArmedPauseOnVMState(CWorkerWin32 *worker, F3CallbackFn cb)
// ============================================================
__declspec(naked) F3CallbackFn SetArmedPauseOnVMState(void *worker, F3CallbackFn cb)
{
    __asm {
        mov    eax, [esp+4]      ; eax = cb
        mov    [ecx+264h], eax   ; worker->armedPauseOnVMState = cb
        ret    4                 ; return cb, clean up the stack arg
    }
}

// ============================================================
//  SetNetHeldCoopFailed  (0x18F6C60 - 0x18F6C6D)  -- set the worker's
//  net-held coop-failed callback (the worker field at +0x268)
//
//  [BYTE-IDENTICAL]  A __thiscall + retn 4 hybrid, the sibling of
//  SetOnCreateDevice / SetOnResetDevice / SetPauseGameOnVMState /
//  SetMultiplayerPauseOnVMState / SetArmedPauseOnVMState: the worker
//  is in ECX and the callback is a stack arg the callee cleans
//  (ret 4). It stores the callback in the worker's netHeldCoopFailed
//  field and returns it. The caller must dispatch it as a __thiscall
//  function (the F3ThiscallSetFn cast) so that the worker lands in ECX.
//
//  F3CallbackFn SetNetHeldCoopFailed(CWorkerWin32 *worker, F3CallbackFn cb)
// ============================================================
__declspec(naked) F3CallbackFn SetNetHeldCoopFailed(void *worker, F3CallbackFn cb)
{
    __asm {
        mov    eax, [esp+4]      ; eax = cb
        mov    [ecx+268h], eax   ; worker->netHeldCoopFailed = cb
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
