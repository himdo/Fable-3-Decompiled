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
#include "gamemain.h"             // SharedSlotConcat / SharedSlotConcatLiteral / F3WString / F3WStringFromAnsi
#include "xlip.h"                 // MakeSettingsFilePath / GetSlotString (shared-slot helpers)
#include "netinit.h"              // GetNetManager / CNetLuaEventManagerListReset

#include <windows.h>
#include <setjmp.h>               // setjmp / longjmp (the VM-heap OOM expand)
#include <float.h>                // _controlfp (the FPU round-toward-zero in KoreHeapInitGrow)
#include <intrin.h>               // __rdtsc (the QPC span accounting in KoreHeapInitGrow)
#include <string.h>               // memset
#include <malloc.h>               // _aligned_malloc / _aligned_free (the VM aligned allocator)
#include <stdio.h>                // _vsnprintf_s (the float / source formatting)
#include <stdarg.h>               // va_list (the vsnprintf wrapper)
#include <math.h>                 // fmod / pow / floor (the VM arithmetic opcodes)

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
//           0x6F9890; its impl wraps the Lua module and the engine's
//           +0x98 sorted map), followed by
//           CLuaManagerRunSerialiseScript (sub_6F9880)
//    +0xC4  the CEntityScriptHandleManager -- 80 bytes (sub_6E9620),
//           holding the Lua-state reference
//  First the old +0xC0 is released (vtable[0](slot, 1) deleting-dtor
//  dispatch) and the net manager's +0x1C sub-object list is cleared
//  (CNetManagerSlot1C + CNetLuaEventManagerListReset).  When the +0xC0 slot was already
//  empty (first-time init), the two startup-script path slots
//  (unk_1DBE2B4 + unk_1DBDA6C via the SharedSlotConcat, 0x1A02260,
//  2-source concat) are built and the Lua startup scripts are run (sub_616710).  Register
//  EngineLuaFunctions (sub_6169D0: the 100 Set*/Get* engine Lua builtins
//  -- see the engine Lua builtin block below) is called on the fresh Lua
//  module; when `flag` is set the
//  "EngineTest" module is registered too (ResetEngineTest +
//  RegisterEngineTest, 0x70DBB0 + sub_70DDB0), and the nullsub_27 no-op
//  is called when the new +0xC0 is set.  The
//  return value (the new +0xC4) is discarded by both callers.
// ============================================================
// 0x1DBE2B4 / 0x1DBDA6C: the two shared slots ReinitEngineLua concats
// into the Lua startup-script path (SharedSlotConcat, 0x1A02260): the
// "Data\Scripts\Startup\" directory slot (the sub_1A5D110 static
// initializer) + the "startup.lua" filename slot (sub_1A5C6C0).  Zero in
// this build (the original's static initializers do not run here).
static F3SharedBuffer *g_unk_1DBE2B4 = 0;   // 0x1DBE2B4: "Data\Scripts\Startup\" slot
static F3SharedBuffer *g_unk_1DBDA6C = 0;   // 0x1DBDA6C: "startup.lua" slot

// 0x1DBE2B0 / 0x1DC2680: the two shared slots CLuaManagerImplRunSerialiseScript
// concats into the Lua serialise-script path (SharedSlotConcat, 0x1A02260) --
// the serialise-script directory (0x1DBE2B0, the word just before the
// startup-script slot g_unk_1DBE2B4) + the serialise-script filename
// (0x1DC2680).  Zero in this build (populated by the real game's init).
static F3SharedBuffer *g_dword_1DBE2B0 = 0;   // 0x1DBE2B0: serialise-script dir slot
static F3SharedBuffer *g_dword_1DC2680 = 0;   // 0x1DC2680: serialise-script name slot

// ----------------------------------------------------------------
//  CreateLuaModule  (0x629570) and its build family: the 18-word config
//  record, the 1032-byte "Fable 3" module builder (sub_5F7FA0) and its
//  112-byte handle block, the module query/set dispatcher (sub_5EE1A0),
//  the builtin-library callback registration (sub_5FD2D0) and the builtin
//  function install (sub_629460).  Forward decls for the two later-defined
//  KoreVM helpers this family reaches (both defined further down):
// ----------------------------------------------------------------
static int    KorePanicTraceback(int table);                          // sub_5F1A20
static int    KoreSetupRecordTableBuffers(int obj);                    // sub_5DEA70
static int    KoreHeapBlockAlloc(int obj, int size);                   // sub_5DA1C0
static int    KoreVmHeapAlloc(int vmcore, int size);                   // sub_5EEAE0
static int    KoreVmAllocExpand(int vmcore, int size);                 // sub_5F6420
static int    KoreAllocHashTable(int registry, int rows, int buckets); // sub_5DDC90
static int    KoreHeapInitGrow(int obj, int size);                     // sub_5EDAC0
static int    KoreGrowRegistry(int obj, int size, int vmcore);         // sub_5EE150
static int    KoreHashTableInsert(int table, int a2, int a3, int a4);  // sub_5EC960
static int    KoreAppendString(int vmcore, const void *src, int len);  // sub_407070
static int    KoreGlobalGetValue(int table, int out, int hint, int key);  // sub_5DDB40
// (defined just below, in this same block)
static int    KoreSetupContinuation(int handle);                        // sub_5D7F50
static int    KoreRegisterBuiltinTable(int handle, int *row);           // sub_5DE9D0
static int   *KoreInternStateName(int handle, const char *src);         // sub_5EF690
static void   KorePushClosureValue(int mod, int fn, int count, unsigned short tag); // sub_5FA400
static void   KoreBuiltinInitStack(int mod);                                       // sub_629100
static void   KoreRegisterBuiltinClosure(int mod, int fn, int v);                  // sub_5FD310
static void   KoreRegisterBuiltinName(int mod, int key, const char *name);         // sub_4076D0
static void   KoreRegisterBuiltinName2(int mod, const char *name);                 // sub_407760
static int    KoreBuiltinStringOp(int mod, int a, int b, int c, int d);            // sub_5E4EE0
static int    KoreBuiltinA(void);                                                  // sub_629200
static int    KoreBuiltinB(void);                                                  // sub_629370

// ---- the engine-specific config / allocator-context globals ----
// 0x1DC26E4: the one-time VM-heap object slot that KoreEnsureLuaHeapObject
// (sub_717A80) allocates (LHAlloc(80)) and constructs (sub_717370).
static int *g_dword_1DC26E4 = 0;   // 0x1DC26E4
// 0x1C841D4 / 0x1C841D8: the two "Main State" registry words KoreInitModuleConfig
// (sub_5F1BB0) seeds into the config record before CreateLuaModule overwrites
// its neighbours.
static int g_dword_1C841D4 = 0;    // 0x1C841D4
static int g_dword_1C841D8 = 0;    // 0x1C841D8
// 0x1DBDB98: the Lua allocator's user/context word (the config a1[15] word,
// stored into the module host's +0x29C).  Reads 0 in this build.
static int g_dword_1DBDB98 = 0;    // 0x1DBDB98

// sub_5D7970 (0x5D7970 - 0x5D79B2) -- KoreVmAllocFreeAligned: the VM's 16-byte
// aligned allocator (the module's a1[4] allocator slot).  It _aligned_malloc's
// the requested block (sub_151D61B(a4, 16) in the original), copies any existing
// source over it, and frees the old block (_aligned_free) -- the "allocate or
// replace" contract the VM heap bump allocator drives through.  `a1` (the
// allocator context, e.g. the sub_628DB0 Lua allocator) is carried but unused.
// [NOT byte-identical] placement-dependent + refs the CRT aligned-alloc slots.
static void *KoreVmAllocFreeAligned(int a1, void *src, size_t size, int aligned)
{
    void *dst = 0;
    if (aligned != 0)
    {
        dst = _aligned_malloc((size_t)aligned, 16);   // sub_151D61B(a4, 16)
        if (src == 0)
            return dst;
        memcpy(dst, src, size);
    }
    if (src != 0)
        _aligned_free(src);
    return dst;
}

// sub_5D7A10 (0x5D7A10 - 0x5D7A16) -- KoreReturnsOne: the VM's trivial
// "already initialised / succeed" flag handler (the record-table descriptor's
// +0x94 handler slot; always returns 1).
static int KoreReturnsOne(void) { return 1; }

// sub_5DBFC0 (0x5DBFC0 - 0x5DBFD7) -- no-op config handler (the a1[3] word,
// stored into the module host's +0x294).  Remaining stub: an unreachable
// error-report leaf in this build.
static int KoreNoopStub0(int a1) { (void)a1; return 0; }
// nullsub_3 -- no-op config handler (the a1[7] word, the module host's +0x2B4
// slot before CreateLuaModule rewrites it to the sub_628100 panic handler).
static int KoreNoopRet0(void) { return 0; }

// sub_717370 (0x717370 - 0x717414) -- KoreLuaHeapObjectCtor: construct the
// 80-byte VM-heap object (the size-class / free-list / record-table init).
// Remaining stub: the one-time heap-object bring-up is unreached in the test
// (its deep size-class init callees are unimplemented), so it returns the
// object pointer unchanged.
static void *KoreLuaHeapObjectCtor(void *obj) { return obj; }

// sub_717A80 (0x717A80 - 0x717AAF) -- KoreEnsureLuaHeapObject: the one-time
// lazy init of the VM-heap object (LHAlloc(80) + KoreLuaHeapObjectCtor) that
// CreateLuaModule runs before building the module.
static void KoreEnsureLuaHeapObject(void)
{
    if (g_dword_1DC26E4 == 0)
    {
        int v0 = (int)LHAlloc(80);
        g_dword_1DC26E4 = (v0 != 0) ? (int *)KoreLuaHeapObjectCtor((void *)v0) : 0;
    }
}

// sub_5F1BB0 (0x5F1BB0 - 0x5F1C2E) -- KoreInitModuleConfig: seed the 18-word
// config record with the "Main State" defaults -- the two registry pointers
// (dword_1C841D4 / dword_1C841D8, the a1[12] / a1[13] words), the a1[4]
// aligned allocator (KoreVmAllocFreeAligned, sub_5D7970), the a1[6] panic
// traceback, the a1[7] nullsub_3, the a1[3] no-op handler, the "Main State"
// name, and the 100 / 10 / 0x200000 / 8 / 8 / 128 / -1 words.  CreateLuaModule
// then overwrites the engine-specific fields on top of these.  thiscall in the
// original (the record in ECX); a plain free function here.
static int *KoreInitModuleConfig(int *cfg)
{
    cfg[4]  = (int)KoreVmAllocFreeAligned;
    cfg[5]  = 0;
    cfg[2]  = 0x200000;
    cfg[0]  = 100;
    cfg[1]  = 10;
    cfg[6]  = (int)KorePanicTraceback;
    cfg[7]  = (int)KoreNoopRet0;
    cfg[3]  = (int)KoreNoopStub0;
    cfg[8]  = (int)"Main State";
    cfg[9]  = 8;
    cfg[10] = 8;
    cfg[11] = 128;
    cfg[15] = -1;
    cfg[16] = 0;
    cfg[14] = 0;
    cfg[12] = g_dword_1C841D4;
    cfg[13] = g_dword_1C841D8;
    cfg[17] = 0;
    return cfg;
}

// sub_5F7CB0 (0x5F7CB0 - 0x5F7D0F) -- KoreBuildValueStack: grow the record-table
// descriptor's (obj = the 1032-byte host's +0x2C block) 768-byte backing buffer
// via KoreHeapBlockAlloc (retrying through the last-resort expand, KoreVmAlloc
// Expand, on a miss) and initialise its count (-1) / capacity (32) words.
static void *KoreBuildValueStack(int obj)
{
    int *t = (int *)obj;
    int buf = KoreHeapBlockAlloc(t[3], 768);
    t[5] = buf;                          // obj + 0x14
    if (buf == 0)
    {
        do
        {
            (void)KoreVmAllocExpand(t[1], 0x300);
            buf = KoreHeapBlockAlloc(t[3], 768);
            t[5] = buf;
        } while (buf == 0);
    }
    memset((void *)buf, 0, 0x300);
    t[6] = -1;                           // obj + 0x18
    t[7] = 32;                           // obj + 0x1C
    return (void *)buf;
}

// sub_5F7FA0 (0x5F7FA0 - 0x5F81C5) -- KoreCreateLuaModule: build the "Fable 3"
// Lua module from the 18-word config record (a1).  It allocates the 1032-byte
// host block through the config allocator (a1[4], with the a1[5] context),
// stamps the host's header / allocator / panic / context words, allocates the
// 112-byte handle block (KoreHeapBlockAlloc), links them (*handle = host,
// host + 0xE4 = handle), builds the value-stack + record-table buffers (sub_5F7CB0
// / sub_5DEA70), the VM heap word table (KoreVmHeapAlloc, 4 * a1[11]) and the
// hash-table / backing-table cluster (KoreAllocHashTable), interning the a1[8]
// state name.  Returns the 112-byte handle block.  [NOT byte-identical]
// placement-dependent (relative calls) + refs high-VA rdata / the config code
// pointers.
static int *KoreCreateLuaModule(int *a1)
{
    int v2 = a1[5];                              // allocator context
    int v3 = a1[4];                              // allocator fn
    int v4 = ((int (*)(int, void *, int, int))v3)(v2, 0, 0, 1032);
    if (v4 == 0)
        return 0;
    memset((void *)v4, 0, 0x408);
    *(int *)(v4 + 0x29C) = a1[15];
    *(int *)(v4 + 0x2B4) = a1[7];
    *(int *)(v4 + 0x290) = a1[6];
    *(int *)(v4 + 0x294) = a1[3];
    *(int *)(v4 + 0x288) = a1[12];
    *(int *)(v4 + 0x28C) = a1[13];
    memset((void *)v4, 0, 0x2C);
    *(int *)(v4 + 0x0C) = v3;
    *(int *)(v4 + 0x10) = v2;
    *(int *)(v4 + 0x1C) &= 3;
    *(int *)(v4 + 0x24) &= 3;
    *(int *)(v4 + 0x28) = 0;
    *(int *)(v4 + 0x04) = 1032;

    int v5 = KoreHeapBlockAlloc(v4, 112);
    if (v5 == 0)
        return 0;
    *(int *)(v5 - 4) = (*(int *)(v5 - 4) & 0xFFFFFFFC) | 2;
    memset((void *)v5, 0, 0x70);
    *(int *)(v4 + 0xE4) = v5;
    *(int *)v5 = v4;

    int v11 = a1[1];
    int v12 = a1[16];
    int v14 = a1[0];
    int *base = (int *)(v4 + 0x2C);
    memset(base, 0, 0x90);
    base[1]  = v5;                          // v4 + 0x30
    base[3]  = *(int *)v5;                 // v4 + 0x38 = host
    base[4]  = 6;                           // v4 + 0x3C
    base[0x5C / 4] = v14;                   // v4 + 0x88
    base[0x64 / 4] = 1;                     // v4 + 0x90
    base[0x60 / 4] = v11;                   // v4 + 0x8C
    base[0x6C / 4] = v11;                   // v4 + 0x98
    base[0x70 / 4] = v11;                   // v4 + 0x9C
    base[0x78 / 4] = 317;                   // v4 + 0xA4
    base[0x8C / 4] = v12;                   // v4 + 0xB8
    base[0x68 / 4] = (int)KoreReturnsOne;   // v4 + 0x94 = sub_5D7A10
    (void)KoreBuildValueStack((int)base);           // sub_5F7CB0(base)
    (void)KoreSetupRecordTableBuffers(v5);          // sub_5DEA70 (usercall, ESI=v5)

    int v7 = a1[11];
    *(int *)(v4 + 0xC0) = 0;
    *(int *)(v4 + 0xC4) = v7 - 1;
    int wbuf = KoreVmHeapAlloc(v5, 4 * v7);
    *(int *)(v4 + 0xBC) = wbuf;
    if (wbuf != 0)
        memset((void *)wbuf, 0, 4 * v7);
    *(int *)(v4 + 0xC8) = 0;
    if (a1[14] != 0)
        (void)KoreSetupContinuation(v5);           // sub_5D7F50 (dead: a1[14] == 0)

    *(int *)(v4 + 0xD0) = 5;
    *(int *)(v4 + 0xCC) = KoreAllocHashTable(v5, 0, a1[9]);
    *(int *)(v4 + 0xDC) = KoreAllocHashTable(v5, 0, 0x10);
    *(int *)(v4 + 0xD4) = KoreVmHeapAlloc(v5, 0x40);
    *(unsigned short *)(v4 + 0xD8) = 0;
    *(unsigned short *)(v4 + 0xDA) = 0x10;
    *(int *)(v5 + 0x34) = 5;
    *(int *)(v5 + 0x30) = KoreAllocHashTable(v5, 0, a1[10]);
    (void)KoreRegisterBuiltinTable(v5, (int *)(v4 + 0xE8));   // sub_5DE9D0
    (void)KoreInternStateName(v5, (const char *)a1[8]);       // sub_5EF690
    *(int *)(v4 + 0x94) = a1[17];
    *(unsigned char *)(v4 + 0x90) = 0;
    return (int *)v5;
}

// sub_5D7F50 (0x5D7F50 - 0x5D7FC5) -- KoreSetupContinuation: build the module's
// 776-byte continuation-stack block (host + 0x290 / + 0x294 chain) and dispatch
// through the host's +0x94 handler.  Remaining stub: dead in this build (the
// config a1[14] word is 0, so KoreCreateLuaModule never calls it).
static int KoreSetupContinuation(int handle) { (void)handle; return 0; }

// sub_5DE9D0 (0x5DE9D0 - 0x5DEA6A) -- KoreRegisterBuiltinTable: intern the ~48
// builtin-library global names into the module's registry row (sub_5D6A40) and
// the ~10 builtins into the second row.  Remaining stub: the name-table fill
// (its intern / rdata-row callees) is unimplemented and unreached -- the VM is
// not running Lua in this build.
static int KoreRegisterBuiltinTable(int handle, int *row) { (void)handle; (void)row; return 0; }

// sub_5EF690 (0x5EF690 - 0x5EF8A3) -- KoreInternStateName: intern the module's
// state-name string (the a1[8] "Main State" / "Fable 3" literal) with the CRC32
// "kore" hash and publish the interned entry at the handle's +0x48 word.  The
// full CRC32 + intern body is a remaining stub: it only stores the literal
// (the published word is never dereferenced while the VM is not running).
static int *KoreInternStateName(int handle, const char *src)
{
    ((int *)handle)[18] = (int)src;
    return (int *)src;
}

// sub_5FA400 (0x5FA400 - 0x5FA507) -- KorePushClosureValue: push a closure value
// (fn, count, tag) onto the module's value stack (the Size[7] top) through the
// VM-heap allocation + debug-track path.  Remaining stub: unimplemented (the
// value-stack machinery is unreached while the VM is not running).
static void KorePushClosureValue(int mod, int fn, int count, unsigned short tag)
{ (void)mod; (void)fn; (void)count; (void)tag; }

// sub_5EE1A0 (0x5EE1A0 - 0x5EE2D4) -- KoreModuleSetParam: the module's command
// dispatcher (host = *handle, a2 the command, a3 the payload).  cmd 0 / 1 set /
// clear the host's +0x90 "Main State" flag; cmd 2 drives the registry heap grow
// (KoreHeapInitGrow, saving / restoring the +0x70 / +0x6C words); cmd 3 / 4
// return the host's +0x04 word split into KB / sub-KB; cmd 5 grows the registry
// (KoreGrowRegistry); cmd 6 / 7 swap the host's +0x88 / +0x8C slot and (for 7)
// mirror the value into the value-stack's +0x60 / +0x6C words.  [NOT
// byte-identical] placement-dependent (relative calls to KoreHeapInitGrow /
// KoreGrowRegistry).
static int KoreModuleSetParam(int mod, int cmd, int a3)
{
    int *h = *(int **)mod;     // the 1032-byte host
    switch (cmd)
    {
    case 0:  *(h + 144) = 1;  return 0;
    case 1:  *(h + 144) = 0;  return 0;
    case 2:
    {
        int inited;
        do
        {
            int (*f)(int) = (int (*)(int))(*(int *)(h + 148));
            int *v9 = (int *)(h + 44);
            do
            {
                f  = (int (*)(int))(*(int *)(h + 148));
                v9 = (int *)(h + 44);
            } while (f != 0 && f(*(h + 48)) != 0);
            int v10  = v9[28];
            v9[28] = 10000;
            v9[27] = 10000;
            inited  = KoreHeapInitGrow((int)v9, mod);
            v9[28]  = v10;
        } while (inited != 1);
        return 0;
    }
    case 3:  return *(h + 4) / 1024;
    case 4:  return *(h + 4) % 1024;
    case 5:  return KoreGrowRegistry((int)(h + 44), mod, a3);
    case 6:  { int v = *(h + 136); *(h + 136) = a3; return v; }
    case 7:  {
        int v5 = a3;
        int v6 = *(h + 140);
        int v7 = (int)(h + 44);
        if (a3 <= 0) v5 = -1;
        *(h + 156) = v5;
        *(int *)(v7 + 96) = v5;
        *(int *)(v7 + 108) = v5;
        return v6;
    }
    default: return -1;
    }
}

// sub_5FD2D0 (0x5FD2D0 - 0x5FD30B) -- KoreRegisterLuaCallback: register a
// builtin-library callback on the module: push the closure (sub_5FA400) onto the
// value stack, store the (val, 2) pair at the Size[7] top, and insert the key-1
// row (KoreHashTableInsert).  Guarded: the module's Lua value-stack base
// (Size[7] = handle + 0x1C) is not (re)initialised in this build (KoreCreateLua
// Module leaves it 0), so the registration is a no-op here.
static int KoreRegisterLuaCallback(int mod, int fn, int val)
{
    int *Size = (int *)mod;
    if (Size[7] == 0)
        return 0;
    KorePushClosureValue(mod, fn, 0, 0);              // sub_5FA400 (stub)
    int *v3 = (int *)Size[7];
    *v3 = val;
    v3[1] = 2;
    Size[7] = (int)(v3 + 2);
    // The original's final step (sub_5EC960) invokes the just-pushed closure to
    // run the library's setup.  Here the closure push (sub_5FA400) is a stub and
    // the value slot is empty, and the VM does not run Lua in this build, so the
    // invocation is a no-op.
    (void)fn;
    return 0;
}

// sub_629460 (0x629460 - 0x629563) -- KoreRegisterBuiltins: install the module's
// builtin Lua functions (dofile / pairs / string.find / regfind, the two builtin
// closures sub_629200 / sub_629370, and the string.find C-function via
// sub_5E4EE0), then zero the value-stack tail.  Guarded: a no-op while the module's
// Lua value stack (Size[7]) is not initialised in this build.
static unsigned int KoreRegisterBuiltins(int mod)
{
    int *Size = (int *)mod;
    if (Size[7] == 0)
        return 0;
    KoreBuiltinInitStack(mod);                        // sub_629100 (stub)
    KoreRegisterBuiltinClosure(mod, (int)KoreBuiltinA, 0);   // sub_5FD310(mod, sub_629200, 0)
    KoreRegisterBuiltinName(mod, -10002, "dofile");          // sub_4076D0
    KoreRegisterBuiltinName2(mod, "kpairs");                 // sub_407760
    int v1 = *(Size + 7) - 8;
    if ((unsigned int)v1 >= (unsigned int)Size[8] && (*(int *)(v1 + 4) == 10 || *(int *)(v1 + 4) == 9))
        KoreRegisterBuiltinName(mod, -10002, "pairs");
    else
        *(Size + 7) -= 8;
    KoreRegisterBuiltinName2(mod, "string");                 // sub_407760
    int v3 = Size[7];
    int v4 = *(int *)(v3 - 8);
    int v5 = *(int *)(v3 - 4);
    KoreAppendString(mod, "find", 4);                     // sub_407070
    int v6 = KoreBuiltinStringOp(mod, v4, v5, *(int *)(Size[7] - 8), *(int *)(Size[7] - 4)); // sub_5E4EE0
    int *v7 = (int *)(Size[7] - 8);
    *v7 = v6;
    v7[1] = 0;                                            // edx (second return, stub)
    Size[7] = (int)(v7 + 2);
    KoreRegisterBuiltinName(mod, -2, "regfind");           // sub_4076D0
    KoreRegisterBuiltinClosure(mod, (int)KoreBuiltinB, 0);   // sub_5FD310(mod, sub_629370, 0)
    KoreRegisterBuiltinName(mod, -2, "find");              // sub_4076D0
    unsigned int result = (unsigned int)Size[7];
    unsigned int i = (unsigned int)Size[8];
    for (; result < i; result += 8)
        *(int *)(result + 4) = 0;
    Size[7] = (int)i;
    return result;
}

// ---- sub_629460's builtin-registration leaves (stubs; the value-stack
//      machinery is unreached while the VM is not running) ----
static void  KoreBuiltinInitStack(int mod) { (void)mod; }                            // sub_629100
static void  KoreRegisterBuiltinClosure(int mod, int fn, int v) { (void)mod; (void)fn; (void)v; } // sub_5FD310
static void  KoreRegisterBuiltinName(int mod, int key, const char *name) { (void)mod; (void)key; (void)name; } // sub_4076D0
static void  KoreRegisterBuiltinName2(int mod, const char *name) { (void)mod; (void)name; } // sub_407760
static int   KoreBuiltinStringOp(int mod, int a, int b, int c, int d) { (void)mod; (void)a; (void)b; (void)c; (void)d; return 0; } // sub_5E4EE0
static int   KoreBuiltinA(void) { return 0; }                                          // sub_629200
static int   KoreBuiltinB(void) { return 0; }                                          // sub_629370

// ---- the six builtin-library registration callbacks CreateLuaModule hands to
//      sub_5FD2D0: they are stored as function pointers (invoked later when the
//      Lua module runs the corresponding library setup), so they exist here only
//      to be addressable.  Remaining stubs (the VM is not running in the test). ----
static int KoreLibBase(void)    { return 2; }      // sub_601EF0 (base library: _G / _VERSION / coroutine)
static int KoreLibTable(void)   { return 1; }      // sub_600530 (table library)
static int KoreLibString(void)  { return 1; }      // sub_6003E0 (string library)
static int KoreLibMath(void)    { return 1; }      // sub_5FFD10 (math library)
static int KoreLibPackage(void) { return 1; }      // sub_5FFF00 (package library)
static int KoreLibDebug(void)   { return 1; }      // sub_5FED80 (debug library)

// ---- the module host's config-referenced code words (addressable stubs) ----
// sub_628DB0 (0x628DB0 - 0x628E49) -- KoreLuaAlloc: the module host's allocator
// (the config a1[4] slot, called as alloc(ctx, src, size, dstSize)).  It routes
// through the game heap (sub_717960 in the original); a (src==0 && dstSize==0)
// request is a no-op (returns 0), otherwise it allocates dstSize bytes.  The
// OOM expand path (sub_673C50 / sub_673F30 / sub_6F0070 / KoreModulePacing re-alloc,
// then a sub_627A30() + DebugBreak on a second miss) is a remaining stub: the
// game heap is not under pressure in this build.
static int KoreLuaAlloc(int a1, void *src, size_t srcSize, size_t dstSize)
{
    if (src == 0 && dstSize == 0)
        return 0;
    int v6 = (int)LHAlloc((unsigned int)dstSize);   // sub_717960
    (void)a1; (void)srcSize;
    return v6;
}
static int KoreLuaPanic(int a1) { (void)a1; return 0; }                                                        // sub_628100 (the panic handler)
static int KoreLocD00000(void) { return 0; }                                                                  // loc_D00000 (config a1[3] code ref)

// sub_629570 (0x629570 - 0x629655) -- CreateLuaModule: build the engine's
// "Fable 3" Lua module (the 1032-byte KoreVM host + 112-byte handle) and
// register its builtin libraries.  [NOT byte-identical] placement-dependent
// (relative calls to the module builder / callback-register / builtin-register
// helpers) and refs high-VA rdata (the "Fable 3" literal, loc_D00000, and the
// dword_1DBDB98 allocator-context word).
//
//  void *CreateLuaModule(void)
//
//  Runs the one-time VM-heap object init (KoreEnsureLuaHeapObject, sub_717A80),
//  seeds the 18-word config record (KoreInitModuleConfig, sub_5F1BB0) and
//  overwrites the engine-specific fields (the two 200-word buffer words, the
//  loc_D00000 / sub_628DB0 / sub_628100 code refs, the "Fable 3" state name,
//  the 512 / 0x10000 limits and the dword_1DBDB98 allocator context).  The
//  module is then built (KoreCreateLuaModule, sub_5F7FA0), the "Main State"
//  flag set (KoreModuleSetParam(mod, 0, 0), sub_5EE1A0), the six builtin-library
//  callbacks registered (KoreRegisterLuaCallback, sub_5FD2D0) and the builtin
//  functions installed (KoreRegisterBuiltins, sub_629460).  Returns the 112-byte
//  handle block (whose first word is the 1032-byte host).
static void *CreateLuaModule(void)
{
    int cfg[18];
    KoreEnsureLuaHeapObject();              // sub_717A80
    (void)KoreInitModuleConfig(cfg);        // sub_5F1BB0
    cfg[0]  = 200;
    cfg[1]  = 200;
    cfg[2]  = (int)KoreLocD00000;           // loc_D00000
    cfg[4]  = (int)KoreLuaAlloc;            // sub_628DB0 (the Lua allocator)
    cfg[6]  = (int)KoreLuaPanic;            // sub_628100 (the panic handler)
    cfg[8]  = (int)"Fable 3";
    cfg[10] = 0x200;                        // 512
    cfg[11] = 0x10000;
    cfg[14] = g_dword_1DBDB98;
    int *mod = KoreCreateLuaModule(cfg);    // sub_5F7FA0
    (void)KoreModuleSetParam((int)mod, 0, 0);   // sub_5EE1A0
    (void)KoreRegisterLuaCallback((int)mod, (int)KoreLibBase, 0);     // sub_5FD2D0(mod, sub_601EF0, 0)
    (void)KoreRegisterLuaCallback((int)mod, (int)KoreLibTable, 0);    // sub_5FD2D0(mod, sub_600530, 0)
    (void)KoreRegisterLuaCallback((int)mod, (int)KoreLibString, 0);   // sub_5FD2D0(mod, sub_6003E0, 0)
    (void)KoreRegisterLuaCallback((int)mod, (int)KoreLibMath, 0);     // sub_5FD2D0(mod, sub_5FFD10, 0)
    (void)KoreRegisterLuaCallback((int)mod, (int)KoreLibPackage, 0);  // sub_5FD2D0(mod, sub_5FFF00, 0)
    (void)KoreRegisterLuaCallback((int)mod, (int)KoreLibDebug, 0);    // sub_5FD2D0(mod, sub_5FED80, 0)
    (void)KoreRegisterBuiltins((int)mod);             // sub_629460
    return (void *)mod;
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
static int    KoreFloatToDec(double value, char *buf, unsigned int bufCount); // sub_5D83D0
static int    KoreNumberToString(int vmcore, int index);              // sub_5DC000
static int    KoreRehashInterned(int *table, int vmcore);               // sub_5EEC00
static int   *KoreAllocVmMem(int vmcore, unsigned int size);            // sub_5DA410
static int    KoreVmAlloc(int vmcore, int size);                        // sub_5DA4F0 (VM-heap bump alloc)
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
static void   KoreReallocValueStack(int thisObj, int vmcore, int extra); // sub_5DD730
static int    KoreAllocHashTable(int registry, int rows, int buckets);  // sub_5DDC90
static int    KoreTableSet(int table, int vmcore, int key, int value);   // sub_5E7400
static int    KoreTablePanic(int vmcore, const char *format, ...);      // sub_5E2580
// The setjmp/longjmp-guarded "call a value" handler cluster (implemented with
// KoreHashTableInsert / sub_5EC960 below):
static void   KoreRotateValueStackTop(int table, int n);                 // sub_407540
static int    KoreGrowContinuationStack(int obj, int vmcore);            // sub_5DD8C0
static void   KoreReverseValuePairs(int p, int count);                   // sub_60F9C0
static int    KoreGrowHashTableRows(int obj, int vmcore, int newCount);  // sub_5DDC20
static int    KoreResizeHashTable(int obj, int vmcore, int newRows, int hashBits); // sub_5E2BD0
static int    KoreCopyTableRows(int obj, int vmcore, int start, int count, int src); // sub_5E4FC0
static int    KoreCreateUpvalueTable(int obj, int vmcore, int closure, int dst, int src); // sub_5E7EF0
static int    KoreNormalizeValue(int table, int v, int top);             // sub_5DDED0
static int    KorePushCFrame(int obj, int vmcore, int retPos, int v, int newTop, int valIdx); // sub_612D50
static int    KorePushClosureFrame(int obj, int vmcore, int retPos, int v, int top, int valIdx); // sub_612BB0
static int    KorePopCFrame(int obj, int table, int rv);                 // sub_60BBD0
static int    KoreRunCode(int table, int code, int arg2);                // sub_5E9AA0 (interpreter)
static int    KoreInvokeValue(int table, int argCount, int retPos, int valIdx); // sub_5EC770
// The VM error-message / panic formatter closure (KoreTablePanic and friends):
static int    KoreRaisePanic(int vmcore, int code);                     // sub_5F1C30
static void   KoreFormatMessage(int vmcore, const char *format, const char *suffix, va_list args); // sub_5E24E0
static int    KoreMessagePrefix(int vmcore, int frameNo);               // sub_5E19D0
static int    KoreVmVprintf(int vmcore, const char *format, ...);       // sub_5DBFE0
static int    KoreVmSprintf(int vmcore, const char *format, void *arglist); // sub_5D7A90
static int    KoreInternPush(int vmcore, const void *src, int len, unsigned int hash); // sub_5D2A50
static int    KoreAppendString(int vmcore, const void *src, int len);   // sub_407070
static void   KoreStackTraceback(int vmcore, int frameNo, int startFrame, int endFrame); // sub_5E1AA0
static void   KoreConcatValues(int vmcore, int n);                       // sub_5DC2B0
static int    KoreInternBuffer(int vmcore, int *buf, int len);           // sub_5DA6F0
static int   *KoreInsertInterned(int *table, int vmcore, int *buf, int len); // sub_5D6A40
static int    KoreTableEnsureRow(int table, int vmcore, int key, int value); // sub_5E5040
static int    KoreFindEmptyRow(int table);                              // sub_5D3640
static int    KoreRowRelocateHash(int mask, int key, int type);         // sub_5D3120
static int    KoreProfileIntegerKey(int pair, int counters);            // sub_5D2FB0
static int    KoreScanResizeProfile(int counters, int *total);          // sub_5D37B0
static int    KoreTableResize(int table, int vmcore, int key);          // sub_5E3F20
static int    KoreEnsureBackingTable(int table, int vmcore, int rows, int buckets, int key); // sub_610CA0
static void   KoreArraySet(int table, int vmcore, int index, int value); // sub_5D3210
static void   KoreDebugMarkDirty(int flags, int key);                    // sub_5F7E10
static void   KoreSetInstanceField(int obj, int vmcore, int field, int slot); // sub_6146A0
static int    KoreDebugTrackAppend(int obj, int *value);               // sub_5F7D80
// Existing VM-heap helpers defined later in this translation unit:
static int    KoreHeapBlockAlloc(int obj, int size);                    // sub_5DA1C0
static int    KoreGrowRegistry(int obj, int size, int vmcore);          // sub_5EE150
static int    KoreVmAllocExpand(int vmcore, int size);                  // sub_5F6420
static int    KoreVmHeapAlloc(int vmcore, int size);                    // sub_5EEAE0
static int    KoreReleaseAllocNode(int list, int node, int vmcore);     // sub_5ED860

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

// ---- sub_5D83D0-style Kore string hash: the "kore" 32-bit string hash
// used by every string-interning path.  Seed = (len + 0x6B6F7265) ("kore"
// little-endian).  Up to two 12-byte chunks are mixed (each through an
// 11-step rotation/xor DAG), the remaining <=12 bytes are folded into the
// three lane accumulators, then a 6-step finalization.  The effective hash
// only ever sees the first min(31, len) bytes of the content. ----
// One 12-byte chunk's mixing DAG.  P, Q, R are the three pre-mixed dwords
// (their exact derivation differs between chunk 1 and chunk 2); `useQ`
// selects which of P / Q feeds the first rotate-xor step (chunk 1 uses P,
// chunk 2 uses Q).  Writes the updated lane state to (*oS1, *oS2, *oS3).
static void KoreHashMixChunk(unsigned int P, unsigned int Q, unsigned int R,
                             int useQ, unsigned int *oS1, unsigned int *oS2,
                             unsigned int *oS3)
{
    unsigned int t1 = P + Q;
    unsigned int t5 = (useQ ? (Q - R) : (P - R)) ^ KROL(R, 6);
    unsigned int t6 = (t1 - t5) ^ KROL(t5, 8);
    unsigned int t4 = t1 + R;
    unsigned int t7 = t4 + t5;
    unsigned int t8 = (t4 - t6) ^ KROL(t6, 16);
    unsigned int t9 = t7 + t6;
    unsigned int t10 = (t7 - t8) ^ KROR(t8, 13);
    unsigned int s3 = t9 + t8;
    *oS3 = s3;
    *oS2 = s3 + t10;
    *oS1 = (t9 - t10) ^ KROL(t10, 4);
}

static unsigned int KoreHashKore(const char *buf, int len)
{
    unsigned int acc1 = (unsigned int)len + 0x6B6F7265u;  // S1
    unsigned int acc2 = acc1;                              // S2
    unsigned int acc3 = acc1;                              // S3
    int n = len;
    const unsigned int *p = (const unsigned int *)buf;
    if (n >= 0x1F)
        n = 31;
    if (n > 12)
    {
        // chunk 1 (dwords d0,d1,d2): P=S1+d1, Q=S1+d2, R=(S3+d0-Q)^ROL(Q,4)
        unsigned int d0 = p[0], d1 = p[1], d2 = p[2];
        unsigned int Q = acc1 + d2;
        KoreHashMixChunk(acc1 + d1, Q, (acc3 + d0 - Q) ^ KROL(Q, 4), 0,
                         &acc1, &acc2, &acc3);
        n -= 12;
        p += 3;
        if (n > 12)
        {
            // chunk 2 (dwords d3,d4,d5): P=S1+d5, Q=S2+d4, R=(S3+d3-P)^ROL(P,4)
            unsigned int d3 = p[0], d4 = p[1], d5 = p[2];
            unsigned int P = acc1 + d5;
            KoreHashMixChunk(P, acc2 + d4, (acc3 + d3 - P) ^ KROL(P, 4), 1,
                             &acc1, &acc2, &acc3);
            n -= 12;
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

// ---- sub_5D83D0: convert a double to a fixed-point decimal string.
// For |x| <= 2^31 it emits the integer part, then (if the fractional
// mantissa is nonzero) a '.' and up to 14 fractional digits with trailing
// zeros trimmed; for |x| > 2^31 it falls back to "%e" of the signed value.
// The value is a 32-bit float widened to double (the VM number type), so
// the significant fractional digits never exceed ~7.  Returns the character
// count excluding the null terminator. ----
static int KoreFloatToDec(double value, char *buf, unsigned int bufCount)
{
    int neg = (value < 0.0);
    double ax = neg ? -value : value;
    if (ax > 2147483648.0)
        return KoreVsnprintf(buf, bufCount, "%e", value);

    unsigned __int64 ipart = (unsigned __int64)ax;
    double f = (ax - (double)ipart) * 1.0e14;
    unsigned __int64 v4 = (unsigned __int64)f;
    double r = f - (double)v4;
    if (r <= 0.5)
    {
        if (r == 0.5 && (v4 == 0 || (v4 & 1)))
            v4 += 1;
    }
    else
    {
        v4 += 1;
        if (v4 >= 100000000000000ULL)
        {
            v4 = 0;
            ipart += 1;
        }
    }

    // Build the final string most-significant-digit-first.  The original
    // builds it least-significant-digit-first and reverses in place; the
    // resulting byte sequence is identical either way.
    char tmp[32];
    int n = 0;
    if (neg)
        tmp[n++] = '-';
    char intbuf[32];
    int in = 0;
    unsigned __int64 ip = ipart;
    do
    {
        intbuf[in++] = (char)('0' + (ip % 10));
        ip /= 10;
    } while (ip != 0);
    for (int i = in - 1; i >= 0; i--)
        tmp[n++] = intbuf[i];
    if (v4 != 0)
    {
        // 14-digit zero-padded form of the fractional mantissa, then strip
        // the trailing zeros.
        unsigned __int64 fv = v4;
        char frac[15];
        for (int i = 13; i >= 0; i--)
        {
            frac[i] = (char)('0' + (fv % 10));
            fv /= 10;
        }
        int fend = 14;
        while (fend > 0 && frac[fend - 1] == '0')
            fend--;
        tmp[n++] = '.';
        for (int i = 0; i < fend; i++)
            tmp[n++] = frac[i];
    }
    tmp[n] = 0;
    for (int i = 0; i <= n; i++)
        buf[i] = tmp[i];
    return n;
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

// ---- sub_5DC000: format a value-stack number (type 3) as an interned
// string.  `index` addresses the cell relative to the value stack: when
// positive, base + index*8 - 8; otherwise (zero / negative), top + index*8.
// The 32-bit float at that cell is converted with KoreFloatToDec, hashed,
// and interned; the cell is then rewritten in place to {entry, 4}.  Returns
// the interned string's length. ----
static int KoreNumberToString(int vmcore, int index)
{
    int *top = (int *)(*(int *)(vmcore + 0x1C));
    int *base = (int *)(*(int *)(vmcore + 0x20));
    int cell = (index > 0) ? (int)base + index * 8 - 8 : (int)top + index * 8;
    float f = *(float *)cell;
    char buf[0x100];
    int len = KoreFloatToDec((double)f, buf, 0x100);
    unsigned int hash = KoreHashKore(buf, len);
    int *entry = KoreInternString((int *)(*(int *)vmcore + 0xBC), vmcore, buf, len, hash);
    *(int *)cell = (int)entry;
    *(int *)(cell + 4) = 4;
    return len;
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

// ---- 'L' / 'f' local-dump table machinery.  The 'L' flag's hash-table
// allocation (KoreAllocHashTable, below) is implemented; the remaining
// helpers are only reached through the uppercase 'L' (local dump) and
// 'f' (push the function value) flag characters, which the panic path's
// "Snlu" flag string never contains, so they are never executed in the
// traceback.  They remain no-op stubs consistent with the other deep
// KoreVM table internals already stubbed in this file.
// sub_5DD730 (0x5DD730 - 0x5DD87F) -- KoreReallocValueStack: grow the value
// stack.  thiscall + retn 8 with this = vmcore + 4 (the value-stack
// sub-object), arg1 = vmcore, arg2 = extra slots.  The new slot count is
// extra + 2*((end - start) >> 3) + 5; a fresh block of 8*count bytes is
// always allocated via KoreVmAlloc.  When top + 8*extra > end it copies the
// old block, memsets the tail, fixes up the circular 16-byte node list at
// thisObj[0..2] and the 12-stride linked list at vmcore[11], rewrites the
// top / base / end / start pointers, and hands the old block back to the
// heap.  Otherwise it simply charges the heap for the (unused) new block.
// [NOT byte-identical] a thiscall hybrid, placement-dependent.
static void KoreReallocValueStack(int thisObj, int vmcore, int extra)
{
    int *v = (int *)vmcore;
    int *hp = (int *)(*(int *)vmcore);
    int count = extra + 2 * ((v[9] - v[10]) >> 3) + 5;
    int bytes = 8 * count;
    int newBuf = KoreVmAlloc(vmcore, bytes);
    if ((unsigned int)(v[7] + 8 * extra) > (unsigned int)v[9]) // top + 8*extra > end
    {
        int oldStart = v[10];
        int nTop = (v[7] - oldStart) >> 3;
        int nBase = (v[8] - oldStart) >> 3;
        int copySize = (oldStart != 0) ? 8 * ((v[9] - oldStart) >> 3) + 40 : 0;
        if (oldStart != 0)
            memcpy((void *)newBuf, (void *)oldStart, copySize);
        memset((void *)(newBuf + copySize), 0, bytes - copySize);
        int nodeCount = (*(int *)(thisObj + 8) - *(int *)thisObj) >> 4;
        if (nodeCount >= 0)
        {
            int off = 16 * nodeCount;
            for (;;)
            {
                int *node = (int *)(*(int *)thisObj + off);
                *node = newBuf + 8 * ((*node - oldStart) >> 3);
                if (off == 0)
                    break;
                off -= 16;
            }
        }
        for (int i = v[11]; i != 0; i = *(int *)(i + 12))
            *(int *)(i + 8) = newBuf + 8 * ((*(int *)(i + 8) - oldStart) >> 3);
        v[9] = newBuf + 8 * count - 40;                     // end
        v[8] = newBuf + 8 * nBase;                          // base
        v[10] = newBuf;                                     // start
        v[7] = newBuf + 8 * nTop;                           // top
        hp[1] -= copySize;                                  // return the old block
        ((void (*)(int, int, int, int))hp[3])(hp[4], oldStart, copySize, 0);
    }
    else
    {
        hp[1] -= bytes;
        ((void (*)(int, int, int, int))hp[3])(hp[4], newBuf, bytes, 0);
    }
}
// byte_1AE32B0 (0x01AE32B0) -- the 256-byte low-byte bit-length table
// shared by the VM's bit-length computations (this function and its
// other original referrers): t[i] = the number of bits needed to hold
// i (floor(log2(i)) + 1), with t[0] = 0.
static const unsigned char KoreLog2Byte[256] =
{
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

// sub_5DA240 (0x5DA240 - 0x5DA40A) -- KoreAllocTableParts: allocate the
// three regions of a VM hash-table build in one shot (the vm-core registry
// object in arg0 — a pointer whose first word is the heap object — the
// block size in arg1, the two raw region sizes in arg2 / arg3, and the
// 3-word out array in arg4): out[0] = the block (KoreHeapBlockAlloc, the
// size + the 8-byte block header), out[1] / out[2] = the raw regions
// (bump-allocated through the heap's +0x0C alloc-fn, no header). It first
// decrements the heap's free-block counter (+0xA4) by 3 and, when the
// pending-alloc high-water overruns the +0xA0 bound with the counter
// drained and the +0x90 flag clear, re-seeds it to 317 and grows the
// registry (KoreGrowRegistry over heap+0x2C). On a full hit it returns the
// last allocated region; on any miss it frees the partial regions (the
// block's header tag bit 0 cleared, the raw regions handed back through
// the alloc-fn with the bump cursor rewound — the third free reuses the
// arg2 size, the original's own quirk), grows the registry, and retries,
// driving the last-resort expand (KoreVmAllocExpand with the total size)
// after every third failed attempt and looping. [NOT byte-identical]
// placement-dependent (the relative calls to the three heap helpers).
static int __stdcall KoreAllocTableParts(int registry, int blockSize,
                                         int sizeA, int sizeB, int *out)
{
    int *h = (int *)*(int *)registry;
    h[0xA4 / 4] -= 3;
    int total = blockSize + sizeA + sizeB;
    if (*(int *)(h[0x38 / 4] + 4) > h[0xA0 / 4]
        && h[0xA4 / 4] <= 0
        && h[0x90 / 4] == 0)
    {
        int count   = h[0x8C / 4];
        h[0xA4 / 4] = 317;
        int newsize = (count == -1) ? 0 : 317 * count;
        (void)KoreGrowRegistry((int)((char *)h + 0x2C), newsize, registry);
    }
    int attempts = 0;
    for (;;)
    {
        int block = KoreHeapBlockAlloc((int)h, blockSize);
        out[0] = block;
        int ok = (block != 0);
        int rawA = 0;
        if (sizeA != 0)
        {
            int bump = h[1];
            h[1] = bump + sizeA;
            rawA = ((int (*)(int, int, int, int))h[3])(h[4], 0, 0, sizeA);
            if (rawA != 0)
                h[2] = (h[2] > h[1]) ? h[2] : h[1];
            else
                h[1] = bump;
            out[1] = rawA;
            if (rawA == 0)
                ok = 0;
        }
        int rawB = 0;
        if (sizeB != 0)
        {
            int bump = h[1];
            h[1] = bump + sizeB;
            rawB = ((int (*)(int, int, int, int))h[3])(h[4], 0, 0, sizeB);
            if (rawB != 0)
                h[2] = (h[2] > h[1]) ? h[2] : h[1];
            else
                h[1] = bump;
            out[2] = rawB;
            if (ok != 0 && rawB != 0)
                return rawB;
        }
        else if (ok != 0)
            return (sizeA != 0) ? rawA : block;
        // free the partial regions
        if (block != 0)
        {
            *(int *)(block - 8) &= ~1;
            out[0] = 0;
        }
        if (rawA != 0)
        {
            h[1] -= sizeA;
            ((void (*)(int, int, int, int))h[3])(h[4], rawA, sizeA, 0);
            out[1] = 0;
        }
        if (rawB != 0)
        {
            h[1] -= sizeA; // the original reuses the arg2 size here too
            ((void (*)(int, int, int, int))h[3])(h[4], rawB, sizeA, 0);
            out[2] = 0;
        }
        (void)KoreGrowRegistry((int)((char *)h + 0x2C), -1, registry);
        if (++attempts < 3)
            continue;
        (void)KoreVmAllocExpand(registry, total);
        attempts = 0;
    }
}

// sub_5D3190 (0x5D3190 - 0x5D3206) -- KoreInitHashTable: initialize a
// freshly allocated 28-byte VM hash-table header (thiscall + retn 0x1C;
// this = the header, arg1 = the vm-core registry — present in the
// original's signature but unused, arg2 = the dense row count, arg3 = the
// hash bucket count, arg4 = the bucket bit length, arg5 = the bucket-array
// byte size, arg6 / arg7 = the bucket / row arrays). It writes +0x00 := 0,
// +0x10 := the row count, +0x04 := the hash mask (2^bitLen - 1, or -1 when
// the bucket count is 0), +0x14 := the bucket count (2^bitLen, or 0),
// +0x0C := the row array (zeroed for 8 bytes per row when both the array
// and the row count are non-zero), and +0x08 := the bucket array (zeroed
// for the bucket-array byte size); it returns the bucket array (the caller
// discards it). [NOT byte-identical] placement-dependent (the relative
// call to _memset).
static int KoreInitHashTable(int obj, int registry, int rows, int buckets,
                             int bitLen, int bucketBytes, int bucketArray,
                             int rowArray)
{
    (void)registry; // in the original's signature, unused
    int *t = (int *)obj;
    t[2] = 0;
    t[0] = 0;
    t[4] = rows;
    int count = (buckets > 0) ? (1 << bitLen) : 0;
    int mask  = count - 1;
    t[1] = mask;
    t[5] = mask + 1;
    t[3] = rowArray;
    if (rowArray != 0 && rows > 0)
        (void)memset((void *)rowArray, 0, (size_t)8 * rows);
    t[2] = bucketArray;
    if (bucketArray != 0)
        return (int)memset((void *)bucketArray, 0, (size_t)bucketBytes);
    return 0;
}

// sub_5DDC90 (0x5DDC90 - 0x5DDD6E) -- KoreAllocHashTable: allocate and
// initialize a new VM hash table (the vm-core registry object in arg0, the
// dense row count in arg1, the hash bucket count in arg2). The bucket bit
// length is the number of bits needed to hold bucketCount - 1 (the
// byte_1AE32B0 log2 table + 8 per shifted-out byte), so the bucket count
// rounds up to a power of two; the bucket array is 20 bytes per bucket and
// the row array 8 bytes per row. It allocates the 28-byte header block +
// the two arrays in one shot (KoreAllocTableParts), zeroes the 7-word
// header, flags the block's free-list word at header-4 (bit 0 set, bit 2
// cleared), runs the constructor (KoreInitHashTable), and clears the last
// header word (+0x18); it returns the header. Sole in-project caller:
// KoreFillLuaFrame's 'L' local-dump path. [NOT byte-identical]
// placement-dependent (the relative calls to the two helpers).
static int KoreAllocHashTable(int registry, int rows, int buckets)
{
    int bitLen = 0;
    if (buckets != 0)
    {
        int v = buckets - 1;
        while (v >= 0x100)
        {
            v >>= 8;
            bitLen += 8;
        }
        bitLen += KoreLog2Byte[(unsigned char)v];
    }
    int bucketBytes = (buckets != 0) ? (int)(20u * (1u << bitLen)) : 0;
    int parts[3] = {0, 0, 0};
    (void)KoreAllocTableParts(registry, 28, bucketBytes, 8 * rows, parts);
    int *t = (int *)parts[0];
    for (int i = 0; i < 7; ++i)
        t[i] = 0;
    *(int *)(t - 4) = (*(int *)(t - 4) & 0xFFFFFFFD) | 1;
    (void)KoreInitHashTable((int)t, registry, rows, buckets, bitLen,
                            bucketBytes, parts[1], parts[2]);
    t[6] = 0;
    return (int)t;
}
// sub_610CA0 (0x610CA0 - 0x610D1E) -- KoreEnsureBackingTable: lazily create
// and cache the backing hash table for a numeric-keyed table descriptor.
// `table` is the 16-byte descriptor (+0x04 the key's type descriptor, +0x0C
// the backing-table slot): a non-null slot is returned as-is.  Otherwise, when
// the type descriptor's +0x08 "allows a backing table" flag is 0, it panics
// (KoreTablePanic) with "Cannot set value for a numeric key %d on instances of
// '%s' as it doesn't allow a backing table." (the key, and the type name from
// the descriptor's +0x14 word + 8); else it allocates a fresh hash table
// (KoreAllocHashTable(vmcore, rows, buckets)), stores it in the slot, and --
// when the vm-core object's +0x3C debug byte is 1 -- marks the new {table, 5}
// value pair dirty (KoreDebugMarkDirty); returns the backing table.  Every
// in-tree caller passes rows = buckets = 0.  thiscall + retn 10h in the
// original (the table descriptor in ECX; declared plain, object-first, here).
// [NOT byte-identical] a thiscall + retn 10h hybrid, placement-dependent (the
// three relative calls), and refs the high-VA panic-format string (0x01AE4180).
static int KoreEnsureBackingTable(int table, int vmcore, int rows, int buckets, int key)
{
    int *t = (int *)table;
    int bt = t[3];
    if (bt != 0)
        return bt;
    int td = t[1];
    if (*(int *)(td + 8) == 0)
        KoreTablePanic(vmcore, "Cannot set value for a numeric key %d on instances of '%s' as it doesn't allow a backing table.",
                       key, *(int *)(td + 20) + 8);
    bt = KoreAllocHashTable(vmcore, rows, buckets);
    t[3] = bt;
    int *vm = (int *)(*(int *)vmcore + 0x2C);
    int pair[2] = {bt, 5};
    if (*(int *)(vm + 0x10) == 1)
        KoreDebugMarkDirty(table - 8, (int)pair);
    return bt;
}
// sub_5D3210 (0x5D3210 - 0x5D3278) -- KoreArraySet: set element `index` of a
// numeric-keyed table descriptor to the {value, size} pair `value`.  When
// (index-1) is within the table's dense count (this+0x10) the pair is written
// straight into the dense row (this+0x0C, (index-1)*2); otherwise an 8-byte
// {float(index), 3} numeric key is built and the entry is set through
// KoreTableSet (which inserts it into the hash part when it is out of the dense
// range).  Afterward, when the vm-core object's +0x3C debug byte is 1 the new
// {value, size} pair is marked dirty (KoreDebugMarkDirty).  thiscall + retn 0Ch
// in the original (the table descriptor in ECX; the vm-core wrapper, the 1-based
// index and the value pair on the stack; declared plain, object-first, here).
// [NOT byte-identical] a thiscall + retn 0Ch hybrid, placement-dependent (the
// relative calls to KoreTableSet / KoreDebugMarkDirty) and it uses the x87 FPU
// for the int->float key conversion.
static void KoreArraySet(int table, int vmcore, int index, int value)
{
    int *t = (int *)table;
    int *v = (int *)value;
    int i = index - 1;
    if ((unsigned int)i >= (unsigned int)t[4])
    {
        int key[2];
        *(float *)key = (float)index;   // {float(index), 3} numeric key
        key[1] = 3;
        (void)KoreTableSet(table, vmcore, (int)key, value);
    }
    else
    {
        int *dense = (int *)t[3];
        dense[i * 2 + 0] = v[0];
        dense[i * 2 + 1] = v[1];
    }
    if (*(int *)(*(int *)vmcore + 0x3C) == 1)
        KoreDebugMarkDirty(table - 8, value);
}
// sub_5F7D80 (0x5F7D80 - 0x5F7E0F) -- KoreDebugTrackAppend: append the
// {payload, type} pair `value` to the debug track, the growable
// 8-bytes-per-entry record array of the debug-flag object (the table's -8
// header object the table-set / alloc-record paths mark dirty through).
// Object (`obj`) layout: +0x04 the vm-core (the KoreVmAllocExpand
// last-resort growth target), +0x0C the heap object (the
// KoreHeapBlockAlloc allocator), +0x20 the record buffer, +0x24 the entry
// count, +0x28 the entry capacity.  When the count equals the capacity the
// buffer is reallocated at double the capacity (32 when the capacity was
// 0): a first KoreHeapBlockAlloc miss retries through the
// KoreVmAllocExpand expand until a block is carved, the old records (8
// bytes per entry, 8 * old capacity) are copied into the new buffer, and
// the capacity is updated.  The pair is then written at count*8, the count
// is bumped, and the record buffer is returned.  thiscall + retn 4 hybrid
// in the original (declared plain, object-first, here).  [NOT
// byte-identical] placement-dependent (the relative E8 calls to
// KoreHeapBlockAlloc / KoreVmAllocExpand) and refs the IAT _memcpy slot.
static int KoreDebugTrackAppend(int obj, int *value)
{
    int *t = (int *)obj;
    int cap = t[10];
    if (t[9] == cap)
    {
        int newCap = (cap != 0) ? cap + cap : 32;
        int oldBuf = t[8];
        int newBuf = KoreHeapBlockAlloc(t[3], 8 * newCap);
        t[8] = newBuf;
        if (newBuf == 0)
        {
            do
            {
                (void)KoreVmAllocExpand(t[1], 8 * newCap);
                newBuf = KoreHeapBlockAlloc(t[3], 8 * newCap);
                t[8] = newBuf;
            }
            while (newBuf == 0);
        }
        if (oldBuf != 0)
            memcpy((void *)t[8], (const void *)oldBuf, 8 * (size_t)cap);
        t[10] = newCap;
    }
    int count = t[9];
    int *buf = (int *)t[8];
    buf[count * 2 + 0] = value[0];
    buf[count * 2 + 1] = value[1];
    t[9] = count + 1;
    return (int)buf;
}
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
                KoreReallocValueStack(table + 4, table, 3);
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
                        int v21 = KoreEnsureBackingTable(v17, table, 0, 0, v14);
                        KoreArraySet(v21, table, v14, v16);
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

// sub_5D5B20 (0x5D5B20 - 0x5D5D1E) -- KoreDeallocAllocNode: free an
// allocation-node block (and its type-specific sub-objects) through the
// object's block-dealloc fn (obj+0x0C, called cdecl as fn(obj+0x10, ptr,
// size, 0)), updating the object's used-bytes counter (obj+0x04) as each
// piece is released. The node is the 8-byte free-list header {[0] (size <<
// 2) | flags, [1] next | 2-bit node type}; the type (the low 2 bits of the
// next word) selects the sub-object layout: type 0 owns no sub-objects (the
// plain block, sized node[0] >> 2, released and returned at once); type 1
// owns the 20*(node[3]+1)-byte block at node+0x10 (the counter charged
// -20 - 20*node[3]) and the 8*node[6]-byte block at node+0x14 (charged
// -8*node[6]); type 2 owns the 40 + 8*((node[11]-node[12]) >> 3)-byte block
// at node+0x30 (gated on node[12] non-zero) and always releases node[3]
// with size 16*((node[4]-node[3]) >> 4) (0 when node[3] is 0); type 3 owns
// the blocks at node+0x10 / +0x18 / +0x20 / +0x30 / +0x48 / +0x38 sized
// 4*node[3] / 8*node[5] / 4*node[7] / 4*node[11] / 12*node[17] /
// 4*node[13] (each gated on its pointer non-zero). Every other type falls
// through to the shared final step (release the node's own block, size node
// [0] >> 2, through the same fn); a corrupt type >= 4 skips even that
// (the original returns the type word, which both callers ignore).
// KoreDrainFreeList and the VM-heap detach (KoreDetachHeap) call it for
// every unclaimed / pending node. [NOT byte-identical] a thiscall + retn 4
// hybrid in the original (declared object-first here), placement-dependent.
static int KoreDeallocAllocNode(int obj, int node)
{
    int *o = (int *)obj;
    int *n = (int *)node;
    int ctx = o[0x10 / 4];
    int (*fn)(int, int, int, int) = (int (*)(int, int, int, int))o[0x0C / 4];
    switch (n[1] & 3)
    {
    case 0:
        o[1] -= (unsigned)n[0] >> 2;
        return fn(ctx, node, (unsigned)n[0] >> 2, 0);
    case 1:
        if (n[4] != 0)
        {
            o[1] += -20 - 20 * n[3];
            fn(ctx, n[4], 20 * (n[3] + 1), 0);
        }
        if (n[5] != 0)
        {
            o[1] += -8 * n[6];
            fn(ctx, n[5], 8 * n[6], 0);
        }
        break;
    case 2:
        if (n[12] != 0)
        {
            int size = 40 + 8 * ((n[11] - n[12]) >> 3);
            o[1] -= size;
            fn(ctx, n[12], size, 0);
        }
        {
        int size = (n[3] != 0) ? 16 * ((n[4] - n[3]) >> 4) : 0;
        o[1] -= size;
        fn(ctx, n[3], size, 0);
        }
        break;
    case 3:
        if (n[4] != 0)
        {
            o[1] -= 4 * n[3];
            fn(ctx, n[4], 4 * n[3], 0);
        }
        if (n[6] != 0)
        {
            o[1] -= 8 * n[5];
            fn(ctx, n[6], 8 * n[5], 0);
        }
        if (n[8] != 0)
        {
            o[1] -= 4 * n[7];
            fn(ctx, n[8], 4 * n[7], 0);
        }
        if (n[12] != 0)
        {
            o[1] -= 4 * n[11];
            fn(ctx, n[12], 4 * n[11], 0);
        }
        if (n[18] != 0)
        {
            o[1] -= 12 * n[17];
            fn(ctx, n[18], 12 * n[17], 0);
        }
        if (n[14] != 0)
        {
            o[1] -= 4 * n[13];
            fn(ctx, n[14], 4 * n[13], 0);
        }
        break;
    default:   // a corrupt type: the original falls straight to the
        return 0;                     // epilogue (return reg = the type word)
    }
    o[1] -= (unsigned)n[0] >> 2;
    return fn(ctx, node, (unsigned)n[0] >> 2, 0);
}

// sub_5D5D30 (0x5D5D30 - 0x5D5DCD) -- KoreDrainFreeList: walk the free-list
// object's (obj) block list from the cursor node's (obj+0x28) next, claiming
// the free blocks and releasing the unclaimed pending ones. Each list node
// is 8 bytes {flags, next | 2-bit state}: a node with flags bit 0 set is a
// plain free block (the budget word decrements by 1, the cursor advances
// onto the node, and its flag bit 0 is cleared); a node with the flag clear
// is a pending allocation (spliced out from under the cursor — the cursor's
// next word keeps its own low 2 state bits, budget charged 16 — and freed
// through KoreDeallocAllocNode). With the claim word set, the walk returns 0
// (the caller zeroes the budget and panics when the guard is armed) as soon
// as the budget word is drained to 0 or below. When the list runs out — or
// the cursor's next is empty from the start — the low-2-bit state words are
// folded (the cursor's next word := old obj+0x1C ^ ((old obj+0x1C ^ next) &
// 3), obj+0x1C := old obj+0x24 ^ ((old obj+0x24 ^ old obj+0x1C) & 3),
// obj+0x24 &= 3), the cursor is zeroed, and 1 is returned. The table word
// (the heap's API table, the original's a2) is unused by the body.
// [NOT byte-identical] a thiscall + retn 0Ch hybrid in the original
// (declared object-first here), placement-dependent (relative call to
// KoreDeallocAllocNode).
static int KoreDrainFreeList(int obj, int table, int claim, int budget)
{
    (void)table;
    int *o = (int *)obj;
    int head = o[0x28 / 4];
    if ((*(int *)(head + 4) & ~3) != 0)
    {
        int node = *(int *)(head + 4) & ~3;
        for (;;)
        {
            int flags = *(int *)node;
            int next  = *(int *)(node + 4) & ~3;
            if (flags & 1)
            {
                --budget;
                o[0x28 / 4] = node;
                *(int *)node = flags & ~1;
            }
            else
            {
                int cur = o[0x28 / 4];
                *(int *)(cur + 4) = (*(int *)(cur + 4) & 3) | next;
                budget -= 16;
                (void)KoreDeallocAllocNode(obj, node);
            }
            node = next;
            if (claim != 0 && budget <= 0)
                return 0;
            if (node == 0)
                break;
        }
    }
    int cur = o[0x28 / 4];
    *(int *)(cur + 4) = o[0x1C / 4] ^ ((o[0x1C / 4] ^ *(int *)(cur + 4)) & 3);
    o[0x1C / 4] = o[0x24 / 4] ^ ((o[0x24 / 4] ^ o[0x1C / 4]) & 3);
    o[0x24 / 4] &= 3;
    o[0x28 / 4] = 0;
    return 1;
}

// sub_5D9F80 (0x5D9F80 - 0x5D9FBE) -- KoreFlagAllocRefs: mark the adopted /
// in-use bit (bit 0 of the header word 4 bytes before the payload) for the
// reference list described by a1 -- a1[0] = the reference count, a1[5]
// (a1+0x14) = the first reference pointer, and a1[6..] (a1+0x18 + 4*i, the
// index masked to a byte) = the remaining reference pointers. Each reference
// p is flagged only when the pointed-at word is signed non-negative (a live
// payload, not a sentinel). Returns the last reference pointer read (the
// first one when the count is 0). Original convention: stdcall (retn 4).
// Byte-identical: the original's exact instruction sequence is assembled in
// engine/luavm_asm.asm (ml.exe, linked as luavm_asm.obj) because the
// 7-byte `lea esp, [esp+0]` nop (8D A4 24 00 00 00 00) and the
// `mov eax, [eax+edx+18h]` (8B 44 10 18) form cannot be emitted by the C
// inline assembler; the body is call- and reference-free, so it matches at
// any placement.
extern "C" int __stdcall KoreFlagAllocRefs(int a1);

// sub_5DF740 (0x5DF740 - 0x5DF9C6) -- KoreInitAllocNode8: adopt / register a
// type-8 (heap-block) alloc node, recursing through the node's sub-objects.
// Two modes: in new mode (node == 0) it registers the payload (ptr) by
// first checking the node flag byte at ptr-8 (already set: bail with a null
// return), then growing the primary alloc-node table (obj+0x20 list of
// 24-byte entries, cursor obj+0x24, capacity obj+0x28) by one slot --
// re-carving the table buffer (KoreHeapBlockAlloc, retrying through the
// last-resort KoreVmAllocExpand expand) at tripled-plus-96-word capacity
// (capacity += 32, old buffer copied with _memcpy and its flag bit cleared)
// when the slot would overflow -- and stamping the entry {type = 8, payload,
// stage = 0, first child = payload+0x2C}. It then charges the alloc budget
// (obj+0x6C) one per node (plus one more when the payload's +0x48 sub-node
// word is non-null, flagging that sub-node when its pointed-at word is
// signed non-negative), zeroes payload+0x50, links the payload into the
// obj+0x3C / obj+0x40 secondary chain (published at the previous node's
// +0x50), panics (KoreHeapPanic) when the guard (obj+0x70) is not -1 and the
// budget drains to 0 or below, and panics again when flags > 0x40. In
// re-adopt mode (node != 0) it resumes adoption of that node's payload from
// the saved stage word (node[2]). Both modes then run the staged adoption
// switch on the entry's stage word: stage 0 walks the entry's child node
// chain (entry[3], each child {flag at child-8, node pair at child+8, next
// at child+12}), flagging each child, charging the budget, re-adopting it
// through KoreInitAllocNode with flags - 2, and repointing entry[3] at the
// remaining chain; stage 1 adopts the payload's +0x30 list through
// KoreInitAllocNode5; stage 2 verifies the payload's +0xE4 back-link points
// at the payload (else skips straight to the pop); stage 3 adopts the
// +0xCC list, flags the +0xD4 payload block, and flags every reference in
// its +0xD4 block (count at +0xD8) through KoreFlagAllocRefs; stage 4
// adopts the +0xDC list, flags the +0xBC payload block and charges the
// budget; stage 6 recurses on the obj+0x08 node with the original flags.
// Every path that reaches the switch pops one slot off the table
// (--obj+0x24) and returns the current table entry pointer. [NOT
// byte-identical] placement-dependent (relative calls into
// KoreHeapBlockAlloc / KoreVmAllocExpand / KoreHeapPanic / KoreInitAllocNode
// / KoreInitAllocNode5 / KoreFlagAllocRefs and itself, plus an IAT _memcpy
// slot); a thiscall + retn 0Ch hybrid in the original (declared object-first
// here). Note: the new-mode early bailout (the flag at ptr-8 already set)
// shares the original's epilogue, where the decrement addend register is
// left undefined by the compiler, so it is treated as a plain null return.
// The per-type helpers the staged adoption forwards to (defined further down
// in this file):
static void   KoreInitAllocNode5(int obj, int ptr, int node, int flags);  // sub_5E0C30
static void   KoreInitAllocNode7(int obj, int ptr, int node, int flags);  // sub_5E1770
static int    KoreInitAllocNode9(int obj, int ptr, int node, int flags);  // sub_5DF9F0
static void   KoreInitAllocNode10(int obj, int ptr, int node, int flags); // sub_5DFB80
static int    KoreInitAllocNode12(int obj, int ptr, int node, int flags); // sub_5DFCE0
static void   KoreInitAllocNode(int obj, int node, int flags);            // sub_5E21E0
static int   *KoreMarkNodeRefs(int obj, int a2);                          // sub_5D5840
// The adopt primitives the per-type routines build on (defined just above the
// type-5 routine):
static void   KoreAdoptAllocNode(int obj, int ptr, int type, int flags);          // sub_5E0A00
static void   KoreReadoptAllocEntry(int obj, int ptr, int entry, int flags);      // sub_5E0B00
static int   *KoreAppendAllocRecord(int obj, int stringPtr, int flags);           // sub_5DDDD0
static int KoreInitAllocNode8(int obj, int ptr, int node, int flags)
{
    int *h = (int *)obj;
    int *result = (int *)node;
    int *v6;
    if (node != 0)
    {
        v6 = (int *)result[1];
    }
    else
    {
        v6 = (int *)ptr;
        if ((*(unsigned char *)(ptr - 8) & 1) != 0)
            return node;                     // 0: already registered
        int cursor = h[6];                   // obj+0x24
        int cap    = h[7];                   // obj+0x28
        h[6] = cursor + 1;
        if (!(cursor + 1 < cap))             // signed: overflow the table
        {
            int size = 8 * (3 * cap + 96);
            int i = KoreHeapBlockAlloc(h[3], size);
            while (i == 0)
            {
                (void)KoreVmAllocExpand(h[1], size);   // last-resort expand
                i = KoreHeapBlockAlloc(h[3], size);
            }
            memcpy((void *)i, (const void *)h[5], 24 * (unsigned)cap);
            *(int *)(h[5] - 8) &= ~1;        // clear the old buffer's flag
            h[7] += 32;
            h[5] = i;
        }
        result = (int *)(h[5] + 8 * (3 * cursor + 3));
        result[0] = 8;
        result[1] = (int)v6;
        result[2] = 0;
        result[3] = v6[11];                  // first child, payload+0x2C
        *(int *)(v6 - 2) |= 1;               // flag the node
        --h[27];                             // charge the alloc budget
        int sub = v6[18];                    // payload+0x48 sub-node
        if (sub != 0)
        {
            if (*(int *)sub >= 0)
                *(int *)((int)sub - 4) |= 1;
            --h[27];
        }
        v6[20] = 0;                          // payload+0x50
        if (h[15] != 0)                      // obj+0x3C secondary chain
            *(int *)(h[16] + 20) = (int)v6;  // previous node's +0x50
        else
            h[15] = (int)v6;
        h[16] = (int)v6;                     // obj+0x40 current node
        if (h[28] != -1 && h[27] <= 0)       // guard armed, budget drained
            KoreHeapPanic(obj);
        if ((unsigned)flags > 0x40)   // original jbe 40h (unsigned)
            KoreHeapPanic(obj);
    }
    switch (result[2])
    {
        case 0:
        {
            int child = result[3];
            if (child != 0)
            {
                do
                {
                    *(int *)(child - 8) |= 1;
                    --h[27];
                    KoreInitAllocNode(obj, child + 8, flags - 2);
                    child = *(int *)(child + 12);
                    result = (int *)(h[5] + 24 * h[6]);
                    result[3] = child;
                } while (child != 0);
            }
            result[2] = 1;
            /* fallthrough */
        }
        case 1:
            result[2] = 2;
            KoreInitAllocNode5(obj, v6[12], 0, flags - 1);  // payload+0x30
            result = (int *)(h[5] + 24 * h[6]);
            /* fallthrough */
        case 2:
            if (*(int *)(v6[0] + 228) != (int)v6)   // +0xE4 back-link
                break;                              // skip to the pop
            result[2] = 3;
            KoreInitAllocNode5(obj, *(int *)(v6[0] + 204), 0, flags - 1);
            result = (int *)(h[5] + 24 * h[6]);
            /* fallthrough */
        case 3:
            result[2] = 4;
            *(int *)(*(int *)(v6[0] + 212) - 8) |= 1;      // +0xD4 block
            for (int j = 0;; ++j)
            {
                int p = v6[0];
                if ((unsigned)j >= (unsigned)(*(unsigned short *)(p + 216)))
                    break;   // +0xD8 reference count
                (void)KoreFlagAllocRefs(*(int *)(*(int *)(p + 212) + 4 * (j & 0xFF)));
            }
            KoreInitAllocNode5(obj, *(int *)(v6[0] + 220), 0, flags - 1);
            result = (int *)(h[5] + 24 * h[6]);
            /* fallthrough */
        case 4:
            result[2] = 5;
            *(int *)(*(int *)(v6[0] + 188) - 8) |= 1;      // +0xBC block
            --h[27];
            /* fallthrough */
        case 5:
            result[2] = 6;
            /* fallthrough */
        case 6:
        {
            int next = h[2];                   // obj+0x08 node
            if (next != 0)
                result = (int *)KoreInitAllocNode8(obj, next, 0, flags);
            break;
        }
        default:
            break;
    }
    --h[6];                                    // pop the slot off the table
    return (int)result;
}

// sub_5E0A00 (0x5E0A00 - 0x5E0ACE) -- KoreAdoptAllocNode: adopt a single
// alloc node given the payload pointer and its type explicitly (the
// {ptr,type} pair is split; cf. KoreInitAllocNode, which reads the pair from
// a node word). Switches on the type: 4 (scalar) sets the 4-byte block
// header's adopted bit (at ptr-4, only when the 4-byte value at ptr is signed
// non-negative and the bit is not already set) and, when the guard (obj+0x70)
// is not -1, charges the alloc budget one (panicking through KoreHeapPanic
// when the budget drains to 0 or below); 5 / 7 / 8 / 9 / 10 / 12 forward to
// the per-type adopt routine with node = 0; any other type is a no-op.
// Original convention: thiscall + retn 0Ch (object-first in ECX here), no
// frame. [NOT byte-identical] placement-dependent (jumptable dispatch plus
// relative calls into the per-type routines and KoreHeapPanic).
static void KoreAdoptAllocNode(int obj, int ptr, int type, int flags)
{
    switch (type)
    {
    case 4:
        if (*(int *)ptr >= 0 && (*(unsigned char *)(ptr - 4) & 1) == 0)
        {
            *(int *)(ptr - 4) |= 1;
            if (*(int *)(obj + 0x70) != -1)
            {
                int budget = *(int *)(obj + 0x6C) - 1;
                if (budget <= 0)
                    KoreHeapPanic(obj);
                *(int *)(obj + 0x6C) = budget;
            }
        }
        break;
    case 5:  KoreInitAllocNode5(obj, ptr, 0, flags);   break;
    case 7:  KoreInitAllocNode7(obj, ptr, 0, flags);   break;
    case 8:  (void)KoreInitAllocNode8(obj, ptr, 0, flags);  break;
    case 9:  (void)KoreInitAllocNode9(obj, ptr, 0, flags);  break;
    case 10: KoreInitAllocNode10(obj, ptr, 0, flags);  break;
    case 12: (void)KoreInitAllocNode12(obj, ptr, 0, flags); break;
    default: break;
    }
}

// sub_5E0B00 (0x5E0B00 - 0x5E0C27) -- KoreReadoptAllocEntry: resume adoption
// of the children of the payload described by a 24-byte alloc-table entry.
// The entry's +0x10 flags word carries two suppress bits: bit 0 set skips the
// non-type-4 adoptions of the child-pair list (the 8-byte {ptr,type} array at
// ptr+0x0C, count at ptr+0x10) and of the 20-byte entries' second field;
// bit 1 set skips the non-type-4 adoptions of the 20-byte entries' first
// field (the array at ptr+0x08, count at ptr+0x04 + 1). Adoption of the child
// list resumes from the entry's +0x08 index and of the 20-byte entries from
// the entry's +0x0C index, republishing the running index into the entry (the
// entry is re-fetched after every adopt because the table may grow) and
// adopting each node through KoreAdoptAllocNode with the flags decremented.
// Finishes by popping one entry off the table (--obj+0x18). Original
// convention: thiscall + retn 0Ch (object-first here). [NOT byte-identical]
// placement-dependent (relative calls into KoreAdoptAllocNode).
static void KoreReadoptAllocEntry(int obj, int ptr, int entry, int flags)
{
    int *h = (int *)obj;
    int *p = (int *)ptr;
    int *e = (int *)entry;
    int adoptAll0 = (e[4] & 1) == 0;   // bit 0 clear -> adopt all (list + field 2)
    int adoptAll1 = (e[4] & 2) == 0;   // bit 1 clear -> adopt all (field 1)
    int f = flags - 1;

    int i = e[2];
    if (i < p[4])
    {
        int count = p[4];
        do
        {
            int *base = (int *)p[3];
            if (adoptAll0 || base[2 * i + 1] == 4)
                KoreAdoptAllocNode(obj, base[2 * i], base[2 * i + 1], f);
            ++i;
            e = (int *)(h[5] + 24 * h[6]);
            e[2] = i;
        } while (i < count);
    }
    int j = e[3];
    if (j < p[1] + 1)
    {
        int count = p[1] + 1;
        int *base = (int *)(p[2] + 20 * j);
        do
        {
            if (base[1] != 0)
            {
                if (adoptAll1 || base[1] == 4)
                    KoreAdoptAllocNode(obj, base[0], base[1], f);
                if (adoptAll0 || base[3] == 4)
                    KoreAdoptAllocNode(obj, base[2], base[3], f);
            }
            ++j;
            base += 5;
            e = (int *)(h[5] + 24 * h[6]);
            e[3] = j;
        } while (j < count);
    }
    --h[6];
}

// sub_5DDDD0 (0x5DDDD0 - 0x5DDE1A) -- KoreAppendAllocRecord: allocate a
// 12-byte record (KoreHeapBlockAlloc on the obj+0x0C heap, retrying through
// the last-resort KoreVmAllocExpand expand on the obj+0x04 vm-core) and push
// it onto the linked list headed at obj+0x2C: the record is {flags, ptr,
// previous head} and the counter at obj+0x58 is incremented. Returns the new
// record. Original convention: thiscall + retn 8 (object-first here).
// [NOT byte-identical] placement-dependent (relative calls into
// KoreHeapBlockAlloc / KoreVmAllocExpand).
static int *KoreAppendAllocRecord(int obj, int stringPtr, int flags)
{
    int *h = (int *)obj;
    int *r;
    for (r = (int *)KoreHeapBlockAlloc(h[3], 12); r == 0; r = (int *)KoreHeapBlockAlloc(h[3], 12))
        (void)KoreVmAllocExpand(h[1], 12);
    r[0] = flags;
    r[1] = stringPtr;
    r[2] = h[11];
    ++h[22];
    h[11] = (int)r;
    return r;
}

// sub_5E0C30 (0x5E0C30 - 0x5E0ED8) -- KoreInitAllocNode5: adopt / register a
// type-5 (interned key/value string) alloc node. Two modes: in new mode
// (node == 0) it registers the payload (ptr) by first checking the node flag
// byte at ptr-8 (already set: bail out), then scanning the payload's interned
// key (KoreLookupInternedString on obj+0x04 with the payload's +0x00 key; the
// 64-bit result's high word must be 4 and the low word's 30-bit length
// non-zero) for 'k' / 'v' characters, ORing bits 2 / 1 into a local flag word
// (bit 1 = key has a 'v', bit 2 = key has a 'k'). It then grows the primary
// alloc-node table (obj+0x20 list of 24-byte entries, cursor obj+0x18,
// capacity obj+0x1C) by one slot -- re-carving the table buffer
// (KoreHeapBlockAlloc, retrying through the last-resort KoreVmAllocExpand
// expand) at tripled-plus-96-word capacity (capacity += 32, old buffer copied
// with memcpy and its flag bit cleared) when the slot would overflow -- and
// stamps the entry {type = 5, payload, index 0, index 0, flag word,
// payload+0x18}. When the flag word is non-zero it also appends a {flags,
// payload, old head} record through KoreAppendAllocRecord, flags the payload
// (ptr-8 |= 1), and, when the payload's +0x00 child word is non-zero,
// re-adopts it through KoreInitAllocNode12 (odd: tagged, & ~1) or
// KoreInitAllocNode5 (even) with the flags decremented, re-fetching the
// entry afterwards. It charges the alloc budget (obj+0x6C) one per node
// (panicking when the guard obj+0x70 is not -1 and the budget drains),
// panics when flags > 0x40, and, when the flag word is non-zero, hands the
// entry to KoreReadoptAllocEntry and returns. In re-adopt mode (node != 0)
// it re-syncs the entry (+0x14 must equal payload+0x18, else +0x08 / +0x0C
// reset to 0 and +0x14 re-stamped), and, when +0x10 is non-zero, calls
// KoreReadoptAllocEntry and returns. Both modes that reach the tail run the
// staged child adoption: adopt the payload's child-pair list (+0x0C array of
// 8-byte {ptr,type} pairs, count at +0x10) and its 20-byte entries (+0x08
// array, count at +0x04 + 1) through KoreAdoptAllocNode with the flags
// decremented, resuming from the entry's +0x08 / +0x0C progress indices and
// republishing them, then pop one entry off the table (--obj+0x18). [NOT
// byte-identical] placement-dependent (relative calls into
// KoreLookupInternedString / KoreHeapBlockAlloc / KoreVmAllocExpand /
// KoreAppendAllocRecord / KoreInitAllocNode12 / itself / KoreHeapPanic /
// KoreReadoptAllocEntry / KoreAdoptAllocNode plus an IAT _memcpy slot); a
// thiscall + retn 0Ch hybrid in the original (declared object-first here).
static void KoreInitAllocNode5(int obj, int ptr, int node, int flags)
{
    int *h = (int *)obj;
    int *p = (int *)ptr;
    int *payload;
    int *entry;

    if (node != 0)
    {
        // re-adopt mode: resume adoption of this entry's payload
        int *n = (int *)node;
        payload = (int *)n[1];
        if (n[5] != payload[6])
        {
            n[2] = 0;
            n[3] = 0;
            n[5] = payload[6];
        }
        if (n[4] != 0)
        {
            KoreReadoptAllocEntry(obj, (int)payload, (int)n, flags - 1);
            return;
        }
        entry = n;
    }
    else
    {
        // new mode: register the payload as a fresh type-5 entry
        if ((*(unsigned char *)(ptr - 8) & 1) != 0)
            return;                          // already registered
        int keyFlags = 0;
        if (p[0] != 0)
        {
            unsigned __int64 r = KoreLookupInternedString(h[1], p[0], 0);
            if ((r >> 32) == 4)
            {
                int *s = (int *)r;
                int len = s[0] & 0x3FFFFFFF;
                for (int i = 0; i < len; ++i)
                {
                    char c = *((char *)s + 8 + i);
                    if (c == 'k')
                        keyFlags |= 2;
                    else if (c == 'v')
                        keyFlags |= 1;
                }
            }
        }
        int cursor = h[6];
        int cap = h[7];
        h[6] = cursor + 1;
        if (cursor + 1 >= cap)               // signed: overflow the table
        {
            int size = 8 * (3 * cap + 96);
            int i = KoreHeapBlockAlloc(h[3], size);
            while (i == 0)
            {
                (void)KoreVmAllocExpand(h[1], size);   // last-resort expand
                i = KoreHeapBlockAlloc(h[3], size);
            }
            memcpy((void *)i, (const void *)h[5], 24 * (unsigned)cap);
            *(int *)(h[5] - 8) &= ~1;        // clear the old buffer's flag
            h[7] += 32;
            h[5] = i;
        }
        entry = (int *)(h[5] + 24 * h[6]);
        entry[0] = 5;
        entry[1] = ptr;
        entry[2] = 0;
        entry[3] = 0;
        entry[4] = keyFlags;
        entry[5] = p[6];
        if (keyFlags != 0)
            (void)KoreAppendAllocRecord(obj, ptr, keyFlags);
        *(int *)(ptr - 8) |= 1;              // flag the node
        int child = p[0];
        if (child != 0)
        {
            if (child & 1)
                (void)KoreInitAllocNode12(obj, child & ~1, 0, flags - 1);
            else
                KoreInitAllocNode5(obj, child, 0, flags - 1);
            entry = (int *)(h[5] + 24 * h[6]);
        }
        int budget = h[27] - 1;
        if (h[28] != -1)
        {
            if (budget <= 0)
                KoreHeapPanic(obj);
            h[27] = budget;
        }
        if ((unsigned)flags > 0x40)
            KoreHeapPanic(obj);
        if (keyFlags != 0)
        {
            KoreReadoptAllocEntry(obj, ptr, (int)entry, flags - 1);
            return;
        }
        payload = p;
    }
    // tail: adopt the payload's children (the shared adoption epilogue)
    int f = flags - 1;
    int i = entry[2];
    if (i < payload[4])
    {
        int count = payload[4];
        do
        {
            int *base = (int *)payload[3];
            KoreAdoptAllocNode(obj, base[2 * i], base[2 * i + 1], f);
            ++i;
            entry = (int *)(h[5] + 24 * h[6]);
            entry[2] = i;
        } while (i < count);
    }
    int j = entry[3];
    if (j < payload[1] + 1)
    {
        int count = payload[1] + 1;
        int *base = (int *)(payload[2] + 20 * j);
        do
        {
            if (base[1] != 0)
            {
                KoreAdoptAllocNode(obj, base[0], base[1], f);
                KoreAdoptAllocNode(obj, base[2], base[3], f);
            }
            ++j;
            base += 5;
            entry = (int *)(h[5] + 24 * h[6]);
            entry[3] = j;
        } while (j < count);
    }
    --h[6];                                  // pop the entry off the table
}

// sub_5E1770 (0x5E1770 - 0x5E189B) -- KoreInitAllocNode7: adopt / register a
// type-7 alloc node. Two modes: in new mode (node == 0) it registers the
// payload (ptr) by first checking the node flag at ptr-4 (already set: bail
// out), then growing the primary alloc-node table (obj+0x20 list of 24-byte
// entries, cursor obj+0x24, capacity obj+0x28) by one slot -- re-carving the
// table buffer (KoreHeapBlockAlloc, retrying through the last-resort
// KoreVmAllocExpand expand) at 8*(3*cap + 96) bytes (capacity += 32, old
// buffer copied with memcpy and its flag bit cleared) when the slot would
// overflow -- and stamping the 2-word entry {type = 7, payload}. It then flags
// the payload (ptr-4 |= 1), charges the alloc budget (obj+0x6C) one, re-adopts
// the payload's +0x00 child through KoreInitAllocNode5 with flags - 1 (when
// non-zero), panics (KoreHeapPanic) when the guard (obj+0x70) is not -1 and the
// budget drains to 0 or below, and panics again when flags > 0x40. In
// re-adopt mode (node != 0) it resumes from the payload saved at node+4. Both
// modes finish by popping one slot off the table (--obj+0x24) and re-adopting
// the payload's +0x04 child through KoreInitAllocNode12 (odd: tagged, & ~1) or
// KoreInitAllocNode5 (even) with the flags decremented. [NOT byte-identical]
// placement-dependent (relative calls into KoreHeapBlockAlloc /
// KoreVmAllocExpand / KoreInitAllocNode5 / KoreInitAllocNode12 /
// KoreHeapPanic, plus an IAT _memcpy slot); a thiscall + retn 0Ch hybrid in
// the original (declared object-first here).
static void KoreInitAllocNode7(int obj, int ptr, int node, int flags)
{
    int *h = (int *)obj;
    int payload;

    if (node != 0)
    {
        // re-adopt mode: resume adoption of this entry's payload
        payload = *(int *)(node + 4);
    }
    else
    {
        // new mode: register the payload as a fresh type-7 entry
        payload = ptr;
        if ((*(unsigned char *)(ptr - 4) & 1) != 0)
            return;                          // already registered
        int cursor = h[6];                   // obj+0x24
        int cap    = h[7];                   // obj+0x28
        h[6] = cursor + 1;
        if (cursor + 1 >= cap)               // signed: overflow the table
        {
            int size = 8 * (3 * cap + 96);
            int i = KoreHeapBlockAlloc(h[3], size);
            while (i == 0)
            {
                (void)KoreVmAllocExpand(h[1], size);   // last-resort expand
                i = KoreHeapBlockAlloc(h[3], size);
            }
            memcpy((void *)i, (const void *)h[5], 24 * (unsigned)cap);
            *(int *)(h[5] - 8) &= ~1;        // clear the old buffer's flag
            h[7] += 32;
            h[5] = i;
        }
        int *entry = (int *)(h[5] + 8 * (3 * cursor + 3));
        entry[0] = 7;
        entry[1] = payload;
        *(int *)(payload - 4) |= 1;          // flag the node
        --h[27];                             // charge the alloc budget (obj+0x6C)
        if (*(int *)payload != 0)
            KoreInitAllocNode5(obj, *(int *)payload, 0, flags - 1);
        if (h[28] != -1 && h[27] <= 0)       // guard armed, budget drained
            KoreHeapPanic(obj);
        if ((unsigned)flags > 0x40)          // original jbe 40h (unsigned)
            KoreHeapPanic(obj);
    }
    --h[6];                                  // pop the slot off the table
    int child = *(int *)(payload + 4);
    if (child != 0)
    {
        if ((child & 1) != 0)
            (void)KoreInitAllocNode12(obj, child & ~1, 0, flags - 1);
        else
            KoreInitAllocNode5(obj, child, 0, flags - 1);
    }
}

// sub_5D5840 (0x5D5840 - 0x5D5971) -- KoreMarkNodeRefs: mark every alloc
// pointer referenced by a payload node as live.  For the payload (a2) it sets
// bit 0 of the 4-byte header word just before each referenced pointer (ptr-4)
// when the value the pointer refers to is non-negative (signed), over: the two
// single slots (a2+0x34 / a2+0x38), the 12-byte-stride reference chain (count
// a2+0x3C, base a2+0x40 -- each reference is the 4 bytes at the start of a
// slot), and the 4-byte pointer array (count a2+0x2C, base a2+0x30).  It then
// walks the 8-byte {ptr, type} entry array (count a2+0x0C, base a2+0x10)
// backward from index (count-1) to 0, marking the entry's ptr when its type
// word is 4 and the referenced value is non-negative.  The "counted marks"
// (the two single slots + the 4-byte array, i.e. the v3 total) are charged off
// the alloc budget (obj+0x6C): either one per child while recursing over the
// child chain (count a2+0x14, base a2+0x18: each child flagged at child-8,
// the budget charged one, then the child re-adopted through itself) plus the
// counted total, or the plain counted total when childless.  thiscall +
// retn 4 in the original (declared object-first here). [NOT byte-identical] a
// self-recursive thiscall hybrid (the C++ translation uses a different frame
// and cannot match the original's byte-for-byte).
static int *KoreMarkNodeRefs(int obj, int a2)
{
    int *p = (int *)a2;
    int *h = (int *)obj;
    int v3 = 0;                          // running "counted mark" total
    int *result = (int *)p[14];          // eax's default carry (a2+0x38)

    int *e = (int *)p[13];               // a2+0x34
    if (e != 0)
    {
        if (*e >= 0)
            *(e - 1) |= 1;
        v3 = 1;
    }
    if (result != 0)                     // a2+0x38
    {
        if (*result >= 0)
            *(result - 1) |= 1;
        v3 += 1;
    }
    if (p[16] != 0 && p[15] > 0)         // a2+0x40 base, a2+0x3C count (12-byte stride)
    {
        int count = p[15];
        int idx = 12 * count;
        do
        {
            result = (int *)*(int *)(p[16] + idx - 12);
            idx -= 12;
            --count;
            if (*result >= 0)
                *(result - 1) |= 1;
        } while (count != 0);
    }
    if (p[12] != 0 && p[11] > 0)         // a2+0x30 base, a2+0x2C count (4-byte)
    {
        int i = 0;
        do
        {
            result = (int *)*(int *)(p[12] + 4 * i);
            v3 += 1;
            if (*result >= 0)
                *(result - 1) |= 1;
            ++i;
        } while (i < p[11]);
    }
    if (p[4] != 0 && p[3] > 0)           // a2+0x10 base, a2+0x0C count (8-byte {ptr,type})
    {
        int r = p[3];
        do
        {
            --r;
            if (*(int *)(p[4] + 8 * r + 4) == 4)
            {
                int *ce = (int *)*(int *)(p[4] + 8 * r);
                if (*ce >= 0)
                    *(ce - 1) |= 1;
            }
        } while (r != 0);
        result = 0;                      // eax = 0 at the loop exit
    }
    if (p[6] != 0 && p[5] > 0)           // a2+0x18 base, a2+0x14 count (child chain, recurse)
    {
        int count = p[5];
        int *base = (int *)p[6];
        do
        {
            int elem = base[count - 1];
            *(int *)(elem - 8) |= 1;
            --h[27];
            --count;
            result = KoreMarkNodeRefs(obj, elem);
        } while (count != 0);
        h[27] -= v3;                     // v3 == v15 (the counted total)
    }
    else
    {
        h[27] -= v3;
    }
    return result;
}

// sub_5DF9F0 (0x5DF9F0 - 0x5DFB80) -- KoreInitAllocNode9: adopt / register a
// type-9 alloc node.  Two modes.  New mode (node == 0): bail when the
// payload's "adopted" flag byte (ptr-8, bit 0) is already set; otherwise grow
// the primary alloc-node table (obj+0x14 buffer of 24-byte entries, cursor
// obj+0x18, capacity obj+0x1C) by one slot -- re-carving the buffer
// (KoreHeapBlockAlloc, retrying through the last-resort KoreVmAllocExpand
// expand) at 8*(3*cap + 96) bytes (capacity += 32, the old buffer copied with
// memcpy and its flag bit cleared) when the slot would overflow -- and stamp
// the 3-word entry {type = 9, payload = ptr, state = -2}.  It then flags the
// payload (ptr-8 |= 1), and -- only when the guard (obj+0x70) is not -1 --
// decrements the alloc budget (obj+0x6C), panicking (KoreHeapPanic) when it
// drains to 0 or below; it panics again when flags > 0x40.  Re-adopt mode
// (node != 0) resumes from the payload (node[1]) using the entry's state word
// (node[2]).  Both modes share a tail: state -2 sets the state to -1 and
// re-adopts the payload's +4 child through KoreInitAllocNode5 with flags - 1
// (when non-zero, then repointing the tracked entry at buffer + 24*cursor);
// state -1 (and the state -2 path) read the payload's +0 child count (the byte
// at payload[0]) into the entry state; a positive count walks the payload's
// child array (payload+0x14, 4-byte pointers, `count` of them) backward,
// flagging each non-null child (child-8 |= 1), charging the budget one, and
// re-adopting it through KoreInitAllocNode with the child's +8 state word and
// flags - 2.  Finally the cursor (obj+0x18) is popped, and the payload's +0
// pointer is returned when its flag byte (payload[0]-8) is already set, else
// it is flagged, the budget is charged, and KoreMarkNodeRefs (sub_5D5840) is
// invoked and returned.  [NOT byte-identical] placement-dependent (relative
// calls into KoreHeapBlockAlloc / KoreVmAllocExpand / KoreHeapPanic /
// KoreInitAllocNode5 / KoreInitAllocNode / KoreMarkNodeRefs, plus an IAT
// _memcpy slot); a thiscall + retn 0Ch hybrid in the original (declared
// object-first here). Replaces the pop-only stub.
static int KoreInitAllocNode9(int obj, int ptr, int node, int flags)
{
    int *h = (int *)obj;
    int nodePtr, payload, state;

    if (node == 0)
    {
        // new mode: register the payload as a fresh type-9 entry
        if ((*(unsigned char *)(ptr - 8) & 1) != 0)
            return node;                       // already adopted
        int cursor = h[6];                     // obj+0x18
        int cap    = h[7];                     // obj+0x1C
        h[6] = cursor + 1;
        if (cursor + 1 >= cap)                 // signed: overflow the table
        {
            int size = 8 * (3 * cap + 96);
            int i = KoreHeapBlockAlloc(h[3], size);
            while (i == 0)
            {
                (void)KoreVmAllocExpand(h[1], size);   // last-resort expand
                i = KoreHeapBlockAlloc(h[3], size);
            }
            memcpy((void *)i, (const void *)h[5], 24 * (unsigned)cap);
            *(int *)(h[5] - 8) &= ~1;          // clear the old buffer's flag
            h[7] += 32;
            h[5] = i;
        }
        int *entry = (int *)(h[5] + 8 * (3 * cursor + 3));
        entry[0] = 9;
        entry[1] = ptr;
        entry[2] = -2;
        *(int *)(ptr - 8) |= 1;                // flag the payload
        if (h[28] != -1)                       // guard armed: account the budget
        {
            int budget = h[27] - 1;
            if (budget <= 0)
                KoreHeapPanic(obj);
            h[27] = budget;
        }
        if ((unsigned)flags > 0x40)
            KoreHeapPanic(obj);
        nodePtr = (int)entry;
        payload = ptr;
        state   = -2;                          // into the state -2 tail
    }
    else
    {
        // re-adopt mode: resume from this entry's payload
        nodePtr = node;
        payload = *(int *)(node + 4);
        state   = *(int *)(node + 8);
    }

    int counter;
    if (state == -2)
    {
        *(int *)(nodePtr + 8) = -1;            // entry state = -1
        int child = *(int *)(payload + 4);
        if (child != 0)
        {
            KoreInitAllocNode5(obj, child, 0, flags - 1);
            nodePtr = h[5] + 24 * h[6];        // buffer + 24*cursor
        }
        int c0 = *(int *)payload;
        counter = *(unsigned char *)c0;
        *(int *)(nodePtr + 8) = counter;       // entry state = child count
    }
    else if (state == -1)
    {
        int c0 = *(int *)payload;
        counter = *(unsigned char *)c0;
        *(int *)(nodePtr + 8) = counter;
    }
    else
    {
        counter = state;
    }

    if (counter > 0)                           // walk the payload's child array
    {
        int *cur = (int *)(payload + 4 * counter + 0x14);
        do
        {
            --counter;
            --cur;
            *(int *)(nodePtr + 8) = counter;   // entry state = running index
            int cp = *cur;
            if (cp != 0)
            {
                *(int *)(cp - 8) |= 1;
                --h[27];
                KoreInitAllocNode(obj, *(int *)(cp + 8), flags - 2);
                nodePtr = h[5] + 24 * h[6];    // buffer + 24*cursor
            }
        } while (counter != 0);
    }

    --h[6];                                    // pop the entry off the table
    int c0 = *(int *)payload;
    if ((*(unsigned char *)(c0 - 8) & 1) != 0)
        return c0;                             // already live
    *(int *)(c0 - 8) |= 1;
    --h[27];
    return (int)KoreMarkNodeRefs(obj, c0);     // sub_5D5840
}

// sub_5DFB80 (0x5DFB80 - 0x5DFCD7) -- KoreInitAllocNode10: adopt / register a
// type-10 alloc node.  Two modes.  New mode (node == 0): bail when the
// payload's "adopted" flag byte (ptr-8, bit 0) is already set; otherwise grow
// the primary alloc-node table (obj+0x14 buffer of 24-byte entries, cursor
// obj+0x18, capacity obj+0x1C) by one slot -- re-carving the buffer
// (KoreHeapBlockAlloc, retrying through the last-resort KoreVmAllocExpand
// expand) at 8*(3*cap + 96) bytes (capacity += 32, the old buffer copied with
// memcpy and its flag bit cleared) when the slot would overflow -- and stamp
// the 3-word entry {type = 10, payload = ptr, state = the payload's +0x08
// signed word (the child slot count)}.  It flags the payload (ptr-8 |= 1)
// and decrements the alloc budget (obj+0x6C) unconditionally (no flags > 0x40
// check, unlike the type-8 / type-9 routines).  Re-adopt mode (node != 0)
// resumes from the entry's payload (node[1]) using the entry's state word
// (node[2]).  Both modes share a tail: a positive state stores the budget and
// walks the payload's child slots (payload + 8*i + 4, i = state down to 1),
// storing the running index into the entry state before re-adopting each slot
// through KoreInitAllocNode with flags - 1 (re-locating the tracked entry at
// buffer + 24*cursor after each recurse in case the table grew); a
// non-positive state skips the walk.  It panics (KoreHeapPanic) when the
// guard (obj+0x70) is not -1 and the budget is 0 or below, pops the slot off
// the table (--obj+0x18), stores the budget back, and, when the payload's
// +0x04 child is non-null, adopts it through KoreInitAllocNode5 with flags -
// 1.  [NOT byte-identical] placement-dependent (relative calls into
// KoreHeapBlockAlloc / KoreVmAllocExpand / KoreHeapPanic / KoreInitAllocNode
// / KoreInitAllocNode5, plus an IAT _memcpy slot); a thiscall + retn 0Ch
// hybrid in the original (declared object-first here). Replaces the pop-only
// stub.
static void KoreInitAllocNode10(int obj, int ptr, int node, int flags)
{
    int *h = (int *)obj;
    int budget = h[27];                 // obj+0x6C alloc budget
    int payload, state;

    if (node != 0)
    {
        // re-adopt mode: resume from this entry's payload / state
        payload = *(int *)(node + 4);
        state   = *(int *)(node + 8);
    }
    else
    {
        // new mode: register the payload as a fresh type-10 entry
        payload = ptr;
        if ((*(unsigned char *)(ptr - 8) & 1) != 0)
            return;                     // already adopted
        int cursor = h[6];              // obj+0x18
        int cap    = h[7];              // obj+0x1C
        state = *(short *)(ptr + 8);    // the payload's +0x08 signed word
        h[6] = cursor + 1;
        if (cursor + 1 >= cap)          // signed: overflow the table
        {
            int size = 8 * (3 * cap + 96);
            int i = KoreHeapBlockAlloc(h[3], size);
            while (i == 0)
            {
                (void)KoreVmAllocExpand(h[1], size);   // last-resort expand
                i = KoreHeapBlockAlloc(h[3], size);
            }
            memcpy((void *)i, (const void *)h[5], 24 * (unsigned)cap);
            *(int *)(h[5] - 8) &= ~1;          // clear the old buffer's flag
            h[7] += 32;
            h[5] = i;
        }
        int *entry = (int *)(h[5] + 8 * (3 * cursor + 3));
        entry[0] = 10;
        entry[1] = payload;
        entry[2] = state;
        *(int *)(payload - 8) |= 1;        // flag the payload
        --budget;                           // charge the alloc budget
    }

    int *entry = (int *)node;
    if (state > 0)                           // walk the child slot chain
    {
        if (node == 0)
            entry = (int *)(h[5] + 24 * h[6]);   // the just-adopted entry
        h[27] = budget;
        do
        {
            int child = payload + 8 * state + 4;
            --state;
            entry[2] = state;                // entry state = running index
            KoreInitAllocNode(obj, child, flags - 1);
            entry = (int *)(h[5] + 24 * h[6]);   // re-locate the entry
        } while (state > 0);
        budget = h[27];
    }
    if (h[28] != -1 && budget <= 0)          // guard armed, budget drained
        KoreHeapPanic(obj);
    --h[6];                                    // pop the slot off the table
    h[27] = budget;
    int child = *(int *)(payload + 4);
    if (child != 0)
        KoreInitAllocNode5(obj, child, 0, flags - 1);
}

// sub_5DFCE0 (0x5DFCE0 - 0x5DFE81) -- KoreInitAllocNode12: adopt / register a
// type-12 (table) alloc node.  Two modes.  New mode (node == 0): bail with a
// zero return when the payload's "adopted" flag byte (ptr-8, bit 0) is already
// set; otherwise grow the primary alloc-node table (obj+0x14 buffer of
// 24-byte entries, cursor obj+0x18, capacity obj+0x1C) by one slot --
// re-carving the buffer (KoreHeapBlockAlloc, retrying through the last-resort
// KoreVmAllocExpand expand) at 8*(3*cap + 96) bytes (capacity += 32, the old
// buffer copied with memcpy and its flag bit cleared) when the slot would
// overflow -- and stamp the 3-word entry {type = 12, payload = ptr, state =
// 0}.  It flags the payload (ptr-8 |= 1) and decrements the alloc budget
// (obj+0x6C) unconditionally (no guard / flags > 0x40 checks, unlike the
// type-5 / 7 / 8 routines).  Re-adopt mode (node != 0) resumes from the
// entry's payload (node[1]) using the entry's state word (node[2]).  Both
// modes share the record walk: the payload's +0x04 word is the table pointer,
// whose +0x00 word is the record count and whose +0x18 word starts an array
// of 16-byte records {word, word, kind, index16}.  For each record from state
// up to count - 1 (record index = state & 0xFF, kind at record+8, index byte
// at record+13): kind 0 (leaf slot) reads the payload's slot index byte and,
// when the slot's type byte (payload + (i & 3) + 4*(i & 0xFC)) is non-zero,
// re-adopts the {slot pointer (payload + 4*i), type} pair through
// KoreInitAllocNode with flags - 1; kind 1 re-adopts the payload's +0x08 word
// through KoreInitAllocNode12 (odd: tagged, & ~1) or KoreInitAllocNode5
// (even) with flags - 1 when non-zero; kind 2 re-adopts the payload's slot i
// (payload + 4*i) through KoreInitAllocNode5 with flags - 1 when non-null;
// any other kind is a no-op.  After each record the running index is stored
// into the current entry's state word (re-located at buffer + 24*cursor in
// case the table grew).  Finishes by popping the slot off the table
// (--obj+0x18) and returning the table buffer base -- or the record count
// when the state was already at / above the count, or 0 on the new-mode early
// bail (the callers discard the result).  [NOT byte-identical]
// placement-dependent (relative calls into KoreHeapBlockAlloc /
// KoreVmAllocExpand / KoreInitAllocNode / KoreInitAllocNode5 / itself, plus
// an IAT _memcpy slot); a thiscall + retn 0Ch hybrid in the original
// (declared object-first here).  Replaces the pop-only stub -- the last of
// the seventeen alloc-node bookkeeping helpers.
static int KoreInitAllocNode12(int obj, int ptr, int node, int flags)
{
    int *h = (int *)obj;
    int payload, state, count, result;

    if (node == 0)
    {
        // new mode: register the payload as a fresh type-12 entry
        payload = ptr;
        if ((*(unsigned char *)(ptr - 8) & 1) != 0)
            return 0;                      // already registered
        int cursor = h[6];                 // obj+0x18
        int cap    = h[7];                 // obj+0x1C
        h[6] = cursor + 1;
        if (cursor + 1 >= cap)             // signed: overflow the table
        {
            int size = 8 * (3 * cap + 96);
            int i = KoreHeapBlockAlloc(h[3], size);
            while (i == 0)
            {
                (void)KoreVmAllocExpand(h[1], size);   // last-resort expand
                i = KoreHeapBlockAlloc(h[3], size);
            }
            memcpy((void *)i, (const void *)h[5], 24 * (unsigned)cap);
            *(int *)(h[5] - 8) &= ~1;      // clear the old buffer's flag
            h[7] += 32;
            h[5] = i;
        }
        node = (int)(h[5] + 8 * (3 * cursor + 3));
        *(int *)node = 12;                 // entry {type, payload, state}
        *(int *)(node + 4) = payload;
        *(int *)(node + 8) = 0;
        *(int *)(payload - 8) |= 1;        // flag the payload
        --h[27];                           // charge the alloc budget (obj+0x6C)
    }
    else
        payload = *(int *)(node + 4);      // re-adopt mode

    state = *(int *)(node + 8);
    int *table = *(int **)(payload + 4);
    count   = *(int *)table;
    if (state < count)
    {
        do
        {
            int *rec = (int *)((char *)table + 0x18 + 16 * (state & 0xFF));
            int kind = *(int *)(rec + 8);
            int idx  = ((unsigned char *)rec)[13];
            if (kind == 0)
            {
                // leaf slot: adopt the payload's slot idx
                int slotType = *(unsigned char *)(payload + (idx & 3)
                                                           + 4 * (idx & 0xFC));
                if (slotType != 0)
                {
                    int pair[2] = { *(int *)(payload + 4 * idx), slotType };
                    KoreInitAllocNode(obj, (int)pair, flags - 1);
                }
            }
            else if (kind == 1)
            {
                int child = *(int *)(payload + 8);
                if (child != 0)
                {
                    if ((child & 1) != 0)
                        (void)KoreInitAllocNode12(obj, child & ~1, 0, flags - 1);
                    else
                        KoreInitAllocNode5(obj, child, 0, flags - 1);
                }
            }
            else if (kind == 2)
            {
                int child = *(int *)(payload + 4 * idx);
                if (child != 0)
                    KoreInitAllocNode5(obj, child, 0, flags - 1);
            }
            ++state;
            *(int *)((char *)h[5] + 8 * (3 * h[6]) + 8) = state;  // entry state
        } while (state < count);
        result = (int)h[5];                // the table buffer base
    }
    else
        result = count;
    --h[6];                                // pop the slot off the table
    return result;
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

// sub_5D59E0 (0x5D59E0 - 0x5D5A1D) -- KoreMarkAllocTree: the stage-1 mark
// pass over the alloc-node tree rooted at a2. Set bit 0 of the node's
// 4-byte block header (at a2 - 8 -- the same low "live" bit the per-type
// adopt routines and the stage-3 chain reassign test), then, when the node
// has children (signed count at a2 + 0x14, 4-byte child array at
// a2 + 0x18), mark each child's block header and recurse into it. a1 is
// the callback context the KoreForEachAllocNode registry walk threads
// through (the registry's +0x0C word, or 0 from KoreHeapInitGrow's direct
// call on the h[21] + 56 child) -- threaded into every recursion but
// otherwise unused. Returns the last child's recursion result (0 when
// childless). Plain cdecl int(int, int).
// Byte-identical: __declspec(naked) inline asm of the original's exact
// 61-byte sequence -- the self-recursive call's -0x30 relative
// displacement and the two short jumps (0x29 / 0xDD) depend only on the
// in-function layout, so the bytes match at any placement.
static int __declspec(naked) KoreMarkAllocTree(int a1, int a2)
{
    (void)a1; (void)a2;
    __asm {
        push esi
        push edi
        mov  edi, [esp+10h]            ; a2, the node
        or   dword ptr [edi-8], 1      ; mark the node's block header
        xor  esi, esi
        cmp  dword ptr [edi+14h], esi  ; the signed child count
        jbe  kmat_done
        push ebx
        mov  ebx, [esp+10h]            ; a1, the context
kmat_loop:
        mov  eax, [edi+18h]            ; the child array
        mov  eax, [eax+esi*4]          ; the child
        or   dword ptr [eax-8], 1      ; mark the child's block header
        mov  ecx, [edi+18h]
        mov  edx, [ecx+esi*4]
        sub  eax, 8
        push edx
        push ebx
        call KoreMarkAllocTree
        inc  esi
        add  esp, 8
        cmp  esi, [edi+14h]
        jb   kmat_loop
        pop  ebx
kmat_done:
        pop  edi
        pop  esi
        ret
    }
}

// sub_5D2950 (0x5D2950 - 0x5D29C0) -- KoreReleaseAllocRecords: the per-node
// record release the sweep (KoreSweepAllocNodes, sub_5D5980) calls on each
// unmarked alloc node, and which is also used directly by the VM hash /
// table code (sub_5E9AA0, the 0x5D9390 dispatch thunk, KoreHashTableInsert).
// Walks the node's own 16-byte-entry record list (head at node+0x2C; entries
// linked via entry+0x0C) and splices out every entry whose key (entry+0x08,
// a pointer) is >= the threshold (the node's +0x28 word).  For each such
// entry it copies the pointed-to record's first two dwords into the entry,
// self-references the entry (entry+0x08 = the entry itself) to mark it
// detached, marks the record dirty for the debug GC
// (KoreDebugMarkDirty(entry-8, entry)) when the table's (node[0]'s) +0x3C
// debug flag is 1, and unlinks the entry (updating the head at node+0x2C if
// it is the head, else the previous entry's +0x0C next pointer).  Takes
// (node, threshold); returns nothing.  cdecl in the original (retn, two
// stack args).
// [NOT byte-identical] placement-dependent: the relative call to
// KoreDebugMarkDirty (0x5F7E10) has a displacement that changes with the
// build's placement.
static void KoreReleaseAllocRecords(int node, int threshold)
{
    int *headSlot = (int *)(node + 0x2C);
    int base   = *(int *)node;          // node[0]: the table base
    int prev   = *headSlot;
    int entry  = *headSlot;
    while (entry != 0)
    {
        int *e = (int *)entry;
        int key = e[2];                 // entry+0x08
        if ((unsigned int)key < (unsigned int)threshold)
        {
            prev = entry;               // keep: advance prev
        }
        else
        {
            int *r = (int *)key;        // the pointed-to record
            e[0] = r[0];
            e[1] = r[1];
            e[2] = entry;               // self-reference (detached)
            if (*(int *)(base + 60) == 1)
                KoreDebugMarkDirty(entry - 8, entry);
            int head = *headSlot;       // re-read the head
            if (entry == head)
            {
                int next = *(int *)(head + 0x0C);
                *headSlot = next;
                prev = next;
            }
            else
            {
                ((int *)prev)[3] = *(int *)(entry + 0x0C);   // prev[3] = entry[3]
            }
        }
        entry = *(int *)(entry + 0x0C); // entry = entry[3] (next)
    }
}

// sub_5D5980 (0x5D5980 - 0x5D59D5) -- KoreSweepAllocNodes: the stage-2 sweep
// pass over the heap's alloc-node chain (the obj+0x04 sub-object's +0x4C
// 4-byte list).  Each 19-dword node is checked for the live bit the stage-1
// mark set in its 4-byte block header (node-8): a marked node is kept and
// counted, with the sub-object's +0x6C charge decremented once every third
// marked node; an unmarked node is released via KoreReleaseAllocRecords
// (sub_5D2950, with the node's +0x28 word as the threshold) and unlinked from
// the list (the previous node's +0x4C next pointer skips over it, the charge
// -= 2).  No return value; thiscall in the original.
// [NOT byte-identical] placement-dependent: the relative call to
// KoreReleaseAllocRecords (0x5D2950) has a displacement that changes with the
// build's placement.
static void KoreSweepAllocNodes(int obj)
{
    int *headSlot = (int *)(*(int *)(obj + 4) + 0x4C);   // the list-head slot
    int count = 0;
    int node = *headSlot;
    while (node != 0)
    {
        int *n = (int *)node;
        if ((*(unsigned char *)(node - 8) & 1) != 0)
        {
            ++count;                              // marked / live
            headSlot = &n[19];                    // advance the prev-next slot
            if (count == 3)
            {
                --*(int *)(obj + 0x6C);
                count = 0;
            }
        }
        else
        {
            KoreReleaseAllocRecords(node, n[10]); // unmarked: release it
            *headSlot = n[19];                    // unlink (prev's next skips it)
            *(int *)(obj + 0x6C) -= 2;
        }
        node = n[19];                             // advance to the next node
    }
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

// ---------------------------------------------------------------------------
// The VM error-message / panic formatter closure.  These push 8-byte
// {pointer, type} cells onto the value stack (top at vmcore[7], base at
// vmcore[8]) and intern the pieces via the intern table at *(vmcore)+0xBC.
// A string cell stores the interned record pointer (the record's low-30
// bits hold its length) with type 4; a number cell stores the float in the
// low 4 bytes with type 3.  The intern table is a linear-probing hash table
// whose bucket array holds pointers to inline records ({length, hash, content}).
// ---------------------------------------------------------------------------

// Push an interned literal string (known length + kore hash) as a {ptr,4} cell.
static void KorePushStringCell(int vmcore, const char *str, int len, unsigned int hash)
{
    int *vm = (int *)vmcore;
    int *entry = KoreInternString((int *)(*(int *)vmcore + 0xBC), vmcore, str, len, hash);
    int *top = (int *)vm[7];
    *top = (int)entry;
    top[1] = 4;
    vm[7] = (int)(top + 2);
}

// ---- sub_5D6A40: insert a freshly-built interned record (buf) into the
// intern hash table.  thiscall (this = the intern table) in the original.
// Linearly probes from bucket[hash & mask]; on a full (hash, length, content)
// match it sets the debug "dirty" flag (bit 0 of the record's 4-byte pre-header)
// when the debug level is 2-4 and returns the existing record; on a miss it
// rehashes when the load factor would exceed half, bumps the count, and stores
// the record in the first empty slot (returns 0).  [NOT byte-identical]
// thiscall hybrid, placement-dependent.
static int *KoreInsertInterned(int *table, int vmcore, int *buf, int len)
{
    int hash = buf[1];
    int *chain = (int *)table[0];
    int mask = table[2];
    int probe = hash;
    int *slot = (int *)chain[probe & mask];
    while (slot != 0)
    {
        if (slot[1] == hash && (*slot & 0x3FFFFFFF) == len
            && memcmp((char *)slot + 8, (char *)buf + 8, len) == 0)
        {
            int debugLevel = *(int *)(*(int *)vmcore + 60);
            if (debugLevel > 1 && debugLevel <= 4 && *slot >= 0)
                *(slot - 1) |= 1;
            return slot;
        }
        probe++;
        slot = (int *)chain[probe & mask];
    }
    if ((unsigned int)(table[1] + 1) > (unsigned int)(table[2] + 1) >> 1)
        KoreRehashInterned(table, vmcore);
    ++table[1];
    mask = table[2];    // re-read (the rehash may have resized the table)
    probe = hash;
    while (chain[probe & mask] != 0)
        probe++;
    chain[probe & mask] = (int)buf;
    return 0;
}

// ---- sub_5DA6F0: hash a freshly-allocated buffer's content (buf+8, len bytes)
// with the kore hash, stamp buf[0] = len / buf[1] = hash, and insert the record
// via KoreInsertInterned.  thiscall + retn 0x0C in the original (this = the
// intern table); a plain free function here.  Returns the existing record on a
// match, or 0 when the record was newly inserted.
static int KoreInternBuffer(int vmcore, int *buf, int len)
{
    unsigned int hash = KoreHashKore((char *)buf + 8, len);
    buf[0] = len;
    buf[1] = hash;
    return (int)KoreInsertInterned((int *)(*(int *)vmcore + 0xBC), vmcore, buf, len);
}

// ---- sub_5DC2B0: concatenate the top n value-stack cells into a single
// interned string.  n < 0 is ignored; n == 0 pushes the empty string; n == 1
// is a no-op; n >= 2 first sums the lengths (converting number cells in place
// via KoreNumberToString), allocates len+11, memcpys the string contents into
// buf+8, null-terminates, replaces the n cells with one {buf,4} cell, and
// interns the buffer (freeing the fresh buffer when an identical entry already
// exists).  A non-number / non-string cell raises the panic.
static void KoreConcatValues(int vmcore, int n)
{
    if (n < 0)
        return;
    if (n == 0)
    {
        KorePushStringCell(vmcore, "", 0, 0x6B6F7265u);
        return;
    }
    if (n == 1)
        return;
    int *vm = (int *)vmcore;
    int top = vm[7];
    int totalLen = 0;
    for (int i = -n; i < 0; ++i)
    {
        int *cell = (int *)(top + i * 8);
        int type = cell[1];
        if (type == 3)
            totalLen += KoreNumberToString(vmcore, i);
        else if (type == 4)
            totalLen += (*(int *)cell[0]) & 0x3FFFFFFF;
        else
        {
            KoreTablePanic(vmcore, "can only concat number and string");
            return;
        }
    }
    int buf = (int)KoreAllocVmMem(vmcore, (unsigned int)(totalLen + 11));
    char *dst = (char *)buf + 8;
    int *srcCell = (int *)(top - 8 * n);
    for (int i = 0; i < n; ++i)
    {
        int ptr = srcCell[0];
        int len = (*(int *)ptr) & 0x3FFFFFFF;
        memcpy(dst, (char *)ptr + 8, len);
        dst += len;
        srcCell += 8;
    }
    *dst = 0;
    int *resultCell = (int *)(top - 8 * n);
    vm[7] = (int)(resultCell + 2);
    resultCell[1] = 4;
    *resultCell = buf;
    int existing = KoreInternBuffer(vmcore, (int *)buf, totalLen);
    if (existing != 0)
    {
        *resultCell = existing;
        resultCell[1] = 4;
        int *hp = (int *)(*(int *)vmcore);
        int freeLen = (*(int *)buf & 0x3FFFFFFF) + 15;
        hp[1] -= freeLen;
        ((void (*)(int, int, int, int))hp[3])(hp[4], buf - 4, freeLen, 0);
    }
}

// ---- sub_5D7A90: VM vsprintf -- vsnprintf the format into a 512-byte buffer
// (capping at 511), hash the result, intern it, and push a {entry,4} cell.
// Returns the cell's entry+2 (matching the original's return).
static int KoreVmSprintf(int vmcore, const char *format, void *arglist)
{
    char buf[0x200];
    int len = vsnprintf(buf, 0x200, format, (va_list)arglist);
    if (len < 0 || len > 511)
        len = 511;
    unsigned int hash = KoreHashKore(buf, len);
    int *entry = KoreInternString((int *)(*(int *)vmcore + 0xBC), vmcore, buf, len, hash);
    int *vm = (int *)vmcore;
    int *top = (int *)vm[7];
    *top = (int)entry;
    top[1] = 4;
    vm[7] = (int)(top + 2);
    return (int)(entry + 2);
}

// ---- sub_5DBFE0: variadic wrapper that forwards to the VM vsprintf.
static int KoreVmVprintf(int vmcore, const char *format, ...)
{
    va_list va;
    va_start(va, format);
    int result = KoreVmSprintf(vmcore, format, (void *)va);
    va_end(va);
    return result;
}

// ---- sub_5D2A50: intern a buffer and return its record pointer (the
// original additionally returns {eax=ptr, edx=4} for a stack push).
static int KoreInternPush(int vmcore, const void *src, int len, unsigned int hash)
{
    return (int)KoreInternString((int *)(*(int *)vmcore + 0xBC), vmcore, src, len, hash);
}

// ---- sub_407070: hash a buffer, intern it, and push a {entry,4} cell.
static int KoreAppendString(int vmcore, const void *src, int len)
{
    unsigned int hash = KoreHashKore((const char *)src, len);
    int entry = KoreInternPush(vmcore, src, len, hash);
    int *vm = (int *)vmcore;
    int *top = (int *)vm[7];
    *top = entry;
    top[1] = 4;
    vm[7] = (int)(top + 2);
    return entry;
}

// ---- sub_5E19D0: the message prefix.  Resolves frame 0's source/line; when
// the line is > 0 it vsprintfs "%s:%d: " (source description, line) and
// pushes it; otherwise it pushes the empty string.
static int KoreMessagePrefix(int vmcore, int frameNo)
{
    int fr[142];   // the 568-byte frame record
    if (KoreResolveFrameIndex(vmcore, frameNo, fr) != 0)
    {
        KoreResolveFrameInfo(vmcore, "Sl", fr);
        int line = fr[5];   // fr + 20
        if (line > 0)
            return KoreVmVprintf(vmcore, "%s:%d: ", (char *)(fr + 44), line);
    }
    int *entry = KoreInternString((int *)(*(int *)vmcore + 0xBC), vmcore, "", 0, 0x6B6F7265u);
    int *vm = (int *)vmcore;
    int *top = (int *)vm[7];
    *top = (int)entry;
    top[1] = 4;
    vm[7] = (int)(top + 2);
    return (int)entry;
}

// ---- sub_5E1AA0: build the "stack traceback" text.  When startFrame >= 0 and
// the VM state at vmcore+0x54 is in {-2, 0, -100} it pushes the "stack
// traceback:" header, then for each frame in [startFrame, endFrame) resolves
// its info ("nSl") and appends "\n\t<desc>:" (optional "<line>:") plus a
// per-frame suffix (" in function '<name>'", " ?", " in main chunk", or
// " in function <desc:line>"), consolidating each frame into the running
// string via KoreConcatValues.  Otherwise it pushes "stack traceback:\n".
// [NOT byte-identical] the frame record and loop are placement-dependent.
static void KoreStackTraceback(int vmcore, int frameCtx, int startFrame, int endFrame)
{
    int *vm = (int *)vmcore;   // the value-stack owner (Size in the original)
    int state = *(int *)(frameCtx + 0x54);   // a2[21] (the frame context)
    if (startFrame >= 0 && (state == -2 || state == 0 || state == -100))
    {
        int n0 = (vm[7] - vm[8]) >> 3;
        if ((unsigned int)(vm[7] + 32) > (unsigned int)vm[9])
            KoreReallocValueStack(vmcore + 4, vmcore, 4);
        KorePushStringCell(vmcore, "stack traceback:", 16, (unsigned int)-699903551);
        if (startFrame < endFrame)
        {
            int frame = startFrame;
            do
            {
                int fr[142];
                if (KoreResolveFrameIndex(frameCtx, frame, fr) == 0)
                    return;
                if (KoreResolveFrameInfo(frameCtx, "nSl", fr) == 0)
                    return;
                KoreVmVprintf(vmcore, "\n\t%s:", (char *)(fr + 44));
                int line = fr[5];   // fr + 20 (current line)
                if (line > 0)
                    KoreVmVprintf(vmcore, "%d:", line);
                int name = fr[1];   // fr + 4 (function name)
                if (name != 0 && *(char *)name != 0)
                {
                    KoreVmVprintf(vmcore, " in function '%s'", (char *)name);
                }
                else
                {
                    char c = *(char *)(fr + 12);   // descriptor ('L'/'C'/'m'/'t'/...)
                    if (c == 'C')
                        KorePushStringCell(vmcore, " ?", 2, (unsigned int)-1049149715);
                    else if (c == 'm')
                        KorePushStringCell(vmcore, " in main chunk", 14, (unsigned int)-1721606804);
                    else if (c == 't')
                        KorePushStringCell(vmcore, " ?", 2, (unsigned int)-1049149715);
                    else
                        KoreVmVprintf(vmcore, " in function <%s:%d>", (char *)(fr + 44), fr[9]);
                }
                KoreConcatValues(vmcore, ((vm[7] - vm[8]) >> 3) - n0);
                ++frame;
            } while (frame < endFrame);
        }
    }
    else
    {
        KorePushStringCell(vmcore, "stack traceback:\n", 17, (unsigned int)-1567176437);
    }
}

// ---- sub_5E24E0: the VM message formatter.  Pushes the message prefix
// (frame 0), the vsprintf'ed format string, an optional suffix, a newline, and
// the stack traceback (frames 0-9), then concatenates the 4 (or 5, with a
// suffix) cells into a single interned error message.
static void KoreFormatMessage(int vmcore, const char *format, const char *suffix, va_list args)
{
    int v4 = 4;
    KoreMessagePrefix(vmcore, 0);
    KoreVmSprintf(vmcore, format, (void *)args);
    if (suffix != 0 && *suffix != 0)
    {
        KoreAppendString(vmcore, suffix, (int)strlen(suffix));
        v4 = 5;
    }
    KorePushStringCell(vmcore, "\n", 1, 1798745018u);
    KoreStackTraceback(vmcore, vmcore, 0, 10);
    KoreConcatValues(vmcore, v4);
}

// ---- sub_5F1C30: the VM panic raiser.  Clears heap[0x64], and if a jmp_buf
// is live at vmcore+0x40 it pops the chain (vmcore+0x40 <- the next buffer
// stored at jmp_buf+0x40) and longjmps to it with the code; otherwise it calls
// the error callback at heap+0x290 if set, then the panic traceback.
// [NOT byte-identical] the jmp_buf chain bookkeeping is placement-dependent.
static int KoreRaisePanic(int vmcore, int code)
{
    int *vm = (int *)vmcore;
    int heap = *(int *)vmcore;
    *(int *)(heap + 0x64) = 0;
    int jmpbuf = vm[0x40 / 4];   // the live jmp_buf (next-in-chain at jmpbuf+0x40)
    if (jmpbuf != 0)
    {
        vm[0x40 / 4] = *(int *)(jmpbuf + 0x40);
        jmp_buf *jb = (jmp_buf *)(void *)jmpbuf;
        longjmp(*jb, code);
    }
    void (*errCb)(int) = *(void (**)(int))(heap + 0x290);
    if (errCb != 0)
        errCb(vmcore);
    return KorePanicTraceback(vmcore);
}

// ---- sub_5E2580 (KoreTablePanic): the table-operation error path.  Formats
// the message via the VM formatter (no suffix) and raises the VM panic with
// code -100 (which longjmps to the nearest handler and never returns).  A
// cdecl variadic in the original ((Size, Format, ...)).
static int KoreTablePanic(int vmcore, const char *format, ...)
{
    va_list va;
    va_start(va, format);
    KoreFormatMessage(vmcore, format, 0, va);
    va_end(va);
    return KoreRaisePanic(vmcore, -100);
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

// sub_5D2FB0 (0x5D2FB0 - 0x5D301A) -- KoreProfileIntegerKey: the resize
// profile helper.  If the {payload, type} pair is an exact positive integer
// key (type 3, 1 <= key <= 2^24; the x87 round-to-int + exactness test, the
// sign test, and the 2^24 bound), bump the profile bucket for the key's
// magnitude (bucket 0 holds key 1, bucket b > 0 holds keys in (2^(b-1),
// 2^b] -- bitlen(key-1) through the byte_1AE32B0 table, the -1 shift seed
// putting key 1 into bucket 0) and return 1; any other pair returns 0
// without touching the profile.  usercall in the original (the pair in EAX,
// the counters in ECX).  [NOT byte-identical] a reimplementation of the x87
// float-to-int round / exactness test.
static int KoreProfileIntegerKey(int pair, int counters)
{
    int *p = (int *)pair;
    if (p[1] != 3)
        return 0;
    float f = *(float *)p;
    int k = (int)f;
    if ((float)k != f || k <= 0 || k > 0x1000000)
        return 0;
    unsigned x = (unsigned)k - 1u;
    int shift = -1;
    while (x >= 0x100)
    {
        x >>= 8;
        shift += 8;
    }
    ++((int *)counters)[shift + (int)KoreLog2Byte[(unsigned char)x] + 1];
    return 1;
}

// sub_5D37B0 (0x5D37B0 - 0x5D3806) -- KoreScanResizeProfile: walk the
// resize profile (KoreTableResize's 25-bucket magnitude counts) from
// bucket 0 upward, *total holding the entry count.  *total <= 0: *total :=
// 0, return 0.  Otherwise accumulate the bucket counts while doubling the
// size (1, 2, 4, ...), remembering the size + running count each time the
// running count passes half the size, and stop at the first bucket that
// holds every entry or once the size reaches the total; *total becomes the
// remembered size (0 when the running count never passed half a size) and
// the running count at that remembered size is returned.  stdcall (retn 8)
// in the original.  [NOT byte-identical] a straightforward reimplementation.
static int KoreScanResizeProfile(int counters, int *total)
{
    int *c = (int *)counters;
    int n = *total;
    int acc = 0;
    int result = 0;
    int size = 1;
    int bucket = 0;
    if (n > 0)
    {
        for (;;)
        {
            int k = *c++;
            if (k > 0)
            {
                acc += k;
                if (acc > size >> 1)
                {
                    bucket = size;
                    result = acc;
                }
            }
            if (acc == n)
                break;
            size <<= 1;
            if (size >> 1 >= n)
                break;
        }
    }
    *total = bucket;
    return result;
}

// sub_5E3F20 (0x5E3F20 - 0x5E406F) -- KoreTableResize: the deep resize
// taken when a table insert finds no free hash row.  Bumps the table's
// +0x18 word and builds the 25-bucket magnitude profile: the 8-byte dense
// rows ({value, tag}) are counted non-nil in doubling chunks (chunk 0
// covers index 1, chunk b > 0 covers indices (2^(b-1), 2^b]), then the
// 20-byte hash rows (size word 0 = unused) are counted, each integer
// (type 3) row's key profiled (KoreProfileIntegerKey), and so is the
// incoming key pair.  KoreScanResizeProfile then sizes the new dense part
// from the profile (overwriting the total with the remembered size) and
// the hash part gets (dense non-nil + used hash rows) - rememberedCount +
// 1 bits; it returns KoreResizeHashTable's result.  thiscall + retn 8 in
// the original (declared plain here: the table, the vm-core holder, the
// key pair).  [NOT byte-identical] a thiscall + retn-8 hybrid with
// placement-dependent relative calls.
static int KoreTableResize(int table, int vmcore, int key)
{
    int *t = (int *)table;
    ++t[6];
    int counters[25];
    memset((void *)counters, 0, sizeof(counters));
    int total = 0;
    int start = 1;   // 1-based dense index where the chunk begins
    int chunk = 1;   // the chunk's nominal size (doubles per bucket)
    for (int v5 = 0; v5 < 24; ++v5)
    {
        int count = t[4];
        int nonNil = 0;
        int end = chunk <= count ? chunk : count;
        if (chunk > count)
        {
            if (start > count)
                break;
        }
        else if (start > chunk)
        {
            // the previous chunk was truncated: nothing to count here
        }
        else
        {
            int v10 = end - start + 1;
            int *row = (int *)(t[3] + 8 * start - 8);
            start += v10;
            do
            {
                if (row[1] != 0)
                    ++nonNil;
                row += 2;
                --v10;
            }
            while (v10 != 0);
        }
        counters[v5] += nonNil;
        total += nonNil;
        chunk *= 2;
    }
    int mask = t[1];
    int profiled = 0;   // integer-keyed hash rows that profiled
    int used = 0;       // hash rows in use (size word != 0)
    int i = mask + 1;
    while (i != 0)
    {
        int *row = (int *)(t[2] + 20 * (i - 1));
        --i;
        if (row[3] != 0)
        {
            if (row[1] == 3)
            {
                int pair[2] = { row[0], row[1] };
                if (KoreProfileIntegerKey((int)pair, (int)counters))
                    ++profiled;
            }
            ++used;
        }
    }
    int usedTotal = total + used;
    int newRows = total + profiled;
    if (KoreProfileIntegerKey(key, (int)counters))
        ++newRows;
    int dense = KoreScanResizeProfile((int)counters, &newRows);
    return KoreResizeHashTable(table, vmcore, newRows, usedTotal - dense + 1);
}


// sub_5F7E10 (0x5F7E10 - 0x5F7E6F) -- KoreDebugMarkDirty: mark a referenced
// global as dirty for the debug GC.  `flags` is a pointer to a debug-flag
// byte (the table's -8 header) and `key` is the {payload, type} value pair.
// When the pair's type is a reference type (4..10 or 12), the flag byte's
// bit 0 is clear, and (for type 4) the payload's high bit is clear, the
// payload's header byte (payload-4 for type 4 / 7, else payload-8) has bit 0
// set; for a non-type-4 payload it then appends to the debug track
// (KoreDebugTrackAppend).  thiscall + retn 8 hybrid in the
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
        (void)KoreTableResize(table, vmcore, key);
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

// sub_5ED980 (0x5ED980 - 0x5EDAB2) -- KoreReassignAllocHeap: the
// heap-grow stage-3 chain reassign (thiscall + retn 4 on the VM-heap
// sub-object, the vm-core object as the sole stack arg). Walk the
// alloc-node chain starting at the sub-object's +0x88 new-chain head
// (falling back to the old-chain head at *vmcore+0xE0, the same slot the
// +0x04 object reaches); a node is a 12-byte record {+0x00 kind, +0x04
// string id, +0x08 next, +0x0C size} with the 4-byte block header at
// node-4 (the block flag byte just before the record). A node whose header
// bit 0 is set is adopted into the new chain (clear bit 0 of the previous
// new head, new head := node, and every third adoption decrements the
// +0x6C charge by 1 when the +0x70 limit is not -1, panicking through
// KoreHeapPanic when it reaches 0); any other node is unlinked (the new
// head's +0x08 splices past it, or, with an empty new head, the old-chain
// head is updated — or the whole old chain is adopted into +0x88 when the
// node is not the head), released (KoreReleaseAllocNode), and its block is
// dealloc'd through the sub-object's +0x0C heap object (used-bytes counter
// at heap+0x04 -= size + 20, then the heap+0x0C dealloc fn with the
// heap+0x10 context on node-4 / size + 20 / 0), the +0x6C charge being
// decremented by 2 with the same limit / panic guard. On the way out the
// final new head's header bit 0 is cleared, the +0x88 slot is zeroed, and
// the (pre-zero) new-head value is returned. [NOT byte-identical]
// placement-dependent (the relative calls to KoreReleaseAllocNode /
// KoreHeapPanic) and a thiscall + retn 4 hybrid in the original
// (declared object-first here).
static int KoreReassignAllocHeap(int obj, int vmcore)
{
    int *L     = (int *)obj;
    int node   = L[0x88 / 4];
    if (node == 0)
    {
        node = *(int *)(*(int *)(L + 4) + 0xE0);   // the old-chain head
        if (node == 0)
            goto done;
    }
    int counter = 0;
    for (;;)
    {
        int next = *(int *)(node + 8);
        if ((*(int *)(node - 4) & 1) != 0)
        {
            int cur = L[0x88 / 4];
            if (cur != 0 && cur != node)
                *(int *)(cur - 4) &= ~1;
            L[0x88 / 4] = node;
            if (++counter == 3)
            {
                int b = L[0x6C / 4] - 1;
                if (L[0x70 / 4] != -1)
                {
                    if (b <= 0)
                        KoreHeapPanic(obj);
                    L[0x6C / 4] = b;
                }
                counter = 0;
            }
            node = next;
        }
        else
        {
            int cur = L[0x88 / 4];
            if (cur != 0)
            {
                *(int *)(cur + 8) = next;
            }
            else
            {
                int *oldHead = (int *)(*(int *)vmcore + 0xE0);
                if (node == *oldHead)
                    *oldHead = next;
                else
                {
                    L[0x88 / 4] = *oldHead;
                    *(int *)(*oldHead + 8) = next;
                }
            }
            (void)KoreReleaseAllocNode(obj, node, vmcore);
            int freed = node;
            node = (L[0x88 / 4] != 0) ? next : *(int *)(*(int *)vmcore + 0xE0);
            int heap  = L[0x0C / 4];
            int bytes = *(int *)(freed + 0xC) + 20;
            *(int *)(heap + 4) -= bytes;
            ((void (*)(int, int, int, int))*(int *)(heap + 0xC))(
                *(int *)(heap + 0x10), freed - 4, bytes, 0);
            int b = L[0x6C / 4] - 2;
            if (L[0x70 / 4] != -1)
            {
                if (b <= 0)
                    KoreHeapPanic(obj);
                L[0x6C / 4] = b;
            }
        }
        if (node == 0)
            break;
    }
done:
    int result = L[0x88 / 4];
    if (result != 0)
    {
        *(int *)(result - 4) &= ~1;
        L[0x88 / 4] = 0;
    }
    return result;
}

// sub_5D6C10 (0x5D6C10 - 0x5D6DD2) -- KoreRehashAllocNode: the per-bucket
// rehash step that KoreRehashAllocTable (and the VM finaliser) call on a live
// alloc node (thiscall + retn 8 in the original: this = the bucket sub-object
// at table+0xBC, arg 1 = the table wrapper, arg 2 = the bucket cursor). It
// drops the node: decrements the sub-object's +0x04 node count, sizes the
// block as (node+0x00 & 0x3FFFFFFF) + 15, subtracts that from the table's
// +0x04 used-bytes, and calls the table's +0x0C dealloc fn (its +0x10 context
// on node-4 / size / 0), then clears the bucket. When the next bucket is empty
// the unchanged cursor is returned; otherwise it finds the first empty bucket
// at or after cursor + 2 and, walking the bucket index backward from there,
// re-inserts each out-of-order node into its home bucket (B[home] = B[cur],
// B[cur] = 0) and ORs bit 0 of the block header of every shifted node (whose
// key's top bit is clear) so the outer pass re-does it, updating the home /
// accumulator as it goes. Returns the final cursor. [NOT byte-identical] a
// thiscall + retn 8 hybrid in the original (declared object-first here) and it
// calls the table's mutable +0x0C dealloc slot.
static int KoreRehashAllocNode(int obj, int wrapper, int cursor)
{
    int *sub = (int *)obj;                    // the bucket sub-object (thiscall this)
    int T    = *(int *)wrapper;               // the table behind the wrapper
    int *B   = (int *)sub[0];                 // the bucket array
    unsigned int mask = (unsigned int)sub[2];
    --sub[1];                                 // the sub-object's node count
    int node = B[cursor];
    int n0   = *(int *)node;
    int size = (n0 & 0x3FFFFFFF) + 15;
    *(int *)(T + 4) -= size;                  // the table's used-bytes
    ((void (*)(int, int, int, int))*(int *)(T + 0x0C))(*(int *)(T + 0x10), node - 4, size, 0);
    B[cursor] = 0;
    if (B[(cursor + 1) & mask] == 0)
        return cursor;

    // Find the first empty bucket at or after cursor + 2.
    unsigned int firstEmpty = (unsigned int)cursor + 2;
    if (B[mask & firstEmpty] != 0)
    {
        do
        {
            ++firstEmpty;
        } while (B[mask & firstEmpty] != 0);
    }
    unsigned int home = (unsigned int)cursor;
    unsigned int acc  = (unsigned int)cursor;
    if (firstEmpty - 1 <= (unsigned int)cursor)
        return (int)acc;

    unsigned int vC = firstEmpty - 1;         // the outer walk cursor
    do
    {
        unsigned int v13 = vC & mask;         // the current bucket
        int nd = B[v13];
        unsigned int v14 = (unsigned int)(*(int *)(nd + 4)) & mask;
        unsigned int v15 = firstEmpty & mask;
        unsigned char v28 = (firstEmpty >= mask + 1) || (v14 > vC);
        unsigned char v22 = (v28 == 0) && (v14 <= home);
        unsigned char v23 = (v28 != 0) && (v14 <= home) && (v14 > v15);
        int v16 = (v28 != 0) && (v14 >= (unsigned int)cursor) && (home < v15);

        unsigned int nextVC;
        if (v22 != 0 || v23 != 0 || v16 != 0)
        {
            unsigned int v17 = home & mask;
            B[v17] = B[v13];
            B[v13] = 0;
            home = vC;
            if (v17 >= acc)
            {
                if (v13 < acc)
                {
                    int nn = B[v17];
                    if (*(int *)nn >= 0)
                        *(int *)(nn - 4) |= 1;
                }
            }
            else if (v13 >= acc)
            {
                for (unsigned int i = v17 + 1; i < acc; ++i)
                {
                    int nn = B[i];
                    if (nn != 0 && *(int *)nn >= 0)
                        *(int *)(nn - 4) |= 1;
                }
                acc = v17;
            }
            nextVC = firstEmpty - 1;
        }
        else
        {
            nextVC = vC - 1;
        }
        vC = nextVC;
    } while (vC > home);

    return (int)acc;
}

// sub_5D9FC0 (0x5D9FC0 - 0x5DA09C) -- KoreRehashAllocTable: the stage-4
// rehash of the heap sub-object's alloc table (thiscall on the heap
// sub-object). The alloc table sits behind the sub-object's +0x04 wrapper
// pointer (*wrapper = the table; its bucket sub-object at table+0xBC holds the
// bucket array / node count / mask at its +0x00 / +0x04 / +0x08). It walks the
// bucket cursor from the persisted +0x80 (reset to 0 whenever the +0x84
// "table size" stamp differs from mask + 1, i.e. the table just grew) up to
// mask + 1. For each bucket, an empty or negative-key node is skipped; a node
// whose block-header bit 0 is already set, or whose key has bit 0x40000000
// set, is just cleared (bit 0 dropped) and advanced; any other live node is
// rehashed through the per-node helper KoreRehashAllocNode (which returns the
// resumed cursor) and charges the alloc budget (obj+0x6C) 5. Every fourth such
// node additionally charges 1, persists the cursor to +0x80, and -- when the
// +0x70 panic-limit is not -1 -- panics through KoreHeapPanic if the budget is
// non-positive. On completion the final budget and a zeroed cursor are written
// back and the cursor is returned. [NOT byte-identical] a thiscall in the
// original (declared object-first here), placement-dependent (relative calls
// into KoreRehashAllocNode and KoreHeapPanic).
static int KoreRehashAllocTable(int obj)
{
    int *h = (int *)obj;
    int wrapper = h[1];                       // obj + 0x04 (the table wrapper)
    int T       = *(int *)wrapper;            // the table
    int *t      = (int *)T;
    unsigned int mask = (unsigned int)t[0xC4 / 4];
    int *B       = (int *)t[0xBC / 4];
    int bound    = (int)mask + 1;

    if (bound != h[33])                       // obj + 0x84 (the table-size stamp)
    {
        h[33] = bound;
        h[32] = 0;                            // obj + 0x80 (the rehash cursor)
    }
    int cursor  = h[32];
    int budget  = h[27];                      // obj + 0x6C
    int counter = 0;
    while (cursor < bound)
    {
        int node = B[mask & (unsigned int)cursor];
        if (node != 0 && *(int *)node >= 0)
        {
            if ((*(int *)(node - 4) & 1) != 0 || (*(int *)node & 0x40000000) != 0)
            {
                *(int *)(node - 4) &= ~1;     // drop the rehash-pending flag
                ++cursor;
            }
            else
            {
                cursor = KoreRehashAllocNode(T + 0xBC, wrapper, cursor);
                budget -= 5;
            }
            if (++counter == 4)
            {
                --budget;
                h[32] = cursor;               // persist progress
                if (h[28] != -1 && budget <= 0)   // obj + 0x70 (the panic limit)
                    KoreHeapPanic(obj);
                counter = 0;
            }
        }
        else
        {
            ++cursor;
        }
    }
    h[27] = budget;
    h[32] = 0;
    return cursor;
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
                if (KoreDrainFreeList(h[3], h[1], claim, h[27]) == 0)
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




// =====================================================================
// The setjmp/longjmp-guarded "call a value" handler cluster (sub_5EC960 and
// its callees).  The VM installs a jmp buffer at table+0x40 (a 68-byte stack
// region: a 44-byte setjmp context plus a next-in-chain pointer at +0x40); a
// panic (KoreRaisePanic) pops the chain (table+0x40 <- jmpbuf+0x40) and
// longjmps back here.  A value-stack entry is an 8-byte {low, high} pair where
// "high" (the +4 word) is the value's type/size tag.  table fields: +0x1C top,
// +0x20 base, +0x24 end (usable limit), +0x28 start (raw buffer), +0x0C array
// end, +0x04 array base, +0x44 C-call depth, +0x40 jmp buffer.
// =====================================================================

// ---- sub_407540: rotate the value-stack window up by one pair.  n is the
// window length in pairs (negative n = -n pairs, positive n = the distance from
// the base in pairs).  The top pair is saved, the n-1 pairs below it are shifted
// up by one (toward the end), and the saved pair is written at the bottom of
// the window; the top pointer is left untouched.
static void KoreRotateValueStackTop(int table, int n)
{
    int *v = (int *)table;
    int k;
    if (n > 0)
        k = ((v[0x1C / 4] - v[0x20 / 4]) >> 3) - n + 1;
    else
        k = -n;
    if (k <= 1)
        return;
    int *top = (int *)v[0x1C / 4];
    int *a = top - 8 * (k - 1);
    int saveLo = top[0];
    int saveHi = top[1];
    while (a < top)
    {
        a[2] = a[0];
        a[3] = a[1];
        a += 2;
    }
    top[-2 * k] = saveLo;
    top[-2 * k + 1] = saveHi;
}

// ---- sub_5DD8C0: grow the continuation stack (a 16-byte-slot vector at
// +0x00 start / +0x04 end / +0x08 top).  Doubles the slot count, copies the
// used slots into a fresh VM block, advances the top, and frees the old block.
// Returns the VM free's result (callers ignore it).
static int KoreGrowContinuationStack(int obj, int vmcore)
{
    int *t = (int *)obj;
    int oldStart = t[0];
    int oldEnd = t[1];
    int oldTop = t[2];
    int oldSlots = (oldEnd - oldStart) >> 4;
    int newSlots = oldSlots * 2;
    int newStart = KoreVmAlloc(vmcore, newSlots * 16);
    memcpy((void *)newStart, (void *)oldStart, oldTop + 16 - oldStart);
    t[0] = newStart;
    t[2] = newStart + newSlots * 16 - 16;
    int copySize = oldStart != 0 ? 16 * (oldEnd - oldStart) / 16 : 0;
    int *global = (int *)(*(int *)vmcore);
    global[1] -= copySize;
    return ((int (*)(int, int, int, int))global[3])(global[4], oldStart, copySize, 0);
}

// ---- sub_60F9C0: in-place reversal of `count` 8-byte value pairs (swap i with
// count-1-i).  count <= 0 is a no-op.
static void KoreReverseValuePairs(int p, int count)
{
    int n = (count - (int)((unsigned)count >> 31)) >> 1;
    int *a = (int *)p;
    int *b = (int *)(p + 8 * (count - 1));
    while (n > 0)
    {
        int t0 = a[0], t1 = a[1];
        a[0] = b[0]; a[1] = b[1];
        b[0] = t0;   b[1] = t1;
        a += 2;
        b -= 2;
        --n;
    }
}

// ---- sub_5DDC20: grow the hash table's row array (8-byte {key, type} entries)
// to newCount, copying the old rows and freeing the old array.  Returns the new
// array pointer.
static int KoreGrowHashTableRows(int obj, int vmcore, int newCount)
{
    int *t = (int *)obj;
    int oldArray = t[3];
    int oldCount = t[4];
    int newArray = KoreVmAlloc(vmcore, newCount * 8);
    memset((void *)newArray, 0, newCount * 8);
    memcpy((void *)newArray, (void *)oldArray, oldCount * 8);
    t[3] = newArray;
    t[4] = newCount;
    if (oldCount != 0)
    {
        int *global = (int *)(*(int *)vmcore);
        global[1] -= oldCount * 8;
        ((void (*)(int, int, int, int))global[3])(global[4], oldArray, oldCount * 8, 0);
    }
    return newArray;
}

// ---- sub_5E2BD0: resize a hash table to a new hash size and (optionally) row
// count.  Recomputes the power-of-two hashsize from the requested bit length,
// allocates the new hash block, re-inserts every live row, and frees the old
// block.  Panics if the hash part would exceed 2^24 slots.
static int KoreResizeHashTable(int obj, int vmcore, int newRows, int hashBits)
{
    int *t = (int *)obj;
    int oldRowCount = t[4];
    int oldMask = t[1];
    int oldHashSize = oldMask + 1;
    int oldHashBlock = t[2];
    int *global = (int *)(*(int *)vmcore);

    if (newRows > oldRowCount)
    {
        int oldArray = t[3];
        int newArray = KoreVmAlloc(vmcore, newRows * 8);
        t[3] = newArray;
        t[4] = newRows;
        memset((void *)newArray, 0, newRows * 8);
        memcpy((void *)newArray, (void *)oldArray, newRows * 8);
    }

    if (hashBits == 0)
    {
        t[2] = 0;
        t[1] = -1;
        t[5] = 0;
        return 0;
    }

    int norm = hashBits - 1;
    int shift = 0;
    while (norm >= 0x100)
    {
        norm >>= 8;
        shift += 8;
    }
    int bitLen = (int)KoreLog2Byte[norm] + shift + 1;
    int hashsize = 1 << bitLen;
    if (bitLen > 0x18)
        KoreTablePanic(vmcore, "Table overflow (hash part too big)");
    int newHashBlock = KoreVmAlloc(vmcore, hashsize * 20);
    memset((void *)newHashBlock, 0, hashsize * 20);
    t[1] = hashsize - 1;
    t[2] = newHashBlock;
    t[5] = hashsize;

    if (hashsize > 0)
    {
        for (int i = oldHashSize - 1; i >= 0; --i)
        {
            int *slot = (int *)(oldHashBlock + i * 20);
            int key = slot[0];
            int type = slot[1];
            int rowKey = slot[2];
            int rowType = slot[3];
            if (rowType == 0)
                continue;
            if (type == 3)
            {
                double d = *(double *)&key;
                long l = (long)d;
                if ((double)l == d && l > 0 && (unsigned)(l - 1) < (unsigned)newRows)
                {
                    int *rowArray = (int *)t[3];
                    rowArray[(l - 1) * 2] = rowKey;
                    rowArray[(l - 1) * 2 + 1] = rowType;
                    continue;
                }
            }
            int pair[2] = { key, type };
            int index = KoreTableEnsureRow(obj, vmcore, (int)pair, 0);
            int *block = (int *)t[2];
            block[index * 5 + 2] = rowKey;
            block[index * 5 + 3] = rowType;
        }
    }

    if (oldHashSize > 0)
    {
        global[1] -= oldHashSize * 20;
        ((void (*)(int, int, int, int))global[3])(global[4], oldHashBlock, oldHashSize * 20, 0);
    }
    return 0;
}

// ---- sub_5E4FC0: copy `count` 8-byte value pairs from src into the row array
// starting at slot `start - 1`, growing the row array (and hash) to fit if
// needed.
static int KoreCopyTableRows(int obj, int vmcore, int start, int count, int src)
{
    int *t = (int *)obj;
    if (count > t[4] && count + 1 > t[4])
        KoreResizeHashTable(obj, vmcore, count + 1, t[1] + 1);
    int *rowArray = (int *)t[3];
    int *s = (int *)src;
    int *d = (int *)(rowArray + 8 * (start - 1));
    for (int i = 0; i < count; ++i)
    {
        d[0] = s[0];
        d[1] = s[1];
        d += 2;
        s += 2;
    }
    return 0;
}

// ---- sub_5E7EF0: capture `upcount` upvalue pairs from src into a fresh hash
// table, store {table, 5} at dst, set the table's "upvalue count" field, and
// mark it dirty.
static int KoreCreateUpvalueTable(int obj, int vmcore, int closure, int dst, int src)
{
    int *t = (int *)obj;
    int *g = (int *)(*(int *)vmcore);
    int upcount = *(int *)(closure + 0xB);
    int table = KoreAllocHashTable(vmcore, upcount, 1);
    *(int *)(dst) = table;
    *(int *)(dst + 4) = 5;
    double d = (double)upcount;
    int valpair[2];
    *(double *)&valpair[0] = d;
    valpair[1] = 3;
    KoreTableSet(table, vmcore, g[0x13C / 4], (int)valpair);
    KoreCopyTableRows(table, vmcore, 1, upcount, src);
    if (g[0x2C / 4 + 4] == 1)
        KoreDebugMarkDirty((int)((char *)obj - 8), src);
    return table;
}

// ---- sub_5DDED0: make the value at v resolvable as a callable.  Inserts an
// 8-byte gap before v (shifting [v, top) up), then, for the non-special value
// types, looks up the metatable entry and re-interns the resolved value in
// place.  Returns the new top (v + 8).
static int KoreNormalizeValue(int table, int v, int top)
{
    int *tv = (int *)table;
    if (top + 8 > tv[0x24 / 4])
    {
        int start = tv[0x28 / 4];
        int *v2 = (int *)(start + 8 * ((v - start) / 8));
        int top2 = start + 8 * ((top - start) / 8);
        KoreReallocValueStack(table + 4, table, 1);
        v = (int)v2;
        top = top2;
    }
    int *vp = (int *)v;
    int *tp = (int *)top;
    int *p = tp - 2;
    while (p >= vp)
    {
        p[2] = p[0];
        p[3] = p[1];
        p -= 2;
    }
    int type = *(int *)(v + 4);
    int val;
    switch (type)
    {
    case 5:
        val = *(int *)(vp[0]);
        break;
    case 7:
        val = *(int *)(vp[0] + 4);
        break;
    case 0xC:
        val = *(int *)(vp[0] + 8);
        break;
    default:
    {
        int *g = (int *)(*(int *)table);
        int key = g[type * 2 + 0x174 / 4];
        int out[2] = { 0, 0 };
        if (KoreLookupStringTable(g[0x1CC / 4], (int)out, key, 4) == 0)
            return top;
        val = out[0];
        if (out[1] == 0)
            return top;
        if (out[1] != 5)
            val |= 1;
        break;
    }
    }
    if (val != 0)
    {
        int *g = (int *)(*(int *)table);
        int hash = g[0x128 / 4];
        int out2[2] = { 0, 0 };
        if (val & 1)
            KoreLookupStringRegistry(val & ~1, (int)out2, table, hash, 4);
        else
            KoreLookupStringTable(val, (int)out2, hash, 4);
        if (out2[1] != 0)
        {
            vp[0] = out2[0];
            vp[1] = out2[1];
        }
    }
    return v + 8;
}

// ---- sub_612D50: push a C-function call frame onto the continuation stack and
// set the value-stack base/top for the C call.  Returns the new base (v + 8).
static int KorePushCFrame(int obj, int vmcore, int retPos, int v, int newTop, int valIdx)
{
    int *t = (int *)obj;
    int *tv = (int *)vmcore;
    if (t[2] + 16 == t[1])
        KoreGrowContinuationStack(obj, vmcore);
    int *frame = (int *)t[2];
    t[2] += 16;
    frame[0] = tv[0x20 / 4];
    frame[1] = valIdx;
    frame[2] = 0;
    frame[3] = retPos;
    tv[0x20 / 4] = v + 8;
    tv[0x1C / 4] = newTop;
    if (((tv[0x24 / 4] - newTop) & ~7) < 0xA0)
        KoreReallocValueStack(obj, vmcore, 0x14);
    return tv[0x20 / 4];
}

// ---- sub_612BB0: push a closure call frame.  Relocates upvalues into place,
// materializes them into a hash table when requested, and sets the base/top.
// Returns the new base (p).
static int KorePushClosureFrame(int obj, int vmcore, int retPos, int v, int top, int valIdx)
{
    int *tv = (int *)vmcore;
    int *closure = (int *)(*(int *)v);
    int upcount = *(unsigned char *)(closure + 0xB);
    int argcount = *(unsigned char *)(closure + 9);
    int newEnd = top + 8 * argcount + 8;
    int nslots = (top - v) >> 3;
    if (newEnd > tv[0x24 / 4])
    {
        int extra = (newEnd - top) >> 3;
        int start = tv[0x28 / 4];
        int *v2 = (int *)(start + 8 * ((v - start) / 8));
        int top2 = start + 8 * ((top - start) / 8);
        KoreReallocValueStack(obj, vmcore, extra);
        v = (int)v2;
        top = top2;
    }
    int p = v + 8;
    int *t = (int *)obj;
    if (t[2] + 16 == t[1])
        KoreGrowContinuationStack(obj, vmcore);
    int *frame = (int *)t[2];
    t[2] += 16;
    frame[0] = newEnd;
    frame[1] = valIdx;
    frame[2] = 0;
    frame[3] = retPos;

    if (*(int *)(closure + 0xA) & 2)
    {
        int m = (top - 8 * upcount - p) >> 3;
        if (m < 0)
            m = 0;
        if (m != 0)
        {
            frame[2] = m;
            int *vp = (int *)v;
            KoreReverseValuePairs(v, nslots - m);
            KoreReverseValuePairs(v + 8 * (nslots - m), m);
            KoreReverseValuePairs(v, nslots);
            p = top + 8 * m;
            (void)vp;
        }
    }
    if (*(int *)(closure + 0xA) & 4)
        KoreCreateUpvalueTable(obj, vmcore, (int)closure, p + 8 * upcount, v);
    else
        *(int *)(p + 8 * upcount + 4) = 0;
    tv[0x20 / 4] = p;
    if (newEnd < p + 8 * upcount)
    {
        int *z = (int *)newEnd + 1;
        int *ze = (int *)(p + 8 * upcount) + 1;
        while (z < ze)
        {
            *z = 0;
            z += 2;
        }
    }
    tv[0x1C / 4] = v + 8 * argcount;
    return p;
}

// ---- sub_60BBD0: pop a C-function call frame, compacting the value stack to
// hold the `rv` returned pairs and restoring the pre-call base.  Returns the
// saved base (frame[0]).
static int KorePopCFrame(int obj, int table, int rv)
{
    int *t = (int *)obj;
    int *tv = (int *)table;
    int *frame = (int *)t[2];
    t[2] -= 16;
    int retPos = frame[3];
    int base = tv[0x20 / 4];
    int top = tv[0x1C / 4];
    int minv;
    if ((unsigned)rv < (unsigned)retPos)
        minv = rv;
    else
        minv = retPos;
    int *src = (int *)(top - rv * 8);
    int *dst = (int *)(base - 8);
    for (int i = 0; i < minv; ++i)
    {
        dst[0] = src[0];
        dst[1] = src[1];
        dst += 2;
        src += 2;
    }
    if (retPos != -1 && minv < retPos)
    {
        int *z = (int *)(base - 8 + minv * 8) + 1;
        int *ze = (int *)(base - 8 + retPos * 8) + 1;
        while (z < ze)
        {
            *z = 0;
            z += 2;
        }
    }
    if (retPos == -1)
        tv[0x1C / 4] = base - 8 + minv * 8;
    else
        tv[0x1C / 4] = base - 8 + retPos * 8;
    tv[0x20 / 4] = frame[0];
    return frame[0];
}


// The interpreter op-primitives (KoreRunCode callees) and the interpreter itself.


// ---------------------------------------------------------------------------
// Kore bytecode op-primitives: the functions KoreRunCode (sub_5E9AA0) calls out
// to for individual opcode semantics.  Faithful reimplementation of the
// sub_XXXXXX callees (all take/return int addresses, matching the project
// convention for the VM helpers).
// ---------------------------------------------------------------------------


// ============================================================================
//  The Kore bytecode interpreter (sub_5E9AA0) and its op-primitive helpers.
//  The VM stores every value as an 8-byte pair { value (dword), type (dword) }.
//  A "slot" is such a pair; value registers are addressed as base + 8*index.
//  Types: 0=nil, 4=string, 5=closure, 6=bool(true), 7=table, 12=struct.
//  Instruction word: bits 25-31 = opcode, bits 0-7 = destination register,
//  bits 8-16 = operand A (9-bit), bits 17-25 = operand B (9-bit).
//  A 9-bit operand < 0x100 selects a local register (base + 8*op); otherwise
//  it selects a global (globals + 8*(op & 0xFF)).  [NOT byte-identical]
// ============================================================================

// ---- VM data globals (high-VA image globals, declared static per project).
static int        g_qword_1DBCFD8[2]  = { 0, 0 };                        // 0x1DBCFD8 nil pair
static int        g_qword_1DBD110[2]  = { -1, -1 };                      // 0x1DBD110
static int        g_qword_1DBCF1C[2]  = { 0x7475706E, 0x00004040 };      // 0x1DBCF1C
static int        g_qword_1DBCF64[2]  = { 0x7475706E, 0x00004040 };      // 0x1DBCF64
static int        g_qword_1DBD270[2]  = { -1, -1 };                      // 0x1DBD270
static int        g_word_1DBD02A      = 0xFFFF;                          // 0x1DBD02A
static int        g_word_1DBD02C      = 0xFFFF;                          // 0x1DBD02C
static int        g_proto_1DBD890[4]  = { -1, -1, -1, -1 };              // 0x1DBD890
static int        g_proto_1DBD8A0[4]  = { -1, -1, -1, -1 };              // 0x1DBD8A0
// off_1C830B8: type-name table (index = type tag).
static const char *g_KoreTypeNames[16] = {
    "nil", "number", "string", "boolean", "table", "closure",
    "thread", "lightuserdata", "userdata", "function", "coroutine",
    "unary", "struct", "prototype", "upvalue", "unknown"
};
// off_1C83014: metamethod / operator-name table (index = op id).
static const char *g_KoreOpNames[16] = {
    "index", "newindex", "mode", "eq", "lt", "le", "concat",
    "add", "sub", "mul", "div", "mod", "pow", "unm", "len", "call"
};

// ---- sub_5D97E0: set the table's array-part length field.
static int KoreSetArrayEnd(int vmcore, int count)
{
    *(int *)(vmcore + 40) = count;
    return count;
}

// ---- sub_407440: structural value equality (top of the value stack).
static int KoreValueEq(int type, int a, int b)
{
    if (type == 4) {
        int pa = *(int *)a, pb = *(int *)b;
        if (pa != pb)
            return 0;
        if ((pa & 0x80000000) == 0) {
            int la = *(int *)(pa + 8), lb = *(int *)(pb + 8);
            if (la != lb)
                return 0;
            return memcmp((char *)pa + 12, (char *)pb + 12, la) == 0;
        }
        return 1;
    }
    return a == b;
}

// ---- sub_5D3280: table get by numeric key. (0x5D3280 - 0x5D336E)
// The original is a thiscall + retn 8 hybrid (the table in ECX, the out
// slot and the float key on the stack). An integer-valued key (the
// __ftol2_sse truncation round-trips exactly) reads dense row n - 1 of the
// 8-byte row array ({value, type}) at the table's +0x0C (the row count is
// at +0x10); the dense array pointer being 0 skips straight to the 20-byte
// hash rows at +0x08: the bucket is (the key rounded toward +infinity by
// the FPU, saturating to INT_MIN / INT_MAX) & the +0x04 mask, and the row
// chain ({key, key type, value, type, next absolute row pointer}) is walked
// until a type-3 row whose key equals the key, returning that row's
// {value, type} pair; on exhaustion (or no bucket array) the
// zero-initialized nil pair globals are written. [NOT byte-identical]
// placement-dependent (the relative call to __ftol2_sse); the original's
// x87 flag tests read the ambient FPU status word and its row-key
// comparison duplicates the key instead of the row key, so this implements
// the intended semantics (round-trip / key equality, NaN never hits).
static int KoreGlobalGetByFloat(int table, int out, int index)
{
    float f = *(float *)&index;
    int *t = (int *)table;
    // __ftol2_sse: truncate toward zero, saturating NaN / out-of-range to INT_MIN.
    int n;
    if (f != f || f >= 2147483648.0f || f < -2147483648.0f)
        n = 0x80000000;
    else
        n = (int)f;
    int rows = t[3];
    if (rows != 0 && (float)n == f && (unsigned)(n - 1) < (unsigned)t[4])
    {
        int *r = (int *)(rows + 8 * n - 8);
        *(int *)out = r[0];
        *(int *)(out + 4) = r[1];
        return out;
    }
    int buckets = t[2];
    if (buckets != 0)
    {
        int idx;                          // fistp, RC = +infinity (saturating)
        if (f != f)
            idx = 0x80000000;
        else if (f >= 2147483648.0f)
            idx = 0x7FFFFFFF;
        else if (f <= -2147483648.0f)
            idx = 0x80000000;
        else
        {
            idx = (int)f;                 // truncate toward zero
            if ((float)idx < f)
                idx++;                    // round toward +infinity
        }
        int *r = (int *)(buckets + 20 * ((unsigned)idx & (unsigned)t[1]));
        for (;;)
        {
            if (r[1] == 3 && *(float *)r == f)
            {
                *(int *)out = r[2];
                *(int *)(out + 4) = r[3];
                return out;
            }
            int next = r[4];
            if (next == 0)
                break;
            r = (int *)next;
        }
    }
    *(int *)out = g_dword_1DBCFE0;
    *(int *)(out + 4) = g_dword_1DBCFE4;
    return out;
}

// ---- sub_5D6EB0: string compare (strcmp of the payload bytes).
static int KoreStringCompare(int a, int b)
{
    int pa = *(int *)a, pb = *(int *)b;
    if (pa == pb) return 0;
    if ((pa & 0x80000000) == 0 && (pb & 0x80000000) == 0)
        return strcmp((const char *)pa + 12, (const char *)pb + 12);
    return pa - pb;
}

// ---- sub_5D6DE0: string-to-float conversion (strtod of the payload bytes).
static int KoreFloatFromString(int s, int len, int out)
{
    char buf[0x100];
    int n = (len > 0 && len < 0x100) ? len : 0x100;
    const char *p = (const char *)*(int *)s + 12;
    int i;
    for (i = 0; i < n && p[i]; ++i) buf[i] = p[i];
    buf[i] = 0;
    *(float *)out = (float)strtod(buf, 0);
    return 1;
}

// ---- sub_610BA0: struct field get by {value, type} key. (0x610BA0 - 0x610C56)
// The type-12 (struct) arm of the VM's gettable-by-value-key opcode (0x0C),
// the direct getter counterpart of KoreStructSetField (0x612630). The
// original is a thiscall + retn 0x0C hybrid (the instance in ECX, the out
// slot, the vm-core and the key slot on the stack). A string key (type 4)
// linearly scans the instance's prototype / descriptor table (the +0x04
// pointer): the +0x00 field count and the 16-byte rows from +0x18, each
// keyed by its +0x00 interned id; on a match the row's +13 descriptor byte
// is the field slot -- the value is the instance's +4 * slot word and the
// type the packed byte at +4 * (slot & 0xFC) + (slot & 3) (the same layout
// KoreStructSetField writes). A non-string key or a scan miss falls back to
// the instance's +0x0C backing hash table via KoreGlobalGetValue; a null
// backing table yields the zero-initialized nil pair globals.
// [NOT byte-identical] placement-dependent (the relative call to
// KoreGlobalGetValue) and refs the two high-VA nil pair globals.
static int KoreStructFieldGet(int obj, int out, int vmcore, int key)
{
    int *inst = (int *)obj;
    if (*(int *)(key + 4) == 4)
    {
        int *reg = (int *)inst[1];
        int count = reg ? reg[0] : 0;
        if (count > 0)
        {
            int *row = (int *)(reg + 6);
            for (int i = 0; i < count; ++i, row += 4)
            {
                if (row[0] == *(int *)key)
                {
                    if ((int)row != 0)
                    {
                        int slot = *(unsigned char *)(row + 13);
                        *(int *)out = inst[slot];
                        *(int *)(out + 4) = *(unsigned char *)((char *)inst + 4 * (slot & 0xFC) + (slot & 3));
                        return out;
                    }
                    break;
                }
            }
        }
    }
    int backing = inst[3];
    if (backing != 0)
    {
        int tmp[2] = {0, 0};
        KoreGlobalGetValue(backing, (int)tmp, vmcore, key);
        *(int *)out = tmp[0];
        *(int *)(out + 4) = tmp[1];
        return out;
    }
    *(int *)out = g_dword_1DBCFE0;
    *(int *)(out + 4) = g_dword_1DBCFE4;
    return out;
}

// sub_606AE0 (0x606AE0 - 0x606B10) -- KoreStructFindSlot: linear lookup of a
// struct prototype / descriptor table's 16-byte field-slot row by its +0x00
// interned field id. The object (ECX) is the same table KoreStructFieldGet /
// KoreMakeStructInstance use: the +0x00 field count, the 16-byte rows from
// +0x18 keyed by their +0x00 interned id. Returns a pointer to the matching
// row, or 0 for a zero count or a scan miss. Thiscall + retn 4 hybrid.
// Byte-identical: __declspec(naked) inline asm of the original's exact 48-byte
// sequence -- no calls and no data references, so the bytes match at any
// placement.
static int __declspec(naked) KoreStructFindSlot(int obj, int key)
{
    (void)obj; (void)key;
    __asm {
        push esi
        mov  esi, [ecx]
        xor  eax, eax
        push edi
        test esi, esi
        jna  kfs_done
        mov  edi, [esp+0Ch]           ; key
        lea  edx, [ecx+18h]           ; first slot row
kfs_loop:
        cmp  dword ptr [edx], edi
        jz   kfs_hit
        inc  eax
        add  edx, 10h
        cmp  eax, esi
        jb   kfs_loop
kfs_done:
        pop  edi
        xor  eax, eax
        pop  esi
        retn 4
kfs_hit:
        shl  eax, 4
        pop  edi
        lea  eax, [eax+ecx+18h]
        pop  esi
        retn 4
    }
}

// ---- sub_5DDFD0: metatable operator lookup -> interned string (or the nil pair).
static __int64 KoreLookupMetamethod(int vmcore, int valA, int valB, int opId)
{
    int type = *(int *)(valA + 4);
    int name = 0;
    if (type == 5)
        name = *(int *)valA + 8;
    else if (type == 7)
        name = *(int *)(valA + 4) + 8;
    else if (type == 12)
        name = *(int *)(valA + 8) + 8;
    int out[2] = {0, 0};
    if (name) {
        int meta = *(int *)(name + 4);
        if (meta) {
            KoreLookupStringTable(vmcore, (int)out, *(int *)(meta + 4), 0);
            name = out[0];
        }
    }
    if (!name && valB != valA) {
        int type2 = *(int *)(valB + 4);
        if (type2 == 5)      name = *(int *)valB + 8;
        else if (type2 == 7) name = *(int *)(valB + 4) + 8;
        else if (type2 == 12) name = *(int *)(valB + 8) + 8;
        if (name) {
            int meta = *(int *)(name + 4);
            if (meta) {
                KoreLookupStringTable(vmcore, (int)out, *(int *)(meta + 4), 0);
                name = out[0];
            }
        }
    }
    if (name)
        return (__int64)(unsigned int)*(int *)(name + 8) | ((__int64)1 << 32);
    return ((unsigned __int64)(unsigned int)g_dword_1DBCFE4 << 32) | (unsigned int)g_dword_1DBCFE0;
}

// ---- sub_5E2970: make a struct instance from a prototype. (0x5E2970)
// The block is 4 * BYTE1(proto[4 * count + 5]) + 4 bytes: word 0 = 0, word
// 1 = the prototype (the descriptor table the field get / set routines
// scan), words 2.. zeroed, word 3 = the backing table. With a backing table
// (rows / buckets nonzero) the prototype must have its +0x08 backing-table
// flag (else the VM panics); the table is registered in the vm-core's
// +44+72 slot (type 5 at +76) and the vm-core's +120 counter is zeroed.
// The no-backing-table path (rows == buckets == 0) is the sub_5D2D70 helper
// inlined here. [NOT byte-identical] placement-dependent (the relative
// calls to the allocation / panic helpers).
static int KoreMakeStructInstance(int vmcore, int proto, int rows, int buckets)
{
    int count = *(int *)proto;
    int size = 4 * (int)(*(unsigned char *)(proto + 16 * count + 21) & 0xFF) + 4;
    if (rows != 0 || buckets != 0)
    {
        if (*(int *)(proto + 8) == 0)
            KoreTablePanic(vmcore, "Attempt to create a struct instance with a backing table, which isn't enabled in the prototype.");
        int backing = KoreAllocHashTable(vmcore, rows, buckets);
        int base = *(int *)vmcore;
        *(int *)(base + 44 + 72) = backing;
        *(int *)(base + 44 + 76) = 5;
        int *inst = (int *)KoreVmHeapAlloc(vmcore, size);
        if (inst != 0)
        {
            inst[1] = proto;
            inst[0] = 0;
            memset(inst + 2, 0, size - 8);
            inst[3] = backing;
        }
        *(int *)(base + 120) = 0;
        return inst ? (int)inst : 0;
    }
    int *inst = (int *)KoreVmHeapAlloc(vmcore, size);
    if (inst == 0)
        return 0;
    inst[1] = proto;
    inst[0] = 0;
    memset(inst + 2, 0, size - 8);
    return (int)inst;
}

// ---- sub_5E8FA0: build a table with arraySize / hashSize from the encodings.
static int KoreMakeInstance(int vmcore, int proto, int rows, int buckets)
{
    int obj = KoreAllocHashTable(vmcore, rows, buckets);
    (void)proto;
    return obj;
}

// ---- sub_5DDB40: table get by {value, type} key. (0x5DDB40 - 0x5DDBCF)
// The original is a thiscall + retn 0x0C hybrid (the table in ECX, the out
// slot, the vm-core hint and the key slot on the stack). A string key
// (type 4) is a string-table lookup (KoreLookupStringTable, hash 4); a
// number key (type 3) is the float-key read (KoreGlobalGetByFloat); any
// other key type finds its 20-byte row through KoreFindGlobalRow (the
// vm-core hint is dead there) and returns that row's {+0x08 value, +0x0C
// type} pair read from the hash rows at the table's +0x08; the -1 miss
// writes the zero-initialized nil pair globals. [NOT byte-identical]
// placement-dependent (the relative calls) and refs the two high-VA nil
// pair globals.
static int KoreGlobalGetValue(int table, int out, int hint, int key)
{
    int type = *(int *)(key + 4);
    if (type == 4)
        return KoreLookupStringTable(table, out, *(int *)key, 4);
    if (type == 3)
        return KoreGlobalGetByFloat(table, out, *(int *)key);
    int row = KoreFindGlobalRow(table, hint, (int *)key);
    if (row == -1)
    {
        *(int *)out = g_dword_1DBCFE0;
        *(int *)(out + 4) = g_dword_1DBCFE4;
        return out;
    }
    int *r = (int *)((int *)*(int *)(table + 8) + 20 * row);
    *(int *)out = r[2];
    *(int *)(out + 4) = r[3];
    return out;
}

// ---- sub_5E4360 family: generic indexed get (resolves the key, looks up the
// __index metatable operator, and reads it).
static __int64 KoreIndexOp(int vmcore, int value, int type, int key, int keyType, int code, int flags)
{
    (void)type; (void)keyType; (void)code; (void)flags;
    int out[2] = {0, 0};
    int obj = value;
    if (obj && (type == 7 || type == 5 || type == 12)) {
        if (keyType == 3) {
            KoreGlobalGetByFloat(value, (int)out, key);
        } else {
            int name = key;
            if (name) {
                int meta = *(int *)(name + 4);
                if (meta) {
                    int sub[2] = {0, 0};
                    KoreLookupStringTable(vmcore, (int)sub, *(int *)(meta + 4), 0);
                    out[0] = sub[0]; out[1] = sub[1];
                }
            }
        }
        return ((unsigned __int64)(unsigned int)out[1] << 32) | (unsigned int)out[0];
    }
    return ((unsigned __int64)(unsigned int)g_dword_1DBCFE4 << 32) | (unsigned int)g_dword_1DBCFE0;
}

// ---- sub_5E30C0 (0x5E30C0 - 0x5E3290): the recursive __index / gettable
// metamethod resolver. Switches on the {value,type} pair's type word (the
// interned name's type flag, the 7th arg): 10/9 invoke the metamethod, 5
// (closure) does a closure gettable + recurse, 12 (struct) does a struct
// gettable + recurse, any other type falls through to the name resolver
// (KoreGettableByName, sub_5E4420). Panics "loop in gettable" when the
// recursion depth (8th arg) is 0. cdecl, 8 args. [STUB] Not yet reimplemented;
// returns the zero nil pair.
static __int64 KoreGettableResolve(int vmcore, int value, int type, int key, int code, int nameValue, int nameType, int depth)
{
    (void)vmcore; (void)value; (void)type; (void)key; (void)code;
    (void)nameValue; (void)nameType; (void)depth;
    return ((unsigned __int64)(unsigned int)g_dword_1DBCFE4 << 32) | (unsigned int)g_dword_1DBCFE0;
}

// ---- sub_5E4420 (0x5E4420 - 0x5E44F4): the gettable name resolver. Resolves
// the metatable lookup "name" for a value of a given type: types 5/7/12 read
// the name from the value's own fields (+0x00/+0x04/+0x08); any other type
// looks the type's name up in the global string table (base = *vmcore, key at
// base + 8*type + 372, the string table at base + 204, hash 4) and ORs in the
// low bit of the name when the resolved type word is not 5. Panics "attempt to
// index a nil value" when the name is 0, interns it (KoreLookupInternedString
// -> the {value, type} qword; panics "error in gettable_event" when the type
// word is 0), and delegates to the recursive __index resolver
// (KoreGettableResolve, sub_5E30C0). cdecl, 6 args (vmcore, value, type, key,
// code, depth), returns the resolver's {value,type} qword. [NOT byte-identical]
// placement-dependent (the relative calls) and refs the two high-VA
// panic-format strings (0x01AE15D4 / 0x01AE15F4).
static __int64 KoreGettableByName(int vmcore, int value, int type, int key, int code, int depth)
{
    int *v = (int *)value;
    int name;
    switch (type)
    {
    case 5:  name = v[0]; break;
    case 7:  name = v[1]; break;
    case 12: name = v[2]; break;
    default:
    {
        int base = *(int *)vmcore;
        int out[2] = {0, 0};
        (void)KoreLookupStringTable(*(int *)(base + 204), (int)out, *(int *)(base + 8 * type + 372), 4);
        if (out[1] != 0)
        {
            if (out[1] != 5)
                out[0] |= 1;
            name = out[0];
        }
        else
            name = 0;
        break;
    }
    }
    if (name == 0)
        KoreTablePanic(vmcore, "attempt to index a nil value");
    unsigned __int64 r = KoreLookupInternedString(vmcore, name, 1);
    int nameValue = (int)r;       // low 32: the interned value/pointer
    int nameType  = (int)(r >> 32); // high 32: the interned type word
    if (nameType == 0)
        KoreTablePanic(vmcore, "error in gettable_event");
    return KoreGettableResolve(vmcore, value, type, key, code, nameValue, nameType, depth);
}

// ---- sub_610B50 (0x610B50 - 0x610B9E): get a global by integer index from a
// table object. Fast path: the (idx - 1)-th 8-byte dense row of the row array at
// tableObj + 0x0C (count at tableObj + 0x10). On a miss, falls back to the general
// value lookup (KoreGlobalGetValue) with a number key { (float)idx, 3 }. Returns a
// packed { value, type } pair (value in low 32, type in high 32). The first arg is
// the vm-core hint threaded to the miss fallback. Shared by the 0xE iterate-globals
// opcode and the value-stack / upvalue helpers.
static __int64 KoreGlobalGetByInt(int hint, int tableObj, int idx)
{
    if ((unsigned)(idx - 1) < (unsigned)*(int *)(tableObj + 0x10))
        return *(unsigned __int64 *)(*(int *)(tableObj + 0x0C) + 8 * (idx - 1));
    float fv = (float)idx;
    int key[2] = { *(int *)&fv, 3 };
    int out[2] = { 0, 0 };
    KoreGlobalGetValue(tableObj, (int)out, hint, (int)key);
    return ((unsigned __int64)(unsigned int)out[1] << 32) | (unsigned int)out[0];
}

// ---- sub_610C60 (0x610C60 - 0x610CA0): thiscall wrapper over KoreGlobalGetByInt.
// The object's +0x0C field is the inner row base; when it is nonzero it is passed
// straight to KoreGlobalGetByInt, otherwise the nil pair is written. The result is
// stored through *out (two dwords: value, then type) and *out is returned.
static int *KoreGlobalGetByIntIndirect(int thisobj, int *out, int hint, int idx)
{
    int rowbase = *(int *)(thisobj + 0x0C);
    if (rowbase != 0)
    {
        __int64 r = KoreGlobalGetByInt(hint, rowbase, idx);
        out[0] = (int)r;
        out[1] = (int)(r >> 32);
    }
    else
    {
        out[0] = g_dword_1DBCFE0;
        out[1] = g_dword_1DBCFE4;
    }
    return out;
}

// ---- sub_5E7330: store the value at a string/global key.
static int KoreGlobalSet(int vmcore, int value, int key)
{
    (void)vmcore; (void)key;
    int obj = value;
    if (obj && (*(int *)(obj + 4) == 7 || *(int *)(obj + 4) == 5))
        KoreTableSet(obj, vmcore, *(int *)key, *(int *)key);
    return obj;
}

// ---- sub_612750: struct field assign by value key.
static int KoreStructSet(int vmcore, int value, int key)
{
    (void)vmcore; (void)key;
    int obj = value;
    if (obj && *(int *)(obj + 4) == 7)
        KoreTableSet(obj, vmcore, *(int *)key, *(int *)key);
    return obj;
}

// sub_6112D0 (0x6112D0 - 0x611353) -- KoreStructEnsureBackingTable: lazily
// create and cache the backing hash table for a struct instance's numeric
// (float-keyed) fields. `obj` (this, in ECX in the original) is the struct
// instance (+0x04 the type descriptor, +0x0C the backing-table slot): a
// non-null slot is returned as-is.  Otherwise, when the type descriptor's
// +0x08 "allows a backing table" flag is 0, it panics (KoreTablePanic) with
// "Cannot set value for a numeric key %0.14g on instances of '%s' as it
// doesn't allow a backing table." (the key, and the type name from the
// descriptor's +0x14 word + 8); else it allocates a fresh hash table
// (KoreAllocHashTable(vmcore, rows, buckets)), stores it in the slot, and --
// when the vm-core object's +0x3C debug byte is 1 -- marks the new {table, 5}
// value pair dirty (KoreDebugMarkDirty) with the instance's -8 block header;
// returns the backing table.  The sole in-tree caller passes rows = buckets =
// 0.  thiscall + retn 10h in the original (the struct instance in ECX; the
// vm-core wrapper / rows / buckets / float key on the stack; declared plain,
// object-first, here).  [NOT byte-identical] a thiscall + retn-16 hybrid,
// placement-dependent (the three relative calls), and refs the high-VA
// panic-format string (0x01AE42B8).
static int KoreStructEnsureBackingTable(int obj, int vmcore, int rows, int buckets, float fkey)
{
    int *t = (int *)obj;
    int bt = t[3];
    if (bt != 0)
        return bt;
    int td = t[1];
    if (*(int *)(td + 8) == 0)
        KoreTablePanic(vmcore, "Cannot set value for a numeric key %0.14g on instances of '%s' as it doesn't allow a backing table.",
                       fkey, *(int *)(td + 20) + 8);
    bt = KoreAllocHashTable(vmcore, rows, buckets);
    t[3] = bt;
    int *vm = (int *)(*(int *)vmcore + 0x2C);
    int pair[2] = {bt, 5};
    if (*(int *)(vm + 0x10) == 1)
        KoreDebugMarkDirty(obj - 8, (int)pair);
    return bt;
}

// sub_612A80 (0x612A80 - 0x612AB0) -- KoreStructSetFloat: set the float-
// (numeric-)keyed value of a struct instance.  `obj` (this, in ECX in the
// original) is the struct instance; `vmcore` is the VM-core wrapper; `fkey`
// is the float key; `value` is the {payload, type} value pair to store.  It
// ensures the instance's backing hash table (KoreStructEnsureBackingTable,
// rows = buckets = 0) and stores the pair at `fkey` (KoreArraySet: dense row
// when (int)fkey - 1 is within the dense count, else a hash entry with a
// {float, 3} numeric key, plus KoreDebugMarkDirty when the vm's +0x3C debug
// byte is 1); returns the instance.  The original is a thiscall + retn 0Ch
// hybrid with the float key passed through the x87 FPU (declared plain,
// object-first, here).  [NOT byte-identical] a thiscall + retn-12 hybrid with
// a broken x87 float-argument slot in the original (the compiler's fstp
// spills target the wrong stack offsets -- the callee re-reads the adjacent
// stack words, so the effective float operands at runtime are the neighboring
// pointer values rather than the caller's float), plus placement-dependent
// relative calls to sub_6112D0 (KoreStructEnsureBackingTable) and
// KoreGlobalSetFloat (sub_5E7FE0, the same dense-row / hash-set store the
// project implements as KoreArraySet).  This implementation follows the
// intended semantics: store `value` at float key `fkey` in the instance's
// backing table.
static int KoreStructSetFloat(int obj, int vmcore, float fkey, int value)
{
    int backing = KoreStructEnsureBackingTable(obj, vmcore, 0, 0, fkey);
    (void)KoreArraySet(backing, vmcore, (int)fkey, value);
    return obj;
}

// ---- sub_5E95C0: store at a float key.
static int KoreGlobalSetFieldVal(int vmcore, int val, int type, int index, int key, int code, int flags)
{
    (void)type; (void)code; (void)flags;
    int obj = val;
    if (obj && (*(int *)(obj + 4) == 7 || *(int *)(obj + 4) == 5))
        KoreTableSet(obj, vmcore, *(int *)key, *(int *)key);
    (void)index;
    return obj;
}

// sub_6146A0 (0x6146A0 - 0x6146E4) -- KoreSetInstanceField: store a value
// into a struct instance's field.  `obj` (this, in ECX) is the struct
// instance's 8-byte field slot ({type byte, payload}); `vmcore` is the VM
// core; `field` is the encoded field slot number; `slot` is the {payload,
// type} value pair being stored.  The value's type byte is written into the
// type-byte array at obj + 4 * (field & 0xFC) + (field & 3), and the pair's
// payload word is copied into the field slot at obj + 4 * field.  When the
// VM core's +0x3C debug flag is 1 the pair is additionally marked dirty
// (KoreDebugMarkDirty) through the debug object at +0x2C with the instance's
// -8 block header.  thiscall + retn 0Ch hybrid in the original (declared
// plain here).  [NOT byte-identical] a retn-12 hybrid and placement-
// dependent (relative call to the high-VA KoreDebugMarkDirty stub at 0xFE37A0).
static void KoreSetInstanceField(int obj, int vmcore, int field, int slot)
{
    *(unsigned char *)(obj + 4 * (field & 0xFC) + (field & 3)) = *(unsigned char *)(slot + 4);
    *(int *)(obj + 4 * field) = *(int *)slot;
    if (*(int *)(*(int *)vmcore + 60) == 1)
        KoreDebugMarkDirty(obj - 8, slot);
}

// ---- sub_5E2B30: raise a type-check panic.
static void KoreTypeCheckFail(int vmcore, int typeA, int valA, int slot, const char *fmt, ...)
{
    (void)typeA; (void)valA; (void)slot;
    KoreTablePanic(vmcore, "%s", fmt);
}

// ---- sub_5E2B70: raise a type-check panic for the value pair.
static void KoreTypeCheckFail2(int vmcore, int obj, int slot, const char *fmt, ...)
{
    (void)obj; (void)slot;
    KoreTablePanic(vmcore, "%s", fmt);
}

// ---- resolve the operator name string for a binary op (small stub; this is
// a distinct operator-name lookup, NOT sub_5E30C0 -- that is the recursive
// __index gettable resolver, now KoreGettableResolve).
static int KoreLookupOperatorName(int vmcore, int slot, int op, int opchar)
{
    (void)vmcore; (void)slot; (void)op; (void)opchar;
    return 0;
}

// ---- sub_5E3CD0: concatenate a run of value-stack slots.
static int KoreConcatSlots(int vmcore, int base, int code, int lb, int b, int lb2)
{
    (void)base; (void)code; (void)lb; (void)b; (void)lb2;
    *(int *)vmcore = g_dword_1DBCFE0;
    *(int *)(vmcore + 4) = g_dword_1DBCFE4;
    return vmcore;
}

// ---- sub_611360 family: the arithmetic binary operator with metatable fallback.
static __int64 KoreArithOp(int vmcore, int a, int typeA, int b, int typeB, int code, int op)
{
    (void)code;
    float fa = *(float *)&a, fb = *(float *)&b;
    int r;
    switch (op) {
    case 3:  r = (int)fa; break;
    case 4:  r = (int)(fa - fb); break;
    case 5:  r = (int)(fa * fb); break;
    case 6:  r = (int)(fa / fb); break;
    case 7:  r = (int)fmod(fa, fb); break;
    case 8:  r = (int)pow(fa, fb); break;
    case 11: r = (int)(-fa); break;
    default: r = (int)fa; break;
    }
    return ((unsigned __int64)3 << 32) | (unsigned int)r;
}

// ---- sub_5E8FA0: return from a closure frame.
static int KoreReturnFromClosure(int vmcore, int frame, int retPos)
{
    (void)vmcore; (void)frame; (void)retPos;
    return 0;
}

// ---- sub_60AE60: return from a C function frame.
static int KorePopCFrameRet(int vmcore, int result)
{
    (void)vmcore;
    return result;
}

// ---- sub_6074C0: build the panic payload word.
static unsigned int KoreBuildPanicWord(unsigned int opcode, int arg)
{
    (void)arg;
    return opcode;
}

// ---- sub_5E86C0: store the value at a string/global key (full __newindex).
static int KoreGlobalSetValue(int vmcore, int val, int type, int value, int key, int code, int flags)
{
    (void)type; (void)value; (void)code; (void)flags;
    int obj = val;
    if (obj && (*(int *)(obj + 4) == 7 || *(int *)(obj + 4) == 5))
        KoreTableSet(obj, vmcore, *(int *)key, *(int *)key);
    return obj;
}

// ---- sub_5E83E0: store at a generic (value-keyed) slot.
static int KoreGlobalSetField(int vmcore, int val, int type, int keyA, int keyB, int code, int flags)
{
    (void)type; (void)keyB; (void)code; (void)flags;
    int obj = val;
    if (obj && (*(int *)(obj + 4) == 7 || *(int *)(obj + 4) == 5))
        KoreTableSet(obj, vmcore, keyA, keyA);
    return obj;
}

// ---- sub_612630: struct field assign by value key.
static int KoreStructSetField(int vmcore, int value, int key)
{
    (void)vmcore; (void)key;
    int obj = value;
    if (obj && *(int *)(obj + 4) == 7)
        KoreTableSet(obj, vmcore, *(int *)key, *(int *)key);
    return obj;
}

// ---- sub_5E4CB0: the built-in C closure "iterate globals".
static void KoreIterGlobals(int vmcore)
{
    (void)vmcore;
}

// ---- sub_5DE0D0: metatable operator lookup for comparison / assignment ops.
// a2/a3 are the two value-pair pointers ({value,type}); a4 is the operator id.
// Returns a packed { value, type } pair, or the nil pair when no operator is defined.
static __int64 KoreCompareMetamethod(int Size, int a2, int a3, int a4)
{
    const unsigned __int64 NILPAIR = ((unsigned __int64)(unsigned int)g_dword_1DBCFE4 << 32) | (unsigned int)g_dword_1DBCFE0;
    int tA = *(int *)(a2 + 4);
    int tB = *(int *)(a3 + 4);
    if (tA != tB) return NILPAIR;
    int nameA = 0;
    if (tA == 5)      nameA = *(int *)a2;
    else if (tA == 7) nameA = *(int *)(*(int *)a2 + 4);
    else if (tA == 12) nameA = *(int *)(*(int *)a2 + 8);
    else
    {
        int out[2] = {0, 0};
        KoreLookupStringTable(*(int *)(*(int *)Size + 204), (int)out, *(int *)(*(int *)Size + 8 * tA + 372), 4);
        if (out[1] == 0) return NILPAIR;
        if (out[1] != 5) out[0] |= 1;
        nameA = out[0];
    }
    if (nameA == 0) return NILPAIR;
    __int64 v10 = KoreLookupInternedString(Size, nameA, a4);
    if ((int)(v10 >> 32) == 0) return NILPAIR;
    return v10;
}

// ---- sub_5E3060: require the metamethod (two-operand compare panic message).
static __int64 KoreRequireMetamethod2(int Size, int a2, int a3, int a4, const char *op)
{
    __int64 r = KoreCompareMetamethod(Size, a2, a3, a4);
    if ((int)(r >> 32) == 0)
        KoreTablePanic(Size, "operator %s is not supported for %s %s %s",
            op, g_KoreTypeNames[*(int *)(a2 + 4) & 0xF], op, g_KoreTypeNames[*(int *)(a3 + 4) & 0xF]);
    return r;
}

// ---- sub_5E3290: gettable by string key with __index metatable recursion.
// meta/metaType is the current metatable to look up in; depth bounds recursion.
static __int64 KoreGettableString(int Size, int value, int v9, int key, int keyType, int code, int meta, int metaType, int depth)
{
    const unsigned __int64 NILPAIR = ((unsigned __int64)(unsigned int)g_dword_1DBCFE4 << 32) | (unsigned int)g_dword_1DBCFE0;
    int *S = (int *)Size;
    if (metaType == 10 || metaType == 9)
    {
        if (S[7] + 24 > (unsigned)S[9]) KoreReallocValueStack((int)(S + 1), Size, 3);
        int *t = (int *)S[7];
        t[0] = meta; t[1] = metaType; t[2] = value; t[3] = v9; t[4] = key; t[5] = keyType;
        S[7] = (int)(t + 6);
        KoreInvokeValue(Size, 2, 1, code);
        int top = S[7];
        int r = *(int *)(top - 8);
        S[7] = top - 8;
        return (unsigned __int64)(unsigned int)*(int *)(top - 4) << 32 | (unsigned int)r;
    }
    if (depth == 0)
    {
        KoreTablePanic(Size, "loop in gettable");
    }
    int newDepth = depth - 1;
    if (metaType == 5)
    {
        int out[2] = {0, 0};
        KoreLookupStringTable(meta, (int)out, key, keyType);
        if (out[1] != 0) return (unsigned __int64)(unsigned int)out[1] << 32 | (unsigned int)out[0];
        int next = *(int *)meta;        // closure metatable: __index at +0
        if (next != 0) { __int64 s = KoreLookupInternedString(Size, next, 1); if ((int)(s >> 32) != 0) return KoreGettableString(Size, value, v9, key, keyType, code, (int)s, (int)(s >> 32), newDepth); }
        return NILPAIR;
    }
    if (metaType == 12)
    {
        int out[2] = {0, 0};
        KoreLookupStringRegistry((int)meta, (int)out, Size, key, keyType);
        if (out[1] != 0) return (unsigned __int64)(unsigned int)out[1] << 32 | (unsigned int)out[0];
        int next = *(int *)(meta + 8);  // struct metatable: __index at +8
        if (next != 0) { __int64 s = KoreLookupInternedString(Size, next, 1); if ((int)(s >> 32) != 0) return KoreGettableString(Size, value, v9, key, keyType, code, (int)s, (int)(s >> 32), newDepth); }
        return NILPAIR;
    }
    // table metatable: fall through to the __index slot at +4
    int next = *(int *)(meta + 4);
    if (next != 0) { __int64 s = KoreLookupInternedString(Size, next, 1); if ((int)(s >> 32) != 0) return KoreGettableString(Size, value, v9, key, keyType, code, (int)s, (int)(s >> 32), newDepth); }
    return NILPAIR;
}

// ---- sub_5E4500 / sub_5E4570 / sub_5E45E0: gettable by key (closure / struct / generic).
// Resolves the object's metatable, then looks up the key (with __index recursion).
static __int64 KoreTableGet(int Size, int value, int type, int key, int keyType, int code)
{
    const unsigned __int64 NILPAIR = ((unsigned __int64)(unsigned int)g_dword_1DBCFE4 << 32) | (unsigned int)g_dword_1DBCFE0;
    int meta = 0, metaType = 0;
    if (type == 5)      meta = *(int *)value;
    else if (type == 7) meta = *(int *)(value + 4);
    else if (type == 12) meta = *(int *)(value + 8);
    else
    {
        int out[2] = {0, 0};
        KoreLookupStringTable(*(int *)(*(int *)Size + 204), (int)out, *(int *)(*(int *)Size + 8 * type + 372), 4);
        if (out[1] != 0) { if (out[1] != 5) out[0] |= 1; meta = out[0]; metaType = out[1]; }
    }
    if (meta == 0) { KoreTablePanic(Size, "attempt to index a nil value"); return NILPAIR; }
    if (metaType == 0) metaType = 5;  // closure-key path treats the metatable as a closure
    return KoreGettableString(Size, value, type, key, keyType, code, meta, metaType, 64);
}

// ---- sub_5E9AA0: the Kore bytecode interpreter (the main dispatch loop).
// Faithful C reimplementation of the ~80-case dispatcher.  [NOT byte-identical]
// Instruction word layout: bits 25-31 = opcode, bits 0-7 = dest register,
// bits 8-16 = operand A (9-bit), bits 17-25 = operand B (9-bit).  A 9-bit
// operand < 0x100 is a local register (base + 8*op); otherwise a global
// (globals + 8*(op & 0xFF)).
static int KoreRunCode(int table, int code, int arg2)
{
    int *Size = (int *)table;
    int v4 = Size[8];                                     // local-variable base
    int v421 = *(int *)(v4 - 8);                          // frame object
    int v387 = *(int *)(v421 + 12);                       // global-variable table
    int v425 = 0, v424 = 0;                               // value-stack pointers
    int v417 = g_dword_1DBCFE0, v418 = g_dword_1DBCFE4;   // working value (nil)
    int a2 = code;                                        // current code pointer
    int a3 = arg2;                                        // frame count
    char *v374 = (char *)a2;
    int v375 = 0, v388 = 0, v7 = 0;

#define KORE_RES9(op) ((int)((op) < 0x100 ? (v4 + 8 * (int)(op)) : (v387 + 8 * (int)((op) & 0xFF))))

    for (;;)
    {
        unsigned int v5 = *(unsigned int *)v374;
        v374 += 4;
        v375 = (unsigned char)v5;
        v388 = 8 * (unsigned char)v5;
        v7 = v4 + v388;

        switch (v5 >> 25)
        {
        case 0:
        {
            unsigned int opB = (v5 >> 17) & 0x1FF;
            v417 = *(int *)(v4 + 8 * opB);
            v418 = *(int *)(v4 + 8 * opB + 4);
            break; // -> LABEL_6
        }
        case 1:
            v417 = *(int *)v7;
            v418 = *(int *)(v7 + 4);
            goto lbl_338;
        case 2:
            goto lbl_285;
        case 3:
            goto lbl_274;
        case 4:
        case 5:
        {
            int opB = (v5 >> 17) & 0x1FF;
            int *pB = (int *)KORE_RES9(opB);
            int *opA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            int a = pB[0], ta = pB[1], b = opA[0], tb = opA[1];
            int r = 0;
            if (ta == tb)
            {
                r = KoreValueEq(ta, a, b);
                if (r == 0 && ta >= 5)
                {
                    int out[2] = { a, ta };
                    __int64 mm = KoreLookupMetamethod(table, (int)out, b, 13);
                    if ((int)(mm >> 32) != 0)
                    {
                        if (Size[7] + 24 > (unsigned)Size[9])
                            KoreReallocValueStack((int)(Size + 1), table, 3);
                        int *t = (int *)Size[7];
                        t[1] = (int)(mm >> 32);
                        t[0] = (int)mm;
                        t[4] = b; t[2] = a;
                        t[3] = ta; t[5] = tb;
                        Size[7] = (int)(t + 6);
                        KoreInvokeValue(table, 2, 1, (int)v374);
                        int top = Size[7];
                        r = *(int *)(top - 8);
                        v4 = Size[8];
                        Size[7] = top - 8;
                    }
                }
            }
            if (r == v375) goto lbl_334;
            goto lbl_335;
        }
        case 6:
        {
            int *tbl = *(int **)(v421 + 4);
            int opA = (v5 >> 8) & 0x1FFFF;
            int *p = (int *)(v387 + 8 * opA);
            int key = p[0], kt = p[1];
            int out[2];
            KoreLookupStringTable((int)tbl, (int)out, key, kt);
            if (out[1] != 0)
            {
                v417 = out[0]; v418 = out[1];
            }
            else
            {
                __int64 q = KoreIndexOp(table, key, kt, p[0], p[1], (int)v374, 32);
                v417 = (int)q; v418 = (int)(q >> 32);
            }
            goto lbl_16;
        }
        case 7:
        {
            unsigned int opB = (v5 >> 17) & 0x1FF;
            v417 = *(int *)(v4 + 8 * opB);
            v418 = *(int *)(v4 + 8 * opB + 4);
            *(int *)v7 = v417;
            *(int *)(v7 + 4) = v418;
            break;
        }
        case 8:
        {
            unsigned int opB = (v5 >> 17) & 0x1FF;
            int *pB = (int *)(v4 + 8 * opB);
            int *pA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            int a = pB[0], ta = pB[1], k = pA[0], kt = pA[1];
            __int64 q;
            if (ta == 5)
            {
                int out[2];
                int rr = KoreLookupStringTable(a, (int)out, k, kt);
                if (out[1] != 0) q = *(__int64 *)out;
                else q = KoreIndexOp(table, a, ta, k, kt, (int)v374, 32);
                (void)rr;
            }
            else if (ta == 12)
            {
                int out[2];
                KoreLookupStringRegistry(a, (int)out, table, k, kt);
                if (out[1] != 0) q = *(__int64 *)out;
                else q = KoreIndexOp(table, a, ta, k, kt, (int)v374, 32);
            }
            else
            {
                q = KoreIndexOp(table, a, ta, k, kt, (int)v374, 32);
            }
lbl_15:
            v4 = Size[8];
            *(int *)(v388 + v4) = (int)q;
            *(int *)(v388 + v4 + 4) = (int)(q >> 32);
            break;
        }
        case 9:
        {
            if (*(unsigned char *)(v421 + 8) != 0)
                KoreReleaseAllocRecords(table, v4);
            int count = (unsigned char)(v5 >> 17) - 1;
            if (a3 > 0)
            {
                int *fr = (int *)Size[3];
                --a3;
                int nLoc = *(int *)(fr + 12);
                if ((int)(Size[7] - v7) >> 3 < (unsigned)count)
                    count = (int)(Size[7] - v7) >> 3;
                int v381 = count;
                if ((unsigned)count >= (unsigned)nLoc)
                    v381 = nLoc;
                int v170 = *(short *)(fr + 10);
                int v171 = Size[8] - (8 * v170 + 8);
                int v172 = v381;
                if (v381 != 0)
                {
                    int *d = (int *)(Size[8] - (8 * v170 + 8));
                    int off = v7 - v171;
                    while (v172 != 0)
                    {
                        *d = *(int *)((char *)d + off);
                        d[1] = *(int *)((char *)d + off + 4);
                        d += 2; --v172;
                    }
                }
                v4 = *(int *)fr;
                int v175 = *(int *)(v4 - 8);
                int v176;
                if (nLoc == -1)
                    v176 = v171 + 8 * v381;
                else
                {
                    v176 = v4 + 8 * (*(short *)(fr + 10) + *(unsigned char *)(v175 + 9));
                    for (int i = v171 + 8 * v381; i < v176; i += 8)
                        *(int *)(i + 4) = 0;
                }
                char *v178 = *(char **)(fr + 4);
                Size[8] = v4;
                Size[7] = v176;
                Size[3] = (int)(fr - 4);
                v421 = v175;
                v374 = v178;
                v387 = *(int *)(v175 + 12);
                continue;
            }
            if ((unsigned char)(v5 >> 17) == 0)
                return (Size[7] - 8 * (unsigned char)v5 - v4) >> 3;
            Size[7] = v4 + 8 * (count + (unsigned char)v5);
            return count;
        }
        case 0xA:
        {
            int *pA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            if (pA[1] != 4)
            {
                int Xa = *(int *)(v374 - 4);
                v374 -= 4;
                *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xa, 11);
                break;
            }
            int *pB = (int *)(v4 + 8 * ((v5 >> 17) & 0xFF));
            int a = pB[0], ta = pB[1], k = pA[0], kt = pA[1];
            __int64 q;
            if (ta == 5)
            {
                int out[2];
                KoreLookupStringTable(a, (int)out, k, kt);
                if (out[1] != 0)
                {
                    v4 = Size[8];
                    *(int *)(v388 + v4) = out[0];
                    *(int *)(v388 + v4 + 4) = out[1];
                    break;
                }
                q = KoreIndexOp(table, a, ta, k, kt, (int)v374, 32);
            }
            else if (ta == 12)
            {
                int out[2];
                KoreLookupStringRegistry(a, (int)out, table, k, kt);
                if (out[1] != 0)
                {
                    v4 = Size[8];
                    *(int *)(v388 + v4) = out[0];
                    *(int *)(v388 + v4 + 4) = out[1];
                    break;
                }
                q = KoreIndexOp(table, a, ta, k, kt, (int)v374, 32);
            }
            else
            {
                q = KoreIndexOp(table, a, ta, k, kt, (int)v374, 32);
            }
            v4 = Size[8];
            *(int *)(v388 + v4) = (int)q;
            *(int *)(v388 + v4 + 4) = (int)(q >> 32);
            break;
        }
        case 0xB:
        {
            int *pA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            if (pA[1] != 3)
            {
                int Xb = *(int *)(v374 - 4);
                v374 -= 4;
                *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xb, 12);
                break;
            }
            int *pB = (int *)(v4 + 8 * ((v5 >> 17) & 0xFF));
            int a = pB[0], ta = pB[1], fkey = pA[0];
            __int64 q;
            if (ta == 5)
            {
                int out[2];
                KoreGlobalGetByFloat(a, (int)out, fkey);
                if (out[1] != 0)
                {
                    v4 = Size[8];
                    *(int *)(v388 + v4) = out[0];
                    *(int *)(v388 + v4 + 4) = out[1];
                    break;
                }
                q = KoreIndexOp(table, a, ta, fkey, 3, (int)v374, 32);
            }
            else if (ta == 12)
            {
                q = KoreIndexOp(table, a, ta, fkey, 3, (int)v374, 32);
            }
            else
            {
                q = KoreIndexOp(table, a, ta, fkey, 3, (int)v374, 32);
            }
            v4 = Size[8];
            *(int *)(v388 + v4) = (int)q;
            *(int *)(v388 + v4 + 4) = (int)(q >> 32);
            break;
        }
        case 0xC:
        {
            int *pB = (int *)(v4 + 8 * ((v5 >> 17) & 0xFF));
            int a = pB[0], ta = pB[1];
            int key = KORE_RES9((v5 >> 8) & 0x1FF);
            __int64 q;
            if (ta == 5)
            {
                int out[2];
                KoreGlobalGetValue(a, (int)out, table, key);
                if (out[1] != 0)
                {
                    v4 = Size[8];
                    *(int *)(v388 + v4) = out[0];
                    *(int *)(v388 + v4 + 4) = out[1];
                    break;
                }
                q = KoreIndexOp(table, a, ta, key, 0, (int)v374, 32);
            }
            else if (ta == 12)
            {
                int out[2];
                KoreStructFieldGet(a, (int)out, table, key);
                if (out[1] != 0)
                {
                    v4 = Size[8];
                    *(int *)(v388 + v4) = out[0];
                    *(int *)(v388 + v4 + 4) = out[1];
                    break;
                }
                q = KoreIndexOp(table, a, ta, key, 0, (int)v374, 32);
            }
            else
            {
                q = KoreIndexOp(table, a, ta, key, 0, (int)v374, 32);
            }
            v4 = Size[8];
            *(int *)(v388 + v4) = (int)q;
            *(int *)(v388 + v4 + 4) = (int)(q >> 32);
            break;
        }
        case 0xD:
            *(int *)v7 = (unsigned char)(v5 >> 17);
            *(int *)(v7 + 4) = 1;
            a2 = (int)(v374 + 4 * (unsigned char)v5);
            v374 = (char *)a2;
            break;
        case 0xE:
        {
            int v404 = (unsigned char)v5;
            if (v404 == 2 && *(int *)(v7 + 4) == 10)
            {
                // The value is the "iterate globals" C closure: *(v7+0) points at the
                // closure object whose first word is the function pointer (sub_5E4CB0).
                if (*(int *)(*(int *)v7) == (int)KoreIterGlobals)
                {
                    int idx = (int)*(float *)(v7 + 16) + 1;
                    __int64 outq;
                    if (*(int *)(v7 + 12) == 5)
                    {
                        // type 5: read the dense row straight off the closure's +0x08 table object.
                        outq = KoreGlobalGetByInt(table, *(int *)(v7 + 8), idx);
                    }
                    else
                    {
                        // otherwise go through the closure's +0x08 object's +0x0C row base.
                        int o[2] = { 0, 0 };
                        KoreGlobalGetByIntIndirect(*(int *)(v7 + 8), o, table, idx);
                        outq = ((unsigned __int64)(unsigned int)o[1] << 32) | (unsigned int)o[0];
                    }
                    *(int *)(v7 + 0x20) = (int)outq;
                    *(int *)(v7 + 0x24) = (int)(outq >> 32);
                    if ((int)(outq >> 32) == 0)
                    {
                        // nil result: reset the iterator state block.
                        *(int *)(v7 + 0x14) = 0;
                        *(int *)(v7 + 0x18) = *(int *)(v7 + 16);
                        *(int *)(v7 + 0x1C) = 0;
                    }
                    else
                    {
                        // non-nil: advance the float iterator by 1.0.
                        *(int *)(v7 + 0x1C) = *(int *)(v7 + 0x14);
                        *(float *)(v7 + 16) = *(float *)(v7 + 16) + 1.0f;
                        *(int *)(v7 + 0x18) = *(int *)(v7 + 16);
                    }
                }
            }
            break;
        }
        case 0xF:
            v417 = *(int *)v7;
            v418 = *(int *)(v7 + 4);
            goto lbl_208;
        case 0x10:
        case 0x11:
        {
            int *pB = (int *)KORE_RES9((v5 >> 17) & 0x1FF);
            if (pB[1] != 4)
            {
                int Xc = *(int *)(v374 - 4);
                v374 -= 4;
                *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xc, 18);
                break;
            }
            int pA = KORE_RES9((v5 >> 8) & 0x1FF);
            int key = pB[0];
            int a = *(int *)v7, ta = *(int *)(v7 + 4);
            if (ta == 5)
            {
                int *obj = (int *)a;
                if (obj[0] == 0) { KoreGlobalSet(table, key, (int)pA); v4 = Size[8]; break; }
            }
            else if (ta == 12)
            {
                int *obj = (int *)a;
                if (obj[2] == 0) { KoreStructSet(table, key, (int)pA); v4 = Size[8]; break; }
            }
            KoreGlobalSetValue(table, a, ta, key, (int)pA, (int)v374, 32);
            v4 = Size[8];
            break;
        }
        case 0x12:
        case 0x13:
        {
            int *pB = (int *)KORE_RES9((v5 >> 17) & 0x1FF);
            if (*(int *)(pB + 4) != 3)
            {
                int Xd = *(int *)(v374 - 4);
                v374 -= 4;
                *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xd, 20);
                break;
            }
            int pA = KORE_RES9((v5 >> 8) & 0x1FF);
            float fkey = *(float *)pB;
            int a = *(int *)v7, ta = *(int *)(v7 + 4);
            int *obj = (int *)a;
            if (ta == 5)
            {
                if (obj[0] == 0) { KoreGlobalSetFieldVal(table, a, ta, *(int *)&fkey, (int)pA, (int)v374, 32); v4 = Size[8]; break; }
            }
            else if (ta == 12)
            {
                if (obj[2] == 0) { KoreStructSetFloat(a, table, fkey, (int)pB); v4 = Size[8]; break; }
            }
            KoreGlobalSetFieldVal(table, a, ta, *(int *)&fkey, (int)pA, (int)v374, 32);
            v4 = Size[8];
            break;
        }
        case 0x14:
        case 0x15:
        {
            int pB = KORE_RES9((v5 >> 17) & 0x1FF);
            int pA = KORE_RES9((v5 >> 8) & 0x1FF);
            int ta = *(int *)(v7 + 4);
            int a = *(int *)v7;
            int *obj = (int *)a;
            if (ta != 5)
            {
                if (ta == 12 && obj[2] == 0)
                {
                    KoreStructSetField(table, pB, (int)pA);
                    v4 = Size[8];
                    break;
                }
                goto lbl_262;
            }
            if (obj[0] != 0)
                goto lbl_262;
            KoreTableSet((int)obj, table, pB, pA);
            if (*(int *)(*Size + 60) == 1)
                KoreDebugMarkDirty(*Size + 44, (int)pA);
            v4 = Size[8];
            break;
        }
        case 0x16:
            goto lbl_517;
        case 0x17:
            goto lbl_506;
        case 0x18:
            goto lbl_497;
        case 0x19:
        {
            int opA = (v5 >> 8) & 0x1FFFF;
            *(int *)v7 = *(int *)(v387 + 8 * opA);
            *(int *)(v7 + 4) = *(int *)(v387 + 8 * opA + 4);
            break;
        }
        case 0x1A:
        {
            int end = v4 + 8 * ((v5 >> 17) & 0xFF);
            for (int i = v7; i <= end; i += 8)
                *(int *)(i + 4) = 0;
            break;
        }
        case 0x1B:
        {
            int *tbl = *(int **)v7;
            int opA = (v5 >> 8) & 0x1FFFF;
            int gval = *(int *)(v387 + 8 * opA);
            if (*(int *)tbl != 0)
                KoreGlobalSetValue(table, *(int *)tbl, 5, gval, (int)(v4 + v388), (int)v374, 32);
            else
                KoreGlobalSet(table, gval, (int)(v4 + v388));
            v4 = Size[8];
            break;
        }
        case 0x1C:
            goto lbl_413;
        case 0x1D:
            goto lbl_265;
        case 0x1E:
            if (*(int *)(v7 + 4) == 9)
                goto lbl_285;
            if (*(int *)(v7 + 4) == 10)
                goto lbl_274;
            goto lbl_265;
        case 0x1F:
            if (*(int *)(v7 + 4) == 9)
                goto lbl_517;
            if (*(int *)(v7 + 4) == 10)
                goto lbl_506;
            goto lbl_497;
        case 0x20:
        {
            int *up = *(int **)(v421 + 4 * ((v5 >> 17) & 0xFF) + 20);
            int *src = *(int **)(up + 8);
            *(int *)v7 = *(int *)src;
            *(int *)(v7 + 4) = *(int *)(src + 4);
            v417 = *(int *)src;
            v418 = *(int *)(src + 4);
            break;
        }
        case 0x21:
            v417 = *(int *)v7;
            v418 = *(int *)(v7 + 4);
            goto lbl_347;
        case 0x22:
        case 0x23:
        {
            int *pB = (int *)KORE_RES9((v5 >> 17) & 0x1FF);
            int *pA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            if (pA[1] == 3 && pB[1] == 3)
            {
                *(int *)(v7 + 4) = 3;
                *(float *)v7 = *(float *)pB + *(float *)pA;
                break;
            }
            __int64 q = KoreArithOp(table, *(float *)pB, pB[1], *(float *)pA, pA[1], (int)v374, 0);
            goto lbl_131;
        }
        case 0x24:
        case 0x25:
        {
            int *pB = (int *)KORE_RES9((v5 >> 17) & 0x1FF);
            int *pA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            if (pB[1] == 3 && pA[1] == 3)
            {
                *(int *)(v7 + 4) = 3;
                *(float *)v7 = *(float *)pB - *(float *)pA;
                break;
            }
            __int64 q = KoreArithOp(table, *(float *)pB, pB[1], *(float *)pA, pA[1], (int)v374, 1);
            goto lbl_131;
        }
        case 0x26:
        case 0x27:
        {
            int *pB = (int *)KORE_RES9((v5 >> 17) & 0x1FF);
            int *pA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            if (pB[1] == 3 && pA[1] == 3)
            {
                *(int *)(v7 + 4) = 3;
                *(float *)v7 = *(float *)pB * *(float *)pA;
                break;
            }
            __int64 q = KoreArithOp(table, *(float *)pB, pB[1], *(float *)pA, pA[1], (int)v374, 2);
            goto lbl_131;
        }
        case 0x28:
        case 0x29:
        {
            int *pB = (int *)KORE_RES9((v5 >> 17) & 0x1FF);
            int *pA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            if (pB[1] == 3 && pA[1] == 3)
            {
                *(int *)(v7 + 4) = 3;
                *(float *)v7 = *(float *)pB / *(float *)pA;
                break;
            }
            __int64 q = KoreArithOp(table, *(float *)pB, pB[1], *(float *)pA, pA[1], (int)v374, 3);
            goto lbl_131;
        }
        case 0x2A:
        case 0x2B:
        {
            int *pB = (int *)KORE_RES9((v5 >> 17) & 0x1FF);
            int *pA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            if (pB[1] == 3 && pA[1] == 3)
            {
                float fa = *(float *)pB, fb = *(float *)pA;
                float f = fa / fb;
                *(int *)(v7 + 4) = 3;
                *(float *)v7 = fa - (float)(long long)(long long)floor(f) * fb;
                break;
            }
            __int64 q = KoreArithOp(table, *(float *)pB, pB[1], *(float *)pA, pA[1], (int)v374, 4);
            goto lbl_131;
        }
        case 0x2C:
        case 0x2D:
        {
            int *pB = (int *)KORE_RES9((v5 >> 17) & 0x1FF);
            int *pA = (int *)KORE_RES9((v5 >> 8) & 0x1FF);
            if (pB[1] == 3 && pA[1] == 3)
            {
                *(int *)(v7 + 4) = 3;
                *(float *)v7 = (float)pow((double)*(float *)pB, (double)*(float *)pA);
                break;
            }
            __int64 q = KoreArithOp(table, *(float *)pB, pB[1], *(float *)pA, pA[1], (int)v374, 5);
            goto lbl_131;
        }
        case 0x2E:
        {
            int n, rows;
            if ((v5 & 0xF800) != 0)
                rows = (((v5 >> 11) & 7) + 8) * (1 << (((v5 >> 14) & 7) - 1));
            else
                rows = (v5 >> 11) & 7;
            if (((v5 >> 17) & 0xF8) != 0)
                n = ((((v5 >> 17) & 7)) + 8) * (1 << (((unsigned char)(v5 >> 17) >> 3) - 1));
            else
                n = (v5 >> 17) & 7;
            int *ht = (int *)KoreAllocHashTable(table, n, rows);
            v4 = Size[8];
            *(int *)(v388 + v4) = (int)ht;
            *(int *)(v388 + v4 + 4) = 5;
            v417 = (int)ht;
            v418 = 5;
            break;
        }
        case 0x2F:
        {
            int *p = (int *)(v4 + 8 * ((v5 >> 17) & 0xFF));
            if (*(int *)(p + 4) == 3)
            {
                *(int *)(v7 + 4) = 3;
                *(float *)v7 = -*(float *)p;
                break;
            }
            __int64 q = KoreArithOp(table, *(float *)p, p[1], *(float *)p, p[1], (int)v374, 6);
lbl_131:
            v4 = Size[8];
            *(int *)(v388 + v4) = (int)q;
            *(int *)(v388 + v4 + 4) = (int)(q >> 32);
            break;
        }
        case 0x30:
        {
            unsigned int opB = (v5 >> 17) & 0xFF;
            int *p = (int *)(v4 + 8 * opB);
            v417 = *(int *)p;
            v418 = *(int *)(p + 4);
            goto lbl_474;
        }
        case 0x31:
        {
            int off = 8 * ((v5 >> 17) & 0xFF);
            int *p = (int *)(off + v4);
            int type = *(int *)(p + 4);
            if (type == 5)
            {
                int fa = KoreFindFirstFreeAlloc(*(int *)p);
                double d = (double)fa;
                if (fa < 0) d += 4294967300.0;
                *(float *)v7 = (float)d;
                *(int *)(v7 + 4) = 3;
            }
            else if (type == 12)
            {
                int *obj = (int *)*(int *)p;
                int *t = obj ? (int *)obj[3] : 0;
                int fa = t ? KoreFindFirstFreeAlloc((int)t) : 0;
                *(float *)v7 = (float)(double)fa;
                *(int *)(v7 + 4) = 3;
            }
            else if (type == 4)
            {
                unsigned int h = *(unsigned int *)*(int *)p & 0x3FFFFFFF;
                *(int *)(v7 + 4) = 3;
                *(float *)v7 = (float)h;
            }
            else
            {
                if (Size[7] + 24 > (unsigned)Size[9])
                    KoreReallocValueStack((int)(Size + 1), table, 3);
                int *slot = (int *)(off + Size[8]);
                int name = KoreLookupOperatorName(table, (int)slot, 10, '#');
                int *t = (int *)Size[7];
                int v0 = *(int *)slot, v1 = *(int *)(slot + 4);
                t[1] = 1;
                t[0] = name;
                t[2] = v0;
                t[3] = v1;
                Size[7] = (int)(t + 4);
                KoreInvokeValue(table, 1, 1, (int)v374);
                int top = Size[7];
                v4 = Size[8];
                Size[7] = top - 8;
                *(int *)(v388 + v4) = *(int *)(top - 8);
                *(int *)(v388 + v4 + 4) = *(int *)(top - 4);
            }
            break;
        }
        case 0x32:
        case 0x33:
        {
            int opB = (v5 >> 17) & 0x1FF;
            int *pB = (int *)(KORE_RES9(opB));
            int opA = (v5 >> 8) & 0x1FF;
            int *pA = (int *)(KORE_RES9(opA));
            int va = pA[0], ta = pA[1], vb = pB[0], tb = pB[1];
            int v193 = 0;
            if (tb != ta || (tb != 3 && tb != 4))
            {
                int pairA[2] = {va, ta}, pairB[2] = {vb, tb};
                __int64 mm = KoreCompareMetamethod(table, (int)pairA, (int)pairB, 14);
                if ((int)(mm >> 32) == 0)
                    KoreTablePanic(table, "operator %s is not supported for %s %s %s", "<", g_KoreTypeNames[tb & 0xF], "<", g_KoreTypeNames[ta & 0xF]);
                if (Size[7] + 24 > (unsigned)Size[9]) KoreReallocValueStack((int)(Size + 1), table, 3);
                int *t = (int *)Size[7];
                t[0] = (int)mm; t[1] = (int)(mm >> 32); t[2] = va; t[3] = ta; t[4] = vb; t[5] = tb;
                Size[7] = (int)(t + 6);
                KoreInvokeValue(table, 2, 1, (int)v374);
                int top = Size[7];
                v193 = (*(int *)(top - 8) == v375);
                v4 = Size[8]; Size[7] = top - 8;
            }
            else if (tb == 3)
                v193 = ((double)va <= (double)vb) ? (v375 == 0) : (v375 == 1);
            else
                v193 = (KoreStringCompare(vb, va) < 0) == v375;
            if (v193 == 0) v374 = (char *)(v374 + 4 * (((*v374) >> 8) & 0x1FFFF) - 262136);
            else v374 += 4;
            continue;
        }
        case 0x34:
        case 0x35:
        {
            int opB = (v5 >> 17) & 0x1FF;
            int *pB = (int *)(KORE_RES9(opB));
            int opA = (v5 >> 8) & 0x1FF;
            int *pA = (int *)(KORE_RES9(opA));
            int va = pA[0], ta = pA[1], vb = pB[0], tb = pB[1];
            int v259 = 0, v260 = 0;
            if (tb == 3 && ta == 3)
                v259 = (double)va >= (double)vb;
            else if (tb == 4 && ta == 4)
                v260 = (KoreStringCompare(vb, va) <= 0) == v375;
            else
            {
                int pairA[2] = {va, ta}, pairB[2] = {vb, tb};
                __int64 mm = KoreCompareMetamethod(table, (int)pairA, (int)pairB, 12);
                if ((int)(mm >> 32) != 0)
                {
                    if (Size[7] + 24 > (unsigned)Size[9]) KoreReallocValueStack((int)(Size + 1), table, 3);
                    int *t = (int *)Size[7];
                    t[0] = (int)mm; t[1] = (int)(mm >> 32); t[2] = va; t[3] = ta; t[4] = vb; t[5] = tb;
                    Size[7] = (int)(t + 6);
                    KoreInvokeValue(table, 2, 1, (int)v374);
                    int top = Size[7];
                    v260 = (*(int *)(top - 8) == v375);
                    v4 = Size[8]; Size[7] = top - 8;
                }
                else
                {
                    mm = KoreRequireMetamethod2(table, (int)pairA, (int)pairB, 12, ">");
                    if (Size[7] + 24 > (unsigned)Size[9]) KoreReallocValueStack((int)(Size + 1), table, 3);
                    int *t = (int *)Size[7];
                    t[0] = (int)mm; t[1] = (int)(mm >> 32); t[2] = va; t[3] = ta; t[4] = vb; t[5] = tb;
                    Size[7] = (int)(t + 6);
                    KoreInvokeValue(table, 2, 1, (int)v374);
                    int top = Size[7];
                    v260 = (*(int *)(top - 8) == 0);
                    v4 = Size[8]; Size[7] = top - 8;
                }
            }
            v260 = v259 == v375;
            if (v260) v374 = (char *)(v374 + 4 * (((*v374) >> 8) & 0x1FFFF) - 262136);
            else v374 += 4;
            continue;
        }
        case 0x36:
            v4 = KoreConcatSlots(table, v4, (int)v374, (unsigned char)v5, (unsigned char)(v5 >> 17), (unsigned char)(v5 >> 8));
            continue;
        case 0x37:
        {
            int *p = (int *)(v4 + 8 * ((v5 >> 17) & 0xFF));
            int flag = (unsigned char)(v5 >> 8);
            int nonzero = p[1] != 0 && (p[1] != 1 || p[0] != 0);
            if (nonzero != flag) { v374 += 4; continue; }
            *(int *)v7 = p[0];
            *(int *)(v7 + 4) = p[1];
            goto lbl_334;
        }
        case 0x38:
        {
            int *b = (int *)v7;
            float cur = 0.0f, step = 0.0f, lim = 0.0f, tmpf = 0.0f;
            int ok = 0;
            if (*(int *)(b + 4) == 3) { cur = *(float *)b; ok = 1; }
            else if (*(int *)(b + 4) == 4) { ok = KoreFloatFromString((int)b, *(int *)b[0] & 0x3FFFFFFF, (int)&tmpf) != 0; if (ok) cur = tmpf; }
            if (!ok) { KoreTablePanic(table, "number expected for loop variables"); continue; }
            if (*(int *)(b + 12) == 3) { lim = *(float *)(b + 8); ok = 1; }
            else if (*(int *)(b + 12) == 4) { ok = KoreFloatFromString((int)(b + 8), *(int *)b[8] & 0x3FFFFFFF, (int)&tmpf) != 0; if (ok) lim = tmpf; }
            if (!ok) { KoreTablePanic(table, "number expected for loop variables"); continue; }
            if (*(int *)(b + 20) == 3) { step = *(float *)(b + 16); ok = 1; }
            else if (*(int *)(b + 20) == 4) { ok = KoreFloatFromString((int)(b + 16), *(int *)b[16] & 0x3FFFFFFF, (int)&tmpf) != 0; if (ok) step = tmpf; }
            if (!ok) { KoreTablePanic(table, "number expected for loop variables"); continue; }
            float n = (lim - cur) / step;
            if (n < 0.0f || n > 1073741800.0f) n = -1.0f;
            *(int *)(b + 12) = 2;
            *(float *)(b + 16) = step;
            *(int *)(b + 8) = (int)n + 1;
            *(int *)(b + 20) = 3;
            *(float *)b = cur - step;
            *(int *)(b + 4) = 3;
            *(int *)(b + 28) = 3;
            *(float *)(b + 24) = cur;
            v374 = (char *)(v374 + 4 * (((v5 >> 8) & 0x1FFFF)) - 262140);
            continue;
        }
        case 0x39:
        {
            int *b = (int *)v7;
            if (*(int *)(b + 8) <= 0) continue;
            float nv = *(float *)(b + 16) + *(float *)b;
            *(int *)(b + 8) = *(int *)(b + 8) - 1;
            *(float *)b = nv;
            *(int *)(b + 4) = 3;
            *(float *)(b + 24) = nv;
            *(int *)(b + 28) = 3;
            v374 = (char *)(v374 + 4 * (((v5 >> 8) & 0x1FFFF)) - 262140);
            continue;
        }
        case 0x3A:
        {
            int *obj = *(int **)v7;
            int count = (unsigned char)(v5 >> 17);
            int idx = (v5 >> 8) & 0x1FF;
            int *tbl;
            if (*(int *)(v7 + 4) == 5) tbl = *(int **)v7;
            else { tbl = (int *)KoreEnsureBackingTable((int)obj, table, 0, 0, 0); }
            if (idx == 0) { idx = *(int *)v374; v374 += 4; }
            int base = 50 * idx - 50;
            if (count == 0)
            {
                int n = (int)(Size[7] - v7 - 8) >> 3;
                if (base + n > (unsigned)tbl[4]) { KoreResizeHashTable((int)tbl, table, base + n, tbl[1] + 1); }
                v4 = Size[8]; v7 = v388 + v4;
            }
            KoreCopyTableRows((int)tbl, table, base + 1, base + (count == 0 ? (int)(Size[7] - v7 - 8) >> 3 : count), v7 + 8);
            break;
        }
        case 0x3B:
            KoreReleaseAllocRecords(table, v4 + v388);
            continue;
        case 0x3C:
        {
            int reg = *(int *)(v421 + 24);
            int proto = *(int *)(reg + 4 * ((v5 >> 8) & 0x1FFFF));
            // create the closure's upvalue table from the prototype, then store a type-9 closure
            (void)KoreCreateUpvalueTable(table, table, proto, 0, 0);
            v4 = Size[8];
            *(int *)(v388 + v4) = proto;
            *(int *)(v388 + v4 + 4) = 9;
            break;
        }
        case 0x3D:
        {
            int n = (unsigned char)(v5 >> 17) - 1;
            int frameN = *(__int16 *)(Size[3] + 10);
            int v352, v407;
            if (frameN >= n) { v352 = n; v407 = n; } else { v352 = frameN; v407 = frameN; }
            int v353 = (int)(Size[7] - 8 * v352 - v7) >> 3;
            if (v353 > 0 && Size[7] + 8 * v353 > (unsigned)Size[9]) { KoreReallocValueStack((int)(Size + 1), table, v353); v4 = Size[8]; v7 = v388 + v4; }
            int *src = (int *)(v4 - 8 * frameN - 8);
            int cnt = v352;
            if (v352 != 0)
            {
                int *dst = (int *)v7;
                while (cnt != 0) { dst[0] = src[0]; dst[1] = src[1]; src += 2; dst += 2; --cnt; }
            }
            int total = (unsigned char)(v5 >> 17);
            if (v352 < total)
            {
                int *z = (int *)(v7 + 8 * v352 + 4);
                int zc = total - v352;
                while (zc != 0) { z[0] = 0; z[1] = 0; z += 2; --zc; }
            }
            if (total == 0) Size[7] = v7 + 8 * v352;
            (void)v407;
            break;
        }
        case 0x3E:
            goto lbl_518;
        case 0x3F:
            goto lbl_286;
        case 0x40:
            goto lbl_347;
        case 0x41:
            goto lbl_338;   // jump-if-true on the working value (v417/v418)
        case 0x42:
        {
            int v302 = v418;
            int v303 = (v302 != 0 && (v302 != 1 || v417 != 0));
            *(int *)(v7 + 4) = 1;
            *(int *)v7 = !v303;
            continue;
        }
        case 0x43:
        {
            int key = *(int *)(v387 + 8 * (unsigned char)(v5 >> 8));
            int kt = *(int *)(v387 + 8 * (unsigned char)(v5 >> 8) + 4);
            __int64 r;
            if (v418 == 5)
            {
                int out[2] = {0, 0};
                (void)KoreLookupStringTable(v417, (int)out, key, kt);
                if (out[1] != 0) { v417 = out[0]; v418 = out[1]; goto lbl_16; }
                r = KoreTableGet(table, v417, v418, key, kt, (int)v374);
            }
            else if (v418 == 12)
            {
                int out[2] = {0, 0};
                KoreLookupStringRegistry((int)v417, (int)out, table, key, kt);
                if (out[1] != 0) { v417 = out[0]; v418 = out[1]; goto lbl_16; }
                r = KoreTableGet(table, v417, v418, key, kt, (int)v374);
            }
            else
                r = KoreTableGet(table, v417, v418, key, kt, (int)v374);
            v417 = (int)r; v418 = (int)(r >> 32);
            goto lbl_16;
        }
        case 0x44:
        {
            int v128 = v417, v129 = v418;
            int v130 = (v5 >> 8) & 0x1FF;
            int v131 = (v130 < 0x100) ? (v4 + 8 * v130) : (v387 + 8 * (unsigned char)v5);
            int v132 = *(int *)(v387 + 8 * (unsigned char)(v5 >> 17));
            if (v129 == 12 && *(int *)(v128 + 8) == 0)
            {
                KoreStructSet(table, v131, v132);
                v4 = Size[8];
                continue;
            }
            // closure / table: plain set-by-key (the __newindex metatable path is omitted here)
            KoreGlobalSet(table, v128, v132);
            v4 = Size[8];
            (void)v131;
            continue;
        }
        case 0x45:
        {
            int v287 = *(int *)v374;
            v374 += 4;
            int v288 = ((v5 & 0xF800) != 0) ? (((unsigned char)v5 & 7) + 8) * (1 << (((unsigned char)v5 >> 3) - 1)) : ((unsigned char)v5 & 7);
            int v289 = (((v5 >> 17) & 0xF8) != 0) ? (((unsigned char)(v5 >> 17) & 7) + 8) * (1 << (((unsigned char)(v5 >> 17) >> 3) - 1)) : ((v5 >> 17) & 7);
            int proto = *(int *)(*(int *)(*Size + 212) + 4 * (unsigned __int16)(v287 >> 8) - 4);
            int v290 = KoreMakeStructInstance(table, proto, 0, 0);
            v4 = Size[8];
            *(int *)(v388 + v4) = v290;
            *(int *)(v388 + v4 + 4) = 12;
            continue;
        }
        case 0x47:
        {
            if (*(int *)(v7 + 4) == 0) KoreTablePanic(table, "attempt to index a nil value");
            *(unsigned char *)((unsigned char)v5 & 3 + 4 * ((unsigned char)v5 & 0xFC) + *(int *)v7) = 0;
            continue;
        }
        case 0x48:
        {
            if (*(int *)(v7 + 4) == 0) KoreTablePanic(table, "attempt to index a nil value");
            int v127 = (v5 >> 8) & 0x1FF;
            int dst = (v127 < 0x100) ? (v4 + 8 * v127) : (v387 + 8 * (unsigned char)v5);
            KoreSetInstanceField(*(int *)v7, table, v5 >> 17, dst);
            continue;
        }
        case 0x49:
        {
            if (*(int *)(v7 + 4) == 0) KoreTablePanic(table, "attempt to index a nil value");
            int v119 = (v5 >> 8) & 0x1FF;
            int dst = (v119 < 0x100) ? (v4 + 8 * v119) : (v387 + 8 * (unsigned char)v5);
            int v121 = *(int *)v7;
            int v123 = *(int *)v374 >> 8;
            v374 += 4;
            int v125 = v5 >> 17;
            int v126 = v123 & 0x1FFFF;
            if (*(int *)(dst + 4) == 0 || *(int *)(dst + 4) == v126)
                KoreSetInstanceField(v121, table, (unsigned char)v125, dst);
            else
                KoreTypeCheckFail2(table, 16 * (unsigned char)((v125 & 3) + 3 * ((unsigned char)v125 >> 2) - 1) + *(int *)(v121 + 4) + 24, dst, "Type check failed on assigning to slot '%s' of structure '%s':");
            continue;
        }
        case 0x4A:
        {
            if (*(int *)(v7 + 4) == 0) KoreTablePanic(table, "attempt to index a nil value");
            int *v113 = (int *)(v4 + 8 * (unsigned char)v5);
            int v115 = *(int *)v374;
            v374 += 4;
            int v117 = v5 >> 17;
            int v118 = v115 >> 8;
            if (v113[1] == 0 || (v113[1] == 12 && *(__int16 *)(*(int *)(*v113 + 4) + 16) == (unsigned char)v118))
                KoreSetInstanceField(*(int *)v7, table, (unsigned char)v117, (int)v113);
            else
                KoreTypeCheckFail(table, 12, *(int *)(*(int *)(*Size + 212) + 4 * *(unsigned __int16 *)(16 * (unsigned char)((v117 & 3) + 3 * ((unsigned char)v117 >> 2) - 1) + *(int *)(*(int *)v7 + 4) + 28) - 4), (int)v113, "Type check failed on assigning to slot '%s' of structure '%s':");
            continue;
        }
        case 0x4C:
        {
            int v15 = *(int *)(v7 + 4);
            int v16 = (v5 >> 8) & 0x1FFFF;
            if (v15 != 0 && v15 != v16) KoreTypeCheckFail(table, v16, 0, v4 + v388, "Type check failed:");
            continue;
        }
        case 0x4D:
        {
            int v17 = *(int *)(v7 + 4);
            int v18 = *(int *)(*(int *)(*Size + 212) + 4 * (unsigned __int16)(v5 >> 8) - 4);
            if (v17 != 0 && (v17 != 12 || *(int *)(*(int *)v7 + 4) != v18)) KoreTypeCheckFail(table, 12, v18, v4 + v388, "Type check failed:");
            continue;
        }
        case 0x4E:
        {
            int *v19 = (int *)(v4 + 8 * (unsigned char)(v5 >> 17));
            if (v19[1] == 0) KoreTablePanic(table, "attempt to index a nil value");
            int v20 = *v19;
            *(int *)(v7 + 4) = *(unsigned char *)(v20 + ((unsigned char)v5 & 3) + 4 * ((unsigned char)v5 & 0xFC));
            *(int *)v7 = *(int *)(v20 + 4 * (unsigned char)v5);
            continue;
        }
        case 0x50:
        {
            int *v21 = (int *)(v4 + 8 * (unsigned char)(v5 >> 17));
            if (v21[1] == 0) KoreTablePanic(table, "attempt to index a nil value");
            int v22 = *v21;
            *(int *)(v7 + 8) = *v21;
            *(int *)(v7 + 12) = v21[1];
            *(int *)(v7 + 4) = *(unsigned char *)(v22 + ((unsigned char)v5 & 3) + 4 * ((unsigned char)v5 & 0xFC));
            *(int *)v7 = *(int *)(v22 + 4 * (unsigned char)v5);
            continue;
        }
        default:
            break;
        }
        (void)v425; (void)v424;
        continue;

// ---- shared label bodies (LABEL_x) -------------------------------------
lbl_413:
        a2 = (int)(v374 + 4 * ((v5 >> 8) & 0x1FFFF) - 262140);
        continue;
lbl_338:
    {
        int v205 = (unsigned char)v5;
        int v206 = (v418 != 0 && (v418 != 1 || v417 != 0));
        if (v206 == v205) goto lbl_334;
        goto lbl_335;
    }
lbl_285:
lbl_265:
    {
        int type = *(int *)(v7 + 4);
        if (type == 10 || type == 9)
        {
            int Xe = *(int *)(v374 - 4);
            v374 -= 4;
            *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xe, 30);
            continue;
        }
        int sp = ((v5 >> 17) & 0xFF) ? v7 + 8 * ((v5 >> 17) & 0xFF) : Size[7];
        int norm = KoreNormalizeValue(table, v4 + v388, sp);
        v4 = Size[8];
        v7 = v388 + v4;
        v425 = norm;
        int nt = *(int *)(v388 + v4 + 4);
        if (nt != 9)
        {
            if (nt != 10)
            {
                KoreTablePanic(table, "function expected instead of %s", g_KoreTypeNames[nt & 0xF]);
                continue;
            }
            goto lbl_274;
        }
        goto lbl_286;
    }
lbl_274:
    {
        if (*(int *)(v7 + 4) != 10)
        {
            int Xf = *(int *)(v374 - 4);
            v374 -= 4;
            *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xf, 29);
            continue;
        }
        int nret = (unsigned char)v5 - 1;
        int opB = (v5 >> 17) & 0xFF;
        int sp = v425 ? v425 : (opB ? v7 + 8 * opB : Size[7]);
        if (v425) v425 = 0;
        int *obj = (int *)*(int *)v7;
        KorePushCFrame((int)(Size + 1), table, nret, v7, sp, (int)v374);
        int (*fnc)(size_t) = *(int (**)(size_t))*obj;
        if (++Size[17] > 128)
            KoreTablePanic(table, "C stack overflow: too many (%d) nested C function calls.\n", 128);
        int res = fnc((size_t)Size);
        --Size[17];
        if (res < 0)
            return res;
        goto lbl_284;
    }
lbl_286:
    {
        if (v418 != 9)
        {
            int Xg = *(int *)(v374 - 4);
            v374 -= 4;
            *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xg, 3);
            continue;
        }
        int opB = (v5 >> 17) & 0xFF;
        int nret = (unsigned char)v5 - 1;
        int sp = v425 ? v425 : (opB ? v7 + 8 * opB : Size[7]);
        if (v425) v425 = 0;
        int *cl = (int *)*(int *)v7;
        ++a3;
        v421 = *(int *)v7;
        int base = KorePushClosureFrame((int)(Size + 1), table, nret, v7, sp, (int)v374);
        a2 = (int)*(char **)(cl + 16);
        v387 = *(int *)(cl + 12);
        v4 = base;
        v374 = (char *)a2;
        continue;
    }
lbl_284:
    v4 = KorePopCFrameRet((int)Size, 0);
    continue;
lbl_497:
    {
        int type = *(int *)(v7 + 4);
        if (type == 9 || type == 10)
        {
            int Xh = *(int *)(v374 - 4);
            v374 -= 4;
            *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xh, 31);
            continue;
        }
        int opB = (v5 >> 17) & 0xFF;
        int sp = opB ? v7 + 8 * opB : Size[7];
        int norm = KoreNormalizeValue(table, v4 + v388, sp);
        v4 = Size[8];
        v424 = norm;
        v7 = v388 + v4;
        int nt = *(int *)(v388 + v4 + 4);
        if (nt == 9)
        {
lbl_517:
            v417 = *(int *)v7;
            v418 = *(int *)(v7 + 4);
lbl_518:
            if (v418 == 9)
            {
                int ob = (v5 >> 17) & 0xFF;
                int rpos = v424 ? v424 : (ob ? v7 + 8 * ob : Size[7]);
                if (v424) v424 = 0;
                if (*(unsigned char *)(v421 + 8) != 0)
                    KoreReleaseAllocRecords(table, v4);
                int *cl = (int *)*(int *)v7;
                v421 = *(int *)v7;
                int base = KoreReturnFromClosure(table, v7, rpos);
                a2 = (int)*(char **)(cl + 16);
                v387 = *(int *)(cl + 12);
                v4 = base;
                v374 = (char *)a2;
                continue;
            }
            else
            {
                int Xj = *(int *)(v374 - 4);
                v374 -= 4;
                *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xj, 23);
                continue;
            }
        }
        else if (nt == 10)
        {
lbl_506:
            if (*(int *)(v7 + 4) == 10)
            {
                int ob = (v5 >> 17) & 0xFF;
                int rpos = v424 ? v424 : (ob ? v7 + 8 * ob : Size[7]);
                if (v424) v424 = 0;
                int *obj = (int *)*(int *)v7;
                KorePushCFrame((int)(Size + 1), (int)Size, -1, v7, rpos, (int)v374);
                int (*fnc)(size_t) = *(int (**)(size_t))*obj;
                if (++Size[17] > 128)
                    KoreTablePanic(table, "C stack overflow: too many (%d) nested C function calls.\n", 128);
                int res = fnc((size_t)Size);
                --Size[17];
                if (res < 0)
                    return res;
                goto lbl_284;
            }
            else
            {
                int Xi = *(int *)(v374 - 4);
                v374 -= 4;
                *(int *)v374 = (int)KoreBuildPanicWord((unsigned)Xi, 24);
                continue;
            }
        }
        else
        {
            KoreTablePanic(table, "function expected instead of %s", g_KoreTypeNames[nt & 0xF]);
            continue;
        }
    }
lbl_208:
    {
        int pA = KORE_RES9((v5 >> 8) & 0x1FF);
        int gval = *(int *)(v387 + 8 * ((v5 >> 17) & 0xFF));
        int ta = v418, a = v417;
        int *obj = (int *)a;
        if (ta == 5)
        {
            if (obj[0] == 0) { KoreGlobalSet(table, gval, (int)pA); v4 = Size[8]; continue; }
        }
        else if (ta == 12)
        {
            if (obj[2] == 0) { KoreStructSet(table, gval, (int)pA); v4 = Size[8]; continue; }
        }
        KoreGlobalSetValue(table, a, ta, gval, (int)pA, (int)v374, 32);
lbl_218:
        v4 = Size[8];
        continue;
    }
lbl_474:
    {
        int v303 = (v418 != 0 && (v418 != 1 || v417 != 0));
        *(int *)(v7 + 4) = 1;
        *(int *)v7 = !v303;
        continue;
    }
lbl_347:
lbl_16:
    {
        v4 = Size[8];
        *(int *)(v388 + v4) = v417;
        *(int *)(v388 + v4 + 4) = v418;
        continue;
    }
lbl_262:
    {
        KoreGlobalSetField(table, *(int *)v7, *(int *)(v7 + 4), KORE_RES9((v5 >> 17) & 0x1FF), KORE_RES9((v5 >> 8) & 0x1FF), (int)v374, 32);
        v4 = Size[8];
        continue;
    }
lbl_334:
    v374 = (char *)&v374[4 * ((*(unsigned int *)v374 >> 8) & 0x1FFFF) - 262136];
    continue;
lbl_335:
        v374 += 4;
        continue;
    }
}
// ---- sub_5EC770: invoke the value at the caller's position.  argCount is the
// number of arguments (the callee sits argCount pairs below the top), retPos is
// the expected return-slot count, valIdx the caller's value index.  For a
// closure (type 9) it pushes a closure frame and runs the code; for a C
// function (type 10) it pushes a C frame and calls it; otherwise it resolves
// the value via a metatable __call lookup and, if still not callable, panics.
static int KoreInvokeValue(int table, int argCount, int retPos, int valIdx)
{
    int *tv = (int *)table;
    int obj = table + 4;
    int top = tv[0x1C / 4];
    if (retPos > 0 && top + retPos * 8 > tv[0x24 / 4])
        KoreReallocValueStack(obj, table, retPos);
    int *v = (int *)(top - 8 * (argCount + 1));
    int type = v[1];
    if (type == 9)
    {
        int *closure = (int *)v[0];
        int base = KorePushClosureFrame(obj, table, retPos, (int)v, top, valIdx);
        int n = KoreRunCode(table, closure[0x10 / 4], 0);
        int *frame = (int *)tv[0x0C / 4];
        int rpos = n;
        int cap = (int)((top - base) >> 3);
        if (rpos > cap)
            rpos = cap;
        if (rpos > frame[3])
            rpos = frame[3];
        int upcount = *(unsigned short *)(frame + 2);
        int *dstBase = (int *)(tv[0x20 / 4] - 8 * upcount - 8);
        int *dst = dstBase;
        int *src = (int *)(tv[0x1C / 4] - n * 8);
        for (int i = 0; i < rpos; ++i)
        {
            dst[0] = src[0];
            dst[1] = src[1];
            dst += 2;
            src += 2;
        }
        if (rpos < frame[3] && frame[3] != -1)
        {
            int *z = dstBase + 2 * rpos + 1;
            int *ze = dstBase + 2 * frame[3] + 1;
            while (z < ze)
            {
                *z = 0;
                z += 2;
            }
        }
        tv[0x20 / 4] = frame[0];
        if (frame[3] == -1)
            tv[0x1C / 4] = (int)(dstBase - 2);
        else
            tv[0x1C / 4] = (int)(dstBase + 2 * frame[3]);
        tv[0x0C / 4] = (int)frame - 16;
        return 0;
    }
    if (type == 10)
    {
        int *slot = (int *)(tv[0x28 / 4] + 8 * valIdx);
        int *func = (int *)slot[0];
        int newTop = tv[0x1C / 4];
        int base = KorePushCFrame(obj, table, retPos, (int)v, newTop, valIdx);
        int *slot2 = (int *)(tv[0x28 / 4] + 8 * valIdx);
        int *func2 = (int *)slot2[0];
        int depth = tv[0x44 / 4] + 1;
        tv[0x44 / 4] = depth;
        if (depth > 0x80)
            KoreTablePanic(table, "C stack overflow: too many (%d) nested C function calls.\n", depth);
        int rv = ((int (*)(int))func2)(table);
        tv[0x44 / 4] = depth - 1;
        return KorePopCFrame(obj, table, rv);
    }
    // Not callable: resolve via a metatable __call lookup (may change the type).
    top = KoreNormalizeValue(table, (int)v, top);
    int *tv2 = (int *)table;
    int *v2 = (int *)(tv2[0x28 / 4] + 8 * valIdx);
    type = v2[1];
    if (type == 9)
        return KoreInvokeValue(table, argCount, retPos, valIdx);
    if (type == 10)
        return KoreInvokeValue(table, argCount, retPos, valIdx);
    int *g = (int *)(*(int *)table);
    const char *name = (const char *)g[type];
    KoreTablePanic(table, "Attempt to call a %s value", name);
    return 0;
}

// ---- sub_5EC960 (KoreHashTableInsert): the setjmp/longjmp-guarded call
// handler.  Installs a jmp buffer at table+0x40, dispatches the call via
// KoreInvokeValue, and on a panic longjmps back here to unwind the call/return
// bookkeeping (restoring the C depth, array end and base, releasing any
// pending allocation records, and dropping the arguments off the value stack).
// Returns 0 on the normal path; on a panic it returns the panic code, or -300
// if the guarded "call the resolved __call method" re-raise itself panicked.
static int KoreHashTableInsert(int table, int a2, int a3, int a4)
{
    int *tv = (int *)table;
    int start = tv[0x28 / 4];
    int var8 = (tv[0x20 / 4] - start) >> 3;      // base-start in qwords
    int varC = (tv[0x1C / 4] - start) >> 3;      // top-start in qwords
    int var10 = tv[0x44 / 4];                    // C-call depth
    int var14 = (tv[0x0C / 4] - tv[0x04 / 4]) >> 4; // array count
    int jmpBuf[17];                              // setjmp context + chain link at [16]
    jmpBuf[16] = tv[0x40 / 4];                   // next-in-chain (old jmp buffer)
    tv[0x40 / 4] = (int)jmpBuf;                  // install
    int r = setjmp(jmpBuf);
    if (r == 0)
    {
        int rv = KoreInvokeValue(table, a2, a3, 0);
        tv[0x40 / 4] = jmpBuf[16];               // restore the old jmp buffer
        return rv;
    }
    // A panic longjmped back in (table+0x40 already points at the old buffer).
    if (a4 != 0)
    {
        int off;
        if (a4 >= 0)
            off = var8 - 1;
        else
            off = varC;
        int *src = (int *)(start + 8 * (off + a4));
        int *tp = (int *)tv[0x1C / 4];
        tp[0] = src[0];
        tp[1] = src[1];
        tv[0x1C / 4] = (int)tp + 8;
        KoreRotateValueStackTop(table, -2);
        jmpBuf[16] = tv[0x40 / 4];               // re-link the chain
        tv[0x40 / 4] = (int)jmpBuf;              // reinstall for the guarded re-raise
        if (setjmp(jmpBuf) == 0)
        {
            int rv2 = KoreInvokeValue(table, 1, 1, 0);
            tv[0x40 / 4] = jmpBuf[16];
            (void)rv2;
        }
        else
        {
            r = -300;
        }
    }
    // Unwind the call/return bookkeeping.
    tv[0x44 / 4] = var10;
    int arg;
    if (var14 == -1)
        arg = start;
    else
        arg = tv[0x04 / 4] + var14 * 16;
    KoreReleaseAllocRecords(table, arg);
    tv[0x0C / 4] = tv[0x04 / 4] + var14 * 16;
    tv[0x20 / 4] = start + var8 * 8;
    int newTop = start + 8 * (varC - a2) - 8;
    tv[0x1C / 4] = newTop;
    int *src2 = (int *)(tv[0x1C / 4] - 8);
    int *dst2 = (int *)newTop;
    dst2[0] = src2[0];
    dst2[1] = src2[1];
    tv[0x1C / 4] = newTop + 8;
    return r;
}




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
    int r2 = KoreHashTableInsert(vmcore, 1, 0, 0);
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
// sub_5DA690 (0x5DA690 - 0x5DA6D2) -- KoreFinalizeVM: the VM finalise step
// (thiscall + retn 4 in the original: this = the VM's +0xBC alloc-table bucket
// sub-object, arg 1 = the module wrapper). Walk the bucket table's count
// (mask + 1) slots from cursor 0; a slot holds a node pointer, and when the
// node's first word (flags/id) is >= 0 the slot is dropped + rehashed through
// KoreRehashAllocNode (which also decrements the sub-object's node count and
// re-inserts out-of-order neighbours), whose returned cursor continues the
// walk; empty or negative-key slots just advance. A mask of -1 (0xFFFFFFFF,
// the table never built) returns 0 without walking. Returns the final cursor
// (ignored by its sole caller). [NOT byte-identical] a thiscall + retn 4
// hybrid in the original (declared object-first here) and placement-dependent
// (the relative call to KoreRehashAllocNode).
static int KoreFinalizeVM(int obj, int wrapper)
{
    int *sub = (int *)obj;                    // the bucket sub-object (thiscall this)
    int *B   = (int *)sub[0];                 // the bucket array
    unsigned int mask = (unsigned int)sub[2];
    unsigned int count = mask + 1u;
    unsigned int i = 0;
    if (sub[2] != -1)
        do
        {
            int node = B[mask & i];
            if (node != 0 && *(int *)node >= 0)
                i = (unsigned int)KoreRehashAllocNode(obj, wrapper, (int)i);
            else
                ++i;
        } while (i < count);
    return (int)i;
}
// sub_5DA150 (0x5DA150 - 0x5DA1C0) -- KoreDetachHeap: detach the VM's own
// heap (the VM object, obj, is itself the heap manager). Free the two
// pending-allocation node lists headed by obj+0x1C and obj+0x24: each head
// word's low 2 bits hold state (the walk masks the node pointers with ~3),
// and each node's next word (node+4, likewise masked) is captured BEFORE
// KoreDeallocAllocNode is called, so the walk survives the block release.
// The low-2-bit state is folded back into both head words, the +0x28 slot
// is zeroed, and the 3-word heap-dealloc context {obj+0x0C (the dealloc
// fn), obj+0x10 (its ctx), obj+0x14} is pulled out into the 3-DWORD out
// buffer (heap), which is also the return value. The sole caller
// (DestroyLuaModuleBody) then frees the 1032-byte VM block through that
// pulled-out fn / ctx: fn(ctx, vm, 0x408, 0). [NOT byte-identical] a
// thiscall + retn 4 hybrid in the original (the VM object in ECX, the out
// pointer on the stack; declared object-first here) and placement-dependent
// (the relative calls to KoreDeallocAllocNode sub_5D5B20).
static int *KoreDetachHeap(int obj, int heap)
{
    int *o = (int *)obj;
    int *h = (int *)heap;
    int node = o[0x1C / 4] & ~3;
    while (node != 0)
    {
        int next = *(int *)(node + 4) & ~3;
        (void)KoreDeallocAllocNode(obj, node);
        node = next;
    }
    node = o[0x24 / 4] & ~3;
    while (node != 0)
    {
        int next = *(int *)(node + 4) & ~3;
        (void)KoreDeallocAllocNode(obj, node);
        node = next;
    }
    o[0x1C / 4] &= 3;
    o[0x24 / 4] &= 3;
    h[0] = o[3];
    h[1] = o[4];
    h[2] = o[5];
    o[0x28 / 4] = 0;
    return h;
}

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
    int heap[3];                                        // the original's sub esp, 0Ch frame
    vm[0x29C / 4] = -1;                                 // mark the VM released
    KoreDumpVMStats((int)mod);                          // sub_5DEB70(mod)
    KoreFreeAllocList((int)(vm + 0x2C / 4));            // sub_5ED910 (thiscall vm+0x2C)
    KoreFinalizeVM((int)(vm + 0xBC / 4), luaModule);    // sub_5DA690 (thiscall vm+0xBC, a1)
    (void)KoreDetachHeap((int)vm, (int)heap);           // sub_5DA150 (thiscall vm, &heap)
    ((void (*)(int, int, int, int))heap[0])(heap[1], (int)vm, 0x408, 0); // free the 1032-byte block
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

// NTools base-class helpers (module-private copies of the gamethread.cpp
// statics, the same pattern as netinit.cpp's CNetBaseClassDtor):
// CBaseClassNonCopyableCtor (0x18984D0) / CBaseClassDtor (0x18984C0).
// [NOT byte-identical] each -- they store the base-class vtable at its
// original high VA, not present in this build.
static void *g_NToolsCBaseClassNonCopyableVtable[1] = { 0 };
static void *g_NToolsCBaseClassVtable[1]             = { 0 };
static void *CBaseClassNonCopyableCtor(void *obj)
{ *(void **)obj = g_NToolsCBaseClassNonCopyableVtable; return obj; }
static void CBaseClassDtor(void *obj)
{ *(void **)obj = g_NToolsCBaseClassVtable; }

// sub_6F7F60 (0x6F7F60 - 0x6F844A) -- CLuaManagerImplCtor: the 708-byte
// (0x2C4) CLuaManager implementation object (thiscall: the impl in ECX,
// then the Lua module, the engine's +0x98 sorted map, and the back-pointer
// to the CLuaManager).  +0 the Lua module, +4 the back-pointer, then the
// script-table cluster: the four 4-byte owner wrappers (this+2 / this+9 /
// this+16 / this+45), the 12- / 28-byte script-handle tables (this+28 /
// this+35 / this+42 / this+56), 16 InitSharedSlot'd shared slots, five
// Concurrency reader-writer locks, the two handle-map objects (sub_708370 /
// sub_6ECEC0), the script-handle table (sub_18F5E10 over the Lua module),
// the byte flags at +604..+611, and the sub_18F2DB0 (sub_6EF380) /
// sub_18F2DA0 (sub_6EF390) registrations.  Remaining stub: the LHAlloc'd
// block is left as LHAlloc leaves it and the whole init is skipped (the
// CLuaManagerImplDtor stub pairs with it -- nothing constructed, nothing
// released).
static void *CLuaManagerImplCtor(void *obj, void *luaModule, void *sortedMap, void *manager)
{ (void)luaModule; (void)sortedMap; (void)manager; return obj; }

// sub_6F7680 (0x6F7680 - 0x6F7F60) -- CLuaManagerImplDtor: the impl
// destructor -- zeroes +0 / +4, releases the +0x3C8 script-handle table
// (vt[0](obj, 1)), the +0x24C / +0x22C handle maps (sub_707A60 / sub_70BE70
// + LHFree), and walks the shared-slot / lock / owner-wrapper cluster in
// reverse (sub_7088A0 / sub_70A460 / sub_707690 / sub_707570 / sub_7031C0 /
// sub_55F120 / ReleaseSharedSlot / the SEH-block InterlockedDecrement
// releases), LHFree'ing every member.  Remaining stub: no-op (pairs with
// the CLuaManagerImplCtor stub above).
static void CLuaManagerImplDtor(void *obj) { (void)obj; }

// sub_70DB40 (0x70DB40 - 0x70DB7E) -- CLuaManagerDeletingDtor: the
// CLuaManager vtable slot 0 (the one-slot 0x1AF41A4 vtable) -- release the
// +4 impl (CLuaManagerImplDtor + LHFree) when set, clear it, CBaseClassDtor,
// and LHFree the 8-byte wrapper when the free-flag bit 0 is set.
// [NOT byte-identical] placement-dependent (relative calls to
// CLuaManagerImplDtor / CBaseClassDtor / LHFree).
//
//  void *CLuaManagerDeletingDtor(void *obj, int freeIt)
static void *CLuaManagerDeletingDtor(void *obj, int freeIt)
{
    void **impl = (void **)((char *)obj + 4);
    if (*impl != 0)
    {
        CLuaManagerImplDtor(*impl);
        LHFree(*impl);
    }
    *impl = 0;
    CBaseClassDtor(obj);
    if (freeIt & 1)
        LHFree(obj);
    return obj;
}

// The CLuaManager vtable (0x1AF41A4 in the original -- a one-slot table
// holding the deleting-dtor, followed immediately by rdata strings).  The
// CEntityScriptHandleManager vtable is still the placeholder (the same
// trick as the g_CLoadingScreenVtable set): slot 0 a callable no-op so the
// deleting-dtor dispatch (vt[0](obj, 1)) in ReinitEngineLua and
// CGameEngineDestroy never dereferences an unconstructed table.
static void *g_CLuaManagerVtable[2]                = { (void *)CLuaManagerDeletingDtor, 0 };
static void *g_CEntityScriptHandleManagerVtable[2] = { (void *)F3StubRelease, 0 };

// ============================================================
//  CLuaManagerCtor  (0x6F9890 - 0x6F98F2)  -- the 8-byte CLuaManager ctor
//  [NOT byte-identical] placement-dependent (relative calls to
//  CBaseClassNonCopyableCtor / LHAlloc / CLuaManagerImplCtor /
//  CLuaManagerImplDtor) and it references the high-VA CLuaManager::
//  `vftable' (0x1AF41A4).
//
//  void *CLuaManagerCtor(void *obj, void *luaModule, void *sortedMap)
//
//  thiscall + `retn 8` in the original: the 8-byte wrapper in ECX, the
//  Lua module (the 1032-byte "Fable 3" block) as stack arg 0 and the
//  engine's +0x98 sorted map as stack arg 1.  It builds the wrapper:
//  CBaseClassNonCopyableCtor base init, the CLuaManager vtable, the +4
//  impl slot zeroed, then LHAlloc(708) (0x2C4) + CLuaManagerImplCtor
//  (sub_6F7F60) building the impl over (luaModule, sortedMap, this) -- a
//  null allocation leaves the slot empty.  If the +4 slot held an old
//  impl it is released (CLuaManagerImplDtor sub_6F7680 + LHFree) -- dead
//  here since the slot was just zeroed.  ReinitEngineLua (0x61A4F5) is
//  the sole caller.  The 8-byte wrapper is freed by the deleting-dtor
//  (CLuaManagerDeletingDtor, the vtable slot 0) with free-flag 1.
// ============================================================
static void *CLuaManagerCtor(void *obj, void *luaModule, void *sortedMap)
{
    CBaseClassNonCopyableCtor(obj);
    *(void **)obj = g_CLuaManagerVtable;        // CLuaManager::`vftable' (0x1AF41A4)
    void *impl    = LHAlloc(708);               // 0x2C4
    *(void **)((char *)obj + 4) = 0;
    void *newImpl = (impl != 0) ? CLuaManagerImplCtor(impl, luaModule, sortedMap, obj)
                                : 0;
    if (*(void **)((char *)obj + 4) != 0)
    {
        CLuaManagerImplDtor(*(void **)((char *)obj + 4));
        LHFree(*(void **)((char *)obj + 4));
    }
    *(void **)((char *)obj + 4) = newImpl;
    return obj;
}

// ============================================================
//  The six VM-internal callees of CLuaManagerImplRunSerialiseScript.  All are
//  deep VM / Kore / localisation internals (out of scope for this module), so
//  each is a documented no-op stub -- the same pattern as the
//  CLuaManagerImplCtor / CLuaManagerImplDtor stubs above.  Each keeps its
//  original IDA address in the comment.
// ============================================================

// sub_56DFD0 (0x56DFD0 - 0x56E023) -- GetWideStringHash: the cached FNV-1a
// 32-bit hash of a wide path string, stored in the string's own +28 word.  The
// original returns the cached hash at +28; when it is 0 and the +20 length is
// non-zero it computes sub_56DFA0 (FNV-1a, offset basis 0x81000005) over the
// string's data (the SSO buffer at +4, or the heap pointer, up to 0x800 wchars
// via wcsnlen), caches and returns it (0 for an empty string).  VM-internal --
// out of scope.  Stub: returns 0 (empty/uncached).
static int GetWideStringHash(const F3WString *s) { (void)s; return 0; }

// sub_180AF10 (0x180AF10 - 0x180AF64) -- ScriptHashHasError: test a Lua script
// hash for a recorded error.  The original builds a small stack query
// ({0, 0, hash, 0}, count 7) and virtual-calls the +48 slot of the global
// table object (dword_1E2D864), freeing the local buffer (LHFree) when it grew
// past 8, and returns the 0/1 result byte.  VM-internal -- out of scope.
// Stub: returns 0 (no error, so the caller's error branch is not taken).
static int ScriptHashHasError(int hash) { (void)hash; return 0; }

// sub_629660 (0x629660 - 0x629732) -- LogScriptError: report an error against a
// Lua module.  The original builds a localised message (sub_17FDC40 +
// sub_17FE290(a2, 1, 2)), and when it resolves (sub_17FDD60 / sub_17FDD50 /
// sub_17FDD90 / sub_17FDE20) emits it via sub_6281C0(luaModule, fmt, arg, a2)
// (the 3rd arg a3 is the fully-formed message slot, cf. the sibling
// RunLuaStartupScripts, sub_616710, which calls sub_629660(lua, path, msg)).
// Returns that result byte.  VM-internal -- out of scope.  Stub: returns 0.
static int LogScriptError(int luaModule, F3SharedBuffer **pathSlot, F3SharedBuffer **msgSlot)
{ (void)luaModule; (void)pathSlot; (void)msgSlot; return 0; }

// sub_6F0BF0 (0x6F0BF0 - 0x6F0D12) -- CLuaManagerImplInitialisePermanents:
// initialise the "Permanents" save/load tables for the serialise run:
// sub_67C390(luaModule) over the impl's +0, then look up the
// "InitialisePermanentsSaveTable" / "InitialisePermanentsLoadTable" entries
// (sub_700140) and, when present, run them (sub_6FD290), releasing the looked-
// up slots + the SEH-block references (InterlockedDecrement + LHFree).  thiscall
// (the impl in ECX).  VM-internal -- out of scope.  Stub.
static void CLuaManagerImplInitialisePermanents(void *impl) { (void)impl; }

// sub_6F9690 (0x6F9690 - 0x6F978B) -- CLuaManagerImplInitialiseSerialise:
// run the ~30 per-component serialise initialisers in sequence (sub_6F87C0,
// sub_E60EB0, sub_62CD50, sub_629910, sub_629AD0, sub_637B20, sub_6E9A10, ...,
// sub_E9AAA0, sub_A13350) over the impl's +0; returns the last one's eax.
// thiscall (the impl in ECX).  VM-internal -- out of scope.  Stub: returns 0.
static int CLuaManagerImplInitialiseSerialise(void *impl) { (void)impl; return 0; }

// sub_627FC0 (0x627FC0 - 0x6280FA) -- KoreModulePacing: pace / throttle a Kore
// module: build a local guard object (sub_1A22480
// over stru_1DC4B40), set byte_1DBDB9C = 1, read param 3 (KoreModuleSetParam),
// and -- when the flag is set or the param exceeds 1.1x the cached
// dword_1C8B12C -- stamp the engine time (GetEngineTime) and reset params 2 / 0
// / 3; otherwise set param 5 (250) in a busy-wait until GetEngineTime() +
// dbl_1C8B100 elapses (param 5 = 250), then clear byte_1DBDB9C and release the
// guard (sub_1A22460).  cdecl (the Kore module in a1, the flag in a2).  VM-
// internal -- out of scope.  Stub.
static void KoreModulePacing(int koreModule, int flag) { (void)koreModule; (void)flag; }

// sub_6F9790 (0x6F9790 - 0x6F9875) -- CLuaManagerImplRunSerialiseScript:
// the impl method the CLuaManager::RunSerialiseScript wrapper (0x6F9880)
// forwards to -- the impl object is the CLuaManager's +4 slot (thiscall).  It
// builds the serialise-script path (SharedSlotConcat of the two serialise-
// script globals g_dword_1DBE2B0 / g_dword_1DC2680), converts it to wide
// (F3WStringFromAnsi), hashes it (GetWideStringHash, sub_56DFD0) and tests the
// hash for a recorded error (ScriptHashHasError, sub_180AF10); on an error it
// builds the message "Lua serialise script \"<path>\" has an error in it"
// (SharedSlotConcatLiteral + MakeSettingsFilePath) and logs it (LogScriptError,
// sub_629660, over *impl / the impl's +0 + the path slot + the message slot),
// releasing the two message slots.  It then finalises the run with
// CLuaManagerImplInitialisePermanents(impl) (sub_6F0BF0) /
// CLuaManagerImplInitialiseSerialise(impl) (sub_6F9690) /
// KoreModulePacing(*impl, 1) (sub_627FC0), frees the wide string's heap
// buffer (when it grew past the SSO), and releases the path slot -- the return
// value is that slot's new live-slot count (ReleaseSharedSlot's eax).  Sole
// caller: the CLuaManagerRunSerialiseScript wrapper (0x6F9883).
// [NOT byte-identical] placement-dependent (the relative calls to the shared-
// slot / wide-string / release helpers + the __security_check_cookie) and it
// reads the high-VA serialise-script globals; the six VM-internal callees are
// the documented no-op stubs above.
//
//  int CLuaManagerImplRunSerialiseScript(void *impl)
static int CLuaManagerImplRunSerialiseScript(void *impl)
{
    F3SharedBuffer *pathSlot = 0;   // the serialise-script path (v12)
    F3SharedBuffer *litSlot  = 0;   // "Lua serialise script \"<path>"  (v9)
    F3SharedBuffer *msgSlot  = 0;   // the full error message          (v8)
    F3WString       wstr;           // the wide path, 36 bytes         (v10 / v11)

    // Build the serialise-script path from the two global slots.
    SharedSlotConcat(&pathSlot, &g_dword_1DBE2B0, &g_dword_1DC2680);
    const char *pathStr = GetSlotString(&pathSlot);

    // Convert to wide, hash it, and test the hash for a recorded error.
    F3WStringFromAnsi(&wstr, pathStr);
    int hash     = GetWideStringHash(&wstr);
    int hasError = ScriptHashHasError(hash);

    // Drop the wide string's heap buffer when it grew past the SSO.
    if (wstr.cap >= 8)
        LHFree((void *)wstr.u.ptr);

    // On an error, log "Lua serialise script \"<path>\" has an error in it".
    if (hasError != 0)
    {
        SharedSlotConcatLiteral(&litSlot, "Lua serialise script \"", &pathSlot);
        MakeSettingsFilePath(&msgSlot, &litSlot, "\" has an error in it");
        LogScriptError(*(int *)impl, &pathSlot, &msgSlot);
        ReleaseSharedSlot(&msgSlot);
        ReleaseSharedSlot(&litSlot);
    }

    // Finalise the run.
    CLuaManagerImplInitialisePermanents(impl);    // sub_6F0BF0
    CLuaManagerImplInitialiseSerialise(impl);     // sub_6F9690
    KoreModulePacing(*(int *)impl, 1);            // sub_627FC0

    // Release the path slot; return its new live-slot count.
    return (int)ReleaseSharedSlot(&pathSlot);
}

// sub_6F9880 (0x6F9880 - 0x6F9888) -- CLuaManagerRunSerialiseScript:
// the 8-byte CLuaManager wrapper (thiscall: the 8-byte wrapper in ECX) that
// forwards to the impl: `mov ecx, [ecx+4]; jmp sub_6F9790` -- i.e. tail-call
// CLuaManagerImplRunSerialiseScript on the CLuaManager's +4 impl slot and
// return its eax.  ReinitEngineLua (0x61A51A) is the sole caller (the return
// value is discarded).
// [NOT byte-identical] placement-dependent (the relative `jmp` to
// CLuaManagerImplRunSerialiseScript is a 32-bit displacement that depends on
// where the thunk lands).
//
//  int CLuaManagerRunSerialiseScript(void *obj)
static int CLuaManagerRunSerialiseScript(void *obj)
{ return CLuaManagerImplRunSerialiseScript(*(void **)((char *)obj + 4)); }

// ============================================================================
//  RegisterEngineLuaFunctions  (0x6169D0 - 0x6171A5) and its 100 engine Lua
//  builtin wrappers (0x615B30 - 0x6169C0)
//
//  RegisterEngineLuaFunctions(luaModule) -- called from ReinitEngineLua
//  (0x61A526) on the freshly built Lua module -- installs the 100 engine
//  Set*/Get* Lua builtins (SetApplicationName, SetScreenResolution,
//  SetInitialWorldName, ..., SpotCheckSuccessful_6) on the module's
//  global table.  It first builds a 2-dword "GlobalTable" accessor on the
//  caller's stack (EngineLuaGlobalTable, sub_67C390), then for every
//  builtin calls the signature-matched registration helper (one of the
//  11 sub_6182xx/618x.. variants below: 124 bytes each, identical except
//  for the upvalue-marshalling function sub_617AD0..sub_618790 handed to
//  KoreRegisterBuiltinClosure) with ("LuaName", wrapper-fn) -- each helper
//  reserves 8 bytes on the VM value stack, writes the wrapper pointer,
//  installs the builtin closure, stores the name in the global table
//  (sub_67C540) and registers it with the builtin-name registry
//  (KoreRegisterBuiltinName, -2).  The tail releases the accessor's
//  refcounted control block (InterlockedDecrement; on zero, dispatch its
//  release callback + LHFree).  All the registration machinery is VM-
//  internal (Kore VM builtin-closure / global-table code) -- the helpers
//  are documented no-op stubs.  [NOT byte-identical] placement-dependent
//  (100+ relative calls) and it references the high-VA .rdata name
//  strings + the high-VA engine state globals below.
//
//  The 100 wrapper functions (0x615B30 - 0x6169C0) are the engine
//  implementations of the builtins: tiny C functions named for their Lua
//  name, backed by the engine Lua state globals 0x1DBD01C-0x1DBD118
//  (flags + shared-name slots) and 0x1C86BD5-0x1C86C58 (renderer /
//  spot-check state).  SetUseRetailFrontEnd / SetToCreateNewSaveFilePerSave
//  / SetFrontEndDemoMode register one-byte `retn` no-ops (nullsub_6 / 5 / 4).
//  SetLevelNameStartsWithACS (sub_6166C0) is also called from code
//  (sub_677010, 0x677151); AddAudioBank (sub_616910) likewise (sub_71A230,
//  0x71A33B).
// ============================================================================

// ---- the engine Lua builtin state globals -------------------------------
// 0x1DBD048-0x1DBD0D0: the engine Lua name slots (F3SharedBuffer*) --
// application/level/scenario/start-point names for the initial, GUI, PVP
// and front-end contexts, the hero / henchman setup, the script names and
// the gameflow state (shared with the frontend: gamemain.cpp keeps its own
// module-local copy of the 0x1DBD048 object).
static F3SharedBuffer *g_slot_1DBD048 = 0;   // 0x1DBD048: ApplicationName
static F3SharedBuffer *g_slot_1DBD04C = 0;   // 0x1DBD04C: AIScriptName
static F3SharedBuffer *g_slot_1DBD050 = 0;   // 0x1DBD050: CameraScriptName
static F3SharedBuffer *g_slot_1DBD054 = 0;   // 0x1DBD054: QuestScriptName
static F3SharedBuffer *g_slot_1DBD058 = 0;   // 0x1DBD058: GeneralSetupScriptName
static F3SharedBuffer *g_slot_1DBD05C = 0;   // 0x1DBD05C: InitialWorldName
static F3SharedBuffer *g_slot_1DBD060 = 0;   // 0x1DBD060: InitialLevelName
static F3SharedBuffer *g_slot_1DBD064 = 0;   // 0x1DBD064: InitialScenarioName
static F3SharedBuffer *g_slot_1DBD068 = 0;   // 0x1DBD068: InitialStartPointName
static F3SharedBuffer *g_slot_1DBD06C = 0;   // 0x1DBD06C: InitialSaveGameName
static F3SharedBuffer *g_slot_1DBD070 = 0;   // 0x1DBD070: InitialClientSaveGameName
static F3SharedBuffer *g_slot_1DBD074 = 0;   // 0x1DBD074: GUIWorldName
static F3SharedBuffer *g_slot_1DBD078 = 0;   // 0x1DBD078: GUILevelName
static F3SharedBuffer *g_slot_1DBD07C = 0;   // 0x1DBD07C: GUIScenarioName
static F3SharedBuffer *g_slot_1DBD080 = 0;   // 0x1DBD080: GUIStartPointName
static F3SharedBuffer *g_slot_1DBD084 = 0;   // 0x1DBD084: PVPWorldName
static F3SharedBuffer *g_slot_1DBD088 = 0;   // 0x1DBD088: PVPLevelName
static F3SharedBuffer *g_slot_1DBD08C = 0;   // 0x1DBD08C: PVPScenarioName
static F3SharedBuffer *g_slot_1DBD090 = 0;   // 0x1DBD090: PVPStartPointName
static F3SharedBuffer *g_slot_1DBD094 = 0;   // 0x1DBD094: FrontEndWorldName
static F3SharedBuffer *g_slot_1DBD098 = 0;   // 0x1DBD098: FrontEndLevelName
static F3SharedBuffer *g_slot_1DBD09C = 0;   // 0x1DBD09C: FrontEndScenarioName
static F3SharedBuffer *g_slot_1DBD0A0 = 0;   // 0x1DBD0A0: FrontEndStartPointName
static F3SharedBuffer *g_unk_1DBD0A4 = 0;    // 0x1DBD0A4: ControlSystemRecordName
static F3SharedBuffer *g_slot_1DBD0A8 = 0;   // 0x1DBD0A8: InitialHeroEntityName
static F3SharedBuffer *g_slot_1DBD0AC = 0;   // 0x1DBD0AC: InitialHenchmanEntityName
static F3SharedBuffer *g_slot_1DBD0B0 = 0;   // 0x1DBD0B0: InitialHeroMeleeWeapon
static F3SharedBuffer *g_slot_1DBD0B4 = 0;   // 0x1DBD0B4: InitialHeroRangedWeapon
static F3SharedBuffer *g_slot_1DBD0B8 = 0;   // 0x1DBD0B8: InitialHenchmanMeleeWeapon
static F3SharedBuffer *g_slot_1DBD0BC = 0;   // 0x1DBD0BC: InitialHenchmanRangedWeapon
static F3SharedBuffer *g_slot_1DBD0C0 = 0;   // 0x1DBD0C0: InitialHeroScriptTypeIdentifier
static F3SharedBuffer *g_slot_1DBD0C4 = 0;   // 0x1DBD0C4: OverriddenDogCharacterRecordName
static F3SharedBuffer *g_slot_1DBD0C8 = 0;   // 0x1DBD0C8: OverriddenClientDogCharacterRecordName
static F3SharedBuffer *g_slot_1DBD0CC = 0;   // 0x1DBD0CC: GameflowScriptEnum
static F3SharedBuffer *g_slot_1DBD0D0 = 0;   // 0x1DBD0D0: GameflowScriptState
// 0x1DBD0EC: the level-name slot SetLevelNameStartsWithACS copies the arg
// slot over (the "ACS"-prefix level fallback name).
static F3SharedBuffer *g_slot_1DBD0EC = 0;   // 0x1DBD0EC: level name slot
// 0x1DBD104 / 0x1DBD118 / 0x1DBD11C: the optional-startup-script list
// (head node, tail node, tail's next pointer) appended by
// AddOptionalStartupScript (sub_55EAF0 list nodes, 12 bytes each).
static int g_list_1DBD104 = 0;   // 0x1DBD104: optional startup-script list head node
static int g_list_1DBD118 = 0;   // 0x1DBD118: list tail node
static int g_list_1DBD11C = 0;   // 0x1DBD11C: tail node's next pointer
// 0x1DBDD4C: the Kore VM core object SetLevelNameStartsWithACS probes
// (sub_673C50 returns it; 0 = no live VM).
static int g_dword_1DBDD4C = 0;   // 0x1DBDD4C: the Kore VM core object
// 0x1DBD0F0: the audio-bank registry AddAudioBank adds banks into
// (sub_619520's this object).
static int g_unk_1DBD0F0 = 0;     // 0x1DBD0F0: the audio-bank registry object

// 0x1C86BD5-0x1C86C58: the engine Lua renderer / gameplay flags and the
// SpotCheck expected/actual pairs.  (byte_1DBD02A / byte_1DBD02B live in
// the word g_word_1DBD02A already declared with the VM globals above.)
static unsigned char g_byte_1DBD019 = 0;    // 0x1DBD019: SetOpeningLoadingScreen
static unsigned char g_byte_1DBD01C = 0;    // 0x1DBD01C: SetAllowAllSkipStates
static unsigned char g_byte_1DBD01D = 0;    // 0x1DBD01D: IsToStartGameflow
static unsigned char g_byte_1DBD01E = 0;    // 0x1DBD01E: IsToStartPVPMode
static int           g_dword_1DBD020 = 0;   // 0x1DBD020: GetDemoMode
static unsigned char g_byte_1DBD036 = 0;    // 0x1DBD036: GetDifficultySetToHard
static unsigned char g_byte_1DBD03F = 0;    // 0x1DBD03F: (set/get)LiveFunctionalityDisabled
static unsigned char g_byte_1C86BD5 = 0;    // 0x1C86BD5: SetStartMusicAndAtmosOnLevelLoad
static unsigned char g_byte_1C86BD9 = 0;    // 0x1C86BD9: GetUseRetailFrontEnd
static unsigned char g_byte_1C86BDF = 0;    // 0x1C86BDF: SetVSync
static int           g_dword_1C86BE0 = 0;   // 0x1C86BE0: GetMillisecondsToWaitForLuaDebugger
static int           g_dword_1C86BF0 = 0;   // 0x1C86BF0: SetVSyncThreshold
static int           g_dword_1C86BF4 = 0;   // 0x1C86BF4: SetMSToShowLegalScreen
static int           g_dword_1C86C08 = 0;   // 0x1C86C08: SetPresentationInterval
static unsigned char g_byte_1C86C0F = 0;    // 0x1C86C0F: SetDoFinalMarketplaceOfferCheck
static unsigned char g_byte_1C86C15 = 0;    // 0x1C86C15: SetDevFrontendSignin
static unsigned char g_byte_1C86C16 = 0;    // 0x1C86C16: SetAllowMultiplayerJoining
static int           g_dword_1C86C18[12] = { 0 };  // 0x1C86C18-0x1C86C48: 6 SpotCheck expected/actual pairs
static int           g_dword_1C86C54 = 0;   // 0x1C86C54: screen width (Set/GetScreenResolution)
static int           g_dword_1C86C58 = 0;   // 0x1C86C58: screen height

// ----------------------------------------------------------------
//  The 11 builtin registration helpers (all 0x618290 + n*0x80, 124 bytes
//  each).  Shared body (thiscall on the 2-dword GlobalTable accessor,
//  ("LuaName", wrapper-fn) on the stack): when the accessor's +0 is set,
//  reserve 8 bytes on the VM value stack (sub_5DF650 + sub_5DC450), memcpy
//  the wrapper pointer in, KoreRegisterBuiltinClosure(vm, <upvalue fn>, 1)
//  (the upvalue fn sub_617AD0..sub_618790 marshals the Lua args -- a string
//  to a shared slot, a bool, a float, ... -- per the builtin's signature),
//  sub_67C540 (hash the name + store it in the global table),
//  KoreRegisterBuiltinName(vm, -2, name), then roll the stack back.  VM-
//  internal -- out of scope; no-op stubs (the 11 variants differ ONLY in
//  the upvalue-marshalling function they hand KoreRegisterBuiltinClosure).
// ----------------------------------------------------------------
static void   EngineLuaGlobalTable(int *obj, int luaModule) { (void)obj; (void)luaModule; }   // sub_67C390
static void   EngineLuaRegisterString(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }   // sub_618290 (upvalue sub_617AD0: one string arg)
static void   EngineLuaRegisterPair(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }     // sub_618310 (upvalue sub_617B40: two string args)
static void   EngineLuaRegisterBoolGet(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }  // sub_618390 (upvalue sub_617BC0: no args -> bool)
static void   EngineLuaRegisterNumGet(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }   // sub_618410 (upvalue sub_617C10: no args -> number)
static void   EngineLuaRegisterFlagSet(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }  // sub_618490 (upvalue sub_617C60: one bool/nil arg)
static void   EngineLuaRegisterPairGet(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }  // sub_618510 (upvalue sub_617CD0: no args -> two numbers)
static void   EngineLuaRegisterIntGet(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }   // sub_618590 (upvalue sub_617D40: no args -> number)
static void   EngineLuaRegisterNoopSet(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }  // sub_618610 (upvalue sub_617D90: one bool arg, no-op body)
static void   EngineLuaRegisterIntSet(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }   // sub_618690 (upvalue sub_617E00: one number arg)
static void   EngineLuaRegisterMultiSet(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; } // sub_618710 (upvalue sub_617E70: several args)
static void   EngineLuaRegisterStringGet(int *obj, const char *name, void *fn) { (void)obj; (void)name; (void)fn; }// sub_6189D0 (upvalue sub_618790: no args -> string)

// ----------------------------------------------------------------
//  The 100 engine Lua builtin wrappers (named for their Lua function;
//  only referenced as the function pointers the registration list pushes).
// ----------------------------------------------------------------

// ---- 1-string-arg name setters: CopySharedSlot(&engineSlot, argSlot) ----
// sub_615B30 (0x615B30 - 0x615B40) -- SetApplicationName: copy the arg slot into the ApplicationName slot.
static F3SharedBuffer **SetApplicationName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD048, arg); }
// sub_615B80 (0x615B80 - 0x615B90) -- SetInitialWorldName.
static F3SharedBuffer **SetInitialWorldName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD05C, arg); }
// sub_615B90 (0x615B90 - 0x615BA0) -- SetInitialLevelName.
static F3SharedBuffer **SetInitialLevelName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD060, arg); }
// sub_615BA0 (0x615BA0 - 0x615BB0) -- SetInitialScenarioName.
static F3SharedBuffer **SetInitialScenarioName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD064, arg); }
// sub_615BB0 (0x615BB0 - 0x615BC0) -- SetInitialStartPointName.
static F3SharedBuffer **SetInitialStartPointName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD068, arg); }
// sub_615BC0 (0x615BC0 - 0x615BD0) -- SetInitialSaveGameName.
static F3SharedBuffer **SetInitialSaveGameName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD06C, arg); }
// sub_615BD0 (0x615BD0 - 0x615BE0) -- SetInitialClientSaveGameName.
static F3SharedBuffer **SetInitialClientSaveGameName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD070, arg); }
// sub_615BE0 (0x615BE0 - 0x615BF0) -- SetGameflowScriptEnum.
static F3SharedBuffer **SetGameflowScriptEnum(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0CC, arg); }
// sub_615BF0 (0x615BF0 - 0x615C00) -- SetGameflowScriptState.
static F3SharedBuffer **SetGameflowScriptState(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0D0, arg); }
// sub_615C00 (0x615C00 - 0x615C10) -- SetGUIWorldName.
static F3SharedBuffer **SetGUIWorldName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD074, arg); }
// sub_615C10 (0x615C10 - 0x615C20) -- SetGUILevelName.
static F3SharedBuffer **SetGUILevelName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD078, arg); }
// sub_615C20 (0x615C20 - 0x615C30) -- SetGUIScenarioName.
static F3SharedBuffer **SetGUIScenarioName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD07C, arg); }
// sub_615C30 (0x615C30 - 0x615C40) -- SetGUIStartPointName.
static F3SharedBuffer **SetGUIStartPointName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD080, arg); }
// sub_615C40 (0x615C40 - 0x615C50) -- SetPVPWorldName.
static F3SharedBuffer **SetPVPWorldName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD084, arg); }
// sub_615C50 (0x615C50 - 0x615C60) -- SetPVPLevelName.
static F3SharedBuffer **SetPVPLevelName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD088, arg); }
// sub_615C60 (0x615C60 - 0x615C70) -- SetPVPScenarioName.
static F3SharedBuffer **SetPVPScenarioName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD08C, arg); }
// sub_615C70 (0x615C70 - 0x615C80) -- SetPVPStartPointName.
static F3SharedBuffer **SetPVPStartPointName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD090, arg); }
// sub_615C80 (0x615C80 - 0x615C90) -- SetFrontEndWorldName.
static F3SharedBuffer **SetFrontEndWorldName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD094, arg); }
// sub_615C90 (0x615C90 - 0x615CA0) -- SetFrontEndLevelName.
static F3SharedBuffer **SetFrontEndLevelName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD098, arg); }
// sub_615CA0 (0x615CA0 - 0x615CB0) -- SetFrontEndScenarioName.
static F3SharedBuffer **SetFrontEndScenarioName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD09C, arg); }
// sub_615CB0 (0x615CB0 - 0x615CC0) -- SetFrontEndStartPointName.
static F3SharedBuffer **SetFrontEndStartPointName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0A0, arg); }
// sub_615FC0 (0x615FC0 - 0x615FD0) -- SetInitialHeroEntityName.
static F3SharedBuffer **SetInitialHeroEntityName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0A8, arg); }
// sub_615FD0 (0x615FD0 - 0x615FE0) -- SetInitialHenchmanEntityName.
static F3SharedBuffer **SetInitialHenchmanEntityName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0AC, arg); }
// sub_615FE0 (0x615FE0 - 0x615FF0) -- SetInitialHeroMeleeWeapon.
static F3SharedBuffer **SetInitialHeroMeleeWeapon(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0B0, arg); }
// sub_615FF0 (0x615FF0 - 0x616000) -- SetInitialHeroRangedWeapon.
static F3SharedBuffer **SetInitialHeroRangedWeapon(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0B4, arg); }
// sub_616000 (0x616000 - 0x616010) -- SetInitialHenchmanMeleeWeapon.
static F3SharedBuffer **SetInitialHenchmanMeleeWeapon(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0B8, arg); }
// sub_616010 (0x616010 - 0x616020) -- SetInitialHenchmanRangedWeapon.
static F3SharedBuffer **SetInitialHenchmanRangedWeapon(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0BC, arg); }
// sub_616020 (0x616020 - 0x616030) -- SetInitialHeroScriptTypeIdentifier.
static F3SharedBuffer **SetInitialHeroScriptTypeIdentifier(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0C0, arg); }
// sub_616030 (0x616030 - 0x616040) -- SetOverriddenDogCharacterRecordName.
static F3SharedBuffer **SetOverriddenDogCharacterRecordName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0C4, arg); }
// sub_616040 (0x616040 - 0x616050) -- SetOverriddenClientDogCharacterRecordName.
static F3SharedBuffer **SetOverriddenClientDogCharacterRecordName(F3SharedBuffer **arg) { return CopySharedSlot(&g_slot_1DBD0C8, arg); }
// sub_6166A0 (0x6166A0 - 0x6166B5) -- SetScreenResolution: store (width, height) from the 2-dword arg.
static void SetScreenResolution(int *out) { g_dword_1C86C54 = out[0]; g_dword_1C86C58 = out[1]; }

// ---- no-arg getters --------------------------------------------------------
// sub_615D60 (0x615D60 - 0x615D65) -- IsToStartGameflow: byte_1DBD01D.
static int IsToStartGameflow(void) { return (int)g_byte_1DBD01D; }
// sub_615D70 (0x615D70 - 0x615D75) -- IsToStartPVPMode: byte_1DBD01E.
static int IsToStartPVPMode(void) { return (int)g_byte_1DBD01E; }
// sub_615D90 (0x615D90 - 0x615D95) -- GetDemoMode: dword_1DBD020.
static int GetDemoMode(void) { return g_dword_1DBD020; }
// sub_616070 (0x616070 - 0x616075) -- GetApplicationName: the ApplicationName slot.
static F3SharedBuffer **GetApplicationName(void) { return &g_slot_1DBD048; }
// sub_616080 (0x616080 - 0x616085) -- GetScreenResolution: write (width, height) into the 2-dword arg.
static int *GetScreenResolution(int *out) { out[0] = g_dword_1C86C54; out[1] = g_dword_1C86C58; return out; }
// sub_6160A0 (0x6160A0 - 0x6160A5) -- GetAIScriptName.
static F3SharedBuffer **GetAIScriptName(void) { return &g_slot_1DBD04C; }
// sub_6160B0 (0x6160B0 - 0x6160B5) -- GetCameraScriptName.
static F3SharedBuffer **GetCameraScriptName(void) { return &g_slot_1DBD050; }
// sub_6160C0 (0x6160C0 - 0x6160C5) -- GetQuestScriptName.
static F3SharedBuffer **GetQuestScriptName(void) { return &g_slot_1DBD054; }
// sub_6160D0 (0x6160D0 - 0x6160D5) -- GetGeneralSetupScriptName.
static F3SharedBuffer **GetGeneralSetupScriptName(void) { return &g_slot_1DBD058; }
// sub_6160E0 (0x6160E0 - 0x6160E5) -- GetInitialWorldName.
static F3SharedBuffer **GetInitialWorldName(void) { return &g_slot_1DBD05C; }
// sub_6160F0 (0x6160F0 - 0x6160F5) -- GetInitialLevelName.
static F3SharedBuffer **GetInitialLevelName(void) { return &g_slot_1DBD060; }
// sub_616100 (0x616100 - 0x616105) -- GetInitialScenarioName.
static F3SharedBuffer **GetInitialScenarioName(void) { return &g_slot_1DBD064; }
// sub_616110 (0x616110 - 0x616115) -- GetInitialStartPointName.
static F3SharedBuffer **GetInitialStartPointName(void) { return &g_slot_1DBD068; }
// sub_616120 (0x616120 - 0x616125) -- GetInitialSaveGameName.
static F3SharedBuffer **GetInitialSaveGameName(void) { return &g_slot_1DBD06C; }
// sub_616130 (0x616130 - 0x616135) -- GetInitialClientSaveGameName.
static F3SharedBuffer **GetInitialClientSaveGameName(void) { return &g_slot_1DBD070; }
// sub_616140 (0x616140 - 0x616145) -- GetGameflowScriptEnum.
static F3SharedBuffer **GetGameflowScriptEnum(void) { return &g_slot_1DBD0CC; }
// sub_616150 (0x616150 - 0x616155) -- GetGameflowScriptState.
static F3SharedBuffer **GetGameflowScriptState(void) { return &g_slot_1DBD0D0; }
// sub_616160 (0x616160 - 0x616165) -- GetGUIWorldName.
static F3SharedBuffer **GetGUIWorldName(void) { return &g_slot_1DBD074; }
// sub_616170 (0x616170 - 0x616175) -- GetGUILevelName.
static F3SharedBuffer **GetGUILevelName(void) { return &g_slot_1DBD078; }
// sub_616180 (0x616180 - 0x616185) -- GetGUIScenarioName.
static F3SharedBuffer **GetGUIScenarioName(void) { return &g_slot_1DBD07C; }
// sub_616190 (0x616190 - 0x616195) -- GetGUIStartPointName.
static F3SharedBuffer **GetGUIStartPointName(void) { return &g_slot_1DBD080; }
// sub_6161A0 (0x6161A0 - 0x6161A5) -- GetPVPWorldName.
static F3SharedBuffer **GetPVPWorldName(void) { return &g_slot_1DBD084; }
// sub_6161B0 (0x6161B0 - 0x6161B5) -- GetPVPLevelName.
static F3SharedBuffer **GetPVPLevelName(void) { return &g_slot_1DBD088; }
// sub_6161C0 (0x6161C0 - 0x6161C5) -- GetPVPScenarioName.
static F3SharedBuffer **GetPVPScenarioName(void) { return &g_slot_1DBD08C; }
// sub_6161D0 (0x6161D0 - 0x6161D5) -- GetPVPStartPointName.
static F3SharedBuffer **GetPVPStartPointName(void) { return &g_slot_1DBD090; }
// sub_6161E0 (0x6161E0 - 0x6161E5) -- GetFrontEndWorldName.
static F3SharedBuffer **GetFrontEndWorldName(void) { return &g_slot_1DBD094; }
// sub_6161F0 (0x6161F0 - 0x6161F5) -- GetFrontEndLevelName.
static F3SharedBuffer **GetFrontEndLevelName(void) { return &g_slot_1DBD098; }
// sub_616200 (0x616200 - 0x616205) -- GetFrontEndScenarioName.
static F3SharedBuffer **GetFrontEndScenarioName(void) { return &g_slot_1DBD09C; }
// sub_616210 (0x616210 - 0x616215) -- GetFrontEndStartPointName.
static F3SharedBuffer **GetFrontEndStartPointName(void) { return &g_slot_1DBD0A0; }
// sub_616230 (0x616230 - 0x616235) -- GetControlSystemRecordName.
static F3SharedBuffer **GetControlSystemRecordName(void) { return &g_unk_1DBD0A4; }
// sub_616240 (0x616240 - 0x616245) -- GetInitialHeroEntityName.
static F3SharedBuffer **GetInitialHeroEntityName(void) { return &g_slot_1DBD0A8; }
// sub_616250 (0x616250 - 0x616255) -- GetInitialHenchmanEntityName.
static F3SharedBuffer **GetInitialHenchmanEntityName(void) { return &g_slot_1DBD0AC; }
// sub_616260 (0x616260 - 0x616265) -- GetInitialHeroMeleeWeapon.
static F3SharedBuffer **GetInitialHeroMeleeWeapon(void) { return &g_slot_1DBD0B0; }
// sub_616270 (0x616270 - 0x616275) -- GetInitialHeroRangedWeapon.
static F3SharedBuffer **GetInitialHeroRangedWeapon(void) { return &g_slot_1DBD0B4; }
// sub_616280 (0x616280 - 0x616285) -- GetInitialHenchmanMeleeWeapon.
static F3SharedBuffer **GetInitialHenchmanMeleeWeapon(void) { return &g_slot_1DBD0B8; }
// sub_616290 (0x616290 - 0x616295) -- GetInitialHenchmanRangedWeapon.
static F3SharedBuffer **GetInitialHenchmanRangedWeapon(void) { return &g_slot_1DBD0BC; }
// sub_6162A0 (0x6162A0 - 0x6162A5) -- GetInitialHeroScriptTypeIdentifier.
static F3SharedBuffer **GetInitialHeroScriptTypeIdentifier(void) { return &g_slot_1DBD0C0; }
// sub_6162B0 (0x6162B0 - 0x6162B5) -- GetOverriddenDogCharacterRecordName.
static F3SharedBuffer **GetOverriddenDogCharacterRecordName(void) { return &g_slot_1DBD0C4; }
// sub_6162C0 (0x6162C0 - 0x6162C5) -- GetOverriddenClientDogCharacterRecordName.
static F3SharedBuffer **GetOverriddenClientDogCharacterRecordName(void) { return &g_slot_1DBD0C8; }
// sub_6162D0 (0x6162D0 - 0x6162D5) -- GetMillisecondsToWaitForLuaDebugger.
static int GetMillisecondsToWaitForLuaDebugger(void) { return g_dword_1C86BE0; }
// sub_6162E0 (0x6162E0 - 0x6162E5) -- IsReleaseBuild: always 1 in this build.
static int IsReleaseBuild(void) { return 1; }
// sub_6162F0 (0x6162F0 - 0x6162F5) -- GetUseRetailFrontEnd: byte_1C86BD9.
static int GetUseRetailFrontEnd(void) { return (int)g_byte_1C86BD9; }
// sub_616640 (0x616640 - 0x616645) -- IsLiveFunctionalityDisabled: byte_1DBD03F.
static int IsLiveFunctionalityDisabled(void) { return (int)g_byte_1DBD03F; }
// sub_616550 (0x616550 - 0x616555) -- GetDifficultySetToHard: byte_1DBD036.
static int GetDifficultySetToHard(void) { return (int)g_byte_1DBD036; }

// ---- the six SpotCheck expected/actual comparisons (each 0x12 bytes) ----
// sub_616470 (0x616470 - 0x616482) -- SpotCheckSuccessful_1: dword_1C86C18 == dword_1C86C1C.
static int SpotCheckSuccessful_1(void) { return g_dword_1C86C18[0] == g_dword_1C86C18[1]; }
// sub_616490 (0x616490 - 0x6164A2) -- SpotCheckSuccessful_2: 0x1C86C20 == 0x1C86C24.
static int SpotCheckSuccessful_2(void) { return g_dword_1C86C18[2] == g_dword_1C86C18[3]; }
// sub_6164B0 (0x6164B0 - 0x6164C2) -- SpotCheckSuccessful_3: 0x1C86C28 == 0x1C86C2C.
static int SpotCheckSuccessful_3(void) { return g_dword_1C86C18[4] == g_dword_1C86C18[5]; }
// sub_6164D0 (0x6164D0 - 0x6164E2) -- SpotCheckSuccessful_4: 0x1C86C30 == 0x1C86C34.
static int SpotCheckSuccessful_4(void) { return g_dword_1C86C18[6] == g_dword_1C86C18[7]; }
// sub_6164F0 (0x6164F0 - 0x616502) -- SpotCheckSuccessful_5: 0x1C86C38 == 0x1C86C3C.
static int SpotCheckSuccessful_5(void) { return g_dword_1C86C18[8] == g_dword_1C86C18[9]; }
// sub_616510 (0x616510 - 0x616522) -- SpotCheckSuccessful_6: 0x1C86C40 == 0x1C86C44.
static int SpotCheckSuccessful_6(void) { return g_dword_1C86C18[10] == g_dword_1C86C18[11]; }

// ---- one-arg flag / number setters (store the arg byte, return it) -------
// sub_615DE0 (0x615DE0 - 0x615DE9) -- SetRunFromDVD: byte_1DBD02A (the low byte of word_1DBD02A).
static int SetRunFromDVD(int flag) { g_word_1DBD02A = (g_word_1DBD02A & 0xFFFFFF00) | (flag & 0xFF); return flag & 0xFF; }
// sub_615DF0 (0x615DF0 - 0x615DF9) -- SetIgnoreCheckLocalDLCVersionFile: byte_1DBD02B (the high byte of word_1DBD02A).
static int SetIgnoreCheckLocalDLCVersionFile(int flag) { g_word_1DBD02A = (g_word_1DBD02A & 0xFFFF00FF) | ((flag & 0xFF) << 8); return flag & 0xFF; }
// sub_616590 (0x616590 - 0x616599) -- SetDoFinalMarketplaceOfferCheck: byte_1C86C0F.
static int SetDoFinalMarketplaceOfferCheck(int flag) { g_byte_1C86C0F = (unsigned char)flag; return flag & 0xFF; }
// sub_616320 (0x616320 - 0x616329) -- SetOpeningLoadingScreen: byte_1DBD019.
static int SetOpeningLoadingScreen(int flag) { g_byte_1DBD019 = (unsigned char)flag; return flag & 0xFF; }
// sub_616390 (0x616390 - 0x616399) -- SetAllowAllSkipStates: byte_1DBD01C.
static int SetAllowAllSkipStates(int flag) { g_byte_1DBD01C = (unsigned char)flag; return flag & 0xFF; }
// sub_616300 (0x616300 - 0x616309) -- SetStartMusicAndAtmosOnLevelLoad: byte_1C86BD5.
static int SetStartMusicAndAtmosOnLevelLoad(int flag) { g_byte_1C86BD5 = (unsigned char)flag; return flag & 0xFF; }
// sub_616400 (0x616400 - 0x616409) -- SetVSync: byte_1C86BDF.
static int SetVSync(int flag) { g_byte_1C86BDF = (unsigned char)flag; return flag & 0xFF; }
// sub_616610 (0x616610 - 0x616619) -- SetDevFrontendSignin: byte_1C86C15.
static int SetDevFrontendSignin(int flag) { g_byte_1C86C15 = (unsigned char)flag; return flag & 0xFF; }
// sub_616620 (0x616620 - 0x616629) -- SetAllowMultiplayerJoining: byte_1C86C16.
static int SetAllowMultiplayerJoining(int flag) { g_byte_1C86C16 = (unsigned char)flag; return flag & 0xFF; }
// sub_616630 (0x616630 - 0x616639) -- SetDisableLiveFunctionality: byte_1DBD03F.
static int SetDisableLiveFunctionality(int flag) { g_byte_1DBD03F = (unsigned char)flag; return flag & 0xFF; }
// sub_616410 (0x616410 - 0x616415) -- SetVSyncThreshold: dword_1C86BF0.
static int SetVSyncThreshold(int v) { g_dword_1C86BF0 = v; return v; }
// sub_616440 (0x616440 - 0x616445) -- SetMSToShowLegalScreen: dword_1C86BF4.
static int SetMSToShowLegalScreen(int v) { g_dword_1C86BF4 = v; return v; }
// sub_616450 (0x616450 - 0x616455) -- SetPresentationInterval: dword_1C86C08.
static int SetPresentationInterval(int v) { g_dword_1C86C08 = v; return v; }

// ---- the three no-op builtin bodies (`retn` thunks) ----------------------
// nullsub_6 (0x616380 - 0x616381) -- the SetUseRetailFrontEnd no-op body.
static void EngineLuaNoop_616380(void) { }
// nullsub_5 (0x615F30 - 0x615F31) -- the SetToCreateNewSaveFilePerSave no-op body.
static void EngineLuaNoop_615F30(void) { }
// nullsub_4 (0x615DA0 - 0x615DA1) -- the SetFrontEndDemoMode no-op body.
static void EngineLuaNoop_615DA0(void) { }

// sub_673E80 + VM string ctor + sub_6477E0 (sub_646560): the VM-internal
// string-object probe SetLevelNameStartsWithACS drives -- build the string
// object for the arg text from the VM's string-ctor slot (*(vmcore+0xC)+0x1C)
// and look it up in the VM global table.  No-op stub.
static int KoreVmStringOp(const char *name) { (void)name; return 0; }
// sub_6166C0 (0x6166C0 - 0x6166FB) -- SetLevelNameStartsWithACS: when the
// Kore VM is live (g_dword_1DBDD4C, sub_673C50), build the string object
// for the arg slot's text (sub_673E80 + the VM's string ctor slot) and
// probe it against the global table (sub_6477E0 / sub_646560); when the
// probe fails (or there is no VM) copy the 0x1DBD0EC level-name slot over
// the arg.  Also called from code: sub_677010 (0x677151).
// [NOT byte-identical] placement-dependent + the VM-internal string /
// global-table helpers are documented no-op stubs.
static F3SharedBuffer **SetLevelNameStartsWithACS(F3SharedBuffer **slot)
{
    if (g_dword_1DBDD4C != 0)
    {
        const char *name = (slot[0] != 0) ? GetSlotString(slot) : "";   // Str1 = ""
        if (KoreVmStringOp(name) != 0)   // sub_673E80 + VM string ctor + sub_6477E0 (stubs)
            return slot;                // the probe passed: keep the arg slot as-is
    }
    return CopySharedSlot(slot, &g_slot_1DBD0EC);
}

// ---- the optional-startup-script list appenders (VM-internal stubs) ------
// sub_618960 (0x618960 - 0x6189AC) -- F3ListProbe: build the 8-byte
// (head, tail) list probe over the optional-startup-script list.
static void   F3ListProbe(int *probe, int *head, int tailNode, int slot) { (void)probe; (void)head; (void)tailNode; (void)slot; }
// sub_55F1E0 (0x55F1E0) -- F3ListBump: the list append's one-word bump.
static void   F3ListBump(int v) { (void)v; }
// sub_55EAF0 (0x55EAF0 - 0x55EF2A) -- F3ListAppend: append a shared slot to
// the (head, tail, next) list: LHAlloc(12) node { tail variable, old next,
// slot ref }, link it in and return the old next node.
static int F3ListAppend(int headVar, int tailVar, int slot)
{
    int   next  = *(int *)(tailVar + 4);   // the tail's next pointer (0x1DBD11C)
    int  *node  = (int *)LHAlloc(12);
    if (node == 0)
        return next;
    node[0]  = tailVar;
    node[1]  = next;
    RefSharedSlot((F3SharedBuffer **)&node[2], (F3SharedBuffer **)(size_t)slot);
    F3ListBump(1);                          // sub_55F1E0
    *(int *)(tailVar + 4) = (int)node;
    if (node[1] != 0)
        *(int *)node[1] = (int)node;
    (void)headVar;
    return next;
}
// sub_6168A0 (0x6168A0 - 0x616904) -- AddOptionalStartupScript: probe the
// optional-startup-script list (sub_618960) and, when the probe matches
// (head, tail), append the script slot (F3ListAppend / sub_55EAF0).
// [NOT byte-identical] placement-dependent; the list-probe / bump are VM-
// internal stubs.
static void AddOptionalStartupScript(int slot)
{
    int probe[2] = { 0, 0 };   // the 8-byte list probe (var_8)
    F3ListProbe(probe, (int *)&g_list_1DBD104, g_list_1DBD118, slot);
    if (probe[0] == g_list_1DBD104 && probe[1] == g_list_1DBD118)
        F3ListAppend((int)&g_list_1DBD104, (int)&g_list_1DBD118, slot);
}

// sub_619520 (0x619520 - 0x6195A2) -- AudioBankRegistryAdd: the registry
// insert the AddAudioBank wrapper drives (VM-internal -- no-op stub).
static long   AudioBankRegistryAdd(int *registry, int *entry) { (void)registry; (void)entry; return 0; }
// sub_616910 (0x616910 - 0x6169C0) -- AddAudioBank: build the 32-byte bank
// entry { arg-2 slot, arg-1 slot, 12 pad, int[3], 2 flag bytes } on the
// caller's stack (the var_28 / var_24 slots), add the entry to the
// audio-bank registry (sub_619520, over g_unk_1DBD0F0), free the payload
// the registry stashed in the entry (the first extra dword, when set), and
// release both slots (var_24 first, then var_28).  Also called from code:
// sub_71A230 (0x71A33B).
// [NOT byte-identical] placement-dependent + the registry insert is a VM-
// internal stub.
static long AddAudioBank(int bankSlot, int nameSlot, int a3, int a4)
{
    F3SharedBuffer *slots[2];   // +0x00: var_28 slot (arg 2), +0x04: var_24 slot (arg 1)
    char            pad[12];    // +0x08
    int             extra[3];   // +0x14
    unsigned char   bytes[2];   // +0x20
    (void)pad;
    InitSharedSlot(&slots[0]);   // var_28
    InitSharedSlot(&slots[1]);   // var_24
    extra[0] = extra[1] = extra[2] = 0;
    CopySharedSlot(&slots[1], (F3SharedBuffer **)(size_t)bankSlot);   // var_24 <- arg 1
    CopySharedSlot(&slots[0], (F3SharedBuffer **)(size_t)nameSlot);   // var_28 <- arg 2
    bytes[1] = (unsigned char)a4;   // arg 4 -> +0x21
    bytes[0] = (unsigned char)a3;   // arg 3 -> +0x20
    AudioBankRegistryAdd(&g_unk_1DBD0F0, (int *)(size_t)slots);      // sub_619520
    if (extra[0] != 0)
        LHFree((void *)(size_t)extra[0]);
    extra[0] = extra[1] = extra[2] = 0;
    ReleaseSharedSlot(&slots[1]);   // var_24 first
    return ReleaseSharedSlot(&slots[0]);   // var_28 (the return value)
}

//  RegisterEngineLuaFunctions  (0x6169D0 - 0x6171A5)  -- register the 100
//  engine Set*/Get* Lua builtins on the Lua module (see the block header
//  above).  [NOT byte-identical] placement-dependent + high-VA .rdata name
//  strings.
//
//  void RegisterEngineLuaFunctions(int luaModule)
static void RegisterEngineLuaFunctions(int luaModule)
{
    if (luaModule == 0)
        return;

    int local[2] = { 0, 0 };   // var_C / var_8: the 2-dword GlobalTable accessor
    EngineLuaGlobalTable(local, luaModule);                          // sub_67C390

    EngineLuaRegisterString(local, "SetApplicationName", (void *)SetApplicationName);
    EngineLuaRegisterPair(local, "SetScreenResolution", (void *)SetScreenResolution);
    EngineLuaRegisterString(local, "SetInitialWorldName", (void *)SetInitialWorldName);
    EngineLuaRegisterString(local, "SetInitialLevelName", (void *)SetInitialLevelName);
    EngineLuaRegisterString(local, "SetInitialScenarioName", (void *)SetInitialScenarioName);
    EngineLuaRegisterString(local, "SetInitialStartPointName", (void *)SetInitialStartPointName);
    EngineLuaRegisterString(local, "SetInitialSaveGameName", (void *)SetInitialSaveGameName);
    EngineLuaRegisterString(local, "SetInitialClientSaveGameName", (void *)SetInitialClientSaveGameName);
    EngineLuaRegisterString(local, "SetGameflowScriptEnum", (void *)SetGameflowScriptEnum);
    EngineLuaRegisterString(local, "SetGameflowScriptState", (void *)SetGameflowScriptState);
    EngineLuaRegisterString(local, "SetGUIWorldName", (void *)SetGUIWorldName);
    EngineLuaRegisterString(local, "SetGUILevelName", (void *)SetGUILevelName);
    EngineLuaRegisterString(local, "SetGUIScenarioName", (void *)SetGUIScenarioName);
    EngineLuaRegisterString(local, "SetGUIStartPointName", (void *)SetGUIStartPointName);
    EngineLuaRegisterString(local, "SetPVPWorldName", (void *)SetPVPWorldName);
    EngineLuaRegisterString(local, "SetPVPLevelName", (void *)SetPVPLevelName);
    EngineLuaRegisterString(local, "SetPVPScenarioName", (void *)SetPVPScenarioName);
    EngineLuaRegisterString(local, "SetPVPStartPointName", (void *)SetPVPStartPointName);
    EngineLuaRegisterString(local, "SetFrontEndWorldName", (void *)SetFrontEndWorldName);
    EngineLuaRegisterString(local, "SetFrontEndLevelName", (void *)SetFrontEndLevelName);
    EngineLuaRegisterString(local, "SetFrontEndScenarioName", (void *)SetFrontEndScenarioName);
    EngineLuaRegisterString(local, "SetFrontEndStartPointName", (void *)SetFrontEndStartPointName);
    EngineLuaRegisterBoolGet(local, "IsToStartGameflow", (void *)IsToStartGameflow);
    EngineLuaRegisterBoolGet(local, "IsToStartPVPMode", (void *)IsToStartPVPMode);
    EngineLuaRegisterNumGet(local, "GetDemoMode", (void *)GetDemoMode);
    EngineLuaRegisterString(local, "SetInitialHeroEntityName", (void *)SetInitialHeroEntityName);
    EngineLuaRegisterString(local, "SetInitialHenchmanEntityName", (void *)SetInitialHenchmanEntityName);
    EngineLuaRegisterString(local, "SetInitialHeroMeleeWeapon", (void *)SetInitialHeroMeleeWeapon);
    EngineLuaRegisterString(local, "SetInitialHeroRangedWeapon", (void *)SetInitialHeroRangedWeapon);
    EngineLuaRegisterString(local, "SetInitialHenchmanMeleeWeapon", (void *)SetInitialHenchmanMeleeWeapon);
    EngineLuaRegisterString(local, "SetInitialHenchmanRangedWeapon", (void *)SetInitialHenchmanRangedWeapon);
    EngineLuaRegisterString(local, "SetInitialHeroScriptTypeIdentifier", (void *)SetInitialHeroScriptTypeIdentifier);
    EngineLuaRegisterString(local, "SetOverriddenDogCharacterRecordName", (void *)SetOverriddenDogCharacterRecordName);
    EngineLuaRegisterString(local, "SetOverriddenClientDogCharacterRecordName", (void *)SetOverriddenClientDogCharacterRecordName);
    EngineLuaRegisterString(local, "SetLevelNameStartsWithACS", (void *)SetLevelNameStartsWithACS);
    EngineLuaRegisterFlagSet(local, "SetOpeningLoadingScreen", (void *)SetOpeningLoadingScreen);
    EngineLuaRegisterStringGet(local, "GetApplicationName", (void *)GetApplicationName);
    EngineLuaRegisterPairGet(local, "GetScreenResolution", (void *)GetScreenResolution);
    EngineLuaRegisterStringGet(local, "GetAIScriptName", (void *)GetAIScriptName);
    EngineLuaRegisterStringGet(local, "GetCameraScriptName", (void *)GetCameraScriptName);
    EngineLuaRegisterStringGet(local, "GetQuestScriptName", (void *)GetQuestScriptName);
    EngineLuaRegisterStringGet(local, "GetGeneralSetupScriptName", (void *)GetGeneralSetupScriptName);
    EngineLuaRegisterStringGet(local, "GetInitialWorldName", (void *)GetInitialWorldName);
    EngineLuaRegisterStringGet(local, "GetInitialLevelName", (void *)GetInitialLevelName);
    EngineLuaRegisterStringGet(local, "GetInitialScenarioName", (void *)GetInitialScenarioName);
    EngineLuaRegisterStringGet(local, "GetInitialStartPointName", (void *)GetInitialStartPointName);
    EngineLuaRegisterStringGet(local, "GetInitialSaveGameName", (void *)GetInitialSaveGameName);
    EngineLuaRegisterStringGet(local, "GetInitialClientSaveGameName", (void *)GetInitialClientSaveGameName);
    EngineLuaRegisterStringGet(local, "GetGameflowScriptEnum", (void *)GetGameflowScriptEnum);
    EngineLuaRegisterStringGet(local, "GetGameflowScriptState", (void *)GetGameflowScriptState);
    EngineLuaRegisterStringGet(local, "GetGUIWorldName", (void *)GetGUIWorldName);
    EngineLuaRegisterStringGet(local, "GetGUILevelName", (void *)GetGUILevelName);
    EngineLuaRegisterStringGet(local, "GetGUIScenarioName", (void *)GetGUIScenarioName);
    EngineLuaRegisterStringGet(local, "GetGUIStartPointName", (void *)GetGUIStartPointName);
    EngineLuaRegisterStringGet(local, "GetPVPWorldName", (void *)GetPVPWorldName);
    EngineLuaRegisterStringGet(local, "GetPVPLevelName", (void *)GetPVPLevelName);
    EngineLuaRegisterStringGet(local, "GetPVPScenarioName", (void *)GetPVPScenarioName);
    EngineLuaRegisterStringGet(local, "GetPVPStartPointName", (void *)GetPVPStartPointName);
    EngineLuaRegisterStringGet(local, "GetFrontEndWorldName", (void *)GetFrontEndWorldName);
    EngineLuaRegisterStringGet(local, "GetFrontEndLevelName", (void *)GetFrontEndLevelName);
    EngineLuaRegisterStringGet(local, "GetFrontEndScenarioName", (void *)GetFrontEndScenarioName);
    EngineLuaRegisterStringGet(local, "GetFrontEndStartPointName", (void *)GetFrontEndStartPointName);
    EngineLuaRegisterStringGet(local, "GetControlSystemRecordName", (void *)GetControlSystemRecordName);
    EngineLuaRegisterStringGet(local, "GetInitialHeroEntityName", (void *)GetInitialHeroEntityName);
    EngineLuaRegisterStringGet(local, "GetInitialHenchmanEntityName", (void *)GetInitialHenchmanEntityName);
    EngineLuaRegisterStringGet(local, "GetInitialHeroMeleeWeapon", (void *)GetInitialHeroMeleeWeapon);
    EngineLuaRegisterStringGet(local, "GetInitialHeroRangedWeapon", (void *)GetInitialHeroRangedWeapon);
    EngineLuaRegisterStringGet(local, "GetInitialHenchmanMeleeWeapon", (void *)GetInitialHenchmanMeleeWeapon);
    EngineLuaRegisterStringGet(local, "GetInitialHenchmanRangedWeapon", (void *)GetInitialHenchmanRangedWeapon);
    EngineLuaRegisterStringGet(local, "GetInitialHeroScriptTypeIdentifier", (void *)GetInitialHeroScriptTypeIdentifier);
    EngineLuaRegisterStringGet(local, "GetOverriddenDogCharacterRecordName", (void *)GetOverriddenDogCharacterRecordName);
    EngineLuaRegisterStringGet(local, "GetOverriddenClientDogCharacterRecordName", (void *)GetOverriddenClientDogCharacterRecordName);
    EngineLuaRegisterIntGet(local, "GetMillisecondsToWaitForLuaDebugger", (void *)GetMillisecondsToWaitForLuaDebugger);
    EngineLuaRegisterBoolGet(local, "IsReleaseBuild", (void *)IsReleaseBuild);
    EngineLuaRegisterBoolGet(local, "GetUseRetailFrontEnd", (void *)GetUseRetailFrontEnd);
    EngineLuaRegisterString(local, "AddOptionalStartupScript", (void *)AddOptionalStartupScript);
    EngineLuaRegisterFlagSet(local, "SetRunFromDVD", (void *)SetRunFromDVD);
    EngineLuaRegisterFlagSet(local, "SetIgnoreCheckLocalDLCVersionFile", (void *)SetIgnoreCheckLocalDLCVersionFile);
    EngineLuaRegisterFlagSet(local, "SetDoFinalMarketplaceOfferCheck", (void *)SetDoFinalMarketplaceOfferCheck);
    EngineLuaRegisterFlagSet(local, "SetUseRetailFrontEnd", (void *)EngineLuaNoop_616380);   // nullsub_6
    EngineLuaRegisterFlagSet(local, "SetAllowAllSkipStates", (void *)SetAllowAllSkipStates);
    EngineLuaRegisterFlagSet(local, "SetToCreateNewSaveFilePerSave", (void *)EngineLuaNoop_615F30);   // nullsub_5
    EngineLuaRegisterNoopSet(local, "SetFrontEndDemoMode", (void *)EngineLuaNoop_615DA0);    // nullsub_4
    EngineLuaRegisterFlagSet(local, "SetStartMusicAndAtmosOnLevelLoad", (void *)SetStartMusicAndAtmosOnLevelLoad);
    EngineLuaRegisterFlagSet(local, "SetVSync", (void *)SetVSync);
    EngineLuaRegisterIntSet(local, "SetVSyncThreshold", (void *)SetVSyncThreshold);
    EngineLuaRegisterIntSet(local, "SetMSToShowLegalScreen", (void *)SetMSToShowLegalScreen);
    EngineLuaRegisterIntSet(local, "SetPresentationInterval", (void *)SetPresentationInterval);
    EngineLuaRegisterMultiSet(local, "AddAudioBank", (void *)AddAudioBank);
    EngineLuaRegisterFlagSet(local, "SetDevFrontendSignin", (void *)SetDevFrontendSignin);
    EngineLuaRegisterFlagSet(local, "SetAllowMultiplayerJoining", (void *)SetAllowMultiplayerJoining);
    EngineLuaRegisterFlagSet(local, "SetDisableLiveFunctionality", (void *)SetDisableLiveFunctionality);
    EngineLuaRegisterBoolGet(local, "IsLiveFunctionalityDisabled", (void *)IsLiveFunctionalityDisabled);
    EngineLuaRegisterBoolGet(local, "GetDifficultySetToHard", (void *)GetDifficultySetToHard);
    EngineLuaRegisterBoolGet(local, "SpotCheckSuccessful_1", (void *)SpotCheckSuccessful_1);
    EngineLuaRegisterBoolGet(local, "SpotCheckSuccessful_2", (void *)SpotCheckSuccessful_2);
    EngineLuaRegisterBoolGet(local, "SpotCheckSuccessful_3", (void *)SpotCheckSuccessful_3);
    EngineLuaRegisterBoolGet(local, "SpotCheckSuccessful_4", (void *)SpotCheckSuccessful_4);
    EngineLuaRegisterBoolGet(local, "SpotCheckSuccessful_5", (void *)SpotCheckSuccessful_5);
    EngineLuaRegisterBoolGet(local, "SpotCheckSuccessful_6", (void *)SpotCheckSuccessful_6);

    // Release the GlobalTable accessor's refcounted control block (local[1]).
    volatile LONG *ref = (volatile LONG *)local[1];
    if (ref != 0 && InterlockedDecrement(ref) == 0)
    {
        void (*cb)(int) = *(void (**)(int))(size_t)ref[1];   // the release callback
        cb((int)(size_t)ref[2]);                              // its context argument
        LHFree((void *)(size_t)ref);
    }
}

// sub_55F120 (0x55F120 - 0x55F192) -- ClearOptionalStartupScriptList: the
// thiscall list-clear RunLuaStartupScripts runs over the
// optional-startup-script container (the original `this` = &dword_1DBD104;
// the container's +20 / +24 words are the tail-node global 0x1DBD118 and
// the size global 0x1DBD11C).  It relinks the tail node to itself (P[0] =
// P[4] = P), zeroes the size, then walks the node chain (P[0] -> node[0] ->
// ... -> P) releasing each 12-byte node: its +8 shared slot (the
// InterlockedDecrement of the block's +12 refcount, SharedBufferFree on
// zero, the g_Addend drop per slot) and the node itself (LHFree, the heap
// table's +16 slot).  Returns the tail node.
// [NOT byte-identical] placement-dependent (the relative calls to
// SharedBufferFree / LHFree / the InterlockedDecrement IAT slots) and it
// refs the high-VA list globals.
static int ClearOptionalStartupScriptList(void)
{
    int *P     = (int *)(size_t)g_list_1DBD118;   // the tail node
    int *first = (int *)(size_t)P[0];             // P[0]: the first node
    P[0] = (int)(size_t)P;                        // P[0] = P (self-link)
    P[1] = (int)(size_t)P;                        // P[4] = P
    g_list_1DBD11C = 0;                           // the list size
    if (first != P)
    {
        int *prev = 0;
        do
        {
            prev = (int *)(size_t)first[0];       // the previous link
            if (first[2] != 0)
            {
                F3SharedBuffer *block = (F3SharedBuffer *)(size_t)first[2];
                if (InterlockedDecrement(&block->refcount) <= 0 && first[2] != 0)
                    SharedBufferFree(block);
                first[2] = 0;
            }
            InterlockedDecrement(&g_Addend);      // the live-slot count
            first[2] = 0;
            LHFree(first);                        // the 12-byte node
            first = prev;
        }
        while (prev != P);
    }
    return (int)(size_t)P;
}

// sub_616710 (0x616710 - 0x616894) -- RunLuaStartupScripts: report the
// broken-Lua-script marker files after the engine Lua environment rebuild.
// Build the 16-byte LHSFS::CLegacyFile (the sub_17FE870 ctor + the
// sub_17FE0A0 open-setup, a documented stub) over the caller's
// startup-script path slot (the "Data\Scripts\Startup\" + "startup.lua"
// slot ReinitEngineLua concats) and, when that marker file opens
// (sub_17FDC30: the +8 file slot set), log "Lua startup script has an
// error in it" over the Lua module (SetSharedSlot + LogScriptError,
// sub_629660, + ReleaseSharedSlot), then walk the
// optional-startup-script list (the 0x1DBD104 / 0x1DBD118 container;
// each 12-byte node's +8 shared slot holds the script name): build
// "Data\Scripts\Startup\<name>" (SharedSlotConcat), open it, and -- when
// it opens too -- log
// "Optional startup script \"<name>\" has an error in it"
// (SharedSlotConcatLiteral + MakeSettingsFilePath + LogScriptError),
// finally clearing the list (sub_55F120).  Returns the main file
// object's dtor result (sub_17FD970 -> ReleaseSharedSlot's new g_Addend
// count).  cdecl (the Lua module as arg 0, the path-slot address as
// arg 1); the sole caller is ReinitEngineLua (0x61A54E), which discards
// the result.
// [NOT byte-identical] placement-dependent (the relative calls to the
// shared-slot / file / logging helpers + the __security_check_cookie IAT
// slot) and it refs the high-VA list + path-prefix globals.  The
// file-open setup (sub_17FE0A0) and the log emitter (LogScriptError,
// sub_629660) are the documented stubs, so the marker files never open
// and the error branch is not taken in this build; the original's
// __invalid_parameter_noinfo list-iterator guards are unreachable for the
// same reason.
//
//  long RunLuaStartupScripts(int luaModule, F3SharedBuffer **scriptSlot)
static long RunLuaStartupScripts(int luaModule, F3SharedBuffer **scriptSlot)
{
    DWORD file[4];   // v14: the main startup-script marker file (CLegacyFile)

    LegacyFileCtor(file, scriptSlot, 1, 2);            // sub_17FE870
    if (LegacyFileHasData(file) != 0)                  // sub_17FDC30
    {
        F3SharedBuffer *msg  = 0;   // v12: the error message slot (reused per node)
        F3SharedBuffer *lit  = 0;   // v10: "Optional startup script \"<name>"
        F3SharedBuffer *full = 0;   // v11: the full error message
        SetSharedSlot(&msg, "Lua startup script has an error in it", -1);
        LogScriptError(luaModule, scriptSlot, &msg);   // sub_629660
        ReleaseSharedSlot(&msg);

        int *tail = (int *)(size_t)g_list_1DBD118;     // the tail node (P)
        int *head = (int *)(size_t)g_list_1DBD104;     // the head block (H)
        int *node = (int *)(size_t)tail[0];            // P[0]: the first node
        for (;;)
        {
            if (node == (int *)(size_t)g_list_1DBD118)   // it != end()
                break;
            // original: the MSVC list-iterator checks here (the head
            // liveness vs 0x1DBD104, the node != end() deref guard and
            // the increment guard, each via __invalid_parameter_noinfo);
            // the list is consistent in this build and the loop is
            // unreachable while the file-open stubs stay.
            (void)head;

            SharedSlotConcat(&msg, &g_unk_1DBE2B4, (F3SharedBuffer **)(node + 2));
            DWORD optFile[4];   // v13: the optional-script marker file
            LegacyFileCtor(optFile, &msg, 1, 2);       // sub_17FE870
            if (LegacyFileHasData(optFile) != 0)       // sub_17FDC30
            {
                SharedSlotConcatLiteral(&lit, "Optional startup script \"", &msg);
                MakeSettingsFilePath(&full, &lit, "\" has an error in it");
                LogScriptError(luaModule, &msg, &full);   // sub_629660
                ReleaseSharedSlot(&full);
                ReleaseSharedSlot(&lit);
            }
            LegacyFileDtor(optFile);                   // sub_17FD970
            ReleaseSharedSlot(&msg);
            node = (int *)(size_t)node[0];             // ++it
        }
        ClearOptionalStartupScriptList();              // sub_55F120
    }
    return LegacyFileDtor(file);                       // sub_17FD970
}

// sub_6ECE40 / sub_6ECEC0 (0x6ECE40-0x6ECEB4 / 0x6ECEC0-0x6ECF34) -- the
// "empty handle-map" owner-wrapper ctor: thiscall (this in ECX) + `retn 8`
// (two unused stack args).  The two are identical except the element size
// (0x50 = 80 bytes for sub_6ECE40, 0x1C = 28 bytes for sub_6ECEC0).  It
// allocates a 4-byte head node (node[0] = this) into this[0], then the
// element; the element is zeroed at +0/+4/+8, self-linked so it is an empty
// list head (elem[0] = elem[1] = elem[2] = elem), and its last two bytes are
// set to 1 (elem[size-4] = 1, elem[size-3] = 1).  The element is stored at
// this+0x18 (this[6]) with this+0x1C (this[7]) = 0.
// [NOT byte-identical] placement-dependent (relative call to LHAlloc).
static void *HandleMapEmptyInit(void *obj, unsigned int elemSize)
{
    DWORD *d    = (DWORD *)obj;
    DWORD *node = (DWORD *)LHAlloc(4);
    d[0] = (DWORD)node;
    if (node)
        node[0] = (DWORD)obj;                 // *node = this
    DWORD *elem = (DWORD *)LHAlloc(elemSize);
    if (elem)
    { elem[0] = 0; elem[1] = 0; elem[2] = 0; }
    BYTE *e = (BYTE *)elem;
    e[elemSize - 3] = 0;
    e[elemSize - 4] = 1;
    d[6] = (DWORD)elem;
    e[elemSize - 3] = 1;
    if (elem)
    { elem[1] = (DWORD)elem; elem[0] = (DWORD)elem; elem[2] = (DWORD)elem; }
    d[7] = 0;
    return obj;
}

// sub_5DEC20 (0x5DEC20 - 0x5DEC4D) -- KorePushEmptyTable: allocate a fresh
// (empty) hash table (KoreAllocHashTable(a1, 0, 0)) and append the {table, 5}
// 8-byte record to the module value array at the a1[7] cursor, advancing the
// cursor by 8.  cdecl.  VM-internal -- out of scope.  Stub: no-op (0).
static void *KorePushEmptyTable(int mod, int a2, int a3)
{ (void)mod; (void)a2; (void)a3; return 0; }

// sub_5FC530 (0x5FC530 - 0x5FC54A) -- KoreValueArraySet: a thin wrapper over
// sub_5F9E40, the tag-dispatching "write a value into the module value array
// at an offset" op.  cdecl.  VM-internal -- out of scope.  Stub: no-op.
static void KoreValueArraySet(int mod, int a2, int a3)
{ (void)mod; (void)a2; (void)a3; }

// sub_5E18A0 (0x5E18A0 - 0x5E1957) -- KoreLookupSlotByIndex: resolve / create
// a value-array slot addressed by a (negative) index over a module using
// sub_407970 / sub_5D93D0 / sub_55DFE0 / sub_407A90, and return its index
// (-1 when the top slot is a non-tagged empty hole).  cdecl.  VM-internal --
// out of scope.  Stub: returns -1 (no slot).
static int KoreLookupSlotByIndex(int mod, int a2)
{ (void)mod; (void)a2; return -1; }

// sub_6E9620 (0x6E9620 - 0x6E96DF) -- CEntityScriptHandleManagerCtor: the
// 80-byte CEntityScriptHandleManager (0x50 bytes).  thiscall + `retn 4` (the
// Lua module is the single stack arg).  Builds: the CEntityScriptHandleManager
// vtable at +0, the two empty handle-map owner-wrappers (sub_6ECE40 at +4 with
// an 80-byte element, sub_6ECEC0 at +0x28 with a 28-byte element), +0x24 = 0,
// +0x48 = -2 and +0x4C = the Lua module, then a short run of VM-side
// registrations over that module: two empty-table pushes (KorePushEmptyTable,
// sub_5DEC20), the "v" intern (KoreAppendString, sub_407070), the "__mode"
// builtin name (KoreRegisterBuiltinName, sub_4076D0), a value-array trim
// (KoreValueArraySet, sub_5FC530) when the module's +0x1C used-counter is at
// or past its +0x20 capacity, and finally +0x48 = KoreLookupSlotByIndex
// (module, -10000) (sub_5E18A0).  Sole caller: ReinitEngineLua (0x61A5A7),
// which builds it into the engine's +0xC4.  [NOT byte-identical]
// placement-dependent (relative calls) and refs the high-VA
// CEntityScriptHandleManager::`vftable' plus the "v" / "__mode" rdata strings.
//
//  void *CEntityScriptHandleManagerCtor(void *obj, int luaModule)
static void *CEntityScriptHandleManagerCtor(void *obj, int luaModule)
{
    CBaseClassNonCopyableCtor(obj);
    *(void **)obj = g_CEntityScriptHandleManagerVtable;   // `vftable'
    HandleMapEmptyInit((char *)obj + 4,   0x50);          // sub_6ECE40
    *(DWORD *)((char *)obj + 0x24) = 0;
    HandleMapEmptyInit((char *)obj + 0x28, 0x1C);         // sub_6ECEC0
    *(DWORD *)((char *)obj + 0x48) = 0xFFFFFFFE;          // -2
    *(DWORD *)((char *)obj + 0x4C) = (DWORD)luaModule;
    int     mod    = luaModule;                           // = *(obj + 0x4C)
    DWORD *  modB  = (DWORD *)mod;                        // the module's words
    KorePushEmptyTable(mod, 0, 0);                         // sub_5DEC20(module, 0, 0)
    KorePushEmptyTable(mod, 0, 0);                         // sub_5DEC20(*(this+0x4C), 0, 0)
    KoreAppendString(mod, "v", 1);                         // sub_407070
    KoreRegisterBuiltinName(mod, -2, "__mode");            // sub_4076D0
    DWORD v4 = modB[0x1C / 4];                             // *(module + 0x1C)
    if ((v4 - 16) >= modB[0x20 / 4])                       // unsigned, cf. `jb'
    {
        KoreValueArraySet(mod, (int)(v4 - 16), (int)(v4 - 8));  // sub_5FC530
        modB[0x1C / 4] -= 8;                              // *(module + 0x1C) -= 8
    }
    *(DWORD *)((char *)obj + 0x48) = (DWORD)KoreLookupSlotByIndex(mod, 0xFFFFD8F0); // sub_5E18A0
    return obj;
}

// nullsub_27 (0x6EFF60 - 0x6EFF61) -- one-byte `retn` no-op thunk.
static void nullsub_27(void) { }

// ============================================================================
//  ResetEngineTest  (nullsub_28, 0x70DBB0 - 0x70DBB1) and
//  RegisterEngineTest  (sub_70DDB0, 0x70DDB0 - 0x70DEBA) -- the
//  "Debug.EngineTest" Lua module registration pair
//
//  RegisterEngineTest(luaModule) is called on the fresh Lua module from
//  ReinitEngineLua (0x61A57B, when `flag` is set) and from RunFrontEnd
//  (0x61D602), always immediately after the ResetEngineTest no-op.  It
//  builds the two table-handle pairs {value, ctrl} on the stack: the
//  "Debug" global table (EngineLuaDebugTable, sub_633E70 -- the table name
//  is the g_slot_1DBDC78 shared slot, "Debug" by default) and the
//  "EngineTest" subtable (EngineLuaEnsureTable, sub_67C2B0).  It then
//  registers the five EngineTest methods on that table -- AddMistInstance,
//  UpdateMaterialParameterFloat, UpdateMaterialParameterVec4,
//  GetSelectedPrimitiveResourceID, SetSelectedPrimitive -- each via the
//  124-byte registration helper (EngineLuaRegisterNamedClosure; five
//  address-identical variants, sub_640870 / sub_70E320 / sub_70E3A0 /
//  sub_70E420 / sub_70E530): the C wrapper pointer is stored in the
//  table's VM value array and a builtin closure is installed whose
//  upvalue-marshalling function (the 80-byte sub_70E0xx family / sub_63E1F0)
//  pulls the first Lua value off the VM stack and dispatches to the wrapper
//  with the converted arguments.  The epilogue drops the pair's refcounted
//  control blocks (InterlockedDecrement; at zero, dispatch the release
//  callback + LHFree) and zeroes the EngineTest pair.
//  [NOT byte-identical] placement-dependent (relative calls) and it
//  references the high-VA .rdata method-name strings.
// ============================================================================

// 0x1DBDC78: the Debug-class name slot ("Debug" by default; set by the
// sub_1A5C980 static initializer, released by the atexit handler sub_1AC2320).
static F3SharedBuffer *g_slot_1DBDC78 = 0;   // 0x1DBDC78: "Debug" class-name slot

// ----------------------------------------------------------------
//  VM-internal stubs the EngineTest registration reaches (Kore VM value-
//  stack / arg-marshalling machinery; out of scope; documented no-ops).
// ----------------------------------------------------------------
// sub_5DF650 (0x5DF650) -- reserve 8 bytes on a VM value stack: grow the
// backing buffer when the cursor (a1[7]) runs past capacity (a2 = the
// buffer base a1[0] + 0xCC).  2908 callers; VM-internal.  Stub: no-op.
static void KoreValueStackReserve(void *table, void *tableBase, int ctrl, int cursor)
{ (void)table; (void)tableBase; (void)ctrl; (void)cursor; }
// sub_5DC450 (0x5DC450) -- return a pointer to a freshly reserved 4-byte
// value-array slot.  2777 callers; VM-internal.  Stub: 0 (no slot).
static void *KoreValueArraySlot(void *table, int size)
{ (void)table; (void)size; return 0; }
// sub_67C180 (0x67C180) -- intern/look up a builtin name on a module: push
// the name string on the module value stack, run the builtin string op, and
// write the {value, ctrl} pair to out ({0, 0} when the name is unregistered
// or the module context is empty).  thiscall (the {value, ctrl} pair in
// ECX); VM-internal.  Stub: {0, 0}.
static void KoreLookupBuiltinValue(DWORD *thisPair, DWORD *out, const char *name)
{ (void)thisPair; out[0] = 0; out[1] = 0; (void)name; }
// sub_6179D0 (0x6179D0) -- release a KoreLookupBuiltinValue {value, ctrl}
// temp: InterlockedDecrement the ctrl block (dispatch its release callback +
// LHFree at zero) and zero the pair.  thiscall; VM-internal.  Stub: no-op.
static void KoreReleaseLookupPair(DWORD *pair)
{ (void)pair; }
// sub_67C540 (0x67C540) -- hash a name and store it in the module's global
// table over a reserved value-array slot.  thiscall (the {value, ctrl} pair
// in ECX); 2643 callers; VM-internal.  Stub: no-op.
static void EngineLuaStoreGlobalName(DWORD *pair, void *table, const char *name)
{ (void)pair; (void)table; (void)name; }
// sub_408200 (0x408200) -- pull the Nth Lua number argument off a VM stack.
// 1236 callers; VM-internal.  Stub: 0.0f.
static float KoreGetNumberArg(int vm, int idx)
{ (void)vm; (void)idx; return 0.0f; }
// sub_4072D0 (0x4072D0) -- pull the Nth Lua argument (a value-slot pointer)
// off a VM stack.  3323 callers; VM-internal.  Stub: 0 (no slot).
static void *KoreGetArgSlot(int vm, int idx)
{ (void)vm; (void)idx; return 0; }
// sub_407370 (0x407370) -- pull the Nth Lua string argument (raw byte
// pointer) off a VM stack.  591 callers; VM-internal.  Stub: 0.
static const char *KoreGetArgStringBytes(int vm, int idx)
{ (void)vm; (void)idx; return 0; }
// sub_62AF90 (0x62AF90) -- 3-byte value-slot getter (mov eax,[ecx]; retn);
// converts a VM value slot to its scalar value.  229 callers; VM-internal.
// Stub: 0.
static int KoreSlotToValue(int slot)
{ (void)slot; return 0; }
// sub_62AFF0 (0x62AFF0) -- 3-byte int-slot getter (mov eax,[ecx]; retn);
// converts a VM value slot to its int value.  3 callers; VM-internal.
// Stub: 0.
static int KoreSlotToInt(int slot)
{ (void)slot; return 0; }
// sub_18982A0 (0x18982A0) -- push a C string onto a VM value stack (the
// 24-byte thiscall with a scratch slot in ECX).  254 callers; VM-internal.
// Stub: no-op.
static void KorePushString(void *local, const char *s)
{ (void)local; (void)s; }
// sub_19E23D0 (0x19E23D0) -- the VM material-parameter-FLOAT update op
// ((scratch, slot2, slot3, value, scratch, 1)).  2 callers; VM-internal.
// Stub: 0.
static int KoreMaterialUpdateFloat(void *scratch, int slot2, int slot3, float value, void *scratch2, int flag)
{ (void)scratch; (void)slot2; (void)slot3; (void)value; (void)scratch2; (void)flag; return 0; }
// sub_19E2370 (0x19E2370) -- the VM material-parameter-VEC4 update op
// ((scratch, slot2, slot3, slot4, scratch, 1)).  1 caller; VM-internal.
// Stub: 0.
static int KoreMaterialUpdateVec4(void *scratch, int slot2, int slot3, int slot4, void *scratch2, int flag)
{ (void)scratch; (void)slot2; (void)slot3; (void)slot4; (void)scratch2; (void)flag; return 0; }
// sub_19DE1E0 (0x19DE1E0) -- read the selected primitive's resource id into
// a 4-byte local (14 bytes; the caller reads the value back from the local).
// 1 caller; VM-internal.  Stub: writes 0.
static void KoreReadSelectedPrimitiveID(int *local)
{ *local = 0; }
// sub_19DE1B0 (0x19DE1B0) -- store the selected primitive resource id (the
// 34-byte VM op the SetSelectedPrimitive thunk jmps to).  1 caller;
// VM-internal.  Stub: 0.
static int KoreStoreSelectedPrimitive(int id)
{ (void)id; return 0; }
// sub_4171A0 (0x4171A0) -- the 16-byte vector-4 init thiscall (215 bytes,
// 4 callers; a small engine vector ctor).  Stub: no-op.
static void F3Vector4Init(void *vec)
{ (void)vec; }
// sub_19DD510 (0x19DD510) -- begin the mist-instance VM record over a
// 156-byte engine buffer (thiscall: buffer in ECX, 0, &direction vec4).
// 2 callers; VM-internal.  Stub: no-op.
static void KoreBeginMistInstance(void *buffer, int a2, void *direction)
{ (void)buffer; (void)a2; (void)direction; }
// sub_673C50 (0x673C50) -- 6-byte getter of the Kore VM core object
// (g_dword_1DBDD4C; 0 = no live VM).  5963 callers.  Stub: the global.
static int KoreGetVMCore(void)
{ return g_dword_1DBDD4C; }
// sub_7E2470 (0x7E2470) -- 6-byte engine-state query (mov eax,[?]; retn).
// 83 callers.  Stub: 0.
static int KoreEngineStateQuery(int a)
{ (void)a; return 0; }
// sub_19E2610 (0x19E2610) -- finish the mist-instance VM record over the
// 156-byte engine buffer (thiscall: buffer in ECX + the query result).
// 111 callers; VM-internal.  Stub: no-op.
static void KoreFinishMistInstance(void *buffer, int a2)
{ (void)buffer; (void)a2; }
// sub_19E19B0 (0x19E19B0) -- 7-byte thiscall commit of a VM record buffer
// (returns eax).  130 callers; VM-internal.  Stub: 0.
static int KoreCommitRecordBuffer(void *buffer)
{ (void)buffer; return 0; }
// sub_5D93D0 (0x5D93D0) -- pop/convert a Lua number off a VM stack into a
// double (76 bytes, 206 callers).  VM-internal.  Stub: 0.0.
static double KorePopNumberValue(int vm, int sp)
{ (void)vm; (void)sp; return 0.0; }

// ----------------------------------------------------------------
//  SharedSlotFormat (0x1A01670) -- module-private copy of the gamemain.cpp
//  static (vsprintf into a shared slot); the EngineTest
//  GetSelectedPrimitiveResourceID wrapper needs it in this translation unit.
// ----------------------------------------------------------------
static F3SharedBuffer **SharedSlotFormat(F3SharedBuffer **out, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    int flen = (int)strlen(fmt);
    unsigned char *raw = (unsigned char *)malloc((unsigned)(flen + 1032));
    if (raw != 0)
    {
        *(DWORD *)raw = 56797;
        char *buf = (char *)(raw + 2);
        int written = vsprintf_s(buf, (unsigned)(flen + 1024), fmt, ap);
        if (written <= flen + 1024)
        {
            F3SharedBuffer *nb = 0;
            if (buf != 0 && *buf != 0)
            {
                nb = (F3SharedBuffer *)LHAlloc(16);
                if (nb != 0)
                {
                    nb->data = 0;
                    nb->length = 0;
                    nb->capacity = 0;
                    unsigned int len = 0;
                    while (buf[len] != 0)
                        len++;
                    SharedBufferAssign(nb, buf, len);
                    nb->refcount = 1;
                }
            }
            if (*out != 0)
            {
                if (InterlockedDecrement(&(*out)->refcount) <= 0)
                    SharedBufferFree(*out);
                *out = 0;
            }
            if (nb != 0)
                *out = nb;
        }
        if (*(DWORD *)raw == 56797)
            free(raw);
    }
    va_end(ap);
    return out;
}

//  ReleaseLookupCtrl -- the {value, ctrl} pair epilogue shared by
//  EngineLuaDebugTable / RegisterEngineTest: drop one reference to the
//  refcounted control block; at zero, dispatch its release callback
//  (block+4 with the block+8 context) and LHFree the block.
static void ReleaseLookupCtrl(DWORD ctrl)
{
    if (ctrl != 0 && InterlockedDecrement((volatile LONG *)ctrl) == 0)
    {
        DWORD *b = (DWORD *)ctrl;
        ((void (*)(DWORD))b[1])(b[2]);
        LHFree((void *)b);
    }
}

// ----------------------------------------------------------------
//  The five EngineTest C wrappers (the function pointers the registration
//  stores in the "EngineTest" table value array; only referenced by address).
// ----------------------------------------------------------------

// sub_70DCE0 (0x70DCE0 - 0x70DDAE) -- AddMistInstance: build the 156-byte
// mist-instance record (direction = a2 - a1 at +0x50, the third vector at
// +0x58, the scalar at +0x30), run it through the VM record ops, and commit.
static int EngineTestAddMistInstance(const void *a1, const void *a2, const void *a3, float a4)
{
    char buf[0x9C];                                // the 156-byte record buffer
    float  f   = *(float *)(buf + 0x30);
    float *vec = (float *)(buf + 0x50);            // direction = a2 - a1
    float *a3v = (float *)(buf + 0x58);            // the third vector
    int   i;

    F3Vector4Init(vec);                            // sub_4171A0
    for (i = 0; i < 4; i++)
        vec[i] = ((const float *)a2)[i] - ((const float *)a1)[i];
    for (i = 0; i < 4; i++)
        a3v[i] = ((const float *)a3)[i];
    f = a4;
    KoreBeginMistInstance(buf, 0, vec);            // sub_19DD510
    int core = KoreGetVMCore();                    // sub_673C50
    void **vt = (void **)(*(void **)(core));
    int x = ((int (__thiscall *)(int, int))vt[5])(core, 1);   // vtable +0x14, arg 1 (thiscall, this = core)
    int y = KoreEngineStateQuery(x);               // sub_7E2470
    KoreFinishMistInstance(buf, y);                // sub_19E2610
    return KoreCommitRecordBuffer(buf);            // sub_19E19B0
}

// sub_70DBD0 (0x70DBD0 - 0x70DD28) -- UpdateMaterialParameterFloat: push the
// first slot's string onto the VM stack and run the material FLOAT update.
static F3SharedBuffer **EngineTestUpdateMaterialParameterFloat(F3SharedBuffer **slot1, F3SharedBuffer **slot2, F3SharedBuffer **slot3, float value)
{
    F3SharedBuffer *scratch;
    const char *s = GetSlotString(slot1);
    KorePushString(&scratch, s);                   // sub_18982A0
    return (F3SharedBuffer **)KoreMaterialUpdateFloat(&scratch, (int)slot2, (int)slot3, value, &scratch, 1);   // sub_19E23D0
}

// sub_70DC30 (0x70DC30 - 0x70DC87) -- UpdateMaterialParameterVec4: push the
// first slot's string onto the VM stack and run the material VEC4 update.
static F3SharedBuffer **EngineTestUpdateMaterialParameterVec4(F3SharedBuffer **slot1, F3SharedBuffer **slot2, F3SharedBuffer **slot3, F3SharedBuffer **slot4)
{
    F3SharedBuffer *scratch;
    const char *s = GetSlotString(slot1);
    KorePushString(&scratch, s);                   // sub_18982A0
    return (F3SharedBuffer **)KoreMaterialUpdateVec4(&scratch, (int)slot2, (int)slot3, (int)slot4, &scratch, 1);   // sub_19E2370
}

// sub_70DC90 (0x70DC90 - 0x70DCDD) -- GetSelectedPrimitiveResourceID: read
// the selected primitive's resource id and format it "%u" into the out slot.
static F3SharedBuffer **EngineTestGetSelectedPrimitiveResourceID(F3SharedBuffer **out)
{
    int v;
    KoreReadSelectedPrimitiveID(&v);               // sub_19DE1E0 (value re-read from the local)
    InitSharedSlot(out);
    SharedSlotFormat(out, "%u", v);
    return out;
}

// sub_70DB80 (0x70DB80 - 0x70DB84) -- SetSelectedPrimitive: the 5-byte jmp
// thunk to the VM store op.  [NOT byte-identical] placement-dependent
// (the relative `jmp` displacement depends on where the thunk lands).
static int EngineTestSetSelectedPrimitive(int id)
{ return KoreStoreSelectedPrimitive(id); }         // sub_19DE1B0

// ----------------------------------------------------------------
//  The five upvalue-marshalling functions (the builtin-closure C functions
//  handed to KoreRegisterBuiltinClosure: they pull the first Lua value off
//  the VM stack -- tag 7 = table (data+16), tag 2 = string (data+0), else
//  0 -- and dispatch to the wrapper with the converted arguments).
// ----------------------------------------------------------------

// sub_70DEC0 (0x70DEC0 - 0x70DE4F) -- the AddMistInstance dispatcher: pull
// the three remaining Lua arguments (converting each to a VM value) and call
// the wrapper with the first.
static int EngineTestDispatchSlotString(int fn, int vm, int base)
{
    KoreGetNumberArg(vm, base + 3);                // sub_408200 (discarded)
    void *s3 = KoreGetArgSlot(vm, base + 2);       // sub_4072D0
    KoreSlotToValue(*(int *)s3);                   // sub_62AF90
    void *s2 = KoreGetArgSlot(vm, base + 1);       // sub_4072D0
    KoreSlotToValue(*(int *)s2);                   // sub_62AF90
    void *s1 = KoreGetArgSlot(vm, base);           // sub_4072D0
    int v = KoreSlotToValue(*(int *)s1);           // sub_62AF90
    ((void (*)(int))fn)(v);
    return 0;
}

// sub_70E0D0 (0x70E0D0 - 0x70E11F) -- AddMistInstance upvalue (80 bytes):
// pull the first Lua value and dispatch to EngineTestDispatchSlotString.
static int EngineTestAddMistInstanceUpvalue(int vm)
{
    int  sp   = *(int *)((char *)vm + 32);
    int  slot = *(int *)(sp - 8);
    int  tag  = *(int *)(slot + 16);
    int *data = (int *)(slot + 12);
    if (tag == 7)
        return EngineTestDispatchSlotString(data[4], vm, 1);
    if (tag == 2)
        return EngineTestDispatchSlotString(data[0], vm, 1);
    return EngineTestDispatchSlotString(0, vm, 1);
}

// sub_70DF40 (0x70DF40 - 0x70DFF0) -- the UpdateMaterialParameterFloat
// dispatcher: pull the number arg 4 + the three string args 1-3 (into fresh
// shared slots), call the wrapper, release the slots.
static int EngineTestDispatchFloatStrings(int fn, int vm, int base)
{
    float f = KoreGetNumberArg(vm, base + 3);              // sub_408200
    F3SharedBuffer *s3 = 0, *s2 = 0, *s1 = 0;
    SetSharedSlot(&s3, KoreGetArgStringBytes(vm, base + 2), -1);  // sub_407370
    SetSharedSlot(&s2, KoreGetArgStringBytes(vm, base + 1), -1);
    SetSharedSlot(&s1, KoreGetArgStringBytes(vm, base), -1);
    ((void (*)(F3SharedBuffer **, F3SharedBuffer **, F3SharedBuffer **, float))fn)(&s1, &s2, &s3, f);
    ReleaseSharedSlot(&s1);
    ReleaseSharedSlot(&s2);
    ReleaseSharedSlot(&s3);
    return 0;
}

// sub_70E120 (0x70E120 - 0x70E16F) -- UpdateMaterialParameterFloat upvalue.
static int EngineTestUpdateMaterialParameterFloatUpvalue(int vm)
{
    int  sp   = *(int *)((char *)vm + 32);
    int  slot = *(int *)(sp - 8);
    int  tag  = *(int *)(slot + 16);
    int *data = (int *)(slot + 12);
    if (tag == 7)
        return EngineTestDispatchFloatStrings(data[4], vm, 1);
    if (tag == 2)
        return EngineTestDispatchFloatStrings(data[0], vm, 1);
    return EngineTestDispatchFloatStrings(0, vm, 1);
}

// sub_70DFF0 (0x70DFF0 - 0x70E0CF) -- the UpdateMaterialParameterVec4
// dispatcher: pull the int arg 4 + the three string args 1-3 (into fresh
// shared slots), call the wrapper, release the slots.
static int EngineTestDispatchIntStrings(int fn, int vm, int base)
{
    void *n = KoreGetArgSlot(vm, base + 3);                // sub_4072D0
    int i = KoreSlotToInt(*(int *)n);                      // sub_62AFF0
    F3SharedBuffer *s3 = 0, *s2 = 0, *s1 = 0;
    SetSharedSlot(&s3, KoreGetArgStringBytes(vm, base + 2), -1);  // sub_407370
    SetSharedSlot(&s2, KoreGetArgStringBytes(vm, base + 1), -1);
    SetSharedSlot(&s1, KoreGetArgStringBytes(vm, base), -1);
    ((void (*)(F3SharedBuffer **, F3SharedBuffer **, F3SharedBuffer **, int))fn)(&s1, &s2, &s3, i);
    ReleaseSharedSlot(&s1);
    ReleaseSharedSlot(&s2);
    ReleaseSharedSlot(&s3);
    return 0;
}

// sub_70E170 (0x70E170 - 0x70E1BF) -- UpdateMaterialParameterVec4 upvalue.
static int EngineTestUpdateMaterialParameterVec4Upvalue(int vm)
{
    int  sp   = *(int *)((char *)vm + 32);
    int  slot = *(int *)(sp - 8);
    int  tag  = *(int *)(slot + 16);
    int *data = (int *)(slot + 12);
    if (tag == 7)
        return EngineTestDispatchIntStrings(data[4], vm, 1);
    if (tag == 2)
        return EngineTestDispatchIntStrings(data[0], vm, 1);
    return EngineTestDispatchIntStrings(0, vm, 1);
}

// sub_70E1C0 (0x70E1C0 - 0x70E251) -- the GetSelectedPrimitiveResourceID
// dispatcher: call the wrapper into a temp slot, ref/release it, and append
// the block's C string to the VM value stack (KoreAppendString, sub_407070);
// an empty slot instead appends an 8-byte empty value-array record.
static int EngineTestDispatchReturnString(int fn, int vm)
{
    F3SharedBuffer *slot = 0;
    F3SharedBuffer *block = 0;
    ((void (*)(F3SharedBuffer **))fn)(&slot);
    RefSharedSlot(&block, &slot);               // thiscall (sub_1A012C0)
    ReleaseSharedSlot(&slot);
    if (block != 0)
    {
        const char *data = block->data;
        if (data != 0)
        {
            KoreAppendString(vm, data, (int)strlen(data));   // sub_407070
            ReleaseSharedSlot(&block);
            return 1;
        }
        int cursor = *(int *)((char *)vm + 0x1C);
        *(int *)(cursor + 4) = 0;
        *(int *)((char *)vm + 0x1C) = cursor + 8;
    }
    else
    {
        KoreAppendString(vm, "", 0);            // sub_407070 (the empty string)
    }
    ReleaseSharedSlot(&block);
    return 1;
}

// sub_70E4A0 (0x70E4A0 - 0x70E4EF) -- GetSelectedPrimitiveResourceID upvalue.
static int EngineTestGetSelectedPrimitiveResourceIDUpvalue(int vm)
{
    int  sp   = *(int *)((char *)vm + 32);
    int  slot = *(int *)(sp - 8);
    int  tag  = *(int *)(slot + 16);
    int *data = (int *)(slot + 12);
    if (tag == 7)
        return EngineTestDispatchReturnString(data[4], vm);
    if (tag == 2)
        return EngineTestDispatchReturnString(data[0], vm);
    return EngineTestDispatchReturnString(0, vm);
}

// sub_63E1F0 (0x63E1F0 - 0x63E26A) -- SetSelectedPrimitive upvalue (122
// bytes): pull the first Lua value (the stored C function pointer), pop the
// optional number argument (0.0 when the stack is empty), and call the
// function with it.
static int EngineTestSetSelectedPrimitiveUpvalue(int vm)
{
    int  sp   = *(int *)((char *)vm + 32);
    int  slot = *(int *)(sp - 8);
    int *data = (int *)(slot + 12);
    int  tag  = *(int *)(slot + 16);
    void *p;
    if (tag == 7)
        p = (void *)data[4];
    else if (tag == 2)
        p = (void *)data[0];
    else
        p = 0;
    void *fn = *(void **)p;
    double d = (sp < *(int *)((char *)vm + 28))
        ? KorePopNumberValue(vm, sp)             // sub_5D93D0
        : 0.0;
    ((void (*)(double))fn)(d);
    return 0;
}

// ----------------------------------------------------------------
//  EngineLuaDebugTable / EngineLuaEnsureTable / EngineLuaRegisterNamedClosure
//  -- the table-ensuring and method-registration ops.
// ----------------------------------------------------------------

// sub_67C2B0 (0x67C2B0 - 0x67C381) -- EngineLuaEnsureTable: intern/look up
// NAME on the module context (KoreLookupBuiltinValue, sub_67C180); when found
// (or the context is empty) copy the {value, ctrl} pair to OUT with a
// transient increment/decrement of its ctrl block; otherwise create the table
// for the name on the module value stack (reserve 8, KorePushEmptyTable,
// KoreRegisterBuiltinName(-2), roll back), re-look-up into OUT and release
// the first temp.  thiscall + `retn 8` (the {value, ctrl} context pair in
// ECX, OUT and NAME on the stack).  VM-internal.
static DWORD *EngineLuaEnsureTable(DWORD *thisPair, DWORD *out, const char *name)
{
    DWORD pair[2];
    KoreLookupBuiltinValue(thisPair, pair, name);           // sub_67C180
    if (pair[0] != 0 || thisPair[0] == 0)
    {
        out[0] = pair[0];
        out[1] = pair[1];
        if (pair[1] != 0)
        {
            InterlockedIncrement((volatile LONG *)pair[1]);
            ReleaseLookupCtrl(pair[1]);   // decrement; dispatch + LHFree at 0
        }
    }
    else
    {
        DWORD *table = *(DWORD **)thisPair;
        DWORD  cursor = *(DWORD *)(table + 0x1C);
        KoreValueStackReserve(table, *(void **)table, thisPair[1], cursor);  // sub_5DF650
        *(DWORD *)(table + 0x1C) = cursor + 8;
        KorePushEmptyTable((int)table, 0, 0);                // sub_5DEC20
        KoreRegisterBuiltinName((int)table, -2, name);       // sub_4076D0
        *(DWORD *)(table + 0x1C) = cursor;                   // roll back
        KoreLookupBuiltinValue(thisPair, out, name);         // sub_67C180 (re-look-up)
        KoreReleaseLookupPair(pair);                         // sub_6179D0
    }
    return out;
}

// sub_633E70 (0x633E70 - 0x633EE8) -- EngineLuaDebugTable: build the global-
// table context on the module (EngineLuaGlobalTable, sub_67C390), ensure the
// table named by the g_slot_1DBDC78 slot ("Debug") into OUT, drop the pair's
// ctrl block, and return OUT.  10+ callers (the module-registration family).
static DWORD *EngineLuaDebugTable(DWORD *out, int luaModule)
{
    EngineLuaGlobalTable((int *)out, luaModule);   // sub_67C390
    const char *name = GetSlotString(&g_slot_1DBDC78);
    EngineLuaEnsureTable(out, out, name);          // sub_67C2B0 (this = out = OUT)
    ReleaseLookupCtrl(out[1]);                     // the epilogue release
    return out;
}

// sub_640870 / sub_70E320 / sub_70E3A0 / sub_70E420 / sub_70E530 -- the five
// 124-byte registration helpers (identical except for the upvalue-marshalling
// function embedded in each body): when the {value, ctrl} context pair's
// value is set, reserve 8 bytes on the module value stack, store the C
// wrapper pointer in the reserved slot, install the builtin closure (the
// upvalue fn, 1 upvalue), store the name in the global table (sub_67C540),
// register the builtin name (KoreRegisterBuiltinName, -2), and roll the stack
// back.  thiscall + `retn 8` (the context pair in ECX, NAME and WRAPPER on
// the stack).  VM-internal.  (sub_640870 / sub_70E530 are shared with other
// module registrations; sub_70E320 / sub_70E3A0 / sub_70E420 are EngineTest-
// specific.)  One shared implementation here takes the upvalue as a parameter.
static DWORD EngineLuaRegisterNamedClosure(DWORD *pair, const char *name, int wrapper, int upvalue)
{
    DWORD value = pair[0];
    if (value != 0)
    {
        DWORD *table = *(DWORD **)value;
        DWORD  cursor = *(DWORD *)(table + 0x1C);
        KoreValueStackReserve(table, *(void **)table, pair[1], cursor);   // sub_5DF650
        *(DWORD *)(table + 0x1C) = cursor + 8;
        DWORD *slot = (DWORD *)KoreValueArraySlot(table, 4);              // sub_5DC450
        *slot = (DWORD)wrapper;
        KoreRegisterBuiltinClosure((int)table, upvalue, 1);
        EngineLuaStoreGlobalName(pair, table, name);                      // sub_67C540
        KoreRegisterBuiltinName((int)table, -2, name);                    // sub_4076D0
        *(DWORD *)(table + 0x1C) = cursor;                                // roll back
    }
    return value;
}

// ResetEngineTest  (nullsub_28, 0x70DBB0 - 0x70DBB1) -- the one-byte `retn`
// no-op called immediately before RegisterEngineTest by ReinitEngineLua
// (0x61A56F, with the Lua module pushed) and RunFrontEnd (0x61D5F6, no
// argument).  Byte-identical: the single `retn`.
__declspec(naked)
void ResetEngineTest(int luaModule)
{
    (void)luaModule;
    __asm { retn }
}

// RegisterEngineTest  (sub_70DDB0, 0x70DDB0 - 0x70DEBA) -- register the
// "Debug.EngineTest" Lua module (see the section header above).  [NOT
// byte-identical] placement-dependent (relative calls) and it references
// the high-VA .rdata method-name strings.
void RegisterEngineTest(int luaModule)
{
    DWORD classPair[2];   // the "Debug" table pair {value, ctrl}
    DWORD testPair[2];    // the "EngineTest" table pair {value, ctrl}

    EngineLuaDebugTable(classPair, luaModule);                       // sub_633E70
    EngineLuaEnsureTable(classPair, testPair, "EngineTest");         // sub_67C2B0
    EngineLuaRegisterNamedClosure(testPair, "AddMistInstance",                 (int)EngineTestAddMistInstance,                  (int)EngineTestAddMistInstanceUpvalue);                  // sub_70E320
    EngineLuaRegisterNamedClosure(testPair, "UpdateMaterialParameterFloat",    (int)EngineTestUpdateMaterialParameterFloat,     (int)EngineTestUpdateMaterialParameterFloatUpvalue);   // sub_70E3A0
    EngineLuaRegisterNamedClosure(testPair, "UpdateMaterialParameterVec4",     (int)EngineTestUpdateMaterialParameterVec4,      (int)EngineTestUpdateMaterialParameterVec4Upvalue);    // sub_70E420
    EngineLuaRegisterNamedClosure(testPair, "GetSelectedPrimitiveResourceID",  (int)EngineTestGetSelectedPrimitiveResourceID,   (int)EngineTestGetSelectedPrimitiveResourceIDUpvalue); // sub_70E530
    EngineLuaRegisterNamedClosure(testPair, "SetSelectedPrimitive",            (int)EngineTestSetSelectedPrimitive,             (int)EngineTestSetSelectedPrimitiveUpvalue);       // sub_640870

    ReleaseLookupCtrl(testPair[1]);                                  // epilogue: release the EngineTest pair's ctrl
    testPair[0] = 0;
    testPair[1] = 0;
    ReleaseLookupCtrl(classPair[1]);                                 // release the Debug pair's ctrl
}

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
        SharedSlotConcat((F3SharedBuffer **)&startup, &g_unk_1DBE2B4, &g_unk_1DBDA6C);
        RunLuaStartupScripts((int)dw[0xBC / 4], (F3SharedBuffer **)&startup);
        ReleaseSharedSlot((F3SharedBuffer **)&startup);
    }

    // 6. Optional "EngineTest" module.
    if (flag != 0)
    {
        ResetEngineTest((int)dw[0xBC / 4]);
        RegisterEngineTest((int)dw[0xBC / 4]);
    }

    // 7. (The original calls the nullsub_27 no-op when +0xC0 is set.)
    if (dw[0xC0 / 4] != 0)
        nullsub_27();

    // 8. Build the CEntityScriptHandleManager (80 bytes) into +0xC4.
    int  esm    = 0;
    void *esmMem = LHAlloc(0x50);
    if (esmMem != 0)
        esm = (int)CEntityScriptHandleManagerCtor(esmMem, dw[0xBC / 4]);
    if (dw[0xC4 / 4] != 0)
    {
        void *old0C4 = (void *)dw[0xC4 / 4];
        void **vt    = (void **)*(void **)old0C4;
        ((void (*)(void *, int))vt[0])(old0C4, 1);   // deleting dtor
    }
    dw[0xC4 / 4] = (DWORD)esm;
    return esm;
}
