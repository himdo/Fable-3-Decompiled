// Fable 3 — frontend run module implementation (RunFrontEnd, 0x61CB70, plus its
// module-private globals, the xlive.dll / steam_api import stubs, and the
// unimplemented engine callees it drives).

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // original used strstr / the CRT directly
#endif

#include "gamemain.h"
#include "common/common.h"        // LHAlloc / LHFree / InitSharedSlot / ReleaseSharedSlot
#include "manager.h"              // GetManager
#include "netinit.h"              // InitNetworking
#include "save/save.h"            // SetSaveGamePath (the shared-slot string setter)
#include "worker.h"               // GetWorker

#include <windows.h>
#include <process.h>              // _beginthread / _getdrive / _chdrive
#include <wchar.h>                // _wgetcwd
#include <string.h>
#include <direct.h>               // _chdir / _mkdir

// ============================================================
//  Globals referenced by RunFrontEnd (kept at their original IDs)
// ============================================================
// Steam / frontend state flags.
static BYTE  g_byte_1DBDC49 = 0;   // 0x1DBDC49: "ThisIsSteam" data present
static BYTE  g_byte_1DBDC4A = 0;   // 0x1DBDC4A: SteamAPI initialized
static BYTE  g_byte_1DBD9E0 = 0;   // 0x1DBD9E0: frontend "entered" flag
// Locale IDs for the sign-in error / guide message boxes.
static DWORD g_dword_1DBDA90 = 0;  // 0x1DBDA90
static DWORD g_dword_1DBDA8C = 0;  // 0x1DBDA8C
static DWORD g_dword_1DBDA80 = 0;  // 0x1DBDA80
static DWORD g_dword_1DBDA84 = 0;  // 0x1DBDA84
static DWORD g_dword_1DBDA88 = 0;  // 0x1DBDA88
static DWORD g_dword_1DBDA7C = 0;  // 0x1DBDA7C
// Parsed command-line / locale words handed to the manager run call.
static DWORD g_dword_1C88DE4 = 0;  // 0x1C88DE4
static DWORD g_dword_1C88DE8 = 0;  // 0x1C88DE8
static DWORD g_dword_1C86BF0 = 0;  // 0x1C86BF0
static DWORD g_dword_1C86C08 = 0;  // 0x1C86C08
// Misc flags read / written by the frontend.
static BYTE  g_byte_1DBD030 = 0;   // 0x1DBD030
static BYTE  g_byte_1DBD034 = 0;   // 0x1DBD034: WaitBeforeEnteringFrontEnd on the command line
static BYTE  g_byte_1DBD03E = 0;   // 0x1DBD03E
static BYTE  g_byte_1DBD01F = 0;   // 0x1DBD01F
static BYTE  g_byte_2075F07 = 0;   // 0x2075F07
// Function-pointer slots published by the frontend (0x1D8C850 / 0x1D8C854).
static void *g_off_1D8C850 = 0;    // 0x1D8C850
static void *g_off_1D8C854 = 0;    // 0x1D8C854
// Global "this" objects the frontend dispatches through (0x1DBD048 / 0x1DBD9E8).
// The callees (stubs) never dereference them, so a zeroed block stands in.
static int   g_unk_1DBD048[8] = { 0 };   // 0x1DBD048
static int   g_unk_1DBD9E8[8] = { 0 };   // 0x1DBD9E8
// Placeholder vtables for the two file objects created on the frontend.
static void *g_CDiskFileWin32Vtable[4] = { 0 };
static void *g_CAFileVtable[4]         = { 0 };

// ============================================================
//  xlive.dll / steam_api import stubs
//
//  In the original these are imported from xlive.dll (the bundled XLiveAPI
//  shim) and steam_api. This self-contained build does not link either, so
//  they are stubbed here. XUserGetSigninState reports "signed in" so the
//  frontend takes the already-signed-in path and skips the blocking XNA
//  sign-in / guide / message-box UI loops.
// ============================================================
static int   XNotifyCreateListener(int a, int b) { (void)a; (void)b; return 1; }
static int   XNotifyGetNext(int listener, int a, void *hi, void *lo)
{ (void)listener; (void)a; (void)hi; (void)lo; return 0; }
static int   XUserGetSigninState(int user) { (void)user; return 3; }   // XUSER_STATE_SIGNED_IN
static int   XShowSigninUI(int a, int b) { (void)a; (void)b; return 0; }
static int   XShowGuideUI(int a) { (void)a; return 0; }
static int   XShowMessageBoxUI(int a, int b, int c, int d, void *e,
                               int f, int g, void *h, void *i)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i; return 6; }
static int   XCancelOverlapped(void *p) { (void)p; return 0; }
static int   SteamAPI_Init(void) { return 0; }

// ============================================================
//  Unimplemented engine callees (stubs)
//
//  Each is a minimal stand-in matching the original call's argument count so
//  the frontend's control flow, stack usage, and call sequence are preserved.
//  They are "remaining stubs": real per-function behavior is out of scope
//  until each is decompiled on its own.
// ============================================================
static void  sub_775EE0(void *obj) { (void)obj; }
static void  sub_72F7B0(void *obj) { (void)obj; }
static void  sub_775D10(void *str, char c) { (void)str; (void)c; }
static int   sub_775DA0(void *str, char c) { (void)str; (void)c; return 0; }

static int   g_sub61A000Buf[2] = { 0, 0 };
static int  *sub_61A000(void *obj) { (void)obj; return g_sub61A000Buf; }

static void  sub_67C930(void *obj) { (void)obj; }
static void  sub_18C5150(void *obj, void *a, int b, int c)
{ (void)obj; (void)a; (void)b; (void)c; }
static void  sub_18C4840(void *obj) { (void)obj; }

static int   sub_1A02560(void *obj, void *str) { (void)obj; (void)str; return 0; }
static void  sub_19FFA70(void *str) { (void)str; }

// ============================================================
//  Current-directory path cluster (0x18AE000-0x18C6000 helpers)
//
//  The engine keeps its "current directory" as a counted block of wide text
//  in a global slot (dword_2122DE4) with a live-wide-slot counter
//  (dword_2075C4C) sitting next to g_Addend (0x02075C48). The resolver chain
//  driven by sub_18B0F30 (sub_18AFB90 -> sub_18AF9D0 -> sub_18AEE10 /
//  sub_19FFAB0 / sub_18AE050) initializes and consults that global. The
//  functions below reproduce its observable effects.
//
//  Block conventions: a "wide" block's length field holds the wide char
//  count (its data holds 2*length bytes); an "ANSI" block's length holds
//  the byte count. In the original, a few InterlockedDecrement(slot+12)
//  sites land in the caller's stack frame (saved register / argument cells
//  holding large values, never <= 0, so no free ever fires); those stack
//  writes are not reproduced.
// ============================================================
static F3SharedBuffer *g_cwdBlock      = 0;  // 0x02122DE4: current dir (wide) block
static DWORD           g_dword_2075C4C = 0;  // 0x02075C4C: live-wide-slot counter

// ============================================================
//  sub_19FFB60  (0x19FFB60 - 0x19FFBD4)  -- fresh wide-text counted block
//  [NOT byte-identical] placement-dependent (relative call to LHAlloc).
//
//  F3SharedBuffer *SharedBlockFromWide(const WCHAR *src, int len)
//
//  Allocate a fresh 16-byte control block and a 2*len+pad wide buffer
//  (pre-filled with spaces, wide null terminator, the source wide string
//  copied in); length = wide char count (len or wcslen(src)), refcount 1
//  (0 on OOM). Builds the "wide" side of the CWD global.
// ============================================================
static F3SharedBuffer *SharedBlockFromWide(const WCHAR *src, int len)
{
    F3SharedBuffer *blk = (F3SharedBuffer *)LHAlloc(16);
    if (blk == 0)
        return 0;

    unsigned int n       = (len == -1) ? (unsigned int)wcslen(src) : (unsigned int)len;
    unsigned int aligned = (2 * n + 5) & 0xFFFFFFFCu;
    char *buf = (char *)LHAlloc(aligned);

    blk->data     = buf;
    blk->length   = n;
    blk->capacity = 0x7FFFFFFF;          // the original flips the capacity flag from 0
    if (buf != 0)
    {
        memset(buf, 0x20, aligned);                   // the original pre-fills with spaces
        if (2 * n + 2 <= aligned)
            ((WCHAR *)buf)[n] = 0;                    // wide terminator
        if (aligned > 2 * n + 4)
            memset(buf + 2 * n + 4, 0, aligned - (2 * n + 4));
        if (n > 0)
            memcpy(buf, src, 2 * n);
    }
    blk->refcount = 1;
    return blk;
}

