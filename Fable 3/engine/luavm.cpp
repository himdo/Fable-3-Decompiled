// Fable 3 — engine Lua / KoreVM module implementation: the engine's
// "Fable 3" Lua environment.  CreateLuaModule (sub_629570) / DestroyLuaModule
// (sub_6281B0) + the 1032-byte KoreVM host and its sub_5xxxxx VM-internals
// family (Kore* heap / string-table / record-table internals), the
// CLuaManager + CEntityScriptHandleManager wrappers, and ReinitEngineLua
// (sub_61A470).  Split out of gamemain.cpp (the frontend run module) so the
// Lua / Kore VM code lives in its own translation unit.

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // keep the module warning-parity with gamemain
#endif

#include "luavm.h"                // DestroyLuaModule / ReinitEngineLua decls
#include "common/common.h"        // LHAlloc / F3SharedBuffer / ReleaseSharedSlot
#include "netinit.h"              // GetNetManager / CNetLuaEventManagerListReset

#include <windows.h>
#include <setjmp.h>               // setjmp / longjmp (the VM-heap OOM expand)
#include <float.h>                // _controlfp (the FPU round-toward-zero in KoreHeapInitGrow)
#include <intrin.h>               // __rdtsc (the QPC span accounting in KoreHeapInitGrow)
#include <string.h>               // memset

// F3StubRelease (the vtable slot 0 release stub) — a module-private copy
// of gamemain.cpp's identical static stub, so the CLuaManager /
// CEntityScriptHandleManager vtables below can reference it locally.
static void F3StubRelease(void *obj, int freeIt) { (void)obj; (void)freeIt; }

// ============================================================
//  ReinitEngineLua  (0x61A470 - 0x61A5D1)  -- (re)build the engine's
//  Lua environment
//  [NOT byte-identical] placement-dependent (relative calls to
//  GetNetManager, the Lua-module / CLuaManager /
//  CEntityScriptHandleManager ctors, and LHAlloc) and it references
//  the high-VA data slots unk_1DBE2B4 / unk_1DBDA6C.
//
//  int ReinitEngineLua(void *obj, int flag)
//
//  thiscall + `retn 4` in the original: the CGameEngine in ECX and one
//  int on the stack (both implemented callers push 0: RunFrontEnd at
//  0x61D3CB and the engine destructor at 0x67914C).  It (re)builds the
//  three Lua-environment members of the engine:
//    +0xBC  the Lua module -- the 1032-byte "Fable 3" lua_State host
//           (CreateLuaModule, sub_629570; the old one is released via
//           DestroyLuaModule (sub_6281B0) -> DestroyLuaModuleBody (sub_5EDFE0))
//    +0xC0  the CLuaManager -- the 8-byte wrapper (CLuaManagerCtor,
//           sub_6F9890; its impl wraps the Lua module and the engine's
//           +0x98 sorted map), followed by
//           CLuaManagerRunSerialiseScript (sub_6F9880)
//    +0xC4  the CEntityScriptHandleManager -- 80 bytes (sub_6E9620),
//           holding the Lua-state reference
//  First the old +0xC0 is released (vtable[0](slot, 1) deleting-dtor
//  dispatch) and the net manager's +0x1C sub-object list is cleared
//  (CNetManagerSlot1C + CNetLuaEventManagerListReset).  When the +0xC0 slot was already
//  empty (first-time init), the two startup-script path slots
//  (unk_1DBE2B4 + unk_1DBDA6C via the sub_1A02260 2-source concat) are
//  built and the Lua startup scripts are run (sub_616710).  Register
//  EngineLuaFunctions (sub_6169D0: the ~40 Set*/Get* engine Lua
//  functions) is called on the fresh Lua module; when `flag` is set the
//  "EngineTest" module is registered too (nullsub_28 + sub_70DDB0), and
//  the nullsub_27 no-op is called when the new +0xC0 is set.  The
//  return value (the new +0xC4) is discarded by both callers.
// ============================================================
// 0x1DBE2B4 / 0x1DBDA6C: the two shared slots ReinitEngineLua concats
// into the Lua startup-script path (sub_1A02260).
static F3SharedBuffer *g_unk_1DBE2B4 = 0;   // 0x1DBE2B4
static F3SharedBuffer *g_unk_1DBDA6C = 0;   // 0x1DBDA6C

// sub_1A02260 (0x1A02260 - 0x1A02292) -- the 2-source shared-slot path
// concat (the same helper as the XLip.cpp copy): zero dst, ref
// (CopySharedSlot) src1, append src2 (sub_1A01940); returns the dst
// slot address.  Both source slots are empty in this build, so the
// stub just zeroes dst.
static int * sub_1A02260(int *a1, int *a2, const char ***a3)
{ (void)a2; (void)a3; *a1 = 0; return a1; }

// sub_629570 (0x629570 - 0x629655) -- CreateLuaModule: build the
// 1032-byte "Fable 3" Lua module (sub_5F7FA0) -- the sub_628DB0 Lua
// allocator, the sub_628100 panic handler ("PANIC: unprotected error
// in call to Lua API"), the 200x200 buffer words and the 512 / 0x10000
// limits -- then register its six callbacks (sub_5FD2D0).  Remaining
// stub: allocate + zero the 1032-byte block and return it.
static void *CreateLuaModule(void)
{
    void *m = LHAlloc(1032);
    if (m != 0)
        memset(m, 0, 1032);
    return m;
}

// ============================================================
//  DestroyLuaModule  (0x6281B0 - 0x6281B5)  -- release the engine's
//  "Fable 3" Lua module (the 1032-byte KoreVM host)
//  [NOT byte-identical] the original is a 5-byte near `jmp sub_5EDFE0`
//  thunk; that tail jump is rebased to the build's DestroyLuaModuleBody,
//  so the E9 displacement differs. A plain C tail call (the forward)
//  emits the same single-jmp shape under /O2.
//
//  void DestroyLuaModule(int luaModule)
//
//  The counterpart of CreateLuaModule (sub_629570). Called by
//  ReinitEngineLua (0x61A4BA, when the +0xBC module is rebuilt) and
//  CGameEngineCleanup (0x61AF0C, post-pump teardown) to release the old
//  Lua module: it forwards the module pointer to the body sub_5EDFE0
//  (DestroyLuaModuleBody), which marks the VM released, dumps its stats,
//  frees the memory-allocation list, runs the finalise step, and
//  deallocates the 1032-byte block through the VM's own heap-dealloc
//  function. (Shared declaration in gameengine.h; both call sites use it.)
// ============================================================
// sub_5D3680 (0x5D3680 - 0x5D372D) -- KoreFindFreeBlock: locate a free
// block of the requested (float) size in the KoreVM block table.  obj is
// the VM allocator object {+0x04 index mask, +0x08 20-byte block table};
// a block-table row is {+0x00 float size, +0x04 state, +0x08 value,
// +0x0C in-use flag, +0x10 next}.  The probe index is
// mask & (int)(float)size (the original truncates through the x87);
// a row matches when state == 3 and its stored float equals the request,
// otherwise the row's next pointer (block offset from the table / 20) is
// followed.  Returns the block index, or -4 (0xFFFFFFFC) when the chain
// is exhausted or unwinds to -4.  thiscall + retn 4 hybrid in the
// original (obj in ECX, size on the stack); a plain free function here.
static int KoreFindFreeBlock(int obj, int size)
{
    int mask   = *(int *)(obj + 4);
    int blocks = *(int *)(obj + 8);
    int idx    = ((int)(float)size) & mask;
    for (;;)
    {
        int state = *(int *)(blocks + idx * 20 + 4);
        int fsize = *(int *)(blocks + idx * 20);
        if (state == 3 && (float)size == (float)fsize)
            return idx;
        int next = *(int *)(blocks + idx * 20 + 16);
        if (next == 0)
            return 0xFFFFFFFC;
        idx = (next - blocks) / 20;
        if (idx == 0xFFFFFFFC)
            return 0xFFFFFFFC;
    }
}

