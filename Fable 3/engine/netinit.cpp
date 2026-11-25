// Fable 3 — networking init module implementation (InitNetworking, 0x6A7210,
// plus the CNetManager / CNetLiveSystem globals, the LIONHEAD::StartupNetworking
// network bring-up, and its unimplemented engine callees).

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // original used memset / the CRT directly
#endif

#include "netinit.h"
#include "common/common.h"        // LHAlloc / LHFree
#include "worker.h"               // CWorkerWin32 / GetWorker / SetPreTranslate
#include "object_registry.h"      // F3SehBlock
#include "manager.h"              // GetManager (the CNetVoiceManagerInit manager dispatch)
#include "xlive.h"                // the xlive.dll (XLiveAPI) import declarations
#include "save/save.h"            // SetSaveGamePath (the shared-slot string setter)

#include <windows.h>
#include <process.h>              // atexit
#include <string.h>

// ============================================================
//  Globals referenced by InitNetworking (kept at their original IDs)
// ============================================================
// LIONHEAD::StartupNetworking start count (bumped on success / re-calls).
static DWORD g_dword_1E2D8C0 = 0;   // 0x1E2D8C0
// CNetLiveSystem "constructed once" flag (bit 0 = done).
static DWORD g_dword_1DBE280 = 0;   // 0x1DBE280
// The enable flag handed to LIONHEAD::StartupNetworking (0 = disabled).
static BYTE  g_byte_1C86C17 = 0;    // 0x1C86C17
// The current CNetManager + its SEH cleanup block (the manager pair).
// Non-static (declared extern in netinit.h): CGameEngineCleanup tears the
// pair down at the end of the game thread.
DWORD g_dword_1DC2550 = 0;   // 0x1DC2550
DWORD g_dword_1DC2554 = 0;   // 0x1DC2554

// 0x1DBDDA8 - the global CNetLiveSystem (spans 0x1DBDDA8..0x1DBE280; the
// ctor / dtor touch up through +0x4D7). The stubbed ctor / dtor below treat
// it as a zeroed block with a placeholder vtable and the -1 session handle.
static BYTE g_CNetLiveSystem[0x4D8] = { 0 };   // 0x1DBDDA8

// Placeholder vtables for the objects the (stubbed) ctors construct.
static void *g_CNetLiveSystemVtable[8]       = { 0 };
static void *g_NToolsCBaseObjectVtable[1]    = { 0 };   // NTools::CBaseObject::`vftable' (0x01BF901C)
static void *g_NToolsCBaseClassVtable[1]     = { 0 };   // NTools::CBaseClass::`vftable' (0x01AF6E74)
static void *g_CNetPacketCallbackVtable[8]   = { 0 };

// Forward declarations so the vtable (below) can reference the deleting
// dtor (defined further down).
static void *CNetManagerDtorBody(void *obj);
static void *CNetManagerDtor(void *obj, int freeIt);

// CNetManager vftable: slot 0 = the deleting dtor (sub_6A8600); the rest are
// zero (nothing implemented yet dispatches through them).
static void *g_CNetManagerVtable[8] = { CNetManagerDtor, 0, 0, 0, 0, 0, 0, 0 };
static void *GetCNetManagerVtable(void) { return g_CNetManagerVtable; }

// ============================================================
//  xlive.dll imports
//
//  XLiveInitializeEx / XNetStartup / XOnlineStartup / XPresenceInitialize /
//  XNotifyCreateListener / XLivePreTranslateMessage are the real
//  link-time imports from the bundled xlive.dll (XLiveAPI) -- see
//  engine/xlive.{h,def}: the build links an import library generated from
//  xlive.def (lib /def -> xlive.lib). XLivePreTranslateMessage
//  is installed as the worker's pre-translate hook by InitNetworking;
//  returning FALSE means "not handled" so the worker still translates /
//  dispatches the message.
// ============================================================

// ============================================================
//  LIONHEAD::StartupNetworking  (0x181F4A0 - 0x181F569)
//  [NOT byte-identical] refs the XNetStartup / XOnlineStartup / WSAStartup /
//  WSACleanup IAT slots and the high-VA g_dword_1E2D8C0.
//
//  void LIONHEAD_StartupNetworking(int enable)
//
//  Brings the networking stack up once (later calls only bump the start
//  count): XNetStartup(&cfg) + XOnlineStartup() with the static XNETCONFIG
//  {0D 02 00 00 20 20 FF FF} (byte 1 = 0A when disabled), then
//  WSAStartup(MAKEWORD(2,2)) with a wVersion == 2.2 check (WSACleanup on
//  mismatch). The original passes the config to XNetStartup only and calls
//  XOnlineStartup with no arguments (0x181F506-0x181F50b).
// ============================================================
static void LIONHEAD_StartupNetworking(int enable)
{
    if (g_dword_1E2D8C0 != 0)
    {
        g_dword_1E2D8C0++;
        return;
    }
    BYTE cfg[8] = { 0x0D, 0x02, 0x00, 0x00, 0x20, 0x20, 0xFF, 0xFF };
    if (enable == 0)
        cfg[1] = 0x0A;
    XNetStartup(cfg);
    XOnlineStartup();   // zero-arg call, matching the original (0x181F50b)
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
        return;
    if (wsadata.wVersion != MAKEWORD(2, 2))
    {
        WSACleanup();
        return;
    }
    g_dword_1E2D8C0++;
}

// ============================================================
//  CNetLiveSystemCtor  (0xB76DF0 - 0xB77072)  -- [remaining stub]
//
//  The real ctor is 0x282 bytes of CNetLiveSystem field setup: the vtable,
//  the XSession object at +0x18, the QOS / handle / queue sub-objects at
//  +0x120 / +0x2A0 / +0x3C0 / +0x3DC / +0x44x..+0x47x, the 0x100-byte
//  cache at +0x2A0, and two 0x30-byte sub-objects built from the 0x1CE5674 /
//  0x1CE5678 globals. Out of scope for InitNetworking.
//
//  void CNetLiveSystemCtor(void *obj)
// ============================================================
static void CNetLiveSystemCtor(void *obj)
{
    memset(obj, 0, sizeof(g_CNetLiveSystem));
    *(void **)obj = (void *)g_CNetLiveSystemVtable;
    ((DWORD *)obj)[0x18 / 4] = (DWORD)-1;   // +0x18 session handle (unstarted)
}

// ============================================================
//  CNetLiveSystemDtor  (0xB76AD0 - 0xB76C35)  -- [remaining stub]
//
//  The real dtor frees every CNetLiveSystem sub-object, XNetQosListen /
//  XSessionDeletes the session, and CloseHandles it. Out of scope.
//
//  void CNetLiveSystemDtor(void *obj)
// ============================================================
static void CNetLiveSystemDtor(void *obj)
{
    *(void **)obj = (void *)g_CNetLiveSystemVtable;
    ((DWORD *)obj)[0x18 / 4] = (DWORD)-1;
}

//  CNetLiveSystemAtExit  (0x1AC2490 - 0x1AC249A)  -- the atexit thunk:
//  dispose the global CNetLiveSystem when the process exits.
static void __cdecl CNetLiveSystemAtExit(void)
{
    CNetLiveSystemDtor((void *)g_CNetLiveSystem);
}

// ============================================================
//  CNetLiveSystemNotifyInit  (0xB73150 - 0xB73171)
//  [NOT byte-identical] refs the XNotifyCreateListener / XPresenceInitialize
//  IAT slots.
//
//  int CNetLiveSystemNotifyInit(void *obj)
//
//  Publishes the self pointer at +0xE4, creates the system notification
//  listener (type 2) at +0xF0, and returns XPresenceInitialize(100).
// ============================================================
static int CNetLiveSystemNotifyInit(void *obj)
{
    DWORD *dw = (DWORD *)obj;
    dw[0xE4 / 4] = (DWORD)obj;
    dw[0xF0 / 4] = (DWORD)XNotifyCreateListener(2, 0);
    return XPresenceInitialize(100);
}

// ============================================================
//  CNetManagerCtor  (0x6A6C10 - 0x6A6C6B)  -- construct the 80-byte
//  CNetManager in place (placeholder vtables in this build)
//
//  void *CNetManagerCtor(void *obj)
// ============================================================
static void *CNetManagerCtor(void *obj)
{
    DWORD *dw = (DWORD *)obj;
    // The original first calls sub_18984D0 (an identity no-op).
    dw[0] = (DWORD)g_CNetManagerVtable;                    // +0x00 vtable
    memset(&dw[1], 0, 14 * sizeof(DWORD));                 // +0x04 .. +0x38
    dw[0x3C / 4] = (DWORD)g_CNetPacketCallbackVtable;      // +0x3C CNetPacketCallback (DNameNode base)
    dw[0x44 / 4] = 0;                                      // +0x44
    dw[0x48 / 4] = 0;                                      // +0x48
    dw[0x4C / 4] = 0;                                      // +0x4C
    return obj;
}

// ============================================================
//  DisposeNetManager  (0x6A76C0 - 0x6A76D1)  -- the net-manager SEH handler
//  [NOT byte-identical]
//
//  int DisposeNetManager(int arg)   (arg = the CNetManager, or 0)
//
//  Deleting-destructor dispatch: vtable[0](arg, 1).
//
//  The original is a 17-byte thiscall thunk (obj at [esp+4], `push 1; call
//  [vtable[0]]; retn`) that the caller invokes thiscall-style. This build is
//  driven through the SEH block as `block->handler(block->arg)` (a plain
//  cdecl call, like the sibling DisposeManager/DisposeWorker), so the obj is
//  read from the cdecl arg slot and the deleting destructor is dispatched
//  as a plain call. Functionally identical (destroys + frees the CNetManager).
// ============================================================
static int DisposeNetManager(int arg)
{
    if (arg == 0)
        return 0;
    void **vt = (void **)*(void **)(void *)arg;
    return ((int (*)(void *, int))vt[0])((void *)arg, 1);
}

