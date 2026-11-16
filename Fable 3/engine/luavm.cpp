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
#include <stdio.h>                // _vsnprintf_s (the float / source formatting)
#include <stdarg.h>               // va_list (the vsnprintf wrapper)

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

// ============================================================
//  KorePanicTraceback  (0x5F1A20 - 0x5F1BA6)  and its full
//  reachable closure -- the "Kore Panic" traceback printer.
//
//  When the VM heap OOM-expands past its last-resort handler the
//  error path (sub_5F1C30) dispatches here to dump the current
//  VM state: the main / state names, the unprotected error string
//  (the top of the value array, reformatted through the float /
//  string interning), and a full function / local traceback (one
//  "Function ... Line ..." line per frame plus the local variable
//  of each frame).  The whole path is dead in the stub world (the
//  VM heap never actually grows or panics), but it is reimplemented
//  faithfully here: every reachable frame / source / string-intern
//  / value-format helper is implemented per IDA, and only the deep
//  local-dump table machinery (the 'L' / 'f' branches of the frame
//  fillers, which the "Snlu" flag string never reaches) is left as
//  documented stubs.
//  [NOT byte-identical] placement-dependent (relative calls to the
//  VM-printf at *a1+692, the frame fillers, the intern / alloc
//  helpers) and it references the high-VA string slots
//  unk_1ADDF7E / unk_1ADDF7F / unk_1ADDF82 (all empty strings).
// ============================================================

// The VM-printf dispatch slot (a1+692) takes (this, format, ...).
typedef int (*KoreVprintfFn)(int, const char *, ...);

// ROL / ROR helpers for the KoreVM string hash (the seed constant
// 0x6B6F7265 == "kore" little-endian).
static unsigned int KROL(unsigned int x, int s) { return (x << s) | (x >> (32 - s)); }
static unsigned int KROR(unsigned int x, int s) { return (x >> s) | (x << (32 - s)); }

// Forward declarations for the reachable closure.
static int    KoreFormatValue(int table, int valuePtr, int *outLen);   // sub_5EF090
static int    KoreResolveFrameIndex(int table, int frameNo, int *fr);  // sub_5D9240
static int    KoreResolveFrameInfo(int table, const char *flags, int *fr); // sub_5E1520
static int    KoreGetLocalName(int table, int *fr, int localIndex);    // sub_5D8D50
static int    KoreFillLuaFrame(int *fr, int table, int desc, const char *flags); // sub_5E1280
static int    KoreFillCFrame(int *fr, int table, int desc, const char *flags);   // sub_5D9090
static int    KoreSourceVarName(int table, int *fr);                    // sub_5D8DE0
static int    KoreBuildSourceDesc(int *fr);                             // sub_5D2430
static int    KoreVsnprintf(char *buf, unsigned int cnt, const char *fmt, ...); // sub_5D1390
static int    KoreStrncpy(char *dst, unsigned int size, const char *src);        // sub_5D1330
static int   *KoreInternString(int *table, int vmcore, const void *src, int size, unsigned int hash); // sub_5EEED0
static int    KoreRehashInterned(int *table, int vmcore);               // sub_5EEC00
static int   *KoreAllocVmMem(int vmcore, unsigned int size);            // sub_5DA410
static int    KoreFrameLocalBase(int *fr, int table);                   // sub_5D8D30
static int    KoreFrameLocalRange(int table, int frameIndex);           // sub_5D2860
static int    KoreFrameRowValid(int table, int frameIndex);             // sub_5D27D0
static int    KoreFrameRowEntry(int table, int frameIndex);             // sub_5D2810
static int    KoreLocalNameAt(int offset, int localIndex, int descriptor); // sub_5D23E0
static int    KoreFrameLocalStart(int table, int frameIndex);           // sub_5D2710
static int    KoreFrameLocalEnd(int table, int frameIndex);             // sub_5D2740
// 'L' / 'f' local-dump table machinery (the table-set cluster below is
// implemented; the rest remain documented stubs, not reached by the "Snlu"
// flag string in the panic path):
static int    KoreReallocValueStack(int table, int extra);              // sub_5DD730
static int    KoreAllocHashTable(int table, int a2, int a3);            // sub_5DDC90
static int    KoreTableSet(int table, int vmcore, int key, int value);   // sub_5E7400
static int    KoreTablePanic(int vmcore, const char *format, ...);      // sub_5E2580
static int    KoreTableEnsureRow(int table, int vmcore, int key, int value); // sub_5E5040
static int    KoreFindEmptyRow(int table);                              // sub_5D3640
static int    KoreRowRelocateHash(int mask, int key, int type);         // sub_5D3120
static int    KoreTableResize(int vmcore, int key, int value);          // sub_5E3F20
static int    KoreEnsureBackingTable(int table, int vmcore, int a3, int a4); // sub_610CA0
static void   KoreArraySet(int table, int index, int value);             // sub_5D3210
static void   KoreDebugMarkDirty(int flags, int key);                    // sub_5F7E10
static int    KoreDebugTrackAppend(int table, int *value);              // sub_5F7D80
// Existing VM-heap helpers defined later in this translation unit:
static int    KoreGrowRegistry(int obj, int size, int vmcore);          // sub_5EE150
static int    KoreVmAllocExpand(int vmcore, int size);                  // sub_5F6420
static int    KoreVmHeapAlloc(int vmcore, int size);                    // sub_5EEAE0

// ---- sub_5D1390: the vsnprintf wrapper (returns BufferCount on a
// negative truncation result, matching the original's contract). ----
static int KoreVsnprintf(char *buf, unsigned int cnt, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    int result = vsnprintf(buf, cnt, fmt, va);
    va_end(va);
    if (result < 0)
        return (int)cnt;
    return result;
}

// ---- sub_5D1330: the strncpy wrapper (null-terminates at size-1). ----
static int KoreStrncpy(char *dst, unsigned int size, const char *src)
{
    if (size != 0)
    {
        (void)strncpy(dst, src, size - 1);
        dst[size - 1] = 0;
    }
    return 0;
}

// ---- sub_5D83D0-style float hash: the "kore" 32-bit string hash used
// by every string-interning path.  Seed = (len + 0x6B6F7265); up to two
// 12-byte chunks are mixed, then the tail bytes are folded into the
// three accumulators, then a 6-step finalization.  (The effective hash
// only ever sees the first min(24, len) bytes of the content.) ----
static unsigned int KoreHashKore(const char *buf, int len)
{
    unsigned int seed = (unsigned int)len + 0x6B6F7265u;
    unsigned int acc1 = seed, acc2 = seed, acc3 = seed;
    int n = len;
    const unsigned int *p = (const unsigned int *)buf;
    if (n >= 0x1F)
        n = 31;
    if (n > 12)
    {
        // chunk 1
        unsigned int d0 = p[0], d1 = p[1];
        n -= 12;
        unsigned int v12 = acc1 + d0;
        unsigned int v13 = d1 + acc1;
        unsigned int v14 = acc3 + d0 - v13;
        unsigned int v16 = v12 + v13;
        unsigned int v17 = v14 ^ KROL(v13, 4);
        unsigned int v18 = v12 - v17;
        unsigned int v20 = v16 + v17;
        unsigned int v21 = v18 ^ KROL(v17, 6);
        unsigned int v22 = (v16 - v21) ^ KROL(v21, 8);
        unsigned int v23 = v20 + v21;
        unsigned int v24 = (v20 - v22) ^ KROL(v22, 16);
        unsigned int v25 = v23 + v22;
        unsigned int v26 = (v23 - v24) ^ KROR(v24, 13);
        acc3 = v25 + v24;
        acc1 = (v25 - v26) ^ KROL(v26, 4);
        acc2 = acc3 + v26;
        p += 3;
        if (n > 12)
        {
            // chunk 2
            unsigned int e0 = p[0], e1 = p[1], e2 = p[2];
            n -= 12;
            unsigned int v27 = e2 + acc1;
            unsigned int v28 = e1 + acc2;
            unsigned int v29 = (acc3 + e0 - v27) ^ KROL(v27, 4);
            unsigned int v30 = v28 + v27;
            unsigned int v31 = v28 - v29;
            unsigned int v33 = v30 + v29;
            unsigned int v34 = v31 ^ KROL(v29, 6);
            unsigned int v35 = (v30 - v34) ^ KROL(v34, 8);
            unsigned int v36 = v33 + v34;
            unsigned int v37 = (v33 - v35) ^ KROL(v35, 16);
            unsigned int v38 = v36 + v35;
            unsigned int v39 = KROR(v37, 13);
            unsigned int v40 = v36 - v37;
            acc3 = v38 + v37;
            acc1 = (v38 - (v40 ^ v39)) ^ KROL(v40 ^ v39, 4);
            acc2 = acc3 + (v40 ^ v39);
            p += 3;
        }
    }
    const unsigned char *q = (const unsigned char *)p;
    switch (n)
    {
        case 12: acc1 += *(const unsigned int *)(q + 8);
        case 11: acc1 += ((unsigned int)q[10]) << 8;
        case 10: acc1 += ((unsigned int)q[9]) << 16;
        case 9:  acc1 += ((unsigned int)q[8]) << 24;
        case 8:  acc2 += *(const unsigned int *)(q + 4);
                 acc3 += *(const unsigned int *)(q + 0);
                 break;
        case 7:  acc2 += ((unsigned int)q[6]) << 8;
        case 6:  acc2 += ((unsigned int)q[5]) << 16;
        case 5:  acc2 += ((unsigned int)q[4]) << 24;
        case 4:  acc3 += *(const unsigned int *)(q + 0);
                 break;
        case 3:  acc3 += ((unsigned int)q[2]) << 8;
        case 2:  acc3 += ((unsigned int)q[1]) << 16;
        case 1:  acc3 += ((unsigned int)q[0]) << 24;
                 break;
        default: break;   // case 0
    }
    unsigned int v41 = (acc2 ^ acc1) - KROL(acc2, 14);
    unsigned int v42 = (acc3 ^ v41) - KROL(v41, 11);
    unsigned int v43 = (v42 ^ acc2) - KROR(v42, 7);
    unsigned int v44 = (v43 ^ v41) - KROL(v43, 16);
    unsigned int v45 = (v42 ^ v44) - KROL(v44, 4);
    unsigned int v46 = (v45 ^ v43) - KROL(v45, 14);
    unsigned int result = (v46 ^ v44) - KROR(v46, 8);
    return result;
}

