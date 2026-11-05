// Fable 3 — game thread module implementation (GameThread, 0x615380, plus
// its module-private globals, SEH handlers, and stub callees).

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // original used _snwprintf
#endif

#include "gamethread.h"
#include "engine.h"               // SetThreadName
#include "framework/framework.h"  // FrameworkInit
#include "common/common.h"        // LHAlloc / LHFree
#include "save/save.h"            // SetSaveGamePath
#include "object_registry.h"      // F3ObjReg / SetDebugTrace / F3ReleaseSehRef
#include "manager.h"              // SetManager / DisposeManager / CManagerWin32Ctor
#include "gameengine.h"           // CGameEngineCtor
#include "gamemain.h"             // RunFrontEnd
#include "netinit.h"              // g_dword_1DC2550/554 + CNetManagerShutdown

#include <windows.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>

// 0x1DBD008: 1 when the shared-memory blob was loaded, 0 otherwise.
static BYTE  g_byte_1DBD008 = 0;
// 0x1DBD010 / 0x1DBD014: objects whose strings form the error message box
// (body / title) shown when the shared-memory blob is missing.
static DWORD g_dword_1DBD010 = 0;
static DWORD g_dword_1DBD014 = 0;

// Placeholder vtables for the two objects created on the game thread.
static void *g_CFableWindowsDebugHandlerVtable[4] = { 0 };
static void *g_CDebugTraceFableVtable[4]          = { 0 };

// The game buffer (0x6C4 bytes) that the shared-memory blob is loaded into.
// Its original address is 0x1DBD318 (a high-VA data global).
static char g_gameBuffer[0x6C4];

// ---- SEH handlers for the three objects (called when their refcount hits 0) ----
// ============================================================
//  LHFreeHandler  (0x00401D20 - 0x00401D2B)  -- SEH cleanup handler that
//  frees its object on the game heap
//  [NOT byte-identical] placement-independent, but refs the heap table
//  (0x02075B24) at the original high VA not present in this build.
//
//  int LHFreeHandler(int obj)
//
//  Byte-identical to LHFree (0x1A032C0): the 11-byte thunk
//  `mov ecx, dword_2075B24; mov edx, [ecx+10h]; jmp edx` that tail-calls
//  the heap table's free slot. Stored as the `handler` of a 12-byte SEH
//  block ({ refcount, handler, arg }); when the block's refcount hits 0
//  the engine calls handler(arg), freeing `arg` via the game heap.
//  Referenced by GameThread (0x615380, CFableWindowsDebugHandler pair)
//  and by sub_42DE70 (generic object + SEH-block registration).
// ============================================================
static int LHFreeHandler(int arg) { LHFree((void *)arg); return 0; }

// ============================================================
//  JmpLHFree  (0x615870 - 0x615875)  -- 5-byte `jmp LHFree` thunk, stored as
//  the CDebugTraceFable SEH block's handler
//  [NOT byte-identical] placement-dependent: the near `jmp LHFree` is rebased
//  to this build's LHFree (the original displacement 0x013EDA4B targets
//  LHFree at its original VA 0x1A032C0).
//
//  A raw tail jump with no arg handling of its own: the SEH release code
//  pushes the object pointer and calls the handler, so LHFree receives that
//  pointer as its own (cdecl) parameter and frees the CDebugTraceFable via
//  the game heap.
// ============================================================
static int __declspec(naked) JmpLHFree(int)
{
    __asm { jmp LHFree }
}

// ---- other engine callees (stubs for now) -----------------------------------
// ============================================================
//  GetGameBuffer  (0x00619AA0 - 0x00619AA6)  -- getter for the game buffer
//  [NOT byte-identical] placement-independent (no calls / relative jumps),
//  but refs the game buffer at its original high VA (0x01DBD318), not present
//  in this build.
//
//  void *GetGameBuffer(void)
//
//  A 6-byte getter: `mov eax, offset <game buffer>; retn`. GameThread zeroes
//  the buffer and fills it from the shared-memory blob (-=[SMS_<exename>_SMS]=-);
//  the engine read paths (sub_6488A0, sub_9F2770, sub_9F31F0, sub_9F38A0,
//  sub_9F4B50) read fields out of it (e.g. +68, +108, +680, +1728).
// ============================================================
void *GetGameBuffer(void)
{
    return g_gameBuffer;   // originally 0x1DBD318
}

// ---- the post-game component shutdown pump (CGameEngineShutdown) -----------
//
// The CGameEngine's node list (the 12-byte self-linked head block stored at
// +0x14, the count at +0x18, and the iterator pair at +0x1C/+0x20) holds
// 12-byte circular-doubly-linked records {+0: link toward the head (older),
// +4: link away from the head (newer), +8: value}. The values are 16-byte
// CGameComponents; CStartupComponent is the concrete wrapper whose +0x0C
// points at the 152-byte CStartupComponentImplementation.