// ============================================================
//  SetNetManager  (0x6A7A30 - 0x6A7A9E)
//  [NOT byte-identical] refs the InterlockedDecrement / LHFree IAT slots and
//  the relative call to DisposeNetManager.
//
//  void *SetNetManager(void *obj)   (obj = the new CNetManager, or 0)
//
//  The __thiscall + retn 4 setter for the global net-manager pair
//  (dword_1DC2550 / dword_1DC2554). Releases the old pair's SEH block
//  (handler + LHFree when the refcount hits zero), stores the new manager,
//  and — if non-null — wraps it in a fresh 12-byte SEH block (refcount 1,
//  DisposeNetManager, arg). Returns the block (0 when unallocated).
// ============================================================
static void *SetNetManager(void *obj)
{
    if (g_dword_1DC2554 != 0 &&
        InterlockedDecrement((volatile LONG *)g_dword_1DC2554) == 0)
    {
        void *block = (void *)g_dword_1DC2554;
        DisposeNetManager((int)((DWORD *)block)[2]);
        LHFree(block);
    }
    g_dword_1DC2550 = (DWORD)obj;
    g_dword_1DC2554 = 0;
    void *result = 0;
    if (obj != 0)
    {
        F3SehBlock *block = (F3SehBlock *)LHAlloc(12);
        if (block != 0)
        {
            block->refcount = 1;
            block->handler  = DisposeNetManager;
            block->arg      = (int)obj;
            g_dword_1DC2554 = (DWORD)block;
            result = block;
        }
    }
    return result;
}

// ============================================================
//  The CNetLuaEventManager -- the CNetManager's +0x1C sub-object (52 bytes)
//  and its net-event node list
// ============================================================
//
//  Layout (built by CNetLuaEventManagerCtor, sub_6AA3C0):
//    +0x00  vtable (CNetLuaEventManager)
//    +0x04  0
//    +0x08  32-byte internal list object (NetLuaEventListCtor, sub_6AAC00):
//        +0x00  4-byte heap owner wrapper (back-pointer to the list object)
//        +0x18  the 48-byte head node (list head)
//        +0x1C  node count (0 when empty)
//    +0x28  12-byte CNetPacketCallback (DNameNode base): vtable + 0
//    +0x30  back-pointer to the CNetLuaEventManager
//
//  The 48-byte event node (one per registered Lua net event) is a circular
//  list element:
//    +0x00  next       / +0x04  prev       / +0x08  child node
//    +0x0C  F3SharedBuffer* shared slot (released via ReleaseSharedSlot)
//    +0x10  20-byte SEH wrapper list object (see SehBlockListClear)
//    +0x24  payload block (LHFreed on dispose)
//    +0x2C  kind byte (1 at construction)
//    +0x2D  dispose flag: 1 = sentinel (never disposed), 0 = disposable
//
//  The head node is constructed with the kind + flag bytes set, so an empty
//  list is the self-linked head with flag 1.

// Placeholder vtable for the CNetLuaEventManager (the same trick as the
// CNetManager vtable above): slot 0 must be a callable no-op so the
// deleting-dtor dispatch (vt[0](slot, 1)) in CNetManagerDtorBody /
// CNetManagerStart never dereferences an unconstructed table.
static void F3NetStubRelease(void *obj, int freeIt) { (void)obj; (void)freeIt; }
static void *g_CNetLuaEventManagerVtable[2] = { (void *)F3NetStubRelease, 0 };

// ============================================================
//  SehBlockListClear  (0x6AAA50 - 0x6AAAB7)  -- release an SEH-block
//  circular list and reset it to empty
//  [NOT byte-identical] placement-dependent (relative calls to the
//  InterlockedDecrement IAT slot and to LHFree).
//
//  void SehBlockListClear(void *obj)   (thiscall in the original)
//
//  obj:
//    +0x14  circular list of 20-byte wrapper nodes ({+0x00 next, +0x10
//           payload (zeroed), +0x14 F3SehBlock*}), or 0
//    +0x18  node count
//  Re-self-links the head, zeroes the count, then walks the old list from
//  the last element: for each node, InterlockedDecrement the block's
//  refcount (skipping null blocks) and, on zero, call
//  block->handler(block->arg); LHFree the block; zero the node's +0x10 / +0x14;
//  LHFree the node. Stops when the next pointer wraps back to the head (the
//  head itself is never freed).
// ============================================================
static void SehBlockListClear(void *obj)
{
    DWORD *dw = (DWORD *)obj;
    DWORD *head = (DWORD *)dw[0x14 / 4];
    if (head == 0)
        return;
    DWORD *cur = (DWORD *)head[0];
    head[0] = (DWORD)head;
    head[1] = (DWORD)head;
    dw[0x18 / 4] = 0;
    while (cur != head)
    {
        F3SehBlock *block = (F3SehBlock *)cur[0x14 / 4];
        DWORD *next = (DWORD *)cur[0];
        if (block != 0)
        {
            if (InterlockedDecrement((volatile long *)&block->refcount) == 0)
                block->handler(block->arg);
            LHFree(block);
        }
        cur[0x10 / 4] = 0;
        cur[0x14 / 4] = 0;
        LHFree(cur);
        cur = next;
    }
}

// ============================================================
//  NetLuaEventNodeDtor  (0x6AB810 - 0x6AB87A)  -- recursively dispose
//  net-event nodes
//  [NOT byte-identical] placement-dependent (relative self-call + the
//  calls to SehBlockListClear / LHFree / ReleaseSharedSlot).
//
//  The original is thiscall + `retn 4`: the internal list object in ECX
//  (obj+8 of the CNetLuaEventManager) + the node on the stack, callee
//  cleans the arg. This build takes the list object + node as plain
//  arguments (same order, plain call).
//
//  void NetLuaEventNodeDtor(void *listObj, DWORD *node)
//
//  Skips the node when its dispose flag (+0x2D) is set (the sentinel).  A
//  disposable node: recursively dispose the child (+0x08), clear the node's
//  SEH wrapper list (SehBlockListClear on +0x10), LHFree the +0x24 payload
//  (zeroed), LHFree the +0x10 wrapper, ReleaseSharedSlot the +0x0C slot,
//  LHFree the node -- then step to the next (+0x00) and stop when its flag
//  is set (the head).
// ============================================================
static void NetLuaEventNodeDtor(void *listObj, DWORD *node)
{
    for (;;)
    {
        if (((BYTE *)node)[0x2D] != 0)
            break;
        NetLuaEventNodeDtor(listObj, (DWORD *)node[0x08 / 4]);
        DWORD *next = (DWORD *)node[0];
        SehBlockListClear((char *)node + 0x10);
        LHFree((void *)node[0x24 / 4]);
        node[0x24 / 4] = 0;
        LHFree((void *)node[0x10 / 4]);
        ReleaseSharedSlot((F3SharedBuffer **)((char *)node + 0x0C));
        LHFree(node);
        node = next;
    }
}

// ============================================================
//  CNetLuaEventManagerListReset  (0x6AA2C0 - 0x6AA2EC)  -- reset the
//  CNetLuaEventManager's event list to empty
//  [NOT byte-identical] placement-dependent (relative call to
//  NetLuaEventNodeDtor).
//
//  DWORD *CNetLuaEventManagerListReset(void *obj)   (thiscall in the
//  original; returns the head node, discarded by both callers)
//
//  obj = the 52-byte CNetLuaEventManager (the CNetManager's +0x1C
//  sub-object).  Disposes the nodes on the list (NetLuaEventNodeDtor on
//  the list object at +0x08, starting at the head node's next field), then
//  restores the 48-byte head node at +0x20 to the empty self-linked state
//  ({head, head, head}) and zeroes the count at +0x24.
// ============================================================
DWORD *CNetLuaEventManagerListReset(void *obj)
{
    DWORD *node = (DWORD *)((DWORD *)obj)[0x20 / 4];
    NetLuaEventNodeDtor((char *)obj + 0x08, (DWORD *)node[1]);
    node[1] = (DWORD)node;
    ((DWORD *)obj)[0x24 / 4] = 0;
    node[0] = (DWORD)node;
    node[2] = (DWORD)node;
    return node;
}

// ============================================================
//  NetLuaEventListCtor  (sub_6AAC00, 0x6AAC00 - 0x6AAC72)  -- construct
//  the 32-byte internal list object in place
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc).
//
//  The original is thiscall + `retn 8` (the list object in ECX + two
//  stack args). The 4-byte heap owner wrapper at +0x00 (back-pointer to
//  the list object), the 48-byte head node at +0x18 (the first three
//  words zeroed -- the game's LHAlloc zeroes the rest of the 48 bytes --
//  byte +0x2C = 1, then the dispose flag +0x2D = 1), the count at +0x1C =
//  0, and the head self-linked ({node, node, node}). This build memsets
//  the node because the test heap (malloc) is not zeroed.
// ============================================================
static void NetLuaEventListCtor(void *obj)
{
    DWORD *dw = (DWORD *)obj;
    void *wrap = LHAlloc(4);
    if (wrap != 0)
        *(DWORD *)wrap = (DWORD)obj;
    dw[0] = (DWORD)wrap;
    DWORD *node = (DWORD *)LHAlloc(48);
    if (node != 0)
    {
        memset(node, 0, 48);
        ((BYTE *)node)[0x2C] = 1;
        dw[6] = (DWORD)node;
        ((BYTE *)node)[0x2D] = 1;
        node[1] = (DWORD)node;
        node[0] = (DWORD)node;
        node[2] = (DWORD)node;
    }
    else
        dw[6] = 0;
    dw[7] = 0;
}

