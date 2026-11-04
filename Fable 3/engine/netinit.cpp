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
static DWORD g_dword_1DC2550 = 0;   // 0x1DC2550
static DWORD g_dword_1DC2554 = 0;   // 0x1DC2554

// 0x1DBDDA8 - the global CNetLiveSystem (spans 0x1DBDDA8..0x1DBE280; the
// ctor / dtor touch up through +0x4D7). The stubbed ctor / dtor below treat
// it as a zeroed block with a placeholder vtable and the -1 session handle.
static BYTE g_CNetLiveSystem[0x4D8] = { 0 };   // 0x1DBDDA8

// Placeholder vtables for the objects the (stubbed) ctors construct.
static void *g_CNetLiveSystemVtable[8]       = { 0 };
static void *g_CNetManagerVtable[8]          = { 0 };
static void *g_CNetPacketCallbackVtable[8]   = { 0 };

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
//  [BYTE-IDENTICAL]
//
//  int DisposeNetManager(int arg)   (arg = the CNetManager, or 0)
//
//  Deleting-destructor dispatch: vtable[0](arg, 1).
// ============================================================
static __declspec(naked) int DisposeNetManager(int arg)
{
    __asm {
        mov    ecx, [esp]
        test   ecx, ecx
        jz     DisposeNetManagerDone
        mov    eax, [ecx]
        mov    edx, [eax]
        push   1
        call   edx
    DisposeNetManagerDone:
        ret
    }
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