// 0x1DC2868: the currently published CStartupComponent (set by the ctor,
// cleared by the dtor). The CGameComponent * type is declared below.
typedef struct CGameComponent CGameComponent;
static CGameComponent *g_startupComponent = 0;
// 0x1DBD9E2: debug stall flag -- when set, the shutdown pump sleeps
// 0x2710 ms (10 s) after each component tick.
static BYTE g_byte_1DBD9E2 = 0;
// 0x1DDC884: debug flag armed by the pump's step branch.
static BYTE g_byte_1DDC884 = 0;

// The original's debug-list iterator checks call the CRT's
// __invalid_parameter_noinfo(), which reports to the (unset) invalid-parameter
// handler and returns normally, so the original keeps executing after each
// check. A no-op with the same return path is functionally equivalent here
// (the VS2008 msvcrt import library does not carry the symbol).
static void F3InvalidParam(void) {}

// ============================================================
//  CGameComponent  -- the 16-byte engine component wrapper
//
//  +0x00 vtable   CGameComponent / CStartupComponent vftable
//  +0x04/+0x05    two flag bytes zeroed by CGameComponentCtor
//  +0x08 engine   the CGameEngine that owns the component
//  +0x0C impl     the 152-byte CStartupComponentImplementation
// ============================================================
struct CGameComponent
{
    void *   vtable;
    char     flag04;
    char     flag05;
    char     _pad0[2];
    void *   engine;
    void *   impl;
};

// Single-slot placeholder vtables (unimplemented in this build) for the NTools
// base classes and the CAInputProcess base object.
static void *g_NToolsCBaseClassNonCopyableVtable[1] = { 0 };
static void *g_NToolsCBaseClassVtable[1] = { 0 };
static void *g_CAInputProcessVtable[1] = { 0 };

// Release a vtable object through its vtable slot 0 with a 1 (the original's
// `obj->vtable[0](obj, 1)` dispatch).
static void *F3ReleaseOne(void *o)
{
    void **vt = (void **)*(void **)o;
    return ((void *(*)(void *, int))vt[0])(o, 1);
}

// ============================================================
//  CBaseClassNonCopyableCtor  (0x18984D0 - 0x18984D8)  -- base ctor
//  [NOT byte-identical] placement-dependent: writes the NTools vtable at its
//  original high VA, not present in this build.
//
//  void *CBaseClassNonCopyableCtor(void *obj)
// ============================================================
static void *CBaseClassNonCopyableCtor(void *obj)
{
    *(void **)obj = g_NToolsCBaseClassNonCopyableVtable;   // 0x...CBaseClassNonCopyable::vftable
    return obj;
}

// ============================================================
//  CBaseClassDtor  (0x18984C0 - 0x18984C6)  -- base dtor (vtable store)
//  [NOT byte-identical] placement-dependent: writes the NTools::CBaseClass
//  vtable at its original high VA, not present in this build.
//
//  void CBaseClassDtor(void *obj)
// ============================================================
static void CBaseClassDtor(void *obj)
{
    *(void **)obj = g_NToolsCBaseClassVtable;   // 0x...CBaseClass::vftable
}

// ============================================================
//  CGameComponentDtor  (0xBF8730 - 0xBF874B)  -- the 16-byte wrapper dtor
//  [NOT byte-identical] placement-dependent (relative calls to the base dtor
//  and LHFree).
//
//  void *CGameComponentDtor(void *obj, int freeIt)
// ============================================================
static void *CGameComponentDtor(void *obj, int freeIt)
{
    CBaseClassDtor(obj);
    if (freeIt & 1)
        LHFree(obj);
    return obj;
}

// nullsub_3553 / nullsub_3544 / nullsub_162: no-op component vtable slots.
static void CGameComponentNoop(void) {}

// CGameComponent vftable (0x1B1FB38).
static void *g_CGameComponentVtable[6] = {
    CGameComponentDtor,   // 0xBF8730
    CGameComponentNoop,   // nullsub_3553
    CGameComponentNoop,   // nullsub_3544
    CGameComponentNoop,   // nullsub_162
    CGameComponentNoop,   // nullsub_162
    CGameComponentNoop    // nullsub_162
};

// 0x71E910 -- the 152-byte implementation's dtor body: releases the sub-object
// slots (+0x90 via sub_71DE20, +0x8C, +0x48, +0x20, +0x1C via sub_B6EB90,
// +0x18, +0x14, the two critical sections at +0x70/+0x50), zeroes them all,
// and stores the CAInputProcess vtable (sub_B6A070). Every slot is zero in
// this build, so only the CS deletes and the vtable store are live.
static void CImpl90Release(void *obj)  { (void)obj; }   // sub_71DE20 -- remaining stub
static void CImpl1CRelease(void *obj)  { (void)obj; }   // sub_B6EB90 -- remaining stub