// ============================================================
//  CNetLuaEventManagerCtor  (sub_6AA3C0, 0x6AA3C0 - 0x6AA438)  --
//  construct the 52-byte CNetLuaEventManager in place
//  [NOT byte-identical] placement-dependent (relative calls to
//  CBaseClassNonCopyableCtor / NetLuaEventListCtor / DNameNode / GetNetManager).
//
//  void *CNetLuaEventManagerCtor(void *obj)
//
//  The original: the base ctor + CNetLuaEventManager vtable, +0x04 = 0,
//  NetLuaEventListCtor at +0x08, the DNameNode base at +0x28 (vtable
//  re-stored to CNetPacketCallback, +0x08 = 0), GetNetManager() +
//  sub_6A6820 (registration -- remaining stub, no-op), the +0x30
//  back-pointer, and a reader-writer-lock + PauseDebugRegisterPacket(386, ...) global
//  registration (unimplemented registry -- skipped here). This build
//  stores the placeholder vtable instead of the CNetLuaEventManager one.
// ============================================================
static void *CNetLuaEventManagerCtor(void *obj)
{
    *(void **)obj = (void *)g_CNetLuaEventManagerVtable;   // +0x00 vtable
    ((DWORD *)obj)[1] = 0;                                 // +0x04
    NetLuaEventListCtor((char *)obj + 0x08);               // +0x08 internal list
    ((DWORD *)obj)[0x28 / 4] = (DWORD)g_CNetPacketCallbackVtable;  // +0x28 DNameNode base vtable
    ((DWORD *)obj)[0x2C / 4] = 0;                          // +0x2C node (0)
    ((DWORD *)obj)[0x30 / 4] = (DWORD)obj;                 // +0x30 back-pointer
    return obj;
}

// ============================================================
//  CNetManagerStart  (0x6A6DB0 - 0x6A70E3)  -- [partial stub]
//
//  The real function is 0x333 bytes: it constructs the CNetManager's ~15
//  sub-objects (LHAlloc + a per-class ctor into each +0x04..+0x5C slot,
//  releasing the old one through its deleting destructor), wires the packet
//  callback + reader-writer lock, and returns PauseDebugRegisterPacket(535, ...).
//  This build constructs only the +0x1C sub-object (the 52-byte
//  CNetLuaEventManager, sub_6AA3C0) -- the slot CNetLuaEventManagerListReset
//  (sub_6AA2C0) reads -- and leaves the rest stubbed. Returns 0 (success).
//
//  int CNetManagerStart(void *obj)
// ============================================================
static int CNetManagerStart(void *obj)
{
    DWORD *dw = (DWORD *)obj;
    if (dw[0x1C / 4] != 0)
    {
        void **vt = (void **)*(void **)dw[0x1C / 4];
        ((void (*)(void *, int))vt[0])((void *)dw[0x1C / 4], 1);   // deleting dtor
    }
    void *sub = LHAlloc(52);
    if (sub != 0)
        CNetLuaEventManagerCtor(sub);
    dw[0x1C / 4] = (DWORD)sub;
    return 0;
}

// The CNetManagerShutdown direct-release callees (remaining stubs): the
// +0x10 sub-object release (thiscall + a 0 flag), the +0x34 sub-object
// release, and the +0x10 sub-object dtor. All slots are zero in this build,
// so the only observable effect is the slot zeroing + LHFree calls below.
static void CNetMgr10Release(void *obj, int flag) { (void)flag; (void)obj; }   // sub_6834D0
static void CNetMgr34Release(void *obj) { (void)obj; }                          // sub_6A84C0
static void CNetMgr10Dtor(void *obj) { (void)obj; }                             // sub_692830

// Release a vtable object through vtable slot 0 with a 1 (the original's
// `obj->vtable[0](obj, 1)` dispatch).
static void CNetReleaseOne(void *o)
{
    void **vt = (void **)*(void **)o;
    ((void (*)(void *, int))vt[0])(o, 1);
}

// ============================================================
//  CNetManagerShutdown  (0x6A70F0 - 0x6A720C)  -- the 80-byte CNetManager
//  teardown (CGameEngineCleanup's net-manager stop)
//  [NOT byte-identical] placement-dependent (relative calls to
//  CNetMgr10Release / CNetMgr34Release / CNetMgr10Dtor / LHFree and the
//  sub-object vtable dispatches).
//
//  void CNetManagerShutdown(void *obj)
//
//  Tears the net manager down in place: releases the +0x10 sub-object
//  (sub_6834D0(slot, 0), unconditional) and LHFrees the +0x38 sub-object
//  (unconditional), then releases the +0x34 sub-object (sub_6A84C0 + LHFree),
//  +0x20/+0x1C/+0x18/+0x14/+0x0C/+0x48/+0x08/+0x04 (vtable[0](slot, 1)), the
//  +0x10 sub-object (sub_692830 + LHFree), and +0x2C/+0x24/+0x28
//  (vtable[0](slot, 1)) -- every slot zeroed. Returns nothing (the return
//  register is discarded by the sole caller).
// ============================================================
void CNetManagerShutdown(void *obj)
{
    DWORD *dw = (DWORD *)obj;

    CNetMgr10Release((void *)dw[0x10 / 4], 0);
    LHFree((void *)dw[0x38 / 4]);
    dw[0x38 / 4] = 0;

    if (dw[0x34 / 4] != 0)
    {
        CNetMgr34Release((void *)dw[0x34 / 4]);
        LHFree((void *)dw[0x34 / 4]);
    }
    dw[0x34 / 4] = 0;

    if (dw[0x20 / 4] != 0) { CNetReleaseOne((void *)dw[0x20 / 4]); } dw[0x20 / 4] = 0;
    if (dw[0x1C / 4] != 0) { CNetReleaseOne((void *)dw[0x1C / 4]); } dw[0x1C / 4] = 0;
    if (dw[0x18 / 4] != 0) { CNetReleaseOne((void *)dw[0x18 / 4]); } dw[0x18 / 4] = 0;
    if (dw[0x14 / 4] != 0) { CNetReleaseOne((void *)dw[0x14 / 4]); } dw[0x14 / 4] = 0;
    if (dw[0x0C / 4] != 0) { CNetReleaseOne((void *)dw[0x0C / 4]); } dw[0x0C / 4] = 0;
    if (dw[0x48 / 4] != 0) { CNetReleaseOne((void *)dw[0x48 / 4]); } dw[0x48 / 4] = 0;
    if (dw[0x08 / 4] != 0) { CNetReleaseOne((void *)dw[0x08 / 4]); } dw[0x08 / 4] = 0;
    if (dw[0x04 / 4] != 0) { CNetReleaseOne((void *)dw[0x04 / 4]); } dw[0x04 / 4] = 0;

    if (dw[0x10 / 4] != 0)
    {
        CNetMgr10Dtor((void *)dw[0x10 / 4]);
        LHFree((void *)dw[0x10 / 4]);
    }
    dw[0x10 / 4] = 0;

    if (dw[0x2C / 4] != 0) { CNetReleaseOne((void *)dw[0x2C / 4]); } dw[0x2C / 4] = 0;
    if (dw[0x24 / 4] != 0) { CNetReleaseOne((void *)dw[0x24 / 4]); } dw[0x24 / 4] = 0;
    if (dw[0x28 / 4] != 0) { CNetReleaseOne((void *)dw[0x28 / 4]); } dw[0x28 / 4] = 0;
}

// ============================================================
//  CNetPacketCallbackDtor  (0x1898510 - 0x1898553)  -- the CNetPacketCallback
//  (DNameNode base) dtor
//  [NOT byte-identical] refs the high-VA vtables (NTools::CBaseObject at
//  0x01BF901C, NTools::CBaseClass at 0x01AF6E74) and the heap table
//  (dword_2075B24, 0x02075B24) at their original high VAs.
//
//  void *CNetPacketCallbackDtor(void *obj)
//
//  thiscall on the 8-byte DNameNode-base object (the CNetManager's +0x3C / the
//  CNetSaveGame's +0x2C0 CNetPacketCallback: +0x00 vtable, +0x04 name-node
//  pointer). The shared base dtor of the whole CNetPacketCallback class family
//  (~500 call sites in the original): store the NTools::CBaseObject vtable;
//  if the +4 node is live, store 0 into the node's own vtable slot (the
//  dead-object marker) and, when the node's +4 field is 0, free the node
//  through the heap table's free slot (the original's
//  `mov eax, dword_2075B24; mov ecx, [eax+10h]; call ecx` -- LHFree here),
//  then zero the +4 slot; store the NTools::CBaseClass vtable and zero +4
//  again. thiscall; returns the tracked EAX (0 if the node was null, else the
//  node -- in the free branch the original returns the free's eax, which is
//  unobservable: every caller discards it). Called from CNetManagerDtorBody
//  (the +0x3C callback) and CNetSaveGameDtor (the +0x2C0 callback); the
//  extern declaration is in netinit.h.
// ============================================================
void *CNetPacketCallbackDtor(void *obj)
{
    char *b = (char *)obj;
    void *node = *(void **)(b + 4);

    *(void **)b = g_NToolsCBaseObjectVtable;   // NTools::CBaseObject::`vftable' (0x01BF901C)
    if (node != 0)
    {
        *(void **)node = 0;                    // dead-object marker in the node
        node = *(void **)(b + 4);              // reloaded (same value)
        if (*(void **)((char *)node + 4) == 0)
            LHFree(node);                      // the original's [dword_2075B24+16] indirect free
        *(void **)(b + 4) = 0;
    }
    *(void **)b = g_NToolsCBaseClassVtable;    // NTools::CBaseClass::`vftable' (0x01AF6E74)
    *(void **)(b + 4) = 0;
    return node;
}

// 0x18984C0 -- the NTools::CBaseClass base dtor (vtable store).
static void CNetBaseClassDtor(void *obj)
{
    *(void **)obj = g_NToolsCBaseClassVtable;
}

