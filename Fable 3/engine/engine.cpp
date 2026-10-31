// Fable 3 — engine module (game engine init + main run loop).
//
// RunGame is the main game function. It:
//   - allocates the 664-byte engine object (LHAlloc(664))
//   - calls sub_18F7040(hInstance, nShowCmd) to start the real engine
//   - builds a 12-byte SEH handler block (refcount / handler / arg)
//   - registers the handler (sub_18F6FC0) then releases the local reference
//   - registers two worker-thread callbacks (sub_6152F0, sub_615340)
//   - spawns the game thread (_beginthread(sub_615380, 0x40000, lpCmdLine))
//   - pumps (sub_18F6A40 / sub_18F8600) while the thread is running
//   - returns the global dword_1DBD00C as the process exit code
//
// The engine-internal callees below are stubs for now; they will be filled in
// as the engine is decompiled.

#include "engine.h"
#include "common/common.h"

#include <windows.h>
#include <process.h>

// 0x1DBD00C — the process exit code, returned by the main run loop.
static DWORD g_dword_1DBD00C = 0;

// A callback the engine invokes on its worker threads.
typedef int (*F3CallbackFn)(void);

// The 12-byte SEH / exception-handler control block:
//   +0  refcount  reference count (Interlocked)
//   +4  handler   invoked as handler(arg) when the count reaches zero
//   +8  arg       the game handle handed to the handler
typedef struct F3SehBlock
{
    volatile long refcount;
    int (*handler)(int);
    int arg;
} F3SehBlock;

// ============================================================
//  sub_18F7040  (0x18F7040)  -- start the real engine
//  [STUB] Returns a dummy non-null handle (a DWORD in the original) so the
//  downstream setup runs.
// ============================================================
static int sub_18F7040(int hInstance, int nShowCmd)
{
    (void)hInstance;
    (void)nShowCmd;
    return 1;
}

// ============================================================
//  sub_615880  (0x615880)  -- SEH handler / engine dispose
//  [STUB]
// ============================================================
static int sub_615880(int a1)
{
    (void)a1;
    return 0;
}

// ============================================================
//  sub_18F6FC0  (0x18F6FC0)  -- register the SEH handler
//  [STUB]
// ============================================================
static long sub_18F6FC0(int *a1)
{
    (void)a1;
    return 0;
}

// ============================================================
//  sub_1A032C0  (0x1A032C0)  -- free the SEH block (game heap free)
// ============================================================
static int sub_1A032C0(void *block)
{
    LHFree(block);
    return 0;
}

// ============================================================
//  sub_18F6A40  (0x18F6A40)  -- return the engine state word (dword_20FC8F0)
//  [STUB] the global defaults to 0.
// ============================================================
static int sub_18F6A40(void)
{
    return 0;
}

// ============================================================
//  sub_18F6CA0 / sub_18F6C50  -- register a worker-thread callback
//  [STUB]
// ============================================================
static int sub_18F6CA0(F3CallbackFn cb) { (void)cb; return 0; }
static int sub_18F6C50(F3CallbackFn cb) { (void)cb; return 0; }

// ============================================================
//  sub_6152F0 / sub_615340  -- worker-thread callbacks
//  [STUB]
// ============================================================
static int sub_6152F0(void) { return 0; }
static int sub_615340(void) { return 0; }

// ============================================================
//  sub_615380  (0x615380)  -- the game thread entry point
//  [STUB] returns immediately, so the run loop exits once the thread dies.
// ============================================================
static void sub_615380(void *a1)
{
    (void)a1;
}

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
// ============================================================
int RunGame(int hInstance, char *lpCmdLine, int nShowCmd)
{
    int          v9;        // game init result / handle
    F3SehBlock  *lpAddend;  // the SEH handler block (or 0)

    // 1. Allocate the 664-byte engine object and start the real engine.
    if (LHAlloc(664) != 0)
        v9 = sub_18F7040(hInstance, nShowCmd);
    else
        v9 = 0;

    // 2. Build the 12-byte SEH handler block (refcount=1, handler, arg).
    if (v9 != 0 && (lpAddend = (F3SehBlock *)LHAlloc(12)) != 0)
    {
        lpAddend->refcount = 1;
        lpAddend->handler  = sub_615880;
        lpAddend->arg      = v9;
    }
    else
    {
        lpAddend = 0;
    }

    // 3. Register the SEH handler.
    sub_18F6FC0(&v9);

    // 4. Release the local reference; on zero invoke the handler and free it.
    if (lpAddend != 0 && InterlockedDecrement(&lpAddend->refcount) == 0)
    {
        lpAddend->handler(lpAddend->arg);
        sub_1A032C0(lpAddend);
    }

    // 5. Register the worker-thread callbacks.
    sub_18F6A40();
    sub_18F6CA0(sub_6152F0);
    sub_18F6A40();
    sub_18F6C50(sub_615340);

    // 6. Spawn the game thread (0x40000-byte stack).
    HANDLE hThread = (HANDLE)_beginthread(sub_615380, 0x40000u, lpCmdLine);

    // 7. Pump the engine while the game thread is still running
    //    (WaitForSingleObject returns WAIT_TIMEOUT = 258 while it is alive).
    do
    {
        int state = sub_18F6A40();
        sub_18F8600(state);
    }
    while (WaitForSingleObject(hThread, 0) == 258);

    // 8. Return the global process exit code.
    return g_dword_1DBD00C;
}