// ============================================================
//  CStartupComponentImplDtorBody  (0x71E910 - 0x71E9C7)  -- impl dtor body
//  [NOT byte-identical] placement-dependent (relative calls to LHFree,
//  sub_71DE20 / sub_B6EB90, the CRT critical-section deletes, and the
//  CAInputProcess vtable store).
//
//  void *CStartupComponentImplDtorBody(void *obj)
// ============================================================
static void *CStartupComponentImplDtorBody(void *impl)
{
    char  *b = (char *)impl;
    void **d = (void **)impl;
    void  *o;

    if ((o = d[36]) != 0)               // +0x90
    {
        CImpl90Release(o);
        LHFree(o);
    }
    d[36] = 0;
    if ((o = d[35]) != 0)               // +0x8C
        F3ReleaseOne(o);
    d[35] = 0;
    DeleteCriticalSection((CRITICAL_SECTION *)(b + 112));
    DeleteCriticalSection((CRITICAL_SECTION *)(b + 80));
    if ((o = d[18]) != 0)               // +0x48
        F3ReleaseOne(o);
    d[18] = 0;
    if ((o = d[8]) != 0)                // +0x20
        F3ReleaseOne(o);
    d[8] = 0;
    if ((o = d[7]) != 0)                // +0x1C
    {
        CImpl1CRelease(o);
        LHFree(o);
    }
    d[7] = 0;
    if ((o = d[6]) != 0)                // +0x18
        F3ReleaseOne(o);
    d[6] = 0;
    if ((o = d[5]) != 0)                // +0x14
        F3ReleaseOne(o);
    d[5] = 0;
    d[0] = g_CAInputProcessVtable;      // sub_B6A070: store the base vtable
    return impl;
}

// ============================================================
//  CStartupComponentImplDtor  (0x71F320 - 0x71F33B)  -- impl dtor
//  [NOT byte-identical] placement-dependent (relative calls to the dtor body
//  and LHFree).
//
//  void *CStartupComponentImplDtor(void *obj, int freeIt)
// ============================================================
static void *CStartupComponentImplDtor(void *obj, int freeIt)
{
    CStartupComponentImplDtorBody(obj);
    if (freeIt & 1)
        LHFree(obj);
    return obj;
}

static void *g_CStartupComponentImplVtable[8] = {
    CStartupComponentImplDtor,  // 0x71F320
    0, 0, 0, 0, 0, 0, 0,        // 0x71E3B0..0x71E4C0, nullsub_4602 -- remaining stubs
};

// The implementation's big method bodies, reached through the CStartupComponent
// vtable slots. They are remaining stubs: the 724-byte release (0x71E630), the
// 333-byte second release (0x71D000), and the 2327-byte tick (0x71EA00). The
// tick's return value (the next component, or 0) drives the shutdown pump;
// returning 0 ends the pump after the first iteration, as the original does
// once the shutdown chain is exhausted.
static int   CStartupComponentImplRelease(void *impl)   { (void)impl; return 0; }   // 0x71E630
static int   CStartupComponentImplRelease2(void *impl)  { (void)impl; return 0; }   // 0x71D000
static void *CStartupComponentImplUpdate(void *impl)    { (void)impl; return 0; }   // 0x71EA00

// 0x71C690 -- CStartupComponent vtable slot 1 (release).
static int CStartupComponentRelease(CGameComponent *obj)
{
    // nullsub_3553: no-op
    return CStartupComponentImplRelease(obj->impl);
}

// 0x71C600 -- CStartupComponent vtable slot 2 (second release).
static int CStartupComponentRelease2(CGameComponent *obj)
{
    // nullsub_3544: no-op
    return CStartupComponentImplRelease2(obj->impl);
}

// 0x71C6B0 -- CStartupComponent vtable slot 3 (tick: run this step of the
// shutdown, return the next component or 0).
static void *CStartupComponentUpdate(CGameComponent *obj)
{
    return CStartupComponentImplUpdate(obj->impl);
}

// 0x71C430 -- CStartupComponent vtable slot 4 (impl +16 getter).
static void *CStartupComponentImplPtr16(CGameComponent *obj)
{
    return *(void **)((char *)obj->impl + 16);
}

// 0x71C440 -- CStartupComponent vtable slot 5 (impl +20 getter).
static void *CStartupComponentImplPtr20(CGameComponent *obj)
{
    return *(void **)((char *)obj->impl + 20);
}

// The CStartupComponent vtable array is defined after the dtor (the dtor
// stores it, so the two reference each other); the getter breaks the cycle.
static void *GetCStartupComponentVtable(void);

// ============================================================
//  CStartupComponentDtor  (0x71D150 - 0x71D1A9)  -- component dtor
//  [NOT byte-identical] placement-dependent (relative calls to the impl
//  vtable, the base dtor, and LHFree; writes dword_1DC2868).
//
//  void *CStartupComponentDtor(CGameComponent *comp, int freeIt)
//
//  Restores the CStartupComponent vtable, releases the implementation through
//  its vtable (twice -- the second read of comp->impl is already zero, so it
//  is a no-op), clears the global, runs the base dtor, and frees the wrapper
//  when freeIt is odd.
// ============================================================
static void *CStartupComponentDtor(CGameComponent *obj, int freeIt)
{
    obj->vtable = GetCStartupComponentVtable();
    if (obj->impl != 0)
        F3ReleaseOne(obj->impl);
    obj->impl = 0;
    g_startupComponent = 0;   // dword_1DC2868
    CBaseClassDtor(obj);
    if (freeIt & 1)
        LHFree(obj);
    return obj;
}