// ---- sub_5DA410: the VM-heap block allocation with retry.  `vmcore`
// holds the heap object at +0; it decrement the free-block counter
// (+0xA4), re-seeds the registry when the free-list is over the
// threshold, then bump-allocates (advance +0x04, call the +0x0C
// block-alloc fn with the +0x10 context) in a bounded 4-attempt retry,
// driving the last-resort expand (KoreVmAllocExpand) before each re-seed.
// Returns the block just past the 4-byte header, or 0 on exhaustion. ----
static int *KoreAllocVmMem(int vmcore, unsigned int size)
{
    int *heap = (int *)vmcore;
    int v2 = heap[0];
    if (size != 0)
    {
        --*(int *)(v2 + 164);
        if (*(int *)(*(int *)(v2 + 56) + 4) > *(int *)(v2 + 160)
            && *(int *)(v2 + 164) <= 0
            && *(int *)(v2 + 144) == 0)
        {
            int v4 = *(int *)(v2 + 140);
            *(int *)(v2 + 164) = 317;
            int v5 = (v4 == -1) ? 0 : 317 * v4;
            KoreGrowRegistry((int)(v2 + 44), (size_t)heap, v5);
        }
        int v6 = 3;
        int v7 = (int)size + 4;
        while (1)
        {
            int v8 = *(int *)(v2 + 4);
            *(int *)(v2 + 4) = v8 + v7;
            --v6;
            int v9 = (*(int (**)(int, int, int, unsigned int))(v2 + 12))(
                         *(int *)(v2 + 16), 0, 0, (unsigned int)v7);
            if (v9 != 0)
            {
                *(int *)v9 = 0;
                *(int *)v9 = *(int *)v2;
                int v10 = *(int *)(v2 + 4);
                if (*(int *)(v2 + 8) > v10)
                    v10 = *(int *)(v2 + 8);
                *(int *)(v2 + 8) = v10;
                if (v9 + 4 != 0)
                    return (int *)(v9 + 4);
            }
            else
            {
                *(int *)(v2 + 4) = v8;   // roll back the bump
            }
            if (v6 == 0)
            {
                KoreVmAllocExpand(vmcore, (int)size);
                v6 = 3;
            }
            KoreGrowRegistry((int)(v2 + 44), (size_t)heap, -1);
        }
    }
    return 0;
}

// ---- sub_5EEC00: rehash the interned-string hash table (double the
// capacity, reinsert every existing entry).  `table` is the intern
// object (+0 array base, +4 count, +8 mask); `vmcore` is the VM core. ----
static int KoreRehashInterned(int *table, int vmcore)
{
    unsigned int newCount = (unsigned int)table[2] + 1;
    int oldArray = table[0];
    void *v4 = (void *)KoreVmHeapAlloc(vmcore, 8 * (int)newCount);
    table[0] = (int)v4;
    memset(v4, 0, 8 * newCount);
    if (*(int *)(*(int *)vmcore + 60) == 1)
        *(int *)((int)v4 - 8) |= 1;
    table[2] = (int)newCount * 2 - 1;
    int i = 0;
    for (i = 0; i < *(int *)(*(int *)vmcore + 172); ++i)
    {
        int e = *(int *)(oldArray + 4 * i);
        if (e != 0 && *(int *)e >= 0)
            *(int *)(e - 4) |= 1;
    }
    i = 0;
    *(int *)(*(int *)vmcore + 172) = 0;
    while (i < (int)newCount)
    {
        int slot = *(int *)(oldArray + 4 * i);
        if (slot != 0)
        {
            int mask = table[2];
            int h = *(int *)(slot + 4);
            if (*(int *)(table[0] + 4 * (h & mask)) != 0)
            {
                do
                    ++h;
                while (*(int *)(table[0] + 4 * (h & mask)) != 0);
            }
            *(int *)(table[0] + 4 * (h & mask)) = slot;
        }
        ++i;
    }
    *(int *)(oldArray - 8) &= ~1;
    return 0;
}

// ---- sub_5EEED0: intern a string into the interned-string table
// (find-or-insert by (hash, length, content)).  On a content match it
// sets the debug dirty flag (bit 0 of entry-1) when debug level is 2-4
// and returns the existing entry; on a miss it grows the table when the
// load factor exceeds half, allocates a {length, hash, bytes, NUL}
// record via KoreAllocVmMem, and linearly probes for a free slot. ----
static int *KoreInternString(int *table, int vmcore, const void *src, int size, unsigned int hash)
{
    int probe = (int)hash;
    for (int i = (int)(hash & (unsigned int)table[2]); ; ++probe, i = table[2] & probe)
    {
        int *entry = *(int **)(table[0] + 4 * i);
        if (entry == 0)
            break;
        if (entry[1] == (int)hash && (*entry & 0x3FFFFFFF) == size)
        {
            if (memcmp(entry + 2, src, (size_t)size) == 0)
            {
                int dbg = *(int *)(*(int *)vmcore + 60);
                if (dbg > 1 && dbg <= 4 && *entry >= 0)
                    *(entry - 1) |= 1;
                return entry;
            }
        }
    }
    if ((unsigned int)(table[1] + 1) > (unsigned int)(table[2] + 1) >> 1)
        KoreRehashInterned(table, vmcore);
    int *v20 = KoreAllocVmMem(vmcore, (unsigned int)(size + 11));
    *v20 = size;
    v20[1] = (int)hash;
    memcpy(v20 + 2, src, (size_t)size);
    *((char *)v20 + size + 8) = 0;
    ++table[1];
    int j = (int)hash;
    while (*(int *)(table[0] + 4 * (j & table[2])) != 0)
        ++j;
    *(int *)(table[0] + 4 * (j & table[2])) = (int)v20;
    return v20;
}

// ---- sub_5EF090: reformat a stack value as an interned string.  For a
// float (type 3) it formats with "%0.14g", hashes, and interns; for a
// pointer (type 4) it returns the pointee; otherwise null.  On a float /
// pointer hit it rewrites the value in place to {entry, 4} and, when
// `outLen` is non-null, writes entry[0] & 0x3FFFFFFF (the length). ----
static int KoreFormatValue(int table, int valuePtr, int *outLen)
{
    int *a2 = (int *)valuePtr;
    if (a2[1] == 3)
    {
        char Buffer[0x100];
        float f = *(float *)a2;
        int len = KoreVsnprintf(Buffer, 0x100u, "%0.14g", f);
        unsigned int hash = KoreHashKore(Buffer, len);
        // The intern table object lives at vmstate+0xBC (table[0]+0xBC);
        // the vmcore passed through is `table` itself.
        int *v5 = KoreInternString((int *)(*(int *)table + 0xBC), table, Buffer, len, hash);
        if (outLen != 0)
            *outLen = *(int *)v5 & 0x3FFFFFFF;
        a2[1] = 4;
        a2[0] = (int)v5;
        return (int)(v5 + 2);
    }
    if (a2[1] == 4)
    {
        if (outLen != 0)
            *outLen = *(int *)a2[0] & 0x3FFFFFFF;
        return (int)((int *)a2[0] + 2);
    }
    if (outLen != 0)
        *outLen = 0;
    return 0;
}

// ---- Frame-row helpers: the 16-byte circular frame list that starts
// at table[1] and ends at table[3] (frame count = (table[3]-table[1])>>4).
// Each row's leading 8 bytes (row-8 descriptor, row-4 type word) precede
// the row; type 9 marks a Lua frame, 10 a C frame. ----

// sub_5D27D0: is the frame row at `frameIndex` a valid Lua (type 9) row?
static int KoreFrameRowValid(int table, int frameIndex)
{
    int frameCount = (*(int *)(table + 12) - *(int *)(table + 4)) >> 4;
    int v3;
    if (frameIndex == frameCount)
        v3 = *(int *)(table + 32);
    else
        v3 = *(int *)(*(int *)(table + 4) + 16 * (frameIndex + 1));
    return *(int *)(v3 - 4) == 9;
}

// sub_5D2810: the 8-byte value descriptor pointer for the frame row.
static int KoreFrameRowEntry(int table, int frameIndex)
{
    int frameCount = (*(int *)(table + 12) - *(int *)(table + 4)) >> 4;
    if (frameIndex == frameCount)
        return *(int *)(*(int *)(table + 32) - 8);
    int p = *(int *)(*(int *)(table + 4) + 16 * (frameIndex + 1));
    return *(int *)(p - 8);
}

