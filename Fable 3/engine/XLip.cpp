// Fable 3 -- XLip (lip-sync) culture / component system.
//
// The XLip* / Lip* function cluster (XLipMem, XLipCultures, XLipComponent,
// XLipDebug, XLipSettings) moved out of gamemain.cpp into its own
// translation unit.  The module's public entry point is InitLipCultures
// (sub_719770), called once by RunFrontEnd right after the engine member
// 0x98 sorted map is built.  Shared helpers it needs from gamemain.cpp
// (EngRefSharedSlot / MakeSettingsFilePath / GetSlotString / F3InvalidParam /
// GetFileSubsystem / RegisterStandardAssets) are declared in xlip.h and
// keep their definitions in gamemain.cpp.

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // original used strstr / the CRT directly
#endif

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>               // va_list / va_start / vswprintf_s (XLipDebugMessage)
#include <wctype.h>               // iswspace (the XLipCultures line trim)
#include "common/common.h"        // LHAlloc / LHFree / InitSharedSlot / ReleaseSharedSlot
#include "gamemain.h"             // CopySharedSlot (the shared-slot copy assign)
#include "gameengine.h"           // GetGameEngine (sub_619A90)
#include "xlip.h"                 // this module's own public declarations

// ============================================================
//  InitLipCultures  (sub_719770, 0x719770 - 0x7199FB)  -- the lip-sync
//  culture / component system init, called once by RunFrontEnd right after
//  the engine member 0x98 sorted map is built.
//  [NOT byte-identical] placement-dependent (relative calls to
//  SetSharedSlot / MakeSettingsFilePath / GetSlotString /
//  ReleaseSharedSlot / LHAlloc / LHFree / memset / GetGameEngine / the
//  remaining lip-culture stubs) and refs the culture global cluster
//  (0x1DC2744 .. 0x1DC2804) and the "cultures_win32.txt" / "\\cultures.txt"
//  / "text" / "\\" rdata strings at their original high VAs.
//
//  long InitLipCultures(void)   -- original: cdecl, plain `retn`
//
//  1. Touches the current game engine (GetGameEngine, sub_619A90, result
//     discarded), publishes the LHAlloc / LHFree pair into the heap-function
//     globals (dword_1DC2808 / 0x1DC280C; the original stores offsets of the
//     j_LHAlloc_1 / j_LHFree_3 jump thunks that forward to LHAlloc / LHFree),
//     releases the leftover "lip%u" strings (ReleaseLipCultures), installs the pair
//     with the lip library (SetLipMemAllocators, dword_1E2C6FC / 0x1E2C700), and
//     lazily initializes the lip runtime (InitLipRuntime, sub_17CA130).
//  2. Loads the culture list file: settings-path base slot (dword_1DC2744)
//     + "\" via MakeSettingsFilePath, concatenated with the
//     "cultures_win32.txt" name slot (SharedSlotConcat, 0x1A02260 = the
//     2-source path concat over a fresh slot), loaded through XLipLoadCultures (sub_17CC7C0);
//     on failure retries with "\\cultures.txt" (each failure touches
//     GetLastError, as the original does through its EDX IAT slot).
//  3. Registers the base path as the default lip component (CreateLipFSRoot,
//     sub_17BEE80) and
//     the seven {name, .., resolved path} component slots (dword_1DC2748,
//     five DWORDs per slot) through RegisterLipComponent, runs the
//     component bring-up
//     (sub_17BF770), then resolves each slot's relative path into its +0x0C
//     slot (ResolveLipComponentSlotPath, sub_717D60).
//  4. Rebuilds the culture-string array: resolves the "text" component's
//     language (XLipGetComponentLanguage, sub_17BFC20, into an 85-byte
//     scratch published as a shared slot; XLipBringUpCultures, sub_719540,
//     then applies it to the seven components),
//     frees the old block (dword_1DC27EC), count = XLipGetCultureCount(NULL)
//     (sub_17C0610, dword_1DC27F0),
//     LHAlloc(96 * count, the original's `mul` overflow folds to -1), then
//     per 96-byte entry fills the 85-byte name (XLipGetCultureName,
//     sub_17C0680), the culture id
//     (XLipGetCultureInfoPtr) at +88, and records the "text" culture index in
//     dword_1DC27F4. (With the empty registered-culture table the count is
//     0, so the per-entry loop is dead in this build.)
//  5. Zeros byte_1DC2804, registers the standard asset packages
//     (RegisterStandardAssets), and releases the two scratch slots (the
//     final release result is the return value).
// ============================================================

// 0x1DC2744 -- the culture-path base shared slot (the settings directory).
static DWORD g_dword_1DC2744 = 0;
// 0x1DC2748 -- the seven lip component slots, five DWORDs each (the +0 slot
// is the component name, the +0x0C slot receives the resolved path).
static DWORD g_dword_1DC2748[35] = { 0 };
// 0x1DC27EC -- the 96-byte-per-entry culture name array (LHAlloc'd).
static DWORD g_dword_1DC27EC = 0;
// 0x1DC27F0 -- the culture count.
static DWORD g_dword_1DC27F0 = 0;
// 0x1DC27F4 -- the "text" culture index.
static DWORD g_dword_1DC27F4 = 0;
// 0x1DC27F8 -- the count of registered "lip%u" culture file entries (only
// ReleaseLipCultures (sub_718140) touches it; it image-initializes to 0 and
// the lip library bumps it as it registers the "lip%u" file names).
static DWORD g_dword_1DC27F8 = 0;
// 0x1DC2808 / 0x1DC280C -- the heap function pointers (LHAlloc / LHFree;
// the original stores the j_LHAlloc_1 / j_LHFree_3 thunks).
static DWORD g_dword_1DC2808 = 0;
static DWORD g_dword_1DC280C = 0;
// 0x1DC2804 -- the lip runtime "initialized" flag byte (zeroed by
// InitLipCultures; sub_17CA130's byte_1E2C6B4 is the counterpart in the
// lip-library module).
static BYTE  g_byte_1DC2804 = 0;

// Remaining lip-library / lip-culture stubs (all in the 0x17Bxxx-0x17Dxxx
// lip-module range; the deep XLipComponent / XLipCultures internals are out
// of scope, so each degrades to its observable no-op / failure path):

// GetFileSubsystem (sub_1801C20, defined below in the file-subsystem
// cluster) is used by ReleaseLipCultures before its definition.
// GetFileSubsystem is declared in xlip.h (defined in gamemain.cpp).

// ============================================================
//  F3AString -- the 28-byte front-end ANSI string (the sub_402F00 family,
//  the MSVC basic_string<char, 16-byte-SSO> layout) that ReleaseLipCultures
//  builds on the stack to name each "lip%u" entry.  The struct type and the
//  F3AStringReset / F3AStringAssign / F3AStringFree helpers below are shared
//  with gamemain.cpp through xlip.h (the type is declared there once).

// default SSO ctor (the original zeroes the capacity, length and buffer[0],
// leaving d0 as-is): cap = 15, len = 0, buffer[0] = 0.
void F3AStringReset(F3AString *s)
{
    s->cap = 15;
    s->len = 0;
    s->u.buf[0] = 0;
}

// sub_402F00 (0x402F00 - 0x402F70) -- basic_string<char>::assign(src, len):
// thiscall, returns the string.  "lip%u" is always short (< 16 chars) so the
// SSO path is taken (no heap buffer); the general heap-growth path is
// mirrored for completeness.
F3AString *F3AStringAssign(F3AString *s, const char *src, unsigned int len)
{
    if (src == 0)
        return s;                       // (Source == null path)
    if (len > 15) {
        s->u.ptr = (DWORD)LHAlloc(len + 1);
        s->cap = len + 1;
    } else
        s->cap = 15;
    char *dst = (s->cap < 16) ? s->u.buf : (char *)s->u.ptr;
    for (unsigned int i = 0; i < len; i++)
        dst[i] = src[i];
    dst[len] = 0;
    s->len = len;
    return s;
}

// the "if (cap >= 16) LHFree(heap ptr)" dtor idiom.
void F3AStringFree(F3AString *s)
{
    if (s->cap >= 0x10)
        LHFree((void *)s->u.ptr);
}

// sub_1801700 (0x1801700 - 0x18017B0) -- the file-subsystem collection's
// "erase by name" method: thiscall + `retn 4` (the collection fs in ECX, the
// F3AString key on the stack).  Looks the name up (sub_1802EC0) and, when
// found, erases the entry and releases the held handle.  The erase body is
// unimplemented in this build, so it degrades to a safe no-op (the
// collection g_fileSubsys is constructed by GetFileSubsystem but no entries
// are ever registered into it here).
static void FileSubsysErase(void *fs, F3AString *name)
{
    (void)fs; (void)name;
}

// ============================================================
//  ReleaseLipCultures  (sub_718140, 0x718140 - 0x7181EC)  -- release the
//  leftover "lip%u" culture file entries (the dword_1DC27F8 count), called
//  at the start of InitLipCultures and by the lip-teardown drivers to drop
//  the "lip0" .. "lip(N-1)" names from the file subsystem before the culture
//  set is rebuilt.
//  [NOT byte-identical] placement-dependent (relative calls to sprintf_s /
//  sub_402F00 / sub_1801C20 / sub_1801700 / LHFree / __security_check_cookie)
//  and refs the high-VA counter global dword_1DC27F8 (0x01DC27F8) and the
//  "lip%u" rdata format string at its original VA.
//
//  int ReleaseLipCultures(void)   -- original: cdecl, plain `retn`
//
//  For i = count down to 1 (count = dword_1DC27F8):
//    1. Build the 28-byte stack F3AString (default SSO ctor).
//    2. sprintf_s(Buffer, 10, "lip%u", i) and assign it (sub_402F00, SSO).
//    3. fs = GetFileSubsystem(&str) (sub_1801C20 -> &g_fileSubsys).
//    4. FileSubsysErase(fs, &str) (sub_1801700, thiscall + retn 4).
//    5. Free the string's heap buffer (dead while cap < 16, i.e. always).
//    6. Decrement the counter.
//  Returns the final counter value (0 once the loop drains it).
// ============================================================
static int ReleaseLipCultures(void)
{
    char Buffer[12];                    // sprintf_s scratch (BufferCount = 10)
    int count = (int)g_dword_1DC27F8;
    while (count > 0)
    {
        F3AString str;
        F3AStringReset(&str);           // default SSO ctor
        sprintf_s(Buffer, 10, "lip%u", (unsigned)count);
        F3AStringAssign(&str, Buffer, (unsigned int)strlen(Buffer));  // sub_402F00
        void *fs = GetFileSubsystem(&str);             // sub_1801C20
        FileSubsysErase(fs, &str);                     // sub_1801700
        F3AStringFree(&str);                           // string dtor
        --count;
        g_dword_1DC27F8 = (DWORD)count;
    }
    return (int)g_dword_1DC27F8;
}
// 0x1E2C6FC / 0x1E2C700 -- the XLipMem custom heap-function pointers, set by
// SetLipMemAllocators (sub_17D0330) from the LHAlloc / LHFree pair and read
// by the lip-mem alloc (sub_17D0350) / free (sub_17D0440) before their
// malloc / free fallbacks.
static DWORD g_dword_1E2C6FC = 0;
static DWORD g_dword_1E2C700 = 0;

//  SetLipMemAllocators  (0x17D0330 - 0x17D0346)  -- install the XLipMem
//  alloc / free function pair and return the alloc pointer.  The lip-mem
//  alloc (sub_17D0350) / free (sub_17D0440) dispatch through these before
//  falling back to malloc / free; InitLipCultures installs the LHAlloc /
//  LHFree pair.  cdecl, two DWORDs, returns a1.
//  [NOT byte-identical] placement-dependent: the original stores through
//  the absolute data addresses 0x1E2C6FC / 0x1E2C700 (A3 / 89 0D), which in
//  this build are the static g_dword_1E2C6FC / g_dword_1E2C700 globals at
//  linker-assigned addresses (deferred PE placement).
static int SetLipMemAllocators(int allocFn, int freeFn)
{
    g_dword_1E2C6FC = (DWORD)allocFn;
    g_dword_1E2C700 = (DWORD)freeFn;
    return allocFn;
}
// ============================================================
//  0x1BC1D48 -- the rdata default table handed to the lip-culture params
//  object's ctor: two zero DWORDs (an empty wide string); "Parameter
//  can't be NULL" and the XLipCultures.cpp assert format strings follow at
//  +8 in the original image.
static const DWORD g_unk_1BC1D48[2] = { 0, 0 };
// 0x1E2C6B4 -- the lip-runtime "initialized" flag byte (set by
// InitLipRuntime, sub_17CA130; cleared by the lip-runtime release
// sub_17CA260; read as the init gate by the whole 0x17Bxxx-0x17Dxxx lip
// library).  Note: distinct from the engine-side g_byte_1DC2804 flag.
static BYTE g_byte_1E2C6B4 = 0;
// 0x1E2C6D0 -- the 32-byte XLipCultures default-params object (LHAlloc'd by
// InitLipRuntime; 0 until it is).
static DWORD g_dword_1E2C6D0 = 0;

// sub_17D0350 (0x17D0350 - 0x17D043D) -- the XLipMem allocator
// (XLipMem.cpp): the embedded scratch pool (dword_1E2C6F0 / 0x1E2C6F4 /
// 0x1E2C6F8 -- image-initializes to 0 and is never armed in this build),
// then the alloc function installed by SetLipMemAllocators (dword_1E2C6FC,
// LHAlloc), then the malloc fallback.  The on-failure dispatch switches on
// the first culture-class id (1 = DebugBreak, 2 = assert, 3 = throw
// "Memory allocation failed!"); id 0 (the value in this build) is a no-op,
// and the failure is only reachable if the installed allocator / malloc
// return null.
//  [NOT byte-identical] placement-dependent (relative calls through the
//  absolute function-pointer globals + the IAT slots _malloc_1 /
//  __imp_DebugBreak / __CxxThrowException) and refs the high-VA globals
//  dword_1E2C6F0..0x1E2C6FC at their original VAs.
static void *LipMemAlloc(unsigned int size)
{
    void *p = 0;
    if (g_dword_1E2C6FC != 0)
        p = ((void *(*)(unsigned int))(size_t)g_dword_1E2C6FC)(size);
    else
        p = malloc(size);
    if (p == 0)
        F3InvalidParam();             // the failure dispatch (id 0 -> no-op)
    return p;
}
// sub_17D0440 (0x17D0440 - 0x17D049A) -- the XLipMem free (XLipMem.cpp):
// null block is an assert (no-op here); the scratch-pool path is dead; then
// the free function installed at dword_1E2C700 (LHFree) with the free()
// fallback.
//  [NOT byte-identical] placement-dependent (the relative call through the
//  absolute function-pointer globals + the _free IAT slot) and refs the
//  high-VA globals dword_1E2C700 / 0x1E2C6F0 at their original VAs.
static void LipMemFree(void *p)
{
    if (p == 0) { F3InvalidParam(); return; }   // (the assert path, no-op)
    if (g_dword_1E2C700 != 0)
        ((void (*)(void *))(size_t)g_dword_1E2C700)(p);
    else
        free(p);
}

// The 24-byte lip DWORD vector (the MSVC STL vector<T> over the XLipMem
// allocator): three leading allocator-wrapper DWORDs (zero image-
// initialized), then begin / end / alloc-end.  Size = (end - begin) / 4.
typedef struct LipDwordVec
{
    DWORD   alloc0;          // +0x00 allocator wrapper (zero)
    DWORD   alloc1;          // +0x04
    DWORD   alloc2;          // +0x08
    DWORD  *begin;           // +0x0C
    DWORD  *end;             // +0x10
    DWORD  *allocEnd;        // +0x14
} LipDwordVec;
// 0x1E2C6B8 -- the global culture-class-id vector (built to {0, 2, 3, 6,
// 15, 5} by InitLipRuntime; read by the lip-component / lip-mem module).
static LipDwordVec g_unk_1E2C6B8 = { 0, 0, 0, NULL, NULL, NULL };

// sub_17C2B10 (0x17C2B10 - 0x17C2B2A) -- the vector size: (end - begin) / 4.
static unsigned int LipDwordVecSize(const LipDwordVec *v)
{
    return (unsigned int)((char *)v->end - (char *)v->begin) >> 2;
}
// sub_17CA660 (0x17CA660 - 0x17CA71D) -- vector resize to `n` elements,
// default-filling the new elements (thiscall): the grow path (sub_17CA8C0)
// allocates a fresh buffer through the lip allocator (sub_17CAE20 =
// LipMemAlloc(4 * newCap), newCap = max(oldCap + oldCap/2, size + add),
// the std::length_error overflow guard against the allocator's max_size is
// dead), moves the old elements (sub_17CAD30), default-fills the added ones
// (sub_17CAD60), and frees the old buffer (sub_17CAE40 = LipMemFree); the
// shrink path (sub_17CA820) compacts the last `n` elements to the front.
//  [NOT byte-identical] placement-dependent (relative calls to the lip
//  allocator / memmove / length_error throw helpers) and refs the
//  high-VA vector global 0x1E2C6B8.
static void LipDwordVecResize(LipDwordVec *v, unsigned int n, DWORD fill)
{
    unsigned int old = LipDwordVecSize(v);
    if (n <= old)
    {
        for (unsigned int i = 0; i < n; i++)
            v->begin[i] = v->begin[old - n + i];
        v->end = v->begin + n;
        return;
    }
    unsigned int cap = (unsigned int)((char *)v->allocEnd - (char *)v->begin) >> 2;
    unsigned int newCap = cap + (cap >> 1);
    if (newCap < old + (n - old))
        newCap = n;
    DWORD *nb = (DWORD *)LipMemAlloc(4 * newCap);
    for (unsigned int i = 0; i < old; i++)
        nb[i] = v->begin[i];
    for (unsigned int i = old; i < n; i++)
        nb[i] = fill;
    if (v->begin != 0)
        LipMemFree(v->begin);
    v->begin = nb;
    v->end = nb + n;
    v->allocEnd = nb + newCap;
}
// sub_17C1D70 (0x17C1D70 - 0x17C1D9F) -- vector element pointer (thiscall):
// __invalid_parameter_noinfo() when index >= size, else begin + index.
static DWORD *LipDwordVecAt(LipDwordVec *v, unsigned int index)
{
    if (index >= LipDwordVecSize(v))
    {
        F3InvalidParam();             // original: _invalid_parameter_noinfo
        return 0;
    }
    return v->begin + index;
}
// the 28-byte lip basic_string<char> assign, defined further down in the
// XLipComponent cluster (sub_17B8EF0 / sub_17B9240 family).
static void XLipStrAssign(F3AString *s, const char *src);
// sub_17CB6F0 (0x17CB6F0 - 0x17CB717) -- the 32-byte XLipCultures params
// object ctor (thiscall + `retn 8`): runs sub_17B8EF0, which resets the
// object's 28-byte string to the 16-byte SSO (capacity 15, length 0 --
// sub_17B9270) and assigns it from `src` (sub_17B9240 via the
// string-length + assign helpers): the source-name string.  Then the
// version WORD at +0x1C is set to `count`.  Returns the object.  Layout:
// +0x00 d0 (untouched), +0x04 SSO buffer / heap ptr, +0x14 length, +0x18
// capacity, +0x1C version (WORD; XLipLoadCultures compares a freshly loaded
// culture file's version against the installed object's).  `src` is the
// empty rdata default table g_unk_1BC1D48 in InitLipRuntime (the assign
// copies nothing) and the culture file name in XLipLoadCultures.
static DWORD *LipCultureParamsCtor(DWORD *obj, const char *src, unsigned short count)
{
    F3AString *s = (F3AString *)obj;
    s->u.ptr = 0;                      // SSO buffer / heap ptr (+0x04), empty
    s->len = 0;                        // string length (+0x14)
    s->cap = 15;                       // string capacity = 15 (SSO) (+0x18)
    XLipStrAssign(s, src);             // sub_17B9240: assign the source string
    *(unsigned short *)&obj[7] = count;    // version WORD (+0x1C)
    return obj;
}