// CStartupComponent vftable (0x1AF4AB0).
static void *g_CStartupComponentVtable[6] = {
    CStartupComponentDtor,      // 0x71D150
    CStartupComponentRelease,   // 0x71C690
    CStartupComponentRelease2,  // 0x71C600
    CStartupComponentUpdate,    // 0x71C6B0
    CStartupComponentImplPtr16, // 0x71C430
    CStartupComponentImplPtr20  // 0x71C440
};

static void *GetCStartupComponentVtable(void)
{
    return g_CStartupComponentVtable;
}

// ============================================================
//  CGameComponentCtor  (0xBF86E0 - 0xBF8700)  -- the 16-byte wrapper ctor
//  [NOT byte-identical] placement-dependent (relative call to the base ctor
//  + the CGameComponent vtable store).
//
//  void *CGameComponentCtor(CGameComponent *comp, void *engine)
// ============================================================
static void *CGameComponentCtor(CGameComponent *comp, void *engine)
{
    CBaseClassNonCopyableCtor(comp);
    comp->flag04 = 0;
    comp->flag05 = 0;
    comp->engine = engine;
    comp->vtable = g_CGameComponentVtable;
    return comp;
}

// ============================================================
//  CStartupComponentImplCtor  (0x71E2F0 - 0x71F31F)  -- 152-byte impl ctor
//  [NOT byte-identical] placement-dependent (relative calls to the CRT
//  critical-section inits + the vtable store). The vtable slots and sub-object
//  pointers are zeroed, the engine pointer is copied from comp+8, the +0x50
//  and +0x70 critical sections are initialized, and the +0x69 byte is set to 1.
//
//  void *CStartupComponentImplCtor(void *obj, void *comp)
// ============================================================
static void *CStartupComponentImplCtor(void *impl, void *comp)
{
    char  *b = (char *)impl;
    void **d = (void **)impl;

    d[0]  = g_CStartupComponentImplVtable;
    d[1]  = 0;
    *(void **)(b + 8)  = 0;
    *(void **)(b + 12) = 0;
    d[5]  = 0;
    d[6]  = 0;
    d[7]  = 0;
    d[8]  = 0;
    *(void **)(b + 40) = 0;
    *(void **)(b + 44) = 0;
    d[12] = 0;
    d[13] = 0;
    d[14] = 0;
    d[15] = 0;
    d[16] = ((void **)comp)[2];    // *(comp+8) = the CGameEngine
    b[68] = 0;
    d[18] = 0;
    d[19] = 0;
    InitializeCriticalSection((CRITICAL_SECTION *)(b + 80));
    b[104] = 0;
    b[105] = 1;
    b[106] = 0;
    b[107] = 0;
    b[108] = 0;
    InitializeCriticalSection((CRITICAL_SECTION *)(b + 112));
    d[34] = 0;
    d[35] = 0;
    d[36] = 0;
    return impl;
}

// ============================================================
//  CStartupComponentCtor  (0x71C630 - 0x71C686)  -- build the startup component
//  [NOT byte-identical] placement-dependent (relative calls to the wrapper
//  ctor, LHAlloc, the impl ctor, the old-impl release, and the
//  CStartupComponent vtable store; writes dword_1DC2868).
//
//  CGameComponent *CStartupComponentCtor(CGameComponent *comp, void *engine)
//
//  Runs the 16-byte wrapper ctor (CGameComponent vtable first, then the
//  CStartupComponent vtable), allocates the 152-byte implementation on the
//  game heap and constructs it, releases the previous implementation through
//  its vtable (zero on first construction), stores the new one at +0x0C, and
//  publishes the wrapper into dword_1DC2868. Returns the wrapper.
// ============================================================
static CGameComponent *CStartupComponentCtor(CGameComponent *comp, void *engine)
{
    CGameComponentCtor(comp, engine);
    comp->vtable = g_CStartupComponentVtable;
    comp->impl   = 0;

    void *impl = LHAlloc(152);
    void *newImpl = 0;
    if (impl != 0)
        newImpl = CStartupComponentImplCtor(impl, comp);

    void *old = comp->impl;   // 0 here: the vtable store zeroed it above
    if (old != 0)
        F3ReleaseOne(old);
    comp->impl = newImpl;

    g_startupComponent = comp;   // dword_1DC2868
    return comp;
}

// ============================================================
//  ListCountInc  (0x624360 - 0x62446F)  -- the list size increment
//  [NOT byte-identical] placement-dependent and refs the std::length_error /
//  exception machinery in its overflow path.
//
//  void ListCountInc(CGameEngine *obj, int n)
//
//  Thiscall + `retn 4`. Adds n to the list count at +0x18; when the unsigned
//  remainder 0x3FFFFFFF - count is smaller than n, the original builds a
//  std::length_error("list<T> too long") and throws. That path is unreachable
//  with a list this size, so the throw is emulated with a plain C++ throw.
// ============================================================
static void ListCountInc(CGameEngine *obj, int n)
{
    DWORD *cnt = (DWORD *)&obj->listCount;
    if (0x3FFFFFFF - *cnt < (DWORD)n)
        throw 1;   // the original throws std::length_error("list<T> too long")
    *cnt += (DWORD)n;
}

