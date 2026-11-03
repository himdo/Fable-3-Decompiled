// Fable 3 — common module (engine-wide memory / buffer helpers).

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// HeapSetInformation compatibility class is 0 in the original (see disasm of
// LHAlloc: `push 0 ; HeapInformationClass`); define it locally so the
// build does not depend on a newer SDK header exposing the name.
#ifndef HeapCompatibilityInformation
#define HeapCompatibilityInformation 0
#endif

// ---- custom heap allocator table (dword_2075B24) --------------------------
// Function-pointer table the engine uses for all heap traffic (the object
// pointer is stored in the global at 0x02075B24). Only the two slots the
// current code touches are backed by real (malloc/free) routines; the rest
// are left null. Layout (x86, 4-byte slots):
//   +0  (unused here)
//   +4  (unused here)
//   +8  allocate(size)      <- LHAlloc calls this
//   +12 (unused here)
//   +16 free(ptr)           <- LHFree / SharedBufferAssign / SharedBufferFree
static void *f3_heap_alloc(unsigned int size) { return malloc(size); }
static void  f3_heap_free(void *p) { free(p); }
typedef struct F3HeapTable
{
    void *                 pad0;
    void *                 pad4;
    void *(*alloc)(unsigned int);
    void *                 pad12;
    void  (*free)(void *);
} F3HeapTable;
static F3HeapTable g_heapTable = { 0, 0, f3_heap_alloc, 0, f3_heap_free };

// ---- globals referenced by the allocator ----------------------------------
static BYTE  g_byte_2075B30  = 0;  // 0x2075B30: heap info initialized
static DWORD g_dword_1DADD10 = 0;  // 0x1DADD10: verbose / OOM-report flag

// 0x2075C48: global live-slot counter (the IDA name is "Addend"). Bumped by
// InitSharedSlot and the worker ctor, dropped by ReleaseSharedSlot and the
// worker dtor body. (Was worker.cpp; moved here with its slot ctor/dtor.)
DWORD g_Addend = 0;

// ============================================================
//  OutOfMemory  (0x01A034F0 - 0x01A0356C)  -- report an out-of-memory condition
//  [NOT byte-identical] placement-dependent (relative calls to swprintf_s, OutputDebugStringW, DebugBreak).
//
//  void OutOfMemory(const wchar_t *context, unsigned int size)
//
//  Locks a private critical section, calls the optional pre-callback
//  (dword_2075B20), formats and writes to the debugger:
//      "Out Of Memory allocating <size> in <context>\n"
//  then calls the optional post-callback (dword_2075B2C) with (0, 0) and breaks.
//
//  Only runs when an allocation fails AND the verbose flag (0x1DADD10) is set.
// ============================================================
static CRITICAL_SECTION g_OomCs;
struct OomCsInit
{
    OomCsInit()  { InitializeCriticalSection(&g_OomCs); }
    ~OomCsInit() { DeleteCriticalSection(&g_OomCs); }
};
static OomCsInit g_OomCsInit;

static void (*g_dword_2075B20)(void)          = 0;   // 0x2075B20: optional pre-callback
static void (*g_dword_2075B2C)(void *, void *) = 0;   // 0x2075B2C: optional post-callback

static void OutOfMemory(const wchar_t *context, unsigned int size)
{
    WCHAR buf[512];

    EnterCriticalSection(&g_OomCs);
    if (g_dword_2075B20 != 0)
        g_dword_2075B20();
    swprintf_s(buf, 0x1FF, L"Out Of Memory allocating %u in %s\n", size, context);
    OutputDebugStringW(buf);
    if (g_dword_2075B2C != 0)
        g_dword_2075B2C(0, 0);
    DebugBreak();
    LeaveCriticalSection(&g_OomCs);
}

// ============================================================
//  LHAlloc  (0x1A035C0 - 0x1A03627)  -- game heap allocator
//  [NOT byte-identical] placement-dependent (relative call to OutOfMemory).
// ============================================================
void *LHAlloc(unsigned int size)
{
    if (g_byte_2075B30 == 0)
    {
        DWORD compat = 2;
        HANDLE hHeap = GetProcessHeap();
        HeapSetInformation(hHeap, (HEAP_INFORMATION_CLASS)HeapCompatibilityInformation, &compat, 4);
        g_byte_2075B30 = 1;
    }

    void *p = g_heapTable.alloc(size);
    if (p == 0 && (g_dword_1DADD10 & 1))
        OutOfMemory(L"LHBaseMemory::Alloc", size);
    return p;
}