// ============================================================
//  InitLipRuntime  (sub_17CA130, 0x17CA130 - 0x17CA256)  -- the lazy lip
//  culture runtime init (XLipCultures.cpp), called once from
//  InitLipCultures (sub_719770) after the LHAlloc / LHFree pair is
//  installed with the lip library (SetLipMemAllocators).
//  [NOT byte-identical] placement-dependent (relative calls to
//  LipCultureParamsCtor / LipDwordVecResize / LipDwordVecAt, the IAT / jump
//  thunks __imp_SetLastError / j_LHAlloc, and the SEH
//  ___CxxFrameHandler3_0 landing pad) and refs the high-VA globals
//  byte_1E2C6B4 (0x01E2C6B4), unk_1E2C6B8 (0x01E2C6B8) and dword_1E2C6D0
//  (0x01E2C6D0) plus the rdata table 0x1BC1D48 at their original VAs.
//
//  void InitLipRuntime(void)   -- original: cdecl, plain `retn`
//
//  Gated by the init flag byte_1E2C6B4: on re-entry it sets the last error
//  to 0x4DF (1247) and returns.  First call:
//    1. SetLastError(0).
//    2. LHAlloc's the 32-byte default-params object and constructs it from
//       the rdata table g_unk_1BC1D48 with culture count 0 (0 on allocation
//       failure), publishing it into dword_1E2C6D0.
//    3. Resizes the culture-class-id vector unk_1E2C6B8 to 6 elements
//       (LipDwordVecResize, through the lip allocator) and stores the six
//       class ids {0, 2, 3, 6, 15, 5} into indices 0..5 (LipDwordVecAt).
//    4. Sets byte_1E2C6B4 = 1.
//  The original wraps the body in an SEH frame whose handler frees the
//  32-byte block (allocator table +16 slot, sub_401090) if the body throws
//  (e.g. std::length_error from the vector grow); that path is unreachable
//  in this build (the lip allocator only ever returns null, never throws).
// ============================================================
static void InitLipRuntime(void)
{
    if (g_byte_1E2C6B4 == 1)
    {
        SetLastError(0x4DF);          // already initialized
        return;
    }
    SetLastError(0);
    DWORD *block = (DWORD *)LHAlloc(32);
    DWORD *params = (block != 0) ? LipCultureParamsCtor(block, (const char *)g_unk_1BC1D48, 0) : 0;
    g_dword_1E2C6D0 = (DWORD)params;
    LipDwordVecResize(&g_unk_1E2C6B8, 6, 0);
    *LipDwordVecAt(&g_unk_1E2C6B8, 0) = 0;
    *LipDwordVecAt(&g_unk_1E2C6B8, 1) = 2;
    *LipDwordVecAt(&g_unk_1E2C6B8, 2) = 3;
    *LipDwordVecAt(&g_unk_1E2C6B8, 3) = 6;
    *LipDwordVecAt(&g_unk_1E2C6B8, 4) = 15;
    *LipDwordVecAt(&g_unk_1E2C6B8, 5) = 5;
    g_byte_1E2C6B4 = 1;
}
// ============================================================
//  XLipCultures load-from-file cluster (XLipCultures.cpp): the culture
//  list object (unk_1E2C6D4 / unk_1E2C6D8), the two culture maps
//  (unk_1E2C620 / unk_1E2C640), the 0x58-byte culture-definition info
//  objects, the culture-file line reader (sub_17CEC00) and its two line
//  callbacks, and XLipLoadCultures (sub_17CC7C0) itself.
// ============================================================
// the lip formatted-message helper (0x17D0570), the lip assert reporter
// (0x17D04A0) and the lip debug-message sink (0x17D0610), all defined
// further down.
static int XLipDebugMessage(const wchar_t *format, ...);
static void XLipAssert(const wchar_t *format, ...);
static void XLipDebugMessageSink(const wchar_t *message);
// the assert-report format and the file names the original bakes into every
// XLipAssert call site (each original site pushes its own copies of these
// wide literals; the text is identical, so the sites share these).
static const wchar_t g_xlipAssertFailed[]  = L"Assert failed. File: %s. Line: %d. Condition: %s";
static const wchar_t g_xlipFileXLipX[]     = L"e:\\depot\\dev\\fable3\\pcbranch\\sourcecode\\externallibs\\xlip\\lib\\XLip_x.h";
static const wchar_t g_xlipFileCulturesH[] = L"e:\\depot\\dev\\fable3\\pcbranch\\sourcecode\\externallibs\\xlip\\lib\\XLipCultures.h";
static const wchar_t g_xlipFileCultures[]  = L".\\XLipCultures.cpp";
static const wchar_t g_xlipFileComponent[] = L".\\XLipComponent.cpp";
static const wchar_t g_xlipFileDebug[]     = L".\\XLipDebug.cpp";
static const wchar_t g_xlipFileSettings[]  = L".\\XLipSettings.cpp";
// 0x1E2C6D4 -- the XLipPtrVector<_tagXLIPCULTUREINFO> object (XLipCultures.
// cpp global): vtable at +0x00 (??_7?$XLipPtrVector@U_tagXLIPCULTUREINFO@@@@
// 6B@, set by the .CRT$Xu static ctor sub_1AA6400 -> sub_17CFF60; the slots
// are unimplemented and never dispatched in this build), then the 24-byte
// culture-definitions vector (unk_1E2C6D8) at +0x04.  Elements are pointers
// to the 0x58-byte culture-definition info objects.
static void *g_XLipPtrVectorVtable[8] = { 0 };
static DWORD g_unk_1E2C6D4[7] = { (DWORD)g_XLipPtrVectorVtable, 0, 0, 0, 0, 0, 0 };
// the 24-byte vector part of the object (begin / end / alloc-end at +0x0C /
// +0x10 / +0x14 of the vector, i.e. g_unk_1E2C6D4[1..6]).
static LipDwordVec *LipCultureListVec(void) { return (LipDwordVec *)&g_unk_1E2C6D4[1]; }
// 0x1E2C620 / 0x1E2C640 -- the two 32-byte STL culture maps (WORD id ->
// object pointer), constructed empty at process start by the .CRT$Xu static
// ctors (sub_1AA6340 -> sub_17BC0D0) and destroyed at exit (sub_1AD37A0 /
// sub_1AD3800).  The 32-byte layout: 16 bytes of _Rbtree header / allocator
// wrapper, the _Myinfo head-node pointer at +0x18, the _Mycount at +0x1C.
// The head node's left / right slots sit at info +4 / info +8; an empty map
// points both at info itself.  This build starts the maps in the constructed-
// empty state (zero-initialized statics) and nothing ever inserts into them
// (the only inserter, the culture-definition line callback, finds nothing to
// insert under), so they stay empty.
typedef struct XLipMap
{
    DWORD   rbt[4];         // +0x00 _Rbtree header (cmpfn / head / ...)
    DWORD   alloc[3];       // +0x0C node-allocator wrapper
    DWORD  *info;           // +0x18 _Myinfo (the head-node block)
    DWORD   count;          // +0x1C _Mycount
} XLipMap;
static char  g_xlipLangMapInfo[0x40] = { 0 };   // head node for the 0x1E2C620 map
static XLipMap g_unk_1E2C620 = { { 0 }, { 0 }, (DWORD *)g_xlipLangMapInfo, 0 };
static char  g_xlipCulMapInfo[0x40] = { 0 };    // head node for the 0x1E2C640 map
static XLipMap g_unk_1E2C640 = { { 0 }, { 0 }, (DWORD *)g_xlipCulMapInfo, 0 };
// 0x1BC33F4 -- the rdata L"null" default name (off_1D94004) used as the
// source name when a culture-definition line has no source field.
static const wchar_t g_xlipNullName[] = L"null";

// sub_17CF280 (0x17CF280 - 0x17CF36D) -- in-place wide-string whitespace
// trim (the XLip line trim): skip leading iswspace characters (the original
// tests each character through its local _putwchar wrapper, 0x17CA530, which
// is a plain _iswspace call), scan back from the end past trailing
// whitespace, NUL-terminate, and shift the result to the front.  An all-
// whitespace (or empty) string is left empty.  cdecl, one arg.
static void XLipTrimWide(wchar_t *s)
{
    wchar_t *p;
    wchar_t *q;
    if (s == NULL)
        return;
    p = s;
    while (iswspace(*p) != 0)
        ++p;
    if (*p != 0)
    {
        q = p + wcslen(p) - 1;
        while (iswspace(*q) != 0)
            --q;
        if (q < p)
            XLipAssert(g_xlipAssertFailed, g_xlipFileXLipX, 119, L"pEnd >= pStart");
        q[1] = 0;
        if (p > s)
        {
            wchar_t *end = q + 1;
            while (p <= end)
                *s++ = *p++;
        }
    }
    else
    {
        *s = 0;
    }
}
// sub_17B8F30 (0x17B8F30 - 0x17B8F47) -- the 28-byte lip basic_string<char>
// dtor (thiscall): frees the heap buffer when the capacity says heap (>= 16),
// then resets to the empty SSO state (capacity 15, length 0, buffer[0] = 0).
static void XLipStrDtor(F3AString *s)
{
    if (s->cap >= 16)
        LipMemFree((void *)s->u.ptr);
    s->cap = 15;
    s->len = 0;
    s->u.buf[0] = 0;
}
// sub_17B8EC0 (0x17B8EC0 - 0x17B8EF0) -- the 28-byte lip basic_string<char>
// reset to empty SSO (the flag = 0 form of sub_17B9270): writes the SSO
// sentinel (capacity 15), length 0, buffer[0] = 0 without reading the old
// capacity, so it is safe on a freshly-allocated (uninitialized) block.
static void XLipStrReset(F3AString *s)
{
    s->cap = 15;
    s->len = 0;
    s->u.buf[0] = 0;
}
// The 28-byte lip basic_string<wchar_t> (the same d0 / buffer / len / cap
// layout as F3AString, but 16 bytes of SSO buffer = 8 wchars and SSO
// capacity 7 -- sub_17CB950 resets it to cap 7 and goes heap at cap >= 8).
typedef struct W3AString
{
    DWORD   d0;
    union { wchar_t buf[8]; DWORD ptr; } u;
    DWORD   len;
    DWORD   cap;
} W3AString;
// sub_17CB950 (0x17CB950 - 0x17CB9C6; the char = 1 / size = 0 reset form
// is sub_17CB930, 0x17CB930 - 0x17CB947) -- the 28-byte lip wstring dtor:
// free the heap buffer when
// cap >= 8, then cap = 7, length 0, buffer[0] = 0.
static void W3AStringDtor(W3AString *s)
{
    if (s->cap >= 8)
        LipMemFree((void *)s->u.ptr);
    s->cap = 7;
    s->len = 0;
    s->u.buf[0] = 0;
}
// the 28-byte lip wstring reset to empty SSO (the flag = 0 form of
// sub_17CB950): writes the SSO sentinel (capacity 7), length 0, buffer[0]
// = 0 without reading the old capacity, so it is safe on a freshly-allocated
// (uninitialized) block.
static void W3AStringReset(W3AString *s)
{
    s->cap = 7;
    s->len = 0;
    s->u.buf[0] = 0;
}
// std::wstring::assign(const wchar_t *) (0x17CF9F0) -- the 28-byte lip
// wstring assign: SSO buffer when the length is < 8, else a LipMemAlloc'd
// heap buffer (the old one freed on a state change).
static void W3AStringAssign(W3AString *s, const wchar_t *src)
{
    unsigned int len = (unsigned int)wcslen(src);
    if (len < 8)
    {
        if (s->cap >= 8)
            LipMemFree((void *)s->u.ptr);
        s->cap = 7;
        memcpy(s->u.buf, src, (len + 1) * 2);
    }
    else
    {
        void *nb = LipMemAlloc((len + 1) * 2);
        if (s->cap >= 8)
            LipMemFree((void *)s->u.ptr);
        s->u.ptr = (DWORD)nb;
        s->cap = len;
        memcpy((void *)s->u.ptr, src, (len + 1) * 2);
    }
    s->len = len;
}
// The 0x58-byte culture-definition info object (_tagXLIPCULTUREINFO):
//    +0x00  name     (28-byte basic_string<char>; the culture name)
//    +0x1C  language (28-byte basic_string<wchar_t>; the language name)
//    +0x38  source   (28-byte basic_string<char>; the source / parent name)
//    +0x54  version  (WORD)
typedef struct XLipCultureInfo
{
    F3AString name;
    W3AString language;
    F3AString source;
    unsigned short version;
    unsigned short pad;
} XLipCultureInfo;                 // 0x58 bytes
// sub_17CF840 (0x17CF840 - 0x17CF967) -- the culture-definition info ctor
// (thiscall + `retn 10h`): resets the name string (+0x00), constructs and
// assigns the wide language string (+0x1C) from `language`, resets the
// source string (+0x38), stores the version WORD at +0x54, then
// WideCharToMultiByte's `name` and `source` into an 85-byte stack buffer
// (asserting the count is > 0, XLipCultures.h lines 93 / 98) and assigns
// each (sub_17CF990 = sub_17B9240).  Returns the object.
static XLipCultureInfo *XLipCultureInfoCtor(XLipCultureInfo *obj,
                                            const wchar_t *name,
                                            const wchar_t *language,
                                            const wchar_t *source,
                                            unsigned short version)
{
    CHAR mb[88];
    XLipStrReset(&obj->name);                    // sub_17B8EC0: reset +0x00
    W3AStringReset(&obj->language);              // the +0x1C wstring reset
    W3AStringAssign(&obj->language, language);   // (sub_17CF9B0: reset + assign)
    XLipStrReset(&obj->source);                  // sub_17B8EC0: reset +0x38
    obj->version = version;                      // +0x54
    if (WideCharToMultiByte(0, 0, name, -1, mb, 85, NULL, NULL) <= 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCulturesH, 93, L"iRet > 0");
    XLipStrAssign((F3AString *)&obj->name, mb);  // sub_17CF990
    if (WideCharToMultiByte(0, 0, source, -1, mb, 85, NULL, NULL) <= 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCulturesH, 98, L"iRet > 0");
    XLipStrAssign(&obj->source, mb);             // sub_17CF990
    return obj;
}
// sub_17CB870 (0x17CB870 - 0x17CB89C) -- the culture-definition info deleting
// destructor (thiscall + `retn 4`): the dtor body (sub_17CB8C0: the source
// string at +0x38, the wide language string at +0x1C, the name string at
// +0x00) then LipMemFree (sub_17CB8A0) when freeIt & 1.
static void XLipCultureInfoDtorFree(XLipCultureInfo *obj)
{
    XLipStrDtor(&obj->source);            // +0x38 (sub_17B8F30)
    W3AStringDtor(&obj->language);        // +0x1C (sub_17CB930)
    XLipStrDtor((F3AString *)&obj->name); // +0x00 (sub_17B8F30)
    LipMemFree(obj);                      // sub_17CB8A0
}
// sub_17CD320 (0x17CD320 - 0x17CD354) -- the culture-list vector capacity:
// (allocEnd - begin) / 4 (0 when begin is null).
static unsigned int LipCultureListCap(const LipDwordVec *v)
{
    return (v->begin != NULL)
           ? (unsigned int)((char *)v->allocEnd - (char *)v->begin) >> 2
           : 0;
}
// sub_17CD570 (0x17CD570 - 0x17CD58A) -- the culture-list vector size:
// (end - begin) / 4.
static unsigned int LipCultureListSize(const LipDwordVec *v)
{
    return (unsigned int)((char *)v->end - (char *)v->begin) >> 2;
}
// sub_17CD1B0 (0x17CD1B0 - 0x17CD319) -- reserve `n` elements: when the
// capacity is smaller, LipMemAlloc(4 * n) (sub_17CD6F0), move the current
// elements, free the old buffer, and reset begin / end / alloc-end.
static void LipCultureListReserve(LipDwordVec *v, unsigned int n)
{
    unsigned int size;
    DWORD *nb;
    if (LipCultureListCap(v) >= n)
        return;
    size = LipCultureListSize(v);
    nb = (DWORD *)LipMemAlloc(4 * n);    // sub_17CD6F0
    for (unsigned int i = 0; i < size; i++)
        nb[i] = v->begin[i];
    if (v->begin != NULL)
        LipMemFree(v->begin);
    v->begin    = nb;
    v->end      = nb + size;
    v->allocEnd = nb + n;
}
// sub_17CD360 (0x17CD360 - 0x17CD3D3) -- push_back: grow to exactly size + 1
// when size == capacity (sub_17CD630), store the element, advance end.
static void LipCultureListPush(LipDwordVec *v, DWORD value)
{
    unsigned int size = LipCultureListSize(v);
    if (size >= LipCultureListCap(v))
    {
        DWORD *nb = (DWORD *)LipMemAlloc(4 * (size + 1));
        for (unsigned int i = 0; i < size; i++)
            nb[i] = v->begin[i];
        if (v->begin != NULL)
            LipMemFree(v->begin);
        v->begin    = nb;
        v->end      = nb + size;
        v->allocEnd = nb + size + 1;
    }
    v->begin[size] = value;
    v->end = v->begin + size + 1;
}
// sub_17CB740 (0x17CB740 - 0x17CB7FC) -- clear the culture-definitions list
// (thiscall + `retn 4`, the XLipPtrVector<_tagXLIPCULTUREINFO> object in ECX
// with the 24-byte vector at +4): walks the vector from begin to end; when
// freeFlag is set, frees each element's 0x58-byte block through the deleting
// destructor (sub_17CBBA0 lookup + sub_17CB870), then erases the element
// (sub_17CBAC0: the tail shifts down one slot, end shrinks) until the vector
// is empty.
//  [NOT byte-identical] placement-dependent (relative calls to the iterator /
//  erase / deleting-destructor helpers) and refs the high-VA culture list
//  object 0x1E2C6D4 at its original VA.
static void XLipClearCultureList(void *obj, char freeFlag)
{
    LipDwordVec *vec = (LipDwordVec *)((char *)obj + 4);
    for (DWORD *pos = vec->begin; pos < vec->end;)
    {
        if (freeFlag)
        {
            DWORD block = *pos;
            if (block != 0)
                XLipCultureInfoDtorFree((XLipCultureInfo *)(void *)block);   // sub_17CB870
        }
        if (pos + 1 < vec->end)
            memmove_s(pos, (char *)vec->end - (char *)(pos + 1), pos + 1,
                      (char *)vec->end - (char *)(pos + 1));
        vec->end = vec->end - 1;
    }
}
// sub_17BE8F0 (0x17BE8F0 - 0x17BE948) -- the culture-map clear (thiscall on
// the 32-byte map): recursively destroys the tree nodes (sub_17BE990 over
// the head node's left child; a no-op for the empty maps this build keeps),
// then resets the head node's left / right slots to the _Myinfo pointer and
// the count to 0 (the right slot is stored twice).
//  [NOT byte-identical] placement-dependent (relative calls to the node /
//  slot helpers) and refs the high-VA map globals at their original VAs.
static void XLipMapClear(XLipMap *map)
{
    DWORD *info = map->info;
    // (sub_17BE990's node destroy walk stops at the sentinel for the empty
    // maps this build keeps -- nothing ever inserts into them.)
    info[1] = (DWORD)info;          // head node left  -> info (empty tree)
    map->count = 0;
    info[2] = (DWORD)info;          // head node right -> info
    info[2] = (DWORD)info;          // (the original stores this slot twice)
}
// ============================================================
//  XLipReadCultureFile  (sub_17CEC00, 0x17CEC00 - 0x17CF274)  -- the
//  culture-file line reader (XLipCultures.cpp): open `fileName` for read,
//  check the 2-byte coding word when `coding` is nonzero (0xFFFE for
//  culture files), then read 256-byte (128-wchar) chunks, tokenize each
//  chunk on "\n\r" (wcstok_s, a fresh context per chunk), append each token
//  to a 1026-wchar line buffer (wcscat_s, capped at 0x400 words; a token
//  that runs into the buffer's 0xFFFF sentinel -- an overlong line -- defers
//  its callback to the next chunk) and hand each line to `lineCb(line,
//  pvInfo)`.  A callback that fails with last error 0x10D2 (4306) means
//  "skip this line" (not counted, not an error); any other failure (and a
//  missing file, a bad magic, an odd-length read, or a read failure) makes
//  the whole read fail.  Returns the number of lines the callback accepted
//  (>= 0) or -1 on failure.
//  [NOT byte-identical] placement-dependent (relative calls to
//  XLipAssert / XLipDebugMessage / wcstok_s / wcscat_s / wcslen and the SEH
//  ___CxxFrameHandler3_0 landing pad) and refs the high-VA init-flag global
//  byte_1E2C6B4 (0x01E2C6B4) plus the CreateFileA / ReadFile IAT slots.
//
//  int XLipReadCultureFile(const char *fileName, unsigned short coding,
//                          int (*lineCb)(wchar_t *, int), int pvInfo)
//  -- original: cdecl, plain `retn`
// ============================================================
static int XLipReadCultureFile(const char *fileName, unsigned short coding,
                               int (*lineCb)(wchar_t *, int), int pvInfo)
{
    HANDLE file;
    int count = 0;
    int failed = 0;
    int nearEnd = 0;
    wchar_t line[1026];
    wchar_t buffer[134];
    wchar_t delim[4];
    wchar_t *context = NULL;
    errno_t appendErr = 0;

    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return -1;
    }
    if (fileName == NULL)
    {
        XLipAssert(g_xlipAssertFailed, g_xlipFileXLipX, 142, L"pcszFileName");
        SetLastError(0x57);
        return -1;
    }
    file = CreateFileA(fileName, 0x80000000, 1, NULL, 3, 0x8000080, NULL);
    // the XLipAutoResource handle wrapper (sub_17B8640 ctor): the invalid
    // handle fails the read; the dtor (sub_17B8670 -> sub_17D0620) closes
    // the handle when it is neither 0 nor INVALID_HANDLE_VALUE.
    if (file == INVALID_HANDLE_VALUE)
    {
        failed = 1;
        goto cleanup;
    }
    if (coding != 0)
    {
        unsigned char magic[2];
        DWORD got = 0;
        if (!ReadFile(file, &magic[0], 1, &got, NULL))
        {
            XLipDebugMessage(L"Can't read from file %S", fileName);
            failed = 1;
            goto cleanup;
        }
        if (got != 1)
        {
            XLipDebugMessage(L"Can't read enough data from file %S", fileName);
            SetLastError(0xB);
            failed = 1;
            goto cleanup;
        }
        if (!ReadFile(file, &magic[1], 1, &got, NULL))
        {
            XLipDebugMessage(L"Can't read from file %S", fileName);
            failed = 1;
            goto cleanup;
        }
        if (got != 1)
        {
            XLipDebugMessage(L"Can't read enough data from file %S", fileName);
            SetLastError(0xB);
            failed = 1;
            goto cleanup;
        }
        if ((unsigned short)(magic[1] | (magic[0] << 8)) != coding)
        {
            XLipDebugMessage(L"File is not on the expected coding (%04x != %04x)",
                             (unsigned)coding, (unsigned)(magic[1] | (magic[0] << 8)));
            SetLastError(0xB);
            failed = 1;
            goto cleanup;
        }
    }
    line[0] = 0;
    buffer[128] = 0;                             // the long-line NUL sentinel
    buffer[129] = 0xFFFF;                        // the overlong-line marker
    while (nearEnd == 0 && failed == 0)
    {
        DWORD got = 0;
        if (!ReadFile(file, buffer, 256, &got, NULL))
        {
            XLipDebugMessage(L"Can't read from file %S", fileName);
            failed = 1;
            break;
        }
        if (got == 0)
        {
            if (line[0] != 0)
            {
                if (lineCb(line, pvInfo) != 0)
                    ++count;
                else if (GetLastError() != 0x10D2)
                    failed = 1;
            }
            break;
        }
        if (got % 2 != 0)
        {
            XLipDebugMessage(L"Bad file format (alignment) in file %S", fileName);
            SetLastError(0xB);
            failed = 1;
            break;
        }
        if (got < 256)
        {
            nearEnd = 1;
            buffer[got >> 1] = 0;
        }
        wcscpy(delim, L"\n\r");
        if ((buffer[0] == 0 || buffer[0] == 10 || buffer[0] == 13) && line[0] != 0)
        {
            int flushResult = lineCb(line, pvInfo);
            if (flushResult != 0)
                ++count;
            else if (GetLastError() != 0x10D2)
                failed = 1;                       // falls through to the token loop
            // clear the buffer on success or a 0x10D2 skip (the original's LABEL_44);
            // a hard error leaves the stale buffer in place and errors out.
            if (flushResult != 0 || GetLastError() == 0x10D2)
                line[0] = 0;
        }
        for (wchar_t *token = wcstok_s(buffer, delim, &context); token != NULL;
             token = wcstok_s(NULL, delim, &context))
        {
            appendErr = wcscat_s(line, 0x400, token);   // sub_17CA590
            if (appendErr != 0)
            {
                XLipDebugMessage(L"Can't append file line, errno = %u", (unsigned)appendErr);
                SetLastError(0x54F);
                failed = 1;
                break;
            }
            if (nearEnd == 0)
            {
                size_t len = wcslen(token);
                if (token[len + 1] == 0xFFFF)     // overlong line: defer to the next chunk
                    break;
            }
            if (lineCb(line, pvInfo) != 0)
                ++count;
            else if (GetLastError() != 0x10D2)
            {
                failed = 1;
                break;
            }
            line[0] = 0;
        }
    }