// ============================================================
//  ListPushFront  (0x624BF0 - 0x624C45)  -- push a value onto the node list
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc and
//  ListCountInc).
//
//  void *ListPushFront(CGameEngine *obj, int unused, void *head, void *value)
//
//  Thiscall + `retn 0Ch` (obj in ECX, three ints on the stack; the first is
//  unused by the callee). Allocates a 12-byte node {head, first, *value} and
//  splices it in front of the old first node (head+4 = the first node,
//  head+0 = the last node: the new node's prev is the head, its next is the
//  old first, the old first's prev becomes the new node -- when the list is
//  empty the old first is the head block itself, so head+0 picks up the new
//  node), then increments the engine's list count via ListCountInc. Returns
//  the old first node (discarded by callers).
// ============================================================
static void *ListPushFront(CGameEngine *obj, int unused, void *head, void *value)
{
    (void)unused;
    void *first = *(void **)((char *)head + 4);
    void *node  = LHAlloc(12);
    if (node != 0)
        *(void **)node = head;
    if (node != 0)
        *(void **)((char *)node + 4) = first;
    if (node != 0)
        *(void **)((char *)node + 8) = *(void **)value;
    ListCountInc(obj, 1);
    *(void **)((char *)head + 4) = node;
    *(void **)first = node;   // old first's prev = the new node (head[0] when empty)
    return first;
}

// ============================================================
//  ListErase  (0x622D20 - 0x622DA0)  -- unlink one node from the list
//  [NOT byte-identical] placement-dependent (relative call to LHFree; the
//  original's debug-list iterator checks are no-op __invalid_parameter_noinfo
//  calls, so they are omitted).
//
//  void *ListErase(CGameEngine *obj, void *iter, void *last, void *node)
//
//  Thiscall + `retn 0Ch`. Unlinks `node` from the circular list rooted at
//  obj->listFirst (the head block) and LHFrees it (skipped when node is the
//  head itself), decrementing the count. The out `iter` (a 4-byte iterator
//  pair) is set to {obj->heapNode, node's prev link} and returned.
// ============================================================
static void *ListErase(CGameEngine *obj, void *iter, void *last, void *node)
{
    (void)last;
    void *prev = *(void **)node;
    if (node != obj->listFirst)
    {
        void *next = *(void **)((char *)node + 4);
        *(void **)next = prev;
        *(void **)((char *)prev + 4) = next;
        LHFree(node);
        *(DWORD *)&obj->listCount = (DWORD)*(DWORD *)&obj->listCount - 1;
    }
    *(void **)iter = obj->heapNode;
    *(void **)((char *)iter + 4) = prev;
    return iter;
}

// ============================================================
//  DestroyComponentsExcept  (0x61AA90 - 0x61ABA3)  -- drain the component list
//  [NOT byte-identical] placement-dependent (relative calls to the component
//  vtable slots, ListErase, and LHFree).
//
//  void DestroyComponentsExcept(CGameEngine *obj, void *keep)
//
//  Walks the node list from the last node toward the head. A node whose value
//  equals `keep` is skipped; every other node's component is released through
//  vtable slots 2 and 0(,1) and erased from the list. Once the walk reaches
//  the head, the list is reset to empty (head+0 = head+4 = head, count = 0,
//  every remaining node LHFreed) and the iterator pair is reset to
//  {heapNode, head}.
// ============================================================
static void DestroyComponentsExcept(CGameEngine *obj, void *keep)
{
    void *head = obj->listFirst;
    obj->listLast = obj->heapNode;
    void *cur = *(void **)head;

    for (;;)
    {
        if (obj->listLast == 0 || obj->listLast != obj->heapNode)
            F3InvalidParam();
        if (cur == head)
            break;
        void *val = *(void **)((char *)cur + 8);
        if (val == keep)
        {
            cur = *(void **)cur;
            continue;
        }
        void **vt = (void **)*(void **)val;
        ((void (*)(void *))vt[2])(val);
        F3ReleaseOne(val);
        void *iter[2];
        ListErase(obj, iter, obj->listLast, cur);
        obj->listLast = *(void **)iter;
        cur = *(void **)((char *)iter + 4);
    }

    head      = obj->listFirst;
    void *node = *(void **)head;
    *(void **)head = head;
    *(void **)((char *)head + 4) = head;
    obj->listCount = 0;
    for (;; node = *(void **)node)
    {
        if (node == head)
            break;
        LHFree(node);
    }
    obj->listLast    = obj->heapNode;
    obj->listSentinel = head;
}

// 0x14640C0 -- store a byte into byte_1DDC884 and return it (the 14-byte
// 0x145D630 thunk forwards a pushed int to it with ECX preserved).
static void SetDebugByte1DDC884(int v)
{
    g_byte_1DDC884 = (BYTE)v;
}