// ============================================================
//  sub_18AEC00  (0x18AEC00 - 0x18AEC49)  -- fill a slot with the current dir
//  [NOT byte-identical] placement-dependent (relative calls to _wgetcwd and
//  the block builder).
//
//  F3SharedBuffer **CwdIntoSlot(F3SharedBuffer **slot)
//
//  _wgetcwd(buf, 259), then the sub_1A00310 slot constructor: null the slot,
//  bump g_dword_2075C4C, build a wide block from the CWD (when non-empty),
//  release any old block the slot held, store the new block, return the slot.
// ============================================================
static F3SharedBuffer **CwdIntoSlot(F3SharedBuffer **slot)
{
    WCHAR buf[259];

    *slot = 0;
    InterlockedIncrement(&g_dword_2075C4C);
    F3SharedBuffer *blk = 0;
    if (_wgetcwd(buf, 259) != 0 && buf[0] != 0)
        blk = SharedBlockFromWide(buf, -1);
    if (*slot != 0)
    {
        if (InterlockedDecrement(&(*slot)->refcount) <= 0 && *slot != 0)
            SharedBufferFree(*slot);
        *slot = 0;
    }
    if (blk != 0)
        *slot = blk;
    return slot;
}

// ============================================================
//  sub_19FFAB0 + sub_18AE050  -- the resolver's CWD-to-ANSI step
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc and
//  SharedBufferCreate).
//
//  F3SharedBuffer *CwdDirectoryBlock(F3SharedBuffer *cwdWide)
//
//  Build a fresh ANSI block holding the wide CWD converted to single bytes
//  (the low byte of each code unit, data[2*i] -- sub_19FFAB0's exact
//  wide->ANSI step) with a trailing backslash appended (sub_18AE050 appends
//  one when the text does not already end in '\\'). Refcount 1. This is the
//  resolved path the resolver stores in the local slot: the process CWD (a
//  directory), so GetFileAttributesA returns the directory attribute.
// ============================================================
static F3SharedBuffer *CwdDirectoryBlock(F3SharedBuffer *cwdWide)
{
    unsigned int len  = (cwdWide != 0) ? cwdWide->length : 0;
    const char *data  = (cwdWide != 0) ? cwdWide->data : 0;
    char *buf = (char *)LHAlloc(len + 2);
    if (buf == 0)
        return 0;
    for (unsigned int i = 0; i < len; i++)
        buf[i] = (data != 0) ? data[2 * i] : 0;
    unsigned int n = len;
    if (n == 0 || buf[n - 1] != '\\')
        buf[n++] = '\\';
    buf[n] = 0;
    F3SharedBuffer *out = SharedBufferCreate(buf, -1);   // refcount 1
    LHFree(buf);
    return out;
}

// ============================================================
//  sub_18AED30  (0x18AED30 - 0x18AEE0B)  -- normalized copy of a slot string
//  [NOT byte-identical] placement-dependent (relative calls to the aligned
//  allocator, memcpy_s and SetSharedSlot).
//
//  void NormalizedCopySlot(F3SharedBuffer **a1, F3SharedBuffer **a2)
//
//  Build a fresh block holding the source slot's C string with every '/'
//  replaced by '\\' (the whole buffer is scanned) and store it in a1 via
//  SetSharedSlot (g_Addend++, a1 = 0 for an empty source). An 8-byte stack
//  buffer stands in when the allocation fails (the original's memcpy_s then
//  clamps the copy; the clamped copy is reproduced).
// ============================================================
static void NormalizedCopySlot(F3SharedBuffer **a1, F3SharedBuffer **a2)
{
    F3SharedBuffer *src = (a2 != 0) ? *a2 : 0;
    unsigned int len = (src != 0) ? src->length : 0;

    char  stackBuf[8];
    char *buf = (char *)LHAlloc(len + 1);          // sub_405280 -> sub_403400
    char *dst = (buf != 0) ? buf : stackBuf;
    if (buf != 0)
        memset(buf, 0, len + 1);
    const char *str = (src != 0) ? src->data : "";
    unsigned int n = (buf != 0) ? len : ((len > 7u) ? 7u : len);
    if (n > 0)
        memcpy(dst, str, n);
    dst[n] = 0;
    for (unsigned int i = 0; i <= n; i++)          // normalize '/' -> '\\'
        if (dst[i] == '/')
            dst[i] = '\\';
    SetSharedSlot(a1, dst, -1);                    // g_Addend++; a1 = fresh block
    if (buf != 0)
        LHFree(buf);
}

// ============================================================
//  sub_18AFB90  (0x18AFB90 - 0x18AFBF1)  -- the path resolver
//  [NOT byte-identical] placement-dependent (relative calls to the whole
//  chain above and the InterlockedDecrement IAT slot) plus the high-VA
//  globals g_cwdBlock (0x02122DE4) and g_Addend (0x02075C48).
//
//  F3SharedBuffer **ResolvePathSlot(F3SharedBuffer **a1, F3SharedBuffer **a2)
//
//  Normalize the source string into a fresh block (a1, sub_18AED30), then
//  through sub_18AF9D0: initialize the global CWD block from the process CWD
//  when it is empty (sub_18AEE10 fill via sub_18AEC00), otherwise reference
//  it; convert the wide CWD to ANSI and append a trailing backslash
//  (sub_19FFAB0 + sub_18AE050) into a fresh block stored in a1. Because the
//  source slot (a2) is the same slot that sub_18AE050 clears first, the
//  source is lost and the resolved path is just the CWD (a directory); the
//  normalized copy built for a1 is abandoned (leaked) when a1 is re-pointed,
//  exactly as in the original. The source slot a2 is left unchanged (the
//  trailing sub_1A01270 is a self-assign no-op). Both the g_Addend and
//  g_dword_2075C4C counter traffic nets to zero here; only the caller's
//  failure path drops g_Addend (see SharedFileExists).
// ============================================================
static F3SharedBuffer **ResolvePathSlot(F3SharedBuffer **a1, F3SharedBuffer **a2)
{
    NormalizedCopySlot(a1, a2);              // sub_18AED30: g_Addend++ (abandoned below)

    F3SharedBuffer *g = g_cwdBlock;
    if (g == 0 || g->length == 0)
    {
        F3SharedBuffer *w = 0;
        CwdIntoSlot(&w);                     // sub_18AEC00: g_dword_2075C4C++
        if (w != g)
        {
            if (g != 0)
            {
                if (InterlockedDecrement(&g->refcount) <= 0)
                    SharedBufferFree(g);
            }
            g_cwdBlock = w;
        }
        g = g_cwdBlock;
        InterlockedDecrement(&g_dword_2075C4C);   // sub_18AEE10 fill tail
    }
    InterlockedIncrement(&g_dword_2075C4C);   // sub_18AEE10 (increment)

    // sub_19FFAB0 + sub_18AE050: fresh ANSI block of the CWD text (low byte of
    // each code unit) with a trailing backslash, stored in a1. When the CWD
    // fetch failed (g == 0) this is just "\\" (the current-dir root); both
    // resolve to a directory. The normalized copy a1 held is abandoned
    // (leaked), as in the original.
    *a1 = CwdDirectoryBlock(g);

    InterlockedDecrement(&g_dword_2075C4C);   // sub_18AF9D0 tail
    return a1;
}