cleanup:
    if (file != 0 && file != INVALID_HANDLE_VALUE)   // the handle wrapper dtor
        CloseHandle(file);
    if (failed != 0)
        return -1;
    return count;
}
// ============================================================
//  XLipParseVersionLine  (sub_17CC940, 0x17CC940 - 0x17CCAAF)  -- the
//  culture-file "version=NNNN" line callback (XLipCultures.cpp line 235).
//  [NOT byte-identical] placement-dependent (relative calls to
//  XLipAssert / XLipDebugMessage / XLipTrimWide / wcstok_s / _wcsicmp /
//  wcstoul and the SEH ___CxxFrameHandler3_0 landing pad).
//
//  int XLipParseVersionLine(wchar_t *line, int pvInfo)   -- cdecl, `retn`
//
//  Asserts line != NULL && pvInfo != 0 (line 235, SetLastError(0x57) and 0
//  when either is null); trims the line, and an empty or ';' comment line is
//  skipped (SetLastError(0x10D2), 0).  Splits on "="; a missing key or value
//  is skipped (0x10D2, 0).  When the trimmed key is "version" (case-
//  insensitive), the trimmed value is wcstoul'd: 0 or > 0xFFFF is reported
//  ("Invalid version found in culture file: %S", SetLastError(0xB), 0),
//  otherwise it is stored as the version WORD at pvInfo + 28 (the fresh
//  params object's +0x1C).  Returns 1 for a processed line.
// ============================================================
static int XLipParseVersionLine(wchar_t *line, int pvInfo)
{
    wchar_t *context = NULL;
    wchar_t delim[4];
    wchar_t *key;
    wchar_t *value;
    unsigned int version;

    if (line == NULL || pvInfo == 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 235, L"pwszLine && pvInfo");
    if (line == NULL || pvInfo == 0)
    {
        SetLastError(0x57);
        return 0;
    }
    XLipTrimWide(line);
    if (line[0] == 0 || line[0] == 59)
    {
        SetLastError(0x10D2);
        return 0;
    }
    wcscpy(delim, L"=");
    key = wcstok_s(line, delim, &context);
    value = wcstok_s(NULL, delim, &context);
    if (key == NULL || value == NULL)
    {
        SetLastError(0x10D2);
        return 0;
    }
    XLipTrimWide(key);
    XLipTrimWide(value);
    if (_wcsicmp(L"version", key) == 0)
    {
        version = wcstoul(value, NULL, 0);
        if (version == 0 || version > 0xFFFF)
        {
            XLipDebugMessage(L"Invalid version found in culture file: %S", value);
            SetLastError(0xB);
            return 0;
        }
        *(unsigned short *)(pvInfo + 28) = (unsigned short)version;
    }
    return 1;
}
// ============================================================
//  XLipParseCultureDefLine  (sub_17CCAB0, 0x17CCAB0 - 0x17CD061)  -- the
//  culture-definition line callback (XLipCultures.cpp line 298): "cultureId,
//  languageId, name, language, source[, version]".  [NOT byte-identical]
//  placement-dependent (relative calls to XLipAssert / XLipDebugMessage /
//  XLipTrimWide / wcstok_s / _wcslwr_s / wcschr / wcstoul / the reserve /
//  push helpers / XLipCultureInfoCtor, the WideCharToMultiByte IAT slot, and
//  the SEH ___CxxFrameHandler3_0 landing pad) and refs the high-VA globals
//  byte_1E2C6B4 / unk_1E2C6B8 / unk_1E2C620 / unk_1E2C640 / unk_1E2C6D8 and
//  the rdata L"null" (0x1BC33F4) at their original VAs.
//
//  int XLipParseCultureDefLine(wchar_t *line, int pvInfo)   -- cdecl, plain
//  `retn 8` (the reader's pvInfo is always 0 for this callback; the original
//  does not use it, but the reader passes both arguments through the
//  callback type)
//  (the reader's pvInfo is always 0 for this callback; the original does not
//  use it)
//
//  Asserts line != NULL (line 298, SetLastError(0x57) and 0); trims the
//  line, and an empty, "version..." or ';' comment line is skipped
//  (SetLastError(0x10D2), 0).  Splits on "," / tab: the first two fields are
//  wcstoul'd (the culture / language ids, 0 when absent), the third is the
//  culture name (lower-cased in place; a '.' or '\' in it is an error,
//  "Invalid character found on culture name: %s", SetLastError(0xB), 0),
//  the fourth is the language name, the fifth the source name (empty or
//  missing = the rdata L"null"), and an optional sixth is the version
//  (wcstoul'd; 0 or >= 0xFFFF asserts line 410 and fails with "Bad format
//  on culture definition string: %s", SetLastError(0xB), 0).  The "register
//  this culture name under the culture / language id" chain (sub_17B44E0 /
//  sub_17B4510 map find + sub_17CD3E0 / sub_17CF990 insert, through the
//  maps at unk_1E2C620 / unk_1E2C640) only runs when the find hits -- the
//  maps stay empty in this build (nothing inserts), so the chain is dead.
//  Then: the culture list is reserved to the class-id vector's element 4
//  (15) when its capacity is 0, a 0x58-byte block is LipMemAlloc'd and
//  constructed (XLipCultureInfoCtor), and pushed into the list
//  (unk_1E2C6D8).  Returns 1.
// ============================================================
static int XLipParseCultureDefLine(wchar_t *line, int pvInfo)
{
    wchar_t *context = NULL;
    (void)pvInfo;
    wchar_t delim[4];
    wchar_t *name;
    wchar_t *language;
    wchar_t *source;
    unsigned int cultureId = 0;
    unsigned int languageId = 0;
    unsigned short version = 1;
    unsigned int tokenVersion;
    size_t len;
    int ret;
    CHAR mb[88];

    if (line == NULL)
    {
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 298, L"pwszLine");
        SetLastError(0x57);
        return 0;
    }
    XLipTrimWide(line);
    if (line[0] == 0 || line[0] == 59 || wcsncmp(L"version", line, 7) == 0)
    {
        SetLastError(0x10D2);
        return 0;
    }
    wcscpy(delim, L",\t");
    {
        wchar_t *token = wcstok_s(line, delim, &context);
        if (token != NULL)
            cultureId = wcstoul(token, NULL, 0);
        token = wcstok_s(NULL, delim, &context);
        if (token != NULL)
            languageId = wcstoul(token, NULL, 0);
    }
    (void)cultureId; (void)languageId;    // (the map-find chain, dead below)
    name = wcstok_s(NULL, delim, &context);
    language = wcstok_s(NULL, delim, &context);
    if (name == NULL || language == 0)
    {
        XLipDebugMessage(L"Bad format on culture definition string: %s", line);
        SetLastError(0xB);
        return 0;
    }
    len = wcslen(name);
    ret = _wcslwr_s(name, len + 1);
    if (ret != 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 359, L"ret==0");
    if (wcschr(name, 46) != NULL)               // '.'
    {
        XLipDebugMessage(L"Invalid character found on culture name: %s", name);
        SetLastError(0xB);
        return 0;
    }
    if (wcschr(name, 92) != NULL)               // '\\'
    {
        XLipDebugMessage(L"Invalid character found on culture name: %s", name);
        SetLastError(0xB);
        return 0;
    }
    if (WideCharToMultiByte(0, 0, name, -1, mb, 85, NULL, NULL) <= 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 384, L"iRet > 0");
    // (The "register under culture id / language id" chain -- the unk_1E2C620
    // / unk_1E2C640 map finds + sub_17CD3E0 / sub_17CF990 inserts -- runs
    // only on a hit; the maps are empty in this build, so it is dead.)
    source = wcstok_s(NULL, delim, &context);
    if (source != NULL)
    {
        len = wcslen(source);
        ret = _wcslwr_s(source, len + 1);
        if (ret != 0)
            XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 399, L"ret==0");
        if (ret != 0)
        {
            SetLastError(0x54F);
            return 0;
        }
        {
            wchar_t *token = wcstok_s(NULL, delim, &context);
            if (token != NULL)
            {
                tokenVersion = wcstoul(token, NULL, 0);
                if (tokenVersion == 0 || tokenVersion >= 0xFFFF)
                    XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 410,
                               L"ulVersion!=0 && ulVersion < XLIPLANGUAGE_MAXVERSION");
                if (tokenVersion == 0 || tokenVersion >= 0xFFFF)
                {
                    XLipDebugMessage(L"Bad format on culture definition string: %s", line);
                    SetLastError(0xB);
                    return 0;
                }
                version = (unsigned short)tokenVersion;
            }
        }
    }
    if (source == NULL || source[0] == 0)
        source = (wchar_t *)g_xlipNullName;     // off_1D94004 (0x1BC33F4)
    if (LipCultureListCap(LipCultureListVec()) == 0)
        LipCultureListReserve(LipCultureListVec(), *LipDwordVecAt(&g_unk_1E2C6B8, 4));
    {
        void *block = LipMemAlloc(0x58);        // sub_17CF970
        XLipCultureInfo *info = (block != NULL)
            ? XLipCultureInfoCtor((XLipCultureInfo *)block, name, language, source, version)
            : NULL;
        LipCultureListPush(LipCultureListVec(), (DWORD)info);    // sub_17CD360
    }
    return 1;
}
// sub_17CD150 (0x17CD150 - 0x17CD169) -- install a freshly loaded params
// object's source-name string into the installed global (thiscall + `retn 4`,
// the global params object at dword_1E2C6D0 in ECX, the fresh object on the
// stack): basic_string::assign (sub_17B3D30 -> sub_17B9DD0(this, src, 0,
// -1)) -- copy the whole source string (SSO or heap, reserving / growing the
// destination heap buffer when the length is >= 16; the original keeps a
// heap buffer when shrinking below 16, so the destination never re-enters
// SSO).  Returns 1.
//  [NOT byte-identical] placement-dependent (relative calls to the string
//  assign / reserve / setlen helpers) and refs the high-VA global params
//  object pointer 0x1E2C6D0 at its original VA.
static int XLipCultureParamsInstall(void *obj, const void *src)
{
    F3AString *dst = (F3AString *)obj;
    const F3AString *s = (const F3AString *)src;
    const char *data = (s->cap < 16) ? s->u.buf : (const char *)s->u.ptr;
    unsigned int len = s->len;
    if (len >= 16)                          // reserve (sub_17B9E90)
    {
        if (dst->cap < 16)
        {
            dst->u.ptr = (DWORD)LipMemAlloc(len + 1);
            dst->cap = len + 1;
        }
        else if (dst->cap < len + 1)
        {
            LipMemFree((void *)dst->u.ptr);
            dst->u.ptr = (DWORD)LipMemAlloc(len + 1);
            dst->cap = len + 1;
        }
    }
    {
        char *d = (dst->cap < 16) ? dst->u.buf : (char *)dst->u.ptr;
        memcpy(d, data, len);
        d[len] = 0;                         // setlen (sub_17B96E0)
    }
    dst->len = len;
    return 1;
}
// sub_17CA610 (0x17CA610 - 0x17CA63C) -- the 32-byte params object deleting
// destructor (thiscall + `retn 4`): the 28-byte string dtor (sub_17CA640 ->
// sub_17B8F30), then LHFree (sub_401090) when freeIt & 1.
static void XLipCultureParamsDtor(void *obj, char freeIt)
{
    XLipStrDtor((F3AString *)obj);
    if (freeIt & 1)
        LHFree(obj);
}
// ============================================================
//  XLipLoadCultures  (sub_17CC7C0, 0x17CC7C0 - 0x17CC931)  -- load the
//  lip-sync culture set from a culture file (XLipCultures.cpp; the
//  XLipLoadCultures API of the xlip library).
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc /
//  LipCultureParamsCtor / XLipReadCultureFile / XLipClearCultureList /
//  XLipMapClear / XLipCultureParamsInstall / XLipCultureParamsDtor, the
//  sub_17CC940 / sub_17CCAB0 callback addresses, and the SEH
//  ___CxxFrameHandler3_0 landing pad) and refs the high-VA init-flag global
//  byte_1E2C6B4 (0x01E2C6B4), the installed params object pointer
//  dword_1E2C6D0 (0x01E2C6D0), and the culture map / list globals
//  (0x1E2C620 / 0x1E2C640 / 0x1E2C6D4) at their original VAs.
//
//  int XLipLoadCultures(const char *fileName)   -- original: cdecl, plain
//                                                 `retn`
//
//  Gated by the init flag byte_1E2C6B4 (SetLastError(0x15), -1 when unset);
//  a null file name is SetLastError(0x57), -1.  LHAlloc's a fresh 32-byte
//  params object and constructs it from the file name with the version WORD
//  at +0x1C = 1 (LipCultureParamsCtor); parses the file's lines with the
//  version-line callback (XLipParseVersionLine), which updates that WORD
//  from the file's "version=NNNN" line.  When the fresh object's version is
//  newer than the installed global's (dword_1E2C6D0 + 0x1C), it clears the
//  culture list (XLipClearCultureList, the XLipPtrVector at unk_1E2C6D4)
//  and the two culture maps (XLipMapClear, unk_1E2C620 / unk_1E2C640),
//  re-parses the file with the culture-definition-line callback
//  (XLipParseCultureDefLine), and, when at least one line was parsed, stores
//  the new version WORD and source-name string into the installed global
//  params object (XLipCultureParamsInstall).  The fresh 32-byte block is
//  always freed through the deleting destructor (XLipCultureParamsDtor).
//  Returns the line reader's count (>= 0) or -1 on failure.  The original
//  derefs the fresh object unconditionally after LHAlloc (a null allocation
//  is not handled); the reimplementation keeps that behavior.
// ============================================================
static int XLipLoadCultures(const char *fileName)
{
    int result;
    DWORD *info;

    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return -1;
    }
    if (fileName == NULL)
    {
        SetLastError(0x57);
        return -1;
    }
    {
        DWORD *block = (DWORD *)LHAlloc(32);   // j_LHAlloc
        info = (block != 0) ? LipCultureParamsCtor(block, fileName, 1) : 0;
    }
    result = XLipReadCultureFile(fileName, 0xFFFE, XLipParseVersionLine, (int)info);
    if (*(unsigned short *)((char *)info + 28) > *(unsigned short *)((char *)g_dword_1E2C6D0 + 28))
    {
        XLipClearCultureList((void *)g_unk_1E2C6D4, 1);
        XLipMapClear(&g_unk_1E2C620);
        XLipMapClear(&g_unk_1E2C640);
        result = XLipReadCultureFile(fileName, 0xFFFE, XLipParseCultureDefLine, 0);
        if (result > 0)
        {
            *(unsigned short *)((char *)g_dword_1E2C6D0 + 28) = *(unsigned short *)((char *)info + 28);
            XLipCultureParamsInstall((void *)g_dword_1E2C6D0, info);
        }
    }
    if (info != 0)
        XLipCultureParamsDtor(info, 1);
    return result;
}
//  XLipAssert  (sub_17D04A0, 0x17D04A0 - 0x17D056F)  -- the lip library's
//  assert reporter (XLipDebug.cpp): vswprintf_s-formats the caller's wide
//  "Assert failed. File: %s. Line: %d. Condition: %s" message into a
//  1024-wchar stack buffer (falling back to the raw format when the format
//  fails / is empty, and to the fixed L"XLipAssert hit!" text when the
//  format itself is null) and hands it to the (no-op) XLipDebugMessageSink.
//  Then it dispatches on the first culture-class id (LipDwordVecAt
//  (&g_unk_1E2C6B8, 0)): 1 = DebugBreak, 3 = throw the L"XLipAssert hit!"
//  pointer with the NULL-type __TI2PAD throwinfo (only a catch(...) handler
//  can see it).  Class id 0 (the value InitLipRuntime installs) is
//  report-only, so every lip assert in this build degrades to a sink no-op.
//
//  void XLipAssert(const wchar_t *format, ...)   -- cdecl, plain `retn`
//  [NOT byte-identical] placement-dependent (relative calls to
//  _vswprintf_s / XLipDebugMessageSink / LipDwordVecAt /
//  __security_check_cookie), plus the high-VA refs to the IAT slots
//  __imp_DebugBreak / __CxxThrowException@8, the rdata __TI2PAD throwinfo
//  (0x1C7C758) and the L"XLipAssert hit!" literals (0x1BC1160 / 0x1BC1180).
static const wchar_t g_xlipAssertHit[] = L"XLipAssert hit!";   // rdata 0x1BC1160
// the CRT throw primitive: the original stores the string pointer in a stack
// slot and calls _CxxThrowException(slot, &__TI2PAD); __TI2PAD is the 12-byte
// { NULL, 0, 0, NULL } throwinfo, so the exception carries no type_info.
struct XLipThrowInfo { const void *pType; unsigned short size, padsize; void *pThrow; };
// The CRT throw primitive is the compiler builtin `_CxxThrowException`
// (referenced by `throw`, exported by msvcr90 as __CxxThrowException@8).
// It is declared by the compiler itself, so no forward declaration is needed
// (declaring it with extern "C" trips C2733); it takes the exception object
// pointer and a built-in `_ThrowInfo *`, so the all-zero struct below is cast
// to that type at the call.
static void XLipAssert(const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    if (format != NULL)
    {
        wchar_t buffer[1024];
        int count = vswprintf_s(buffer, 1024, format, args);
        if (count > 0)
            XLipDebugMessageSink(buffer);
        else
            XLipDebugMessageSink(format);          // the raw format on failure
    }
    else
        XLipDebugMessageSink(g_xlipAssertHit);     // "XLipAssert hit!"
    va_end(args);
    {
        DWORD classId = *LipDwordVecAt(&g_unk_1E2C6B8, 0);
        if (classId == 1)
            DebugBreak();
        else if (classId == 3)
        {
            const wchar_t *hit = g_xlipAssertHit;
            const XLipThrowInfo throwInfo = { NULL, 0, 0, NULL };  // __TI2PAD
            _CxxThrowException((void *)&hit, (_ThrowInfo *)&throwInfo);
        }
    }
}