// sub_5D97E0 (0x5D97E0 - 0x5D9937) -- KoreFindFirstFreeAlloc: find the
// first free allocation index in the VM's registry allocator (the vm+0xCC
// object, {+0x04 count field, +0x08 block table, +0x0C 8-byte item array
// {value, in-use flag}, +0x10 item count}).  When the item array is empty
// or its last item is in use the block table is probed instead: starting
// at size count+1 (KoreFindFreeBlock), doubling while the found block is
// still in use (a binary search between the last two sizes once the
// doubling overflows 0x7FFFFFFF -- or, past 0x7FFFFFFF, a linear scan
// from 1 + the in-use item count -- then the block's index is returned).
// Otherwise a binary search returns the first item whose flag is 0 (the
// in-use items are packed at the front).  The value KoreDumpVMStats
// prints as the "Registry Array Size".  thiscall free function in the
// original (obj in ECX, retn); a plain free function here.
// [NOT byte-identical] placement-dependent (relative calls to
// KoreFindFreeBlock).
static int KoreFindFirstFreeAlloc(int obj)
{
    int count = *(int *)(obj + 0x10);
    if (count > 0)
    {
        int items = *(int *)(obj + 0x0C);
        if (*(int *)(items + 8 * count - 4) == 0)
        {
            int result = 0;
            if (count > 1)
                while (count - result > 1)
                {
                    int mid = (result + count) >> 1;
                    if (*(int *)(items + 8 * mid - 4) != 0)
                        result = mid;
                    else
                        count = mid;
                }
            return result;
        }
    }
    if (*(int *)(obj + 4) == 0xFFFFFFFF)
        return count;
    int v5 = count;
    unsigned int v6 = (unsigned int)count + 1;
    int v7 = KoreFindFreeBlock(obj, (int)v6);
    int blocks = 0;
    if (v7 != 0xFFFFFFFC)
    {
        blocks = *(int *)(obj + 8);
        for (;;)
        {
            if (*(int *)(blocks + v7 * 20 + 12) == 0)
                break;
            v5 = (int)v6;
            v6 *= 2;
            if (v6 > 0x7FFFFFFFu)
            {
                int v9 = 1;
                if (count > 0)
                {
                    int p = *(int *)(obj + 0x0C);
                    while (*(int *)(p + 4) != 0)
                    {
                        ++v9;
                        p += 8;
                        if (v9 > count)
                            break;
                    }
                }
                while (KoreFindFreeBlock(obj, v9) == 0xFFFFFFFC)
                    ++v9;
                return v9 - 1;
            }
            v7 = KoreFindFreeBlock(obj, (int)v6);
            if (v7 == 0xFFFFFFFC)
                break;
        }
    }
    while (v6 - (unsigned int)v5 > 1)
    {
        int mid = (int)((v6 + (unsigned int)v5) >> 1);
        int v8 = KoreFindFreeBlock(obj, mid);
        if (v8 == 0xFFFFFFFC || *(int *)(blocks + v8 * 20 + 12) == 0)
            v6 = (unsigned int)mid;
        else
            v5 = mid;
    }
    return v5;
}

// sub_5DEB70 (0x5DEB70 - 0x5DEC12) -- KoreDumpVMStats: dump the KoreVM's
// stats through the VM's printf-like logger (vm+0x2B4, cdecl
// (module, format, ...)): the "KoreVM: %s" line (the module name from
// *(vm+0xE4)+0x48), the registry array size (KoreFindFirstFreeAlloc on
// the vm+0xCC registry allocator -- the original stashes the result in
// its own dead parameter slot and re-reads it for the line; a local is
// equivalent, the sole caller never re-reads the slot), the globals hash
// size (*( *(mod+0x30) )+4) + 1, the intern-string table (vm+0xC0
// allocated, vm+0xC4 + 1 used), the max memory allocated (vm+8 >> 10
// kilobytes), and a blank line.  [NOT byte-identical] placement-dependent
// (the relative call to KoreFindFirstFreeAlloc) and refs the six format
// strings at their original high VAs (0x01AE2DA0..0x01AE2E48).  mod is
// the 1032-byte module block; *mod is the VM.  The vm == 0 guard keeps
// the stub-world (zeroed module block) a no-op where the original would
// fault.
static void KoreDumpVMStats(int mod)
{
    int *vm = (int *)mod;
    if (vm == 0)
        return;
    int registrySize = KoreFindFirstFreeAlloc(vm[0xCC / 4]);
    typedef void (__cdecl *KoreLogFn)(int, const char *, ...);
    KoreLogFn log = (KoreLogFn)(void *)(unsigned int)vm[0x2B4 / 4];
    log(mod, "KoreVM:               %s\n", *(int *)(vm[0xE4 / 4] + 0x48) + 8);
    log(mod, "Registry Array Size:  %d\n", registrySize);
    log(mod, "Globals Hash Size:    %d\n", *(int *)(*(int *)(mod + 0x30) + 4) + 1);
    log(mod, "Intern String Table:  %d allocated, %d used\n", vm[0xC0 / 4], vm[0xC4 / 4] + 1);
    log(mod, "Max memory allocated: %d kilobytes\n", vm[8 / 4] >> 10);
    log(mod, "\n");
}
// sub_5D3370 (0x5D3370 - 0x5D33DD) -- KoreLookupStringTable: hash-chain
// search of the VM's 20-byte-row string table (thiscall + retn 12; the table
// object in ECX is {+0x04 mask, +0x08 row array}, a row is {+0x00 key,
// +0x04 hash, +0x08 value, +0x0C size, +0x10 next absolute row pointer}).
// Probe index = (key ^ ((key ^ (key>>3)) >> 5)) & mask; walk the chain until
// a row matches {key, hash} and copy {row+8, row+12} into the 8-byte out
// parameter; on exhaustion write the zero-initialized "no interned string"
// globals (dword_1DBCFE0 / dword_1DBCFE4, both image-zero) and return out.
// [NOT byte-identical] a thiscall + retn 12 hybrid in the original (declared
// object-first here) and refs the two high-VA globals.
static int KoreLookupStringTable(int table, int out, int key, int hash)
{
    int rows = *(int *)(table + 8);
    if (rows != 0)
    {
        int idx = (key ^ ((key ^ (key >> 3)) >> 5)) & *(int *)(table + 4);
        int *row = (int *)(rows + idx * 20);
        for (;;)
        {
            int next = row[4];
            if (row[0] == key && row[1] == hash)
            {
                *(int *)out = row[2];
                *(int *)(out + 4) = row[3];
                return out;
            }
            row = (int *)next;
            if (next == 0)
                break;
        }
    }
    *(int *)out = 0;       // dword_1DBCFE0 (zero-init "not found" global)
    *(int *)(out + 4) = 0; // dword_1DBCFE4
    return out;
}

// sub_60C5E0 (0x60C5E0 - 0x60C689) -- KoreLookupStringRegistry: the registry
// (odd-object) form of the interned-string lookup (thiscall + retn 16).
// obj's {+0x04 count, +0x1C 16-byte row array} is scanned for a row whose
// first word equals key; on a hit the row's +13 descriptor byte selects the
// result pair {obj word[desc], byte obj[16*(desc>>2) + (desc&3)]}. On a miss
// (or empty array) it falls back to the sub-table at obj+12 through
// KoreLookupStringTable (key, hash), or to the zero globals when that is
// null. [NOT byte-identical] a thiscall + retn 16 hybrid in the original
// (declared object-first here) and refs the two high-VA zero globals.
static int KoreLookupStringRegistry(int obj, int out, int key, int hash, int a4)
{
    (void)a4; // the 4 the sole caller pushes (dead in the original too)
    int count = *(int *)(obj + 4);
    if (count > 0)
    {
        int *row = (int *)(obj + 0x1C);
        for (int i = 0; i < count; ++i, row += 4)
        {
            if (*row == key && row != 0)
            {
                int desc = *(unsigned char *)(row + 13);
                *(int *)out = *(int *)(obj + 4 * desc);
                *(int *)(out + 4) = *(unsigned char *)(obj + 16 * (desc >> 2) + (desc & 3));
                return out;
            }
        }
    }
    int sub = *(int *)(obj + 12);
    if (sub != 0)
    {
        int tmp[2];
        (void)KoreLookupStringTable(sub, (int)tmp, key, hash);
        *(int *)out = tmp[0];
        *(int *)(out + 4) = tmp[1];
        return out;
    }
    *(int *)out = 0;       // dword_1DBCFE0 (zero-init "not found" global)
    *(int *)(out + 4) = 0; // dword_1DBCFE4
    return out;
}

