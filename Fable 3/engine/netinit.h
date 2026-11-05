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

// CNetManagerShutdown (0x6A70F0 - 0x6A720C) — the 80-byte CNetManager teardown:
// sub_6834D0(slot+0x10, 0) + LHFree(slot+0x38) up front (unconditional), then
// sub_6A84C0 + LHFree of slot+0x34, vtable[0](slot, 1) releases of
// +0x20/+0x1C/+0x18/+0x14/+0x0C/+0x48/+0x08/+0x04, sub_692830 + LHFree of
// slot+0x10, and vtable[0](slot, 1) releases of +0x2C/+0x24/+0x28 — every
// slot zeroed. thiscall, no stack args.
void CNetManagerShutdown(void *obj);

#endif  // F3_NETINIT_H