// ============================================================
//  LHFree  (0x1A032C0 - 0x1A032CB)  -- the game heap free (counterpart to
//  LHAlloc)
//  [NOT byte-identical] placement-independent, but refs the heap-table
//  pointer global (0x02075B24) at the original high VA not present in this
//  build.
//
//  void LHFree(void *p)
//
//  11-byte thiscall thunk: `mov ecx, dword_2075B24; mov edx, [ecx+10h];
//  jmp edx` -- calls the allocator table's free member with p as the first
//  stack argument. Unconditional; the underlying free(NULL) is a no-op.
// ============================================================
void LHFree(void *p)
{
    g_heapTable.free(p);
}

// ============================================================
//  SharedBufferAssign  (0x403FE0 - 0x404053)  -- assign (src, len) into the buffer
//  [NOT byte-identical] placement-dependent (relative call to memcpy_s).
// ============================================================
void SharedBufferAssign(F3SharedBuffer *this_, const char *src, unsigned int len)
{
    if (this_->data != 0)
    {
        g_heapTable.free(this_->data);
        this_->data = 0;
    }

    unsigned int aligned = (len + 4) & 0xFFFFFFFCu;
    char *buf = (char *)LHAlloc(aligned);

    this_->capacity = ((aligned ^ this_->capacity) & 0x7FFFFFFFu) ^ this_->capacity;
    this_->data     = buf;
    this_->length   = len;

    if (buf != 0)
    {
        memcpy(buf, src, len);
        buf[len] = 0;
        if (aligned > len + 1)
            memset(buf + len + 1, 0, aligned - (len + 1));
    }
}

// ============================================================
//  SharedBufferFree  (0x403820 - 0x40385A)  -- release the counted buffer
//  [NOT byte-identical] placement-independent but references the heap table (0x02075B24)
//  at the original high VA, which is not present in this build.
// ============================================================
void SharedBufferFree(F3SharedBuffer *this_)
{
    if (this_->data != 0)
    {
        g_heapTable.free(this_->data);
        this_->data = 0;
    }
    this_->capacity &= 0x80000000u;
    this_->length   = 0;
    g_heapTable.free(this_);
}

// ============================================================
//  SharedBufferCreate  (0x1A01360 - 0x1A013D3)  -- allocate + fill a counted buffer
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc and
//  SharedBufferAssign).
//
//  F3SharedBuffer * SharedBufferCreate(const char *src, int size)
//
//  Allocate a fresh 16-byte F3SharedBuffer control block (LHAlloc(16)) and fill
//  it from the source byte buffer: zero data / length / capacity, then run
//  SharedBufferAssign(src, len) where len is `size` or, when size == -1
//  (0xFFFFFFFF), the length of the null-terminated src (0 when *src is 0). The
//  new block's refcount is set to 1 and the block is returned (0 on OOM).
//  userpurge in the original (src in EDI, size on the stack); SetSharedSlot
//  (0x1A01CC0) is its primary caller (the original has 6 call sites).
// ============================================================
F3SharedBuffer *SharedBufferCreate(const char *src, int size)
{
    F3SharedBuffer *blk = (F3SharedBuffer *)LHAlloc(16);
    if (blk == 0)
        return 0;

    blk->data     = 0;
    blk->length   = 0;
    blk->capacity = 0;

    unsigned int len;
    if (size == -1)
    {
        len = 0;
        if (src != 0 && *src != 0)
            do { ++len; } while (src[len] != 0);
    }
    else
        len = (unsigned int)size;

    SharedBufferAssign(blk, src, len);
    blk->refcount = 1;
    return blk;
}

// ============================================================
//  InitSharedSlot  (0x1A00C30 - 0x1A00C48)  -- construct a (null) shared slot
//  [NOT byte-identical] placement-independent (no relative calls / long jumps),
//  but refs the high-VA global g_Addend (0x02075C48) and the
//  InterlockedIncrement IAT slot (0x01AD52F8) at original high VAs not present
//  in this build.
//
//  F3SharedBuffer ** __thiscall InitSharedSlot(F3SharedBuffer **obj)
//
//  The shared-slot constructor: null the slot (it holds no buffer yet) and
//  register the slot in the global live-slot count (g_Addend), then return the
//  slot. Called from ~1588 sites in the original to initialize the single-word
//  F3SharedBuffer * slots embedded in classes (e.g. CDebugTraceFable's string
//  member at +4, the global save-path slot) and on the stack. Its matching
//  destructor is ReleaseSharedSlot (sub_1A01320), which releases the buffer and
//  drops the counter. __thiscall in the original: the slot pointer (obj) is in
//  ECX and returned in EAX, with no stack arguments (plain `retn`); declared
//  here as a plain free function to match the project convention for
//  non-byte-identical thiscall methods.
// ============================================================
F3SharedBuffer ** InitSharedSlot(F3SharedBuffer **obj)
{
    *obj = 0;
    InterlockedIncrement(&g_Addend);
    return obj;
}