// sub_5DDE20 (0x5DDE20 - 0x5DDE86) -- KoreLookupInternedString: resolve an
// interned string to the {pointer, size} qword (returned in {eax, edx}).
// The table is *(obj+0) + 0xE8 + 4*index; when obj's low bit is set the
// registry form is used (KoreLookupStringRegistry on obj & ~1 with index
// a3), otherwise the plain hash table (KoreLookupStringTable on obj with
// index a2). [NOT byte-identical] placement-dependent (relative calls).
static unsigned __int64 KoreLookupInternedString(int obj, int idxEven, int idxOdd)
{
    int out[2];
    if (obj & 1)
    {
        int table = *(int *)(*(int *)obj + 4 * idxOdd + 0xE8);
        (void)KoreLookupStringRegistry(obj & ~1, (int)out, obj, table, 4);
    }
    else
    {
        int table = *(int *)(*(int *)obj + 4 * idxEven + 0xE8);
        (void)KoreLookupStringTable(obj, (int)out, table, 4);
    }
    return ((unsigned __int64)(unsigned int)out[1] << 32) | (unsigned int)out[0];
}

// sub_5DA0A0 (0x5DA0A0 - 0x5DA14F) -- KoreSwapTableContext: swap the 12-word
// {+0x04..+0x2C} block + the +0x48 word between two 80-byte VM table-context
// objects (a pure word exchange; the original is a usercall taking the two
// objects in EAX/ECX and returns the first). [NOT byte-identical] a plain
// C++ reimplementation of the swap (the original is call-free 175 bytes).
static void KoreSwapTableContext(int a, int b)
{
    int *pa = (int *)a, *pb = (int *)b;
    int t;
    int i;
    for (i = 1; i <= 11; ++i)
    {
        t = pa[i]; pa[i] = pb[i]; pb[i] = t;
    }
    t = pa[18]; pa[18] = pb[18]; pb[18] = t;
}

// sub_5F1A20 (0x5F1A20 - 0x5F1BA6) -- KorePanicTraceback: the "Kore Panic"
// traceback printer — the out-of-scope leaf of the record-table allocation
// path. In the real VM it walks the current VM state and prints a full
// function/local traceback, then never returns; the whole path is dead in the
// stub world (the VM heap never grows or panics), so this is a no-op stub.
// [NOT byte-identical] remaining stub (390-byte traceback emitter).
static void KorePanicTraceback(int obj) { (void)obj; }

// ---- KoreHeapInitGrow's callees: the VM-heap alloc-node bookkeeping ----

// sub_5D56D0 (0x5D56D0 - 0x5D5704) -- KoreHeapPanic: the OOM abort. The
// heap sub-object's +0x3C / +0x40 / +0x44 words are the OOM-handler list
// (head / tail / pending node) whose nodes link through their +0x50 next.
// When the +0x44 pending node is non-null and not already the +0x40 tail it
// is spliced in: the +0x3C head becomes the node's old +0x50 next, the node's
// +0x50 next is cleared, the old tail's +0x50 next becomes the old head, and
// the +0x40 tail becomes the node; then longjmp(*(obj+0), 1) back into the
// KoreHeapInitGrow setjmp frame (the +0x00 slot is the published jmp buffer).
// [NOT byte-identical] a pure thiscall in the original (declared object-first
// here) that ends in a relative call to the _longjmp IAT slot (high-VA
// import), so it is placement-dependent.
static void KoreHeapPanic(int obj)
{
    int *h = (int *)obj;
    int node = h[17];                    // the +0x44 pending OOM-handler node
    if (node != 0 && node != h[16])      // skip when null or already the tail
    {
        int head = h[15];                // the +0x3C OOM-handler list head
        h[15]  = *(int *)(node + 0x50);  // head  := node's next
        *(int *)(node + 0x50) = 0;       // node's next := 0 (detach)
        *(int *)(h[16] + 0x50) = head;   // old tail's next := old head
        h[16] = node;                    // tail := node
    }
    longjmp(*(jmp_buf *)h[0], 1);        // back into the KoreHeapInitGrow setjmp
}

// sub_5D5D30 (0x5D5D30 - 0x5D5DCD) -- KoreDrainFreeList: claim free blocks
// off the free-list object (obj; its block head is obj[10]), freeing the
// unclaimed blocks on the way; returns 1 when the claim (the `claim` word)
// can be satisfied, 0 on OOM (the caller then panics). [NOT byte-identical]
// remaining stub (returns the success result so the grow path never panics).
static int KoreDrainFreeList(int obj, int heap, int claim)
{
    (void)obj; (void)heap; (void)claim;
    return 1;
}

// sub_5DF740 (0x5DF740 - 0x5DFF46) -- KoreInitAllocNode8: adopt / register a
// type-8 (heap-block) alloc node. In re-adopt mode (node != 0) it finishes by
// popping one node off the table (--*(obj+24)). [NOT byte-identical]
// remaining stub (keeps the pop so the KoreHeapInitGrow table walk ends).
static int KoreInitAllocNode8(int obj, int ptr, int node, int flags)
{
    (void)ptr; (void)flags;
    if (node != 0)
        --*(int *)(obj + 24);
    return node;
}

// sub_5E0C30 (0x5E0C30 - 0x5E0ED8) -- KoreInitAllocNode5: adopt / register a
// type-5 (interned key/value string) alloc node (same shape as the type-8
// routine; re-adopt mode pops one node off the table). [NOT byte-identical]
// remaining stub.
static void KoreInitAllocNode5(int obj, int ptr, int node, int flags)
{
    (void)ptr; (void)flags;
    if (node != 0)
        --*(int *)(obj + 24);
}

// sub_5E1770 (0x5E1770 - 0x5E189B) -- KoreInitAllocNode7: adopt / register a
// type-7 alloc node (same shape as the type-8 routine; re-adopt mode pops one
// node off the table). [NOT byte-identical] remaining stub.
static void KoreInitAllocNode7(int obj, int ptr, int node, int flags)
{
    (void)ptr; (void)flags;
    if (node != 0)
        --*(int *)(obj + 24);
}

// sub_5DF9F0 (0x5DF9F0 - 0x5DFB80) -- KoreInitAllocNode9: adopt / register a
// type-9 alloc node (same shape as the type-8 routine; re-adopt mode pops one
// node off the table). [NOT byte-identical] remaining stub.
static int KoreInitAllocNode9(int obj, int ptr, int node, int flags)
{
    (void)ptr; (void)flags;
    if (node != 0)
        --*(int *)(obj + 24);
    return node;
}

// sub_5DFB80 (0x5DFB80 - 0x5DFCD7) -- KoreInitAllocNode10: adopt / register a
// type-10 alloc node (same shape as the type-8 routine; re-adopt mode pops
// one node off the table). [NOT byte-identical] remaining stub.
static void KoreInitAllocNode10(int obj, int ptr, int node, int flags)
{
    (void)ptr; (void)flags;
    if (node != 0)
        --*(int *)(obj + 24);
}

// sub_5DFCE0 (0x5DFCE0 - 0x5DFE81) -- KoreInitAllocNode12: adopt / register a
// type-12 (table) alloc node (same shape as the type-8 routine; re-adopt mode
// pops one node off the table). [NOT byte-identical] remaining stub.
static int KoreInitAllocNode12(int obj, int ptr, int node, int flags)
{
    (void)ptr; (void)flags;
    if (node != 0)
        --*(int *)(obj + 24);
    return node;
}

// sub_5E21E0 (0x5E21E0 - 0x5E229F) -- KoreInitAllocNode: the alloc-node
// init/adopt dispatcher -- the node's type word (node[1]) selects the
// per-type routine. Thiscall + `retn 8`: `this` is ecx, the node pointer
// (a 2-word header, [0] = payload pointer, [1] = type) and the flags are
// the stack parameters. Type 4 (plain buffer) just marks the byte just
// before the buffer with bit 0 (the "adopted" flag) when the buffer
// pointer is non-negative and the flag is still clear, then decrements
// the alloc budget (obj+0x6C) -- unless the guard word (obj+0x70) is -1,
// which suspends budget accounting -- and panics (KoreHeapPanic) when the
// decrement would drive the budget to 0 or below. Types 5 / 7 / 8 / 9 /
// 10 / 12 forward to their per-type routine with node = 0 (adopt mode).
// [NOT byte-identical] the body has relative calls to the per-type
// helpers and to KoreHeapPanic, so it is placement-dependent.
static void KoreInitAllocNode(int obj, int node, int flags)
{
    int *n = (int *)node;
    switch (n[1])
    {
        case 4:
        {
            int p = n[0];
            if (p >= 0 && (*((unsigned char *)p - 1) & 1) == 0)
            {
                *((unsigned char *)p - 1) |= 1;   // mark the buffer header
                int budget = *(int *)(obj + 0x6C) - 1;  // the alloc budget
                if (*(int *)(obj + 0x70) != -1)
                {
                    if (budget <= 0)
                        KoreHeapPanic(obj);   // longjmps to the grow setjmp
                    *(int *)(obj + 0x6C) = budget;
                }
            }
            break;
        }
        case 5:  KoreInitAllocNode5(obj, n[0], 0, flags);  break;
        case 7:  KoreInitAllocNode7(obj, n[0], 0, flags);  break;
        case 8:  (void)KoreInitAllocNode8(obj, n[0], 0, flags);  break;
        case 9:  (void)KoreInitAllocNode9(obj, n[0], 0, flags);  break;
        case 0xA:(void)KoreInitAllocNode10(obj, n[0], 0, flags); break;
        case 0xC:(void)KoreInitAllocNode12(obj, n[0], 0, flags); break;
        default: break;
    }
}

