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

// The 2-word "current worker" registration (dword_20FC8F0 / dword_20FC8F4):
//   +0  worker    the current CWorkerWin32 (or 0)
//   +4  sehBlock  the F3SehBlock that disposes the worker when released (or 0)
typedef struct F3WorkerReg
{
    void *worker;
    void *sehBlock;
} F3WorkerReg;

// dword_20FC8F0 / dword_20FC8F4: the current worker + its SEH cleanup block.
static F3WorkerReg g_WorkerPair;

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
        pop    edi
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
//  [BYTE-IDENTICAL]  DisposeWorker  (0x00615880 - 0x00615891)  -- SEH handler
//
//  Invoked by the SEH block when its refcount reaches zero. It calls the
//  worker's first virtual method (vtable[0]) with argument 1:
//      if (worker != 0) return worker->vtable[0](worker, 1);
//  Emits the original 17 bytes exactly (placement-independent: no absolute
//  refs, and the vtable call is indirect via EDX).
// ============================================================
static int __declspec(naked) DisposeWorker(int)
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
//  void ReleaseWorkerPair(void *pair)  (pair = &F3WorkerReg)
//
//  Decrement the SEH block's refcount; when it reaches zero invoke
//  handler(arg) and free the block via the game heap. Then clear both words.
// ============================================================
static void ReleaseWorkerPair(void *pair)
{
    F3WorkerReg *p = (F3WorkerReg *)pair;
    F3SehBlock *sb = (F3SehBlock *)p->sehBlock;

    if (sb != 0 && InterlockedDecrement(&sb->refcount) == 0)
    {
        sb->handler(sb->arg);
        LHFree(sb);
    }
    p->worker = 0;
    p->sehBlock = 0;
}

// ============================================================
//  SetWorker  (0x18F6FC0)  -- install the current worker + SEH cleanup block
//  [NOT byte-identical] placement-dependent (relative calls to
//  ReleaseWorkerPair and InterlockedIncrement).
//
//  void SetWorker(F3WorkerReg *reg)
//
//  If reg->sehBlock already matches the current global, no-op. Otherwise
//  release the current pair (dropping the old SEH block's reference), store
//  (reg->worker, reg->sehBlock) in the global pair, and take a reference on
//  the new SEH block so it outlives RunGame.
// ============================================================
static void SetWorker(F3WorkerReg *reg)
{
    if (g_WorkerPair.sehBlock == reg->sehBlock)
        return;

    ReleaseWorkerPair(&g_WorkerPair);
    g_WorkerPair.worker = reg->worker;
    g_WorkerPair.sehBlock = reg->sehBlock;
    if (reg->sehBlock != 0)
        InterlockedIncrement(&((F3SehBlock *)reg->sehBlock)->refcount);
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
//  GetWorker  (0x18F6A40 - 0x18F6A46)  -- return the current worker
//  [NOT byte-identical] placement-independent but references the high-VA
//  global dword_20FC8F0 (0x020FC8F0) not present in this build.
//
//  A tiny getter (mov eax, dword_20FC8F0; retn) used pervasively by the
//  engine to reach the current CWorkerWin32.
// ============================================================
void *GetWorker(void)
{
    return g_WorkerPair.worker;
}

// ============================================================
//  sub_18F6CA0 / sub_18F6C50  -- register a worker-thread callback
//  [STUB] __thiscall on the current worker (worker in ECX, callback on stack).
// ============================================================
static int sub_18F6CA0(void *worker, F3CallbackFn cb) { (void)worker; (void)cb; return 0; }
static int sub_18F6C50(void *worker, F3CallbackFn cb) { (void)worker; (void)cb; return 0; }

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
    F3WorkerReg reg;
    reg.worker = v9;
    reg.sehBlock = lpAddend;
    SetWorker(&reg);

    // 4. Release the local reference; on zero invoke the handler and free it.
    //    (SetWorker took a reference, so this drops 2->1, leaving the global
    //    pair as the owner of the worker.)
    if (lpAddend != 0 && InterlockedDecrement(&lpAddend->refcount) == 0)
    {
        lpAddend->handler(lpAddend->arg);
        sub_1A032C0(lpAddend);
    }

    // 5. Register the worker-thread callbacks (on the current worker).
    sub_18F6CA0(GetWorker(), sub_6152F0);
    sub_18F6C50(GetWorker(), sub_615340);

    // 6. Spawn the game thread (0x40000-byte stack).
    HANDLE hThread = (HANDLE)_beginthread(sub_615380, 0x40000u, lpCmdLine);

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
