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
static void *g_NToolsCBaseClassVtable[1]     = { 0 };
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
//  CNetManagerStart  (0x6A6DB0 - 0x6A70E3)  -- [remaining stub]
//
//  The real function is 0x333 bytes: it constructs the CNetManager's ~15
//  sub-objects (LHAlloc + a per-class ctor into each +0x04..+0x5C slot,
//  releasing the old one through its deleting destructor), wires the packet
//  callback + reader-writer lock, and returns sub_A1AA30(535, ...).
//  Out of scope for InitNetworking. Returns 0 (success).
//
//  int CNetManagerStart(void *obj)
// ============================================================
static int CNetManagerStart(void *obj)
{ (void)obj; return 0; }

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
//  CNetPacketCallbackDtor  (0x1898510 - 0x1898553)  -- [remaining stub]
//
//  The CNetManager's +0x3C CNetPacketCallback (DNameNode base) dtor: stores
//  the NTools::CBaseObject vtable, releases the +4 node through the heap,
//  then stores the NTools::CBaseClass vtable and zeroes the node. Out of
//  scope: the packet-callback object is a placeholder in this build.
// ============================================================
static void *CNetPacketCallbackDtor(void *obj) { (void)obj; return obj; }

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