// sub_5E22D0 (0x5E22D0 - 0x5E2331) -- KoreInitAllocList: re-adopt every node
// of the heap's primary alloc-node list (the obj+0x20 list of 8-byte
// {payload, type} pairs, count at obj+0x24) through KoreInitAllocNode. The
// walk runs from the last pair (base + 8*count - 8) backward down to the
// base: each node's header word is unflagged (bit 0 cleared at payload-4 for
// types 4 / 7, else payload-8), the list count (obj+0x24) is popped, and the
// node pair is re-adopted through KoreInitAllocNode via a stack copy of the
// two words, with flags - 1. A null list base skips the walk entirely.
// [NOT byte-identical] a thiscall + retn 4 hybrid in the original (the heap
// object in ECX, the flags word on the stack; declared object-first here) and
// placement-dependent (relative call into KoreInitAllocNode). The sole caller
// (KoreHeapInitGrow stage 1) discards the return value.
static int KoreInitAllocList(int obj, int flags)
{
    int *h = (int *)obj;
    int base = h[5];                 // obj+0x20 primary alloc-node list
    if (base != 0)
    {
        int *cur = (int *)(base + 8 * h[6] - 8);   // the last node pair
        for (;;)
        {
            if ((unsigned int)cur < (unsigned int)base)
                break;
            int payload = cur[0];
            int type    = cur[1];
            *(int *)((type == 4 || type == 7) ? payload - 4
                                              : payload - 8) &= ~1;
            --h[6];                  // pop the node off the list
            int nodeCopy[2] = { payload, type };
            KoreInitAllocNode(obj, (int)nodeCopy, flags - 1);
            cur -= 2;
        }
    }
    return 0;
}

// sub_5E2340 (0x5E2340 - 0x5E23B6) -- KoreInitAllocTable: re-adopt every node
// of the heap's secondary alloc-node table chain (obj+0x3C), zeroing the
// retired blocks. [NOT byte-identical] remaining stub.
static void KoreInitAllocTable(int obj, int flags)
{
    (void)obj; (void)flags;
}

// sub_5DC610 (0x5DC610 - 0x5DC721) -- KoreForEachAllocNode: walk every alloc
// node known to obj and invoke the callback (a1, a2) on each. [NOT
// byte-identical] remaining stub.
static int KoreForEachAllocNode(int obj, int (*callback)(int, int))
{
    (void)obj; (void)callback;
    return 0;
}

// sub_5D59E0 (0x5D59E0 - 0x5D5A1D) -- KoreMarkAllocTree: mark the alloc node
// at a2 (and its children, a2+20 count / a2+24 array) with the +(-8) bit.
// [NOT byte-identical] remaining stub.
static int KoreMarkAllocTree(int a1, int a2)
{
    (void)a1; (void)a2;
    return 0;
}

// sub_5D5980 (0x5D5980 - 0x5D59D5) -- KoreSweepAllocNodes: sweep the heap's
// alloc-node chain (obj+0x04 sub-object, its +76 list), dropping the unmarked
// nodes and charging the marked ones against the alloc budget (obj+0x6C).
// [NOT byte-identical] remaining stub.
static void KoreSweepAllocNodes(int obj)
{
    (void)obj;
}

// sub_5E8220 (0x5E8220 - 0x5E83D9) -- KoreRebindAllocNodes: rebind the heap's
// alloc nodes to the (possibly new) heap storage after a grow, updating the
// alloc budget (obj+0x6C). [NOT byte-identical] remaining stub.
static int KoreRebindAllocNodes(int obj)
{
    (void)obj;
    return 0;
}

// sub_5ED980 (0x5ED980 - 0x5EDAB2) -- KoreReassignAllocHeap: move the alloc
// node chain from the old heap (the vm-core's +224 slot) onto the heap
// sub-object's +0x88 chain, releasing the retired nodes. [NOT byte-identical]
// remaining stub.
static int KoreReassignAllocHeap(int obj, int vmcore)
{
    (void)obj; (void)vmcore;
    return 0;
}

// sub_5D9FC0 (0x5D9FC0 - 0x5DA09C) -- KoreRehashAllocTable: rebuild / refresh
// the alloc-node hash table (the heap sub-object's +0x80 table, +0x84 cursor)
// after a grow, charging the alloc budget (obj+0x6C). [NOT byte-identical]
// remaining stub.
static int KoreRehashAllocTable(int obj)
{
    (void)obj;
    return 0;
}