// ============================================================
//  SharedFileExists  (0x18B0F30 - 0x18B0F98)  -- sub_18B0F30
//  [NOT byte-identical] placement-dependent (relative calls to the resolver,
//  ReleaseSharedSlot, SharedBufferFree and the GetFileAttributesA /
//  InterlockedDecrement IAT slots) plus the high-VA globals Str1 (the empty
//  string at 0x01BD44D7) and g_Addend (0x02075C48).
//
//  int SharedFileExists(F3SharedBuffer **obj)
//
//  "Is the (resolved) path of this shared slot an existing, non-directory
//  file?" The slot is resolved through the path resolver (sub_18AFB90), which
//  stores a fresh block holding the process CWD (as ANSI text, a trailing
//  backslash appended) in the local slot. The CWD is a directory, so
//  GetFileAttributesA returns FILE_ATTRIBUTE_DIRECTORY and the failure path
//  always runs: drop the local block's reference (freeing it at zero), drop
//  g_Addend, return 0. (The source slot is effectively ignored -- the resolver
//  clears it before re-deriving the path -- so this answers "is the current
//  working directory a regular file?", which is never true. The original's
//  InterlockedDecrement(slot+12) hits the caller's stack frame, a large value
//  that never triggers a free; not reproduced.) Callers: RunFrontEnd (the
//  "Data/ThisIsSteam" Steam probe and the abnormal_shutdown check), sub_67E030,
//  sub_1329EE0, SaveSettingsBeforeExit, sub_18F72B0.
// ============================================================
static int SharedFileExists(F3SharedBuffer **obj)
{
    F3SharedBuffer *local = 0;

    ResolvePathSlot(&local, obj);

    const char *path = (local != 0) ? local->data : "";
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        if (local != 0)
        {
            if (InterlockedDecrement(&local->refcount) <= 0 && local != 0)
                SharedBufferFree(local);
            local = 0;
        }
        InterlockedDecrement(&g_Addend);
        return 0;
    }
    ReleaseSharedSlot(&local);
    return 1;
}

// ============================================================
//  Path-directory utilities (the sub_18ABxxx / sub_18B07xx / sub_18B1xxx
//  cluster, plus the resolver's CWD-global helpers). The engine keeps the
//  process CWD in the global wide block g_cwdBlock (0x02122DE4) and its text
//  is converted to ANSI fresh each time (no trailing backslash unless the
//  resolver appends one). All functions below are [NOT byte-identical]:
//  placement-dependent (relative calls to the cluster above and the CRT /
//  KERNEL32 IAT slots) and/or they ref the high-VA globals g_Addend,
//  g_dword_2075C4C, g_cwdBlock and Str1 (the empty string, 0x01BD44D7).
// ============================================================

// ============================================================
//  ExtractPathComponent  (0x18ABC00 - 0x18ABC63)  -- one path component
//  [NOT byte-identical] (kept as a plain C++ loop; the original is a tight
//  byte-scan with no calls, but it is only reachable through the cluster
//  below, which is placement-dependent regardless).
//
//  char ExtractPathComponent(const char *path, int index, char *dest)
//
//  Copy the index-th (0-based) backslash-delimited component of path into
//  dest (the destination is large enough for the component; 260 in the one
//  caller). Leading backslash runs are skipped; runs of backslashes count as
//  a single separator. Returns 1 when a non-empty component was written,
//  0 when the path ends before the requested component (dest is then "").
// ============================================================
static char ExtractPathComponent(const char *path, int index, char *dest)
{
    const char *p = path;
    int depth = 0;

    while (*p == '\\')
        p++;

    if (index > 0)
    {
        for (;;)
        {
            char c = *p;
            if (c == 0)
            {
                dest[0] = 0;
                return 0;
            }
            if (c == '\\')
            {
                do { p++; } while (*p == '\\');
                if (++depth == index)
                    break;
            }
            else
                p++;
        }
    }

    int n = 0;
    for (;;)
    {
        char c = *p;
        if (c == 0 || c == '\\')
            break;
        dest[n++] = c;
        p++;
    }
    dest[n] = 0;
    return (n > 0) ? 1 : 0;
}

// ============================================================
//  NextPathComponent  (0x18ABE80 - 0x18ABEF3)  -- component N of a slot path
//  [NOT byte-identical] placement-dependent (relative call to
//  SetSaveGamePath) plus the high-VA empty-string global Str1.
//
//  char NextPathComponent(F3SharedBuffer **pathSlot, int index, F3SharedBuffer **comp)
//
//  Read the index-th component of the slot's C string into a 260-byte
//  buffer and store it in the comp slot via SetSaveGamePath (replacing, and
//  thus releasing, the block comp held). Returns 1 when the component is
//  non-empty, else 0 (comp is left unchanged).
// ============================================================
static char NextPathComponent(F3SharedBuffer **pathSlot, int index, F3SharedBuffer **comp)
{
    char source[260];
    const char *path = (pathSlot != 0 && *pathSlot != 0) ? (*pathSlot)->data : "";

    if (ExtractPathComponent(path, index, source) == 0)
        return 0;
    SetSaveGamePath(comp, source);
    return 1;
}

// ============================================================
//  RefCwdSlot  (0x18AEE10 - 0x18AEEE3)  -- reference the global CWD block
//  [NOT byte-identical] placement-dependent (relative calls to CwdIntoSlot /
//  the wide-block release) plus the high-VA globals g_cwdBlock and
//  g_dword_2075C4C.
//
//  F3SharedBuffer ** RefCwdSlot(F3SharedBuffer **slot)
//
//  Ensure the global CWD wide block (g_cwdBlock) is filled (CwdIntoSlot, the
//  sub_18AEC00 fill, when it is empty or null), then take a reference to it
//  in the local slot (refcount + 1, g_dword_2075C4C + 1). The caller drops
//  both when done. (The original's InterlockedDecrement(slot+12) on the
//  freshly filled block lands in the caller's stack frame and is not
//  reproduced, as in the resolver.)
// ============================================================
static F3SharedBuffer ** RefCwdSlot(F3SharedBuffer **slot)
{
    if (g_cwdBlock == 0 || g_cwdBlock->length == 0)
    {
        F3SharedBuffer *w = 0;
        CwdIntoSlot(&w);                     // sub_18AEC00: g_dword_2075C4C++
        g_cwdBlock = w;
        InterlockedDecrement(&g_dword_2075C4C);   // fill tail
    }
    *slot = 0;
    InterlockedIncrement(&g_dword_2075C4C);
    if (g_cwdBlock != 0)
    {
        *slot = g_cwdBlock;
        InterlockedIncrement(&g_cwdBlock->refcount);
    }
    return slot;
}

// ============================================================
//  AnsiFromWideSlot  (0x19FFAB0 - 0x19FFB51)  -- wide block to ANSI slot
//  [NOT byte-identical] placement-dependent (relative calls to the aligned
//  allocator, SetSharedSlot and the game-heap free) plus the high-VA heap
//  table (dword_2075B24).
//
//  F3SharedBuffer ** AnsiFromWideSlot(F3SharedBuffer *wide, F3SharedBuffer **out)
//
//  Build a fresh ANSI block holding the low byte of each wide code unit of
//  the source wide block (null-terminated, length + 1 bytes) and store it in
//  the out slot via SetSharedSlot (g_Addend++), freeing the temporary buffer.
//  On allocation failure the out slot is still set through SetSharedSlot with
//  no source (the slot is nulled, g_Addend bumped, no block).
// ============================================================
static F3SharedBuffer ** AnsiFromWideSlot(F3SharedBuffer *wide, F3SharedBuffer **out)
{
    unsigned int len  = (wide != 0) ? wide->length : 0;
    const char *data  = (wide != 0) ? wide->data : 0;
    char *buf = (char *)LHAlloc(len + 1);

    if (buf != 0)
    {
        memset(buf, 0, len + 1);
        for (unsigned int i = 0; i < len; i++)
            buf[i] = (data != 0) ? data[2 * i] : 0;
        SetSharedSlot(out, buf, -1);        // g_Addend++
        LHFree(buf);
    }
    else
        SetSharedSlot(out, 0, -1);          // OOM: the original still runs the setter
    return out;
}

