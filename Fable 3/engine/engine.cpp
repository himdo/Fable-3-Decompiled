// Fable 3 — engine module (game engine init + main run loop).
//
// RunGame is the main game function. It:
//   - allocates the 664-byte engine object (LHAlloc(664))
//   - constructs the 664-byte main worker object (CWorkerWin32Ctor)
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
#include <string.h>

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
    int              _pad5;         // +556
    int              hInstance;     // +560
    int              nShowCmd;      // +564
    char             _pad6[88];     // +568 .. +656
    double           timing;        // +656 .. +664
} CWorkerWin32;

// Compile-time check: the object must be exactly 664 bytes (LHAlloc(664)).
typedef char CWorkerWin32SizeCheck[(sizeof(CWorkerWin32) == 664) ? 1 : -1];

static DWORD g_Addend = 0;   // 0x2075C48: worker reference count

typedef void *(*CWorkerWin32_DtorFn)(void *, int);

// ============================================================
//  CWorkerWin32_DtorBody  (0x55D930 - 0x55D9B3)  -- the worker destructor body
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
//  void *CWorkerWin32_Dtor(void *obj, int freeFlag)
//
//  Vtable slot 0 — the one DisposeWorker (sub_615880) dispatches to as
//  vtable[0](worker, 1). Runs the destructor body, then frees the object via
//  the heap if the flag is set. Returns this.
// ============================================================
static void *CWorkerWin32_Dtor(void *obj, int freeFlag)
{
    CWorkerWin32_DtorBody((CWorkerWin32 *)obj);
    if (freeFlag & 1)
        LHFree(obj);
    return obj;
}

// NSystem::CWorkerWin32 vtable — slot 0 is the deleting destructor (the one
// the dispose handler calls); the remaining slots are not implemented.
static void *g_CWorkerWin32Vtable[8] = { (void *)CWorkerWin32_Dtor, 0, 0, 0, 0, 0, 0, 0 };

// ============================================================
//  GetEngineTime  (0x18A88E0)  -- seconds since engine start
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
//
//  void *CWorkerWin32Ctor(void *obj, int hInstance, int nShowCmd)
//
//  Zeroes the 664-byte object, sets the vtable, initializes the critical
//  section, creates four auto-reset events, stores hInstance/nShowCmd, seeds
//  the timing field, and bumps the worker refcount. Returns this.
// ============================================================
static void *CWorkerWin32Ctor(void *obj, int hInstance, int nShowCmd)
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
//  DisposeWorker  (0x00615880 - 0x00615891)  -- SEH handler: dispose the worker
//
//  Invoked by the SEH block when its refcount reaches zero. It calls the
//  worker's first virtual method (vtable[0]) with argument 1:
//      if (worker != 0) return worker->vtable[0](worker, 1);
//  `worker` is the game handle (the CWorkerWin32 object) passed as the arg.
// ============================================================
static int DisposeWorker(int worker)
{
    CWorkerWin32 *w = (CWorkerWin32 *)worker;
    if (w != 0)
    {
        CWorkerWin32_DtorFn fn = (CWorkerWin32_DtorFn)((void **)w->vtable)[0];
        return (int)fn((void *)w, 1);
    }
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

    // 3. Register the SEH handler (with the address of the worker pointer).
    sub_18F6FC0((int *)&v9);

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