// ============================================================
//  CGameEngineShutdown  (0x61BE00 - 0x61BFD6)  -- the post-game shutdown pump
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc,
//  CStartupComponentCtor, the component vtable slots, ListPushFront,
//  ListCountInc, DestroyComponentsExcept, Sleep, and the heap-manager getters)
//  and it refs the high-VA debug flags byte_1DBD9E2 / byte_1DDC884.
//
//  void CGameEngineShutdown(CGameEngine *obj)
//
//  Called by GameThread when RunFrontEnd succeeds. It allocates the 16-byte
//  CStartupComponent wrapper on the game heap, constructs it (publishing it
//  into dword_1DC2868), runs its release step (vtable slot 1), and pushes it
//  onto the engine's node list. It then pumps while the +0x24 done flag is
//  clear: each iteration ticks the current node's component (vtable slot 3).
//  A non-null result drains the list except for the result (release the rest
//  through vtable slots 2/0, free the nodes), releases the result through
//  vtable slot 1, and re-pushes it as the sole list node (iterator reset to
//  {heapNode, last node}); a null result with the +0x25 flag set arms
//  byte_1DDC884, steps the iterator to the previous node (wrapping to the last
//  node at the head), releases that node's component through vtable slot 1,
//  and clears the flag; a null result without the flag sets the done flag and
//  returns. When byte_1DBD9E2 is set, the pump sleeps 0x2710 ms after each
//  tick. The original's __invalid_parameter_noinfo debug-list checks are kept:
//  they return normally, so the pump continues after each one.
// ============================================================
void CGameEngineShutdown(CGameEngine *obj)
{
    char  *base = (char *)obj;
    DWORD *dw   = (DWORD *)obj;

    // 1. Build the CStartupComponent and push it onto the node list.
    CGameComponent *comp = (CGameComponent *)LHAlloc(16);
    if (comp != 0)
    {
        comp = CStartupComponentCtor(comp, obj);
        if (comp != 0)
        {
            void **vt = (void **)comp->vtable;
            ((void (*)(void *))vt[1])(comp);
            void *head = (void *)dw[5];
            void *val  = comp;
            ListPushFront(obj, (int)obj->heapNode, head, &val);
            dw[7] = (DWORD)obj->heapNode;
            dw[8] = (DWORD)*(void **)head;
        }
    }

    // 2. Pump the current node until the done flag is set.
    while (base[0x24] == 0)
    {
        void *it = (void *)dw[7];
        if (it == 0)
            F3InvalidParam();
        void *val0 = (it != 0) ? *(void **)it : 0;
        if ((void *)dw[8] == (val0 != 0 ? *(void **)((char *)val0 + 20) : 0))
            F3InvalidParam();

        void *val  = *(void **)((char *)(void *)dw[8] + 8);   // cur's value (component)
        void **vt  = (void **)*(void **)val;
        void *next = ((void *(*)(void *))vt[3])(val);

        if (g_byte_1DBD9E2 != 0)
            Sleep(0x2710);   // debug stall (10 s)

        if (next != 0)
        {
            // New component: drain the list except for it, release it, and
            // re-push it as the sole node.
            DestroyComponentsExcept(obj, next);
            void **nvt = (void **)*(void **)next;
            ((void (*)(void *))nvt[1])(next);

            void *head = (void *)dw[5];
            void *oldFirst = *(void **)((char *)head + 4);
            void *node = LHAlloc(12);
            if (node != 0)
            {
                *(void **)node = head;
                *(void **)((char *)node + 4) = oldFirst;
                *(void **)((char *)node + 8) = next;
            }
            ListCountInc(obj, 1);
            *(void **)((char *)head + 4) = node;
            *(void **)oldFirst = node;   // old first's prev = the new node
            dw[7] = (DWORD)obj->heapNode;
            dw[8] = (DWORD)*(void **)head;   // head[0] = the new node (list was empty)
        }
        else if (base[0x25] != 0)
        {
            // Step: arm the debug flag, advance to the previous node (wrapping
            // to the last node at the head), release the node's component,
            // and clear the flag.
            void *hm = GetHeapManager();            // 0x18FA3E0: dword_2075ED8
            hm = GetHeapManagerSlot1C(hm);          // 0x18FA3D0: *(hm+0x1C)
            SetDebugByte1DDC884(1);                 // 0x145D630(1) -> 0x14640C0
            (void)hm;

            it = (void *)dw[7];
            if (it == 0)
                F3InvalidParam();
            void *val1 = (it != 0) ? *(void **)it : 0;
            if ((void *)dw[8] == (val1 != 0 ? *(void **)((char *)val1 + 20) : 0))
                F3InvalidParam();
            dw[8] = (DWORD)*(void **)(void *)dw[8];   // cur = cur's prev link
            if (it == 0 || it != obj->heapNode)       // debug checks after the step
                F3InvalidParam();
            if ((void *)dw[8] == (void *)dw[5])       // wrapped around to the head
            {
                dw[7] = (DWORD)obj->heapNode;
                dw[8] = (DWORD)*(void **)(void *)dw[5];
            }
            it = (void *)dw[7];
            if (it == 0)
                F3InvalidParam();
            void *val2 = (it != 0) ? *(void **)it : 0;
            if ((void *)dw[8] == (val2 != 0 ? *(void **)((char *)val2 + 20) : 0))
                F3InvalidParam();
            void *valN = *(void **)((char *)(void *)dw[8] + 8);   // the new cur's value
            void **svt = (void **)*(void **)valN;
            ((void (*)(void *))svt[1])(valN);
            base[0x25] = 0;
        }
        else
        {
            base[0x24] = 1;
        }
    }
}