// ============================================================
//  ChdirSlot  (0x18AFA80 - 0x18AFAFD)  -- chdir to a slot, update the global
//  [NOT byte-identical] placement-dependent (relative calls to _chdir, the
//  wide-block builder / release and SharedBufferFree) plus the high-VA
//  globals g_cwdBlock and g_dword_2075C4C.
//
//  void ChdirSlot(F3SharedBuffer **slot)
//
//  _chdir to the slot's C string (the empty string when the slot is empty),
//  then rebuild the global CWD wide block from that ANSI text (the original
//  builds a fresh wide block from the slot's bytes, releases the old global,
//  and re-points it); finally drop the slot's local reference and the live-
//  wide-slot counter (g_dword_2075C4C--).
// ============================================================
static void ChdirSlot(F3SharedBuffer **slot)
{
    F3SharedBuffer *blk = (slot != 0) ? *slot : 0;
    const char *path = (blk != 0) ? blk->data : "";

    _chdir(path);

    {
        unsigned int len = (blk != 0) ? blk->length : 0;
        WCHAR wide[520];
        const unsigned char *bytes = (blk != 0) ? (const unsigned char *)blk->data : 0;
        for (unsigned int i = 0; i < len; i++)
            wide[i] = (WCHAR)((bytes != 0) ? bytes[i] : 0);
        wide[len] = 0;
        F3SharedBuffer *nw = SharedBlockFromWide(wide, (int)len);
        if (g_cwdBlock != 0)
        {
            if (InterlockedDecrement(&g_cwdBlock->refcount) <= 0 && g_cwdBlock != 0)
                SharedBufferFree(g_cwdBlock);
            g_cwdBlock = 0;
        }
        if (nw != 0)
            g_cwdBlock = nw;
    }
    if (blk != 0)
    {
        if (InterlockedDecrement(&blk->refcount) <= 0 && blk != 0)
            SharedBufferFree(blk);
    }
    InterlockedDecrement(&g_dword_2075C4C);
}

// ============================================================
//  SplitPathIntoSlots  (0x18ABFA0 - 0x18AC060)  -- splitpath into slots
//  [NOT byte-identical] placement-dependent (relative call to _splitpath_s
//  and to SetSaveGamePath) plus the high-VA empty-string global Str1.
//
//  int SplitPathIntoSlots(F3SharedBuffer **path, F3SharedBuffer **driveOut,
//      F3SharedBuffer **dirOut, F3SharedBuffer **fileOut, F3SharedBuffer **extOut)
//
//  _splitpath_s over the slot's C string (the empty string when the slot is
//  empty) into 4 / 256 / 256 / 256 byte buffers; each requested out slot
//  receives its part via SetSaveGamePath (empty part leaves the slot null).
//  Returns the _splitpath_s status (always 0 on a valid path).
// ============================================================
static int SplitPathIntoSlots(F3SharedBuffer **path, F3SharedBuffer **driveOut,
                              F3SharedBuffer **dirOut, F3SharedBuffer **fileOut,
                              F3SharedBuffer **extOut)
{
    char drive[4];
    char dir[256];
    char file[256];
    char ext[256];
    const char *p = (path != 0 && *path != 0) ? (*path)->data : "";

    int rc = _splitpath_s(p, drive, 3, dir, 256, file, 256, ext, 256);   // the original's drive max
    if (driveOut != 0)
        SetSaveGamePath(driveOut, drive);
    if (dirOut != 0)
        SetSaveGamePath(dirOut, dir);
    if (fileOut != 0)
        SetSaveGamePath(fileOut, file);
    if (extOut != 0)
        SetSaveGamePath(extOut, ext);
    return rc;
}

// ============================================================
//  MakePathSlot  (0x18ABF00 - 0x18AC000)  -- makepath into a slot
//  [NOT byte-identical] placement-dependent (relative call to _makepath_s
//  and SetSharedSlot) plus the high-VA empty-string global Str1 and the
//  high-VA live-slot counter g_Addend.
//
//  F3SharedBuffer ** MakePathSlot(F3SharedBuffer **out, F3SharedBuffer **driveIn,
//      F3SharedBuffer **dirIn, F3SharedBuffer **fileIn, F3SharedBuffer **extIn)
//
//  _makepath_s over the four input slots' C strings (the empty string for an
//  empty / null slot) into a 260-byte buffer, stored in the out slot via
//  SetSharedSlot (g_Addend++). Returns the out slot.
// ============================================================
static F3SharedBuffer ** MakePathSlot(F3SharedBuffer **out, F3SharedBuffer **driveIn,
                                      F3SharedBuffer **dirIn, F3SharedBuffer **fileIn,
                                      F3SharedBuffer **extIn)
{
    char buf[260];

    const char *drv = (driveIn != 0 && *driveIn != 0) ? (*driveIn)->data : "";
    const char *dir = (dirIn   != 0 && *dirIn   != 0) ? (*dirIn)->data   : "";
    const char *fil = (fileIn  != 0 && *fileIn  != 0) ? (*fileIn)->data  : "";
    const char *ext = (extIn   != 0 && *extIn   != 0) ? (*extIn)->data   : "";

    _makepath_s(buf, 260, drv, dir, fil, ext);
    SetSharedSlot(out, buf, -1);            // g_Addend++
    return out;
}

