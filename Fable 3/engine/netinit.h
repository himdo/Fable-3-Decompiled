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

// int InitNetworking(void)   (0x6A7210 - 0x6A72EF)
//
// Returns the result of the CNetManager start call (CNetManagerStart); the
// sole caller (RunFrontEnd) discards it.
int InitNetworking(void);

#endif  // F3_NETINIT_H