// sub_17C47E0 (0x17C47E0 - 0x17C4842) -- delete the char at `pos` out of the
// `size`-char string at `base`: memmove_s the tail (size - offset) bytes one
// slot left (a zeroed terminator when pos is the last char), returning the
// base (0 on a memmove_s failure).  The return value is discarded by the
// only caller.
static void LipPathRemoveChar(char *base, int size, char *pos)
{
    int srcSize = size - (int)(pos - base);
    if (srcSize != 0)
        (void)memmove_s(pos, srcSize + 1, pos + 1, srcSize);
    else
        *pos = 0;
}

// ============================================================
//  NormalizeLipPath  (sub_17C08D0, 0x17C08D0 - 0x17C0A0C)  -- in-place lip
//  path normalization (XLipComponent.cpp), used by CreateLipFSRoot before
//  the XLipFSRoot is constructed.
//  [NOT byte-identical] placement-dependent (relative calls to
//  XLipAssert / _strlen / __strlwr_s / j__strstr / LipPathRemoveChar /
//  j__strchr / the __security_check_cookie landing pad).
//
//  void *NormalizeLipPath(char *path)   -- original: cdecl, plain `retn`
//
//  Asserts pszPath != NULL (line 784) and cchPath > 0 (line 788),
//  lower-cases the string in place (_strlwr_s), skips past a "X:\" drive
//  prefix, then walks the rest: every run of consecutive backslashes is
//  deleted in place (LipPathRemoveChar, the length tracked down), a single
//  backslash is kept and skipped over, until the terminator.  A trailing
//  backslash is stripped (set to 0).
// ============================================================
static void *NormalizeLipPath(char *path)
{
    if (path == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 784, L"pszPath");
    int len = (int)strlen(path);
    if (len <= 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 788, L"cchPath > 0");
    _strlwr_s(path, (unsigned)(len + 1));
    char *walk = strstr(path, ":\\");
    walk = (walk != NULL) ? walk + 2 : path;
    char *result = walk;
    while (walk != NULL)
    {
        result = walk;
        if (*walk == 0)
            break;
        if (*walk == '\\')
        {
            while (*walk == '\\')
            {
                LipPathRemoveChar(path, len, walk);
                --len;
            }
            ++walk;
        }
        else
        {
            result = strchr(walk, '\\');
            walk = result;
            if (result != NULL)
                ++walk;
        }
    }
    if (path[len - 1] == '\\')
    {
        --len;
        path[len] = 0;
    }
    return result;
}

// ============================================================
//  XLipFSRoot -- the 0x110-byte lip file-system root object
//  (XLipComponent.cpp):
//    +0x00  vtable (XLipFSRoot::`vftable', 0x1BC446C)
//    +0x04  "XLIPFSRT" tag (8 bytes)
//    +0x0C  path (char[0x104])
// ============================================================
typedef struct XLipFSRoot
{
    void   *vtable;             // +0x00
    char    tag[8];             // +0x04 "XLIPFSRT"
    char    path[0x104];        // +0x0C
} XLipFSRoot;
// a vtable slot (thiscall, one stack arg).
typedef void (__thiscall *XLipFSRootVfn)(void *obj, int arg);

static void *GetXLipFSRootVtable(void);
// XLipFSRoot vtable slot 5 (sub_17D1680), defined below with InitLipFSRoot.
static int XLipFSRootIsInitDirValid(void *root);

// sub_17D1A10 (0x17D1A10 - 0x17D1A3C) -- the XLipFSRoot deleting destructor
// (vtable slot 7; thiscall + `retn 4`, the freeIt flag on the stack): the
// dtor body (sub_17D1A40, re-stores the XLipFSRoot vtable) runs, then
// LipMemFree (via sub_17D1860) frees the block when freeIt & 1, and the
// object is returned.
//  [NOT byte-identical] placement-dependent (relative call to LipMemFree).
//  __declspec(naked) so it matches the original's thiscall + `retn 4` frame
//  when dispatched through the vtable slot.
static __declspec(naked) void *XLipFSRootDtor(void *obj, char freeIt)
{
    __asm
    {
        push  ecx                            ; stack: [esp]=obj, [esp+4]=freeIt
        call  GetXLipFSRootVtable            ; [esp]=ret, [esp+4]=obj, [esp+8]=freeIt
        mov   ecx, [esp + 4]                 ; obj
        mov   [ecx], eax                     ; dtor body (sub_17D1A40): obj+0 <- vtable
        mov   eax, [esp + 8]                 ; freeIt
        and   eax, 1
        test  eax, eax
        jz    L_xlipfsrootdtor_skip
        push  ecx                            ; obj
        call  LipMemFree
        add   esp, 4
L_xlipfsrootdtor_skip:
        pop   ecx
        ret   4
    }
}

// XLipFSRoot::`vftable' (0x1BC446C, 8 slots).  Slot 7 is the deleting
// destructor (sub_17D1A10); slot 5 is the init-directory check
// (XLipFSRootIsInitDirValid, sub_17D1680), dispatched by InitLipFSRoot /
// XLipBringUpComponents; the remaining slots (the IXLipRoot overrides,
// sub_17D0930 .. sub_17D16F0) are unimplemented and never dispatched in
// this build, so they are zero placeholders.
static void *g_XLipFSRootVtable[8] =
    { 0, 0, 0, 0, 0, (void *)XLipFSRootIsInitDirValid, 0, (void *)XLipFSRootDtor };
static void *GetXLipFSRootVtable(void) { return g_XLipFSRootVtable; }

// sub_17D08C0 (0x17D08C0 - 0x17D090C) -- the XLipFSRoot ctor (thiscall +
// `retn 4`, Source on the stack): the IXLipRoot base ctor (sub_17D18A0,
// overwritten immediately), the path copy (sub_62A3D0 = strcpy_s(dst, 0x104,
// src)) into +0x0C, and the "XLIPFSRT" tag memcpy_s into +0x04.
// Returns the object.
static XLipFSRoot *XLipFSRootCtor(XLipFSRoot *obj, const char *source)
{
    obj->vtable = g_XLipFSRootVtable;
    strcpy_s(obj->path, 0x104, source);
    memcpy_s(obj->tag, 8, "XLIPFSRT", 8);
    return obj;
}

// InitLipFSRoot (sub_17BF110, 0x17BF110 - 0x17BF32D) -- bring up the
// XLipFSRoot (XLipComponent.cpp); defined below, after the vector / string
// helpers and the XLipFSRootVT / g_unk_1E2C664 globals it uses.
static int InitLipFSRoot(void *root);

// ============================================================
//  CreateLipFSRoot  (sub_17BEE80, 0x17BEE80 - 0x17BF102)  -- create the lip
//  file-system root (the 0x110-byte XLipFSRoot) for the given directory path
//  (XLipComponent.cpp) and bring it up; on success 1 is returned, on any
//  failure the root is torn down through its vtable slot 7 deleting
//  destructor and 0 is returned.
//  [NOT byte-identical] placement-dependent (relative calls to
//  NormalizeLipPath / LipMemAlloc / XLipFSRootCtor / InitLipFSRoot, the
//  vtable-slot-7 dispatch, and the IAT thunks _strlen / _strcpy_s /
//  __imp_SetLastError / __security_check_cookie) and reads the high-VA
//  init-flag global byte_1E2C6B4 (0x01E2C6B4).
//
//  int CreateLipFSRoot(const char *path)   -- original: cdecl, plain `retn`
//  (the original's parameter is a non-const char *; the value is only read)
//
//  Gated by the lip-runtime init flag byte_1E2C6B4 (SetLastError(0x15) and
//  return 0 when unset).  Asserts path != NULL (line 26, SetLastError(0x57)),
//  strlen(path) > 0 (line 37) and < 0x104 (line 38), falling out with
//  SetLastError(0xA1) when the gate fails; copies into a 0x104-byte stack
//  buffer with strcpy_s (line 52, SetLastError(0x54F) on failure),
//  normalizes it (NormalizeLipPath), LipMemAlloc's the 0x110-byte block
//  (sub_17D1840; NULL when the allocator fails), constructs the XLipFSRoot
//  in it (XLipFSRootCtor), and brings it up (InitLipFSRoot, sub_17BF110
//  [STUB]): 1 on success; when the ctor yields NULL (line 60,
//  SetLastError(0xE)) or the bring-up fails, vtable slot 7
//  (XLipFSRootDtor) frees the block and 0 is returned.  The original's SEH
//  frame frees the block (LipMemFree, sub_17D1860) when the body throws;
//  that path is unreachable in this build (the lip allocator never throws).
// ============================================================
static int CreateLipFSRoot(const char *path)
{
    char destination[0x104];

    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return 0;
    }
    if (path == NULL)
    {
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 26, L"pcszPath");
        SetLastError(0x57);
        return 0;
    }
    {
        size_t len = strlen(path);
        if (len == 0)
            XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 37, L"len > 0");
        if (len >= 0x104)
            XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 38, L"len < MAX_PATH");
        if (!(len > 0 && len < 0x104))
        {
            SetLastError(0xA1);
            return 0;
        }
        if (strcpy_s(destination, 0x104, path) != 0)
        {
            XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 52,
                       L"(ret==0) && \"failed to copy path to internal buffer\"");
            SetLastError(0x54F);
            return 0;
        }
    }
    (void)NormalizeLipPath(destination);          // sub_17C08D0
    void *block = LipMemAlloc(0x110);             // sub_17D1840
    XLipFSRoot *root = (block != NULL) ? XLipFSRootCtor((XLipFSRoot *)block, destination) : NULL;
    if (root == NULL)
    {
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 60, L"pRoot != NULL");
        SetLastError(0xE);
        return 0;
    }
    if (InitLipFSRoot(root) != 0)                 // sub_17BF110
        return 1;
    (*((XLipFSRootVfn **)root->vtable))[7](root, 1);   // vtable slot 7
    return 0;
}
// ============================================================
//  XLipComponent -- the 0x50-byte lip component object
//  (XLipComponent.cpp), registered by RegisterLipComponent into the global
//  registry vector (unk_1E2C69C):
//    +0x00  name (28-byte basic_string<char>, the F3AString layout)
//    +0x1C  path (28-byte basic_string<char>, the F3AString layout)
//    +0x38  subobject (24-byte vector-like, zeroed begin / end / alloc-end)
// ============================================================
typedef struct XLipComponent
{
    F3AString name;              // +0x00
    F3AString path;              // +0x1C
    char      subobject[0x18];   // +0x38 (task_continuation_context-like)
} XLipComponent;                 // 0x50 bytes total

// 0x1E2C69C -- the global lip-component registry vector (the 24-byte
// LipDwordVec of component pointers); grows as RegisterLipComponent pushes
// each registered component in.
static LipDwordVec g_unk_1E2C69C = { 0, 0, 0, NULL, NULL, NULL };

// sub_17C1BD0 (0x17C1BD0 - 0x17C1BFA) -- the vector capacity: (allocEnd -
// begin) / 4 (0 when begin is null).
static unsigned int LipCompRegCap(const LipDwordVec *v)
{
    return (v->begin != NULL)
           ? (unsigned int)((char *)v->allocEnd - (char *)v->begin) >> 2
           : 0;
}
// sub_17C1A60 (0x17C1A60 - 0x17C1B3B) -- reserve: grow the capacity to at
// least `n` elements, keeping the size (the length_error overflow guard is
// dead against the lip allocator).
static void LipCompRegReserve(LipDwordVec *v, unsigned int n)
{
    if (LipCompRegCap(v) >= n)
        return;
    unsigned int size = LipDwordVecSize(v);
    DWORD *nb = (DWORD *)LipMemAlloc(4 * n);
    for (unsigned int i = 0; i < size; i++)
        nb[i] = v->begin[i];
    if (v->begin != NULL)
        LipMemFree(v->begin);
    v->begin    = nb;
    v->end      = nb + size;
    v->allocEnd = nb + n;
}
// sub_17C1C10 (0x17C1C10 - 0x17C1C7C) -- push_back: grow the capacity when
// size == capacity, copy the element, and advance the end.
static void LipCompRegPush(LipDwordVec *v, DWORD value)
{
    unsigned int size = LipDwordVecSize(v);
    if (size >= LipCompRegCap(v))
    {
        unsigned int newCap = LipCompRegCap(v);
        newCap = (newCap < 4) ? 4 : newCap + (newCap >> 1);
        if (newCap <= size)
            newCap = size + 1;
        DWORD *nb = (DWORD *)LipMemAlloc(4 * newCap);
        for (unsigned int i = 0; i < size; i++)
            nb[i] = v->begin[i];
        if (v->begin != NULL)
            LipMemFree(v->begin);
        v->begin    = nb;
        v->allocEnd = nb + newCap;
    }
    v->begin[size] = value;
    v->end = v->begin + size + 1;
}

// The 28-byte lip basic_string<char> (the F3AString layout): the C string of
// the value (the SSO buffer when capacity < 16, else the heap pointer).
static const char *XLipStrCstr(const F3AString *s)
{
    return (s->cap < 0x10) ? s->u.buf : (char *)s->u.ptr;
}
// sub_17B8EF0 (0x17B8EF0 - 0x17B906E) -- the 28-byte basic_string<char>
// assign from a C string (thiscall + retn 4): the SSO buffer (capacity 15)
// when the length is < 16, else a LipMemAlloc'd heap buffer (capacity length
// + 1); the old heap buffer is freed when the SSO state changes.
static void XLipStrAssign(F3AString *s, const char *src)
{
    unsigned int len = (unsigned int)strlen(src);
    char *dst;
    if (len < 0x10)
    {
        if (s->cap >= 0x10)
            LipMemFree((void *)s->u.ptr);
        s->cap = 15;
        dst = s->u.buf;
    }
    else
    {
        void *nb = LipMemAlloc(len + 1);
        if (s->cap >= 0x10)
            LipMemFree((void *)s->u.ptr);
        s->u.ptr = (DWORD)nb;
        s->cap = len + 1;
        dst = (char *)s->u.ptr;
    }
    memcpy(dst, src, len + 1);
    s->len = len;
}

// sub_17C65E0 (0x17C65E0 - 0x17C6654) -- the 0x50-byte lip component ctor
// (thiscall + retn 8): assigns the name string (+0x00) from the original
// name and the path string (+0x1C) from the normalized path, then constructs
// the +0x38 subobject (sub_17C6720 -> sub_17C68E0, a 24-byte vector-like
// object, zeroed begin / end / alloc-end).  Returns the object.
static XLipComponent *XLipComponentCtor(XLipComponent *obj, const char *name, const char *path)
{
    memset(obj, 0, sizeof(*obj));
    XLipStrAssign(&obj->name, name);
    XLipStrAssign(&obj->path, path);
    return obj;
}
// sub_17C0B00 (0x17C0B00 - 0x17C0C0A) -- find the registered lip component
// whose name string equals `name` (iterating the registry vector and
// comparing each component's +0x00 name string via std::string::compare);
// the component pointer on a match, 0 otherwise.  Asserts name != NULL
// (line 866).
static void *FindLipComponentByName(const char *name)
{
    if (name == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 866, L"pcszName");
    if (name == NULL)
        return NULL;
    const LipDwordVec *reg = &g_unk_1E2C69C;
    for (unsigned int i = 0; i < LipDwordVecSize(reg); i++)
    {
        const XLipComponent *comp = (const XLipComponent *)reg->begin[i];
        if (strcmp(XLipStrCstr(&comp->name), name) == 0)
            return (void *)comp;
    }
    return NULL;
}
// sub_17C0BB0 (0x17C0BB0 - 0x17C0C59) -- find the registered lip component
// whose path string equals `path` (the +0x1C path string); the component
// pointer on a match, 0 otherwise.  Asserts path != NULL (line 879).
static void *FindLipComponentByPath(const char *path)
{
    if (path == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 879, L"pcszPath");
    if (path == NULL)
        return NULL;
    const LipDwordVec *reg = &g_unk_1E2C69C;
    for (unsigned int i = 0; i < LipDwordVecSize(reg); i++)
    {
        const XLipComponent *comp = (const XLipComponent *)reg->begin[i];
        if (strcmp(XLipStrCstr(&comp->path), path) == 0)
            return (void *)comp;
    }
    return NULL;
}
// XLipDebugMessageSink  (sub_17D0610, 0x17D0610 - 0x17D0615)  -- the lip debug-message sink: a 5-byte
// no-op (push ebp / mov ebp,esp / pop ebp / retn).  It receives a fully-formatted
// wide string (from XLipAssert / XLipDebugMessage / the cultures loader) and would
// emit it; in this build it discards the message, so lip debug output is never
// observable.  Byte-identical to the original.
static void XLipDebugMessageSink(const wchar_t *message) { (void)message; }
// XLipDebugMessage (0x17D0570 - 0x17D0602) -- the XLipDebug.cpp formatted
// message: assert pcwszFormat != NULL (line 65), vswprintf_s the varargs into a
// 1024-wchar buffer, then hand the buffer (or the raw format on a failed/zero
// format) to the no-op sink.  Returns the value the no-op sink leaves in EAX
// (the vswprintf_s count on success, the format pointer on failure); no caller
// uses it.
// [NOT byte-identical] placement-dependent (relative calls to XLipAssert /
// _vswprintf_s / the sink / the __security_check_cookie landing pad, plus the
// high-VA rdata refs to the assert file/line/condition strings).
static int XLipDebugMessage(const wchar_t *format, ...)
{
    va_list va;
    va_start(va, format);
    if (format == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileDebug, 65, L"pcwszFormat");
    wchar_t buffer[1024];
    int result = vswprintf_s(buffer, 1024, format, va);
    if (result <= 0) {
        XLipDebugMessageSink(format);       // emit the raw format (no-op)
        return (int)(size_t)format;         // EAX the sink leaves = the format ptr
    }
    XLipDebugMessageSink(buffer);           // emit the formatted buffer (no-op)
    return result;                          // EAX the sink leaves = the count
}

