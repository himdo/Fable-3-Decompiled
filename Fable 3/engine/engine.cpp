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

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // original used _snwprintf
#endif

#include "engine.h"
#include "common/common.h"
#include "save/save.h"
#include "framework/framework.h"

#include <windows.h>
#include <process.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>

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

// The 2-word "current object + SEH block" registration. Used for the worker
// pair (dword_20FC8F0 / dword_20FC8F4) and the manager pair
// (dword_20FC8C4 / dword_20FC8C8):
//   +0  object    the current CWorkerWin32 / CManagerWin32 (or 0)
//   +4  sehBlock  the F3SehBlock that disposes the object when released (or 0)
typedef struct F3ObjReg
{
    void *object;
    void *sehBlock;
} F3ObjReg;

// dword_20FC8F0 / dword_20FC8F4: the current worker + its SEH cleanup block.
static F3ObjReg g_WorkerPair;
// dword_20FC8C4 / dword_20FC8C8: the current manager + its SEH cleanup block.
static F3ObjReg g_ManagerPair;
// dword_20B600C / dword_20B6010: the CDebugTraceFable object + its SEH block.
static F3ObjReg g_DebugTracePair;

// stru_20B6014: the critical section shared by the "register / release object"
// helpers (sub_1A02DD0, sub_1A02DA0, SetDebugTrace/sub_1A02E20, sub_1A030A0, ...) that all
// lock the same mutex before touching their global object pair.
static CRITICAL_SECTION g_ObjectRegistryCS;
struct ObjectRegistryCsInit
{
    ObjectRegistryCsInit()  { InitializeCriticalSection(&g_ObjectRegistryCS); }
    ~ObjectRegistryCsInit() { DeleteCriticalSection(&g_ObjectRegistryCS); }
};
static ObjectRegistryCsInit g_ObjectRegistryCsInit;

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
static void SetWorker(F3ObjReg *reg)
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
    return g_WorkerPair.object;
}

// ============================================================
//  ReleaseManagerPair  (0x0055D7C0 - 0x0055D818)  -- release the manager pair
//  [NOT byte-identical] placement-dependent (relative call to the heap free).
//
//  long ReleaseManagerPair(void *pair)  (pair = &F3ObjReg)
//
//  The manager-pair analog of ReleaseWorkerPair (sub_55DB10). Decrement the
//  SEH block's refcount; when it reaches zero invoke handler(arg) and free
//  the block via the game heap. Then clear both words. Returns the release
//  result (the refcount, or the block pointer when there is no SEH block).
// ============================================================
static long ReleaseManagerPair(void *pair)
{
    F3ObjReg *p = (F3ObjReg *)pair;
    F3SehBlock *sb = (F3SehBlock *)p->sehBlock;
    long result = (long)sb;
    if (sb != 0)
    {
        result = InterlockedDecrement(&sb->refcount);
        if (result == 0)
        {
            sb->handler(sb->arg);
            LHFree(sb);
        }
    }
    p->object = 0;
    p->sehBlock = 0;
    return result;
}

// ============================================================
//  SetManager  (0x18F87E0 - 0x18F8817)  -- install the current manager + SEH
//  [NOT byte-identical] placement-dependent (relative calls to
//  ReleaseManagerPair and InterlockedIncrement).
//
//  long SetManager(F3ObjReg *reg)
//
//  The manager-pair analog of SetWorker (sub_18F6FC0). Registers the 48-byte
//  CManagerWin32 object (reg->object) + its SEH block (reg->sehBlock) into the
//  global manager pair (dword_20FC8C4/8C8). If reg->sehBlock already matches
//  the current global, no-op. Otherwise release the current pair, store the
//  new pair, and take a reference on the new SEH block. Returns the SEH
//  block's new refcount, or reg on no-op / no block.
// ============================================================
static long SetManager(F3ObjReg *reg)
{
    long result = (long)reg;
    if (g_ManagerPair.sehBlock == reg->sehBlock)
        return result;
    result = ReleaseManagerPair(&g_ManagerPair);
    g_ManagerPair.object   = reg->object;
    g_ManagerPair.sehBlock = reg->sehBlock;
    if (reg->sehBlock != 0)
        result = InterlockedIncrement(&((F3SehBlock *)reg->sehBlock)->refcount);
    return result;
}

