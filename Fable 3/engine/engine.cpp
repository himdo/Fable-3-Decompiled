// Fable 3 — engine module (game engine init + main run loop).
//
// RunGame is the main game function. It:
//   - allocates the 664-byte engine object (LHAlloc(664))
//   - constructs the 664-byte main worker object (CWorkerWin32Ctor)
//   - builds a 12-byte SEH handler block (refcount / handler / arg)
//   - registers the handler (sub_18F6FC0) then releases the local reference
//   - registers two worker-thread callbacks (OnCreateDevice, OnResetDevice)
//   - spawns the game thread (_beginthread(sub_615380, 0x40000, lpCmdLine))
//   - pumps (sub_18F6A40 / sub_18F8600) while the thread is running
//   - returns the global dword_1DBD00C as the process exit code
//
// The rest of the engine lives in focused modules alongside this file:
//   worker.h / worker.cpp         CWorkerWin32 ctor + dtor + current-worker
//                                 registry (GetWorker / SetWorker / ...)
//   manager.h / manager.cpp       CManagerWin32 dtor + current-manager
//                                 registry (GetManager / SetManager / ...)
//   object_registry.h/.cpp        SEH cleanup blocks + global object pairs
//                                 (ReleaseObjectPair / SetDebugTrace)
//   xlo_device.h / xlo_device.cpp XLO on-create / on-reset-device callbacks
//   gameengine.h / gameengine.cpp CGameEngine (356-byte) ctor + sub-objects
//   gamethread.h / gamethread.cpp GameThread (0x615380) + its stub callees
//   thread.cpp                    SetThreadName (0x18C5840)

#include "engine.h"
#include "xlo_device.h"
#include "common/common.h"

#include <windows.h>
#include <process.h>
#include <string.h>

// 0x1DBD00C — the process exit code, returned by the main run loop.
// (Set by GameThread, see gamethread.cpp.)
DWORD g_dword_1DBD00C = 0;

// ============================================================
//  sub_18F8600  (0x18F8600)  -- per-frame engine pump
//  [STUB]
// ============================================================
static void sub_18F8600(int a1)
{
    (void)a1;
}

// ============================================================
//  RunGame  (0x00615740 - 0x006157F9)  -- engine init + main run loop
//  [NOT byte-identical] placement-dependent (12 relative calls to engine callees).
// ============================================================
int RunGame(int hInstance, char *lpCmdLine, int nShowCmd)
{
    void       *v9;         // the worker object (game handle)
    F3SehBlock *lpAddend;   // the SEH handler block (or 0)

    // 1. Allocate the 664-byte worker object and construct it in place.
    void *block = LHAlloc(664);
    v9 = (block != 0) ? CWorkerWin32Ctor(block, hInstance, nShowCmd) : 0;

    // 2. Build the 12-byte SEH handler block (refcount=1, handler, arg).
    if (v9 != 0 && (lpAddend = (F3SehBlock *)LHAlloc(12)) != 0)
    {
        lpAddend->refcount = 1;
        lpAddend->handler  = DisposeWorker;
        lpAddend->arg      = (int)v9;
    }
    else
    {
        lpAddend = 0;
    }

    // 3. Install the current worker + SEH cleanup block (sets the global pair
    //    and takes a reference so the worker outlives this call).
    F3ObjReg reg;
    reg.object = v9;
    reg.sehBlock = lpAddend;
    SetWorker(&reg);

    // 4. Release the local reference; on zero invoke the handler and free it.
    //    (SetWorker took a reference, so this drops 2->1, leaving the global
    //    pair as the owner of the worker.)
    if (lpAddend != 0 && InterlockedDecrement(&lpAddend->refcount) == 0)
    {
        lpAddend->handler(lpAddend->arg);
        LHFree(lpAddend);
    }

    // 5. Register the worker-thread callbacks (on the current worker).
    //    Both setters are __thiscall + retn 4 hybrids, so dispatch them
    //    through a __thiscall function pointer (the callee cleans the arg).
    ((F3ThiscallSetFn)SetOnCreateDevice)(GetWorker(), OnCreateDevice);
    ((F3ThiscallSetFn)SetOnResetDevice)(GetWorker(), OnResetDevice);

    // 6. Spawn the game thread (0x40000-byte stack).
    HANDLE hThread = (HANDLE)_beginthread(GameThread, 0x40000u, lpCmdLine);

    // 7. Pump the engine while the game thread is still running
    //    (WaitForSingleObject returns WAIT_TIMEOUT = 258 while it is alive).
    do
    {
        int state = (int)GetWorker();
        sub_18F8600(state);
    }
    while (WaitForSingleObject(hThread, 0) == 258);

    // 8. Return the global process exit code.
    return g_dword_1DBD00C;
}