// ============================================================
//  RegisterLipComponent  (sub_17BF400, 0x17BF400 - 0x17BF76A)  -- register
//  a named lip component with a relative path (XLipComponent.cpp): validate
//  the name and the path, reject a duplicate name or path (ERROR_ALREADY_
//  EXISTS, 0xB7), normalize the path, construct the 0x50-byte component, and
//  push it into the global registry vector (unk_1E2C69C).
//  [NOT byte-identical] placement-dependent (relative calls to XLipAssert /
//  _strlen / FindLipComponentByName / XLipDebugMessage / _strcpy_s /
//  NormalizeLipPath / FindLipComponentByPath / j__strchr / j__strstr /
//  LipMemAlloc / XLipComponentCtor / the registry vector helpers, and the
//  IAT thunks __imp_SetLastError / __security_check_cookie) and refs the
//  high-VA init-flag global byte_1E2C6B4 (0x01E2C6B4), the registry vector
//  unk_1E2C69C (0x01E2C69C), the class-id vector dword_1E2C6B8 (0x01E2C6B8),
//  and the "The component with name ..." / "There is already a component
//  registered against: ..." rdata strings at their original VAs.
//
//  int RegisterLipComponent(const char *pcszName, const char *pcszPath)
//  -- original: cdecl, plain `retn` (the original's parameters are
//  non-const char *; the name value is only read)
//
//  Gated by the init flag byte_1E2C6B4 (SetLastError(0x15), return 0 when
//  unset).  Asserts pcszName != NULL (line 0xB5) and pcszPath != NULL
//  (line 0xB6), falling out with SetLastError(0x57) when either is null;
//  asserts strlen(pcszName) > 0 (line 0xC0), falling out with
//  SetLastError(0x57).  When the name is already registered
//  (FindLipComponentByName) it reports "The component with name \"%S\" is
//  already registered" and returns 0 (SetLastError 0xB7).  Asserts
//  strlen(pcszPath) > 0 (line 0xD5) and < MAX_PATH (line 0xDC), falling out
//  with SetLastError(0xA1); copies the path into a 0x104-byte buffer
//  (_strcpy_s), normalizes it (NormalizeLipPath), and, when the normalized
//  path is already registered (FindLipComponentByPath), reports "There is
//  already a component registered against: %S" and returns 0 (SetLastError
//  0xB7).  When the path has a '*' wildcard it must also contain "*."
//  (SetLastError(0xA1), return 0 otherwise).  On success it LipMemAlloc's the
//  0x50-byte component (XLipComponentCtor with the original name + the
//  normalized path; a null ctor yields SetLastError(0xE), return 0), seeds
//  the registry capacity from the class-id vector element 3 when empty
//  (LipCompRegReserve), pushes the component (LipCompRegPush), and returns 1.
// ============================================================
static int RegisterLipComponent(const char *pcszName, const char *pcszPath)
{
    char path[0x104];

    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return 0;
    }
    if (pcszName == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 0xB5, L"pcszName");
    if (pcszPath == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 0xB6, L"pcszPath");
    if (pcszName == NULL || pcszPath == NULL)
    {
        SetLastError(0x57);
        return 0;
    }
    {
        int nameLen = (int)strlen(pcszName);
        if (nameLen <= 0)
            XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 0xC0,
                       L"len > 0 && \"component name can't be an empty string\"");
        if (nameLen <= 0)
        {
            SetLastError(0x57);
            return 0;
        }
    }
    if (FindLipComponentByName(pcszName) != NULL)
    {
        XLipDebugMessage(L"The component with name \"%S\" is already registered", pcszName);
        SetLastError(0xB7);
        return 0;
    }
    {
        int pathLen = (int)strlen(pcszPath);
        if (pathLen <= 0)
            XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 0xD5,
                       L"len > 0 && \"component relative path can't be an empty string\"");
        if (pathLen <= 0)
        {
            SetLastError(0xA1);
            return 0;
        }
        if (pathLen >= 260)
            XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 0xDC,
                       L"len < MAX_PATH && \"component relative path length should be < MAX_PATH\"");
        if (pathLen >= 260)
        {
            SetLastError(0xA1);
            return 0;
        }
    }
    (void)strcpy_s(path, 0x104, pcszPath);
    (void)NormalizeLipPath(path);
    if (FindLipComponentByPath(path) != NULL)
    {
        XLipDebugMessage(L"There is already a component registered against: %S", path);
        SetLastError(0xB7);
        return 0;
    }
    if (!(strchr(path, '*') == NULL || strstr(path, "*.") != NULL))
    {
        SetLastError(0xA1);
        return 0;
    }
    void *block = LipMemAlloc(0x50);
    XLipComponent *comp =
        (block != NULL) ? XLipComponentCtor((XLipComponent *)block, pcszName, path) : NULL;
    if (comp == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 0xFC, L"pComponent != NULL");
    if (comp == NULL)
    {
        SetLastError(0xE);
        return 0;
    }
    if (LipCompRegCap(&g_unk_1E2C69C) == 0)
        LipCompRegReserve(&g_unk_1E2C69C, *LipDwordVecAt(&g_unk_1E2C6B8, 3));
    LipCompRegPush(&g_unk_1E2C69C, (DWORD)comp);
    return 1;
}
// ============================================================
//  0x1E2C5C4 -- the previous-session registered-language list (XLipSettings):
//  a 28-byte object (an untouched DWORD header at +0x00, a 24-byte vector of
//  28-byte char-string pointers at +0x04), freed at the start of every
//  XLipInitUILanguages.  Empty in this build.
typedef struct XlipOldLanguageList
{
    DWORD       header;           // +0x00 (untouched)
    LipDwordVec languages;        // +0x04 (28-byte char strings)
} XlipOldLanguageList;
static XlipOldLanguageList g_unk_1E2C5C4 = { 0, { 0, 0, 0, NULL, NULL, NULL } };
// 0x1E2C5C8 -- the registered UI-language names (24-byte vector of 28-byte
// char-string pointers allocated through LHAlloc by XLipAddLanguage).
static LipDwordVec g_unk_1E2C5C8 = { 0, 0, 0, NULL, NULL, NULL };
// 0x1E2C664 -- the registered lip file-system roots (24-byte vector of
// XLipFSRoot pointers); XLipBringUpComponents iterates it in both passes.
//  Empty in this build (InitLipFSRoot is implemented but unreached, since
//  CreateLipFSRoot falls out on the empty settings path before registering).
static LipDwordVec g_unk_1E2C664 = { 0, 0, 0, NULL, NULL, NULL };
// 0x1E2C67C -- the in-flight PPL parallel culture-task callbacks (24-byte
// vector of CancellationTokenCallback pointers), cancelled / cleared by
// XLipBringUpComponents (sub_17C6B20).  Empty in this build.
static LipDwordVec g_unk_1E2C67C = { 0, 0, 0, NULL, NULL, NULL };
// 0x1E2C680 -- the registered cultures (24-byte vector of 60-byte
// registered-culture objects: a 28-byte name string at +0x00, a DWORD packed
// version ((major << 16) | minor) at +0x1C, a DWORD root index at +0x20, 24
// PPL task bytes at +0x24).  Populated on demand by XLipGetCultureId; empty
// in this build (pass 1 never runs).
typedef struct XLipRegisteredCulture
{
    F3AString name;               // +0x00
    DWORD     version;            // +0x1C (packed major / minor)
    DWORD     rootIndex;          // +0x20
    char      task[0x18];         // +0x24 (PPL task / token context)
} XLipRegisteredCulture;          // 0x3C bytes
static LipDwordVec g_unk_1E2C680 = { 0, 0, 0, NULL, NULL, NULL };
// the XLipFSRoot vtable (0x1BC446C) as dispatched by
// XLipBringUpComponents.  The project's g_XLipFSRootVtable keeps slots 0..6
// as zero placeholders (no roots are registered in this build, so none of
// these ever fires).
typedef struct XLipFSRootVT
{
    void *vt0;                                                            // +0x00
    int  (*getCultureName)(void *root, unsigned int index, char *name,
                           unsigned int cchName);                         // +0x04  sub_17D0B50
    int  (*hasCultureDir)(void *root, const char *path, const char *cultureName); // +0x08 sub_17D0F80
    void *vt3;                                                            // +0x0C
    int  (*readCultureVersion)(void *root, const char *cultureName,
                               unsigned short *major, unsigned short *minor); // +0x10 sub_17D14A0
    int  (*isInitDirValid)(void *root);                                   // +0x14  sub_17D1680
    void *vt6;                                                            // +0x18
    void *vt7;                                                            // +0x1C
} XLipFSRootVT;

// ---- 24-byte vector element helpers (4-byte and 12-byte elements) ----
// reserve `n` 4-byte elements (sub_17B3FD0 / sub_17C1500 semantics): when
// the current capacity (4-byte elements) is smaller, allocate exactly 4*n
// bytes through LipMemAlloc, move the existing elements, free the old block
// and rewrite the begin / end / allocEnd pointers.
static void XLipVecReserve4(LipDwordVec *v, unsigned int n)
{
    unsigned int cap = (v->begin != NULL) ? (unsigned int)(v->allocEnd - v->begin) : 0;
    if (cap >= n)
        return;
    {
        DWORD *nb = (DWORD *)LipMemAlloc(4 * n);
        unsigned int size;
        if (nb == NULL)
            return;
        size = LipDwordVecSize(v);
        if (size != 0 && v->begin != NULL)
        {
            memcpy(nb, v->begin, 4 * size);
            LipMemFree(v->begin);
        }
        v->begin = nb;
        v->end   = nb + size;
        v->allocEnd = nb + n;
    }
}
// reserve `n` 12-byte elements (sub_17C1760): allocate 12*n bytes, move the
// existing 12-byte elements, free the old block.
static void XLipVecReserve12(LipDwordVec *v, unsigned int n)
{
    unsigned int cap = (v->begin != NULL) ? (unsigned int)(v->allocEnd - v->begin) / 3 : 0;
    if (cap >= n)
        return;
    {
        DWORD *nb = (DWORD *)LipMemAlloc(12 * n);
        unsigned int size;
        if (nb == NULL)
            return;
        size = (unsigned int)(v->end - v->begin) / 3;
        if (size != 0 && v->begin != NULL)
        {
            memcpy(nb, v->begin, 12 * size);
            LipMemFree(v->begin);
        }
        v->begin = nb;
        v->end   = nb + 3 * size;
        v->allocEnd = nb + 3 * n;
    }
}
// push a 4-byte value, growing the vector when full (sub_17B9010 /
// sub_17C16E0 semantics).
static void XLipVecPush4(LipDwordVec *v, DWORD value)
{
    unsigned int size;
    if (v->begin == NULL)
        XLipVecReserve4(v, 1);
    size = LipDwordVecSize(v);
    if (size >= (unsigned int)(v->allocEnd - v->begin))
        XLipVecReserve4(v, 1 + 2 * size);
    *v->end = value;
    v->end += 1;
}
// append a 4-byte value only when it is not already present (sub_17B85D0,
// the addIfAbsent helper over a 4-byte-element vector).
static void XLipVecAddIfAbsent(LipDwordVec *v, DWORD value)
{
    unsigned int size = LipDwordVecSize(v);
    unsigned int i;
    for (i = 0; i < size; i++)
    {
        if (v->begin[i] == value)
            return;
    }
    XLipVecPush4(v, value);
}
// push a 12-byte value, growing the vector when full (sub_17C1980).
static void XLipVecPush12(LipDwordVec *v, const DWORD *src)
{
    unsigned int size;
    if (v->begin == NULL)
        XLipVecReserve12(v, 1);
    size = (unsigned int)(v->end - v->begin) / 3;
    if (size >= (unsigned int)(v->allocEnd - v->begin) / 3)
        XLipVecReserve12(v, 1 + 2 * size);
    memcpy(v->end, src, 12);
    v->end += 3;
}
// erase-all (sub_17B9090 / sub_17C1A00): reset end to begin (the elements
// are POD -- no destructors).
static void XLipVecClear(LipDwordVec *v)
{
    v->end = v->begin;
}

// ============================================================
//  XLipFSRoot vtable slot 5  (sub_17D1680, 0x17D1680 - 0x17D16EF)  -- the
//  init-directory validity check (XLipComponent.cpp): the root's path string
//  (root + 0x0C) must be non-empty and name an existing directory.
//  [NOT byte-identical] placement-dependent (relative calls to the IAT
//  thunks GetFileAttributesA / __imp_SetLastError / __security_check_cookie).
//  int XLipFSRootIsInitDirValid(void *root)   -- original: thiscall, `retn`
//  1 when the path is an existing directory; 0 when it is empty
//  (SetLastError 0x139F) or not a directory (SetLastError 0x7B); when the
//  attribute query fails the returned GetLastError is preserved and 0.
// ============================================================
static int XLipFSRootIsInitDirValid(void *rootObj)
{
    const XLipFSRoot *root = (const XLipFSRoot *)rootObj;
    if (root->path[0] == 0)
    {
        SetLastError(0x139F);
        return 0;
    }
    DWORD attr = GetFileAttributesA(root->path);
    if (attr == (DWORD)-1)
        return 0;
    if ((attr & 0x10) != 0)                 // FILE_ATTRIBUTE_DIRECTORY
        return 1;
    SetLastError(0x7B);
    return 0;
}
// sub_17C13C0 (0x17C13C0 - 0x17C13E8) -- the 4-byte-element vector capacity:
// (allocEnd - begin) / 4 (0 when begin is null).
static unsigned int XLipVecCap4(const LipDwordVec *v)
{
    return (v->begin != NULL) ? (unsigned int)(v->allocEnd - v->begin) : 0;
}
// sub_17C0A10 (0x17C0A10 - 0x17C0A3F) -- find the XLipFSRoot already present
// in the registered-roots vector (g_unk_1E2C664); the stored element (== the
// root pointer) on a hit, 0 otherwise (sub_17C0A50 linear scan).
static void *XLipFindRoot(const void *root)
{
    const LipDwordVec *v = &g_unk_1E2C664;
    unsigned int i;
    for (i = 0; i < LipDwordVecSize(v); i++)
        if (v->begin[i] == (DWORD)root)
            return (void *)v->begin[i];
    return 0;
}
// grow a 28-byte lip basic_string<char> to hold at least `newLen` chars and
// return its writable buffer: the SSO buffer (capacity 15) when newLen < 16,
// else a LipMemAlloc'd heap buffer; the existing characters are moved.
static char *XLipStrEnsure(F3AString *s, unsigned int newLen)
{
    const char *old = XLipStrCstr(s);
    char *buf;
    if (newLen >= 0x10)
    {
        void *nb = LipMemAlloc(newLen + 1);
        buf = (char *)nb;
        memcpy(buf, old, s->len);
        if (s->cap >= 0x10)
            LipMemFree((void *)s->u.ptr);
        s->u.ptr = (DWORD)nb;
        s->cap = newLen + 1;
    }
    else
    {
        buf = s->u.buf;
        s->cap = 15;
    }
    return buf;
}
// ?append@basic_string... (0x17BF223) -- append a NUL-terminated C string to
// a 28-byte lip basic_string<char>.
static void XLipStrAppendCstr(F3AString *s, const char *src)
{
    unsigned int add = (unsigned int)strlen(src);
    char *buf = XLipStrEnsure(s, s->len + add);
    memcpy(buf + s->len, src, add + 1);
    s->len = s->len + add;
}
// sub_17C1190 (0x17C1190 - 0x17C11A8) -- append one 28-byte lip
// basic_string<char> (the whole source) to another (sub_17C2070 with pos 0,
// count -1).
static void XLipStrAppendStr(F3AString *s, const F3AString *src)
{
    unsigned int add = src->len;
    const char *c = XLipStrCstr(src);
    char *buf = XLipStrEnsure(s, s->len + add);
    memcpy(buf + s->len, c, add);
    buf[s->len + add] = 0;
    s->len = s->len + add;
}
// sub_17C1200 (0x17C1200 - 0x17C1240) -- dst = src[pos, count) (count -1 = to
// the end): clears dst (sub_17B9270) then appends the substring
// (sub_17B9DD0).
static F3AString *XLipStrSubstr(F3AString *dst, const F3AString *src,
                                unsigned int pos, int count)
{
    unsigned int n;
    const char *c;
    if (dst->cap >= 0x10)
        LipMemFree((void *)dst->u.ptr);
    dst->cap = 15;
    dst->len = 0;
    dst->u.buf[0] = 0;
    if (pos >= src->len)
        n = 0;
    else
    {
        unsigned int rem = src->len - pos;
        n = (count < 0) ? rem : (((unsigned int)count < rem) ? (unsigned int)count : rem);
    }
    c = XLipStrCstr(src) + pos;
    if (n >= 0x10)
    {
        void *nb = LipMemAlloc(n + 1);
        memcpy((char *)nb, c, n);
        ((char *)nb)[n] = 0;
        dst->u.ptr = (DWORD)nb;
        dst->cap = n + 1;
    }
    else
    {
        memcpy(dst->u.buf, c, n);
        dst->u.buf[n] = 0;
    }
    dst->len = n;
    return dst;
}
// sub_17C11E0 (0x17C11E0 - 0x17C11F8) -- rfind a single char in a 28-byte lip
// basic_string<char> (sub_17C2210 with pos -1); the last matching index, -1
// when absent.
static int XLipStrRFind(const F3AString *s, char ch)
{
    const char *c = XLipStrCstr(s);
    int i = (int)s->len - 1;
    for (; i >= 0; i--)
        if (c[i] == ch)
            return i;
    return -1;
}