// sub_5D2710: the start of the frame's local range.
static int KoreFrameLocalStart(int table, int frameIndex)
{
    int frameCount = (*(int *)(table + 12) - *(int *)(table + 4)) >> 4;
    if (frameIndex == frameCount)
        return *(int *)(table + 32);
    return *(int *)(*(int *)(table + 4) + 16 * (frameIndex + 1));
}

// sub_5D2740: the end of the frame's local range.
static int KoreFrameLocalEnd(int table, int frameIndex)
{
    int frameCount = (*(int *)(table + 12) - *(int *)(table + 4)) >> 4;
    int v4;
    if (frameIndex == frameCount)
        v4 = *(int *)(table + 28);
    else if (frameIndex + 1 == frameCount)
        v4 = *(int *)(table + 32);
    else
        v4 = *(int *)(*(int *)(table + 4) + 16 * (frameIndex + 2));
    int localCount = *(short *)(*(int *)(table + 4) + 16 * frameIndex + 0xA);
    return v4 - (8 * localCount + 8);
}

// sub_5D2860: resolve the start of the frame's local range (validating
// the row is a Lua frame and adjusting for the descriptor base).
static int KoreFrameLocalRange(int table, int frameIndex)
{
    int frameCount = (*(int *)(table + 12) - *(int *)(table + 4)) >> 4;
    int v3;
    if (frameIndex == frameCount)
        v3 = *(int *)(table + 32);
    else
        v3 = *(int *)(*(int *)(table + 4) + 16 * (frameIndex + 1));
    if (*(int *)(v3 - 4) != 9)
        return 0;
    if (frameIndex == *(int *)(table + 24))       // this[5] = table[6]
        return *(int *)(table + 20);              // this[4] = table[5]
    if (frameIndex == frameCount)
        return *(int *)(table + 16);              // this[3] = table[4]
    int v5 = *(int *)(*(int *)(table + 4) + 16 * frameIndex + 0x14);
    if (v5 == 0)
        return 0;
    int v6 = *(int *)KoreFrameRowEntry(table, frameIndex);
    if (v5 != *(int *)(v6 + 8))
        return v5 - 4;
    return v5;
}

// sub_5D23E0: resolve the name of the Nth local whose [low,high] range
// contains `offset`, from the descriptor's 12-byte local-name entries.
static int KoreLocalNameAt(int offset, int localIndex, int descriptor)
{
    if (offset == 0)
        return 0;
    int v7 = (offset - *(int *)(descriptor + 8)) >> 2;
    int count = *(int *)(descriptor + 60);
    int base = *(int *)(descriptor + 64);
    int v8 = 0;
    if (count > 0 && localIndex > 0)
    {
        int *v9 = (int *)(base + 8);
        while (localIndex > 0)
        {
            if (*(v9 - 1) <= v7 && *v9 >= v7)
                --localIndex;
            if (localIndex == 0)
                return *(int *)(base + 12 * v8) + 8;
            ++v8;
            v9 += 3;
            if (v8 >= count)
                return 0;
        }
    }
    return 0;
}

// sub_5D8D30: the base of the frame's local range (0 for tail-call
// frames, i.e. frame[141] != 0; otherwise the resolved local range).
static int KoreFrameLocalBase(int *fr, int table)
{
    if (*(int *)(fr + 564) != 0)
        return 0;
    return KoreFrameLocalRange(table, *(int *)(fr + 560));
}

// ---- sub_5D2430: format the source description into frame+0x2C
// (512 bytes) from the frame's +0x10 source string: '@' is a file path
// (copy path-1, "..."-truncated to the last 504 bytes when long), '='
// is a chunk name (copy up to 512 bytes), anything else is shown as
// [string "first line..."] (first line truncated at 494 bytes); a null
// source writes [string "?"]. ----
static int KoreBuildSourceDesc(int *fr)
{
    char *v1 = *(char **)(fr + 16);
    if (v1 != 0)
    {
        if (*v1 == 64)   // '@' -> file path
        {
            unsigned int v3 = (unsigned int)strlen(v1);
            if (v3 <= 0x1F9)
            {
                (void)KoreStrncpy((char *)(fr + 44), 0x200u, v1 + 1);
                *(char *)(fr + 555) = 0;
            }
            else
            {
                (void)KoreStrncpy((char *)(fr + 44), 4u, "...");
                *(char *)(fr + 47) = 0;
                (void)KoreStrncpy((char *)(fr + 47), 0x1F9u, &v1[v3 - 504]);
                *(char *)(fr + 551) = 0;
            }
        }
        else if (*v1 == 61)   // '=' -> chunk name
        {
            unsigned int v4 = (unsigned int)strlen(v1);
            if (v4 > 0x200)
                v4 = 512;
            (void)KoreStrncpy((char *)(fr + 44), v4, v1 + 1);
            *(char *)(fr + 44 + v4 - 1) = 0;
        }
        else   // -> [string "first line..."]
        {
            (void)KoreStrncpy((char *)(fr + 44), 0xAu, "[string \"");
            *(char *)(fr + 53) = 0;
            unsigned int v5 = (unsigned int)strcspn(v1, "\n\r");
            if (v5 <= 0x1EF)
            {
                unsigned int v6 = (unsigned int)strlen(v1);
                KoreStrncpy((char *)(fr + 53), v5 + 1, v1);
                if (v5 < v6)
                {
                    KoreStrncpy((char *)(v5 + fr + 53), 4u, "...");
                    v5 += 3;
                }
            }
            else
            {
                v5 = 498;
                KoreStrncpy((char *)(fr + 53), 0x1F3u, v1);
                (void)KoreStrncpy((char *)(fr + 548), 4u, "...");
                *(char *)(fr + 551) = 0;
            }
            char *v7 = (char *)(v5 + fr + 53);
            (void)KoreStrncpy(v7, 3u, "\"]");
            v7[2] = 0;
        }
    }
    else
    {
        (void)KoreStrncpy((char *)(fr + 44), 0x200u, "[string \"?\"]");
        *(char *)(fr + 555) = 0;
    }
    return 0;
}

// ---- sub_5D8DE0: resolve the source variable name for a frame, writing
// the class label ("field" / "global" / "local" / "method" / "UpValue")
// to frame+8 and the resolved name pointer to frame+4.  It reads the
// frame's code-object descriptor and walks its 12-byte operand range
// entries to match the current instruction operand's type tag. ----
static int KoreSourceVarName(int table, int *fr)
{
    int v3 = fr[140];
    fr[2] = (int)"";      // class label default = ""
    fr[1] = 0;            // function name default = 0
    if (v3 >= 1 && fr[141] == 0)
    {
        int v4 = v3 - 1;
        int v2b = *(int *)(table + 4);
        int v6 = KoreFrameLocalRange(table, v3 - 1);
        if (*(short *)(16 * v3 + v2b + 8) == 0)
        {
            int valid = KoreFrameRowValid(table, v4);
            if (valid != 0 && v6 != 0)
            {
                int v7 = KoreFrameRowEntry(table, v4);
                int v8 = *(int *)v7;
                int v9 = *(unsigned char *)v6;
                int v10 = *(int *)(v7 + 16);
                int v2c = *(int *)v6 >> 25;
                if (v2c == 2 || v2c == 3 || v2c == 22 || v2c == 23 || v2c == 24
                    || v2c == 29 || v2c == 30 || v2c == 31 || v2c == 62 || v2c == 63)
                {
                    int v11 = *(int *)(v8 + 8);
                    int *v12 = (int *)(v6 - 4);
                    if ((unsigned int)v12 >= (unsigned int)v11)
                    {
                        int result = v11 + 4 * *(int *)(v8 + 4);
                        do
                        {
                            if ((unsigned int)v12 >= (unsigned int)(v11 + 4 * *(int *)(v8 + 4)))
                                return result;
                            int d = *v12;
                            int tag = *(unsigned char *)v12;
                            --v12;
                            if (tag == v9)
                            {
                                switch (d >> 25)
                                {
                                    case 0:
                                    case 0x43:
                                        result = v10 + 8 * ((d >> 8) & 0xFF);
                                        fr[2] = (int)"field";
                                        if (*(int *)(result + 4) == 4)
                                            fr[1] = *(int *)result + 8;
                                        else
                                            fr[1] = (int)"?";
                                        return result;
                                    case 2:
                                    case 3:
                                    case 0x1D:
                                    case 0x1E:
                                    case 0x3F:
                                        return d;
                                    case 6:
                                        {
                                            int idx = (d >> 8) & 0x1FFFF;
                                            result = v10 + 8 * idx;
                                            if (*(int *)(result + 4) != 4)
                                                return result;
                                            fr[2] = (int)"global";
                                            fr[1] = *(int *)result + 8;
                                            return result;
                                        }
                                    case 7:
                                        {
                                            int idx = (d >> 17) & 0xFF;
                                            if (idx < *(unsigned char *)(v8 + 3))
                                            {
                                                fr[2] = (int)"local";
                                                int name = KoreLocalNameAt((int)(v12 + 8), idx + 1, v8);
                                                fr[1] = name;
                                                return name;
                                            }
                                            v9 = idx;
                                            break;
                                        }
                                    case 8:
                                        {
                                            result = (d >> 8) & 0x1FF;
                                            fr[2] = (int)"method";
                                            if (result >= 0x100)
                                                fr[1] = *(int *)(v10 + 8 * (result & 0xFF)) + 8;
                                            return result;
                                        }
                                    case 0xA:
                                    case 0xC:
                                        {
                                            result = (d >> 8) & 0x1FF;
                                            fr[2] = (int)"field";
                                            if (result < 0x100)
                                                return result;
                                            result = v10 + 8 * (result & 0xFF);
                                            if (*(int *)(result + 4) == 4)
                                                fr[1] = *(int *)result + 8;
                                            else
                                                fr[1] = (int)"?";
                                            return result;
                                        }
                                    case 0x20:
                                        {
                                            result = (d >> 17) & 0xFF;
                                            fr[2] = (int)"UpValue";
                                            if ((signed int)result < *(int *)(v8 + 44))
                                                fr[1] = *(int *)(*(int *)(v8 + 48) + 4 * result) + 8;
                                            return result;
                                        }
                                    default:
                                        break;
                                }
                            }
                        }
                        while ((unsigned int)v12 >= (unsigned int)v11);
                    }
                }
            }
        }
    }
    return 0;
}

