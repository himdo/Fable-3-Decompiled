// Fable 3 — CWorkerWin32 module (the 664-byte main worker object + registry).
//
// The main worker (NSystem::CWorkerWin32) is allocated by RunGame
// (LHAlloc(664)), constructed with CWorkerWin32Ctor, published as the current
// worker (g_WorkerPair, dword_20FC8F0/20FC8F4), and disposed through the
// byte-identical DisposeWorker SEH handler, which calls the worker's
// deleting destructor (vtable[0]).
#ifndef F3_WORKER_H
#define F3_WORKER_H

#include <windows.h>
#include "object_registry.h"

// A callback the engine invokes on its worker threads.
typedef int (*F3CallbackFn)(void);

// A __thiscall "set worker callback" method (worker in ECX, callback on the
// stack, callee cleans the arg via ret 4) used to install a worker callback.
typedef F3CallbackFn (__thiscall *F3ThiscallSetFn)(void *, F3CallbackFn);

// ============================================================
//  CWorkerWin32  -- the main worker object (NSystem::CWorkerWin32, 664 bytes)
//
//  Layout (offsets match the original so events / hInstance / nShowCmd / timing
//  sit where the rest of the engine expects them):
//    +0    vtable
//    +4    critical section (24 bytes on x86)
//    +156  event1   +288 event2   +420 event3   +552 event4  (auto-reset)
//    +560  hInstance   +564 nShowCmd
//    +656  timing (double, seconds since engine start)
// ============================================================
typedef struct CWorkerWin32
{
    void *           vtable;        // +0
    CRITICAL_SECTION cs;            // +4  (24 bytes on x86)
    char             _pad1[128];    // +28  .. +156
    HANDLE           event1;        // +156
    char             _pad2[128];    // +160 .. +288
    HANDLE           event2;        // +288
    char             _pad3[128];    // +292 .. +420
    HANDLE           event3;        // +420
    char             _pad4[128];    // +424 .. +552
    HANDLE           event4;        // +552
    int              workerState;   // +556 pump state (0 idle / 1 window / 2 message / 3 shutdown)
    int              hInstance;     // +560
    int              nShowCmd;      // +564
    char             _pad6a[32];    // +568 .. +600
    F3CallbackFn     onCreateDevice; // +600 (0x258) on-create-device callback
    char             _pad6b1[16];   // +604 .. +620
    F3CallbackFn     onResetDevice; // +620 (0x26C) on-reset-device callback
    char             _pad6b2[32];   // +624 .. +656
    double           timing;        // +656 .. +664
} CWorkerWin32;

// Compile-time check: the object must be exactly 664 bytes (LHAlloc(664)).
typedef char CWorkerWin32SizeCheck[(sizeof(CWorkerWin32) == 664) ? 1 : -1];

// CWorkerWin32Ctor (0x18F7040) -- construct the main worker (see worker.cpp).
void *CWorkerWin32Ctor(void *obj, int hInstance, int nShowCmd);

// 0x18F6A40 - return the current worker (dword_20FC8F0). Used pervasively by
// the engine to reach the current CWorkerWin32.
void *GetWorker(void);

// 0x00615880 - the worker's SEH dispose handler [BYTE-IDENTICAL]. Invoked by
// the SEH block when its refcount reaches zero; calls vtable[0](worker, 1).
int DisposeWorker(int arg);

// 0x18F6FC0 - install the current worker + SEH cleanup block.
void SetWorker(F3ObjReg *reg);

// 0x18F6CA0 - set the worker's on-create-device callback [BYTE-IDENTICAL].
// __thiscall + retn 4 hybrid: dispatch through F3ThiscallSetFn.
F3CallbackFn SetOnCreateDevice(void *worker, F3CallbackFn cb);

// 0x18F8600 - one step of the worker's state machine, called on every
// iteration of RunGame's main pump loop. See worker.cpp.
void PumpWorker(CWorkerWin32 *obj);

// 0x18F6C50 - set the worker's on-reset-device callback [BYTE-IDENTICAL].
// __thiscall + retn 4 hybrid: dispatch through F3ThiscallSetFn.
F3CallbackFn SetOnResetDevice(void *worker, F3CallbackFn cb);

#endif  // F3_WORKER_H