// ============================================================
//  CreatePathTree  (0x18B0730 - 0x18B0969)  -- recursive directory creation
//  [NOT byte-identical] placement-dependent (relative calls to the CWD
//  helpers above, the resolver, the component reader and the CRT / KERNEL32
//  IAT slots) plus the high-VA globals g_Addend, g_dword_2075C4C, g_cwdBlock
//  and Str1 (the empty string, 0x01BD44D7).
//
//  int CreatePathTree(F3SharedBuffer **pathSlot)
//
//  "Create the directory tree of this path": resolve the path, take its drive
//  letter (component 0), switch to that drive, _chdir to the drive root, then
//  walk components 1..N with _chdir / _mkdir (create each directory that is
//  missing, relative to the previous one). Restores the previous drive, then
//  ChdirSlot back to the process CWD (updating the global CWD block) -- the
//  ANSI copy of the CWD taken before the walk is what it returns to. Returns
//  1 on success (also when the path has no directory components to create),
//  0 when a _mkdir / _chdir fails. Callers: EnsureParentDirectory, sub_BC9B50.
// ============================================================
static int CreatePathTree(F3SharedBuffer **pathSlot)
{
    int result     = 1;
    int savedDrive = 0;
    F3SharedBuffer *comp = 0;               // the current component slot

    F3SharedBuffer *cwdRef  = 0;            // the CWD wide block reference
    F3SharedBuffer *ansiCwd = 0;            // the ANSI copy of the CWD
    RefCwdSlot(&cwdRef);                    // sub_18AEE10
    AnsiFromWideSlot(cwdRef, &ansiCwd);     // sub_19FFAB0
    if (cwdRef != 0)
    {
        if (InterlockedDecrement(&cwdRef->refcount) <= 0)
            SharedBufferFree(cwdRef);
        cwdRef = 0;
    }
    InterlockedDecrement(&g_dword_2075C4C);

    F3SharedBuffer *resolved = 0;
    InterlockedIncrement(&g_Addend);
    ResolvePathSlot(&resolved, pathSlot);   // sub_18AFB90

    F3SharedBuffer *drive = 0;
    InterlockedIncrement(&g_Addend);
    SplitPathIntoSlots(&resolved, &drive, 0, 0, 0);   // sub_18ABFA0

    const char *drv = (drive != 0) ? drive->data : "";
    if (drv[0] == 0)
    {
        _chdrive(-1);                       // the original's ecx = -1 no-op
    }
    else
    {
        int letter = 0;
        if (drv[0] >= 'a' && drv[0] <= 'z')
            letter = drv[0] - 'a' + 1;
        else if (drv[0] >= 'A' && drv[0] <= 'Z')
            letter = drv[0] - 'A' + 1;

        savedDrive = _getdrive();
        _chdrive(letter);
        if (letter != 0)
        {
            _chdir("\\");
            int index = 1;                  // component 0 is the drive itself
            if (NextPathComponent(&resolved, index, &comp) != 0)
            {
                while (1)
                {
                    const char *c = (comp != 0) ? comp->data : "";
                    if (_chdir(c) != 0)
                    {
                        if (_mkdir(c) == -1)
                        {
                            result = 0;
                            goto restore;
                        }
                        if (_chdir(c) != 0)
                        {
                            result = 0;
                            goto restore;
                        }
                    }
                    index++;
                    if (NextPathComponent(&resolved, index, &comp) == 0)
                        break;
                }
            }
        }
    }

restore:
    if (savedDrive != 0)
        _chdrive(savedDrive);
    ChdirSlot(&ansiCwd);                    // sub_18AFA80: back to the process CWD

    if (drive != 0 && InterlockedDecrement(&drive->refcount) <= 0)
        SharedBufferFree(drive);
    InterlockedDecrement(&g_Addend);
    if (resolved != 0 && InterlockedDecrement(&resolved->refcount) <= 0)
        SharedBufferFree(resolved);
    InterlockedDecrement(&g_Addend);
    if (comp != 0 && InterlockedDecrement(&comp->refcount) <= 0)
        SharedBufferFree(comp);
    InterlockedDecrement(&g_Addend);
    if (ansiCwd != 0 && InterlockedDecrement(&ansiCwd->refcount) <= 0)
        SharedBufferFree(ansiCwd);
    InterlockedDecrement(&g_Addend);
    return result;
}

// ============================================================
//  SharedPathExists  (0x18B1000 - 0x18B104F)  -- resolved path exists
//  [NOT byte-identical] placement-dependent (relative calls to the resolver,
//  ReleaseSharedSlot and the GetFileAttributesA / KERNEL32 IAT slots) plus
//  the high-VA empty-string global Str1.
//
//  char SharedPathExists(F3SharedBuffer **slot)
//
//  "Does the (resolved) path of this slot exist, as either a file or a
//  directory?" Unlike SharedFileExists (0x18B0F30) it accepts directories:
//  an empty slot returns 0; otherwise the slot is resolved through the path
//  resolver into a local block (the source slot is left unchanged), and
//  GetFileAttributesA on its text (the empty string when the resolution
//  failed) decides: not -1 means it exists. The local block is released via
//  ReleaseSharedSlot (g_Addend--) on both outcomes; the source slot's own
//  counter is untouched.
// ============================================================
static char SharedPathExists(F3SharedBuffer **slot)
{
    if (slot == 0 || *slot == 0 || (*slot)->length == 0)
        return 0;

    F3SharedBuffer *local = 0;
    ResolvePathSlot(&local, slot);          // sub_18AFB90
    const char *path = (local != 0) ? local->data : "";

    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
    {
        ReleaseSharedSlot(&local);
        return 1;
    }
    ReleaseSharedSlot(&local);
    return 0;
}

// ============================================================
//  EnsureParentDirectory  (0x18B1360 - 0x18B14F5)  -- make the parent tree
//  [NOT byte-identical] placement-dependent (relative calls to the resolver,
//  SharedPathExists, the splitpath / makepath pair, CreatePathTree,
//  SharedBufferFree and the Interlocked IAT slots) plus the high-VA globals
//  g_Addend, g_cwdBlock / g_dword_2075C4C and the empty strings at
//  0x01BD44D7 (Str1) and 0x01BCE523.
//
//  int EnsureParentDirectory(F3SharedBuffer **pathSlot)
//
//  "Make sure the directory tree of this path exists": resolve the path into
//  a local slot, and -- when that resolved path does not exist (file or
//  directory) -- split it (drive + directory vs. filename + extension),
//  compose drive + directory back into a directory-path slot (the empty
//  string at 0x01BCE523 stands in for the filename / extension parts), and,
//  when that directory path itself does not exist, create it recursively via
//  CreatePathTree (the mkdir walk). All local slots are released (refcount +
//  g_Addend--) before returning. Returns 1 unless the directory walk failed.
//  Callers: RunFrontEnd (the abnormal-shutdown file's directory), sub_775DA0,
//  sub_BC9B50, sub_18C5150, ApplyDisplaySettings.
// ============================================================
static int EnsureParentDirectory(F3SharedBuffer **pathSlot)
{
    int result = 1;
    F3SharedBuffer *resolved = 0;

    ResolvePathSlot(&resolved, pathSlot);   // sub_18AFB90
    if (SharedPathExists(&resolved) == 0)   // sub_18B1000
    {
        F3SharedBuffer *drive   = 0;        // splitpath: drive part
        F3SharedBuffer *dir     = 0;        // splitpath: directory part
        F3SharedBuffer *file    = 0;        // empty (filename stand-in)
        F3SharedBuffer *ext     = 0;        // empty (extension stand-in)
        F3SharedBuffer *dirPath = 0;        // the composed directory path

        InitSharedSlot(&drive);             // null, g_Addend++
        InitSharedSlot(&dir);               // null, g_Addend++
        SplitPathIntoSlots(&resolved, &drive, &dir, 0, 0);   // sub_18ABFA0
        SetSharedSlot(&file, 0, -1);        // the empty string (0x01BCE523), g_Addend++
        SetSharedSlot(&ext,  0, -1);        // the empty string (0x01BCE523), g_Addend++
        MakePathSlot(&dirPath, &drive, &dir, &file, &ext);   // sub_18ABF00: g_Addend++

        if (ext != 0 && InterlockedDecrement(&ext->refcount) <= 0)
            SharedBufferFree(ext);
        InterlockedDecrement(&g_Addend);
        if (file != 0 && InterlockedDecrement(&file->refcount) <= 0)
            SharedBufferFree(file);
        InterlockedDecrement(&g_Addend);

        if (SharedPathExists(&dirPath) == 0)      // sub_18B1000
            result = CreatePathTree(&dirPath);    // sub_18B0730

        if (dirPath != 0 && InterlockedDecrement(&dirPath->refcount) <= 0)
            SharedBufferFree(dirPath);
        InterlockedDecrement(&g_Addend);
        if (dir != 0 && InterlockedDecrement(&dir->refcount) <= 0)
            SharedBufferFree(dir);
        InterlockedDecrement(&g_Addend);
        if (drive != 0 && InterlockedDecrement(&drive->refcount) <= 0)
            SharedBufferFree(drive);
        InterlockedDecrement(&g_Addend);
    }
    if (resolved != 0 && InterlockedDecrement(&resolved->refcount) <= 0)
        SharedBufferFree(resolved);
    InterlockedDecrement(&g_Addend);
    return result;
}

static void  sub_19FFA20(void *str) { (void)str; }
static int   sub_19FF560(void *str) { (void)str; return 0; }
static int   sub_18F02B0(void) { return 0; }

static int   g_sub18F1100Buf[2] = { 0, 0 };
static int  *sub_18F1100(void *obj) { (void)obj; return g_sub18F1100Buf; }