// ============================================================
//  CNetManagerDtorBody  (0x6A6C70 - 0x6A6DAA)  -- the 80-byte CNetManager
//  dtor body (restores the vtable first, runs the base dtor last)
//  [NOT byte-identical] placement-dependent (relative calls to the
//  CNetPacketCallback dtor, LHFree, the sub-object vtable dispatches, and
//  the tail jmp CBaseClassDtor).
//
//  void *CNetManagerDtorBody(void *obj)
//
//  Restores the CNetManager vtable, then releases in order: +0x48
//  (vtable[0](slot, 1)), the +0x3C packet callback (CNetPacketCallbackDtor),
//  the +0x38 sub-object (LHFree, unconditional), +0x34 (sub_6A84C0 + LHFree),
//  +0x30/+0x2C/+0x28/+0x24/+0x20/+0x1C/+0x18/+0x14 (vtable[0](slot, 1)),
//  +0x10 (sub_692830 + LHFree), and +0x0C/+0x08/+0x04
//  (vtable[0](slot, 1)) -- every slot zeroed -- and tail-calls the
//  NTools::CBaseClass base dtor.
// ============================================================
static void *CNetManagerDtorBody(void *obj)
{
    DWORD *dw = (DWORD *)obj;
    *(void **)obj = GetCNetManagerVtable();   // restore the CNetManager vtable

    if (dw[0x48 / 4] != 0) { CNetReleaseOne((void *)dw[0x48 / 4]); } dw[0x48 / 4] = 0;
    CNetPacketCallbackDtor((char *)obj + 0x3C);   // sub_1898510
    LHFree((void *)dw[0x38 / 4]);
    dw[0x38 / 4] = 0;
    if (dw[0x34 / 4] != 0)
    {
        CNetMgr34Release((void *)dw[0x34 / 4]);
        LHFree((void *)dw[0x34 / 4]);
    }
    dw[0x34 / 4] = 0;
    if (dw[0x30 / 4] != 0) { CNetReleaseOne((void *)dw[0x30 / 4]); } dw[0x30 / 4] = 0;
    if (dw[0x2C / 4] != 0) { CNetReleaseOne((void *)dw[0x2C / 4]); } dw[0x2C / 4] = 0;
    if (dw[0x28 / 4] != 0) { CNetReleaseOne((void *)dw[0x28 / 4]); } dw[0x28 / 4] = 0;
    if (dw[0x24 / 4] != 0) { CNetReleaseOne((void *)dw[0x24 / 4]); } dw[0x24 / 4] = 0;
    if (dw[0x20 / 4] != 0) { CNetReleaseOne((void *)dw[0x20 / 4]); } dw[0x20 / 4] = 0;
    if (dw[0x1C / 4] != 0) { CNetReleaseOne((void *)dw[0x1C / 4]); } dw[0x1C / 4] = 0;
    if (dw[0x18 / 4] != 0) { CNetReleaseOne((void *)dw[0x18 / 4]); } dw[0x18 / 4] = 0;
    if (dw[0x14 / 4] != 0) { CNetReleaseOne((void *)dw[0x14 / 4]); } dw[0x14 / 4] = 0;
    if (dw[0x10 / 4] != 0)
    {
        CNetMgr10Dtor((void *)dw[0x10 / 4]);
        LHFree((void *)dw[0x10 / 4]);
    }
    dw[0x10 / 4] = 0;
    if (dw[0x0C / 4] != 0) { CNetReleaseOne((void *)dw[0x0C / 4]); } dw[0x0C / 4] = 0;
    if (dw[0x08 / 4] != 0) { CNetReleaseOne((void *)dw[0x08 / 4]); } dw[0x08 / 4] = 0;
    if (dw[0x04 / 4] != 0) { CNetReleaseOne((void *)dw[0x04 / 4]); } dw[0x04 / 4] = 0;

    CNetBaseClassDtor(obj);   // tail jmp CBaseClassDtor
    return obj;
}

// ============================================================
//  CNetManagerDtor  (0x6A8600 - 0x6A861B)  -- the CNetManager deleting dtor
//  (vtable slot 0; DisposeNetManager's dispatch target)
//  [NOT byte-identical] placement-dependent (relative calls to the dtor body
//  and LHFree; thiscall + `retn 4`).
//
//  The original is thiscall + `retn 4` (obj in ECX, freeIt on the stack,
//  callee cleans the arg). This build dispatches it as a plain call through
//  the vtable (see DisposeNetManager), so it is a plain function taking the
//  obj explicitly. The body + free step matches the original.
// ============================================================
static void *CNetManagerDtor(void *obj, int freeIt)
{
    CNetManagerDtorBody(obj);
    if (freeIt & 1)
        LHFree(obj);
    return obj;
}

// ============================================================
//  InitNetworking  (0x6A7210 - 0x6A72EF)  -- the network-subsystem
//  bootstrap (RunFrontEnd calls it first, right after InitEngineTime)
//  [NOT byte-identical] refs the GetUserDefaultLangID / XLiveInitializeEx /
//  atexit IAT slots and the high-VA globals byte_1C86C17 / dword_1DBE280 /
//  unk_1DBDDA8 / dword_1DC2550.
//
//  int InitNetworking(void)
// ============================================================
int InitNetworking(void)
{
    // 1. Build the 32-byte XLive settings block (dwSize = 28, wLanguageId =
    //    the user's default language id) and call XLiveInitializeEx
    //    (XNA 3.2 version flags).
    DWORD settings[8];
    memset(settings, 0, sizeof(settings));
    settings[0] = 28;
    settings[0x10 / 4] = (DWORD)GetUserDefaultLangID();
    XLiveInitializeEx(settings, 0x32000300);

    // 2. Install XLivePreTranslateMessage as the worker's pre-translate hook.
    //    (The original assumes the live worker RunGame created before
    //    spawning the game thread; the standalone test harness calls
    //    GameThread with no worker, so skip when there is none.)
    void *worker = GetWorker();
    if (worker != 0)
        SetPreTranslate(worker, (F3PreTranslateFn)XLivePreTranslateMessage);

    // 3. Bring up the LIONHEAD networking stack (XNet / XOnline / Winsock).
    LIONHEAD_StartupNetworking((int)g_byte_1C86C17);

    // 4. One-time CNetLiveSystem construction + process-exit teardown, then
    //    the presence / notification init.
    if ((g_dword_1DBE280 & 1) == 0)
    {
        g_dword_1DBE280 |= 1;
        CNetLiveSystemCtor((void *)g_CNetLiveSystem);
        atexit(CNetLiveSystemAtExit);
    }
    CNetLiveSystemNotifyInit((void *)g_CNetLiveSystem);

    // 5. Allocate + construct the 80-byte CNetManager, publish it as the
    //    current net-manager pair, and start it.
    void *mgr = LHAlloc(80);
    mgr = (mgr != 0) ? CNetManagerCtor(mgr) : 0;
    SetNetManager(mgr);
    return CNetManagerStart((void *)g_dword_1DC2550);
}

// ============================================================
//  GetNetManager  (sub_6A66C0)  -- the current CNetManager getter
//  [NOT byte-identical] placement-dependent (absolute ref to the global).
//
//  void *GetNetManager(void)
//
//  Returns the current CNetManager (g_dword_1DC2550), or 0 when no net
//  manager is installed.
// ============================================================
void *GetNetManager(void)
{
    return (void *)g_dword_1DC2550;
}

// ============================================================
//  CNetManagerGetUserManager  (0x6A6960 - 0x6A6964)  -- the
//  CNetManager's +0x28 CNetUserManager getter
//
//  void *CNetManagerGetUserManager(void *obj)   (obj = the CNetManager)
//
//  thiscall getter: returns *(obj + 0x28), the 60-byte CNetUserManager
//  (the XLive 4-user sign-in-state object; CNetManagerStart constructs it
//  with LHAlloc(60) + sub_772E50). ~100 call sites, all of the form
//  GetNetManager() -> this getter -> a CNetUserManager method (the
//  sub_771xxx / sub_772xxx cluster: user sign-in / connection queries,
//  XNotify pumping). The offset siblings (0x6A6900..0x6A69F0, two per
//  offset) are the compiler-generated const overloads; 0x6A6960 is the
//  live one for +0x28 (its twin 0x6A6970 is unreferenced).
//
//  Byte-identical: the original 4 bytes are `mov eax,[ecx+28h]; retn`
//  (no calls, no globals, no absolute refs). `__declspec(naked)` inline
//  asm so /Ob2 doesn't swap the out-of-line body for a 5-byte stack-
//  argument thunk.
// ============================================================
__declspec(naked)
void *CNetManagerGetUserManager(void *obj)
{
    __asm {
        mov eax, [ecx+28h]
        retn
    }
}

// ============================================================
//  CNetManagerGetVoiceManager  (0x6A69A0 - 0x6A69A4)  -- the
//  CNetManager's +0x30 CNetVoiceManager getter
//
//  void *CNetManagerGetVoiceManager(void *obj)   (obj = the CNetManager)
//
//  thiscall getter: returns *(obj + 0x30), the 104-byte CNetVoiceManager
//  (the XLive voice object; CNetManagerStart constructs it with
//  LHAlloc(104) + sub_7425C0). Its methods are the sub_741xxx / sub_742xxx
//  cluster: the XNotify-listener + XHV-engine init (sub_741E50), the
//  4-user voice enable pass (sub_741DD0) and the voice packet handlers.
//  5 call sites: RunFrontEnd (0x61D48E: GetNetManager() -> this getter ->
//  sub_741E50 init) and four Lua/net-state handlers (0x1330B4F, 0x13E6D95,
//  0x13E7155, 0x13E728D) that stage the manager in ECX and read it back.
//  The offset siblings (0x6A69A0 / 0x6A69B0, two per offset) are the
//  compiler-generated const overloads; 0x6A69A0 is the live one for +0x30
//  (its twin 0x6A69B0 is unreferenced).
//
//  Byte-identical: the original 4 bytes are `mov eax,[ecx+30h]; retn`
//  (no calls, no globals, no absolute refs). `__declspec(naked)` inline
//  asm so /Ob2 doesn't swap the out-of-line body for a 5-byte stack-
//  argument thunk.
// ============================================================
__declspec(naked)
void *CNetManagerGetVoiceManager(void *obj)
{
    __asm {
        mov eax, [ecx+30h]
        retn
    }
}

