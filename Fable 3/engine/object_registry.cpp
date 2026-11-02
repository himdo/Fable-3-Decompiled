// Fable 3 — engine object registry implementation (SEH cleanup blocks +
// global object pairs). The CDebugTraceFable pair and the registry critical
// section are the module-private globals.

#include "object_registry.h"
#include "common/common.h"

// dword_20B600C / dword_20B6010: the CDebugTraceFable object + its SEH block.
static F3ObjReg g_DebugTracePair;

// stru_20B6014: the critical section shared by the "register / release object"
// helpers (sub_1A02DD0, sub_1A02DA0, SetDebugTrace/sub_1A02E20, sub_1A030A0, ...) that all
// lock the same mutex before touching their global object pair.
static CRITICAL_SECTION g_ObjectRegistryCS;
struct ObjectRegistryCsInit
{
    ObjectRegistryCsInit()  { InitializeCriticalSection(&g_ObjectRegistryCS); }
    ~ObjectRegistryCsInit() { DeleteCriticalSection(&g_ObjectRegistryCS); }
};
static ObjectRegistryCsInit g_ObjectRegistryCsInit;

// ============================================================
//  ReleaseObjectPair  (0x00403220 - 0x00403262)  -- release a global object pair
//  [NOT byte-identical] placement-dependent (relative call to the heap free).
//
//  void ReleaseObjectPair(void *pair)  (pair = &F3ObjReg)
//
//  General-purpose release used by the critical-section-protected "register
//  object" helpers. Decrement the SEH block's refcount; when it reaches zero
//  invoke handler(arg) and free the block via the game heap. Then clear both
//  words. Structurally identical to ReleaseWorkerPair / ReleaseManagerPair.
// ============================================================
void ReleaseObjectPair(void *pair)
{
    F3ObjReg *p    = (F3ObjReg *)pair;
    F3SehBlock *sb = (F3SehBlock *)p->sehBlock;

    if (sb != 0 && InterlockedDecrement(&sb->refcount) == 0)
    {
        sb->handler(sb->arg);
        LHFree(sb);
    }
    p->object   = 0;
    p->sehBlock = 0;
}

// ============================================================
//  SetDebugTrace  (0x1A02E20 - 0x1A02E6C)  -- register the CDebugTraceFable
//  [NOT byte-identical] placement-dependent (relative calls to
//  ReleaseObjectPair, InterlockedIncrement and the critical-section helpers).
//
//  void SetDebugTrace(F3ObjReg *reg)
//
//  Critical-section-protected setter for the CDebugTraceFable global pair. If
//  the incoming SEH block differs from the current one, release the old pair,
//  store the new (object, SEH block), and take a reference on the new SEH block
//  so the global pair owns it. (Sole caller: GameThread.)
// ============================================================
void SetDebugTrace(F3ObjReg *reg)
{
    EnterCriticalSection(&g_ObjectRegistryCS);
    if (g_DebugTracePair.sehBlock != reg->sehBlock)
    {
        ReleaseObjectPair(&g_DebugTracePair);
        g_DebugTracePair.object   = reg->object;
        g_DebugTracePair.sehBlock = reg->sehBlock;
        if (reg->sehBlock != 0)
            InterlockedIncrement(&((F3SehBlock *)reg->sehBlock)->refcount);
    }
    LeaveCriticalSection(&g_ObjectRegistryCS);
}

// Release a locally-held SEH reference; on zero invoke the handler and free.
void F3ReleaseSehRef(void *sehBlock)
{
    if (sehBlock && InterlockedDecrement(&((F3SehBlock *)sehBlock)->refcount) == 0)
    {
        F3SehBlock *sb = (F3SehBlock *)sehBlock;
        sb->handler(sb->arg);
        LHFree(sb);
    }
}
