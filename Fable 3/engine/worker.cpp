// Fable 3 — CWorkerWin32 module implementation (the 664-byte main worker
// object, its deleting destructor, and the current-worker registry).

#include "worker.h"
#include "common/common.h"

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
//  GetEngineTime  (0x18A88E0)  -- seconds since engine start
//  [NOT byte-identical] placement-independent but references the QueryPerformanceCounter
//  IAT slot (0x01AD50C8) and start globals (0x020B5FC0/0x020B5FB8) at original high VAs not present in this build.
//
//  (QueryPerformanceCounter() - startCounter) / frequency, using a lazy
//  one-time capture of the start counter and the frequency.
// ============================================================
static double GetEngineTime(void)
{
    static LARGE_INTEGER startCounter;
    static double startFreq = 0.0;
    LARGE_INTEGER now;

    if (startFreq == 0.0)
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        startFreq = (double)f.QuadPart;
        QueryPerformanceCounter(&startCounter);
    }
    QueryPerformanceCounter(&now);
    return (double)(unsigned __int64)(now.QuadPart - startCounter.QuadPart) / startFreq;
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
//  [STUB]  sub_18F82E0  (0x18F82E0 - 0x18F852D)  -- create / restore the
//  worker window (the "window state 1" step of PumpWorker)
//
//  __thiscall (worker in ECX). Ensures the worker window is up (sub_18F7AE0
//  creates it into worker->hwnd, +568), restores/resizes it (sub_18F28C0 /
//  sub_18F2B30 with the cached size at +576/+580/+584, window style via
//  SetWindowLongA GWLP_WNDPROC/GWLP_HINSTANCE + SetWindowPos), and on failure
//  shows an error MessageBoxW (manager GetManager window, 0x10) and posts
//  WM_CLOSE (0x10) to the window. Then sub_18F7E60(visible), ShowWindow(hwnd,
//  nShowCmd), UpdateWindow, and finally invokes the worker's +600 callback
//  (onCreateDevice) and sets the visible/ready bytes at +648/+649.
// ============================================================
static void sub_18F82E0(CWorkerWin32 *obj) { (void)obj; }

// ============================================================
//  [STUB]  sub_18F8530  (0x18F8530 - 0x18F85FB)  -- drain the worker's
//  pending message queue (the "message state 2" step of PumpWorker)
//
//  Nonstandard caller convention: the worker pointer arrives in EDI (the
//  decompiler recovers it as a1@<edi>). Loops while PeekMessageA(&msg,
//  worker->hwnd (+568), 0, 0, PM_REMOVE) is true: leaves the CS, runs the
//  +624 message-filter callback on the message (if it returns 0 or is
//  absent), TranslateMessage + DispatchMessageA, and if worker->+641 is
//  set, re-enters the CS and calls sub_18F8060 (the window resize handler)
//  under it. Runs with the worker CS already held by PumpWorker.
// ============================================================
static void sub_18F8530(CWorkerWin32 *obj) { (void)obj; }

// ============================================================
//  [STUB]  sub_18F17B0  (0x18F17B0 - 0x18F1804)  -- shut the device down
//  (the "shutdown state 3" step of PumpWorker)
//
//  __thiscall (worker in ECX). If byte_2075DDB (device-active flag) is set:
//  sub_17D6510(worker) (device teardown), sub_17D1E60(), clear the flag.
//  Then dword_1E2ED70 = dword_1E2ED6C = -1, sub_18F0E00(), sub_18F0C10(),
//  sub_18F1760(), and if the device object dword_1E2EEF8 is set, invokes its
//  second virtual method (vtable[1]) and clears it.
// ============================================================
static void sub_18F17B0(CWorkerWin32 *obj) { (void)obj; }

// ============================================================
//  PumpWorker  (0x18F8600 - 0x18F86A9)  -- one step of the worker's state
//  machine, called on every iteration of RunGame's main pump loop
//  [NOT byte-identical] placement-dependent (relative calls to sub_18F82E0 /
//  sub_18F8530 / sub_18F17B0) and refs the EnterCriticalSection /
//  LeaveCriticalSection / SetEvent / WaitForSingleObjectEx / WaitMessage IAT
//  slots at their original high VAs.
//
//  __thiscall-style (worker in ECX, no stack args). Reads the pump state
//  word at +556 (workerState) under the worker's critical section (+4):
//    state 1  create/restore the worker window (sub_18F82E0), set the state
//             to 2, signal event1 (+156), leave the CS, block on
//             WaitForSingleObjectEx(event2, INFINITE, false), re-enter the CS
//    state 2  leave the CS, WaitMessage() (sleep until a message arrives),
//             re-enter the CS, drain the message queue (sub_18F8530), leave
//    state 3  shut the device down (sub_18F17B0), reset the state to 0, leave
//    state 0  leave the CS (idle no-op)
// ============================================================
void PumpWorker(CWorkerWin32 *obj)
{
    LPCRITICAL_SECTION cs = (LPCRITICAL_SECTION)&obj->cs;

    EnterCriticalSection(cs);
    switch (obj->workerState)
    {
    case 1:
        sub_18F82E0(obj);
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
        sub_18F8530(obj);
        LeaveCriticalSection(cs);
        return;
    case 3:
        sub_18F17B0(obj);
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