// ============================================================
//  CNetUserManagerRefreshSignin  (0x00772190 - 0x00772421)  --
//  Re-read local user 0's XLive sign-in state and refresh the
//  CNetUserManager's first 32-byte sign-in record
//
//  void CNetUserManagerRefreshSignin(void *obj)   (obj = the 60-byte
//  CNetUserManager, the CNetManager's +0x28; thiscall in the original)
//
//  [NOT byte-identical] refs the xlive.dll IAT slot (XUserGetSigninInfo,
//  ordinal 5267), calls SetSaveGamePath, and the original's MSVC
//  prologue/epilogue (__security_check_cookie, __invalid_parameter_noinfo
//  bounds checks) all relocate to different offsets in this build.
//
//  The CNetUserManager keeps its sign-in records in an array of 32-byte
//  elements (obj+0x14 = start, obj+0x18 = end). The original MSVC build
//  bounds-checks element 0 before every store: when (end-start) holds no
//  full 32-byte element it calls __invalid_parameter_noinfo and keeps
//  going. The CNetUserManager always holds the 4-user array, so the check
//  always passes; it is reproduced here as a single up-front guard.
//
//  XUserGetSigninInfo's sign-in buffer, first 16 bytes used (the original
//  zeroes a 32-byte local before the calls):
//      +0  XUID high     +4  XUID low
//      +8  flags  (bit 1 -> record+0x02)
//      +12 state  (== 1 / == 2 -> record+0x01 / +0x00)
//
//  Behavior (user index 0 throughout):
//    1. XUserGetSigninInfo(0, 2, info). When it fails (nonzero return),
//       the record is cleared: the state / flag bytes and all XUID words
//       zeroed, and the save-game path slot assigned the empty string.
//       The original passes byte_1BCE523 (a zeroed .rdata region read as
//       an empty string); either way SetSaveGamePath releases the previous
//       block and stores null.
//    2. On success:
//         record+0x10 / +0x14 <- info's XUID;
//         the save-game path slot <- the empty string (the original's
//         16-byte zeroed stack buffer);
//         signState2 = (state == 2), signState1 = (state == 1),
//         signFlag   = (flags & 2) != 0;
//         when XUserGetSigninInfo(0, 1, info) succeeds, record+0x18 /
//         +0x1C <- that call's XUID;
//         record+0x08 / +0x0C <- (the +0x18/+0x1C XUID nonzero) ? it :
//         (the +0x10/+0x14 XUID).
//
//  Callers: RunFrontEnd (0x61D1B3: right after the local user signs in,
//  CNetUserManagerRefreshSignin(CNetManagerGetUserManager(GetNetManager()))),
//  the XNotify sign-in pump (0x7724BA, event type 10) and the listener
//  setup tail call (0x772EFA, sub_772ED0).
// ============================================================
void CNetUserManagerRefreshSignin(void *obj)
{
    // The original unconditionally dereferences the CNetUserManager that
    // CNetManagerStart (0x6A6DB0) constructs (LHAlloc(60) + sub_772E50,
    // published at the CNetManager's +0x28). In this build CNetManagerStart
    // is a remaining stub, so the user manager is absent (the slot stays 0)
    // and the object is null; the original would fault reading +0x14/+0x18
    // in that case, so an absent manager is treated as a no-op.
    if (obj == 0)
        return;

    unsigned char *o = (unsigned char *)obj;
    unsigned int  start = *(unsigned int *)(o + 0x14);   // sign-in array start
    unsigned int  end   = *(unsigned int *)(o + 0x18);   // sign-in array end
    F3NetUserSignin *rec = (F3NetUserSignin *)start;     // element 0

    // The 16-byte sign-in buffer XUserGetSigninInfo fills (zeroed up front,
    // as the original does).
    unsigned int info[4] = { 0, 0, 0, 0 };

    // The original's per-store element-0 bounds check ((end-start) >> 5 must
    // be nonzero, else __invalid_parameter_noinfo). Guarded once here; the
    // 4-user array is always present, so this never fires.
    if (((end - start) >> 5) == 0)
        return;

    if (XUserGetSigninInfo(0, 2, info) != 0)
    {
        // No sign-in info (or error): clear the record.
        rec->signState2 = 0;                     // +0x00
        rec->signState1 = 0;                     // +0x01
        rec->xuidHi     = 0;  rec->xuidLo     = 0;   // +0x08 / +0x0C
        rec->xuidB_hi   = 0;  rec->xuidB_lo   = 0;   // +0x18 / +0x1C
        rec->xuidA_hi   = 0;  rec->xuidA_lo   = 0;   // +0x10 / +0x14
        SetSaveGamePath((F3SharedBuffer **)&rec->savePath, "");   // +0x04 <- empty
        rec->signFlag   = 0;                     // +0x02
    }
    else
    {
        rec->xuidA_hi = info[0];                 // +0x10 <- info XUID high
        rec->xuidA_lo = info[1];                 // +0x14 <- info XUID low
        SetSaveGamePath((F3SharedBuffer **)&rec->savePath, "");   // +0x04 <- empty
        rec->signState2 = (info[3] == 2);        // +0x00
        rec->signState1 = (info[3] == 1);        // +0x01
        rec->signFlag   = (info[2] & 2) != 0;    // +0x02

        if (XUserGetSigninInfo(0, 1, info) == 0) {
            rec->xuidB_hi = info[0];             // +0x18 <- previous XUID high
            rec->xuidB_lo = info[1];             // +0x1C <- previous XUID low
        }

        if ((rec->xuidB_hi | rec->xuidB_lo) != 0) {
            rec->xuidHi = rec->xuidB_hi;         // +0x08
            rec->xuidLo = rec->xuidB_lo;         // +0x0C
        }
        else {
            rec->xuidHi = rec->xuidA_hi;         // +0x08
            rec->xuidLo = rec->xuidA_lo;         // +0x0C
        }
    }
}

// ============================================================
//  CNetVoiceManager  (the CNetManager's +0x30 104-byte voice manager)
//
//  CNetVoiceManagerInit (sub_741E50 - 0x741F1B, thiscall): the voice bring-up
//  (XNotifyCreateListener + XHVCreateEngine + the 4-user voice enable/reset
//  pass). See netinit.h for the full description.
//
//  CNetManagerGetSlot24 (sub_6A6940 - 0x6A6944, thiscall): the CNetManager
//  +0x24 sub-object getter (byte-identical).
//
//  The success-path callees CNetVoiceRouteInit (sub_C1AC70),
//  CNetVoiceEnableUsers (sub_741DD0) and CNetVoiceResetUsers (sub_741440)
//  are faithful ports of the original control flow; their deep callees (the
//  route-list / sign-in / mute-list cluster) are the remaining no-op stubs
//  below.
// ============================================================

__declspec(naked) void *CNetManagerGetSlot24(void *obj)
{
    __asm {
        mov eax, [ecx+24h]   // sub_6A6940: mov eax, [ecx+24h]; retn
        retn
    }
}

// --- the deep callees (remaining no-op stubs; the voice route-list / sign-in
//     / mute-list cluster is out of scope). Signatures match the original
//     thiscall + retn-N conventions so the faithful ports above compile. ---

// sub_C1D950 (6 args): the voice route-list tree lookup. Fills *out =
// { input, found-node } and returns out. The stub reports "not found"
// (found == 0) so the caller's insert branch is skipped.
static DWORD *C1D950Lookup(DWORD *out, DWORD input, DWORD a3, DWORD a4, DWORD a5, int a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    out[0] = input;   // a2 (the input key)
    out[1] = 0;       // not found
    return out;
}

// sub_C1DDD0 (stdcall, 3 args): inserts a route into the user's packet list.
static void C1DDD0List(DWORD a1, DWORD a2, int *a3)
{
    (void)a1; (void)a2; (void)a3;
}

// sub_771DB0 (thiscall + retn 4): is the user present and unmuted? The
// CNetUserManager 32-byte record (start = *(um+0x14), end = *(um+0x18),
// 32-byte elements): state byte != 0 and sign-in flag byte == 0.
static int CNetUserManagerUserPresent(void *um, int idx)
{
    if (um == 0 || idx > 3) return 0;
    DWORD *umdw = (DWORD *)um;
    DWORD start = umdw[5];   // *(um+0x14): user array start
    DWORD end   = umdw[6];   // *(um+0x18): user array end
    if ((DWORD)idx >= (end - start) / 32) return 0;   // bounds
    const unsigned char *r = (const unsigned char *)(start + 32 * idx);
    return (r[0] != 0 && r[2] == 0) ? 1 : 0;
}

// sub_741770 (thiscall + retn 4): enable voice for user idx.
static void CNetVoiceEnableUser(void *obj, int idx)
{
    (void)obj; (void)idx;
}

// sub_7412C0 (thiscall + retn 4): clear/reset the user voice slot for idx.
static void CNetVoiceClearUser(void *obj, int idx)
{
    (void)obj; (void)idx;
}

// sub_740430 (thiscall + retn 8): is the source user muted for this route?
static int CNetVoiceMutedCheck(void *obj, int slot, int child)
{
    (void)obj; (void)slot; (void)child;
    return 0;
}

// sub_740790 (thiscall + retn 12): is the (child, child1) route present in
// the user's +0x10 packet list?
static int CNetVoiceFindRoute(void *obj, unsigned i, unsigned child, unsigned child1)
{
    (void)obj; (void)i; (void)child; (void)child1;
    return 0;
}

// sub_740CE0 (thiscall + retn 12): erase the (child, child1) route from the
// user's +0x10 packet list.
static void CNetVoiceEraseRoute(void *obj, unsigned i, int child, int child1)
{
    (void)obj; (void)i; (void)child; (void)child1;
}

// sub_745970 (thiscall + retn 4): append the route to the user's +0x10
// packet list (growable; sub_7455E0).
static int CNetVoiceListAppend(void *obj, void *val)
{
    (void)obj; (void)val;
    return 0;
}

// sub_742BE0 (thiscall): advance the voice route-list iterator (the node
// sentinel at +0x2D).
static void *CNetVoiceListNext(void *list)
{
    return list;
}

// sub_741160 (thiscall + retn 4): send the CNetVoiceMuteListPacket for user
// idx (iterates the voice packet list, sub_740580 per node).
static int CNetVoiceSendMuteList(void *obj, int idx)
{
    (void)obj; (void)idx;
    return 0;
}

// sub_740DB0 (thiscall): the main per-user voice packet processor (iterates
// the 4 user slots + the voice packet list, XHV engine vtable[18] per
// packet).
static void CNetVoiceSyncRoutes(void *obj)
{
    (void)obj;
}

static void CNetVoiceRouteInit(void *obj, int a2);
static int  CNetVoiceEnableUsers(void *obj);
static int  CNetVoiceResetUsers(void *obj);