// ---- 'L' / 'f' local-dump table machinery: documented stubs.  These
// are only reached through the uppercase 'L' (local dump) and 'f'
// (push the function value) flag characters, which the panic path's
// "Snlu" flag string never contains, so they are never executed in the
// traceback.  They remain no-op stubs consistent with the other deep
// KoreVM table internals already stubbed in this file.
static int KoreReallocValueStack(int table, int extra) { (void)table; (void)extra; return 0; }
static int KoreAllocHashTable(int table, int a2, int a3) { (void)table; (void)a2; (void)a3; return 0; }
static int KoreEnsureBackingTable(int table, int vmcore, int a3, int a4)
{ (void)table; (void)vmcore; (void)a3; (void)a4; return 0; }
static void KoreArraySet(int table, int index, int value)
{ (void)table; (void)index; (void)value; }
static int KoreDebugTrackAppend(int table, int *value) { (void)table; (void)value; return 0; }
// KoreTableSet (sub_5E7400) and KoreDebugMarkDirty (sub_5F7E10), along with
// their insert-path closure (KoreTablePanic / KoreTableEnsureRow /
// KoreFindEmptyRow / KoreRowRelocateHash / KoreTableResize), are implemented
// further down in this file, after KoreFindGlobalRow (their probe helper).

// ---- sub_5E1280: fill a Lua frame record from the 8-byte value
// descriptor, walking the flag characters.  'S' sets the type /
// description / line-range words, 'n' resolves the function name, 'l'
// resolves the line, 'u' copies the two value words; 'f' pushes the
// descriptor as a type-9 value and 'L' performs the full local dump
// (the deep table machinery, stubbed above). ----
static int KoreFillLuaFrame(int *fr, int table, int desc, const char *flags)
{
    int v5 = desc;                 // the descriptor base (*a3)
    char c = *flags;
    int fL = 0;                    // 'L' seen
    int ff = 0;                    // 'f' seen
    int ebp = 0;                   // the 'l'-resolved line / locals pointer
    if (c != 0)
    {
        do
        {
            switch (c)
            {
                case 'L':
                    fL = 1;
                    break;
                case 'S':
                    fr[3] = (int)"Lua";
                    fr[4] = *(int *)(v5 + 52) + 8;
                    KoreBuildSourceDesc(fr);
                    fr[9] = *(int *)(v5 + 28);
                    fr[10] = *(int *)(v5 + 32);
                    break;
                case 'f':
                    ff = 1;
                    break;
                case 'l':
                {
                    fr[5] = -1;
                    if (v5 != 0)
                    {
                        int v8 = *(int *)(v5 + 40);
                        ebp = v8;
                        if (v8 != 0 && fr[140] != -1)
                        {
                            int v9 = KoreFrameLocalBase(fr, table);
                            if (v9 != 0)
                                fr[5] = ((int *)v8)[((v9 - *(int *)(v5 + 8)) >> 2)];
                            else
                                fr[5] = *(int *)v8;
                        }
                    }
                    break;
                }
                case 'n':
                {
                    KoreSourceVarName(table, fr);
                    int v7 = *(int *)(v5 + 56);
                    if (v7 != 0)
                        fr[1] = v7 + 8;
                    if (fr[1] == 0)
                        fr[1] = (int)"(anonymous)";
                    break;
                }
                case 'u':
                    fr[6] = *(unsigned char *)v5;
                    fr[7] = *(unsigned char *)(v5 + 1);
                    fr[8] = ebp;
                    break;
                default:
                    break;
            }
            c = *++flags;
        } while (*flags != 0);
        if (ff != 0)
        {
            int top = *(int *)(table + 28);
            *(int *)top = desc;
            *(int *)(top + 4) = 9;
            *(int *)(table + 28) = top + 8;
        }
        if (fL != 0)
        {
            if ((unsigned int)(*(int *)(table + 28) + 24) > (unsigned int)*(int *)(table + 36))
                KoreReallocValueStack(table, 3);
            int v11 = KoreAllocHashTable(table, 1, 1);
            *(int *)(*(int *)(table + 28) + 4) = 5;
            *(int *)(*(int *)(table + 28)) = v11;
            *(int *)(table + 28) += 8;
            if (v5 != 0)
            {
                int v12 = 0;
                while (v12 < *(int *)(v5 + 36))
                {
                    int v13 = *(int *)(table + 28);
                    *(int *)v13 = 1;
                    *(int *)(v13 + 4) = 1;
                    v13 += 8;
                    *(int *)(table + 28) = v13;
                    int v14 = *(int *)(*(int *)(v5 + 40) + 4 * v12);
                    int is5 = *(int *)(v13 - 12) == 5;
                    int v16 = v13 - 8;
                    int v17 = *(int *)(v13 - 16);
                    int v25 = v14;
                    if (is5)
                    {
                        int v18 = v14 - 1;
                        int v23 = v17;
                        if ((unsigned int)v18 >= (unsigned int)*(int *)(v17 + 16))
                        {
                            int v22[2];
                            v22[0] = (int)(float)v25;
                            v22[1] = 3;
                            KoreTableSet(v17, table, v22[0], v16);
                            v17 = v23;
                        }
                        else
                        {
                            int v19 = *(int *)v16;
                            int v24 = ((int *)v16)[1];
                            int v20 = *(int *)(v17 + 12);
                            *(int *)(v20 + 8 * v18) = v19;
                            *(int *)(v20 + 8 * v18 + 4) = v24;
                        }
                        if (*(int *)(*(int *)table + 60) == 1)
                            KoreDebugMarkDirty(v17 - 8, v16);
                    }
                    else
                    {
                        KoreEnsureBackingTable(table, 0, 0, v14);
                        KoreArraySet(table, v14, v16);
                    }
                    *(int *)(table + 28) -= 8;
                    ++v12;
                }
            }
        }
    }
    return 1;
}

// ---- sub_5D9090: fill a C frame record from the 8-byte value
// descriptor, walking the flag characters.  'S' sets the "C" type /
// "=[C]" description / "[C]" buffer, 'n' resolves the name, 'l' clears
// the line, 'u' copies the two 16-bit value words; 'f' pushes the
// descriptor as a type-10 value and 'L' pushes an 8-byte zero record. ----
static int KoreFillCFrame(int *fr, int table, int desc, const char *flags)
{
    char c = *flags;
    int fL = 0;                    // 'L' seen
    int ff = 0;                    // 'f' seen
    if (c != 0)
    {
        do
        {
            switch (c)
            {
                case 'L':
                    fL = 1;
                    break;
                case 'S':
                    *(int *)(fr + 16) = (int)"=[C]";
                    *(int *)(fr + 12) = (int)"C";
                    *(int *)(fr + 36) = -1;
                    *(int *)(fr + 40) = -1;
                    (void)KoreStrncpy((char *)(fr + 44), 0x200u, "[C]");
                    *(char *)(fr + 555) = 0;
                    break;
                case 'f':
                    ff = 1;
                    break;
                case 'l':
                    *(int *)(fr + 20) = -1;
                    break;
                case 'n':
                    KoreSourceVarName(table, fr);
                    break;
                case 'u':
                    *(int *)(fr + 28) = 0;
                    *(int *)(fr + 24) = *(short *)(desc + 8);
                    *(int *)(fr + 32) = *(short *)(desc + 10);
                    break;
                default:
                    break;
            }
            c = *++flags;
        } while (*flags != 0);
        if (ff != 0)
        {
            int *v7 = *(int **)(table + 28);
            *v7 = desc;
            v7[1] = 10;
            *(int *)(table + 28) = (int)(v7 + 2);
        }
        if (fL != 0)
        {
            int v8 = *(int *)(table + 28);
            *(int *)(v8 + 4) = 0;
            *(int *)(table + 28) = v8 + 8;
        }
    }
    return 1;
}