// sub_5EDAC0 (0x5EDAC0 - 0x5EDDB7) -- KoreHeapInitGrow: the setjmp/longjmp
// state machine that (re)initialises / grows the VM heap (thiscall on the
// heap+0x2C sub-object, the grow size / vm-core on the stack). It publishes
// a local setjmp buffer in the sub-object's +0x00 slot (KoreHeapPanic and the
// other OOM paths longjmp back into it) and then resumes the six-stage
// machine from the persisted +0x10 state: (0) zero the OOM-handler list and
// register the heap object as an alloc node; (1) re-adopt every alloc node of
// the type tables, the two inline nodes at +0x34 / +0x48, and the eight fixed
// nodes of the API table at +0x1F0 .. +0x228, mark them, and flag the array
// blocks; (2) sweep + rebind the alloc nodes; (3) reassign the alloc chain
// from the old heap; (4) rehash the alloc table and reset the free list;
// (5) drain the free list (panics when it cannot claim a block); (6) compute
// the new grow size (48 * +0x1C + 20 * +0x58 + 2576), warn through the API
// table's +0x2B4 printer when occupancy is over the +0x8C percent threshold,
// store floor(used * +0x5C percent / 100) at +0x74 (the FPU is put in
// round-toward-zero with a 2^32 bias for negative values, only the low dword
// is kept), and set the API table's +0x2B0 flag. On the way out it zeroes the
// +0x00 slot and adds the QPC span to the API table's +0x2A8 / +0x2AC
// 64-bit counter. Returns 1 on completion, 0 when a panic longjmped in (with
// +0x6C restored from +0x70); a call with a live +0x00 slot returns 0 at
// once. [NOT byte-identical] a thiscall + retn 4 hybrid in the original
// (declared object-first here), placement-dependent (relative calls into the
// alloc-node helpers), and refs the "Low memory warning" string, the 100.0 /
// 2^32 doubles, and the API table's mutable slots.
static int KoreHeapInitGrow(int obj, int size)
{
    int *h = (int *)obj;
    if (h[0] != 0)
        return 0;

    // aLowMemoryWarni (0x1AE1454)
    static const char lowMemoryWarning[] =
        "Low memory warning: %d out of %d (%d%%) bytes occupied\n";
    // dbl_1AE36F8 (100.0) / dbl_1BF9398 (2^32)
    static const double koreHundredDouble    = 100.0;
    static const double koreTwoThirtyTwoBias = 4294967296.0;
    // first arg lands in ECX (the original's "this"), the rest on the stack
    typedef void (__thiscall *KoreLowMemoryPrintFn)(int, int, const char *, int, int, int);

    unsigned __int64 start = __rdtsc();
    jmp_buf jmb;
    h[0] = (int)jmb;
    int ret = 0;
    if (setjmp(jmb) != 0)                       // a panic longjmped back in
    {
        h[27] = h[28];                          // restore +0x6C from +0x70
    }
    else
    {
        switch (h[4])
        {
        case 0:
            h[6]  = -1;                         // the type-table node count
            h[15] = h[16] = h[17] = 0;          // the +0x3C OOM-handler list
            h[4]  = 1;
            (void)KoreInitAllocNode8(obj, h[1], 0, 0x40);
            // fall through (the original falls into the case 1 code)
        case 1:
            {
            while (h[6] >= 0)
            {
                int node = h[5] + 24 * h[6];
                switch (*(int *)node)
                {
                case 5:  (void)KoreInitAllocNode5(obj, 0, node, 0x40);  break;
                case 7:  (void)KoreInitAllocNode7(obj, 0, node, 0x40);  break;
                case 8:  (void)KoreInitAllocNode8(obj, 0, node, 0x40);  break;
                case 9:  (void)KoreInitAllocNode9(obj, 0, node, 0x40);  break;
                case 0xA:(void)KoreInitAllocNode10(obj, 0, node, 0x40); break;
                case 0xC:(void)KoreInitAllocNode12(obj, 0, node, 0x40); break;
                default: break;
                }
            }
            (void)KoreInitAllocList(obj, 0x40);
            if (h[14] != 0)
                KoreInitAllocNode(obj, (int)(h + 13), 0x40);
            if (h[19] != 0)
                KoreInitAllocNode(obj, (int)(h + 18), 0x40);
            int table = *(int *)h[1];           // the API table behind the heap
            for (int off = 0x1F0; off < 0x230; off += 8)
                KoreInitAllocNode(obj, table + off, 0x40);
            KoreInitAllocTable(obj, 0x40);
            if (h[20] != 0)
                (void)KoreForEachAllocNode(h[20], KoreMarkAllocTree);
            int child = h[21];
            if (child != 0)
            {
                int kid = *(int *)(child + 56);
                if (kid != 0)
                    (void)KoreMarkAllocTree(0, kid);
            }
            *(int *)h[3]           = 1;         // the free-list head tag
            *(int *)(h[5] - 8)    |= 1;         // the type-table block flag
            if (h[8] != 0)
                *(int *)(h[8] - 8) |= 1;        // the second array block flag
            h[4] = 2;
            }
            // fall through
        case 2:
            KoreSweepAllocNodes(obj);
            (void)KoreRebindAllocNodes(obj);
            h[4] = 3;
            // fall through
        case 3:
            (void)KoreReassignAllocHeap(obj, size);
            h[4] = 4;
            // fall through
        case 4:
            (void)KoreRehashAllocTable(obj);
            {
                int *freeList = (int *)h[3];
                freeList[0]  = 0;
                freeList[9]  = freeList[7] ^ ((freeList[7] ^ freeList[9]) & 3);
                freeList[10] = (int)(freeList + 8);
                freeList[7] &= 3;
            }
            h[4] = 5;
            // fall through
        case 5:
            {
                int claim = (h[28] != -1) ? 1 : 0;
                if (KoreDrainFreeList(h[3], h[1], claim) == 0)
                {
                    h[27] = 0;
                    if (h[28] != -1)
                        KoreHeapPanic(obj);     // longjmps to the setjmp above
                }
            }
            h[4] = 6;
            // fall through
        case 6:
            {
                int threshold = h[35];
                h[31] = 48 * h[7] + 20 * h[22] + 2576;  // the new grow size
                h[22] = 0;
                int used = *(int *)(h[3] + 4);
                if (threshold != 0)
                {
                    int pct = 100 * used / used;  // 100, exactly as the original div
                    if (pct > threshold)
                    {
                        int *table = (int *)h[1];
                        ((KoreLowMemoryPrintFn)(table + 173))(used, h[1],
                            lowMemoryWarning, used, used, pct);
                    }
                }
                double scale  = (double)h[23] / koreHundredDouble;
                double scaled = (double)used;
                if (used < 0)
                    scaled += koreTwoThirtyTwoBias;
                h[4] = 0;
                unsigned short cw = _controlfp(0, 0);
                _controlfp(_RC_CHOP, _MCW_RC);       // round toward zero
                h[29] = (int)(__int64)(scaled * scale);   // only the low dword kept
                _controlfp(cw, 0xFFFF);
                *(char *)((int *)h[1] + 0x2B0) = 1;       // the API-table flag
            }
            break;
        default:
            break;
        }
        ret = 1;
    }

    // the QPC span goes onto the API table's 64-bit counter at +0x2A8 / +0x2AC
    unsigned __int64 now   = __rdtsc();
    unsigned __int64 delta = now - start;
    h[0] = 0;
    int *table = (int *)h[1];
    unsigned __int64 sum = (unsigned __int64)(unsigned int)table[170] + (unsigned int)delta;
    table[170] = (int)sum;
    table[171] = (int)((sum >> 32) + (unsigned int)(delta >> 32));
    return ret;
}

// sub_5F6420 (0x5F6420 - 0x5F64A5) -- KoreVmAllocExpand: the last-resort
// expansion for a failed VM-heap allocation (the OOM path). First retries a
// direct alloc through the heap's +0x294 function pointer and returns 1 when
// it satisfies the size; otherwise it records a {heap+0x144, heap+0x148} pair
// at the vm-core's +0x1C entry cursor (advancing it by 8), zeroes the heap's
// +0x78 and +0x64 counters, and either longjmp(-200) back through the
// vm-core's +0x40 saved jmp buffer (when set) or calls the heap's +0x290
// handler and the KorePanicTraceback. [NOT byte-identical] placement-dependent
// (the relative call to KorePanicTraceback) and refs the heap's +0x290 / +0x294
// function slots.
static int KoreVmAllocExpand(int vmcore, int size)
{
    int *heap = (int *)*(int *)vmcore;
    int fn = *(int *)((char *)heap + 0x294);
    int r  = ((int (*)(int, int))fn)(vmcore, size);
    if (r >= size)
        return 1;
    *(int *)((char *)heap + 0x78) = 0;
    int *cur = (int *)(vmcore + 0x1C);
    cur[0]   = *(int *)((char *)heap + 0x144);
    cur[1]   = *(int *)((char *)heap + 0x148);
    *(int *)(vmcore + 0x1C) = (int)(cur + 2);
    *(int *)((char *)heap + 0x64) = 0;
    int jmpbuf = *(int *)(vmcore + 0x40);
    if (jmpbuf != 0)
    {
        *(int *)(vmcore + 0x40) = *(int *)(jmpbuf + 0x40);
        longjmp(*(jmp_buf *)jmpbuf, -200);
    }
    int handler = *(int *)((char *)heap + 0x290);
    if (handler != 0)
        ((void (*)(int))handler)(vmcore);
    (void)KorePanicTraceback(vmcore);
    return 0;
}

// sub_5DA1C0 (0x5DA1C0 - 0x5DA238) -- KoreHeapBlockAlloc: allocate a block of
// the given size from the VM heap's bump region (thiscall + retn 4; the heap
// object in ECX is {+0x00 tag word, +0x04 bump cursor, +0x08 high-water, +0x0C
// alloc-fn ptr, +0x10 alloc-fn context, +0x1C free-list link}). Advance the
// bump cursor by size+8 and call the alloc-fn (context, 0, 0, size+8); on
// success stamp the 8-byte block header {tag | ((size+8)*4 & ~1), free-list
// link = heap+0x1C & ~3} at the block and relink the free-list tag, on failure
// revert the cursor; returns the block at result+8 (the 8-byte header sits
// just before it). [NOT byte-identical] a thiscall + retn 4 hybrid in the
// original (declared object-first here).
static int KoreHeapBlockAlloc(int obj, int size)
{
    int *h    = (int *)obj;
    int bump  = h[1];
    int bytes = size + 8;
    h[1] = bump + bytes;
    int r = ((int (*)(int, int, int, int))h[3])(h[4], 0, 0, bytes);
    if (r == 0)
    {
        h[1] = bump;
        return 0;
    }
    *(int *)r       = ((bytes * 4) & ~1) | h[0];
    *(int *)(r + 4) = h[7] & ~3;
    h[7] = (h[7] & 3) | r;
    h[2] = (h[2] > h[1]) ? h[2] : h[1];
    return r + 8;
}

