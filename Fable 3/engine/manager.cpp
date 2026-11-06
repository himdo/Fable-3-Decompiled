// Fable 3 — CManagerWin32 module implementation (the 48-byte manager object,
// its deleting destructor, and the current-manager registry).

#include "manager.h"
#include "common/common.h"
#include "worker.h"               // GetWorker / CWorkerWin32_RunGame / FindActiveInputDevice

// dword_20FC8C4 / dword_20FC8C8: the current manager + its SEH cleanup block.
static F3ObjReg g_ManagerPair;

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
        pop   edi
        retn   4                   ; clean up the flag arg
    }
}

// NSystem::CManagerWin32 (48 bytes): vtable at +0, CRITICAL_SECTION at +8,
// the input-manager object + SEH block at +0x20/+0x24, refcounted block at
// +36. Slot 0 is the deleting destructor.
//
// The slots below are the virtual methods the frontend (RunFrontEnd, 0x61CB70)
// and the engine dispatch through. The unimplemented ones stay zero.
//
//  slot 1  (0x04)  CManagerWin32_RunGame (0x18F7D30) -- the big "run the game"
//              start-up dispatch.
//  slot 2  (0x08)  CManagerWin32_StopWorker (0x18F6F80) -- stop the worker
//              (KillTimer 0x1000 + pump state 3); CGameEngineCleanup calls it
//              on shutdown and discards the result.
//  slot 8  (0x20)  CManagerWin32_GetWindow (0x18F6DC0) -- the worker's HWND.
//  slot 12 (0x30)  CManagerWin32_PostMessage (0x18F6CE0) -- PostMessageA
//              (GetWindow, 0x8002, byte, 0), called as (this, 1) on the
//              sign-in paths and (this, 0) from the run dispatch.

// ============================================================
//  CManagerWin32_GetWindow  (0x18F6DC0 - 0x18F6DCC)  -- the manager's HWND
//  [NOT byte-identical] placement-independent but references the high-VA
//  worker-pair global dword_20FC8F0 (0x020FC8F0) not present in this build.
//
//  A tiny getter (mov eax, [dword_20FC8F0 + 0x238]; retn) that returns the
//  current worker's main window handle (worker +0x238). Null-guarded (the
//  original assumes the worker global is live; the standalone test has none).
// ============================================================
static void *CManagerWin32_GetWindow(void *obj)
{
    (void)obj;
    void *w = GetWorker();
    if (w == 0)
        return 0;
    return *(HWND *)((char *)w + 0x238);
}

// ============================================================
//  CManagerWin32_StopWorker  (0x18F6F80 - 0x18F6FB8)  -- stop the worker pump
//  [NOT byte-identical] placement-dependent (relative calls to the
//  EnterCriticalSection / LeaveCriticalSection / KillTimer IAT slots) and refs
//  the high-VA worker-pair global dword_20FC8F0 (0x020FC8F0).
//
//  Kills the worker window's 0x1000 timer and sets the pump state (+0x22C) to
//  3 (the shutdown state, which PumpWorker services with ShutdownDevice) under
//  the worker's critical section. Null-guarded (the original assumes the
//  worker global is live).
// ============================================================
static void CManagerWin32_StopWorker(void *obj)
{
    (void)obj;
    void *w = GetWorker();
    if (w == 0)
        return;
    KillTimer(*(HWND *)((char *)w + 0x238), 0x1000);
    EnterCriticalSection((LPCRITICAL_SECTION)((char *)w + 4));
    *(int *)((char *)w + 0x22C) = 3;
    LeaveCriticalSection((LPCRITICAL_SECTION)((char *)w + 4));
}

// ============================================================
//  CManagerWin32_PostMessage  (0x18F6CE0 - 0x18F6CFE)  -- post a flag message
//  [NOT byte-identical] placement-dependent (relative call to the PostMessageA
//  IAT slot) and refs the high-VA worker-pair global through slot 8.
//
//  BOOL CManagerWin32_PostMessage(void *obj, unsigned char flag)
//
//  Posts 0x8002 (wParam = flag) to the worker's main window, reached through
//  the manager's own vtable slot 8 (GetWindow). Returns the PostMessageA
//  result (0 when there is no window).
// ============================================================
static int CManagerWin32_PostMessage(void *obj, unsigned char flag)
{
    HWND hwnd = (HWND)CManagerWin32_GetWindow(obj);
    return (int)PostMessageA(hwnd, 0x8002, flag, 0);
}