// ---- the post-pump engine cleanup (CGameEngineCleanup, 0x61AE70) ---------
//
// Engine-service teardown + unimplemented callees of CGameEngineCleanup.
//
// The 0x18E-series engine-time / audio-queue teardown (remaining stubs):
//  sub_18EC3E0  resets two global node lists (dword_20FD110 / dword_20FD0B4)
//               and clears byte_2075F28; sub_18E7710  spin-waits (SwitchToThread)
//               for byte_2075F38 to clear under stru_20FD1B4 (arming
//               byte_2075F39 when it was set); sub_18E7830  drains four queue
//               lists (sub_584870 release + list reset each) and frees the
//               dword_1DB12E8 block through the heap table; sub_18E8B70  is
//               sub_57FDF0() + dword_20FD1AC = 0.
static void  EngineTimeReset(void) { }                                    // sub_18EC3E0
static void  EngineTimeWaitIdle(void) { }                                 // sub_18E7710
static void  EngineQueueDrain(void) { }                                   // sub_18E7830
static void  EngineServiceStop(void) { }                                  // sub_18E8B70
// sub_718C50 (sub_718140 + sub_17CA260) and the sub_1926120 -> sub_1922420
// thunk: engine-subsystem stops (remaining stubs).
static void  sub_718C50(void) { }
static int   sub_1926120(void) { return 0; }
// The +0xBC release chain (remaining stubs): sub_6AA2C0 clears the net
// manager's +0x1C sub-object's internal list (sub_6AB810 + reset),
// sub_6281B0 forwards to sub_5EDFE0 (the +0xBC object's release), and
// sub_717AB0 frees the dword_1DC26E4 global (sub_19FEA30 / sub_19FE510
// releases + LHFree).
static void  sub_6AA2C0(void *obj) { (void)obj; }
static void  sub_6281B0(int a) { (void)a; }
static void  sub_717AB0(void) { }
// nullsub_189 / nullsub_190: bare `retn` thunks (no-ops).
static void  nullsub_189(void) { }
static void  nullsub_190(void) { }

// 0x6A6900 -- thiscall getter for the CNetManager's +0x1C sub-object.
// (GetNetManager / sub_6A66C0 is canonical in netinit.cpp, extern via netinit.h.)
static void *CNetManagerSlot1C(void *obj) { return *(void **)((char *)obj + 0x1C); }

// ============================================================
//  CGameEngineCleanup  (0x61AE70 - 0x61AF33)  -- post-pump engine cleanup
//  [NOT byte-identical] placement-dependent (relative calls to
//  DestroyComponentsExcept, the engine-time teardowns, the net-manager
//  teardown, the Win32 IAT slots, and the shared 0x6A72F0 epilogue).
//
//  void CGameEngineCleanup(CGameEngine *obj)
//
//  Called by GameThread right after CGameEngineShutdown on the RunFrontEnd
//  success path. It drains the component list (DestroyComponentsExcept
//  (obj, 0)), closes the +0xE8 handle (CloseHandle when nonzero), runs the
//  engine-time / queue teardowns (sub_18EC3E0 / sub_18E7710 / sub_18E7830),
//  dispatches the manager's vtable slot 2 (sub_18F6DC0 in the original),
//  stops the engine services (sub_18E8B70 / sub_718C50 / sub_1926120), and
//  sleeps 10 ms. It then releases the +0xC0 object through its deleting
//  destructor (vtable[0](slot, 1), zeroed) and, when the +0xBC handle is
//  set, clears the net manager's +0x1C sub-object list (sub_6A66C0 /
//  sub_6A6900 / sub_6AA2C0), releases the +0xBC object (sub_6281B0, zeroed),
//  and frees the dword_1DC26E4 global (sub_717AB0). The trailing no-op
//  thunks (nullsub_189 / nullsub_190) lead into the shared 0x6A72F0
//  epilogue: when the net-manager pair (dword_1DC2550 / dword_1DC2554) is
//  active, it stops the net manager (CNetManagerShutdown, 0x6A70F0),
//  releases the pair's SEH block (InterlockedDecrement; on zero the block's
//  handler is invoked with its arg + LHFree), and clears both globals.
//  The return register is discarded by the sole caller.
// ============================================================
void CGameEngineCleanup(CGameEngine *obj)
{
    DestroyComponentsExcept(obj, 0);

    if (obj->member0xE8 != 0)
    {
        CloseHandle((HANDLE)obj->member0xE8);
        obj->member0xE8 = 0;
    }

    EngineTimeReset();          // sub_18EC3E0
    EngineTimeWaitIdle();       // sub_18E7710
    EngineQueueDrain();         // sub_18E7830

    void *mgr = GetManager();
    if (mgr != 0)
    {
        void **vt = (void **)*(void **)mgr;
        ((void (*)(void *))vt[2])(mgr);   // vtable slot 2 (offset 8)
    }

    EngineServiceStop();        // sub_18E8B70
    sub_718C50();
    (void)sub_1926120();
    Sleep(10);

    if (obj->member0xC0 != 0)
        F3ReleaseOne(obj->member0xC0);   // vtable[0](slot, 1)
    obj->member0xC0 = 0;

    if (obj->member0xBC != 0)
    {
        sub_6AA2C0(CNetManagerSlot1C(GetNetManager()));
        if (obj->member0xBC != 0)
        {
            sub_6281B0((int)obj->member0xBC);
            obj->member0xBC = 0;
        }
        sub_717AB0();
    }

    nullsub_189();
    nullsub_190();

    // The shared 0x6A72F0 epilogue: tear down the net-manager pair.
    if (g_dword_1DC2550 != 0)
    {
        CNetManagerShutdown((void *)g_dword_1DC2550);
        if (g_dword_1DC2554 != 0)
        {
            F3SehBlock *block = (F3SehBlock *)g_dword_1DC2554;
            if (InterlockedDecrement((volatile long *)&block->refcount) == 0)
            {
                block->handler(block->arg);
                LHFree(block);
            }
        }
        g_dword_1DC2554 = 0;
        g_dword_1DC2550 = 0;
    }
}

