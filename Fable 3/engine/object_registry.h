// Fable 3 — engine object registry (SEH cleanup blocks + global object pairs).
//
// The engine registers refcounted "object + SEH cleanup block" pairs in
// globals (the current worker, the current manager, the CDebugTraceFable,
// ...). The SEH block disposes the object through its `handler` when the
// block's refcount reaches zero. The registry's critical section
// (stru_20B6014) guards the CS-protected setters.
#ifndef F3_OBJECT_REGISTRY_H
#define F3_OBJECT_REGISTRY_H

#include <windows.h>

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

// ReleaseObjectPair (0x00403220 - 0x00403262) -- release a global object pair
// [NOT byte-identical] placement-dependent (relative call to the heap free).
//
// void ReleaseObjectPair(void *pair)  (pair = &F3ObjReg)
//
// General-purpose release used by the critical-section-protected "register
// object" helpers. Decrement the SEH block's refcount; when it reaches zero
// invoke handler(arg) and free the block via the game heap. Then clear both
// words. Structurally identical to ReleaseWorkerPair / ReleaseManagerPair.
void ReleaseObjectPair(void *pair);

// SetDebugTrace (0x1A02E20 - 0x1A02E6C) -- register the CDebugTraceFable
// [NOT byte-identical] placement-dependent (relative calls to
// ReleaseObjectPair, InterlockedIncrement and the critical-section helpers).
//
// void SetDebugTrace(F3ObjReg *reg)
//
// Critical-section-protected setter for the CDebugTraceFable global pair
// (dword_20B600C / dword_20B6010). If the incoming SEH block differs from the
// current one, release the old pair, store the new (object, SEH block), and
// take a reference on the new SEH block so the global pair owns it.
// (Sole caller: GameThread.)
void SetDebugTrace(F3ObjReg *reg);

// SetDebugHandler (0x1A02DD0 - 0x1A02E1C) -- register the
// NDebug::NDebugManager::CFableWindowsDebugHandler
// [NOT byte-identical] placement-dependent (relative call to
// ReleaseObjectPair, plus the InterlockedIncrement / critical-section IAT slots
// and the high-VA dword_20B6004 / lpAddend globals).
//
// void SetDebugHandler(F3ObjReg *reg)
//
// Critical-section-protected setter for the CFableWindowsDebugHandler global
// pair (dword_20B6004 / lpAddend, 0x20B6008). If the incoming SEH block differs
// from the current one, release the old pair, store the new (object, SEH block),
// and take a reference on the new SEH block so the global pair owns it.
// (Callers: GameThread 0x615412, sub_19D4FC0 0x19D517E.)
void SetDebugHandler(F3ObjReg *reg);

// GetDebugHandlerPair -- return the CFableWindowsDebugHandler global pair
// (dword_20B6004 / lpAddend). Exposed so framework.cpp's ReleaseGlobalObject
// releases the same pair it tears down at FrameworkShutdown.
F3ObjReg *GetDebugHandlerPair(void);

// Release a locally-held SEH reference; on zero invoke the handler and free.
void F3ReleaseSehRef(void *sehBlock);

#endif  // F3_OBJECT_REGISTRY_H