// ============================================================
//  GetManager  (0x18F8750 - 0x18F8756)  -- return the current manager
//  [NOT byte-identical] placement-independent but references the high-VA
//  global dword_20FC8C4 (0x020FC8C4) not present in this build.
//
//  A tiny getter (mov eax, dword_20FC8C4; retn) used by the engine to reach
//  the current CManagerWin32.
// ============================================================
void *GetManager(void)
{
    return g_ManagerPair.object;
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
//  GameThread  (0x00615380 - 0x00615740)  -- the game thread entry point
//  [NOT byte-identical] placement-dependent (many relative calls to engine
//  callees and Win32/CRT functions).
//
//  void GameThread(void *a1)
//
//  Called by _beginthread (RunGame) with a 0x40000-byte stack. It:
//    1. Names the thread "GameThread", sets heap info (class 1), FrameworkInit.
//    2. Creates a debug handler, a 48-byte object, and a CDebugTraceFable, each
//       with an SEH cleanup block. The debug handler's SEH block is kept
//       (released at the end by sub_401700); the other two release their local
//       reference immediately (leaving the global as the owner).
//    3. Clears the game buffer and loads a shared-memory blob named after the
//       exe (-=[SMS_<exename>_SMS]=-). If the blob is missing, shows a message
//       box and ExitProcess(1).
//    4. Runs the game (sub_61C530, sub_61CB70) and sets the exit code
//       (dword_1DBD00C), then cleans up (sub_61C670, sub_401700).
// ============================================================

// 0x1DBD008: 1 when the shared-memory blob was loaded, 0 otherwise.
static BYTE  g_byte_1DBD008 = 0;
// 0x1DBD010 / 0x1DBD014: objects whose strings form the error message box
// (body / title) shown when the shared-memory blob is missing.
static DWORD g_dword_1DBD010 = 0;
static DWORD g_dword_1DBD014 = 0;

// Placeholder vtables for the two objects created on the game thread.
static void *g_CFableWindowsDebugHandlerVtable[4] = { 0 };
static void *g_CDebugTraceFableVtable[4]          = { 0 };

// ---- SEH handlers for the three objects (called when their refcount hits 0) ----
// sub_401D20 (0x401D20): tail call to the heap free -- frees the debug handler.
static int sub_401D20(int arg) { LHFree((void *)arg); return 0; }
// ============================================================
//  [BYTE-IDENTICAL]  DisposeManager  (0x00615850 - 0x00615860)  -- SEH handler
//
//  Invoked by the SEH block (for the 48-byte NSystem::CManagerWin32 object
//  created by sub_18F7160) when its refcount reaches zero. It calls the
//  object's first virtual method (vtable[0], the scalar-deleting destructor)
//  with argument 1:
//      if (obj != 0) return obj->vtable[0](obj, 1);
//  Byte-identical to DisposeWorker (0x615880); only the target object differs.
//  Emits the original 17 bytes exactly (placement-independent: no absolute
//  refs, and the vtable call is indirect via EDX).
// ============================================================
static int __declspec(naked) DisposeManager(int)
{
    __asm {
        mov    ecx, [esp+4]
        test   ecx, ecx
        jz     short f3_dm_done
        mov    eax, [ecx]
        mov    edx, [eax]
        push   1
        call   edx
    f3_dm_done:
        retn
    }
}
// sub_615870 (0x615870): thunk to LHFree -- frees the CDebugTraceFable.
static int sub_615870(int arg) { LHFree((void *)arg); return 0; }

// ---- other engine callees (stubs for now) -----------------------------------
static void  sub_18C5840(const char *name) { (void)name; }              // set thread name
static void  sub_1A02DD0(F3ObjReg *reg) { (void)reg; }               // register debug handler
// ============================================================
//  CManagerWin32_DtorBody  (0x55D5F0 - 0x55D63E)  -- the destructor body
//  [NOT byte-identical] placement-dependent (relative call to the heap free).
//
//  Releases the refcounted block at this+36 (decrementing its refcount at +0;
//  on zero calls the block's destructor (block+4) with arg (block+8) and frees
//  the block via the heap), clears this+32/this+36, deletes the critical
//  section at this+8, and resets the vtable to the base-class vftable.
// ============================================================
static void CManagerWin32_DtorBody(void *obj)
{
    char *base = (char *)obj;

    void *block = *(void **)(base + 36);
    if (block != 0 && InterlockedDecrement((volatile long *)block) == 0)
    {
        void  (*destructor)(int) = *(void (**)(int))((char *)block + 4);
        int   arg               = *(int *)((char *)block + 8);
        destructor(arg);
        LHFree(block);
    }
    *(int *)(base + 32) = 0;
    *(int *)(base + 36) = 0;
    DeleteCriticalSection((LPCRITICAL_SECTION)(base + 8));
    *(void **)base = 0;   // reset vtable (base-class vftable in the original)
}

// ============================================================
//  CManagerWin32_Dtor  (0x55D8B0 - 0x55D8D6)  -- the deleting destructor
//
//  Functional (not byte-identical; it has relative calls). __thiscall (obj in
//  ECX) and cleans up its own flag arg (retn 4), matching the original
//  sub_55D8B0's convention so the byte-identical DisposeManager can dispatch
//  through it. Runs the dtor body, then frees via the heap if the flag is set.
// ============================================================
static __declspec(naked) void *CManagerWin32_Dtor(void)
{
    __asm {
        push   edi
        mov    edi, ecx            ; edi = obj
        push   ecx                 ; pass obj on the stack to the body
        call   CManagerWin32_DtorBody
        add    esp, 4
        test   [esp+8], 1          ; the flag arg
        jz     short f3_cmd_nofree
        push   edi
        call   LHFree
        add    esp, 4
    f3_cmd_nofree:
        mov    eax, edi            ; return obj
        pop    edi
        retn   4                   ; clean up the flag arg
    }
}

// NSystem::CManagerWin32 (48 bytes): vtable at +0, CRITICAL_SECTION at +8,
// refcounted block at +36. Slot 0 is the deleting destructor.
static void *g_CManagerWin32Vtable[16] = { (void *)CManagerWin32_Dtor };
// sub_18F7160 (0x18F7160): NSystem::CManagerWin32 constructor.
static void *sub_18F7160(void *obj)
{
    char *p = (char *)obj;
    *(void **)p = g_CManagerWin32Vtable;   // vtable (base set first, then win32)
    InitializeCriticalSection((CRITICAL_SECTION *)(p + 8));
    *(int *)(p + 32) = 0;
    *(int *)(p + 36) = 0;
    p[40] = 0;
    p[41] = 0;
    p[44] = 0;
    return p;
}
// sub_1A00C30 (0x1A00C30): zero the save-path slot + bump the worker refcount.
static void *sub_1A00C30(void *obj)
{
    *(void **)obj = 0;                 // this[0] = 0 (clear the save-path slot)
    InterlockedIncrement(&g_Addend);   // &Addend (worker refcount)
    return obj;
}
// ============================================================
//  ReleaseObjectPair  (0x00403220 - 0x00403262)  -- release a global object pair
//  [NOT byte-identical] placement-dependent (relative call to the heap free).
//
//  void ReleaseObjectPair(void *pair)  (pair = &F3ObjReg)
//
//  General-purpose release used by the critical-section-protected "register
//  object" helpers. Decrement the SEH block's refcount; when it reaches zero
//  invoke handler(arg) and free the block via the game heap. Then clear both
//  words. Structurally identical to ReleaseWorkerPair / ReleaseManagerPair.
// ============================================================
static void ReleaseObjectPair(void *pair)
{
    F3ObjReg *p    = (F3ObjReg *)pair;
    F3SehBlock *sb = (F3SehBlock *)p->sehBlock;

    if (sb != 0 && InterlockedDecrement(&sb->refcount) == 0)
    {
        sb->handler(sb->arg);
        LHFree(sb);
    }
    p->object   = 0;
    p->sehBlock = 0;
}

// ============================================================
//  SetDebugTrace  (0x1A02E20 - 0x1A02E6C)  -- register the CDebugTraceFable
//  [NOT byte-identical] placement-dependent (relative calls to
//  ReleaseObjectPair, InterlockedIncrement and the critical-section helpers).
//
//  void SetDebugTrace(F3ObjReg *reg)
//
//  Critical-section-protected setter for the CDebugTraceFable global pair. If
//  the incoming SEH block differs from the current one, release the old pair,
//  store the new (object, SEH block), and take a reference on the new SEH block
//  so the global pair owns it. (Sole caller: GameThread.)
// ============================================================
static void SetDebugTrace(F3ObjReg *reg)
{
    EnterCriticalSection(&g_ObjectRegistryCS);
    if (g_DebugTracePair.sehBlock != reg->sehBlock)
    {
        ReleaseObjectPair(&g_DebugTracePair);
        g_DebugTracePair.object   = reg->object;
        g_DebugTracePair.sehBlock = reg->sehBlock;
        if (reg->sehBlock != 0)
            InterlockedIncrement(&((F3SehBlock *)reg->sehBlock)->refcount);
    }
    LeaveCriticalSection(&g_ObjectRegistryCS);
}
// The game buffer (0x6C4 bytes) that the shared-memory blob is loaded into.
static char g_gameBuffer[0x6C4];
// ============================================================
//  HeapStressTest  (0x18986E0 - 0x189888C)  -- engine heap sanity check
//  [NOT byte-identical] placement-dependent (relative call to LHAlloc,
//  indirect heap frees, /GS stack cookie, 0x3E8-iteration loops).
//
//  void *HeapStressTest(void)
//
//  The engine's heap self-test: allocate 1000 (0x3E8) 16-byte blocks and free
//  them all again, stressing the allocator. The original builds an intrusive
//  doubly-linked list over the blocks (list head embedded at offset 4 of each
//  block, first block held in the global dword_20B5FC8) and tears it down by
//  removing the head and tail block each iteration, freeing the head before the
//  tail -- which yields the exact allocation/free sequence reproduced here.
//  The return value is discarded by the sole caller (GameThread).
// ============================================================
static void *HeapStressTest(void)
{
    static void *g_head = 0;        // dword_20B5FC8: first allocated block
    void        *blocks[0x3E8];     // the 1000 blocks (the list is internal)

    for (int i = 0; i < 0x3E8; i++)
        blocks[i] = LHAlloc(16);
    g_head = blocks[0];

    // Remove head + tail each iteration (head freed before tail): the free
    // order is N1, N1000, N2, N999, ..., N500, N501.
    for (int k = 1; k <= 0x1F4; k++)   // 0x1F4 = 500 iterations
    {
        LHFree(blocks[k - 1]);         // head block (Nk)
        LHFree(blocks[0x3E8 - k]);     // tail block (N(1001-k))
    }
    return g_head;
}
static void *sub_619AA0(void) { return g_gameBuffer; }                  // get the game buffer
static const wchar_t *sub_19FF570(void *obj) { (void)obj; return L""; } // get a string
static int   *sub_6195C0(int a1) { (void)a1; return 0; }                // get an object
static void *sub_61C530(void *obj) { return obj; }                      // init the game (ArgList)
static int   sub_61CB70(void *argList, int a2) { (void)argList; (void)a2; return 1; }  // run the game (success)
static void  sub_61BE00(void *obj) { (void)obj; }                       // cleanup (part 1)
static void  sub_61AE70(void *obj) { (void)obj; }                       // cleanup (part 2)
static void  sub_61C670(void *obj) { (void)obj; }                       // cleanup (part 3)
static long  sub_401700(F3ObjReg *reg) { (void)reg; return 0; }      // free the debug handler

// Release a locally-held SEH reference; on zero invoke the handler and free.
static void F3ReleaseSehRef(void *sehBlock)
{
    if (sehBlock && InterlockedDecrement(&((F3SehBlock *)sehBlock)->refcount) == 0)
    {
        F3SehBlock *sb = (F3SehBlock *)sehBlock;
        sb->handler(sb->arg);
        LHFree(sb);
    }
}

void GameThread(void *a1)
{
    int arg = (int)a1;

    // 1. Thread setup + framework init.
    HeapSetInformation(NULL, (HEAP_INFORMATION_CLASS)1, NULL, 0);
    sub_18C5840("GameThread");   // set the thread name
    FrameworkInit();

    // 2. Debug handler (4-byte) + SEH block (sub_401D20). The SEH block is
    //    kept (released at the end by sub_401700), not released here.
    F3ObjReg reg1;
    reg1.object = LHAlloc(4);
    if (reg1.object)
        *(void **)reg1.object = g_CFableWindowsDebugHandlerVtable;
    reg1.sehBlock = 0;
    if (reg1.object && (reg1.sehBlock = LHAlloc(12)) != 0)
    {
        F3SehBlock *sb = (F3SehBlock *)reg1.sehBlock;
        sb->refcount = 1;
        sb->handler  = sub_401D20;
        sb->arg      = (int)reg1.object;
    }
    sub_1A02DD0(&reg1);

    // 3. 48-byte object (sub_18F7160) + SEH block (DisposeManager); release local.
    F3ObjReg reg2;
    reg2.object = LHAlloc(48);
    reg2.object = reg2.object ? sub_18F7160(reg2.object) : 0;
    reg2.sehBlock = 0;
    if (reg2.object && (reg2.sehBlock = LHAlloc(12)) != 0)
    {
        F3SehBlock *sb = (F3SehBlock *)reg2.sehBlock;
        sb->refcount = 1;
        sb->handler  = DisposeManager;
        sb->arg      = (int)reg2.object;
    }
    SetManager(&reg2);
    F3ReleaseSehRef(reg2.sehBlock);

    // 4. CDebugTraceFable (8-byte) + SEH block (sub_615870); release local.
    F3ObjReg reg3;
    reg3.object = LHAlloc(8);
    if (reg3.object)
    {
        *(void **)reg3.object = g_CDebugTraceFableVtable;
        sub_1A00C30((void *)((char *)reg3.object + 4));
        SetSaveGamePath((F3SharedBuffer **)(char *)reg3.object + 1, "FTRACE");
    }
    reg3.sehBlock = 0;
    if (reg3.object && (reg3.sehBlock = LHAlloc(12)) != 0)
    {
        F3SehBlock *sb = (F3SehBlock *)reg3.sehBlock;
        sb->refcount = 1;
        sb->handler  = sub_615870;
        sb->arg      = (int)reg3.object;
    }
    SetDebugTrace(&reg3);
    F3ReleaseSehRef(reg3.sehBlock);

    // 5. Heap sanity check + clear the game buffer.
    HeapStressTest();
    if (sub_619AA0() != 0)
        memset(sub_619AA0(), 0, 0x6C4);

    // 6. Load the shared-memory blob (named after the exe).
    WCHAR Filename[0x800];
    WCHAR Buffer[0x800];
    memset(Filename, 0, sizeof(Filename));
    memset(Buffer, 0, sizeof(Buffer));
    g_byte_1DBD008 = 0;
    if (sub_619AA0() != 0)
    {
        HMODULE hMod = GetModuleHandleA(NULL);
        if (GetModuleFileNameW(hMod, Filename, 0x800) != 0)
        {
            const WCHAR *p = wcsrchr(Filename, L'\\');
            if (p == 0)
                p = wcsrchr(Filename, L'/');
            if (p != 0)
            {
                _snwprintf(Buffer, 0x800, L"-=[SMS_%s_SMS]=-", p + 1);
                HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, Buffer);
                if (hMap != 0)
                {
                    BYTE *view = (BYTE *)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
                    if (view != 0)
                    {
                        if (sub_619AA0() != 0)
                            memcpy(sub_619AA0(), view, 0x6C4);
                        if (UnmapViewOfFile(view))
                            g_byte_1DBD008 = 1;
                    }
                    CloseHandle(hMap);
                }
            }
        }
    }

    // 7. If the blob was not loaded, show a message box and exit.
    if (g_byte_1DBD008 == 0)
    {
        int v19 = (int)GetManager();
        const WCHAR *title = (const WCHAR *)sub_19FF570(sub_6195C0(g_dword_1DBD010));
        const WCHAR *body  = (const WCHAR *)sub_19FF570(sub_6195C0(g_dword_1DBD014));
        HWND hwnd = 0;
        if (v19)
        {
            void **vtable = (void **)*(void ***)v19;
            int (__thiscall *getWindow)(int) =
                (int (__thiscall *)(int))vtable[8];   // vtable slot at offset 32
            hwnd = (HWND)getWindow(v19);
        }
        MessageBoxW(hwnd, body, title, MB_ICONERROR);   // 0x10
        ExitProcess(1);
    }

    // 8. Run the game.
    char ArgList[356];
    sub_61C530(ArgList);
    if (sub_61CB70(ArgList, arg) != 0)
    {
        sub_61BE00(ArgList);
        sub_61AE70(ArgList);
        g_dword_1DBD00C = 0;
    }
    else
    {
        g_dword_1DBD00C = 1;
    }
    sub_61C670(ArgList);

    // 9. Free the debug handler.
    sub_401700(&reg1);
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
        sub_1A032C0(lpAddend);
    }

    // 5. Register the worker-thread callbacks (on the current worker).
    sub_18F6CA0(GetWorker(), sub_6152F0);
    sub_18F6C50(GetWorker(), sub_615340);

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
