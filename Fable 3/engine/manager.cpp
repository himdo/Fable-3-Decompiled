// Fable 3 — CManagerWin32 module implementation (the 48-byte manager object,
// its deleting destructor, and the current-manager registry).

#include "manager.h"
#include "common/common.h"

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
// refcounted block at +36. Slot 0 is the deleting destructor.
//
// The remaining slots below are the virtual methods the frontend (RunFrontEnd,
// 0x61CB70) dispatches through. They are stubbed here so the vtable indirection
// resolves; real per-method behavior is out of scope until each is decompiled.
//
//  slot 1  (0x04)  "run the game" -- the big start-up dispatch. Returns 1 so the
//              frontend proceeds down the success path.
//  slot 8  (0x20)  GetWindow -- the manager's HWND (0 = none, the desktop).
//  slot 10 (0x28)  a flag setter called as (this, byte).
//  slot 12 (0x30)  a setter called as (this, 1) on the sign-in paths.
static int CManagerWin32_RunGame(void *obj, int a1, int a2, int a3, int a4,
                                 int a5, int a6, int a7, int a8)
{
    (void)obj; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    (void)a6; (void)a7; (void)a8;
    return 1;
}
static void *CManagerWin32_GetWindow(void *obj) { (void)obj; return 0; }
static void  CManagerWin32_SetFlag(void *obj, int flag) { (void)obj; (void)flag; }

static void *g_CManagerWin32Vtable[16] = {
    (void *)CManagerWin32_Dtor,        // 0
    (void *)CManagerWin32_RunGame,     // 1
    0, 0, 0, 0, 0, 0,                  // 2..7
    (void *)CManagerWin32_GetWindow,   // 8
    0,                                 // 9
    (void *)CManagerWin32_SetFlag,     // 10
    0,                                 // 11
    (void *)CManagerWin32_SetFlag,     // 12
    0, 0, 0                            // 13..15
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