// ============================================================
//  ReleaseSharedSlot  (0x1A01320 - 0x1A0135D)  -- destroy a (shared) slot
//  [NOT byte-identical] placement-dependent (relative calls to the
//  InterlockedDecrement IAT slot and to SharedBufferFree).
//
//  long ReleaseSharedSlot(F3SharedBuffer **slot)
//
//  The shared-slot destructor (the counterpart to InitSharedSlot, 0x1A00C30).
//  `slot` points at the single-word F3SharedBuffer * slot. When the slot holds
//  a control block it decrements the block's refcount (at +12); on zero it
//  frees the block via SharedBufferFree (0x403820) and nulls the slot. It then
//  drops the global live-slot count (g_Addend) and returns that new count.
//  __thiscall in the original (slot in ECX, plain `retn`); declared as a plain
//  free function to match the project convention.
// ============================================================
long ReleaseSharedSlot(F3SharedBuffer **slot)
{
    if (*slot != 0)
    {
        if (InterlockedDecrement(&(*slot)->refcount) <= 0 && *slot != 0)
            SharedBufferFree(*slot);
        *slot = 0;
    }
    long result = InterlockedDecrement(&g_Addend);
    *slot = 0;
    return result;
}

// ============================================================
//  SetSharedSlot  (0x1A01CC0 - 0x1A01D26)  -- assign (src, size) into a shared slot
//  [NOT byte-identical] placement-dependent (relative calls to SharedBufferCreate
//  and SharedBufferFree), plus the InterlockedIncrement / InterlockedDecrement IAT
//  slots and the high-VA live-slot counter g_Addend (0x02075C48).
//
//  F3SharedBuffer ** SetSharedSlot(F3SharedBuffer **obj, const char *src, int size)
//
//  The engine's general shared-slot setter: thiscall in the original (the slot
//  pointer is in ECX, src and size are the two stack args, the callee cleans via
//  `retn 8`), with ~18,460 call sites across the engine. It nulls the slot, bumps
//  the global live-slot count (g_Addend), then -- when src is a non-empty buffer --
//  builds a fresh counted buffer from (src, size) via SharedBufferCreate (0x1A01360).
//  It releases any block the slot still holds (InterlockedDecrement its refcount at
//  +12; SharedBufferFree on zero) and re-nulls it; the slot was already nulled above,
//  so this branch is a no-op unless the builder touches it. It stores the new block
//  if one was built and returns the slot pointer (this). Declared as a plain free
//  function (first param `obj` = the thiscall slot) to match the project convention.
//  The sibling SetSaveGamePath (0x1A019B0) is the save-path specialization (no size
//  arg, no counter bump, void return).
// ============================================================
F3SharedBuffer **SetSharedSlot(F3SharedBuffer **obj, const char *src, int size)
{
    F3SharedBuffer *v4 = 0;

    *obj = 0;
    InterlockedIncrement(&g_Addend);

    if (src != 0 && *src != 0)
        v4 = SharedBufferCreate(src, size);

    if (*obj != 0)
    {
        if (InterlockedDecrement(&(*obj)->refcount) <= 0 && *obj != 0)
            SharedBufferFree(*obj);
        *obj = 0;
    }

    if (v4 != 0)
        *obj = v4;
    return obj;
}

// 0x01BDFD28: the default wide string returned when a localized-string entry
// is null. In the original it is the (empty) wide string IDA names "Path";
// it lives in .rdata at a high VA not present in this build, so a local empty
// string stands in for it (the value only matters on the null-entry fallback).
static const WCHAR g_defaultLocalizedString[] = L"";