// sub_5EE150 (0x5EE150 - 0x5EE196) -- KoreGrowRegistry: the VM-heap grow
// wrapper (thiscall + retn 8; the heap+0x2C sub-object in ECX, the grow size in
// the first stack arg, the vm-core in the second). When the sub-object's +0x68
// notify slot is set and fires (called with the sub-object's +0x04 arg) it
// aborts (returns 0); otherwise it temporarily sets the sub-object's +0x70 slot
// to the vm-core (when non-zero), mirrors it to +0x6C, drives the heap grow
// through KoreHeapInitGrow, and restores +0x70. [NOT byte-identical] a thiscall
// + retn 8 hybrid in the original (declared object-first here).
static int KoreGrowRegistry(int obj, int size, int vmcore)
{
    int *b      = (int *)obj;
    int handler = b[0x68 / 4];
    if (handler != 0)
    {
        int r = ((int (*)(int))handler)(b[1]);
        if (r != 0)
            return 0;
    }
    int saved = b[0x70 / 4];
    if (vmcore != 0)
        b[0x70 / 4] = vmcore;
    b[0x6C / 4] = b[0x70 / 4];
    int r = KoreHeapInitGrow(obj, size);
    b[0x70 / 4] = saved;
    return r;
}

// sub_5DA4F0 (0x5DA4F0 - 0x5DA5B5) -- KoreVmAlloc: the VM-heap bump allocator
// (the primary size-class allocator; the vm-core object is the first arg, the
// size the second; returns the allocated block or 0). A zero size returns 0;
// otherwise it decrements the heap's free-block counter (+0xA4) and, when the
// pending-alloc high-water (+0x38) has overrun the +0xA0 bound with the counter
// drained and the +0x90 flag clear, re-seeds the counter to 317 and grows the
// registry (KoreGrowRegistry over heap+0x2C). It then bump-allocates (advance
// heap+0x04 by the size and call the heap's +0x0C alloc-fn with the +0x10
// context) up to three times, growing the registry after each miss and, on the
// third miss, driving the last-resort expand (KoreVmAllocExpand); a hit updates
// the +0x08 high-water and returns the block. [NOT byte-identical]
// placement-dependent (relative calls to the grow / expand helpers) and refs the
// heap's +0x0C alloc-fn slot.
static int KoreVmAlloc(int vmcore, int size)
{
    int *heap = (int *)*(int *)vmcore;
    if (size == 0)
        return 0;
    --heap[0xA4 / 4];
    if (*(int *)(heap[0x38 / 4] + 4) > heap[0xA0 / 4]
        && heap[0xA4 / 4] <= 0
        && heap[0x90 / 4] == 0)
    {
        int count   = heap[0x8C / 4];
        heap[0xA4 / 4] = 317;
        int newsize = (count == -1) ? 0 : 317 * count;
        (void)KoreGrowRegistry((int)((char *)heap + 0x2C), newsize, vmcore);
    }
    int retry = 3;
    for (;;)
    {
        int bump = heap[1];
        heap[1] = bump + size;
        --retry;
        int result = ((int (*)(int, int, int, int))heap[3])(heap[4], 0, 0, size);
        if (result != 0)
        {
            heap[2] = (heap[2] > heap[1]) ? heap[2] : heap[1];
            return result;
        }
        heap[1] = bump;
        if (retry == 0 && KoreVmAllocExpand(vmcore, size) != 0)
            retry = 3;
        (void)KoreGrowRegistry((int)((char *)heap + 0x2C), -1, vmcore);
        if (retry == 0)
            return 0;
    }
}

// sub_5EEAE0 (0x5EEAE0 - 0x5EEBA3) -- KoreVmHeapAlloc: allocate a block from
// the VM heap for a record-table build (the vm-core object is the first arg,
// the size the second; returns the allocated block or 0). It mirrors the
// growth re-seed of KoreVmAlloc (decrement the free-block counter +0xA4,
// re-seed it to 317 and grow the registry over heap+0x2C when the pending
// high-water overruns +0xA0 with the counter drained and +0x90 clear), then
// block-allocates (KoreHeapBlockAlloc) in a bounded retry: after each miss it
// fires the heap's +0x94 error handler (with the +0x30 arg; a non-zero result
// aborts), sets the heap+0x2C sub-object's +0x9C / +0x98 slots to -1 and drives
// the heap grow through KoreHeapInitGrow, restoring +0x9C; on the third miss it
// first drives the last-resort expand (KoreVmAllocExpand) before the grow, and
// only gives up (returns 0) once the expand has not reset the retry count.
// [NOT byte-identical] placement-dependent (relative calls to the block / grow /
// expand helpers) and refs the heap's +0x94 handler slot.
static int KoreVmHeapAlloc(int vmcore, int size)
{
    int *heap = (int *)*(int *)vmcore;
    --heap[0xA4 / 4];
    if (*(int *)(heap[0x38 / 4] + 4) > heap[0xA0 / 4]
        && heap[0xA4 / 4] <= 0
        && heap[0x90 / 4] == 0)
    {
        int count   = heap[0x8C / 4];
        heap[0xA4 / 4] = 317;
        int newsize = (count == -1) ? 0 : 317 * count;
        (void)KoreGrowRegistry((int)((char *)heap + 0x2C), newsize, vmcore);
    }
    int retry = 3;
    for (;;)
    {
        --retry;
        int result = KoreHeapBlockAlloc((int)heap, size);
        if (result != 0)
            return result;
        if (retry == 0 && KoreVmAllocExpand(vmcore, size) != 0)
            retry = 3;
        int handler = heap[0x94 / 4];
        int hr = 0;
        if (handler != 0)
        {
            hr = ((int (*)(int))handler)(heap[0x30 / 4]);
            if (hr != 0)
                return hr;
        }
        int saved = heap[0x9C / 4];
        heap[0x9C / 4] = -1;
        heap[0x98 / 4] = -1;
        (void)KoreHeapInitGrow((int)((char *)heap + 0x2C), vmcore);
        heap[0x9C / 4] = saved;
        if (retry == 0)
            return 0;
    }
}

// sub_5DEA70 (0x5DEA70 - 0x5DEAC6) -- KoreSetupRecordTableBuffers: set up the
// two backing buffers of a freshly allocated 112-byte record-table object (a
// usercall passing the block in ESI). Allocate a 440-byte buffer (zeroed) and
// publish it at the block's +0x1C / +0x20 / +0x28 words (all the buffer) and
// +0x24 (the buffer's +400 tail); then a 128-byte buffer published at the
// block's +0x04 (head), +0x08 (head+128) and +0x0C (head-16) words; and mark
// the block's +0x18 word -2. Returns the -16 base of the 128-byte buffer.
// [NOT byte-identical] a usercall in the original (declared object-first here)
// and placement-dependent (relative calls to KoreVmAlloc).
static int KoreSetupRecordTableBuffers(int obj)
{
    int *b    = (int *)obj;
    int buf1  = KoreVmAlloc(obj, 440);
    memset((void *)buf1, 0, 440);
    b[9]  = buf1 + 400;
    b[8]  = buf1;
    b[7]  = buf1;
    b[10] = buf1;
    int buf2 = KoreVmAlloc(obj, 128);
    b[1]  = buf2;
    b[2]  = buf2 + 128;
    b[3]  = buf2 - 16;
    b[6]  = -2;
    return buf2 - 16;
}

// sub_5DEAD0 (0x5DEAD0 - 0x5DEB6D) -- KoreBuildRecordTable: build a fresh
// 112-byte record-table object from the vm-core (the vm-core is the sole arg;
// returns the new block). Allocate 112 bytes (KoreVmHeapAlloc), zero them, clear
// bit 0 and set bit 1 of the 4-byte header word just before the block, then
// clone the table-context fields from the vm-core: the +0x00 head, the +0x30 /
// +0x34 words, the +0x48 word (from *vmcore+0x1B4, later corrected by the
// caller), the +0x58..+0x6C run, and the +0x4C chain pointer (linked into the
// *vmcore+0xE4 chain); record the {block, 8} entry at the vm-core's +0x1C entry
// cursor (advancing it by 8); and set up the two backing buffers
// (KoreSetupRecordTableBuffers). [NOT byte-identical] placement-dependent
// (relative calls to KoreVmHeapAlloc / KoreSetupRecordTableBuffers).
static int KoreBuildRecordTable(int vmcore)
{
    int *block = (int *)KoreVmHeapAlloc(vmcore, 112);
    memset(block, 0, 112);
    *(int *)((char *)block - 4) =
        (*(int *)((char *)block - 4) & 0xFFFFFFFE) | 2;
    block[18] = *(int *)(*(int *)vmcore + 0x1B4);
    block[12] = *(int *)(vmcore + 0x30);
    block[13] = *(int *)(vmcore + 0x34);
    block[0]  = *(int *)vmcore;
    block[22] = *(int *)(vmcore + 0x58);
    block[23] = *(int *)(vmcore + 0x5C);
    block[24] = *(int *)(vmcore + 0x60);
    block[25] = *(int *)(vmcore + 0x64);
    block[26] = *(int *)(vmcore + 0x68);
    block[27] = *(int *)(vmcore + 0x6C);
    int chainHead = *(int *)(*(int *)vmcore + 0xE4);
    block[19]  = *(int *)(chainHead + 0x4C);
    *(int *)(chainHead + 0x4C) = (int)block;
    int *cursor = (int *)*(int *)(vmcore + 0x1C);
    cursor[0]   = (int)block;
    cursor[1]   = 8;
    *(int *)(vmcore + 0x1C) = (int)(cursor + 2);
    (void)KoreSetupRecordTableBuffers((int)block);
    return (int)block;
}