// ---- sub_5D9240: resolve the frame that contains the Nth value in the
// traceback (walking the circular 16-byte frame list backward,
// subtracting each frame's local-count+1) and initialize the 568-byte
// frame record (defaults, then the frame index at +0x230 and the
// partial-remaining flag at +0x234).  Returns 1 on success, 0 when the
// index is negative or out of range. ----
static int KoreResolveFrameIndex(int table, int frameNo, int *fr)
{
    int v3 = frameNo;
    *(int *)(fr + 4) = 0;
    *(int *)(fr + 8) = (int)"";          // unk_1ADDF82 (empty)
    *(int *)(fr + 12) = 0;
    *(int *)(fr + 16) = 0;
    *(int *)(fr + 20) = -1;
    *(int *)(fr + 24) = 0;
    *(int *)(fr + 28) = 0;
    *(int *)(fr + 36) = -1;
    *(int *)(fr + 40) = -1;
    *(char *)(fr + 44) = 0;
    *(int *)(fr + 560) = -1;
    *(int *)(fr + 564) = -1;
    if (frameNo < 0)
        return 0;
    int v5 = (*(int *)(table + 12) - *(int *)(table + 4)) >> 4;
    if (v5 < 0)
        return 0;
    short *i = (short *)(16 * v5 + *(int *)(table + 4) + 8);
    for (; ; i -= 8)
    {
        int v7 = *i + 1;
        if (v3 < v7)
            break;
        --v5;
        v3 -= v7;
        if (v5 < 0)
            return 0;
    }
    *(int *)(fr + 560) = v5;
    *(int *)(fr + 564) = v3 > 0;
    return 1;
}

// ---- sub_5E1520: resolve the function name / type / line for a frame,
// filling the 568-byte record.  Three paths: (a) a '>'-prefixed flag
// pops one 8-byte value from the top of the value array and resets the
// frame state; (b) a tail-call frame (frame[141] != 0) writes the
// "tail" / "=(tail call)" labels and consumes the 'f' / 'L' value slots;
// (c) otherwise it dispatches to the Lua (type 9) or C (type 10) frame
// filler based on the row's type word. ----
static int KoreResolveFrameInfo(int table, const char *flags, int *fr)
{
    char *v3 = (char *)flags;
    int v7, v8;
    if (*flags == 62)   // '>'
    {
        int top = *(int *)(table + 28);
        if ((int)((top - *(int *)(table + 32)) & 0xFFFFFFF8) < 8)
            return 0;
        v7 = *(int *)(top - 8);
        v8 = *(int *)(top - 4);
        v3 = (char *)flags + 1;
        *(int *)(table + 28) = top - 8;
        *(int *)(fr + 564) = 0;
        *(int *)(fr + 560) = -1;
    }
    else
    {
        if (*(int *)(fr + 564) != 0)   // tail-call frame
        {
            *(int *)(fr + 12) = (int)"tail";
            *(int *)(fr + 16) = (int)"=(tail call)";
            (void)KoreStrncpy((char *)(fr + 44), 0x200u, "(tail call)");
            *(char *)(fr + 555) = 0;
            *(int *)(fr + 8) = (int)"";       // unk_1ADDF7F (empty)
            *(int *)(fr + 4) = (int)"";       // unk_1ADDF7F (empty)
            int v11 = 0, v10 = 0;
            char *p = (char *)flags;
            while (*p != 0)
            {
                if (*p == 'f')
                    v11 = 1;
                else if (*p == 'L')
                    v10 = 1;
                ++p;
            }
            if (v11 != 0)
            {
                int t = *(int *)(table + 28);
                *(int *)(t + 4) = 0;
                *(int *)(table + 28) = t + 8;
            }
            if (v10 != 0)
            {
                int t = *(int *)(table + 28);
                *(int *)(t + 4) = 0;
                *(int *)(table + 28) = t + 8;
            }
            return 1;
        }
        int v15 = *(int *)(fr + 560);
        int v16;
        if (v15 == (*(int *)(table + 12) - *(int *)(table + 4)) >> 4)
            v16 = *(int *)(table + 32);
        else
            v16 = *(int *)(16 * (v15 + 1) + *(int *)(table + 4));
        v7 = *(int *)(v16 - 8);
        v8 = *(int *)(v16 - 4);
    }
    int v17 = v8 - 9;
    if (v17 == 0)
        return KoreFillLuaFrame(fr, table, v7, v3);
    if (v17 != 1)
        return 0;
    return KoreFillCFrame(fr, table, v7, v3);
}

// ---- sub_5D8D50: the local-variable name for the Nth local of the
// frame.  Returns the resolved name, "(*temporary)" for an unnamed local
// within the frame's local range, or 0 when out of range / tail-call. ----
static int KoreGetLocalName(int table, int *fr, int localIndex)
{
    if (*(int *)(fr + 564) != 0)
        return 0;
    int v3 = *(int *)(fr + 560);
    int frameCount = (*(int *)(table + 12) - *(int *)(table + 4)) >> 4;
    if (v3 < 0 || v3 > frameCount || localIndex <= 0)
        return 0;
    int result = 0;
    if (KoreFrameRowValid(table, v3) != 0)
    {
        int entry = KoreFrameRowEntry(table, v3);
        int offset = KoreFrameLocalBase(fr, table);   // sub_5D8D30 return
        result = KoreLocalNameAt(offset, localIndex, *(int *)entry);
    }
    if (result == 0)
    {
        int v6 = KoreFrameLocalStart(table, v3) + 8 * localIndex - 8;
        return v6 < KoreFrameLocalEnd(table, v3) ? (int)"(*temporary)" : 0;
    }
    return result;
}

// ---- sub_5F1A20: the "Kore Panic" traceback printer (the top of the
// error path).  Prints the main / state names, the unprotected error
// string (the top value, reformatted), and a full function / local
// traceback; returns -1.  [NOT byte-identical] placement-dependent (the
// relative calls to the VM-printf at *a1+692, the frame fillers, the
// intern / alloc helpers) and it references the high-VA string slots
// unk_1ADDF7E / unk_1ADDF7F / unk_1ADDF82.
static int KorePanicTraceback(int table)
{
    int *T = (int *)table;
    int top = T[7] - 8;
    int errstr = 0;
    if (top >= T[8])
        errstr = KoreFormatValue(table, top, 0);
    else
        errstr = 0;
    KoreVprintfFn vprintf = *(KoreVprintfFn *)(T[0] + 692);
    int mainstate = *(int *)(*(int *)(T[0] + 228) + 72) + 8;
    vprintf(table, "Kore Panic - Main State: %s - State name: %s\n\n",
            (const char *)mainstate, (const char *)(T[18] + 8));
    vprintf(table, "Unprotected error (%s)\n\n", (const char *)errstr);
    vprintf(table, "Full Traceback:\n\n");
    int frameInfo[142];   // the 568-byte frame record
    for (int i = 1; KoreResolveFrameIndex(table, i, frameInfo) != 0; ++i)
    {
        int name = frameInfo[1];
        if (name == 0)
            name = (int)"?";
        KoreResolveFrameInfo(table, "Snlu", frameInfo);
        vprintf(table, "Function %s() %s Line %d\n", (const char *)name,
                (const char *)(frameInfo + 0x2C), frameInfo[5]);
        for (int j = 1; ; ++j)
        {
            int localName = KoreGetLocalName(table, frameInfo, j);
            if (localName == 0)
                break;
            int v16 = frameInfo[140];
            int v6;
            if (v16 == (*(int *)(table + 12) - *(int *)(table + 4)) >> 4)
                v6 = *(int *)(table + 32);
            else
                v6 = *(int *)(16 * (v16 + 1) + *(int *)(table + 4));
            int *v7 = (int *)T[7];
            *v7 = *(int *)(v6 + 8 * j - 8);
            v7[1] = *(int *)(v6 + 8 * j - 4);
            v7 += 2;
            T[7] = (int)v7;
            int v8 = (int)v7 - 8;
            int v9 = 0;
            if ((unsigned int)v8 >= (unsigned int)T[8])
                v9 = KoreFormatValue(table, v8, 0);
            else
                v9 = 0;
            vprintf(table, "    Local %2d: %s = %s\n", j,
                    (const char *)localName, (const char *)v9);
            T[7] -= 8;
        }
    }
    return -1;
}

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
// of the heap's secondary alloc-node table chain (the obj+0x3C linked list of
// nodes, each node's next pointer at +0x50) through KoreInitAllocNode, first
// zeroing each node's retired payload blocks. For each node i in the chain the
// current node is published at obj+0x44, and (only when the node's
// "retired" bit is set, the low bit at i-8) the node's payload block base
// (i+0x1C, non-null) is memset to zero for 8*((i+0x24 - base + 40) >> 3) - 8
// bytes, then every 8-byte {payload, type} node pair in [i+0x28 .. i+0x1C)
// is re-adopted through KoreInitAllocNode with flags - 1. On exit obj+0x44 is
// reset to null. [NOT byte-identical] a thiscall + retn 4 hybrid in the
// original (the heap object in ECX, the flags word on the stack; declared
// object-first here) and placement-dependent (relative call into
// KoreInitAllocNode plus an IAT call to memset).
static void KoreInitAllocTable(int obj, int flags)
{
    int *h = (int *)obj;
    int i = h[15];                     // obj+0x3C secondary alloc-node chain
    for (; i != 0; i = ((int *)i)[20]) // next node at i+0x50
    {
        int *n = (int *)i;
        h[17] = i;                     // obj+0x44 current node
        if ((*(unsigned char *)(i - 8) & 1) != 0)  // retired bit at i-8
        {
            int base = n[7];           // i+0x1C payload block base
            if (base != 0)
            {
                int span = ((n[9] - base + 40) >> 3);   // (end - base + 40) / 8
                memset((void *)base, 0, 8 * span - 8);  // zero the retired block
            }
            for (int j = n[10]; (unsigned int)j < (unsigned int)n[7]; j += 8)
                KoreInitAllocNode(obj, j, flags - 1);   // i+0x28..i+0x1C node pairs
        }
    }
    h[17] = 0;                         // clear the current-node slot
}