// ============================================================
//  DisposeInputManager  (0x55DAF0 - 0x55DB01)  -- the input-manager SEH
//  handler. [NOT byte-identical] placement-dependent (relative call through
//  the virtual slot).
//
//  The deleting-destructor dispatch for the 108-byte NInput::CInputManagerDX
//  object (the pair installed into the manager at +0x20/+0x24): if (obj) the
//  object's first virtual method (the deleting dtor) is invoked as
//  vt[0](obj, 1).
// ============================================================
static int DisposeInputManager(int arg)
{
    if (arg != 0)
    {
        void **vt = *(void ***)arg;
        return ((int (*)(void *, int))vt[0])((void *)arg, 1);
    }
    return 0;
}

// ============================================================
//  SetInputManagerPair  (0x55D9C0 - 0x55DA27)  -- install the input-manager
//  object pair. [NOT byte-identical] placement-dependent (relative calls to
//  the InterlockedDecrement IAT slot, the heap free, and LHAlloc) and refs
//  the high-VA heap table (dword_2075B24 + 0x10 = LHFree).
//
//  __usercall in the original (newObject in EDI, F3ObjReg *pair in ESI).
//
//  void SetInputManagerPair(F3ObjReg *pair, void *inputManager)
//
//  Releases the pair's current SEH block (InterlockedDecrement; at zero the
//  block's handler is invoked with its arg and the block is freed through the
//  game heap), clears the pair's SEH word, stores the new object, and installs
//  a fresh 12-byte SEH block (refcount 1, the DisposeInputManager handler,
//  arg = the new object) when the object is nonzero.
// ============================================================
static void SetInputManagerPair(F3ObjReg *pair, void *inputManager)
{
    F3SehBlock *sb = (F3SehBlock *)pair->sehBlock;
    if (sb != 0)
    {
        if (InterlockedDecrement(&sb->refcount) == 0)
        {
            sb->handler(sb->arg);
            LHFree(sb);
        }
    }
    pair->sehBlock = 0;
    pair->object = inputManager;
    if (inputManager != 0)
    {
        F3SehBlock *block = (F3SehBlock *)LHAlloc(12);
        if (block != 0)
        {
            block->refcount = 1;
            block->handler  = DisposeInputManager;
            block->arg      = (int)inputManager;
            pair->sehBlock = block;
        }
    }
}