// sub_5DFE90 (0x5DFE90 - 0x5DFEB9) -- KoreAllocRecordTable: lazily create the
// allocation list's +0x08 record-table object (thiscall on the list object,
// whose +0x04 word is the vm-core). Build the 112-byte record table from the
// vm-core (KoreBuildRecordTable), publish it at the list's +0x08, set the
// table's +0x48 word to *vmcore+0x14C, and rewind the vm-core's +0x1C entry
// cursor by 8 (KoreBuildRecordTable advanced it by 8); returns the new table.
// [NOT byte-identical] placement-dependent (the relative call to
// KoreBuildRecordTable).
static int KoreAllocRecordTable(int obj)
{
    int vmcore = *(int *)(obj + 4);
    int table  = KoreBuildRecordTable(vmcore);
    *(int *)(obj + 8) = table;
    *(int *)(table + 0x48) = *(int *)(*(int *)vmcore + 0x14C);
    *(int *)(vmcore + 0x1C) -= 8;
    return table;
}

// sub_5EC960 (0x5EC960 - 0x5ECACA) -- KoreHashTableInsert: the
// setjmp/longjmp-guarded insert of a table entry (thiscall on the table,
// the table also as the first stack arg): swap the table's +0x40 slot with a
// 64-byte stack jmp buffer, run the table core (sub_5EC770), restore the
// slot, and unwind through the saved chain (sub_407540 / sub_5D2950) on the
// longjmp path; returns 0 on the no-exception path. Remaining stub (the
// 459-byte table core + its resize/error cluster is out of scope); the no-op
// returns 0 (the no-exception result) and leaves the table untouched.
static int KoreHashTableInsert(int obj, int a1, int a2, int a3, int a4)
{ (void)obj; (void)a1; (void)a2; (void)a3; (void)a4; return 0; }

// sub_5ED860 (0x5ED860 - 0x5ED90E) -- KoreReleaseAllocNode: the per-node
// step of the allocation-list free (thiscall on the list + retn 8: stack
// args are the node, then the vm-core object vmcore = list+0x04). Look up
// the node's interned string (node+0x04 is the string id):
// (name, size) = KoreLookupInternedString(vmcore, id, 0x0F); when size != 0,
// lazily create the list's record table (KoreAllocRecordTable on list+0x08
// when null), swap the vm-core's table context into the record table
// (KoreSwapTableContext), append the {name, size, node, 7} 16-byte entry at
// the vm-core's entry cursor (+0x1C, advanced by 16), and insert it into the
// hash table (KoreHashTableInsert, the list+0x30 re-entrancy flag set for
// the duration). A non-zero insert result truncates the entry array: zero
// the second word of each 8-byte pair from the new cursor to +0x20 and set
// the cursor to +0x20. Swap the context back; returns the name word (the
// original returns the lookup qword; both callers ignore it).
// [NOT byte-identical] placement-dependent (relative calls to the helpers).
static int KoreReleaseAllocNode(int list, int node, int vmcore)
{
    int id = *(int *)(node + 4);
    if (id == 0)
        return 0;
    unsigned __int64 r = KoreLookupInternedString(vmcore, id, 0x0F);
    int name = (int)r;
    int size = (int)(r >> 32);
    if (size == 0)
        return name;
    if (*(int *)(list + 8) == 0)
        KoreAllocRecordTable(list);
    int table = *(int *)(list + 8);
    KoreSwapTableContext(table, vmcore);
    int *ent = (int *)*(int *)(vmcore + 0x1C);
    ent[0] = name;
    ent[1] = size;
    ent[2] = node;
    ent[3] = 7;
    *(int *)(vmcore + 0x1C) = (int)(ent + 4);
    *(int *)(list + 0x30) = 1;
    int r2 = KoreHashTableInsert(vmcore, vmcore, 1, 0, 0);
    *(int *)(list + 0x30) = 0;
    if (r2 != 0)
    {
        int *cur = (int *)*(int *)(vmcore + 0x1C);
        int end = *(int *)(vmcore + 0x20);
        for (;;)
        {
            if ((int)cur >= end)
                break;
            cur[1] = 0;
            cur += 2;
        }
        *(int *)(vmcore + 0x1C) = end;
    }
    KoreSwapTableContext(table, vmcore);
    return name;
}

// sub_5ED910 (0x5ED910 - 0x5ED97E) -- KoreFreeAllocList: free the VM's
// memory-allocation list (thiscall on the VM's +0x2C list object, which is
// {+0x00 state flag, +0x04 vm-core object P, +0x08 record-table ptr,
// +0x0C heap object}). Set the state flag to -1, then walk P's
// allocation-node freelist (head at *(P+0)+0xE0; a node is {+0x04 string
// id, +0x08 next, +0x0C size}, with the block flag word at node-4): release
// the node's interned
// string record (KoreReleaseAllocNode), then dealloc the node's block
// through the list's heap object — decrement the used-byte counter
// (heap+0x04) by size + 20 and call heap+0x0C(heap+0x10, node-4, size+20,
// 0) (the 4 bytes before the node are the block header). Zero the freelist
// head (P+0xE0) and the record-table pointer (list+0x08); returns P (the
// original's return value, ignored by both callers).
// [NOT byte-identical] placement-dependent (the relative call to
// KoreReleaseAllocNode).
static int KoreFreeAllocList(int list)
{
    int vmcore = *(int *)(list + 4);
    *(int *)list = -1;
    int node = *(int *)(((int *)vmcore)[0] + 0xE0);
    while (node != 0)
    {
        KoreReleaseAllocNode(list, node, vmcore);
        int next = *(int *)(node + 8);
        int heap = *(int *)(list + 0xC);
        int bytes = *(int *)(node + 0xC) + 20;
        *(int *)(heap + 4) -= bytes;
        int fn = *(int *)(heap + 0xC);
        int ctx = *(int *)(heap + 0x10);
        ((void (*)(int, int, int, int))fn)(ctx, node - 4, bytes, 0);
        node = next;
    }
    *(int *)(((int *)vmcore)[0] + 0xE0) = 0;
    *(int *)(list + 8) = 0;
    return vmcore;
}
// sub_5DA690 (0x5DA690 - 0x5DA6D2) -- the VM finalise step (thiscall on the
// VM's +0xBC object with the module as the second arg): the finalise loop over
// the VM's remaining slots. Remaining stub (deep VM internals out of scope).
static void KoreFinalizeVM(int obj, int a1) { (void)obj; (void)a1; }
// sub_5DA150 (0x5DA150 - 0x5DA1C0) -- detach the VM's heap: free the two
// pending-allocation lists (sub_5D5B20 each), zero the +0x28 slot, and pull
// out the 3-word heap-dealloc context {vm[3] (the dealloc fn), vm[4], vm[5]}
// into the out pointer. Remaining stub (deep VM internals out of scope).
static void KoreDetachHeap(int obj, int heap) { (void)obj; (void)heap; }

