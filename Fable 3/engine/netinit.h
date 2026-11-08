// Fable 3 — networking init module (InitNetworking, 0x6A7210).
//
// InitNetworking is the network-subsystem bootstrap that RunFrontEnd runs
// first (right after InitEngineTime): it builds the XLive settings and calls
// XLiveInitializeEx, installs XLivePreTranslateMessage as the worker's
// pre-translate hook, brings up the LIONHEAD networking stack
// (XNet / XOnline / Winsock), constructs the global CNetLiveSystem (once,
// with an atexit teardown) and runs its presence / notification init, then
// allocates + constructs the 80-byte CNetManager, publishes it as the
// current net-manager pair (dword_1DC2550 / dword_1DC2554), and starts it.
#ifndef F3_NETINIT_H
#define F3_NETINIT_H

#include <windows.h>

// int InitNetworking(void)   (0x6A7210 - 0x6A72EF)
//
// Returns the result of the CNetManager start call (CNetManagerStart); the
// sole caller (RunFrontEnd) discards it.
int InitNetworking(void);

// The current CNetManager (80 bytes) + its 12-byte SEH cleanup block pair.
// Set by InitNetworking (SetNetManager), torn down by CGameEngineCleanup
// (CNetManagerShutdown + SEH release + pair clear).
extern DWORD g_dword_1DC2550;   // 0x1DC2550: the current CNetManager
extern DWORD g_dword_1DC2554;   // 0x1DC2554: its SEH cleanup block

// void *GetNetManager(void)   (sub_6A66C0) -- the current CNetManager getter:
// returns g_dword_1DC2550 (0 when no net manager is installed).
void *GetNetManager(void);

// void *CNetManagerGetUserManager(void *obj)   (0x6A6960, byte-identical)
// -- the CNetManager's +0x28 getter: returns the 60-byte CNetUserManager
// (the XLive 4-user sign-in-state object built by CNetManagerStart via
// LHAlloc(60) + sub_772E50; its methods are the sub_771xxx / sub_772xxx
// cluster). ~100 call sites, all `CNetUserManager = CNetManagerGetUserManager(
// GetNetManager())`.
void *CNetManagerGetUserManager(void *obj);

// CNetManagerShutdown (0x6A70F0 - 0x6A720C) — the 80-byte CNetManager teardown:
// sub_6834D0(slot+0x10, 0) + LHFree(slot+0x38) up front (unconditional), then
// sub_6A84C0 + LHFree of slot+0x34, vtable[0](slot, 1) releases of
// +0x20/+0x1C/+0x18/+0x14/+0x0C/+0x48/+0x08/+0x04, sub_692830 + LHFree of
// slot+0x10, and vtable[0](slot, 1) releases of +0x2C/+0x24/+0x28 — every
// slot zeroed. thiscall, no stack args.
void CNetManagerShutdown(void *obj);

// The 32-byte per-user sign-in record held in the CNetUserManager's sign-in
// array (CNetUserManager +0x14 .. +0x18, 32-byte elements, one per local
// user). Offsets are the ones CNetUserManagerRefreshSignin (0x772190) reads
// and writes.
typedef struct F3NetUserSignin
{
    unsigned char signState2;   // +0x00: 1 when the sign-in state == 2
    unsigned char signState1;   // +0x01: 1 when the sign-in state == 1
    unsigned char signFlag;     // +0x02: 1 when (sign-in flags & 2)
    unsigned char pad3;         // +0x03
    unsigned int  savePath;     // +0x04: F3SharedBuffer* save-game path (cleared here)
    unsigned int  xuidHi;       // +0x08: effective XUID high (sign-in B, or A when B is 0)
    unsigned int  xuidLo;       // +0x0C: effective XUID low
    unsigned int  xuidA_hi;     // +0x10: XUID high from XUserGetSigninInfo(0, 2, ...)
    unsigned int  xuidA_lo;     // +0x14: XUID low  from XUserGetSigninInfo(0, 2, ...)
    unsigned int  xuidB_hi;     // +0x18: XUID high from XUserGetSigninInfo(0, 1, ...)
    unsigned int  xuidB_lo;     // +0x1C: XUID low  from XUserGetSigninInfo(0, 1, ...)
} F3NetUserSignin;   // 32 bytes

// void CNetUserManagerRefreshSignin(void *obj)   (0x772190 - 0x772421, thiscall)
//
// Re-reads the XLive sign-in state of local user 0 (XUserGetSigninInfo with
// WhichSignin 2 then 1) and refreshes the CNetUserManager's first (element
// 0) 32-byte sign-in record (F3NetUserSignin): the sign-in state / flag
// bytes, the effective + per-sign-in XUIDs, and a cleared save-game path.
// obj = the 60-byte CNetUserManager (CNetManagerGetUserManager). Called by
// RunFrontEnd (0x61D1B3, right after local sign-in), the XNotify pump
// (0x7724BA, on a sign-in event) and the listener setup (0x772EFA).
void CNetUserManagerRefreshSignin(void *obj);

// 0x770960 - seed the global net-state factory map (0x1DC4AA4) with the four
// net matchmaking state factories (id 2..5). Called by RunFrontEnd after
// CreateHeapManager; a later net-state transition looks a state id up in
// this map and calls the factory. See the implementation in netinit.cpp.
void RegisterNetStateFactoryRegs(void);

#endif  // F3_NETINIT_H