// ============================================================
//  GetLocalizedString  (0x019FF570 - 0x019FF57F)  -- localized-string getter
//  [NOT byte-identical] placement-independent (no calls / relative jumps),
//  but refs the default "Path" wide-string global (0x01BDFD28) at its original
//  high VA, which is not present in this build.
//
//  const WCHAR * GetLocalizedString(const WCHAR ***obj)
//
//  thiscall accessor for a per-LCID localized-string entry (obj is the this
//  pointer, no stack args, plain `retn`). obj points to a word holding the
//  current-locale string pointer P (a `const WCHAR **`). If P is non-null it
//  returns *P (the wide string); otherwise it returns the default (the empty
//  wide string, the original "Path" global at 0x01BDFD28). The entries are the
//  per-LCID string tables selected by GetLocaleEntry (sub_6195C0).
//
//  Original body (15 bytes, 8B 01 85 C0 75 06 B8 28 FD BD 01 C3 8B 00 C3):
//      mov eax, [ecx]        ; eax = P        = *obj
//      test eax, eax
//      jnz  L_ret            ; if P != 0
//      mov eax, offset Path  ; else return the default (0x01BDFD28)
//      retn
//  L_ret:
//      mov eax, [eax]        ; eax = *P       = the wide string
//      retn
// ============================================================
const WCHAR *GetLocalizedString(const WCHAR ***obj)
{
    const WCHAR **p = *obj;
    if (p != 0)
        return *p;
    return g_defaultLocalizedString;
}

// ============================================================
//  Per-LCID localized-string tables (0x01DBD120.., .data)
//
//  GetLocaleEntry (0x6195C0) maps (key, current user LCID) -> a pointer into
//  one of thirteen 9-entry per-locale string tables. Each table entry is a
//  `const WCHAR **` (the current locale's wide-string pointer) consumed by
//  GetLocalizedString (0x19FF570). The key table (dword_1DBD2F4, 9 dwords) is
//  searched to find the entry index; all values are 0xFFFFFFFF in the binary
//  (the tables are filled in at runtime by the save/load path).
//
//  Original layout (each table is 0x24 bytes = 9 dwords):
//    0x1DBD120  default (English-US fallback)   0x1DBD1B0  Spanish
//    0x1DBD144  English                         0x1DBD1D4  Italian
//    0x1DBD168  German                          0x1DBD1F8  Dutch
//    0x1DBD18C  French                          0x1DBD21C  Norwegian
//                                              0x1DBD240  Swedish
//    0x1DBD264  Turkish                         0x1DBD288  Polish
//    0x1DBD2AC  Finnish                         0x1DBD2D0  Russian
//    0x1DBD2F4  key table (dword_1DBD2F4)
//  The not-found default table is at 0x20B6078 (dword_20B6078).
// ============================================================
#define F3_LOCALE_TABLE_INIT \
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, \
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF

static DWORD g_localeTable_1DBD120[9] = { F3_LOCALE_TABLE_INIT };  // default / English-US fallback
static DWORD g_localeTable_1DBD144[9] = { F3_LOCALE_TABLE_INIT };  // English
static DWORD g_localeTable_1DBD168[9] = { F3_LOCALE_TABLE_INIT };  // German
static DWORD g_localeTable_1DBD18C[9] = { F3_LOCALE_TABLE_INIT };  // French
static DWORD g_localeTable_1DBD1B0[9] = { F3_LOCALE_TABLE_INIT };  // Spanish
static DWORD g_localeTable_1DBD1D4[9] = { F3_LOCALE_TABLE_INIT };  // Italian
static DWORD g_localeTable_1DBD1F8[9] = { F3_LOCALE_TABLE_INIT };  // Dutch
static DWORD g_localeTable_1DBD21C[9] = { F3_LOCALE_TABLE_INIT };  // Norwegian
static DWORD g_localeTable_1DBD240[9] = { F3_LOCALE_TABLE_INIT };  // Swedish
static DWORD g_localeTable_1DBD264[9] = { F3_LOCALE_TABLE_INIT };  // Turkish
static DWORD g_localeTable_1DBD288[9] = { F3_LOCALE_TABLE_INIT };  // Polish
static DWORD g_localeTable_1DBD2AC[9] = { F3_LOCALE_TABLE_INIT };  // Finnish
static DWORD g_localeTable_1DBD2D0[9] = { F3_LOCALE_TABLE_INIT };  // Russian
static DWORD g_localeKeys[9]          = { F3_LOCALE_TABLE_INIT };  // 0x1DBD2F4: key table
static DWORD g_localeDefault[9]       = { F3_LOCALE_TABLE_INIT };  // 0x20B6078: not-found default

#undef F3_LOCALE_TABLE_INIT