// sub_5EDFE0 (0x5EDFE0 - 0x5EE03C) -- DestroyLuaModuleBody: the Lua module's
// (KoreVM's) release.  [NOT byte-identical] placement-dependent (relative
// calls to the four VM-teardown helpers above).
//
//  void DestroyLuaModuleBody(int luaModule)
//
//  luaModule is the 1032-byte block; *luaModule is the VM object it hosts.
//  (In the real binary *luaModule is always the valid VM object; the stub
//  CreateLuaModule leaves the block zeroed, so the guard below keeps that
//  path a no-op instead of writing to address 0x29C.)
static void DestroyLuaModuleBody(int luaModule)
{
    int *mod = (int *)luaModule;
    int *vm  = (int *)mod[0];
    if (vm == 0)
        return;
    vm[0x29C / 4] = -1;                                 // mark the VM released
    KoreDumpVMStats((int)mod);                          // sub_5DEB70(mod)
    KoreFreeAllocList((int)(vm + 0x2C / 4));            // sub_5ED910 (thiscall vm+0x2C)
    KoreFinalizeVM((int)(vm + 0xBC / 4), luaModule);    // sub_5DA690 (thiscall vm+0xBC, a1)
    KoreDetachHeap((int)vm, 0);                         // sub_5DA150 (thiscall vm, &heap)
    if (vm[3] != 0)                                     // the VM's heap-dealloc fn (vm[3])
        ((void (*)(int, int, int, int))vm[3])(vm[4], (int)vm, 1032, 0);  // free the 1032-byte block
}

void DestroyLuaModule(int luaModule)
{
    DestroyLuaModuleBody(luaModule);   // the original's 5-byte `jmp sub_5EDFE0`
}

// CNetLuaEventManagerListReset (0x6AA2C0 - 0x6AA2EC) -- clear the net
// manager's +0x1C sub-object's internal list (sub_6AB810 + list reset);
// implemented in netinit.cpp, declared in netinit.h.

// CNetManagerSlot1C (0x6A6900 - 0x6A6904) -- thiscall getter of the
// CNetManager's +0x1C sub-object (the same stub as the gamethread.cpp
// copy; GetNetManager is canonical in netinit.h).
static void *CNetManagerSlot1C(void *obj) { return *(void **)((char *)obj + 0x1C); }

// Placeholder vtables for the Lua-environment members built by the stub
// ctors below (the same trick as the g_CLoadingScreenVtable set): slot 0
// must be a callable no-op so the deleting-dtor dispatch (vt[0](obj, 1))
// in ReinitEngineLua and CGameEngineDestroy never dereferences an
// unconstructed table.
static void *g_CLuaManagerVtable[2]                = { (void *)F3StubRelease, 0 };
static void *g_CEntityScriptHandleManagerVtable[2] = { (void *)F3StubRelease, 0 };

// sub_6F9890 (0x6F9890 - 0x6F98F2) -- CLuaManagerCtor: CBaseClassNon
// CopyableCtor, store &CLuaManager::`vftable', zero the +4 impl slot,
// LHAlloc(708) + build the impl via sub_6F7F60(luaModule, sortedMap).
// Remaining stub: store the placeholder vtable + zero the impl slot.
static void *CLuaManagerCtor(void *obj, void *luaModule, void *sortedMap)
{ (void)luaModule; (void)sortedMap;
  *(void **)obj = g_CLuaManagerVtable; *(void **)((char *)obj + 4) = 0; return obj; }

// sub_6F9880 (0x6F9880 - 0x6F9888) -- CLuaManagerRunSerialiseScript:
// forward to the impl (sub_6F9790: the "Lua serialise script ... has an
// error in it" path check + sub_6F0BF0 / sub_6F9690 / sub_627FC0).
// Remaining stub.
static void CLuaManagerRunSerialiseScript(void *obj) { (void)obj; }

// sub_6169D0 (0x6169D0 - 0x6171A5) -- RegisterEngineLuaFunctions:
// register the ~40 engine Set*/Get* Lua functions (SetApplicationName,
// SetScreenResolution, SetInitialWorldName, ..., GetDemoMode) on the
// Lua module.  Remaining stub.
static void RegisterEngineLuaFunctions(int luaModule) { (void)luaModule; }

// sub_616710 (0x616710 - 0x616894) -- RunLuaStartupScripts: run the
// startup .lua script (error path "Lua startup script has an error in
// it" via sub_629660) plus the optional per-locale startup scripts.
// Remaining stub.
static void RunLuaStartupScripts(int luaModule, int scriptPath)
{ (void)luaModule; (void)scriptPath; }

// sub_6E9620 (0x6E9620 - 0x6E96DF) -- CEntityScriptHandleManagerCtor:
// the 80-byte object (&CEntityScriptHandleManager::`vftable') with
// +0x4C = -2 and +0x50 = the Lua module, wiring the Lua references.
// Remaining stub: store the placeholder vtable + zero the rest.
static void *CEntityScriptHandleManagerCtor(void *obj, void *luaModule)
{ (void)luaModule;
  *(void **)obj = g_CEntityScriptHandleManagerVtable;
  memset((char *)obj + 4, 0, 76);
  return obj; }

// nullsub_27 (0x6EFF60 - 0x6EFF61) -- one-byte `retn` no-op thunk.
static void nullsub_27(void) { }

// nullsub_28 / sub_70DDB0 -- the "EngineTest" module registration pair
// (module-private stub copies; gamemain.cpp keeps its own for the
// EngineTest registration outside this module).
static void  nullsub_28(int a) { (void)a; }
static void  sub_70DDB0(int a) { (void)a; }

int ReinitEngineLua(void *obj, int flag)
{
    DWORD *dw = (DWORD *)obj;

    // 1. Release the old CLuaManager (+0xC0); remember if it was empty.
    int wasEmpty = (dw[0xC0 / 4] == 0);
    if (dw[0xC0 / 4] != 0)
    {
        void *old0C0 = (void *)dw[0xC0 / 4];
        void **vt    = (void **)*(void **)old0C0;
        ((void (*)(void *, int))vt[0])(old0C0, 1);   // deleting dtor
        dw[0xC0 / 4] = 0;
    }

    // 2. Clear the net manager's +0x1C sub-object list.
    CNetLuaEventManagerListReset(CNetManagerSlot1C(GetNetManager()));

    // 3. Release the old Lua module (+0xBC); build the fresh one.
    if (dw[0xBC / 4] != 0)
    {
        DestroyLuaModule((int)dw[0xBC / 4]);
        dw[0xBC / 4] = 0;
    }
    dw[0xBC / 4] = (DWORD)CreateLuaModule();

    // 4. Build the new CLuaManager (8 bytes) over the fresh Lua module +
    //    the engine's +0x98 sorted map.  (The original re-reads and
    //    re-releases +0xC0 here, but it is always 0 -- cleared in step 1
    //    -- so that branch is dead.)
    int luaMgr = 0;
    void *newMgr = LHAlloc(8);
    if (newMgr != 0)
        luaMgr = (int)CLuaManagerCtor(newMgr, (void *)dw[0xBC / 4], (void *)dw[0x98 / 4]);
    dw[0xC0 / 4] = (DWORD)luaMgr;

    CLuaManagerRunSerialiseScript((void *)luaMgr);
    RegisterEngineLuaFunctions((int)dw[0xBC / 4]);

    // 5. First-time init: build the startup-script path and run the
    //    Lua startup scripts.
    if (wasEmpty)
    {
        int startup = 0;
        sub_1A02260(&startup, (int *)&g_unk_1DBE2B4,
                    (const char ***)(void *)&g_unk_1DBDA6C);
        RunLuaStartupScripts((int)dw[0xBC / 4], startup);
        ReleaseSharedSlot((F3SharedBuffer **)&startup);
    }

    // 6. Optional "EngineTest" module.
    if (flag != 0)
    {
        nullsub_28((int)dw[0xBC / 4]);
        sub_70DDB0((int)dw[0xBC / 4]);
    }

    // 7. (The original calls the nullsub_27 no-op when +0xC0 is set.)
    if (dw[0xC0 / 4] != 0)
        nullsub_27();

    // 8. Build the CEntityScriptHandleManager (80 bytes) into +0xC4.
    int  esm    = 0;
    void *esmMem = LHAlloc(0x50);
    if (esmMem != 0)
        esm = (int)CEntityScriptHandleManagerCtor(esmMem, (void *)dw[0xBC / 4]);
    if (dw[0xC4 / 4] != 0)
    {
        void *old0C4 = (void *)dw[0xC4 / 4];
        void **vt    = (void **)*(void **)old0C4;
        ((void (*)(void *, int))vt[0])(old0C4, 1);   // deleting dtor
    }
    dw[0xC4 / 4] = (DWORD)esm;
    return esm;
}