// sub_5DC610 (0x5DC610 - 0x5DC721) -- KoreForEachAllocNode: walk every
// alloc node registered in the VM's alloc-node registry (the heap+0x50
// sub-object) and invoke the callback (a1 = the registry's +0x0C context
// pointer, a2 = the node) on each. When the registry's +0x7E58 single node
// is non-null it is the only visit and the callback's result is returned.
// Otherwise, when the +0x7E40 record count is non-zero, the row chain is
// walked: each row (the +0x7E38 head, linked through +0x7B04) owns a block
// of three 0x1EC0-byte table records at +0x7B00; each record's +0x04 node
// is visited, and when that node is null or child-less (node+0x18 == 0) and
// the record's +0xB20 flag is set, the record's inline node array is
// visited too: the 12-byte chain elements at +0xB18 (each owning a
// 28-byte run of 4-byte nodes at +0x20, linked through +0x24) are walked
// up to the record's +0xB24 end pointer, invoking the callback on every
// 4-byte slot. The walk ends when the record pointer reaches the registry's
// +0x7E44 end (or becomes null); the last row pointer is returned. The
// callback is a plain cdecl int(int, int). Byte-identical: the original's
// exact instruction sequence with the original thiscall + retn 4
// convention (the registry in ECX, the callback on the stack); the body is
// assembled in engine/luavm_asm.asm (ml.exe, linked as luavm_asm.obj)
// because the 3-byte `lea ecx, [ecx]` nop (8D 49 00) after the entry jmp
// cannot be emitted by the C inline assembler -- both it and ml normalize
// the zero-displacement lea to the 2-byte 8D 09 form -- so the nop is raw
// bytes there. Callers invoke it through the KoreForEachAllocFn __thiscall
// function-pointer cast.
extern "C" int KoreForEachAllocNode(int obj, int (*callback)(int, int));
typedef int (__thiscall *KoreForEachAllocFn)(int, int (*callback)(int, int));

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

// ---- KoreRebindAllocNodes' callees: the VM's global-table enumeration ----
//
// A VM table object (the KoreVM "globals" table) is laid out:
//   +0x00  (owner / tag)
//   +0x04  hash row count - 1
//   +0x08  20-byte hash row array, a row is {key, type, value, size, next}
//          (next is an absolute row-array pointer; type 3 = integer key,
//          type 4 = string key, 0 = empty, 5..10 / 12 = reference types)
//   +0x0C  8-byte dense row array, a row is {size, state}
//   +0x10  dense row count
// The enumeration walks the dense rows first, then the hash rows; the 2-word
// `key` parameter is the enumerator's in/out cursor of the form
// {index (as float), 3} while walking the dense rows, or {row key, row type}
// while walking the hash rows.  A 2-word `cursor` receives the entry
// {row value, row size} (the "not found" marker pair on exhaustion).

// dword_1DBCFE0 / dword_1DBCFE4 (both image-zero): the "not found" marker
// pair a KoreVM lookup fills when it misses and a table entry holds when it
// is absent.
static int g_dword_1DBCFE0 = 0;
static int g_dword_1DBCFE4 = 0;

// sub_5D30B0 (0x5D30B0 - 0x5D3115) -- KoreGlobalRowHash: the probe index
// into a table's 20-byte row hash array (the mask word first, the {key,
// type} pair second). Type 3 (integer key): (key >> 2) * 0x9E3779B1 & mask;
// type 4 (string key): (key ^ ((key ^ (key>>3)) >> 5)) & mask; anything
// else: the x87-truncated int of the key & mask.
// [NOT byte-identical] trivial reimplementation.
static int KoreGlobalRowHash(int mask, int *key)
{
    if (key[1] == 3)
        return (int)((((unsigned int)key[0] >> 2) * 0x9E3779B1u) & (unsigned int)mask);
    if (key[1] == 4)
    {
        unsigned int k = (unsigned int)key[0];
        return (int)((k ^ ((k ^ (k >> 3)) >> 5)) & (unsigned int)mask);
    }
    return (int)(((unsigned int)(int)(float)key[0]) & (unsigned int)mask);
}

// sub_5D3730 (0x5D3730 - 0x5D37AA) -- KoreFindGlobalStringRow: hash-chain
// search of the table's 20-byte rows for the string key (type 4, key at
// row+0, chain through row+16); returns the row index, or -1 when the table
// is empty (count-1 word == -1), the chain exhausts, or the row index
// unwinds to -1. Thiscall + retn 4 in the original (the table in ECX, the
// key on the stack); a plain function here. [NOT byte-identical]
// placement-dependent (the relative call shape of the original).
static int KoreFindGlobalStringRow(int table, int key)
{
    int *t = (int *)table;
    if (t[1] == -1)
        return -1;
    int *rows = (int *)t[2];
    int k4[2] = { key, 4 };
    int idx = KoreGlobalRowHash(t[1], k4);
    for (;;)
    {
        int *row = rows + 5 * idx;
        int v = (row[1] == 4) ? row[0] : 0;
        if (v == key)
            return idx;
        int next = row[4];
        if (next != 0)
        {
            idx = (next - t[2]) / 20;
            if (idx != -1)
                continue;
        }
        return -1;
    }
}

// sub_5D9940 (0x5D9940 - 0x5D9A86) -- KoreFindGlobalRow: find the 20-byte
// row index of a {key, type} pair in the table's hash rows. Empty table
// (the +0x04 count-1 word == -1) or type-0 key -> -1; an integer key (type
// 3, a whole number) -> the free-block-table lookup KoreFindFreeBlock (its
// -4 miss result is passed through); a string key (type 4) ->
// KoreFindGlobalStringRow. Everything else probes (KoreGlobalRowHash) and
// walks the row chain: a row matches when its type equals the key's type
// and (for type 3) the row key equals the key exactly as floats (an
// unordered NaN comparison falls straight to the chain), or when the key
// words are equal as dwords. Thiscall + retn 8 in the original (the table
// in ECX, the vm-core and the key on the stack; the vm-core is dead).
// [NOT byte-identical] placement-dependent (relative calls).
static int KoreFindGlobalRow(int table, int vmcore, int *key)
{
    (void)vmcore;
    int *t = (int *)table;
    if (t[1] == -1 || key[1] == 0)
        return -1;
    if (key[1] == 3)
    {
        float f = *(float *)key;
        int v = (int)f;
        if ((double)v == (double)f)
            return KoreFindFreeBlock(table, v);
    }
    else if (key[1] == 4)
        return KoreFindGlobalStringRow(table, key[0]);
    int idx = KoreGlobalRowHash(t[1], key);
    int *rows = (int *)t[2];
    for (;;)
    {
        int *row = rows + 5 * idx;
        if (row[1] == key[1])
        {
            if (key[1] == 3)
            {
                float a = *(float *)row;
                float b = *(float *)key;
                if (a == b)
                    return idx;
                if (a != a || b != b)   // unordered (NaN): skip to the chain
                    goto next_row;
            }
            if (row[0] == key[0])
                return idx;
        }
next_row:
        int next = row[4];
        if (next == 0)
            return -1;
        idx = (next - t[2]) / 20;
        if (idx == -1)
            return -1;
    }
}

// sub_5E2580 (0x5E2580 - 0x5E25A4) -- KoreTablePanic: the table-operation
// error path -- format the message (sub_5E24E0) then raise the VM panic
// (sub_5F1C30, which longjmps to the nearest handler or calls the error
// callback).  Only reached for a nil / NaN table index, which the panic
// traceback's "Snlu" flag string never produces, so it is a documented no-op
// stub (the original would not return).  A cdecl variadic in the original
// ((Size, Format, ...)); declared plain here.
static int KoreTablePanic(int vmcore, const char *format, ...)
{
    (void)vmcore; (void)format;
    return -100;
}

// sub_5D3640 (0x5D3640 - 0x5D3676) -- KoreFindEmptyRow: find an empty
// (type == 0) 20-byte hash row by walking the hash-row count (the table's
// +0x14 word) downward; returns the row index, or -1 when the table has no
// rows (count <= 0) or the walk reaches row 0.  thiscall in the original
// (the table in ECX); a plain free function here.  [NOT byte-identical] a
// thiscall hybrid and placement-dependent.
static int KoreFindEmptyRow(int table)
{
    int *t = (int *)table;
    if (t[5] <= 0)
        return -1;
    int *rows = (int *)t[2];
    for (;;)
    {
        int idx = --t[5];
        if (rows[idx * 5 + 1] == 0)   // the row's type word is 0
            return idx;
        if (idx == 0)
            return -1;
    }
}

// sub_5D3120 (0x5D3120 - 0x5D3188) -- KoreRowRelocateHash: recompute the
// probe index of a {key, type} pair after a table rehash.  Type 3 (integer
// key): the x87 round-to-nearest int of the key & mask; type 4 (string):
// (key ^ ((key ^ (key>>3)) >> 5)) & mask; anything else: (key>>2) *
// 0x9E3779B1 & mask.  [NOT byte-identical] a trivial reimplementation (the
// x87 round-to-nearest is approximated by a plain int cast).
static int KoreRowRelocateHash(int mask, int key, int type)
{
    if (type == 3)
        return (int)(((unsigned int)(int)(float)key) & (unsigned int)mask);
    if (type == 4)
    {
        unsigned int k = (unsigned int)key;
        return (int)((k ^ ((k ^ (k >> 3)) >> 5)) & (unsigned int)mask);
    }
    return (int)((((unsigned int)key >> 2) * 0x9E3779B1u) & (unsigned int)mask);
}