// Placeholder vtables for the 108-byte NInput::CInputManagerDX object: the
// base ??_7CInputManager (0x01BF915C) and the derived
// ??_7CInputManagerDX (0x01BF9198). Only the deleting-destructor slot (0) is
// live in this build (reached only if the input-manager SEH pair is released);
// the remaining slots stay zero. The dtor body is a [remaining stub].
static int CInputManagerDtorStub(void *obj, int freeIt) { (void)freeIt; return (int)obj; }
static void *g_CInputManagerVtable[12]  = { (void *)CInputManagerDtorStub, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static void *g_CInputManagerDxVtable[12] = { (void *)CInputManagerDtorStub, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// ============================================================
//  CInputManagerDxCtor  (0x188DE60 - 0x188DF82)  -- NInput::CInputManagerDX
//  constructor (the 108-byte input manager)  [NOT byte-identical]
//  placement-dependent (relative calls to the InitializeCriticalSection / LHAlloc
//  IAT slots and the device ctors) and refs the high-VA class vftables
//  (0x01BF915C / 0x01BF9198) + the DirectInput8Create / XInputGetState IAT
//  slots.
//
//  __usercall in the original (hinst in ECX, the 108-byte object in ESI, three
//  stack args). Reimplemented as a plain free function.
//
//  void *CInputManagerDxCtor(void *obj, void *hinst, int *flags, int *res,
//                            int hwnd)
//
//  Sets the base CInputManager vftable, zeroes the device-list / state fields
//  (+0x0C..+0x3C and +0x48), stores the requested resolution pair (res[0]/
//  res[1]) at +0x40/+0x44, initializes the critical section at +0x4C, swaps in
//  the derived CInputManagerDX vftable, and stores the window handle at +0x68.
//  The original then calls DirectInput8Create (hinst, 0x800, IID_IDirectInput8
//  into +0x64) and, per enable flag, creates the three device sub-objects --
//  gamepad 0xA0 (sub_188C730, XInputGetState-gated), mouse 0x244 (sub_188E190)
//  and keyboard 0xC0 (sub_188DB60) -- each registered into the device list
//  (sub_188EF00, the +0x0C/+0x10 circular list). Those device ctors are out of
//  scope here, so they remain stubs: the DirectInput slot (+0x64) stays 0 and
//  the device list stays empty (FindActiveInputDevice then finds no device,
//  exactly as the original does with no input hardware).
// ============================================================
static void *CInputManagerDxCtor(void *obj, void *hinst, int *flags, int *res,
                                 int hwnd)
{
    (void)hinst;
    (void)flags;
    DWORD *o = (DWORD *)obj;
    char *c = (char *)obj;

    o[0] = (DWORD)g_CInputManagerVtable;   // base vftable first
    o[3] = o[4] = o[5]  = 0;   // +0x0C / +0x10 / +0x14 device list
    o[8] = o[9] = o[10] = 0;   // +0x20 / +0x24 / +0x28
    o[13] = o[14] = o[15] = 0; // +0x34 / +0x38 / +0x3C
    o[16] = res[0];            // +0x40 requested width
    o[17] = res[1];            // +0x44 requested height
    c[0x48] = 0;               // active state
    InitializeCriticalSection((CRITICAL_SECTION *)(c + 0x4C));
    o[0] = (DWORD)g_CInputManagerDxVtable; // then the derived vftable
    o[25] = 0;                 // +0x64 LPDIRECTINPUT8 (DirectInput8Create stubbed)
    o[26] = hwnd;               // +0x68
    return obj;
}

// ============================================================
//  CManagerWin32_RunGame  (0x18F7D30 - 0x18F7E58)  -- the manager's "run the
//  game" start-up dispatch (vtable slot 1, the RunFrontEnd game-run call)
//  [NOT byte-identical] placement-dependent (relative calls to
//  CWorkerWin32_RunGame / LHAlloc / CInputManagerDxCtor / SetInputManagerPair /
//  FindActiveInputDevice / the Enter / LeaveCriticalSection IAT slots and the
//  SetEvent IAT slot) and refs the high-VA worker-pair global dword_20FC8F0.
//
//  __thiscall + retn 0x20 in the original (obj in ECX, eight stack args, the
//  result byte in AL).
//
//  char CManagerWin32_RunGame(void *obj, int a2, int a3, int a4, int a5,
//                             int a6, int a7, int a8, F3SharedBuffer *a9)
//
//  Delegates to the worker's run step (CWorkerWin32_RunGame, 0x18F7C50; the
//  manager's a7 is not forwarded to it) and returns 0 when that fails. On
//  success it allocates the 108-byte NInput::CInputManagerDX object and
//  constructs it (CInputManagerDxCtor, 0x188DE60: hinst = the worker's
//  hInstance (+0x230), the three enable flags {1,1,1}, the resolution pair
//  {a3, a4}, hwnd = GetWindow), publishes it into the manager's input pair
//  (+0x20 object / +0x24 SEH block, SetInputManagerPair, 0x55D9C0), enables
//  the input manager (the +0x48 byte = 1 under the +0x4C CS), posts the
//  0x8002 flag-clear message through vtable slot 12, activates the active
//  input device (FindActiveInputDevice, 0x188EC90, then device vtable[10]
//  (device, 1)), signals the worker's event2 (+0x120), and returns 1.
// ============================================================
char CManagerWin32_RunGame(void *obj, int a2, int a3, int a4, int a5, int a6,
                           int a7, int a8, F3SharedBuffer *a9)
{
    (void)a7;   // not forwarded to the worker's run step
    if (!CWorkerWin32_RunGame(GetWorker(), a2, a3, a4, a5, a6, a8, a9))
        return 0;

    CWorkerWin32 *worker = (CWorkerWin32 *)GetWorker();

    void *inputManager = 0;
    void *block = LHAlloc(108);
    if (block != 0)
    {
        int flags[3] = { 1, 1, 1 };   // gamepad / mouse / keyboard enable
        int res[2] = { a3, a4 };      // requested resolution pair
        inputManager = CInputManagerDxCtor(block, (void *)worker->hInstance,
                                           flags, res,
                                           (int)CManagerWin32_GetWindow(obj));
    }
    SetInputManagerPair((F3ObjReg *)((char *)obj + 0x20), inputManager);

    void *im = *(void **)((char *)obj + 0x20);
    if (im != 0)
    {
        char *c = (char *)im;
        EnterCriticalSection((LPCRITICAL_SECTION)(c + 0x4C));
        c[0x48] = 1;   // enable the input manager
        LeaveCriticalSection((LPCRITICAL_SECTION)(c + 0x4C));
    }

    CManagerWin32_PostMessage(obj, 0);   // vtable slot 12 (0x18F6CE0)

    void *device = FindActiveInputDevice(im);
    if (device != 0)
    {
        void **vt = *(void ***)device;
        ((void (*)(void *, int))vt[10])(device, 1);   // activate the device
    }

    if (worker != 0)
        SetEvent(worker->event2);   // +0x120: release the main pump's step
    return 1;
}

static void *g_CManagerWin32Vtable[16] = {
    (void *)CManagerWin32_Dtor,            // 0
    (void *)CManagerWin32_RunGame,         // 1
    (void *)CManagerWin32_StopWorker,      // 2
    0, 0, 0, 0, 0,                         // 3..7
    (void *)CManagerWin32_GetWindow,       // 8
    0,                                     // 9
    0,                                     // 10
    0,                                     // 11
    (void *)CManagerWin32_PostMessage,     // 12
    0, 0, 0                                // 13..15
};

// ============================================================
//  CManagerWin32Ctor  (0x18F7160 - 0x18F718E)  -- NSystem::CManagerWin32
//  constructor
//  [NOT byte-identical] placement-independent, but refs the two class
//  vftables (CManagerBase 0x01BE7340 / CManagerWin32 0x01BE737C) and the
//  InitializeCriticalSection IAT slot (0x01AD531C) at original high VAs not
//  present in this build.
//
//  Construct the 48-byte CManagerWin32: set the vtable (the base CManagerBase
//  vftable first, then the derived CManagerWin32 vftable), initialize the
//  CRITICAL_SECTION at +8, and zero the members at +0x20/+0x24 (dwords) and
//  +0x28/+0x29/+0x2C (bytes; +0x2A/+0x2B left untouched). Returns obj (this).
// ============================================================
void *CManagerWin32Ctor(void *obj)
{
    char *p = (char *)obj;
    *(void **)p = g_CManagerWin32Vtable;   // vtable (base set first, then win32)
    InitializeCriticalSection((CRITICAL_SECTION *)(p + 8));
    *(int *)(p + 32) = 0;   // +0x20
    *(int *)(p + 36) = 0;   // +0x24
    p[40] = 0;              // +0x28
    p[41] = 0;              // +0x29
    p[44] = 0;              // +0x2C
    return p;
}

// ============================================================
//  [BYTE-IDENTICAL]  DisposeManager  (0x00615850 - 0x00615860)  -- SEH handler
//
//  Invoked by the SEH block (for the 48-byte NSystem::CManagerWin32 object
//  created by CManagerWin32Ctor) when its refcount reaches zero. It calls the
//  object's first virtual method (vtable[0], the scalar-deleting destructor)
//  with argument 1:
//      if (obj != 0) return obj->vtable[0](obj, 1);
//  Byte-identical to DisposeWorker (0x615880); only the target object differs.
//  Emits the original 17 bytes exactly (placement-independent: no absolute
//  refs, and the vtable call is indirect via EDX).
// ============================================================
int __declspec(naked) DisposeManager(int)
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
long SetManager(F3ObjReg *reg)
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