static void  sub_61C670(void *obj) { (void)obj; }                       // cleanup (part 3)
static long  sub_401700(F3ObjReg *reg) { (void)reg; return 0; }         // free the debug handler

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
//    4. Runs the game (CGameEngineCtor, RunFrontEnd) and sets the exit code
//       (dword_1DBD00C); on success it pumps the component shutdown
//       (CGameEngineShutdown, CGameEngineCleanup), then cleans up
//       (sub_61C670, sub_401700).
// ============================================================
void GameThread(void *a1)
{
    int arg = (int)a1;

    // 1. Thread setup + framework init.
    HeapSetInformation(NULL, (HEAP_INFORMATION_CLASS)1, NULL, 0);
    SetThreadName("GameThread");   // announce the thread name (first-chance SEH raise)
    FrameworkInit();

    // 2. Debug handler (4-byte) + SEH block (LHFreeHandler). The SEH block is
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
        sb->handler  = LHFreeHandler;
        sb->arg      = (int)reg1.object;
    }
    SetDebugHandler(&reg1);   // register the CFableWindowsDebugHandler (0x1A02DD0)

    // 3. 48-byte object (CManagerWin32Ctor) + SEH block (DisposeManager); release local.
    F3ObjReg reg2;
    reg2.object = LHAlloc(48);
    reg2.object = reg2.object ? CManagerWin32Ctor(reg2.object) : 0;
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

    // 4. CDebugTraceFable (8-byte) + SEH block (JmpLHFree); release local.
    F3ObjReg reg3;
    reg3.object = LHAlloc(8);
    if (reg3.object)
    {
        *(void **)reg3.object = g_CDebugTraceFableVtable;
        InitSharedSlot((F3SharedBuffer **)((char *)reg3.object + 4));   // construct the +4 string slot
        SetSaveGamePath((F3SharedBuffer **)(char *)reg3.object + 1, "FTRACE");
    }
    reg3.sehBlock = 0;
    if (reg3.object && (reg3.sehBlock = LHAlloc(12)) != 0)
    {
        F3SehBlock *sb = (F3SehBlock *)reg3.sehBlock;
        sb->refcount = 1;
        sb->handler  = JmpLHFree;
        sb->arg      = (int)reg3.object;
    }
    SetDebugTrace(&reg3);
    F3ReleaseSehRef(reg3.sehBlock);

    // 5. Heap sanity check + clear the game buffer.
    HeapStressTest();
    if (GetGameBuffer() != 0)
        memset(GetGameBuffer(), 0, 0x6C4);

    // 6. Load the shared-memory blob (named after the exe).
    WCHAR Filename[0x800];
    WCHAR Buffer[0x800];
    memset(Filename, 0, sizeof(Filename));
    memset(Buffer, 0, sizeof(Buffer));
    g_byte_1DBD008 = 0;
    if (GetGameBuffer() != 0)
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
                        if (GetGameBuffer() != 0)
                            memcpy(GetGameBuffer(), view, 0x6C4);
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
        const WCHAR *title = GetLocalizedString((const WCHAR ***)GetLocaleEntry(g_dword_1DBD010));
        const WCHAR *body  = GetLocalizedString((const WCHAR ***)GetLocaleEntry(g_dword_1DBD014));
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

    // 8. Run the game. Zero the buffer so the freshly-constructed engine has
    //    well-defined (null) object slots; CGameEngineCtor re-initializes the
    //    rest in place.
    char ArgList[356];
    memset(ArgList, 0, sizeof(ArgList));
    CGameEngineCtor(ArgList);
    if (RunFrontEnd(ArgList, arg) != 0)   // success -> run the component shutdown
    {
        CGameEngineShutdown((CGameEngine *)ArgList);
        CGameEngineCleanup((CGameEngine *)ArgList);
        g_dword_1DBD00C = 0;   // process exit code (returned by RunGame)
    }
    else
    {
        g_dword_1DBD00C = 1;
    }
    sub_61C670(ArgList);

    // 9. Free the debug handler.
    sub_401700(&reg1);
}