// sub_5E3F20 (0x5E3F20 - 0x5E406F) -- KoreTableResize: grow / rehash the
// table's 20-byte row hash array (the deep resize taken when no free row is
// available), charging the alloc budget.  Only reached when a table grows
// during a set, which the panic path never does, so it is a documented no-op
// stub.  [NOT byte-identical] remaining stub.
static int KoreTableResize(int vmcore, int key, int value)
{
    (void)vmcore; (void)key; (void)value;
    return 0;
}

// sub_5F7E10 (0x5F7E10 - 0x5F7E6F) -- KoreDebugMarkDirty: mark a referenced
// global as dirty for the debug GC.  `flags` is a pointer to a debug-flag
// byte (the table's -8 header) and `key` is the {payload, type} value pair.
// When the pair's type is a reference type (4..10 or 12), the flag byte's
// bit 0 is clear, and (for type 4) the payload's high bit is clear, the
// payload's header byte (payload-4 for type 4 / 7, else payload-8) has bit 0
// set; for a non-type-4 payload it then appends to the debug track
// (KoreDebugTrackAppend, a remaining stub).  thiscall + retn 8 hybrid in the
// original (declared plain here).  [NOT byte-identical] a retn-8 hybrid and
// placement-dependent.
static void KoreDebugMarkDirty(int flags, int key)
{
    int *pair = (int *)key;
    int type = pair[1];
    if (!((type >= 4 && type <= 10) || type == 12))
        return;
    if ((*(unsigned char *)flags & 1) != 0)
        return;
    int payload = pair[0];
    if (type == 4 && (*(int *)payload & 0x80000000) != 0)
        return;
    int header = (type == 7 || type == 4) ? payload - 4 : payload - 8;
    if ((*(unsigned char *)header & 1) != 0)
        return;
    *(int *)header |= 1;
    if (type != 4)
        (void)KoreDebugTrackAppend(flags, pair);
}

// sub_5E5040 (0x5E5040 - 0x5E52AB) -- KoreTableEnsureRow: reserve the 20-byte
// hash row for a {key, type} pair and return the row index the key now
// occupies (or a negative dense index, -index, for a whole-number integer key
// once the table has been resized).  The probe (KoreGlobalRowHash) row is used
// directly when its value word is empty; otherwise a free row
// (KoreFindEmptyRow) is taken and the probe's incumbent is relocated -- either
// chained right after the probe (when the incumbent's rehashed probe
// KoreRowRelocateHash equals the probe) or spliced into its own rehashed chain
// (the incumbent's 20 bytes are copied, the probe row is reset to the "not
// found" globals, and KoreDebugMarkDirty is invoked for the relocated key and
// value halves when the relocated row precedes the probe and the vm's +0x3C
// debug byte is 1) -- then the key pair is written into the reserved row. With
// no free row the value is stashed in the vm-core's +0x60 / +0x64 words and
// the table is resized (KoreTableResize); a whole-number integer key within
// the dense range then returns -index, otherwise the probe is recomputed.
// thiscall + retn 0Ch in the original (declared plain here).  [NOT
// byte-identical] a thiscall + retn 0Ch hybrid, placement-dependent (relative
// calls), and refs the high-VA "not found" globals.
static int KoreTableEnsureRow(int table, int vmcore, int key, int value)
{
    int *t = (int *)table;
    int *kp = (int *)key;      // {key, type}
    int *vp = (int *)value;    // {value, size}
    int *rows;
    int v8;
    for (;;)
    {
        rows = (int *)t[2];
        v8 = (rows != 0) ? KoreGlobalRowHash(t[1], kp) : 0;
        if (rows != 0 && rows[v8 * 5 + 3] == 0)
            break;   // empty probe row: write the key directly
        int freerow = KoreFindEmptyRow(table);
        if (freerow != -1)
        {
            int newhash = KoreRowRelocateHash(t[1], rows[v8 * 5 + 0], rows[v8 * 5 + 1]);
            if (newhash == v8)
            {
                // fast: chain the free row right after the probe
                rows[freerow * 5 + 4] = rows[v8 * 5 + 4];
                rows[v8 * 5 + 4] = (int)(rows + freerow * 20);
                v8 = freerow;
            }
            else
            {
                // walk the rehashed chain to find where the probe sits
                int pos = newhash;
                int found = 0;
                for (;;)
                {
                    int next = rows[pos * 5 + 4];
                    int nidx = (next == 0) ? -1 : (next - (int)rows) / 20;
                    if (nidx == v8)
                    {
                        found = 1;
                        break;
                    }
                    if (nidx == -1)
                        break;   // chain exhausted before the probe
                    pos = nidx;
                }
                if (found)
                {
                    rows[pos * 5 + 4] = (int)(rows + freerow * 20);
                    // copy the probe's 20 bytes into the free row
                    rows[freerow * 5 + 0] = rows[v8 * 5 + 0];
                    rows[freerow * 5 + 1] = rows[v8 * 5 + 1];
                    rows[freerow * 5 + 2] = rows[v8 * 5 + 2];
                    rows[freerow * 5 + 3] = rows[v8 * 5 + 3];
                    rows[freerow * 5 + 4] = rows[v8 * 5 + 4];
                    if (freerow < v8 && *(int *)(*(int *)vmcore + 0x3C) == 1)
                    {
                        int relocated[2] = { rows[v8 * 5 + 0], rows[v8 * 5 + 1] };
                        int rvalue[2]    = { rows[v8 * 5 + 2], rows[v8 * 5 + 3] };
                        KoreDebugMarkDirty(table - 8, (int)relocated);
                        KoreDebugMarkDirty(table - 8, (int)rvalue);
                    }
                    // reset the probe row to the "not found" globals
                    rows[v8 * 5 + 4] = 0;
                    rows[v8 * 5 + 2] = g_dword_1DBCFE0;
                    rows[v8 * 5 + 3] = g_dword_1DBCFE4;
                }
            }
            break;
        }
        // no free row: stash the value and resize the table
        *(int *)(*(int *)vmcore + 0x60) = vp[0];
        *(int *)(*(int *)vmcore + 0x64) = vp[1];
        (void)KoreTableResize(vmcore, key, value);
        *(int *)(*(int *)vmcore + 0x64) = 0;
        int idx = (int)(float)kp[0];
        if (kp[1] == 3 && (double)idx == (double)(float)kp[0] && idx > 0 &&
            (unsigned int)(idx - 1) < (unsigned int)t[4])
            return -idx;
        // else recompute the probe (loop)
    }
    rows[v8 * 5 + 1] = kp[1];
    rows[v8 * 5 + 0] = kp[0];
    return v8;
}

// sub_5E7400 (0x5E7400 - 0x5E7521) -- KoreTableSet: set a {key, type} ->
// {value, size} entry in a VM table.  The table layout is {+0x00 ?, +0x04
// hash mask, +0x08 20-byte hash rows, +0x0C 8-byte dense rows, +0x10 dense
// count, +0x14 hash row count}.  A whole-number integer key (type 3) within
// the dense range is written straight into the dense row (index-1); a NaN
// index raises the "table index is NaN" panic and a nil key (type 0) the
// "table index is nil" panic (both then fall through).  Otherwise the key's
// row is found (KoreFindGlobalRow): a found row gets the value pair written
// into its +0x08 / +0x0C words; a not-found key is inserted (KoreTableEnsureRow)
// -- a non-negative result writes the value into the new hash row (with
// KoreDebugMarkDirty when the vm's +0x3C debug byte is 1), a negative result
// is a dense index that writes into the dense row.  Returns the value's size
// word.  thiscall + retn 0Ch in the original (declared plain here).  [NOT
// byte-identical] a thiscall + retn 0Ch hybrid, placement-dependent (relative
// calls), and refs the high-VA "table index" string globals.
static int KoreTableSet(int table, int vmcore, int key, int value)
{
    int *t = (int *)table;
    int *kp = (int *)key;      // {key, type}
    int *vp = (int *)value;    // {value, size}
    if (kp[1] == 3)
    {
        float f = *(float *)kp;
        if (f != f)   // NaN index
            KoreTablePanic(vmcore, "table index is NaN");
        else
        {
            int idx = (int)f;
            if (idx > 0 && (unsigned int)(idx - 1) < (unsigned int)t[4])
            {
                int *dense = (int *)t[3];
                dense[(idx - 1) * 2 + 0] = vp[0];
                dense[(idx - 1) * 2 + 1] = vp[1];
                return vp[1];
            }
        }
    }
    else if (kp[1] == 0)
        KoreTablePanic(vmcore, "table index is nil");
    int row = KoreFindGlobalRow(table, vmcore, kp);
    if (row == -1)
    {
        int r = KoreTableEnsureRow(table, vmcore, key, value);
        if (r >= 0)
        {
            if (*(int *)(*(int *)vmcore + 0x3C) == 1)
                KoreDebugMarkDirty(table - 8, key);
            int *rows = (int *)t[2];
            rows[r * 5 + 2] = vp[0];
            rows[r * 5 + 3] = vp[1];
            return vp[1];
        }
        int *dense = (int *)t[3];
        int base = -(r + 1);   // r = -index, so base = index - 1
        dense[base * 2 + 0] = vp[0];
        dense[base * 2 + 1] = vp[1];
        return vp[1];
    }
    int *rows = (int *)t[2];
    rows[row * 5 + 2] = vp[0];
    rows[row * 5 + 3] = vp[1];
    return vp[1];
}