// ============================================================
//  InitLipFSRoot  (sub_17BF110, 0x17BF110 - 0x17BF32D)  -- bring up the
//  XLipFSRoot (XLipComponent.cpp), the routine CreateLipFSRoot calls after
//  constructing the 0x110-byte root object.
//  [NOT byte-identical] placement-dependent (relative calls to XLipAssert /
//  XLipFindRoot / the vtable-slot-5 dispatch / XLipVecReserve4 / XLipVecPush4
//  / the string helpers / XLipLoadCultures, the IAT thunks
//  __imp_SetLastError / __imp_GetLastError / __security_check_cookie, and the
//  SEH ___CxxFrameHandler3_0 landing pad) and refs the high-VA globals
//  byte_1E2C6B4 (0x01E2C6B4), unk_1E2C6B8 (0x01E2C6B8), unk_1E2C664
//  (0x01E2C664) and dword_1E2C6D0 (0x01E2C6D0) at their original VAs.
//
//  int InitLipFSRoot(void *root)   -- original: cdecl, plain `retn`
//
//  Gated by the init flag byte_1E2C6B4 (SetLastError(0x15), return 0 when
//  unset).  Asserts pRoot != NULL (line 84, SetLastError(0x57)).  When the
//  root is already in the registered-roots vector (XLipFindRoot, sub_17C0A10)
//  it returns 1; when vtable slot 5 (XLipFSRootIsInitDirValid, sub_17D1680)
//  says the directory is unusable it returns 0.  Otherwise it seeds the vector
//  capacity from class-id element 2 (XLipVecReserve4, sub_17C1250) when empty,
//  pushes the root (XLipVecPush4, sub_17C1450), builds the culture-file path
//  as  root->path + "\" + (the culture-directory string in the default-params
//  object after its last backslash, or the whole string when it has none),
//  and, when that path is <= 0x104 chars, loads it (XLipLoadCultures): 1 on
//  success (including a missing file, ERROR_FILE_NOT_FOUND 2 /
//  ERROR_PATH_NOT_FOUND 3), 0 otherwise; when the path is too long it sets
//  0xA1 and returns 0.
// ============================================================
static int InitLipFSRoot(void *rootObj)
{
    XLipFSRoot *root = (XLipFSRoot *)rootObj;
    F3AString *paramsStr = (F3AString *)&g_dword_1E2C6D0;
    F3AString path;
    int found;
    int rc;

    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return 0;
    }
    if (root == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 84, L"pRoot");
    if (root == NULL)
    {
        SetLastError(0x57);
        return 0;
    }
    if (XLipFindRoot(root) != 0)
        return 1;
    if (((const XLipFSRootVT *)root->vtable)->isInitDirValid(root) == 0)
        return 0;
    if (XLipVecCap4(&g_unk_1E2C664) == 0)
        XLipVecReserve4(&g_unk_1E2C664, *LipDwordVecAt(&g_unk_1E2C6B8, 2));
    XLipVecPush4(&g_unk_1E2C664, (DWORD)root);

    F3AStringReset(&path);
    found = XLipStrRFind(paramsStr, '\\');
    XLipStrAssign(&path, root->path);
    XLipStrAppendCstr(&path, "\\");
    if (found < 0)
    {
        XLipStrAppendStr(&path, paramsStr);
    }
    else
    {
        F3AString sub;
        F3AStringReset(&sub);
        XLipStrSubstr(&sub, paramsStr, (unsigned int)(found + 1), -1);
        XLipStrAppendStr(&path, &sub);
        XLipStrDtor(&sub);
    }
    if (path.len <= 0x104)
    {
        rc = XLipLoadCultures(XLipStrCstr(&path));
        {
            int lastErr = GetLastError();
            int ok = (rc >= 0 || lastErr == 2 || lastErr == 3);
            XLipStrDtor(&path);
            return ok ? 1 : 0;
        }
    }
    SetLastError(0xA1);
    XLipStrDtor(&path);
    return 0;
}
// sub_17CD070 (0x17CD070 - 0x17CD149) -- look up the culture definition
// (an XLipCultureInfo from the parsed cultures file) by lowercased name:
// assert non-null (XLipCultures.cpp line 448), scan the definition list
// (g_unk_1E2C6D4) comparing each entry's name string, return the entry on
// an exact match, otherwise log "The culture %S could not be found on the
// culture vector" and return NULL.  cdecl, one arg.
static XLipCultureInfo *XLipFindCultureDef(const char *pcszCulture)
{
    LipDwordVec *list;
    unsigned int i;
    if (pcszCulture == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 448, L"pcszCulture");
    list = LipCultureListVec();
    for (i = 0; i < LipCultureListSize(list); i++)
    {
        const XLipCultureInfo *info = (const XLipCultureInfo *)list->begin[i];
        if (strcmp(XLipStrCstr(&info->name), pcszCulture) == 0)
            return (XLipCultureInfo *)info;
    }
    XLipDebugMessage(L"The culture %S could not be found on the culture vector",
                     pcszCulture);
    return NULL;
}
// sub_17C0D90 (0x17C0D90 - 0x17C0E08) -- look up the 0-based index of a
// culture name in the registered-culture table (g_unk_1E2C680), scanning
// each 60-byte object's name string; -1 when not present.  The name is
// compared exactly (both sides are lowercased at the call sites).  cdecl,
// one arg.
static int XLipFindRegisteredCulture(const char *pcszCulture)
{
    unsigned int i;
    if (pcszCulture == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 933, L"pcszCulture");
    for (i = 0; i < LipDwordVecSize(&g_unk_1E2C680); i++)
    {
        const XLipRegisteredCulture *reg =
            (const XLipRegisteredCulture *)g_unk_1E2C680.begin[i];
        if (strcmp(XLipStrCstr(&reg->name), pcszCulture) == 0)
            return (int)i;
    }
    return -1;
}
// sub_17CC4F0 (0x17CC4F0 - 0x17CD06D) -- fetch one field of a culture
// definition (XLipCultures.cpp):
//    type 1 -- the language name (the wide language string as UTF-16,
//              size = 2 * len + 2)
//    type 2 -- the source / parent name (narrow; -1 + SetLastError 0x10D2
//              when the source is "null", i.e. a top-level culture)
//    type 3 -- the defined major version (WORD, size 2)
//  Gate on the init flag (SetLastError 0x15), assert the name is non-null
//  and non-empty (line 92) and the buffer consistent (line 93), lowercase a
//  copy of the name (strcpy_s / _strlwr_s, line-115 asserts), resolve it
//  through XLipFindCultureDef (SetLastError 0x490 on a miss), copy `size`
//  bytes into pbBuffer when one was given (SetLastError 0x7A when the buffer
//  is too small), and return the size.  cdecl, four args.
static int XLipGetCultureInfo(const char *pcszCultureName, unsigned int type,
                              void *pbBuffer, unsigned int cbBuffer)
{
    char name[85];
    const char *src;
    unsigned int size;
    XLipCultureInfo *info;
    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return -1;
    }
    if (pcszCultureName == NULL || pcszCultureName[0] == 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 92,
                   L"pcszCultureName && pcszCultureName[0] && \"Parameter can't be NULL\"");
    if ((pbBuffer != NULL || cbBuffer != 0)
        && (pbBuffer == NULL || cbBuffer <= 0))
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 93,
                   L"((!pbBuffer && cbBuffer==0) || (pbBuffer && cbBuffer > 0)) && \"Buffer length must consistent\"");
    if (pcszCultureName == NULL || pcszCultureName[0] == 0
        || (pbBuffer != NULL && cbBuffer < 1) || (pbBuffer == NULL && cbBuffer != 0))
    {
        SetLastError(0x57);
        return -1;
    }
    if (strcpy_s(name, 0x55, pcszCultureName) != 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 115, L"ret==0");
    if (_strlwr_s(name, 0x55) != 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 115, L"ret==0");
    info = XLipFindCultureDef(name);
    if (info == NULL)
    {
        SetLastError(0x490);
        return -1;
    }
    switch (type)
    {
    case 1:
        src = (info->language.cap < 8)
              ? (const char *)info->language.u.buf
              : (const char *)info->language.u.ptr;
        size = 2 * info->language.len + 2;
        break;
    case 2:
        if (strcmp(XLipStrCstr(&info->source), "null") == 0)
        {
            SetLastError(0x10D2);
            return -1;
        }
        src = XLipStrCstr(&info->source);
        size = (unsigned int)strlen(src) + 1;
        break;
    case 3:
        src = (const char *)&info->version;
        size = 2;
        break;
    default:
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 160,
                   L"false && \"Unknown XLIPCULTUREINFOTYPE specified\"");
        SetLastError(0x57);
        return -1;
    }
    if (pbBuffer != NULL)
    {
        if (cbBuffer < size)
        {
            XLipDebugMessageSink(L"buffer too small");
            SetLastError(0x7A);
            return -1;
        }
        memcpy(pbBuffer, src, size);
    }
    return (int)size;
}
// sub_17C0C60 (0x17C0C60 - 0x17C0DAF) -- resolve a (lowercased) culture or
// language name to its 0-based index in the registered-culture table
// (XLipComponent.cpp): assert non-null (line 906), reserve 5 elements when
// the table is still empty (the class-id vector element 5), and: when the
// name is a known culture definition -- return its registered index, or
// register it first: LipMemAlloc a 60-byte registered-culture object,
// assign its name string, zero its version / root-index pair, push the
// pointer into g_unk_1E2C680, and return the new (count - 1) index.  When
// the name is not a supported culture at all, log 'The language "%S" is not
// a supported culture' and return -1.  cdecl, one arg.
static int XLipGetCultureId(const char *pcszLanguage)
{
    int j;
    if (pcszLanguage == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 906, L"pcszLanguage");
    if (g_unk_1E2C680.begin == NULL)
        XLipVecReserve4(&g_unk_1E2C680, *LipDwordVecAt(&g_unk_1E2C6B8, 5));
    if (XLipFindCultureDef(pcszLanguage) == NULL)
    {
        XLipDebugMessage(L"The language \"%S\" is not a supported culture",
                         pcszLanguage);
        return -1;
    }
    j = XLipFindRegisteredCulture(pcszLanguage);
    if (j >= 0)
        return j;
    {
        DWORD block = (DWORD)LipMemAlloc(sizeof(XLipRegisteredCulture));
        if (block != NULL)
        {
            XLipRegisteredCulture *reg = (XLipRegisteredCulture *)block;
            XLipStrReset(&reg->name);
            XLipStrAssign(&reg->name, pcszLanguage);
            reg->version = 0;
            reg->rootIndex = 0;
        }
        XLipVecPush4(&g_unk_1E2C680, block);      // sub_17C16E0
    }
    return (int)LipDwordVecSize(&g_unk_1E2C680) - 1;
}
// sub_17B3570 (0x17B3570 - 0x17B3688) -- register a UI language (an already
// lowercased narrow name) into the registered-language vector
// (g_unk_1E2C5C8): LHAlloc a 28-byte string, assign the name, push the
// pointer, then follow the culture-definition source chain -- when the name
// resolves to a culture whose source is not "null" and is not already
// registered, recurse on the source name.  cdecl, two args (the second, a
// procedure pointer in the original, is unused here).
static void XLipAddLanguage(const char *pcszLanguage, void *procAddress)
{
    LipDwordVec *list = &g_unk_1E2C5C8;
    const XLipCultureInfo *info;
    unsigned int i;
    (void)procAddress;
    for (i = 0; i < LipDwordVecSize(list); i++)
    {
        if (strcmp(XLipStrCstr((const F3AString *)list->begin[i]),
                   pcszLanguage) == 0)
            return;                     // already registered
    }
    {
        F3AString *s = (F3AString *)LHAlloc(0x1C);
        if (s == NULL)
            return;
        XLipStrReset(s);
        XLipStrAssign(s, pcszLanguage);
        XLipVecPush4(list, (DWORD)s);
    }
    info = XLipFindCultureDef(pcszLanguage);
    if (info != NULL)
    {
        const char *source = XLipStrCstr(&info->source);
        int haveSource = 0;
        if (strcmp(source, "null") != 0)
        {
            for (i = 0; i < LipDwordVecSize(list); i++)
            {
                if (strcmp(XLipStrCstr((const F3AString *)list->begin[i]),
                           source) == 0)
                {
                    haveSource = 1;
                    break;
                }
            }
            if (!haveSource)
                XLipAddLanguage(source, procAddress);   // follow the chain
        }
    }
}
// sub_17B9760 (0x17B9760 - 0x17B97B8) -- free and clear the old registered-
// language list (the vector inside g_unk_1E2C5C4 at +0x04): run the 28-byte
// string dtor on each element (XLipStrDtor) and LHFree the block when the
// freeIt flag is set, then reset end to begin.
static void XLipClearLanguageList(int freeIt)
{
    LipDwordVec *v = &g_unk_1E2C5C4.languages;
    unsigned int i;
    for (i = 0; i < LipDwordVecSize(v); i++)
    {
        F3AString *s = (F3AString *)v->begin[i];
        if (s != NULL && freeIt != 0)
        {
            XLipStrDtor(s);
            LHFree(s);
        }
    }
    XLipVecClear(v);
}
// sub_17B3690 (0x17B3690 - 0x17B3950) -- the Vista+ UI-language enumeration
// (XLipSettings.cpp): resolve GetThreadPreferredUILanguages from the given
// kernel32 handle (0 when absent), call it with dwFlags 8 into a 595-wide-
// char buffer, walk the NUL-separated language list, convert each entry to
// a lowercase narrow name (WideCharToMultiByte into an 85-byte buffer with
// the line-616 assert, _strlwr_s with the line-621 assert) and register it
// through XLipAddLanguage.  Returns 1 when the enumeration ran, 0 when the
// function pointer is missing or the call failed.
static int XLipRegisterThreadUILanguages(HMODULE hModule)
{
    typedef int (WINAPI *GetThreadPreferredUILanguagesFn)(DWORD, LPDWORD,
                                                          LPWSTR, LPDWORD);
    WCHAR langBuf[595];
    DWORD cchBuffer = 595;
    DWORD numLanguages = 0;
    GetThreadPreferredUILanguagesFn getLangs;
    const WCHAR *p;

    getLangs = (GetThreadPreferredUILanguagesFn)GetProcAddress(
        hModule, "GetThreadPreferredUILanguages");
    if (getLangs == NULL)
        return 0;
    if (getLangs(8, &numLanguages, langBuf, &cchBuffer) == 0)
        return 0;
    for (p = langBuf; *p != 0; p += wcslen(p) + 1)
    {
        char mb[85];
        int iRet;
        memset(mb, 0, 85);
        iRet = WideCharToMultiByte(0, 0, p, (int)wcslen(p), mb, 85, NULL, NULL);
        if (iRet <= 0)
            XLipAssert(g_xlipAssertFailed, g_xlipFileXLipX, 616, L"iRet > 0");
        if (iRet > 0)
        {
            if (_strlwr_s(mb, 0x55) != 0)
                XLipAssert(g_xlipAssertFailed, g_xlipFileXLipX, 621, L"errret == 0");
            XLipAddLanguage(mb, getLangs);
        }
    }
    return 1;
}
// sub_17B3950 (0x17B3950 - 0x17B3A50) -- the pre-Vista UI-language fallback
// (XLipSettings.cpp): resolve GetUserDefaultUILanguage (0 when absent),
// look the resulting language id up in the two 32-byte XLip maps
// (sub_17B3860 on g_unk_1E2C640 and sub_17B38D0 on g_unk_1E2C620) and
// register each string found.  Both maps are constructed empty in this
// build and never receive insertions, so both lookups always miss and the
// function returns 0 without registering anything.
static int XLipRegisterUserUILanguages(HMODULE hModule)
{
    typedef WORD (WINAPI *GetUserDefaultUILanguageFn)(void);
    GetUserDefaultUILanguageFn getUserLang = (GetUserDefaultUILanguageFn)
        GetProcAddress(hModule, "GetUserDefaultUILanguage");
    if (getUserLang == NULL)
        return 0;
    // (sub_17B3860 / sub_17B38D0 map lookups: the maps are empty in this
    //  build, so no user language string is found.)
    return 0;
}
// sub_17B3A50 (0x17B3A50 - 0x17B3BB6) -- the UI-language settings init
// (XLipSettings.cpp): reserve 7 on the registered-language vector
// (g_unk_1E2C5C8), free and clear the old list (g_unk_1E2C5C4), load
// kernel32 (XLipAssert at line 717 when that fails), and -- when
// GetThreadPreferredUILanguages is available -- register each thread
// preferred UI language (XLipRegisterThreadUILanguages), otherwise the
// pre-Vista fallback (XLipRegisterUserUILanguages); then always register
// "en-uk" as the final fallback language.  cdecl, no args.
static void XLipInitUILanguages(void)
{
    HMODULE hModule;
    XLipVecReserve4(&g_unk_1E2C5C8, 7);
    XLipClearLanguageList(1);
    hModule = LoadLibraryW(L"kernel32.dll");
    if (hModule == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileXLipX, 717, L"hKernel32");
    if (hModule != NULL)
    {
        if (GetProcAddress(hModule, "GetThreadPreferredUILanguages") != NULL)
            (void)XLipRegisterThreadUILanguages(hModule);
        else
            (void)XLipRegisterUserUILanguages(hModule);
    }
    XLipAddLanguage("en-uk", NULL);
}
// sub_17B3B00 (0x17B3B00 - 0x17B3CC0) -- rebuild one component's
// culture-index list (the 24-byte vector at +0x38 of the XLipComponent,
// reinterpreted here as a 4-byte-element vector): reserve the
// registered-culture count (g_unk_1E2C680), clear the vector, then -- after
// the g_ComponentFallbacks chain (an empty map in this build, dead here) --
// for each registered UI language in g_unk_1E2C5C8 add its registered
// culture index when found, and finally add every index 0 .. count-1.
// The add is sub_17B85D0: a linear scan that appends only when the index is
// not already present.  thiscall component + one stack arg in the original.
static void XLipUpdateComponentCultureList(XLipComponent *comp)
{
    LipDwordVec *vec = (LipDwordVec *)comp->subobject;    // +0x38
    unsigned int count;
    unsigned int i;
    count = LipDwordVecSize(&g_unk_1E2C680);
    XLipVecReserve4(vec, count);        // sub_17B3FD0
    XLipVecClear(vec);                  // sub_17B9090
    for (i = 0; i < LipDwordVecSize(&g_unk_1E2C5C8); i++)
    {
        int j = XLipFindRegisteredCulture(
            XLipStrCstr((const F3AString *)g_unk_1E2C5C8.begin[i]));
        if (j >= 0)
            XLipVecAddIfAbsent(vec, (DWORD)j);   // sub_17B85D0
    }
    for (i = 0; i < count; i++)
        XLipVecAddIfAbsent(vec, i);              // sub_17B85D0
}
// sub_17B3CC0 (0x17B3CC0 - 0x17B3D18) -- run
// XLipUpdateComponentCultureList over every registered lip component
// (g_unk_1E2C69C).  cdecl, no args.
static void XLipUpdateComponentCultureLists(void)
{
    unsigned int i;
    for (i = 0; i < LipDwordVecSize(&g_unk_1E2C69C); i++)
        XLipUpdateComponentCultureList(
            (XLipComponent *)g_unk_1E2C69C.begin[i]);
}
// sub_17C6B20 (0x17C6B20 - 0x17C6DB0) -- cancel and clear the in-flight PPL
// culture-task callbacks (the 24-byte vector in ecx): for each element,
// when the freeIt flag is set and the element is non-null, run the
// CancellationTokenCallback dtor and LipMemFree the block, then reset end to
// begin.  thiscall, one stack arg (freeIt).  Empty in this build.
static void XLipClearTaskVector(LipDwordVec *v, int freeIt)
{
    unsigned int i;
    for (i = 0; i < LipDwordVecSize(v); i++)
    {
        void *cb = (void *)v->begin[i];
        if (cb != NULL && (freeIt & 1))
            LipMemFree(cb);
    }
    XLipVecClear(v);
}
// sub_17BF770 (0x17BF770 - 0x17BFC20) -- XLip component bring-up
// (XLipComponent.cpp), the routine that makes the registered lip components
// culture-ready, called from the engine after the seven RegisterLipComponent
// calls (and again from the component-settings validator and the culture
// switcher).  [NOT byte-identical] -- placement-dependent (relative calls
// through the PPL template / iterator inlines, the IAT jump thunks and the
// high-VA globals below); reimplemented for behavioral equivalence:
//  1. Gate on the init flag: zero -- SetLastError(0x15) and return;
//     otherwise SetLastError(0) and cancel the in-flight parallel culture
//     tasks (XLipClearTaskVector on g_unk_1E2C67C, sub_17C6B20).
//  2. Pass 1, over the registered roots (g_unk_1E2C664): skip a root whose
//     vtable[5] (IsInitDirectoryValid, 0x17D1680) says the directory is
//     unusable; enumerate its on-disk culture names through vtable[1]
//     (GetCultureNameFromDisk, 0x17D0B50) until it stops yielding names;
//     resolve each name through XLipGetCultureId (sub_17C0C60; -1 skips it,
//     and a defined-but-unregistered culture gets a fresh 60-byte
//     registered-culture object); read its on-disk version through vtable[4]
//     (ReadCultureVersion, 0x17D14A0); when the on-disk major version
//     matches the culture definition's major version (XLipGetCultureInfo
//     type 3) store {packed major/minor, root index} at +0x1C of the
//     registered-culture object, otherwise log "Found unsupported (%hu)
//     version for culture %S".  (The version-read / definition-lookup
//     failure branch logs "Failed to get culture supported major version
//     for %S".)
//  3. Pass 2, over the registered components (g_unk_1E2C69C): clear the
//     component's +0x38 vector and reserve 5 twelve-byte slots (the class-id
//     vector element 5), then for each registered culture (g_unk_1E2C680)
//     find the first root (asserting pRoot != NULL, line 374) whose
//     vtable[2] (DoesCultureDirectoryExist, 0x17D0F80) says
//     root \ compPath \ culture exists, and push a twelve-byte PPL lambda
//     record capturing the loop locals (the captures are stack addresses,
//     cleared and reinterpreted as the culture-index list in step 5 before
//     any consumer reads them).
//  4. Initialize the UI-language settings (XLipInitUILanguages, sub_17B3A50).
//  5. Rebuild every component's culture-index list (XLipUpdateComponent
//     CultureLists, sub_17B3CC0).
//  In this build the roots vector is always empty (CreateLipFSRoot falls out
//  on the empty settings path before calling InitLipFSRoot, so no root is
//  registered), so both passes run zero iterations and the function
//  reduces to: cancel tasks (a no-op on the empty task vector), the UI-
//  language registration (which pushes the OS preferred languages and
//  "en-uk" into g_unk_1E2C5C8) and the component list rebuild (each
//  component's +0x38 vector is cleared and stays empty -- the registered-
//  culture table g_unk_1E2C680 is empty throughout the test).
//  void XLipBringUpComponents(void)   -- original: cdecl, plain `retn`
static void XLipBringUpComponents(void)
{
    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return;
    }
    SetLastError(0);
    XLipClearTaskVector(&g_unk_1E2C67C, 1);           // sub_17C6B20

    {
        // Pass 1 -- roots x on-disk cultures.
        unsigned int rootCount = LipDwordVecSize(&g_unk_1E2C664);
        unsigned int r;
        for (r = 0; r < rootCount; r++)
        {
            XLipFSRoot *pRoot = (XLipFSRoot *)g_unk_1E2C664.begin[r];
            const XLipFSRootVT *vt = (const XLipFSRootVT *)pRoot->vtable;
            char name[85];
            unsigned int i;
            if (vt->isInitDirValid(pRoot) == 0)       // vtable[5], 0x17D1680
                continue;
            for (i = 0; ; i++)                        // vtable[1], 0x17D0B50
            {
                unsigned short major = 0;
                unsigned short minor = 0;
                int cultureId;
                if (vt->getCultureName(pRoot, i, name, 0x55) <= 0)
                    break;
                cultureId = XLipGetCultureId(name);   // sub_17C0C60
                if (cultureId < 0)
                    continue;
                if (vt->readCultureVersion(pRoot, name, &major, &minor) == 0)
                    continue;                         // vtable[4], 0x17D14A0
                {
                    unsigned short defVersion = 0;
                    if (XLipGetCultureInfo(name, 3, &defVersion, 2) == 2)
                    {
                        if (major == defVersion)
                        {
                            XLipRegisteredCulture *reg =
                                (XLipRegisteredCulture *)
                                g_unk_1E2C680.begin[cultureId];
                            reg->version = ((DWORD)major << 16) | minor;
                            reg->rootIndex = (DWORD)r;
                        }
                        else
                            XLipDebugMessage(
                                L"Found unsupported (%hu) version for culture %S",
                                (unsigned short)major, name);
                    }
                    else
                        XLipDebugMessage(
                            L"Failed to get culture supported major version for %S",
                            name);
                }
            }
        }
    }
    {
        // Pass 2 -- components x registered cultures x roots.
        unsigned int compCount = LipDwordVecSize(&g_unk_1E2C69C);
        unsigned int c;
        for (c = 0; c < compCount; c++)
        {
            const XLipComponent *comp =
                (const XLipComponent *)g_unk_1E2C69C.begin[c];
            LipDwordVec *vec = (LipDwordVec *)comp->subobject;   // +0x38
            unsigned int cultureCount = LipDwordVecSize(&g_unk_1E2C680);
            unsigned int rootCount = LipDwordVecSize(&g_unk_1E2C664);
            unsigned int i;
            unsigned int r;
            XLipVecClear(vec);            // sub_17C1A00
            XLipVecReserve12(vec, *LipDwordVecAt(&g_unk_1E2C6B8, 5)); // sub_17C1760(5)
            for (i = 0; i < cultureCount; i++)
            {
                const char *cultureName = XLipStrCstr(
                    (const F3AString *)g_unk_1E2C680.begin[i]);
                int found = 0;
                for (r = 0; r < rootCount && !found; r++)
                {
                    XLipFSRoot *pRoot = (XLipFSRoot *)g_unk_1E2C664.begin[r];
                    const XLipFSRootVT *vt;
                    if (pRoot == NULL)    // XLipComponent.cpp line 374
                        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent,
                                   374, L"pRoot != NULL");
                    vt = (const XLipFSRootVT *)pRoot->vtable;
                    if (vt->hasCultureDir(pRoot, XLipStrCstr(&comp->path),
                                          cultureName) != 0)   // vtable[2], 0x17D0F80
                    {
                        DWORD record[3];
                        record[0] = (DWORD)i;      // the culture index
                        record[1] = 0;
                        record[2] = (DWORD)r;      // the matching root index
                        XLipVecPush12(vec, record); // sub_17C1980
                        found = 1;
                    }
                }
            }
        }
    }

    XLipInitUILanguages();                          // sub_17B3A50
    XLipUpdateComponentCultureLists();              // sub_17B3CC0
}
// ============================================================
//  IXLipComponent -- the 16-byte "XLIPFSCP" signature header the lip
//  component-path resolver fills in and verifies (XLipComponent.cpp):
//    +0x00  vtable
//    +0x04  "XLIPFSCP" magic (8 bytes)
//    +0x0C  path (the resolved component path string)
// ============================================================
typedef struct IXLipComponent
{
    DWORD       vtable;   // +0x00
    char        magic[8]; // +0x04  "XLIPFSCP"
    const char *path;     // +0x0C
} IXLipComponent;         // 16 bytes

// sub_17D06B0 (0x17D06B0 - 0x17D06B0+54) -- the IXLipComponent ctor:
// stamps the vtable + the "XLIPFSCP" signature and clears the path.
// [NOT byte-identical] the vtable is the high-VA XLipFSComponent vftable, left
// zero here (nothing in this build dispatches through it); the signature and
// path are the only members the resolver reads.
static IXLipComponent *IXLipComponentCtor(IXLipComponent *obj)
{
    obj->vtable = 0;
    memcpy(obj->magic, "XLIPFSCP", 8);
    obj->path = NULL;
    return obj;
}

// sub_17C66A0 (0x17C66A0 - 0x17C66A0+20) -- the IXLipComponent dtor:
// resets the vtable (the path string is owned elsewhere and not freed here).
// Functionally a no-op in this build (the placeholder vtable is already zero).
static IXLipComponent *IXLipComponentDtor(IXLipComponent *obj)
{
    obj->vtable = 0;
    return obj;
}