// ---------------------------------------------------------------------------
// void CNetVoiceRouteInit(void *obj, int a2)   (sub_C1AC70 - 0xC1AD05,
// thiscall + retn 4): the voice route-init lookup.
//
// obj = the CNetManager's +0x24 voice route/state sub-object. Reads
// v7 = *(obj+0x24) and v8 = *(obj+0x38), runs the route-list tree lookup
// (C1D950Lookup), and on a match inserts the route into the user's list
// (C1DDD0List). Faithful port of the original control flow; the two deep
// callees are remaining no-op stubs.
// ---------------------------------------------------------------------------
static void CNetVoiceRouteInit(void *obj, int a2)
{
    if (obj == 0) return;
    DWORD *dw = (DWORD *)obj;
    DWORD v7 = dw[9];       // *(obj+0x24)
    DWORD v8 = dw[14];      // *(obj+0x38)
    DWORD v9[8];
    DWORD *v4 = C1D950Lookup(v9, v7, v8, v7, v8, a2);
    if (v4 == 0 || *v4 == 0 || *v4 != v7)
        return;   // __invalid_parameter_noinfo in the original
    if (v4[1] == v8)
        C1DDD0List(v7, v8, &a2);   // sub_C1DDD0(*(obj+0x24), *(obj+0x38), &a2)
}

// ---------------------------------------------------------------------------
// int CNetVoiceEnableUsers(void *obj)   (sub_741DD0 - 0x741E4E, thiscall):
// the 4-user voice enable pass.
//
// For each of the 4 local users (48-byte slots in the CNetVoiceManager's
// user array, start at obj+0x1C, end at obj+0x20): if the user is present
// (CNetUserManagerUserPresent), enable voice for the slot (CNetVoiceEnable-
// User); otherwise, if the slot's +4 flag is set, clear the slot
// (CNetVoiceClearUser). Tail-calls CNetVoiceResetUsers (sub_741440).
// Faithful port of the original control flow.
// ---------------------------------------------------------------------------
static int CNetVoiceEnableUsers(void *obj)
{
    if (obj == 0) return 0;
    DWORD *dw = (DWORD *)obj;
    void *um = CNetManagerGetUserManager(GetNetManager());   // the +0x28 user manager
    for (int i = 0; i < 4; ++i)
    {
        if (CNetUserManagerUserPresent(um, i))
        {
            CNetVoiceEnableUser(obj, i);   // sub_741770
        }
        else
        {
            if ((DWORD)i >= (dw[8] - dw[7]) / 48)
                return 0;   // __invalid_parameter_noinfo in the original
            if (*(unsigned char *)(dw[7] + (DWORD)i * 48 + 4) != 0)
                CNetVoiceClearUser(obj, i);   // sub_7412C0
        }
    }
    return CNetVoiceResetUsers(obj);   // tail jmp sub_741440
}

// ---------------------------------------------------------------------------
// int CNetVoiceResetUsers(void *obj)   (sub_741440 - 0x74176F, thiscall):
// the 4-user voice route sync/reset pass (the CNetVoiceEnableUsers tail call).
//
// For each of the 4 local users (48-byte slots): if the slot's +4 flag is
// set, walk the voice route lists (obj+0x40 list A and obj+0x28 list B) in
// lockstep; for each route pair, decide (CNetVoiceMutedCheck + CNetVoice-
// FindRoute) whether to add (CNetVoiceListAppend) or remove (CNetVoice-
// EraseRoute) the route from the user's +0x10 packet list, and mark the
// changed flag. When the flag is set, send the mute list (CNetVoiceSendMute-
// List, sub_741160). After the 4-user loop, run the per-user voice packet
// processor (CNetVoiceSyncRoutes, sub_740DB0).
//
// Faithful port of the original control flow; the deep callees are remaining
// no-op stubs. The original's many __invalid_parameter_noinfo element bounds
// checks are condensed to the outer count guard (the inner route-list
// structural assertions are not replicated -- unreachable in this build
// because the route lists are empty while the voice manager is absent).
// ---------------------------------------------------------------------------
static int CNetVoiceResetUsers(void *obj)
{
    if (obj == 0) return 0;
    DWORD *dw = (DWORD *)obj;
    int i  = 0;      // user index
    int v2 = 0;      // user byte offset (0, 48, 96, 144)
    while (v2 < 0xC0)
    {
        if ((DWORD)i >= (dw[8] - dw[7]) / 48)
            return 0;   // __invalid_parameter_noinfo
        if (*(unsigned char *)(dw[7] + (DWORD)v2 + 4) != 0)
        {
            int  changed = 0;
            void *slot  = (void *)(dw[7] + (DWORD)v2);   // user slot base
            void *listA = (void *)dw[16];                // obj+0x40 (route list A)
            void *listB = (void *)dw[10];                // obj+0x28 (route list B)
            void *headA = (listA != 0) ? *(void **)listA : 0;
            for (void *nodeA = headA; listA != 0 && nodeA != listA; nodeA = (char *)nodeA + 0x28)
            {
                char *n = (char *)nodeA;
                void *child = *(void **)(n + 0x1C);
                void *next  = *(void **)(n + 0x20);
                if (child == next)
                {
                    listB = CNetVoiceListNext(listB);   // sub_742BE0
                }
                else
                {
                    unsigned child1 = (unsigned)(DWORD)*(void **)((char *)child + 4);
                    if (CNetVoiceMutedCheck(obj, (int)(DWORD)slot, (int)(DWORD)child))   // sub_740430
                    {
                        if (!CNetVoiceFindRoute(obj, (unsigned)i, (unsigned)(DWORD)child, child1))   // sub_740790
                            CNetVoiceListAppend((void *)((char *)slot + 0x10), (void *)&child);   // sub_745970
                    }
                    else if (CNetVoiceFindRoute(obj, (unsigned)i, (unsigned)(DWORD)child, child1))   // sub_740790
                    {
                        CNetVoiceEraseRoute(obj, (unsigned)i, (int)(DWORD)child, (int)child1);   // sub_740CE0
                    }
                    changed = 1;
                }
            }
            if (changed != 0)
                CNetVoiceSendMuteList(obj, i);   // sub_741160
        }
        v2 += 48;
        i++;
    }
    CNetVoiceSyncRoutes(obj);   // sub_740DB0
    return 0;
}

// ---------------------------------------------------------------------------
// int CNetVoiceManagerInit(void *obj)   (sub_741E50 - 0x741F1B, thiscall):
// the CNetVoiceManager's voice bring-up. See netinit.h for the full
// description. [NOT byte-identical] C++ reimplementation.
// ---------------------------------------------------------------------------
int CNetVoiceManagerInit(void *obj)
{
    if (obj == 0)
        return 0;   // voice manager absent in this build (CNetManagerStart leaves +0x30 = 0)

    DWORD *dw = (DWORD *)obj;
    char *cobj = (char *)obj;

    // 1. The voice-notification listener (type 3, sync) -> this+0x60 (dw[24]).
    dw[24] = (DWORD)XNotifyCreateListener(3, 0);

    // 2. The 28-byte XHV_CONFIG (a 4-byte header word + 7 config words), built
    //    on the stack. XHVCreateEngine's arg 1 is &cfg[1] (B+4); the two
    //    self-pointers (cfg[3], cfg[5]) reference the header word (B = &cfg[0]).
    DWORD cfg[8];
    cfg[0] = 2;                       // B+0  (header)
    cfg[1] = 0x1E;                    // B+4  = 30
    cfg[2] = 1;                       // B+8
    cfg[3] = (DWORD)&cfg[0];          // B+0C = B (self pointer)
    cfg[4] = 1;                       // B+10
    cfg[5] = (DWORD)&cfg[0];          // B+14 = B (self pointer)
    cfg[6] = 1;                       // B+18
    cfg[7] = 0;                       // B+1C

    // 3. GetManager() -> manager vtable[8] (CManagerWin32_GetWindow), result
    //    stored in a local and never used (the original discards it).
    void *mgr = GetManager();
    if (mgr != 0)
    {
        void **vt = (void **)*(void **)mgr;
        (void)((DWORD (*)(void *))vt[8])(mgr);
    }

    // 4. XHVCreateEngine(config, 0, this+0x08) -- create the Havoc voice engine
    //    at this+0x08.
    int result = XHVCreateEngine((void *)&cfg[1], 0, (void *)(cobj + 8));

    if (result >= 0)
    {
        void *nm    = GetNetManager();
        void *state = CNetManagerGetSlot24(nm);                       // sub_6A6940: the +0x24 sub-object
        CNetVoiceRouteInit(state, (int)(DWORD)(cobj + 4));           // sub_C1AC70(state, this+0x04)
        CNetVoiceEnableUsers(obj);                                   // sub_741DD0(obj)
        return CNetVoiceResetUsers(obj);                             // sub_741440(obj)
    }
    return result;
}

// ============================================================
//  Net-state factory map  (std::map<int, factory>) at 0x1DC4AA4
//
//  The global net-state factory registry: an MSVC std::map keyed by net-state
//  id holding a factory function pointer as the value. RegisterNetStateFactory-
//  Regs (sub_770960, called by RunFrontEnd right after CreateHeapManager) seeds
//  it with the four net matchmaking state factories (id 2..5); a later net-state
//  transition looks a state id up in this map and calls the factory to build
//  the CState object.
//
//  The map is a 56-byte object (0x1DC4AA4..0x1DC4AD3): a 4-byte owner tag (a
//  heap block holding a back-pointer to the map) at +0x00, the sentinel head
//  node at +0x18 (dword_1DC4ABC), and the node count at +0x1C (dword_1DC4AC0).
//  Each node is 24 bytes: { next +0x00, prev +0x04, right +0x08, key +0x0C,
//  value(factory) +0x10, color +0x14, sentinel +0x15 }. The head sentinel is
//  color=1, sentinel=1 and self-links while the map is empty. The binary search
//  (sub_7707E0 / sub_771120) walks from the head using the in-order next /
//  right-child links; the nodes form a red-black tree (color 1 = black, 0 = red).
//
//  The original constructs this map at static init (sub_1A5F2C0:
//  sub_770FB0(&map) + atexit(sub_1AC2DE0)); the static-initializer object at
//  the end of this section reproduces that timing. LHAlloc is safe at static
//  init -- it routes through the CRT process heap, which is ready before any
//  dynamic ctor runs. The atexit teardown (sub_1AC2DE0) is not reproduced (it
//  needs the map's erase-all, sub_771030, which is out of scope).
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc / the
//  rotate helpers) and refs the global map at its original high VA 0x01DC4AA4.
// ============================================================