static void  sub_18E6D40(void) {}
static void  sub_619D70(void *p) { (void)p; }                     // _beginthread entry (no-op)
static void  sub_61C320(void *p) { (void)p; }                     // _beginthread entry (no-op)
static int   sub_6A66C0(void) { return 0; }
static int   sub_6A6960(int a) { (void)a; return 0; }
static int   sub_6A69A0(int a) { (void)a; return 0; }
static void  sub_772190(int a) { (void)a; }
static void  sub_61B090(void *obj) { (void)obj; }
static void  sub_18FBD00(int a) { (void)a; }
static void  nullsub_7(void) {}
static void  sub_770960(void) {}
static void  sub_620F30(void *obj) { (void)obj; }
static void *sub_18EADB0(void *obj) { return obj; }
static void  sub_18EB1A0(void *obj) { (void)obj; }
static void  sub_719770(void) {}
static int   sub_1801C20(void) { return 0; }
static void  sub_4358A0(void *obj, const char *name) { (void)obj; (void)name; }
static void  sub_1801580(void *obj, void *str) { (void)obj; (void)str; }
static void *sub_767360(void *obj) { return obj; }
static void *sub_762490(void *obj, void *a) { (void)a; return obj; }
static void  sub_6234A0(void *obj) { (void)obj; }
static void  sub_7672E0(void *obj) { (void)obj; }
static int   sub_760FA0(void *obj) { (void)obj; return 0; }
static void  sub_72EDF0(void *obj, int a) { (void)obj; (void)a; }
static void  sub_61A470(void *obj, int a) { (void)obj; (void)a; }
static void *sub_754DE0(void *obj) { return obj; }
static void  sub_627690(void *obj) { (void)obj; }
static void  sub_619990(void) {}
static void  sub_6199A0(void) {}
static void  sub_18FA3F0(void) {}
static void  sub_18C4560(void *obj, int a, int b) { (void)obj; (void)a; (void)b; }
static int   sub_18FA3E0(void *hwnd) { (void)hwnd; return 0; }
static int   sub_18FA3D0(int a) { (void)a; return 0; }
static void  sub_145D7B0(int a) { (void)a; }
static void  sub_741E50(int a) { (void)a; }

// Worker callback functions the frontend installs (their addresses are stored
// in the worker at +0x25C/+0x260/+0x264/+0x268 by the sub_18F6Cxx setters).
static void  sub_61A330(void) {}
static void  sub_619DE0(void) {}
static void  sub_6199C0(void) {}
static void  sub_619A70(void) {}

// Worker callback setters (thiscall, worker in ECX + one function pointer).
// The originals store the pointer at worker +0x25C/+0x260/+0x264/+0x268; the
// worker object in this build is smaller, so they are stubbed as no-ops.
static void  sub_18F6C90(void *worker, void *fn) { (void)worker; (void)fn; }
static void  sub_18F6C80(void *worker, void *fn) { (void)worker; (void)fn; }
static void  sub_18F6C70(void *worker, void *fn) { (void)worker; (void)fn; }
static void  sub_18F6C60(void *worker, void *fn) { (void)worker; (void)fn; }

static void  sub_402F00(void *obj, const char *name, int size)
{ (void)obj; (void)name; (void)size; }
static int   g_sub18013B0Word = 0;
static void *sub_18013B0(void *obj, void *a, void *b)
{ (void)obj; (void)a; (void)b; return &g_sub18013B0Word; }
static void  sub_18E8CD0(void) {}
static int   sub_18E8960(int a, int b) { (void)a; (void)b; return 0; }
static void  sub_18E8D50(void *obj, int a) { (void)obj; (void)a; }
static void  sub_18E7180(void) {}
static void  sub_18E84C0(void) {}
static void *sub_7330F0(void *obj) { return obj; }
static void *sub_72FB30(void *obj) { return obj; }
static void  sub_72FB10(void *obj, void *argList, void *a, void *b)
{ (void)obj; (void)argList; (void)a; (void)b; }
static void  nullsub_28(int a) { (void)a; }
static void  sub_70DDB0(int a) { (void)a; }
static void  sub_18F0180(void) {}
static void  sub_72FAE0(void *obj) { (void)obj; }
static void  sub_61BFE0(void *obj, int a) { (void)obj; (void)a; }
static void  sub_61A5E0(void *obj) { (void)obj; }
static void *sub_731B80(void *obj) { return obj; }
static void *sub_712430(void *obj) { return obj; }
static void  nullsub_29(void) {}
static void  sub_18FB930(void) {}
static void  sub_40E1F0(void *obj) { (void)obj; }

// F3_VT(OBJ): read OBJ's vtable (the first word) as a void ** so its slots
// can be indexed. (OBJ is a class instance pointer.)
#define F3_VT(OBJ) ((void **)(*(void **)(OBJ)))