// sub_17D0850 (0x17D0850 - 0x17D0850+47) -- verify the IXLipComponent
// signature: the first 8 bytes must be the "XLIPFSCP" magic.
static int IXLipComponentValid(const IXLipComponent *obj)
{
    return (obj != NULL) && (memcmp(obj->magic, "XLIPFSCP", 8) == 0);
}

// unknown_libname_2509 (0x17D0830 - 0x17D0830+17) -- the IXLipComponent
// path accessor: returns the resolved path string at +0x0C.
static const char *IXLipComponentGetPath(const IXLipComponent *obj)
{
    return obj->path;
}

//  XLipResolveComponentBuf  (sub_17BFF60, 0x17BFF60 - 0x17BFF60+1019)  --
//  resolve a registered lip component (by name) for a language into an output
//  IXLipComponent buffer (XLipComponent.cpp).  cdecl: (pcszComponent,
//  pcszLanguage, pComponentOut).  Returns non-zero on success, 0 on failure.
//  [NOT byte-identical] placement-dependent (relative calls to FindLipComponent
//  by name / XLipAssert / the culture-fallback map helpers / the component
//  vtable[3] path resolver) and refs the high-VA init-flag byte_1E2C6B4 and the
//  rdata assert strings.
//
//  Gated by the init flag (SetLastError(0x15), return 0 when unset).  Asserts
//  pcszComponent != NULL (line 508) and pComponentOut != NULL (line 515),
//  falling out with SetLastError(0x57).  Looks the component up by name
//  (FindLipComponentByName); a miss asserts (line 523) and returns 0
//  (SetLastError(0x57)).  A non-null language is lower-cased into a 0x55-byte
//  scratch (line 532 / 540).  The language is then resolved: a direct
//  culture lookup (sub_17C0E40) for a named language, else the component
//  fallback-language map walk (sub_17B4C10 / sub_17BD1F0 / sub_17B5CC0 /
//  sub_17C1F70 / sub_17B8FB0 / sub_17B8FE0 / sub_17B91F0 / sub_17B91C0 /
//  sub_17C16B0 / sub_17B3D50 / sub_17C0E40).  On a resolved language the
//  component's vtable[3] (0x17C0F60) path resolver is dispatched with
//  (component, the component's own path string, the resolved language, the
//  output buffer) and its result is returned; a resolution miss returns 0
//  (SetLastError(0x490)).  In this build no component is ever registered with a
//  non-empty name (the seven slot names are all empty), so the lookup misses
//  and the function returns 0 -- the culture-fallback + vtable[3] dispatch is
//  the remaining stub (unreached).
// ============================================================
static int XLipResolveComponentBuf(const char *pcszComponent, const char *pcszLanguage,
                                   IXLipComponent *pComponentOut)
{
    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return 0;
    }
    if (pcszComponent == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 508, L"pcszComponent");
    if (pComponentOut == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 515, L"pComponentOut");
    if (pcszComponent == NULL || pComponentOut == NULL)
    {
        SetLastError(0x57);
        return 0;
    }
    void *comp = FindLipComponentByName(pcszComponent);
    if (comp == NULL)
    {
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 523, L"pComponent");
        SetLastError(0x57);
        return 0;
    }
    // The language resolution (named-language culture lookup / the component
    // fallback-language map walk) and the component vtable[3] path-resolver
    // dispatch are the remaining stub: unreachable in this build because no
    // component is registered with a non-empty name, so the lookup above always
    // misses.  Fall out as a resolution miss.
    (void)pcszLanguage;
    (void)comp;
    SetLastError(0x490);
    return 0;
}

//  XLipResolveComponentPath  (sub_17C0360, 0x17C0360 - 0x17C0360+680)  --
//  resolve a registered lip component's path into a C string (XLipComponent.cpp).
//  cdecl: (pcszComponent, pcszLanguage, destination, sizeInBytes).  Constructs a
//  temporary IXLipComponent header, resolves the component through
//  XLipResolveComponentBuf, verifies the "XLIPFSCP" signature, and copies the
//  resolved path into destination (bounded by sizeInBytes); returns
//  strlen(path) + 1 on success, -1 on failure.  [NOT byte-identical]
//  placement-dependent (relative calls to IXLipComponentCtor /
//  XLipResolveComponentBuf / IXLipComponentValid / IXLipComponentGetPath /
//  IXLipComponentDtor / XLipAssert / _strlen / _strcpy_s) and refs the high-VA
//  init-flag byte_1E2C6B4 + the rdata assert strings.
// ============================================================
static int XLipResolveComponentPath(const char *pcszComponent, const char *pcszLanguage,
                                    char *destination, int sizeInBytes)
{
    IXLipComponent comp;
    int ret;
    int len;
    const char *path;

    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return -1;
    }
    if (pcszComponent == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 593, L"pcszComponent");
    if (pcszComponent == NULL)
    {
        SetLastError(0x57);
        return -1;
    }
    (void)IXLipComponentCtor(&comp);
    ret = XLipResolveComponentBuf(pcszComponent, pcszLanguage, &comp);
    if (ret == 0)
    {
        (void)IXLipComponentDtor(&comp);
        return -1;
    }
    if (!IXLipComponentValid(&comp))
    {
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 609,
                   L"FALSE && \"failed to verify IXLipComponent signature\"");
        SetLastError(0x70C);
        (void)IXLipComponentDtor(&comp);
        return -1;
    }
    path = IXLipComponentGetPath(&comp);
    len = (int)strlen(path) + 1;
    if (destination != NULL)
    {
        if (sizeInBytes < len)
        {
            SetLastError(0x7A);
            (void)IXLipComponentDtor(&comp);
            return -1;
        }
        if (strcpy_s(destination, (size_t)sizeInBytes, path) != 0)
        {
            XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 626, L"crterr==0");
            SetLastError(0x54F);
            (void)IXLipComponentDtor(&comp);
            return -1;
        }
    }
    (void)IXLipComponentDtor(&comp);
    return len;
}

//  ResolveLipComponentSlotPath  (sub_717D60, 0x717D60 - 0x717DF2)  -- resolve a
//  lip component slot's relative path and publish it into the out slot
//  (XLipComponent.cpp).  cdecl: (compSlot, outSlot).  Reads the component name
//  from compSlot (GetSlotString), resolves it to a path through
//  XLipResolveComponentPath (a 0x104-byte scratch), and, on a positive result,
//  builds a fresh shared slot from the path, copy-assigns it into outSlot, and
//  releases the scratch slot.  Returns 1 on success, 0 when the path did not
//  resolve.  [NOT byte-identical] placement-dependent (relative calls to
//  GetSlotString / XLipResolveComponentPath / SetSharedSlot / CopySharedSlot /
//  ReleaseSharedSlot / the __security_check_cookie landing pad) and refs the
//  high-VA ___security_cookie global.
// ============================================================
static char ResolveLipComponentSlotPath(F3SharedBuffer **compSlot, F3SharedBuffer **outSlot)
{
    char destination[0x104];
    F3SharedBuffer *temp = 0;

    if (XLipResolveComponentPath(GetSlotString(compSlot), NULL, destination, 0x104) <= 0)
        return 0;
    (void)SetSharedSlot(&temp, destination, 0xFFFFFFFF);
    CopySharedSlot(outSlot, &temp);
    ReleaseSharedSlot(&temp);
    return 1;
}
// ============================================================
//  XLipGetComponentLanguage  (sub_17BFC20, 0x17BFC20 - 0x17BFF58)  --
//  resolve a registered lip component's effective language name into an
//  output buffer (XLipComponent.cpp, lines 406-481).  The component's entry
//  in the global component-fallback map (g_ComponentFallbacks, 0x1E2C5E0)
//  names its fallback cultures (the 24-byte culture-index vector at the map
//  value's +0x1C), and the first fallback whose culture resolves through the
//  component's culture table (sub_17C0E40) is the component's language.
//  cdecl: (pcszComponent, Destination, SizeInBytes).  Returns
//  strlen(language) + 1 on success (query mode when Destination == NULL,
//  else copied with _strcpy_s), -1 on failure.
//  [NOT byte-identical] placement-dependent (relative calls to XLipAssert /
//  FindLipComponentByName / XLipStrAssign / XLipStrDtor / _strlen /
//  _strcpy_s and the __imp_SetLastError IAT thunk) and refs the high-VA
//  init-flag global byte_1E2C6B4 (0x01E2C6B4), the component-fallback map
//  (0x01E2C5E0) and the registered-culture vector (0x01E2C680).
//
//  Gated by the init flag byte_1E2C6B4 (SetLastError(0x15), return -1 when
//  unset).  Asserts pcszComponent != NULL (line 406), SizeInBytes >= 0
//  (line 413) and !(Destination == NULL && SizeInBytes > 0), falling out
//  with SetLastError(0x57) and -1.  Looks the component up by name
//  (FindLipComponentByName); a miss asserts (line 428) and returns -1
//  (SetLastError(0x57)).  The component name is then copied into a 28-byte
//  temp string (XLipStrAssign, sub_17B8EF0) and used as the key of the
//  fallback-map lookup (sub_17C1CD0; the temp is dropped, XLipStrDtor):
//  a miss asserts (line 442, "it != g_ComponentFallbacks.end()") and
//  returns -1 (SetLastError(0x57)).  The found entry's culture-index vector
//  is then walked (sub_17B8FB0 / sub_17B8FE0 / sub_17B91F0 / sub_17B91C0 /
//  sub_17C1C90): each index names a registered culture (sub_17C16B0 over
//  the 0x1E2C680 vector) and the culture's name string (sub_17B3D50) is
//  resolved through the component (sub_17C0E40) until one hits; an empty
//  walk returns -1 (SetLastError(0x490)).  The found language (asserted
//  non-NULL, line 465) is returned as strlen + 1; when Destination is
//  non-NULL it is copied with _strcpy_s (SetLastError(0x7A) when the buffer
//  is too small, 0x54F when the copy fails, line 481).
//
//  In this build the component registry and the fallback map are both empty
//  (the seven InitLipCultures slot names are all empty and nothing ever
//  inserts into the map), so the FindLipComponentByName lookup always
//  misses and the function returns -1 (SetLastError(0x57)) -- the fallback
//  walk + copy is the remaining stub (unreached).  Replaces the sub_17BFC20
//  no-op stub.
// ============================================================
static int XLipGetComponentLanguage(const char *pcszComponent, char *Destination,
                                    int SizeInBytes)
{
    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return -1;
    }
    if (pcszComponent == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 406, L"pcszComponent");
    if (SizeInBytes < 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 413, L"cchBuffer >= 0");
    if ((Destination == NULL && SizeInBytes > 0) ||
        pcszComponent == NULL || SizeInBytes < 0)
    {
        SetLastError(0x57);
        return -1;
    }
    {
        void *comp = FindLipComponentByName(pcszComponent);
        if (comp == NULL)
        {
            XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 428, L"pComponent");
            SetLastError(0x57);
            return -1;
        }
        // The fallback-map lookup (sub_17C1CD0 over g_ComponentFallbacks,
        // keyed by the name copied into the 28-byte temp string) always
        // misses in this build (the map is empty and never populated), so the
        // original's line-442 assert fires and its found == begin() check
        // falls out with E_INVALIDARG before the fallback-language resolution
        // walk (the culture-index vector + sub_17C0E40) is entered.
        (void)comp;
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 442,
                   L"it != g_ComponentFallbacks.end()");
        SetLastError(0x57);
        return -1;
    }
}
// ============================================================
//  XLipGetCultureCount  (sub_17C0610, 0x17C0610 - 0x17C0674)  -- the
//  number of lip cultures in scope (XLipComponent.cpp).  cdecl:
//  (pcszComponent).  Gated by the init flag byte_1E2C6B4 (SetLastError(0x15)
//  and -1 when unset).  For a NULL component it counts the global
//  registered-culture table (sub_17B3DC0 over the 24-byte vector at
//  0x1E2C680: (end - begin) / 4); for a named component it resolves it
//  (FindLipComponentByName) and counts the 12-byte culture-index entries of
//  the component's +0x38 subobject (sub_17C1930: (end - begin) / 12),
//  reporting a component miss as -1 with SetLastError(0x490).
//  [NOT byte-identical] placement-dependent (relative calls to
//  FindLipComponentByName / the two vector-count methods) and refs the
//  high-VA init-flag global byte_1E2C6B4 (0x01E2C6B4), the culture table
//  (0x01E2C680) and the __imp_SetLastError IAT slot.
// ============================================================
static int XLipGetCultureCount(const char *pcszComponent)
{
    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return -1;
    }
    if (pcszComponent == NULL)
        return (int)LipDwordVecSize(&g_unk_1E2C680);   // sub_17B3DC0
    void *comp = FindLipComponentByName(pcszComponent);
    if (comp == NULL)
    {
        SetLastError(0x490);
        return -1;
    }
    {
        const LipDwordVec *sub = (const LipDwordVec *)((const char *)comp + 0x38);
        return (int)(((char *)sub->end - (char *)sub->begin) / 12);   // sub_17C1930
    }
}
// ============================================================
//  XLipGetCultureName  (sub_17C0680, 0x17C0680 - 0x17C08D0)  -- copy the
//  name of culture `iLanguage` into `pszBuffer` (XLipComponent.cpp).
//  cdecl: (pcszComponent, iLanguage, pszBuffer, cchBuffer).
//
//  Gated by the init flag byte_1E2C6B4 (SetLastError(0x15) and -1 when
//  unset).  Asserts iLanguage >= 0 (line 681), cchBuffer >= 0 (line 689)
//  and pszBuffer != NULL || cchBuffer == 0 (line 696), falling out with
//  SetLastError(0x57) and -1 when any is violated.  For a NULL component
//  the culture is the global registered-culture table (g_unk_1E2C680, the
//  24-byte vector of XLipRegisteredCulture pointers: iLanguage >
//  size - 1 -> SetLastError(0x585) and -1; sub_17C16B0 element access).
//  For a named component it resolves it (FindLipComponentByName, a miss ->
//  SetLastError(0x57) and -1), bounds iLanguage against the component's
//  +0x38 12-byte-entry culture-index vector (sub_17C1930 count, sub_17C1950
//  element; iLanguage > count - 1 -> SetLastError(0x585) and -1) and uses
//  the entry's first DWORD as the index into the global table.  The
//  resolved language-name string (the 24-byte F3AString at +0x00 of the
//  XLipRegisteredCulture: length at +0x14, SSO/heap data at +0x04) is
//  asserted non-NULL (line 748, no fall-out); a NULL pszBuffer returns the
//  name length + 1, else cchBuffer <= length is a fail (SetLastError(0x7A)
//  and -1) and the _strcpy_s copy (source through the SSO-aware sub_17B3D50
//  data accessor, size cchBuffer) returns length + 1 on success, else the
//  line-763 "(ret==0) && \"error copying language name\"" assert +
//  SetLastError(0x54F) and -1.
//
//  [NOT byte-identical] placement-dependent (relative calls to XLipAssert /
//  FindLipComponentByName / the sub_17B3DC0 / sub_17C1930 / sub_17C1950 /
//  sub_17C16B0 / sub_17B3D50 vector and string helpers / _strcpy_s / the
//  __imp_SetLastError IAT thunk) and refs the high-VA init-flag global
//  byte_1E2C6B4 (0x01E2C6B4), the registered-culture table (0x01E2C680) and
//  the rdata assert strings at their original VAs.
// ============================================================
static int XLipGetCultureName(const char *pcszComponent, int iLanguage,
                              char *pszBuffer, int cchBuffer)
{
    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return -1;
    }
    if (iLanguage < 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 681, L"iLanguage >= 0");
    if (iLanguage < 0)
    {
        SetLastError(0x57);
        return -1;
    }
    if (cchBuffer < 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 689, L"cchBuffer >= 0");
    if (cchBuffer < 0)
    {
        SetLastError(0x57);
        return -1;
    }
    if (pszBuffer == NULL && cchBuffer != 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 696,
                   L"pszBuffer!=NULL || cchBuffer==0");
    if (pszBuffer == NULL && cchBuffer != 0)
    {
        SetLastError(0x57);
        return -1;
    }
    F3AString *pLangName;
    if (pcszComponent == NULL)
    {
        int count = (int)LipDwordVecSize(&g_unk_1E2C680);   // sub_17B3DC0
        if (iLanguage > count - 1)
        {
            SetLastError(0x585);
            return -1;
        }
        pLangName = (F3AString *)g_unk_1E2C680.begin[iLanguage];   // sub_17C16B0
    }
    else
    {
        void *comp = FindLipComponentByName(pcszComponent);
        if (comp == NULL)
        {
            SetLastError(0x57);
            return -1;
        }
        {
            const LipDwordVec *sub = (const LipDwordVec *)((const char *)comp + 0x38);
            int count = (int)((char *)sub->end - (char *)sub->begin) / 12;   // sub_17C1930
            if (iLanguage > count - 1)
            {
                SetLastError(0x585);
                return -1;
            }
            {
                DWORD cultureIndex = ((const DWORD *)sub->begin)[3 * iLanguage];   // sub_17C1950
                pLangName = (F3AString *)g_unk_1E2C680.begin[cultureIndex];        // sub_17C16B0
            }
        }
    }
    if (pLangName == NULL)
        XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 748, L"pLangName");
    if (pszBuffer == NULL)
        return (int)pLangName->len + 1;               // unknown_libname_2358 (+0x14)
    if (cchBuffer <= (int)pLangName->len)
    {
        SetLastError(0x7A);
        return -1;
    }
    if (strcpy_s(pszBuffer, (size_t)cchBuffer, XLipStrCstr(pLangName)) == 0)   // sub_17B3D50
        return (int)pLangName->len + 1;
    XLipAssert(g_xlipAssertFailed, g_xlipFileComponent, 763,
               L"(ret==0) && \"error copying language name\"");
    SetLastError(0x54F);
    return -1;
}
// sub_17CC320 (0x17CC320 - 0x17CC4E2) -- return a direct pointer to one
// field of a culture definition (the XLipCultures.cpp pointer-form sibling
// of XLipGetCultureInfo, sub_17CC4F0):  type 1 -- the wide language string
// (the W3AString at +0x1C; the SSO buffer or heap data pointer, through the
// sub_17CD170 getter);  type 2 -- the source / parent name (the F3AString
// at +0x38, through the sub_17B3D50 cstr getter; NULL +
// SetLastError(0x10D2) when the source is "null", i.e. a top-level culture);
//  type 3 -- the defined major version WORD (at +0x54);  anything else --
// the line-72 `false && "Unknown XLIPCULTUREINFOTYPE specified"` assert +
// SetLastError(0x57) + NULL.  Gates on the init flag (unset ->
// SetLastError(0x15) + NULL), asserts the name is non-null and non-empty
// (line 28, falling through to SetLastError(0x57) + NULL when it is not),
// lowercases an 85-byte copy (strcpy_s / _strlwr_s, the line-37 ret==0
// asserts), and resolves it through XLipFindCultureDef (a miss ->
// SetLastError(0x490) + NULL).  cdecl, two args.
// [NOT byte-identical] placement-dependent (relative calls to XLipAssert /
// XLipFindCultureDef / the sub_17CD170 and sub_17B3D50 string getters) and
// refs the high-VA init-flag global byte_1E2C6B4, the "null" rdata string
// (0x01BC33EC), the wide assert-condition strings and the _strcpy_s /
// _strlwr_s / __imp_SetLastError IAT slots.
static const void *XLipGetCultureInfoPtr(const char *pcszCultureName,
                                         unsigned int type)
{
    char name[85];
    XLipCultureInfo *info;
    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return 0;
    }
    if (pcszCultureName == NULL || pcszCultureName[0] == 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 28,
                   L"pcszCultureName && pcszCultureName[0] && \"Parameter can't be NULL\"");
    if (pcszCultureName == NULL || pcszCultureName[0] == 0)
    {
        SetLastError(0x57);
        return 0;
    }
    if (strcpy_s(name, 0x55, pcszCultureName) != 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 37, L"ret==0");
    if (_strlwr_s(name, 0x55) != 0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 37, L"ret==0");
    info = XLipFindCultureDef(name);
    if (info == NULL)
    {
        SetLastError(0x490);
        return 0;
    }
    switch (type)
    {
    case 1:   /* sub_17CD170(info + 0x1C) -- the wide language data pointer */
        return (info->language.cap < 8)
             ? (const void *)info->language.u.buf
             : (const void *)info->language.u.ptr;
    case 2:   /* sub_17B3D50(info + 0x38) -- the source / parent cstr */
        if (strcmp(XLipStrCstr(&info->source), "null") == 0)
        {
            SetLastError(0x10D2);
            return 0;
        }
        return XLipStrCstr(&info->source);
    case 3:
        return (const void *)&info->version;   /* the +0x54 major version */
    default:
        XLipAssert(g_xlipAssertFailed, g_xlipFileCultures, 72,
                   L"false && \"Unknown XLIPCULTUREINFOTYPE specified\"");
        SetLastError(0x57);
        return 0;
    }
}
//  RegisterStandardAssets  (0x718690 - 0x718BCC)  -- register the game's
// standard asset packages ("speech" / "speechdoc" / "lipsync" / "text" /
// "fonts" / "videos" / "gui") into the language / VFS resource table.
// Defined below, after the F3WString helpers it builds its temp strings with.
// RegisterStandardAssets is declared in xlip.h (defined in gamemain.cpp).
// ============================================================
//  XLipBringUpCultures  (sub_719540, 0x719540 - 0x719765)  -- bring the
//  lip cultures up across the seven registered lip components
//  (XLipComponent.cpp):
//
//    1. Snapshot each component's current effective language into a
//       24-byte shared-slot vector (XLipCollectComponentLanguages,
//       sub_7194A0).
//    2. For each of the seven component slots (dword_1DC2748, 0x14 bytes
//       per slot), apply the 176-byte XLIPCOMPONENTSETTINGS block -- the
//       component name + the language named by the culture slot argument --
//       through XLipApplyComponentSettings (sub_17B2C30); any call that
//       does not return 1 sets the failure flag (the return value).
//    3. Bring the components up (XLipBringUpComponents).
//    4. Take a second snapshot and set *changed when the two lists differ
//       (a count mismatch, or any element pair differing under
//       SharedSlotDiffers, sub_1A00E80).
//    5. Tear down both vectors (the per-element ReleaseSharedSlot, the
//       LHFree'd backing array, the 4-byte owner block) and return the
//       failure flag.
//
//  char XLipBringUpCultures(F3SharedBuffer **cultureSlot, char *changed)
//  -- original: cdecl, plain `retn` (the 0xE8 frame + the
//  __security_cookie landing pad).
//
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc /
//  LHFree / GetSlotString / _strcpy_s / XLipBringUpComponents /
//  ReleaseSharedSlot / the __security_check_cookie landing pad) and refs
//  the high-VA seven-component slot cluster (dword_1DC2748, 0x01DC2748)
//  plus the lip-library globals reached through the callees.
// ============================================================