// sub_5D34E0 (0x5D34E0 - 0x5D3587) -- KoreNextHashGlobal: advance the
// enumerator to the first occupied (type != 0) 20-byte hash row at or after
// row `start` (0-based, the count is the +0x04 word + 1): the key pair
// becomes {row key, row type} and the cursor {row value, row size}. On
// exhaustion (an empty row array or the scan reaching the count) the cursor
// becomes the "not found" marker pair (key is left untouched, exactly as
// in the original). Thiscall + retn 12 in the original (the table in ECX,
// the cursor / start / key on the stack). [NOT byte-identical] a thiscall +
// retn 12 hybrid (declared object-first here) and refs the high-VA globals.
static void KoreNextHashGlobal(int table, int *cursor, int start, int *key)
{
    int *t = (int *)table;
    int *rows = (int *)t[2];
    if (rows == 0)
    {
        cursor[0] = g_dword_1DBCFE0;
        cursor[1] = g_dword_1DBCFE4;
        return;
    }
    int n = t[1] + 1;
    int i = start;
    while (i < n && rows[5 * i + 1] == 0)
        ++i;
    if (i >= n)
    {
        cursor[0] = g_dword_1DBCFE0;
        cursor[1] = g_dword_1DBCFE4;
        return;
    }
    int *row = rows + 5 * i;
    key[0]     = row[0];
    key[1]     = row[1];
    cursor[0]  = row[2];
    cursor[1]  = row[3];
}

// sub_5D3590 (0x5D3590 - 0x5D363E) -- KoreNextDenseGlobal: advance the
// enumerator over the table's 8-byte dense rows ({size, state}, count at
// +0x10, base +0x0C): the index resumes at the whole-number float in the
// key (0 when the key type is 0) and skips empty (state 0) rows. On a hit
// the key becomes {index+1 (as a float, + 2^32 when negative, exactly as
// the original), 3} and the cursor {row size, row state}; when the scan
// reaches the count it hands off to the hash rows through
// KoreNextHashGlobal (start 0). Thiscall + retn 8 in the original.
// [NOT byte-identical] a thiscall + retn 8 hybrid (declared object-first
// here) and placement-dependent (the relative call into KoreNextHashGlobal).
static void KoreNextDenseGlobal(int table, int *cursor, int *key)
{
    int *t = (int *)table;
    int i = (key[1] != 0) ? (int)(float)key[0] : 0;
    int count = t[4];
    int *rows = (int *)t[3];
    if (i < count)
        while (i < count && rows[2 * i + 1] == 0)
            ++i;
    if (i >= count)
    {
        KoreNextHashGlobal(table, cursor, 0, key);
        return;
    }
    int next = i + 1;
    *(float *)key = (float)next;
    if (next < 0)
        *(float *)key += 4294967296.0f;  // the original's 2^32 bias branch
    key[1] = 3;
    int *row = rows + 2 * i;
    cursor[0] = row[0];
    cursor[1] = row[1];
}

// sub_5DDBD0 (0x5DDBD0 - 0x5DDE1A) -- KoreNextGlobalByKey: the hash-side
// step of the global enumerator -- find the row of the key (KoreFindGlobalRow
// with the vm-core forwarded, dead) and advance to the next occupied row
// past it (KoreNextHashGlobal, start = row + 1; a -1 miss makes the cursor
// the "not found" marker pair). Returns the cursor pointer. Thiscall +
// retn 12 in the original. [NOT byte-identical] a thiscall + retn 12 hybrid
// (declared object-first here), placement-dependent, and refs the high-VA
// globals.
static int *KoreNextGlobalByKey(int table, int *cursor, int vmcore, int *key)
{
    int idx = KoreFindGlobalRow(table, vmcore, key);
    if (idx == -1)
    {
        cursor[0] = g_dword_1DBCFE0;
        cursor[1] = g_dword_1DBCFE4;
    }
    else
        KoreNextHashGlobal(table, cursor, idx + 1, key);
    return cursor;
}

// sub_5DF6B0 (0x5DF6B0 - 0x5DF73A) -- KoreNextGlobal: one step of the VM's
// global-table enumerator. `key` is the in/out cursor ({index-as-float, 3}
// while walking the dense rows, {row key, row type} while walking the hash
// rows) and `cursor` receives the entry {value, size}; the vm-core (the 2nd
// stack arg) is forwarded to the hash-side step but dead there. The dense
// side runs when the key type is 0 (fresh enumerator) or when it is 3 with
// a whole-number index in 1..dense count (the dense row array's count at
// +0x10 must be > 0); anything else takes the hash side
// (KoreNextGlobalByKey). Returns the cursor pointer. Thiscall + retn 12 in
// the original. [NOT byte-identical] a thiscall + retn 12 hybrid (declared
// object-first here) and placement-dependent (relative calls).
static int *KoreNextGlobal(int table, int *cursor, int vmcore, int *key)
{
    int dense = (key[1] == 0);
    if (!dense && *(int *)(table + 16) > 0 && key[1] == 3)
    {
        float f = *(float *)key;
        int v = (int)f;
        dense = ((double)v == (double)f && v > 0 && v - 1 < *(int *)(table + 16));
    }
    if (dense)
        KoreNextDenseGlobal(table, cursor, key);
    else
        KoreNextGlobalByKey(table, cursor, vmcore, key);
    return cursor;
}

// sub_5E8220 (0x5E8220 - 0x5E83D9) -- KoreRebindAllocNodes: the post-grow
// rebind of the heap's alloc-node chain (obj+0x2C head, a node is {flags,
// table, next}). For each node, every global of the node's table is
// enumerated (KoreNextGlobal, the fresh key from the "not found" globals);
// for each entry whose key half ({row key, row type}) or value half
// ({row value, row size}) carries a reference type (5..10 or 12), whose
// node flag bit is set (bit 2 for the key half, bit 1 for the value half)
// and whose payload header byte is unflagged (the low bit clear at
// pointer-4 for type 4 / 7 payloads, else pointer-8), the global is reset
// through KoreTableSet to the "not found" marker pair, the VM's +0x3C
// debug byte (behind the vm-core's +0x00, the heap's obj+0x04 slot) is
// checked and KoreDebugMarkDirty invoked when it is 1, and the alloc
// budget (obj+0x6C) is charged 5 per rebind. Every third node additionally
// charges the budget 1. The chain head (obj+0x2C) is cleared and the null
// tail returned. [NOT byte-identical] a thiscall in the original (declared
// object-first here), placement-dependent (relative calls into the
// enumerator, KoreTableSet and KoreDebugMarkDirty), and refs the high-VA
// globals.
static int KoreRebindAllocNodes(int obj)
{
    int *h = (int *)obj;
    int *node = (int *)h[11];      // the obj+0x2C alloc-node chain head
    if (node == 0)
    {
        h[11] = 0;
        return 0;
    }
    int vmcore = h[1];             // obj+0x04 (the table enumerator arg, dead)
    int counter = 0;
    for (;;)
    {
        int table = node[1];       // the node's table
        int key[2]  = { g_dword_1DBCFE0, g_dword_1DBCFE4 };
        int nil[2]  = { g_dword_1DBCFE0, g_dword_1DBCFE4 };
        int cursor[2];
        (void)KoreNextGlobal(table, cursor, vmcore, key);
        int i = cursor[0];
        int v = cursor[1];
        while (v != 0)
        {
            if ((key[1] >= 5 && key[1] <= 10 || key[1] == 12) && (node[0] & 2) != 0)
            {
                int p = (key[1] == 7) ? (key[0] - 4) : (key[0] - 8);
                if ((*(unsigned char *)p & 1) == 0)
                {
                    (void)KoreTableSet(table, vmcore, (int)key, (int)nil);
                    if (*(int *)(*(int *)vmcore + 0x3C) == 1)
                        KoreDebugMarkDirty((int)cursor, (int)nil);
                    h[27] -= 5;
                }
            }
            if ((v >= 5 && v <= 10 || v == 12) && (node[0] & 1) != 0)
            {
                int p = (v == 7) ? (i - 4) : (i - 8);
                if ((*(unsigned char *)p & 1) == 0)
                {
                    (void)KoreTableSet(table, vmcore, (int)key, (int)nil);
                    if (*(int *)(*(int *)vmcore + 0x3C) == 1)
                        KoreDebugMarkDirty((int)cursor, (int)nil);
                    h[27] -= 5;
                }
            }
            (void)KoreNextGlobal(table, cursor, vmcore, key);
            i = cursor[0];
            v = cursor[1];
        }
        if (++counter == 3)
        {
            --h[27];
            counter = 0;
        }
        node = (int *)node[2];
        if (node == 0)
        {
            h[11] = 0;
            return 0;
        }
    }
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
                (void)((KoreForEachAllocFn)KoreForEachAllocNode)(h[20], KoreMarkAllocTree);
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