// A real node: next = LEFT child (smaller keys), prev = PARENT, right = RIGHT
// child (larger keys). The sentinel `head` reuses the same three slots to hold
// the tree book-keeping pointers: head->prev = the ROOT, head->next = the MIN
// (leftmost) node, head->right = the MAX (rightmost) node; a null child of a
// real node is a pointer back to head (the sentinel).
typedef struct F3NetStateNode
{
    unsigned int  next;      // +0x00  left child (head: min node)
    unsigned int  prev;      // +0x04  parent     (head: root node)
    unsigned int  right;     // +0x08  right child (head: max node)
    unsigned int  key;       // +0x0C  net-state id
    unsigned int  value;     // +0x10  factory function pointer
    unsigned char color;     // +0x14  red-black color (1 = black, 0 = red)
    unsigned char sentinel;  // +0x15  1 for the head sentinel, 0 for real nodes
    unsigned char pad[2];    // +0x16
} F3NetStateNode;   // 24 bytes

typedef struct F3NetStateMap
{
    unsigned int ownerTag;   // +0x00  4-byte heap block holding a back-pointer
    unsigned int pad[5];     // +0x04 .. +0x14
    unsigned int head;       // +0x18  the sentinel head node
    unsigned int count;      // +0x1C  node count
    unsigned int tail[6];    // +0x20 .. +0x34
} F3NetStateMap;   // 56 bytes

static F3NetStateMap g_netStateMap;   // 0x1DC4AA4

// F3NetStateMapCtor  (0x770FB0 - 0x77101F)  -- construct the (empty) map in place.
static void F3NetStateMapCtor(F3NetStateMap *m)
{
    unsigned int *tag = (unsigned int *)LHAlloc(4);
    if (tag != 0) *tag = (unsigned int)m;   // else tag stays 0
    m->ownerTag = (unsigned int)tag;

    F3NetStateNode *h = (F3NetStateNode *)LHAlloc(24);
    if (h != 0) h->next = 0;
    if (h != 0) h->prev = 0;
    if (h != 0) h->right = 0;
    if (h != 0) h->sentinel = 0;
    if (h != 0) h->color = 1;
    m->head = (unsigned int)h;
    if (h != 0)
    {
        h->sentinel = 1;
        h->prev  = (unsigned int)h;
        h->next  = (unsigned int)h;
        h->right = (unsigned int)h;
    }
    m->count = 0;
}

// F3RbRotateLeft  (0x76FEC0 - 0x770020)  -- red-black LEFT rotation around X
// (promote X->right in place of X).
static F3NetStateNode *F3RbRotateLeft(F3NetStateMap *m, F3NetStateNode *x)
{
    F3NetStateNode *y    = (F3NetStateNode *)x->right;
    x->right             = y->next;
    if (((F3NetStateNode *)y->next)->sentinel == 0)
        ((F3NetStateNode *)y->next)->prev = (unsigned int)x;
    y->prev              = x->prev;
    F3NetStateNode *head = (F3NetStateNode *)m->head;
    if (x == (F3NetStateNode *)head->prev)   // X is the root
    {
        head->prev = (unsigned int)y;
        y->next    = (unsigned int)x;
        x->prev    = (unsigned int)y;
    }
    else
    {
        F3NetStateNode *p = (F3NetStateNode *)x->prev;
        if (x == (F3NetStateNode *)p->next)
            p->next = (unsigned int)y;
        else
            p->right = (unsigned int)y;
        y->next = (unsigned int)x;
        x->prev = (unsigned int)y;
    }
    return y;
}

// F3RbRotateRight  (0x76FF50 - 0x76FFB6)  -- red-black RIGHT rotation around X
// (promote X->next in place of X).
static F3NetStateNode *F3RbRotateRight(F3NetStateMap *m, F3NetStateNode *x)
{
    F3NetStateNode *z    = (F3NetStateNode *)x->next;
    x->next              = z->right;
    F3NetStateNode *r    = (F3NetStateNode *)z->right;
    if (r->sentinel == 0)
        r->prev = (unsigned int)x;
    z->prev              = x->prev;
    F3NetStateNode *head = (F3NetStateNode *)m->head;
    if (x == (F3NetStateNode *)head->prev)   // X is the root
    {
        head->prev = (unsigned int)z;
        z->right   = (unsigned int)x;
        x->prev    = (unsigned int)z;
    }
    else
    {
        F3NetStateNode *p = (F3NetStateNode *)x->prev;
        if (x == (F3NetStateNode *)p->right)
            p->right = (unsigned int)z;
        else
            p->next = (unsigned int)z;
        z->right = (unsigned int)x;
        x->prev  = (unsigned int)z;
    }
    return z;
}

// F3NetStateMapInsert  (0x770D60 - 0x771210)  -- insert a fresh node (key, value)
// at the position the search found, rebalancing the red-black tree. `flag`
// selects the link: 0 = new is prevNode's right child, 1 = new is prevNode's
// in-order successor. `iterOut` (a { tag, node, flag } scratch) is filled with
// { ownerTag, newNode } and `pair` = { key, value }. Returns the new node.
static F3NetStateNode *F3NetStateMapInsert(F3NetStateMap *m, unsigned int *iterOut,
                                           unsigned char flag, unsigned int prevNodeAddr,
                                           const unsigned int *pair)
{
    F3NetStateNode *head = (F3NetStateNode *)m->head;
    if (m->count >= 0x1FFFFFFE)   // throw std::length_error("map/set<T> too long")
        return 0;                 // (unreachable for the four net states)

    F3NetStateNode *n = (F3NetStateNode *)LHAlloc(0x18);
    if (n != 0)
    {
        n->next = (unsigned int)head;
        n->prev = prevNodeAddr;
        n->right = (unsigned int)head;
        n->key = pair[0];
        n->value = pair[1];
        n->color = 0;
        n->sentinel = 0;
    }
    m->count++;
    if (prevNodeAddr == (unsigned int)head)
    {
        head->prev = (unsigned int)n;
        head->next = (unsigned int)n;
        head->right = (unsigned int)n;
    }
    else if (flag != 0)
    {
        ((F3NetStateNode *)prevNodeAddr)->next = (unsigned int)n;
        if (prevNodeAddr == head->next) head->next = (unsigned int)n;
    }
    else
    {
        ((F3NetStateNode *)prevNodeAddr)->right = (unsigned int)n;
        if (prevNodeAddr == head->right) head->right = (unsigned int)n;
    }

    // Red-black rebalance (color 1 = black, 0 = red); runs only while the
    // parent of the running node is red. Ported instruction-for-instruction
    // from the original (the guard at 0x770EB9, the two rotation branches, and
    // the recolor/fixup cases below).
    F3NetStateNode *node = n;
    F3NetStateNode *p = (F3NetStateNode *)n->prev;
    if (p->color == 0)
    {
        for (;;)
        {
            p = (F3NetStateNode *)node->prev;
            F3NetStateNode *g = (F3NetStateNode *)p->prev;
            // P's sibling (the uncle): G's other child. P is G's left child
            // (p == g->next) when the uncle is G's right child, else the uncle
            // is G's left child (g->next). (next=left, prev=parent, right=right.)
            F3NetStateNode *uncle =
                (p == (F3NetStateNode *)g->next) ? (F3NetStateNode *)g->right
                                                 : (F3NetStateNode *)g->next;
            if (uncle->color == 0)
            {
                // Uncle red: recolor the node, the uncle and the grandparent,
                // then climb to the grandparent.
                p->color = 1;
                uncle->color = 1;
                g->color = 0;
                node = g;
            }
            else if (p == (F3NetStateNode *)g->next)
            {
                if (node == (F3NetStateNode *)p->right)
                {
                    node = p;
                    F3RbRotateLeft(m, node);
                }
                F3NetStateNode *pn = (F3NetStateNode *)node->prev;
                pn->color = 1;
                F3NetStateNode *gn = (F3NetStateNode *)pn->prev;
                gn->color = 0;
                F3RbRotateRight(m, gn);
            }
            else
            {
                if (node == (F3NetStateNode *)p->next)
                {
                    node = p;
                    F3RbRotateRight(m, node);
                }
                F3NetStateNode *pn = (F3NetStateNode *)node->prev;
                pn->color = 1;
                F3NetStateNode *gp = (F3NetStateNode *)pn->prev;
                gp->color = 0;
                F3NetStateNode *gr  = (F3NetStateNode *)gp->right;
                F3NetStateNode *grn = (F3NetStateNode *)gr->next;
                gp->right = (unsigned int)grn;
                if (grn->sentinel == 0) grn->prev = (unsigned int)gp;
                F3NetStateNode *gpp = (F3NetStateNode *)gp->prev;
                gr->prev = (unsigned int)gpp;
                if (gp == (F3NetStateNode *)head->prev)      head->prev = (unsigned int)gr;
                else if (gp == (F3NetStateNode *)gpp->next)  gpp->next  = (unsigned int)gr;
                else                                          gpp->right = (unsigned int)gr;
                gr->next = (unsigned int)gp;
                gp->prev = (unsigned int)gr;
            }
            if (((F3NetStateNode *)node->prev)->color != 0) break;
        }
    }
    ((F3NetStateNode *)head->prev)->color = 1;   // the root goes black
    iterOut[0] = m->ownerTag;
    iterOut[1] = (unsigned int)n;
    return n;
}