// The 24-byte shared-slot vector (the sub_488910 push_back family) that
// XLipBringUpCultures / XLipCollectComponentLanguages build on the stack:
// +0x00 a 4-byte LHAlloc'd owner block that points back at the vector
// itself (the grow machinery sub_489460 asserts the passed owner equals
// vec->owner), two spare words (the original never initializes them and
// nothing reads them), then begin / end / alloc-end over 4-byte
// F3SharedBuffer slots.  Size = (end - begin) / 4.
typedef struct F3SharedSlotVec
{
    void *              owner;
    DWORD               spare0;
    DWORD               spare1;
    F3SharedBuffer    **begin;
    F3SharedBuffer    **end;
    F3SharedBuffer    **allocEnd;
} F3SharedSlotVec;
// the vector ctor (the LHAlloc(4) + self-pointer pair at the top of
// sub_719540): begin / end / alloc-end null.
static void SharedSlotVecInit(F3SharedSlotVec *vec)
{
    void *block = LHAlloc(4);
    vec->owner  = block;
    if (block != 0)
        *(void **)block = vec;
    vec->spare0   = 0;   // the original leaves +4 / +8 uninitialized
    vec->spare1   = 0;   // (unread; zeroed for determinism)
    vec->begin    = 0;
    vec->end      = 0;
    vec->allocEnd = 0;
}
// sub_488910 (0x488910 - 0x48896E) -- the vector push_back (thiscall +
// retn 4): when size < capacity it refs the source slot into the next
// element (EngRefSharedSlot) and advances end; when full it goes through the
// grow helper sub_489460: a fresh LHAlloc(4 * newCap) array through the
// element-count allocator sub_433120 (newCap = max(cap + cap/2, size + 1);
// the length_error overflow guard is dead against the game allocator),
// the old slots ref-copied in (sub_48C230), the new element built from the
// source slot (sub_4895A0), the old slots released (sub_48A640), and the
// old array freed through the allocator table (dword_2075B24 + 0x10).
// The owner-block consistency check (arg owner != 0 && != vec->owner ->
// _invalid_parameter_noinfo) is dead in this build (the owner always
// matches).  [NOT byte-identical] placement-dependent (relative calls to
// EngRefSharedSlot / the grow helpers).
static void SharedSlotVecPush(F3SharedSlotVec *vec, F3SharedBuffer **src)
{
    unsigned int size = (unsigned int)((char *)vec->end - (char *)vec->begin) >> 2;
    unsigned int cap = (vec->begin != 0)
                       ? (unsigned int)((char *)vec->allocEnd - (char *)vec->begin) >> 2
                       : 0;
    unsigned int i;
    if (size < cap)
    {
        if (vec->end != 0)
            EngRefSharedSlot(vec->end, src);
        vec->end = vec->end + 1;
        return;
    }
    unsigned int newcap = cap + (cap >> 1);
    if (newcap < size + 1)
        newcap = size + 1;
    F3SharedBuffer **nb = (F3SharedBuffer **)LHAlloc(4 * newcap);
    for (i = 0; i < size; i++)
        EngRefSharedSlot(&nb[i], &vec->begin[i]);
    EngRefSharedSlot(&nb[size], src);
    for (i = 0; i < size; i++)
        ReleaseSharedSlot(&vec->begin[i]);
    if (vec->begin != 0)
        LHFree(vec->begin);
    vec->begin    = nb;
    vec->end      = nb + size + 1;
    vec->allocEnd = nb + newcap;
}
// the vector teardown (the two identical inline cleanups at the end of
// sub_719540): ReleaseSharedSlot each slot, LHFree the backing array (when
// begin is non-null), zero the pointers, and LHFree the 4-byte owner block
// (LHFree(0) is a no-op when the ctor's LHAlloc failed).
static void SharedSlotVecDtor(F3SharedSlotVec *vec)
{
    F3SharedBuffer **slot;
    if (vec->begin != 0)
    {
        for (slot = vec->begin; slot != vec->end; slot++)
            ReleaseSharedSlot(slot);
        LHFree(vec->begin);
    }
    vec->begin    = 0;
    vec->end      = 0;
    vec->allocEnd = 0;
    LHFree(vec->owner);
    vec->owner    = 0;
}
// XLipCollectComponentLanguages  (sub_7194A0, 0x7194A0 - 0x719540)  --
// snapshot the current effective language of each of the seven lip
// components into `vec` (the 24-byte shared-slot vector).  For each
// component slot (dword_1DC2748, 0x14 bytes per slot, iterated
// dword_1CA63C4 = 7 times per the rdata constant at 0x1CA63C4): zero an
// 85-byte buffer (the original's memset(0x54) + the 0x54 byte), resolve the
// component language (XLipGetComponentLanguage, sub_17BFC20); when it
// resolves (> 0) set a fresh shared slot from the language (SetSharedSlot)
// and push a ref of it into the vector (SharedSlotVecPush, sub_488910),
// then drop the local ref (ReleaseSharedSlot).  cdecl: (vec).
// [NOT byte-identical] placement-dependent (relative calls to memset /
// GetSlotString / XLipGetComponentLanguage / SetSharedSlot / SharedSlotVecPush
// / ReleaseSharedSlot / the __security_check_cookie landing pad) and refs
// the high-VA seven-component slot cluster (0x01DC2748) and the component
// count constant (0x01CA63C4).
static void XLipCollectComponentLanguages(F3SharedSlotVec *vec)
{
    unsigned int i;
    for (i = 0; i < 7; i++)   // dword_1CA63C4 (the rdata constant 7)
    {
        char destination[85];
        F3SharedBuffer *slot = 0;
        const char *name =
            GetSlotString((F3SharedBuffer **)&g_dword_1DC2748[i * 5]);
        memset(destination, 0, 85);
        if (XLipGetComponentLanguage(name, destination, 0x55) > 0)
        {
            (void)SetSharedSlot(&slot, destination, -1);
            SharedSlotVecPush(vec, &slot);
            ReleaseSharedSlot(&slot);
        }
    }
}
// XLIPCOMPONENTSETTINGS -- the 176-byte (0xB0) lip component-settings block
// (XLipSettings.cpp) that the component-settings map (unk_1E2C600, the
// name-keyed tree whose nodes hold the 176-byte value at +28) stores per
// component; sub_17B2F60 writes the whole block out per map entry.
typedef struct XLipComponentSettings
{
    int   cbSize;          // +0x00: must be 176 (sizeof XLIPCOMPONENTSETTINGS)
    char  name[0x55];      // +0x04: the component name (85 bytes)
    char  language[0x57];  // +0x59: the effective language name (87 bytes)
} XLipComponentSettings;   // 176 bytes total
// XLipSettingsNode -- one entry of the lip component-settings map
// (dword_1E2C600): a 176-byte XLIPCOMPONENTSETTINGS block keyed by the
// component name.  The original is an MSVC red-black tree whose 0xDC-byte
// nodes hold a 28-byte F3AString key and the 176-byte value (at node + 28);
// here it is modeled as a list of heap nodes with stable value addresses so
// the find-or-insert (map::operator[]) semantics are identical.
typedef struct XLipSettingsNode
{
    XLipSettingsNode *next;           // next entry (insertion order)
    char              *name;          // heap NUL-terminated component name (key)
    unsigned int       nameLen;       // key length (basic_string compare)
    XLipComponentSettings value;      // 176-byte settings block (stable address)
} XLipSettingsNode;
// XLipSettingsMap -- the map object itself (the original tree at 0x1E2C600,
// the thiscall `this` for XLipSettingsMapFind / the tree walk and insert).
typedef struct XLipSettingsMap
{
    XLipSettingsNode *first;          // head of the node list
    unsigned int      count;          // entry count (the tree's +0x1C count)
} XLipSettingsMap;
// 0x1E2C600 -- the lip component-settings map object (name-keyed).  Image-
// initializes empty and is only ever populated through XLipSettingsMapFind.
static XLipSettingsMap g_xlipSettingsMap;
// sub_17B4270 (0x17B4270 - 0x17B43B6) -- the component-settings map
// find-or-create (map::operator[]) over the settings map at 0x1E2C600
// (thiscall, the map object in ECX, the 28-byte name key on the stack, `retn
// 4`): the tree walk (sub_17B71C0 find / sub_17B5A70 equal / sub_17B5290
// compare) looks the name up; a hit returns the found node's 176-byte
// settings value (node + 28), a miss inserts a zero-filled default value
// (memset 0xB0, sub_17B5BE0 / the sub_17B52E0 rebalancing insert) and
// returns it.  Returns the stable value pointer, or 0 if the entry could not
// be allocated.  The original's SEH frame drops the temp copy of the key when
// the body throws; nothing here throws, so that path is a no-op.  [NOT
// byte-identical] placement-dependent (relative calls to the tree walk /
// insert / _memset and the __security_check_cookie landing pad) and refs the
// high-VA settings-map object (0x01E2C600).
static void *XLipSettingsMapFind(void *map, F3AString *name)
{
    XLipSettingsMap *m = (XLipSettingsMap *)map;
    const char *key = XLipStrCstr(name);
    unsigned int keyLen = name->len;
    XLipSettingsNode *n;

    for (n = m->first; n != 0; n = n->next)
        if (n->nameLen == keyLen && memcmp(n->name, key, keyLen) == 0)
            return (void *)&n->value;

    n = (XLipSettingsNode *)LHAlloc(sizeof(XLipSettingsNode));
    if (n == 0)
        return 0;
    n->name = (char *)LHAlloc(keyLen + 1);
    if (n->name == 0)
    {
        LHFree(n);
        return 0;
    }
    memcpy(n->name, key, keyLen);
    n->name[keyLen] = 0;
    n->nameLen = keyLen;
    memset(&n->value, 0, sizeof(n->value));
    n->next = m->first;
    m->first = n;
    m->count++;
    return (void *)&n->value;
}
// XLipApplyComponentSettings  (sub_17B2C30, 0x17B2C30 - 0x17B2DDD)  --
// apply one XLIPCOMPONENTSETTINGS block to the component named in its +4
// field (XLipSettings.cpp): gated by the init flag byte_1E2C6B4
// (SetLastError(0x15), 0 when unset); asserts pSettings != NULL (line 179,
// falling out with SetLastError(0x57)); asserts
// pSettings->cbSize == sizeof(XLIPCOMPONENTSETTINGS) (line 187,
// SetLastError(0x57)); an empty name falls out (SetLastError(0x57)); a
// component miss (FindLipComponentByName) is 0 with SetLastError(0xD).
// On success the name is copied into a 28-byte temp string (XLipStrAssign,
// sub_17B8EF0), the map entry (XLipSettingsMapFind, sub_17B4270) is
// memcpy_s'd with the whole cbSize block (a failure is SetLastError(0x54F),
// 0), the component's culture list is rebuilt (sub_17B3B00 -- modeled by
// the project's XLipUpdateComponentCultureList; only reachable on a map
// hit, unreached in this build), the temp is dropped (XLipStrDtor,
// sub_17B8F30), and 1 is returned.  The original's SEH frame drops the
// temp string when the body throws; that path is unreachable in this build
// (nothing here throws).  cdecl: (pSettings).
// [NOT byte-identical] placement-dependent (relative calls to XLipAssert /
// XLipStrAssign / XLipStrDtor / FindLipComponentByName / _memcpy_s /
// sub_17B3B00, the __imp_SetLastError IAT thunk, and the
// __security_check_cookie landing pad) and refs the high-VA init-flag
// global byte_1E2C6B4 (0x01E2C6B4) and the settings map (0x01E2C600).
static int XLipApplyComponentSettings(char *pSettings)
{
    if (g_byte_1E2C6B4 == 0)
    {
        SetLastError(0x15);
        return 0;
    }
    if (pSettings == NULL)
    {
        XLipAssert(g_xlipAssertFailed, g_xlipFileSettings, 179,
                   L"pSettings != NULL");
        SetLastError(0x57);
        return 0;
    }
    if (*(DWORD *)pSettings != 0xB0)
        XLipAssert(g_xlipAssertFailed, g_xlipFileSettings, 187,
                   L"pSettings->cbSize == sizeof(XLIPCOMPONENTSETTINGS)");
    if (*(DWORD *)pSettings != 0xB0)
    {
        SetLastError(0x57);
        return 0;
    }
    if (pSettings[4] == 0)
    {
        SetLastError(0x57);
        return 0;
    }
    if (FindLipComponentByName(pSettings + 4) == NULL)
    {
        SetLastError(0xD);
        return 0;
    }
    {
        F3AString temp;
        XLipStrAssign(&temp, pSettings + 4);      // sub_17B8EF0
        void *entry = XLipSettingsMapFind(&g_xlipSettingsMap, &temp); // sub_17B4270
        if (memcpy_s(entry, 0xB0, pSettings, *(DWORD *)pSettings) != 0)
        {
            SetLastError(0x54F);
            XLipStrDtor(&temp);                   // sub_17B8F30
            return 0;
        }
        /* sub_17B3B00: rebuild the component's culture list from this
           settings block; unreached in this build (the map lookup above
           never hits). */
        (void)entry;
        XLipStrDtor(&temp);
        return 1;
    }
}
// sub_403AF0 (0x403AF0 - 0x403B17) -- counted-buffer equality (thiscall +
// retn 4): the +4 lengths are equal and the data strings compare equal
// (sub_403F10: the generic byte-wise C string compare, 0 equal / -1 less /
// 1 greater -- _strcmp semantics).
static int SharedBufferBlocksEqual(F3SharedBuffer *a, F3SharedBuffer *b)
{
    return (a->length == b->length) && (strcmp(a->data, b->data) == 0);
}
// sub_1A00E80 (0x1A00E80 - 0x1A00EC2) -- shared-slot difference (thiscall +
// retn 4): the two 4-byte slots differ when they hold different blocks --
// same block: equal; either null: different; both non-null: different when
// the block contents differ (SharedBufferBlocksEqual, sub_403AF0).
static int SharedSlotDiffers(F3SharedBuffer **a, F3SharedBuffer **b)
{
    if (*a == *b)
        return 0;
    if (*a != 0 && *b != 0)
        return SharedBufferBlocksEqual(*a, *b) ? 0 : 1;
    return 1;
}
// XLipBringUpCultures -- see the cluster header above (sub_719540).
static char XLipBringUpCultures(F3SharedBuffer **cultureSlot, char *changed)
{
    F3SharedSlotVec before, after;
    XLipComponentSettings settings;
    F3SharedBuffer **comp;
    int failed = 0;
    int i;

    SharedSlotVecInit(&before);
    XLipCollectComponentLanguages(&before);

    settings.cbSize = 176;
    (void)strcpy_s(settings.language, 0x55, GetSlotString(cultureSlot));
    comp = (F3SharedBuffer **)&g_dword_1DC2748;
    for (i = 7; i != 0; --i)
    {
        (void)strcpy_s(settings.name, 0x55, GetSlotString(comp));
        if (XLipApplyComponentSettings((char *)&settings) != 1)
            failed = 1;
        comp = comp + 5;
    }
    XLipBringUpComponents();

    SharedSlotVecInit(&after);
    XLipCollectComponentLanguages(&after);

    *changed = 0;
    {
        unsigned int countBefore =
            (unsigned int)((char *)before.end - (char *)before.begin) >> 2;
        unsigned int countAfter =
            (unsigned int)((char *)after.end - (char *)after.begin) >> 2;
        if (countBefore != countAfter)
        {
            *changed = 1;
        }
        else
        {
            for (i = 0; (unsigned int)i < countBefore; i++)
            {
                if (SharedSlotDiffers(&before.begin[i], &after.begin[i]) != 0)
                {
                    *changed = 1;
                    break;
                }
            }
        }
    }
    SharedSlotVecDtor(&after);
    SharedSlotVecDtor(&before);
    return (char)failed;
}

long InitLipCultures(void)
{
    F3SharedBuffer *cultureName = 0;   // "cultures_win32.txt" name slot (v23)
    F3SharedBuffer *path        = 0;   // settings dir + "\" (v19)
    F3SharedBuffer *fullPath    = 0;   // path + culture name (v20)
    F3SharedBuffer *textSlot    = 0;   // the "text" culture name slot (v21)
    char            textFlag    = 0;   // XLipBringUpCultures's changed flag

    (void)GetGameEngine();             // sub_619A90, result discarded
    g_dword_1DC2808 = (DWORD)(void (*)(void))LHAlloc;   // j_LHAlloc_1
    g_dword_1DC280C = (DWORD)(void (*)(void))LHFree;    // j_LHFree_3
    ReleaseLipCultures();
    SetLipMemAllocators((int)g_dword_1DC2808, (int)g_dword_1DC280C);
    InitLipRuntime();

    SetSharedSlot(&cultureName, "cultures_win32.txt", -1);
    MakeSettingsFilePath(&path, (F3SharedBuffer **)&g_dword_1DC2744, "\\");
    SharedSlotConcat(&fullPath, &path, &cultureName);
    int rc = XLipLoadCultures(GetSlotString(&fullPath));
    ReleaseSharedSlot(&fullPath);
    ReleaseSharedSlot(&path);
    if (rc < 0)
    {
        (void)GetLastError();
        MakeSettingsFilePath(&path, (F3SharedBuffer **)&g_dword_1DC2744, "\\cultures.txt");
        rc = XLipLoadCultures(GetSlotString(&path));
        ReleaseSharedSlot(&path);
        if (rc < 0)
            (void)GetLastError();
    }

    CreateLipFSRoot(GetSlotString((F3SharedBuffer **)&g_dword_1DC2744));

    F3SharedBuffer **comp = (F3SharedBuffer **)&g_dword_1DC2748;
    int i;
    for (i = 7; i != 0; --i)
    {
        const char *name = GetSlotString(comp);
        RegisterLipComponent(name, name);
        comp += 5;
    }
    XLipBringUpComponents();
    F3SharedBuffer **slot = (F3SharedBuffer **)&g_dword_1DC2748;
    for (i = 7; i != 0; --i)
    {
        ResolveLipComponentSlotPath(slot, slot + 3);
        slot += 5;
    }

    if (g_dword_1DC27EC != 0)
    {
        LHFree((void *)g_dword_1DC27EC);
        g_dword_1DC27EC = 0;
    }
    g_dword_1DC27F0 = 0;
    g_dword_1DC27F4 = 0;
    g_dword_1DC27F0 = (DWORD)XLipGetCultureCount(NULL);   // sub_17C0610
    {
        unsigned long long n = 96ULL * g_dword_1DC27F0;   // `mul`, overflow -> -1
        unsigned size = (n > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (unsigned)n;
        g_dword_1DC27EC = (DWORD)LHAlloc(size);
    }

    char name[85];
    memset(name, 0, sizeof(name));   // original: memset(0x54) + the 0x54 byte
    XLipGetComponentLanguage("text", name, 0x55);   // sub_17BFC20
    SetSharedSlot(&textSlot, name, -1);
    XLipBringUpCultures(&textSlot, &textFlag);   // sub_719540

    unsigned count = 0;
    if (g_dword_1DC27F0 != 0)
    {
        int off = 0;
        do
        {
            char *entry = (char *)(DWORD)g_dword_1DC27EC + off;
            XLipGetCultureName(0, (int)count, entry, 0x55);
            *(DWORD *)(entry + 88) = (DWORD)XLipGetCultureInfoPtr(entry, 1);
            if (strcmp(entry, GetSlotString(&textSlot)) == 0)
                g_dword_1DC27F4 = count;
            ++count;
            off += 96;
        }
        while (count < g_dword_1DC27F0);
    }

    g_byte_1DC2804 = 0;
    RegisterStandardAssets();
    ReleaseSharedSlot(&textSlot);
    return ReleaseSharedSlot(&cultureName);
}