// ============================================================
//  RunFrontEnd  (0x0061CB70 - 0x0061D7D6)  -- the game start-up / frontend
//  driver
//  [NOT byte-identical] placement-dependent (hundreds of relative calls to
//  engine callees, xlive.dll / steam_api imports, and Win32 functions) and
//  it references high-VA data globals / IAT slots.
//
//  char RunFrontEnd(void *obj, int arg)   (obj = the CGameEngine, 356 bytes)
//
//  thiscall + `retn 4` in the original: obj in ECX, one int on the stack
//  (the game-thread argument / command-line int), result byte in AL. Returns
//  1 on success (the frontend ran and the device / threads were started), 0
//  when the user cancelled sign-in / the frontend was aborted.
//
//  The control flow mirrors the disassembly: the Steam / XNA set-up, the
//  manager "run the game" dispatch, the XNA sign-in / guide / message-box UI
//  loop, the device + engine sub-object construction, and the shared cleanup
//  epilogue (success returns 1, cancel / failure returns 0).
// ============================================================
char RunFrontEnd(void *obj, int arg)
{
    char   *base = (char *)obj;
    DWORD  *dw   = (DWORD *)obj;

    // ---- locals (the original's stack frame) ----
    F3SharedBuffer *steamSlot = 0;          // the "Data/ThisIsSteam" string slot
    char            v108[100];              // sub_775EE0 object
    DWORD           v70[5];                 // sub_72F7B0 object
    DWORD           str103[4];              // the 16-byte string (v103..v106)
    DWORD           v107 = 0;               // the separate command-line word
    int             v84;                    // sub_61A000 slot
    F3SharedBuffer *v88 = 0;                // sub_67C930 / SharedFileExists slot
    DWORD           v93[6];                 // the CDiskFileWin32 / CAFile object
    F3SharedBuffer *v96;                    // InitSharedSlot / ReleaseSharedSlot slot
    DWORD           notify[2];              // the 8-byte XNotify buffer
    int             v76;                    // sub_1A02560 string
    int             v85, v86;               // sub_18F1100 strings
    DWORD           v91, v92, v89, v90;     // sign-in UI strings
    DWORD           v102[2];
    DWORD           v97[2];
    int             v98, v99, v100, v101;
    int             v72;                    // var_16D (game ran)
    int             v71;                    // var_16E (sign-in wait flag)
    int             v164;                   // sub_1801C20 result
    void           *v168 = 0;              // sub_18013B0 out-pointer
    char            v81[4];                 // "startup.vfsconfig" string
    int             vfs[4];                 // the 16-byte vfs config word block
    DWORD           modelHdr;               // "globals_model_headers" string
    int             v140, v154;             // sub_402F00 model-headers word block
    F3SharedBuffer *v160slot = 0;          // microsoft_logo.bik string slot
    char            result = 0;             // default: cancel / failure -> 0

    // 1. Framework / engine bootstrap.
    InitEngineTime();   // 0x18A89A0: capture the engine-start QPC counter / frequency
    InitNetworking();   // 0x6A7210: XLive init, pre-translate hook, CNetLiveSystem + CNetManager

    // 2. Ensure the XNA notification listener exists (obj+0xE8).
    if (dw[0xEA / 4] == 0)
        dw[0xEA / 4] = XNotifyCreateListener(3, 0);

    // 3. Probe the Steam data slot ("Data/ThisIsSteam").
    SetSharedSlot(&steamSlot, "Data/ThisIsSteam", -1);
    int steamOk = SharedFileExists(&steamSlot);
    ReleaseSharedSlot(&steamSlot);
    if (steamOk != 0)
    {
        g_byte_1DBDC49 = 1;
        if (SteamAPI_Init() != 0)
            g_byte_1DBDC4A = 1;
    }

    // 4. Build the small frontend strings.
    sub_775EE0(v108);
    sub_72F7B0(v70);
    sub_775D10(str103, (char)v70[0]);

    // 5. Parse the command-line / locale words.
    int *parsed = sub_61A000(&v84);
    g_dword_1C88DE4 = (DWORD)parsed[0];
    g_dword_1C88DE8 = (DWORD)parsed[1];
    if (((v107 >> 8) & 0xFF) == 0)   // BYTE1(v107)
    {
        str103[1] = (DWORD)parsed[0];
        str103[2] = (DWORD)parsed[1];
    }

    // 6. Build the file object + shared slot; show an error box on failure.
    sub_67C930(&v88);
    v93[0] = (DWORD)g_CDiskFileWin32Vtable;
    memset(&v93[1], 0, 20);
    InitSharedSlot(&v96);
    if (SharedFileExists(&v88) != 0)
    {
        void *mgr   = GetManager();
        void **vt   = (void **)*(void ***)mgr;
        const WCHAR *title = GetLocalizedString((const WCHAR ***)GetLocaleEntry(g_dword_1DBDA90));
        const WCHAR *body  = GetLocalizedString((const WCHAR ***)GetLocaleEntry(g_dword_1DBDA8C));
        HWND hwnd = (HWND)((int (*)(void *))vt[8])(mgr);
        MessageBoxW(hwnd, body, title, 0x30);   // MB_OK | MB_ICONINFORMATION
    }
    else
    {
        EnsureParentDirectory(&v88);
        sub_18C5150(v93, &v88, 4, 2);
        sub_18C4840(v93);
    }

    v72 = 0;

    // 7. Dispatch the manager "run the game" vtable slot.
    {
        void *mgr  = GetManager();
        void **vt  = (void **)*(void ***)mgr;
        int   v13  = sub_1A02560(&g_unk_1DBD048, &v76);
        int   r    =
            ((int (*)(void *, int, int, int, int, int, int, int, int))vt[1])(
                mgr, (int)v107, (int)str103[1], (int)str103[2], (int)str103[0], arg,
                (int)g_dword_1C86BF0, (int)g_dword_1C86C08, v13);
        sub_19FFA70(&v76);
        if (r != 0)
        {
            int *p = sub_18F1100(&v85);
            if ((unsigned char)str103[3] != (unsigned char)sub_18F02B0()
                || (int)str103[1] != p[0]
                || (int)str103[2] != p[1])
            {
                str103[3] = (DWORD)sub_18F02B0();
                int *p2 = sub_18F1100(&v86);
                str103[1] = (DWORD)p2[0];
                str103[2] = (DWORD)p2[1];
                base[0x160] = 1;
                sub_72F7B0(v70);
                sub_775DA0(str103, (char)v70[0]);
            }
            sub_18E6D40();
            v72 = 1;
        }
    }

    // 8. Start the pump thread, set the cursor, and handle XNA sign-in.
    g_byte_1DBD9E0 = 1;
    _beginthread(sub_619D70, 0, 0);
    SetCursor(LoadCursorA(0, (LPCSTR)0x7F00));
    v71 = 1;
    if (XUserGetSigninState(0) == 0)
    {
        void *mgr = GetManager();
        void **vt = (void **)*(void ***)mgr;
        ((void (*)(void *, int))vt[12])(mgr, 1);
        XShowSigninUI(1, 0);
        v71 = 0;
    }

    notify[0] = 0;
    notify[1] = 0;
    while (v71 == 0)
    {
        if (XNotifyGetNext(dw[0xEA / 4], 0, &notify[1], &notify[0]) != 0
            && notify[1] == 9 && notify[0] == 0)   // 0x0000000900000000
            v71 = 1;
        Sleep(1);
    }

    // 9. The XNA sign-in / guide / message-box UI loop (skipped when signed in).
    if (XUserGetSigninState(0) == 0)
    {
        int cancelled = 0;
        while (1)
        {
            void *mgr = GetManager();
            void **vt = (void **)*(void ***)mgr;
            ((void (*)(void *, int))vt[12])(mgr, 1);
            sub_19FFA20(GetLocaleEntry(g_dword_1DBDA80));
            sub_19FFA20(GetLocaleEntry(g_dword_1DBDA84));
            sub_19FFA20(GetLocaleEntry(g_dword_1DBDA88));
            sub_19FFA20(GetLocaleEntry(g_dword_1DBDA7C));
            v102[0] = (DWORD)sub_19FF560(&v91);
            v102[1] = (DWORD)sub_19FF560(&v92);
            v98 = v99 = v100 = v101 = 0;
            v97[0] = v97[1] = 0;

            int code = 5;
            do
            {
                v70[0] = (DWORD)&v98;
                int a = sub_19FF560(&v89);
                int b = sub_19FF560(&v90);
                code = XShowMessageBoxUI(0, b, a, 2, v102, 0, 3, v97, &v98);
                if (code == 5)
                {
                    XCancelOverlapped(&v98);
                    v98 = v99 = v100 = v101 = 0;
                }
                Sleep(1);
            } while (code == 5);

            while ((DWORD)v98 == 997)
                Sleep(1);

            if (v97[0] == 1)
            {
                cancelled = 1;
                break;
            }

            v71 = 0;
            do
            {
                if (XNotifyGetNext(dw[0xEA / 4], 0, &notify[1], &notify[0]) != 0
                    && notify[1] == 9 && notify[0] == 0)
                    v71 = 1;
                Sleep(1);
            } while (v71 == 0);

            while (XShowGuideUI(0) != 0)
                Sleep(1);

            v71 = 0;
            do
            {
                if (XNotifyGetNext(dw[0xEA / 4], 0, &notify[1], &notify[0]) != 0
                    && notify[1] == 9 && notify[0] == 0)
                    v71 = 1;
                if (XUserGetSigninState(0) != 0)
                    break;
                Sleep(1);
            } while (v71 == 0);

            int signState = XUserGetSigninState(0);
            Sleep(1);
            sub_19FFA70(&v91);
            sub_19FFA70(&v89);
            sub_19FFA70(&v90);
            sub_19FFA70(&v92);

            if (signState != 0)
                goto signedIn;   // LABEL_46: the user signed in mid-loop
        }

        // Cancelled: tear down the file object, post WM_CLOSE, return 0.
        (void)cancelled;
        g_byte_1DBD9E0 = 0;
        sub_18C4560(&g_unk_1DBD9E8, -1, 0);
        {
            void *mgr = GetManager();
            void **vt = (void **)*(void ***)mgr;
            HWND hwnd = (HWND)((int (*)(void *))vt[8])(mgr);
            PostMessageA(hwnd, 0x10, 0, 0);   // WM_CLOSE
        }
        sub_19FFA70(&v91);
        sub_19FFA70(&v89);
        sub_19FFA70(&v90);
        sub_19FFA70(&v92);
        v93[0] = (DWORD)g_CDiskFileWin32Vtable;
        sub_18C4840(v93);
        goto epilogue;
    }

signedIn:
    // 10. The signed-in continuation: front-end data + device construction.
    {
        int a = sub_6A66C0();
        int b = sub_6A6960(a);
        sub_772190(b);
        sub_61B090(obj);
        sub_18FBD00(g_byte_1DBD030 != 0);
        nullsub_7();
        sub_770960();

        const char *cmd = GetCommandLineA();
        g_byte_1DBD034 = (strstr(cmd, "WaitBeforeEnteringFrontEnd") != 0);
        base[0x24] = 0;
        sub_620F30(obj);

        // Copy the node-list pointers (heapNode -> +0x1C, listFirst -> +0x20).
        int heapNode  = (int)dw[0];
        int listFirst = (int)dw[0x14 / 4];
        dw[0x1C / 4] = (DWORD)heapNode;
        dw[0x20 / 4] = (DWORD)listFirst;

        void *p56 = LHAlloc(0x38);
        int   v35 = (p56 != 0) ? (int)sub_18EADB0(p56) : 0;
        void *v36 = (void *)dw[0x98 / 4];
        if (v36 != 0)
        {
            sub_18EB1A0(v36);
            LHFree(v36);
        }
        dw[0x98 / 4] = (DWORD)v35;

        sub_719770();
        v164 = sub_1801C20();
        sub_4358A0(&v81[0], "startup.vfsconfig");
        memset(vfs, 0, sizeof(vfs));
        sub_1801580((void *)v164, &v81[0]);
        if (vfs[3] >= 8)
            LHFree((void *)vfs[0]);

        vfs[0] = vfs[1] = vfs[2] = 0;
        vfs[3] = 7;
        void *p48 = LHAlloc(0x30);
        int   v39 = (p48 != 0) ? (int)sub_767360(p48) : 0;

        void *v40 = (void *)dw[0xAC / 4];
        if (v40 != 0)
            ((void (*)(void *, int))F3_VT(v40)[0])(v40, 1);
        dw[0xAC / 4] = (DWORD)v39;

        void *p288 = LHAlloc(0x120);
        int   v37 = (p288 != 0) ? (int)sub_762490(p288, (void *)dw[0xAC / 4]) : 0;

        void *v41 = (void *)dw[0xB0 / 4];
        if (v41 != 0)
        {
            sub_6234A0(v41);
            LHFree(v41);
        }
        dw[0xB0 / 4] = (DWORD)v37;

        sub_7672E0((void *)dw[0xAC / 4]);
        int v42 = sub_760FA0((void *)dw[0xB0 / 4]);
        sub_72EDF0(base + 0xEC, v42);
        sub_61A470(obj, 0);

        void *p736 = LHAlloc(0x2E0);
        int   v44 = (p736 != 0) ? (int)sub_754DE0(p736) : 0;
        void *v45 = (void *)dw[0xA8 / 4];
        if (v45 != 0)
        {
            sub_627690(v45);
            LHFree(v45);
        }
        dw[0xA8 / 4] = (DWORD)v44;

        g_off_1D8C850 = (void *)sub_619990;
        g_off_1D8C854 = (void *)sub_6199A0;
        sub_18FA3F0();

        g_byte_1DBD9E0 = 0;
        sub_18C4560(&g_unk_1DBD9E8, -1, 0);
    }

    if (v72 == 0)
        goto epilogue;

    // 11. The device-created / worker-callback / thread-start block.
    {
        void *mgr = GetManager();
        void **vt = (void **)*(void ***)mgr;
        ((void (*)(void *, int))vt[10])(mgr, (unsigned char)g_byte_1DBD01F);

        HWND hwnd = (HWND)((int (*)(void *))vt[8])(GetManager());
        int a = sub_18FA3E0(hwnd);
        int b = sub_18FA3D0(a);
        sub_145D7B0(b);
        int c = sub_6A66C0();
        int d = sub_6A69A0(c);
        sub_741E50(d);

        sub_18F6C90(GetWorker(), (void *)sub_61A330);
        sub_18F6C80(GetWorker(), (void *)sub_619DE0);
        sub_18F6C70(GetWorker(), (void *)sub_6199C0);
        sub_18F6C60(GetWorker(), (void *)sub_619A70);

        g_byte_2075F07 = 0;
        v140 = 0xF;
        v154 = 0;
        sub_402F00(&modelHdr, "globals_model_headers", 0x15);
        void *v52 = sub_18013B0((void *)v164, &modelHdr, &v168);
        void *v52obj = *(void **)v52;
        if (v52obj != 0)
            ((void (*)(void *))F3_VT(v52obj)[1])(v52obj);
        void *v53 = (void *)dw[0x7C / 4];
        if (v53 != 0)
            ((void (*)(void *))F3_VT(v53)[2])(v53);
        dw[0x7C / 4] = (DWORD)v52obj;
        if (v168 != 0)
            ((void (*)(void *))F3_VT(v168)[2])(v168);
        if (v140 >= 0x10)
            LHFree((void *)v154);

        sub_18E8CD0();
        int e = sub_18E8960(0, 0);
        sub_18E8D50((void *)e, 0);
        sub_18E7180();
        sub_18E84C0();

        void *p88 = LHAlloc(0x58);
        int   v55 = (p88 != 0) ? (int)sub_7330F0(p88) : 0;
        void *v56 = (void *)dw[0x28 / 4];
        if (v56 != 0)
            ((void (*)(void *, int))F3_VT(v56)[0])(v56, 1);
        dw[0x28 / 4] = (DWORD)v55;

        void *p8 = LHAlloc(8);
        int   v58 = (p8 != 0) ? (int)sub_72FB30(p8) : 0;
        void *v59 = (void *)dw[0x9C / 4];
        if (v59 != 0)
            ((void (*)(void *, int))F3_VT(v59)[0])(v59, 1);
        dw[0x9C / 4] = (DWORD)v58;
        sub_72FB10((void *)v58, obj, v108, str103);

        nullsub_28((int)dw[0xBC / 4]);
        sub_70DDB0((int)dw[0xBC / 4]);
        sub_18F0180();
        sub_72FAE0((void *)dw[0x9C / 4]);

        int fileId = (int)SetSharedSlot(&v160slot, "data\\art\\videos\\microsoft_logo.bik", -1);
        sub_61BFE0(obj, fileId);
        ReleaseSharedSlot(&v160slot);
        sub_61A5E0(obj);

        base[0x84] = 0;
        dw[0x80 / 4] = (DWORD)_beginthread(sub_61C320, 0x40000u, obj);

        void *p8b = LHAlloc(8);
        int   v62 = (p8b != 0) ? (int)sub_731B80(p8b) : 0;
        void *v63 = (void *)dw[0x2C / 4];
        if (v63 != 0)
            ((void (*)(void *, int))F3_VT(v63)[0])(v63, 1);
        dw[0x2C / 4] = (DWORD)v62;

        void *p8c = LHAlloc(8);
        int   v64 = (p8c != 0) ? (int)sub_712430(p8c) : 0;
        volatile LONG **v65 = (volatile LONG **)(void *)dw[0xB4 / 4];
        if (v65 != 0)
        {
            if (v65[1] != 0 && InterlockedDecrement(v65[1]) == 0)
            {
                ((void (*)(int))F3_VT(v65[1])[1])((int)(*(void **)((char *)v65[1] + 8)));
                LHFree((void *)v65[1]);
            }
            *v65   = 0;
            v65[1] = 0;
            LHFree((void *)v65);
        }
        dw[0xB4 / 4] = (DWORD)v64;

        if (g_byte_1DBD03E != 0)
            nullsub_29();
        sub_18FB930();
    }

    // Success: the frontend ran and the device / threads were started.
    result = 1;

epilogue:
    // Shared cleanup epilogue (identical on the success and cancel / failure
    // paths; only the return value differs).
    v93[0] = (DWORD)g_CDiskFileWin32Vtable;
    sub_18C4840(v93);
    ReleaseSharedSlot(&v96);
    v93[0] = (DWORD)g_CAFileVtable;
    ReleaseSharedSlot(&v88);
    sub_40E1F0(v108);
    return result;
}