// F3NetStateMapIteratorDec  (0x770300 - 0x770390)  -- move the iterator's node
// to its in-order predecessor (handling the sentinel and the tree links). Only
// reached for non-ascending lookups; not exercised by the four ascending net-
// state inserts but ported faithfully.
static void F3NetStateMapIteratorDec(unsigned int *iter)   // iter = { tag, node }
{
    F3NetStateNode *node = (F3NetStateNode *)iter[1];
    if (node->sentinel != 0)
    {
        F3NetStateNode *r = (F3NetStateNode *)node->right;
        iter[1] = (unsigned int)r;
        if (r->sentinel != 0) return;   // _invalid_parameter_noinfo
        return;
    }
    F3NetStateNode *nxt = (F3NetStateNode *)node->next;
    if (nxt->sentinel != 0)
    {
        F3NetStateNode *i = (F3NetStateNode *)node->prev;
        while (i->sentinel == 0)
        {
            if (iter[1] != i->right) break;
            iter[1] = (unsigned int)i;
            i = (F3NetStateNode *)i->prev;
        }
        if (((F3NetStateNode *)iter[1])->sentinel != 0) return;   // _invalid_parameter_noinfo
        iter[1] = (unsigned int)i;
    }
    else
    {
        F3NetStateNode *j = (F3NetStateNode *)nxt->right;
        F3NetStateNode *last = nxt;
        while (j->sentinel == 0)
        {
            last = j;
            j = (F3NetStateNode *)j->right;
        }
        iter[1] = (unsigned int)last;
    }
}

// F3NetStateMapFindOrInsert  (0x771120 - 0x771210)  -- the std::map operator[]:
// search the map for `pair[0]`; when absent, insert { key, factory } and point
// the returned iterator at the new node (flag 1), otherwise point it at the
// existing node (flag 0). `iterOut` is the 12-byte { tag, node, flag } result.
static F3NetStateNode *F3NetStateMapFindOrInsert(F3NetStateMap *m, unsigned int *iterOut,
                                                 const unsigned int *pair)
{
    unsigned int key = pair[0];
    F3NetStateNode *head = (F3NetStateNode *)m->head;
    F3NetStateNode *lastReal = head;                          // the last real node (or head)
    F3NetStateNode *node = (F3NetStateNode *)head->prev;      // the search cursor
    unsigned char flag = 1;

    while (node->sentinel == 0)
    {
        if (key < node->key)
        {
            flag = 1;
            lastReal = node;
            node = (F3NetStateNode *)node->next;
        }
        else
        {
            flag = 0;
            lastReal = node;
            node = (F3NetStateNode *)node->right;
        }
    }

    unsigned int localIter[3] = { m->ownerTag, (unsigned int)lastReal, 0 };   // { tag, node }

    if (flag != 0)   // key < lastReal.key: the position is at / before lastReal
    {
        if (lastReal == (F3NetStateNode *)head->next)
        {
            // Empty map (or before the head): insert at the head position.
            F3NetStateMapInsert(m, localIter, 1, (unsigned int)lastReal, pair);
            iterOut[0] = localIter[0];
            iterOut[1] = localIter[1];
            iterOut[2] = 1;
        }
        else
        {
            F3NetStateMapIteratorDec(localIter);
            if (lastReal->key < key)
            {
                F3NetStateMapInsert(m, localIter, flag, (unsigned int)lastReal, pair);
                iterOut[0] = localIter[0];
                iterOut[1] = localIter[1];
                iterOut[2] = 1;
            }
            else
            {
                iterOut[0] = localIter[0];
                iterOut[1] = (unsigned int)lastReal;
                iterOut[2] = 0;
            }
        }
    }
    else   // key >= lastReal.key
    {
        if (lastReal->key >= key)   // found (key == lastReal.key)
        {
            iterOut[0] = m->ownerTag;
            iterOut[1] = (unsigned int)lastReal;
            iterOut[2] = 0;
        }
        else
        {
            F3NetStateMapInsert(m, localIter, flag, (unsigned int)lastReal, pair);
            iterOut[0] = localIter[0];
            iterOut[1] = localIter[1];
            iterOut[2] = 1;
        }
    }
    return (F3NetStateNode *)iterOut[1];
}

// ---- the four net-state factories -------------------------------------------
//
//  Each factory (sub_770090 / sub_7700B0 / sub_7700D0 / sub_7700F0) builds a
//  20-byte net-state object: LHAlloc(20), a shared-slot string (the state name)
//  referenced at +0x08 (RefSharedSlot), the CBaseState ctor (sub_138C630) at
//  +0x00/+0x04, the +0x0C/+0x10 zeros, then the derived vtable at +0x00. The
//  full NStateSystem::CState* vtables (and their methods) are out of scope; the
//  placeholder vtables below give each object a valid, non-null vtable pointer.
static void *g_CBaseStateVtable[4]                       = { 0 };
static void *g_CStateNetMatchmakingDataVtable[8]         = { 0 };
static void *g_CStateNetStartMatchmakingVtable[8]        = { 0 };
static void *g_CStateNetSearchingForSessionVtable[8]     = { 0 };
static void *g_CStateNetFailedToFindSessionVtable[8]     = { 0 };
static void *g_CStateNetJoinSessionVtable[8]             = { 0 };

// sub_770090 - build a CStateNetStartMatchmaking object.
static void *CreateNetStartMatchmakingState(void)
{
    unsigned int *obj = (unsigned int *)LHAlloc(20);
    if (obj == 0) return 0;
    F3SharedBuffer *slot = 0;
    SetSharedSlot(&slot, "NetStartMatchmaking", -1);
    obj[0] = (unsigned int)&g_CBaseStateVtable;                     // +0x00
    obj[1] = 0;                                                     // +0x04
    RefSharedSlot((F3SharedBuffer **)&obj[2], &slot);               // +0x08
    obj[0] = (unsigned int)&g_CStateNetMatchmakingDataVtable;       // +0x00
    obj[3] = 0;                                                     // +0x0C
    obj[4] = 0;                                                     // +0x10
    ReleaseSharedSlot(&slot);
    obj[0] = (unsigned int)&g_CStateNetStartMatchmakingVtable;      // +0x00
    return obj;
}

// sub_7700B0 - build a CStateNetSearchingForSession object.
static void *CreateNetSearchingForSessionState(void)
{
    unsigned int *obj = (unsigned int *)LHAlloc(20);
    if (obj == 0) return 0;
    F3SharedBuffer *slot = 0;
    SetSharedSlot(&slot, "NetSearchingForSession", -1);
    obj[0] = (unsigned int)&g_CBaseStateVtable;
    obj[1] = 0;
    RefSharedSlot((F3SharedBuffer **)&obj[2], &slot);
    obj[0] = (unsigned int)&g_CStateNetMatchmakingDataVtable;
    obj[3] = 0;
    obj[4] = 0;
    ReleaseSharedSlot(&slot);
    obj[0] = (unsigned int)&g_CStateNetSearchingForSessionVtable;
    return obj;
}

// sub_7700D0 - build a CStateNetFailedToFindSession object.
static void *CreateNetFailedToFindSessionState(void)
{
    unsigned int *obj = (unsigned int *)LHAlloc(20);
    if (obj == 0) return 0;
    F3SharedBuffer *slot = 0;
    SetSharedSlot(&slot, "NetFailedToFindSession", -1);
    obj[0] = (unsigned int)&g_CBaseStateVtable;
    obj[1] = 0;
    RefSharedSlot((F3SharedBuffer **)&obj[2], &slot);
    obj[0] = (unsigned int)&g_CStateNetMatchmakingDataVtable;
    obj[3] = 0;
    obj[4] = 0;
    ReleaseSharedSlot(&slot);
    obj[0] = (unsigned int)&g_CStateNetFailedToFindSessionVtable;
    return obj;
}

// sub_7700F0 - build a CStateNetJoinSession object.
static void *CreateNetJoinSessionState(void)
{
    unsigned int *obj = (unsigned int *)LHAlloc(20);
    if (obj == 0) return 0;
    F3SharedBuffer *slot = 0;
    SetSharedSlot(&slot, "NetJoinSession", -1);
    obj[0] = (unsigned int)&g_CBaseStateVtable;
    obj[1] = 0;
    RefSharedSlot((F3SharedBuffer **)&obj[2], &slot);
    obj[0] = (unsigned int)&g_CStateNetMatchmakingDataVtable;
    obj[3] = 0;
    obj[4] = 0;
    ReleaseSharedSlot(&slot);
    obj[0] = (unsigned int)&g_CStateNetJoinSessionVtable;
    return obj;
}

// ============================================================
//  RegisterNetStateFactoryRegs  (0x770960 - 0x7709F3)  -- seed the global
//  net-state factory map with the four net matchmaking state factories.
//
//  void RegisterNetStateFactoryRegs(void)   -- cdecl, no args
//
//  Registers { 2 -> CStateNetStartMatchmaking, 3 -> CStateNetSearchingFor-
//  Session, 4 -> CStateNetFailedToFindSession, 5 -> CStateNetJoinSession } into
//  g_netStateMap (0x1DC4AA4) via F3NetStateMapFindOrInsert (sub_771120), reusing
//  a single 12-byte iterator out buffer and a { id, factory } pair, exactly as
//  the original. Sole caller: RunFrontEnd (0x61D269), right after
//  CreateHeapManager and before the engine's member-0x98 sorted map is built.
//  [NOT byte-identical] placement-dependent (relative calls to the four
//  factories and to F3NetStateMapFindOrInsert) and refs the global map and the
//  factory VAs at their original high addresses.
// ============================================================
void RegisterNetStateFactoryRegs(void)
{
    unsigned int iter[3] = { 0, 0, 0 };   // the 12-byte iterator out buffer
    unsigned int pair[2];                 // { id, factory }

    pair[0] = 2; pair[1] = (unsigned int)CreateNetStartMatchmakingState;
    F3NetStateMapFindOrInsert(&g_netStateMap, iter, pair);
    pair[0] = 3; pair[1] = (unsigned int)CreateNetSearchingForSessionState;
    F3NetStateMapFindOrInsert(&g_netStateMap, iter, pair);
    pair[0] = 4; pair[1] = (unsigned int)CreateNetFailedToFindSessionState;
    F3NetStateMapFindOrInsert(&g_netStateMap, iter, pair);
    pair[0] = 5; pair[1] = (unsigned int)CreateNetJoinSessionState;
    F3NetStateMapFindOrInsert(&g_netStateMap, iter, pair);
}

// Static initializer: construct the global net-state factory map at startup
// (reproducing the original's sub_1A5F2C0 static ctor timing). Runs before
// WinMain; LHAlloc is safe at this point (CRT process heap, ready pre-ctor).
namespace {
struct F3NetStateMapInit
{
    F3NetStateMapInit() { F3NetStateMapCtor(&g_netStateMap); }
};
F3NetStateMapInit g_f3NetStateMapInit;
}