// ============================================================
//  GetLocaleEntry  (0x006195C0 - 0x00619850)  -- per-LCID localized-string
//  entry lookup
//  [NOT byte-identical] refs the .data locale/key tables (0x01DBD120..0x01DBD2F4)
//  and the not-found default table (0x020B6078) at their original high VAs
//  (not present in this build), plus an indirect IAT call to
//  GetUserDefaultLCID (0x01AD...) and MSVC switch jump tables.
//
//  int * GetLocaleEntry(int key)
//
//  Search the 9-entry key table (dword_1DBD2F4) for `key`; if absent, return
//  the default entry table (dword_20B6078). Otherwise pick one of 13 per-LCID
//  string tables by the current user locale (GetUserDefaultLCID) and return a
//  pointer into that table at the matched entry (table + index). The returned
//  entry is a `const WCHAR **` consumed by GetLocalizedString (0x19FF570).
//
//  The LCID -> table decision tree below is a faithful transcription of the
//  original cmp/switch ladder (verified against the disassembly). `table` is
//  initialized to the default table so every LCID resolves to a valid entry.
// ============================================================
int *GetLocaleEntry(int key)
{
    int i = 0;
    while (key != g_localeKeys[i])
    {
        if ((unsigned int)++i >= 9)
            return (int *)g_localeDefault;    // 0x20B6078
    }

    DWORD           lcid   = GetUserDefaultLCID();
    const DWORD    *table  = g_localeTable_1DBD120;   // default fallback

    if (lcid <= 0x804)
    {
        if (lcid != 2052)                                  // 0x0804
        {
            switch (lcid)
            {
            case 0x404:  table = g_localeTable_1DBD144; break;
            case 0x405:  table = g_localeTable_1DBD168; break;
            case 0x407:  table = g_localeTable_1DBD1B0; break;
            case 0x40A:  table = g_localeTable_1DBD2AC; break;
            case 0x40C:  table = g_localeTable_1DBD18C; break;
            case 0x40E:  table = g_localeTable_1DBD1D4; break;
            case 0x410:  table = g_localeTable_1DBD1F8; break;
            case 0x411:  table = g_localeTable_1DBD21C; break;
            case 0x412:  table = g_localeTable_1DBD240; break;
            case 0x415:  table = g_localeTable_1DBD264; break;
            case 0x419:  table = g_localeTable_1DBD288; break;
            default:     table = g_localeTable_1DBD120; break;
            }
        }
        else
            table = g_localeTable_1DBD144;                 // 0x0804 -> English
    }
    else if (lcid > 0x200C)
    {
        if (lcid > 0x380C)
        {
            if (lcid > 0x4C0A)
            {
                if (lcid <= 0x580A)
                {
                    if (lcid != 22538 && lcid != 20490 && lcid != 21514)   // 0x580A/0x500A/0x540A
                        table = g_localeTable_1DBD120;
                    else
                        table = g_localeTable_1DBD2D0;
                }
                else    // lcid > 0x580A
                    table = (lcid == 58380) ? g_localeTable_1DBD18C   // 0xE40C
                                            : g_localeTable_1DBD120;
            }
            else        // 0x380C < lcid <= 0x4C0A
            {
                if (lcid == 19466)                                 // 0x4C0A
                    table = g_localeTable_1DBD2D0;
                else if (lcid > 0x400A)
                {
                    if (lcid != 17418 && lcid != 18442)            // 0x440A/0x480A
                        table = g_localeTable_1DBD120;
                    else
                        table = g_localeTable_1DBD2D0;
                }
                else        // 0x380C < lcid <= 0x400A
                {
                    if (lcid == 16394 || lcid - 15370 == 0)        // 0x400A / 0x3C0A
                        table = g_localeTable_1DBD2D0;
                    else
                        table = (lcid - 15370 == 2) ? g_localeTable_1DBD18C  // 0x3C0C
                                                    : g_localeTable_1DBD120;
                }
            }
        }
        else        // 0x200C < lcid <= 0x380C
        {
            if (lcid == 14348)                                     // 0x380C
                table = g_localeTable_1DBD18C;
            else if (lcid > 0x2C0C)
            {
                if (lcid > 0x340A)
                {
                    if (lcid == 13324)                             // 0x340C
                        table = g_localeTable_1DBD18C;
                    else if (lcid != 14346)                        // 0x380A
                        table = g_localeTable_1DBD120;
                    else
                        table = g_localeTable_1DBD2D0;
                }
                else        // 0x2C0C < lcid <= 0x340A
                {
                    if (lcid == 13322 || lcid - 12298 == 0)        // 0x340A / 0x300A
                        table = g_localeTable_1DBD2D0;
                    else
                        table = (lcid - 12298 == 2) ? g_localeTable_1DBD18C  // 0x300C
                                                    : g_localeTable_1DBD120;
                }
            }
            else        // 0x200C < lcid <= 0x2C0C
            {
                if (lcid == 11276)                                 // 0x2C0C
                    table = g_localeTable_1DBD18C;
                else if (lcid > 0x280A)
                {
                    if (lcid == 10252)                             // 0x280C
                        table = g_localeTable_1DBD18C;
                    else if (lcid != 11274)                        // 0x2C0A
                        table = g_localeTable_1DBD120;
                    else
                        table = g_localeTable_1DBD2D0;
                }
                else        // 0x200C < lcid <= 0x280A
                {
                    if (lcid == 10250 || lcid - 9226 == 0)         // 0x280A / 0x240A
                        table = g_localeTable_1DBD2D0;
                    else
                        table = (lcid - 9226 == 2) ? g_localeTable_1DBD18C   // 0x240C
                                                   : g_localeTable_1DBD120;
                }
            }
        }
    }
    else        // 0x804 < lcid <= 0x200C
    {
        if (lcid == 8204)                                          // 0x200C
            table = g_localeTable_1DBD18C;
        else if (lcid > 0x100A)
        {
            if (lcid > 0x180A)
            {
                if (lcid > 0x1C0C)
                {
                    if (lcid != 8202)                              // 0x200A
                        table = g_localeTable_1DBD120;
                    else
                        table = g_localeTable_1DBD2D0;
                }
                else        // 0x180A < lcid <= 0x1C0C
                {
                    if (lcid == 7180 || lcid == 6156)              // 0x1C0C/0x180C
                        table = g_localeTable_1DBD18C;
                    else if (lcid != 7178)                         // 0x1C0A
                        table = g_localeTable_1DBD120;
                    else
                        table = g_localeTable_1DBD2D0;
                }
            }
            else if (lcid != 6154)                                 // 0x180A
            {
                if (lcid <= 0x1407)
                {
                    switch (lcid)
                    {
                    case 0x1407: table = g_localeTable_1DBD1B0; break;
                    case 0x100C: table = g_localeTable_1DBD18C; break;
                    case 0x1404: table = g_localeTable_1DBD144; break;
                    default:     table = g_localeTable_1DBD120; break;
                    }
                }
                else        // 0x1407 < lcid < 0x180A
                {
                    if (lcid == 5130)                              // 0x140A
                        table = g_localeTable_1DBD2D0;
                    else
                        table = (lcid - 5130 == 2) ? g_localeTable_1DBD18C   // 0x140C
                                                   : g_localeTable_1DBD120;
                }
            }
            else            // lcid == 0x180A
                table = g_localeTable_1DBD2D0;
        }
        else if (lcid == 4106)                                     // 0x100A
            table = g_localeTable_1DBD2D0;
        else if (lcid > 0xC04)
        {
            if (lcid <= 0xC0C)
            {
                if (lcid != 3084)                                  // 0xC0C
                {
                    if (lcid == 3079)                              // 0xC07
                        table = g_localeTable_1DBD1B0;
                    else if (lcid - 3079 == 3)                     // 0xC0A
                        table = g_localeTable_1DBD2AC;
                    else
                        table = g_localeTable_1DBD120;
                }
                else
                    table = g_localeTable_1DBD18C;                 // 0xC0C
            }
            else        // 0xC0C < lcid <= 0x100A
            {
                if (lcid == 4100)                                  // 0x1004
                    table = g_localeTable_1DBD144;
                else if (lcid - 4100 == 3)                         // 0x1007
                    table = g_localeTable_1DBD1B0;
                else
                    table = g_localeTable_1DBD120;
            }
        }
        else if (lcid == 3076)                                     // 0xC04
            table = g_localeTable_1DBD144;
        else        // 0x804 < lcid < 0xC04
        {
            switch (lcid)
            {
            case 0x807: table = g_localeTable_1DBD1B0; break;
            case 0x80A: table = g_localeTable_1DBD2D0; break;
            case 0x80C: table = g_localeTable_1DBD18C; break;
            case 0x810: table = g_localeTable_1DBD1F8; break;
            case 0x819: table = g_localeTable_1DBD288; break;
            default:    table = g_localeTable_1DBD120; break;
            }
        }
    }

    return (int *)(table + i);
}
