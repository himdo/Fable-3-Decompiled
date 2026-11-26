// Fable 3 — frontend run module implementation (RunFrontEnd, 0x61CB70, plus its
// module-private globals, the steam_api dynamic import (the xlive.dll
// functions are real link-time imports via engine/xlive.h), and the
// unimplemented engine callees it drives).

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // original used strstr / the CRT directly
#endif

#include <stdio.h>
#include "gamemain.h"
#include "luavm.h"                // ReinitEngineLua (the engine Lua env rebuild)
#include "xlip.h"                 // F3AString + the XLip module (engine/xlip.h)
#include "banktree.h"             // BankTreeEraseRange (the bank-tree range erase)
#include "namemap.h"              // NameMapEraseRange (the +0x94 name-map range erase)
                                    // / InitNameMapTree (the +0x94 name-map tree-object init)
#include "subnode.h"              // SubNodeEraseRange / InitSubNodeTree (the +0x74 SubNode tree)
#include "common/common.h"        // LHAlloc / LHFree / JmpLHAllocAligned / LHFreeAligned / JmpLHFreeAligned /
// InitSharedSlot / ReleaseSharedSlot
#include "engine/engine.h"        // SetThreadName (the thread-name announce)
#include "gameengine.h"           // GetGameEngine (sub_619A90)
#include "gamethread.h"           // CNetSaveGameCtor/Dtor (the 0x2E0-byte co-op net object ctor/dtor)
#include "manager.h"              // GetManager
#include "netinit.h"              // InitNetworking
#include "save/save.h"            // SetSaveGamePath (the shared-slot string setter)
#include "worker.h"               // GetWorker
#include "xlive.h"                // the xlive.dll (XLiveAPI) import declarations

#include <windows.h>
#include <objbase.h>              // CoInitializeEx / CoCreateInstance (the LHVFS COM bring-up)
#include <process.h>              // _beginthread / _getdrive / _chdrive
#include <wchar.h>                // _wgetcwd
#include <stdarg.h>               // va_list / va_start / vswprintf_s (XLipDebugMessage)
#include <string.h>
#include <ctype.h>                // tolower (the shared-slot lowercase pass)
#include <setjmp.h>               // longjmp / jmp_buf (the VM-heap OOM expand)
#include <wctype.h>               // iswspace (the XLipCultures line trim)
#include <mbstring.h>             // _mbscmp / _mbsicmp
#include <stdlib.h>               // _atoi64 (the int64 value reader)
#include <math.h>                 // fabsf
#include <float.h>                // _controlfp (the FPU round-toward-zero in KoreHeapInitGrow)
#include <intrin.h>               // __rdtsc (the QPC span accounting in KoreHeapInitGrow)
#include <time.h>                  // clock() / clock_t (the VfsQueryTableBuild timeout check)
#include <direct.h>               // _chdir / _mkdir
#include <stdexcept>              // std::length_error (F3ListSizeAdd overflow path)

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
static BYTE  g_byte_1DBD02A = 0;   // 0x1DBD02A: run-from-disk flag (data/streaming.bnk or FakeRunFromDVD)
static F3SharedBuffer *g_unk_1DBDA78 = 0;   // 0x1DBDA78: build-version content slot
static BYTE  g_byte_2075F07 = 0;   // 0x2075F07
static BYTE  g_byte_2075F2B = 0;   // 0x2075F2B: engine-started flag
static BYTE  g_byte_2075F38 = 0;   // 0x2075F38: Texture Streamer running flag
static BYTE  g_byte_2075F39 = 0;   // 0x2075F39: Texture Streamer stop-arm flag
static CRITICAL_SECTION g_cs_20FD1B4;   // 0x20FD1B4: the Texture Streamer critical section (zeroed)
static int   g_cs_20FD1B4Init = 0;
static BYTE  g_byte_2075F4E = 1;   // 0x2075F4E: path-normalize enable flag
static BYTE  g_byte_2075F28 = 0;   // 0x2075F28: one-shot global-init flag
// Function-pointer slots published by the frontend (0x1D8C850 / 0x1D8C854).
static void *g_off_1D8C850 = 0;    // 0x1D8C850
static void *g_off_1D8C854 = 0;    // 0x1D8C854
// 0x1DBD048 (the ApplicationName slot) is the engine-wide shared global
// g_slot_1DBD048 (common/common.cpp), read below to build the worker window
// title.  0x1DBD9E8 (the frontend object) still needs a zeroed placeholder:
// the callees (stubs) never dereference it, so a block stands in.
// 0x1DBD9E8: the frontend object.  The original constructor (EventObjCtor,
// 0x18C46E0, thiscall + retn 8, invoked at registration by sub_1A5C6A0) stores a
// CreateEventA handle at +0x80; WaitForEvent / SetFrontendEvent / the
// CloseHandle sibling all read that slot.  It is not created in this build,
// so the 0x84-byte block stays zeroed: SetEvent(0) is a no-op failure and
// WaitForSingleObjectEx(0, ...) returns WAIT_FAILED immediately.
static int   g_unk_1DBD9E8[33] = { 0 };  // 0x1DBD9E8 (0x84 bytes)
// Placeholder vtables for the two file objects created on the frontend.
static void *g_CDiskFileWin32Vtable[4] = { 0 };
static void *g_CAFileVtable[4]         = { 0 };
static void *g_CLegacyFileVtable[4]    = { 0 };

// F3_VT(OBJ): read OBJ's vtable (the first word) as a void ** so its slots
// can be indexed. (OBJ is a class instance pointer.)
#define F3_VT(OBJ) ((void **)(*(void **)(OBJ)))

// ============================================================
//  xlive.dll / steam_api imports
//
//  The xlive.dll functions (XNotifyCreateListener, XNotifyGetNext,
//  XUserGetSigninState, XShowSigninUI, XShowGuideUI, XShowMessageBoxUI,
//  XCancelOverlapped) are now the real link-time imports from the bundled
//  xlive.dll (XLiveAPI) -- see engine/xlive.{h,def}: the build links an
//  import library generated from xlive.def (lib /def -> xlive.lib). XUserGetSigninState reports the XLiveAPI sign-in state (user 0
//  signed in by default), so the frontend normally takes the
//  already-signed-in path and skips the blocking XNA sign-in / guide /
//  message-box UI loops. steam_api is resolved dynamically below.
// ============================================================

//  SteamAPI_Init  (0x17DA5EA - 0x17DA5F0)  -- steam_api.dll import thunk
//
//  In the original this is a 6-byte MSVC import thunk, not an in-image body:
//      FF 25 C8 55 AD 01   ; jmp ds:__imp_SteamAPI_Init   (steam_api.dll)
//  The real implementation lives in steam_api.dll (the bundled Steamworks
//  proxy) and returns bool (1 = Steam client initialized, 0 = not). RunFrontEnd
//  calls it only after confirming "Data/ThisIsSteam" is present and, on a
//  nonzero return, sets byte_1DBDC4A (the "SteamAPI initialized" flag).
//
//  This self-contained build does not link steam_api.dll, so the import is
//  resolved dynamically: when the DLL is present the real call is forwarded
//  (matching the thunk's target); when it is not (standalone test / no Steam)
//  the init is reported as failed (0), leaving byte_1DBDC4A clear so the
//  frontend takes the "no Steam" path. The sibling steam_api imports
//  (SteamFriends 0x17DA5F0, SteamUtils 0x17DA5F6) are not referenced by any
//  implemented caller yet.
//
//  [NOT byte-identical] -- it is a steam_api.dll import thunk with no
//  in-image body; the self-contained build has no steam_api IAT slot to jump
//  to, so the original `jmp ds:__imp_SteamAPI_Init` cannot be reproduced.
static int SteamAPI_Init(void)
{
    typedef int (__cdecl *SteamAPI_Init_f)(void);   // bool SteamAPI_Init(void)
    static HMODULE hSteam = (HMODULE)LoadLibraryA("steam_api.dll");
    static SteamAPI_Init_f pInit =
        hSteam ? (SteamAPI_Init_f)GetProcAddress(hSteam, "SteamAPI_Init") : 0;
    if (!pInit)                                    // steam_api.dll unavailable
        return 0;                                  // -> Steam init failed
    return pInit() ? 1 : 0;
}

// ============================================================
//  Unimplemented engine callees (stubs)
//
//  Each is a minimal stand-in matching the original call's argument count so
//  the frontend's control flow, stack usage, and call sequence are preserved.
//  They are "remaining stubs": real per-function behavior is out of scope
//  until each is decompiled on its own.
// ============================================================
// (sub_775DA0, the saved-video-options file writer, is implemented below near
//  LoadVideoOptionsValues as SaveVideoOptionsFile -- no longer a stub.)

// ============================================================
//  CDiskFileWin32 disk-file object + Open (the sub_18C5xxx cluster).
//
//  The game's Win32 disk-file class (LHFile::CDiskFileWin32). The 28-byte
//  object stores the vtable, the open mode, the file position / size, the
//  HANDLE, an is-open flag, and the filename C string (at +0x18). The Open
//  method (CDiskFileWin32Open, 0x18C5150) resolves the path, calls CreateFileA with the
//  mode-dependent access / share / disposition, and on success fills in the
//  HANDLE / size / position / is-open flag. The two constructors
//  (sub_18C5450 stack, sub_18C5490 heap) zero the object, set the vtable,
//  bump g_Addend, and forward to Open.
// ============================================================

// Forward declarations (defined later in this file).
static void DetachSharedSlot(F3SharedBuffer **slot);
void AppendSharedSlot(F3SharedBuffer **slot, const char *src);   // sub_1A01880
static F3SharedBuffer **ResolvePathSlot(F3SharedBuffer **a1, F3SharedBuffer **a2);
static int SplitPathIntoSlots(F3SharedBuffer **path, F3SharedBuffer **driveOut,
                              F3SharedBuffer **dirOut, F3SharedBuffer **fileOut,
                              F3SharedBuffer **extOut);
static int EnsureParentDirectory(F3SharedBuffer **pathSlot);

struct CDiskFileWin32
{
    const void *      vtable;      // +0x00
    int               mode;        // +0x04
    DWORD             position;    // +0x08
    DWORD             size;        // +0x0C
    HANDLE            handle;      // +0x10
    DWORD             openFlag;    // +0x14 (byte 0 = is-open)
    const char *      fileName;    // +0x18 (the C string filename)
};

// sub_1A01270 (0x1A01270 - 0x1A012B7): copy-assign one shared slot onto
//  another with refcounting. this / a2 are both slot addresses (the address
//  of an F3SharedBuffer * field). Releases the old block this held
//  (SharedBufferFree at zero refcount, then null), copies the source block
//  pointer in, and increments the new block's refcount. A self-assign is a
//  no-op. [NOT byte-identical] placement-independent but refs the Interlocked
//  IAT slots and SharedBufferFree.
F3SharedBuffer **CopySharedSlot(F3SharedBuffer **thisSlot, F3SharedBuffer **srcSlot)
{
    F3SharedBuffer *old = *thisSlot;
    F3SharedBuffer *src = (srcSlot != 0) ? *srcSlot : 0;
    if (thisSlot == srcSlot || old == src)
        return thisSlot;
    if (old != 0)
    {
        if (InterlockedDecrement(&old->refcount) <= 0 && *thisSlot != 0)
            SharedBufferFree(*thisSlot);
        *thisSlot = 0;
    }
    if (src != 0)
    {
        *thisSlot = src;
        InterlockedIncrement(&src->refcount);
    }
    return thisSlot;
}

// sub_1A01940 (0x1A01940 - 0x1A019A1): append (or copy) one slot's C string
//  onto another. When thisSlot already holds a block, DetachSharedSlot makes
//  a private copy and AppendSharedSlot appends the source C string. When
//  thisSlot is empty, the source block is copied (with a refcount bump) and
//  the source is left untouched. [NOT byte-identical] (placement-dependent
//  relative calls to DetachSharedSlot / AppendSharedSlot + the Interlocked IAT).
static F3SharedBuffer **AppendStringSlot(F3SharedBuffer **thisSlot, F3SharedBuffer **srcSlot)
{
    if (*thisSlot != 0)
    {
        DetachSharedSlot(thisSlot);
        const char *src = (srcSlot != 0 && *srcSlot != 0) ? (*srcSlot)->data : "";
        AppendSharedSlot(thisSlot, src);
        return thisSlot;
    }
    F3SharedBuffer *src = (srcSlot != 0) ? *srcSlot : 0;
    if (src != 0 && thisSlot != srcSlot)
    {
        *thisSlot = src;
        InterlockedIncrement(&src->refcount);
    }
    return thisSlot;
}

// sub_18AFC00 (0x18AFC00 - 0x18AFC8E): resolve + split the path (the
//  "normalize" step). Gated by g_byte_2075F4E (skips entirely when 0). When
//  active: bump g_Addend, resolve the path through the resolver (sub_18AF9D0,
//  encapsulated in ResolvePathSlot), split the resolved path into its
//  components (SplitPathIntoSlots, the drive extracted into a local), then
//  release both the resolved and drive blocks (SharedBufferFree at zero
//  refcount) and drop g_Addend twice. The source slot is left unchanged. The
//  net g_Addend traffic nets to zero (the resolver / splitter own their own
//  increments). [NOT byte-identical] (placement-dependent relative calls to
//  ResolvePathSlot / SplitPathIntoSlots + the Interlocked IAT).
static void NormalizePathSlot(F3SharedBuffer **pathSlot)
{
    if (g_byte_2075F4E == 0)
        return;
    InterlockedIncrement(&g_Addend);
    F3SharedBuffer *resolved = 0;
    InterlockedIncrement(&g_Addend);
    ResolvePathSlot(&resolved, pathSlot);   // sub_18AF9D0
    F3SharedBuffer *drive = 0;
    InterlockedIncrement(&g_Addend);
    SplitPathIntoSlots(&resolved, &drive, 0, 0, 0);   // sub_18ABFA0
    if (resolved != 0)
    {
        if (InterlockedDecrement(&resolved->refcount) <= 0 && resolved != 0)
            SharedBufferFree(resolved);
    }
    InterlockedDecrement(&g_Addend);
    if (drive != 0)
    {
        if (InterlockedDecrement(&drive->refcount) <= 0 && drive != 0)
            SharedBufferFree(drive);
    }
    InterlockedDecrement(&g_Addend);
}

// CDiskFileWin32Open  (0x18C5150 - 0x18C53FA)  -- LHFile::CDiskFileWin32::Open
//
//  long CDiskFileWin32Open(CDiskFileWin32 *obj, F3SharedBuffer **name, int mode, int flags)
//
//  Open a file. `name` is a shared slot holding the filename block (the C
//  string is at (*name)->data). The mode selects the CreateFileA access /
//  share / disposition:
//    1 = read  (OPEN_EXISTING, share read, GENERIC_READ only)
//    2 = write (OPEN_EXISTING, share read, after path-normalize)
//    3 = create(CREATE_ALWAYS, no share, after path-normalize)
//    4 = open-or-create (OPEN_ALWAYS, no share, after path-normalize +
//       EnsureParentDirectory).
//  Modes 2-4 use the desired access 0xC0000000 (GENERIC_READ|GENERIC_WRITE);
//  mode 1 uses 0x80000000 (GENERIC_READ) only; an invalid mode uses 0 (and
//  fails in CreateFileA). `flags` selects dwFlagsAndAttributes:
//  FILE_FLAG_WRITE_THROUGH (0x8000000) when the caller set bit 1, else 0.
//  On success the HANDLE / size / position / is-open flag are filled in.
//  On failure (a null or empty filename, or CreateFileA failing) an error
//  string is built in temp slots, the slots are released, and the new
//  g_Addend value is returned. The object's fileName field (+0x18) is set
//  up front (CopySharedSlot) and the HANDLE field is cleared before the
//  CreateFileA attempt. [NOT byte-identical] (placement-dependent relative
//  calls to the whole string / path cluster + the KERNEL32 IAT slots, the
//  thiscall+retn-0Ch hybrid convention, plus the high-VA globals g_Addend
//  and Str1 (0x1BD44D7, the empty string)).
static long CDiskFileWin32Open(CDiskFileWin32 *obj, F3SharedBuffer **name, int mode, int flags)
{
    // 1. Copy the filename into the object's fileName field (sub_1A01270).
    CopySharedSlot((F3SharedBuffer **)&obj->fileName, name);

    // 2. Clear the HANDLE up front (the original zeroes [this+10h] here).
    obj->handle = 0;

    // 3. Resolve the path (sub_18AFB90 = ResolvePathSlot).
    ResolvePathSlot(name, name);

    // 4. Check the filename is non-null and non-empty.
    if (name == 0 || *name == 0 || (*name)->length <= 0)
        goto BadFilename;

    // 5. Mode switch.
    DWORD shareMode = 0, creationDisposition = 0, desiredAccess = 0;
    switch (mode)
    {
        case 1:
            shareMode = 1;                       // FILE_SHARE_READ
            desiredAccess = 0x80000000;          // GENERIC_READ
            creationDisposition = OPEN_EXISTING; // 3
            obj->mode = mode;
            break;
        case 2:
            NormalizePathSlot(name);             // sub_18AFC00
            shareMode = 1;                       // FILE_SHARE_READ
            desiredAccess = 0xC0000000;          // GENERIC_READ | GENERIC_WRITE
            creationDisposition = OPEN_EXISTING; // 3
            obj->mode = mode;
            break;
        case 3:
            NormalizePathSlot(name);             // sub_18AFC00
            shareMode = 0;
            desiredAccess = 0xC0000000;
            creationDisposition = CREATE_ALWAYS; // 4
            obj->mode = 2;
            break;
        case 4:
            NormalizePathSlot(name);             // sub_18AFC00
            EnsureParentDirectory(name);         // sub_18B1360
            shareMode = 0;
            desiredAccess = 0xC0000000;
            creationDisposition = OPEN_ALWAYS;   // 2
            obj->mode = 2;
            break;
        default:
            break;
    }

    // 6. CreateFileA.
    SetLastError(0x10000001);                    // the original's sentinel
    const char *fileName = (name != 0 && *name != 0) ? (*name)->data : ""; // Str1 (0x1BD44D7)
    HANDLE hFile = CreateFileA(fileName, desiredAccess, shareMode, 0,
                               creationDisposition,
                               (flags & 2) ? 0x8000000 : 0,   // FILE_FLAG_WRITE_THROUGH
                               0);
    DWORD lastErr = GetLastError();

    if (hFile == INVALID_HANDLE_VALUE || (lastErr & 0x10000000))
        goto Error;

    // 7. Success: fill in the object.
    obj->size     = GetFileSize(hFile, 0);
    SetFilePointer(hFile, 0, 0, FILE_BEGIN);
    obj->handle   = hFile;
    obj->position = 0;
    obj->openFlag = 1;
    SetFilePointer(hFile, 0, 0, FILE_END);
    return ReleaseSharedSlot(name);

BadFilename:
    {
        F3SharedBuffer *msg = 0;
        SetSharedSlot(&msg, "Bad filename ", -1);
        AppendStringSlot(&msg, name);            // msg += name
        ReleaseSharedSlot(&msg);
        return ReleaseSharedSlot(name);
    }

Error:
    {
        F3SharedBuffer *msg = 0;
        char numBuf[32];
        _snprintf(numBuf, sizeof(numBuf), "%d", (int)lastErr);
        SetSharedSlot(&msg, "Cannot open file '", -1);
        AppendStringSlot(&msg, name);            // msg += name
        AppendSharedSlot(&msg, "' (Error ");
        AppendSharedSlot(&msg, numBuf);
        AppendSharedSlot(&msg, ")");
        ReleaseSharedSlot(name);
        return ReleaseSharedSlot(&msg);
    }
}

// CDiskFileWin32Close  (0x18C4840 - 0x18C4873)  -- LHFile::CDiskFileWin32::Close
//
//  BOOL CDiskFileWin32Close(CDiskFileWin32 *obj)
//
//  Close the disk file. When the is-open flag (+0x14) is set, rewind the file
//  pointer to the start (SetFilePointer, BEGIN), flush the buffers
//  (FlushFileBuffers), and close the HANDLE (CloseHandle). The is-open flag is
//  always cleared. The CloseHandle result is returned (when not already open,
//  eax holds its pre-call value, modelled here as TRUE). [NOT byte-identical]
//  (refs the SetFilePointer / FlushFileBuffers / CloseHandle KERNEL32 IAT
//  slots).
static BOOL CDiskFileWin32Close(CDiskFileWin32 *obj)
{
    BOOL result = TRUE;
    if (obj->openFlag != 0)
    {
        SetFilePointer(obj->handle, 0, 0, FILE_BEGIN);
        FlushFileBuffers(obj->handle);
        result = CloseHandle(obj->handle);
    }
    obj->openFlag = 0;
    return result;
}

// ============================================================
//  GetDefaultResolution  (0x61A000 - 0x61A100)
// ============================================================
//
//  Picks the default video resolution to run at based on the desktop's
//  aspect ratio. It computes desktopX / desktopY (SM_CXSCREEN / SM_CYSCREEN
//  via GetSystemMetrics), finds the closest candidate aspect ratio, and
//  writes the matching { width, height } pair to *out, returning out.
//
//  Candidate aspect ratio -> resolution pairs (the aspect values are the
//  rdata floats, kept at their original IDA names / addresses):
//      dword_1BF95A4  1.7777778f  (16:9)  ->  1280 x 720
//      dword_1AE9B04  1.3333334f  ( 4:3)  ->  1152 x 864
//      dword_1AE9B00  1.6f        (16:10) ->  1280 x 800
//  (dword_1AE9AF0 = -1.0f is the fabs sign mask the original ANDs with).
//
//  The closest-candidate scan keeps the first index on ties (a candidate
//  only wins on a strictly smaller |aspect - candidate| difference). The
//  first call-site is RunFrontEnd (0x61CC1D), which seeds the frontend
//  resolution globals (dword_1C88DE4 / dword_1C88DE8) and, when the saved
//  settings record is uncalibrated, the saved resolution words too.
//
//  [NOT byte-identical] -- refs the __imp_GetSystemMetrics IAT slot and the
//  high-VA rdata floats above at their original addresses.
static const float dword_1BF95A4 = 1.7777778f;   // 0x1BF95A4: 16:9
static const float dword_1AE9B04 = 1.3333334f;   // 0x1AE9B04: 4:3
static const float dword_1AE9B00 = 1.6f;         // 0x1AE9B00: 16:10
static const float dword_1AE9AF0 = -1.0f;        // 0x1AE9AF0: fabs mask

//  GetDefaultResolution  (0x61A000 - 0x61A100)  -- pick the default
//  resolution matching the desktop's aspect ratio; returns *out.
DWORD *GetDefaultResolution(DWORD *out)
{
    float aspect = (float)GetSystemMetrics(0) / (float)GetSystemMetrics(1);
    const float candidates[]  = { dword_1BF95A4, dword_1AE9B04, dword_1AE9B00, 0.0f };
    const int   resolutions[] = { 1280, 720,  1152, 864,  1280, 800 };
    int best = 0;
    for (int i = 0; candidates[i] > 0.0f; ++i)
    {
        // |x| as the original computes it (andps with the dword_1AE9AF0 mask).
        float d1 = fabsf(candidates[i]   - aspect);
        float d0 = fabsf(candidates[best] - aspect);
        if (d1 < d0)
            best = i;
    }
    out[0] = (DWORD)resolutions[best * 2];
    out[1] = (DWORD)resolutions[best * 2 + 1];
    return out;
}

// ============================================================
//  Video-options option-map cluster (LoadVideoOptions, 0x775EE0, and the
//  32-byte option-map objects, the LHXML stream helpers, and the map
//  copy / destroy machinery it drives)
//
//  The frontend builds a 96-byte triple of 32-byte "option map" objects
//  (the Int / Float / Detail settings; each is a red-black tree of 24-byte
//  nodes) from VideoOptionsConfig.xml: three local maps are constructed on
//  the stack, the "ConfigSettings" section's IntSettings / FloatSettings /
//  DetailSettings child elements are read into them, and the locals are then
//  heap-copied into the caller's object (Int at +0x00, Float at +0x20,
//  Detail at +0x40). The LHXML parse engine itself is a remaining stub, so
//  no section is ever found and the maps stay empty (exactly the original's
//  behavior when the file is missing); the map layout, the heap traffic, and
//  the copy / destroy paths below are all reproduced.
// ============================================================

// The F3OptionNode (24-byte) / F3OptionMap (32-byte) option-map types
// live in gamemain.h (moved for CNetSaveGameCtor in gamethread.cpp).

// The 24-byte LHXML::CXMLReadStream stack object (sub_17DC290 / sub_17DC2B0).
typedef struct F3XmlStream
{
    void *   vtable;      // +0x00
    DWORD    flags;       // +0x04: 0x8000 (the base ctor)
    DWORD *  fileBuf;     // +0x08: the 8-byte {0, 4096-byte buffer} block
    DWORD    section;     // +0x0C: the current section node
    DWORD    sectionInfo; // +0x10
    BYTE     sectionFound;// +0x14 (byte 20)
    BYTE     pad[3];      // +0x15..+0x17
} F3XmlStream;             // 24 bytes

// Placeholder LHXML vtables: the stack stream's slots are never dispatched
// in this build (the parse below is a remaining stub, so no section is ever
// found and the option readers are unreachable).
static void *g_CXMLStreamVtable[8]       = { 0 };
static void *g_CXMLReadStreamVtable[32]  = { 0 };
static void *g_CXMLWriteStreamVtable[32] = { 0 };

// sub_17DC6D0 (defined below): the real vtable[1] slot (enter a section)
static char FindXmlSection(F3XmlStream *obj, const char *name);
// sub_17DC680 / sub_17DC3A0 / sub_17DC470 (defined below): the real
// vtable[2] / [3] / [10] slots (check the current root element by name,
// advance to the next sibling, and read a named boolean attribute). These are
// the LHXML reader slots the VFS-config root reader (VfsLoadConfigRoot) uses.
static char XmlCheckRootSection(F3XmlStream *obj, const char *name);
static int  XmlAdvance(F3XmlStream *obj);
static char XmlReadBoolAttr(F3XmlStream *obj, void *name, BYTE *out);
// sub_17DBE60 (defined below): the real vtable[8] slot (read the current
// element's value as an int64/hex word into *out)
static void XmlReadAttrValue64(F3XmlStream *obj, DWORD *out);
// sub_17DC4B0 (defined below): the real vtable[11] slot (intern the
// current element's named attribute value and hand back its handle)
static char XmlReadAttrInterned(F3XmlStream *obj, const char *name, DWORD *outHandle);
// sub_17E6EF0 (defined below): find the named attribute's value string on a
// node (the attribute list at node+0x20)
static const char *XmlFindAttrValue(DWORD *node, const char *name);
// sub_17E4D10 (defined below): intern an XML string in the global table
static DWORD InternXmlString(const char *src);

// ============================================================
//  LHXML report handler (off_1D94CA0)
//
//  The LHXML module forwards parser / reader errors to a single global
//  report handler (off_1D94CA0, type int (__cdecl *)(char *message)); the
//  report builder sub_17E4FA0 formats the message (vprintf + vsprintf_s into
//  a growing buffer) and calls it. The saved video-options loader installs a
//  silent no-op handler (nullsub_33) for the duration of the file read.
// ============================================================
typedef int (__cdecl *F3XmlReportHandler)(char *message);
static F3XmlReportHandler g_dword_1D94CA0 = 0;   // 0x1D94CA0: the report handler

// sub_17E4F80 (0x17E4F80 - 0x17E4F86): the current report handler
static F3XmlReportHandler GetXmlReportHandler(void)
{
    return g_dword_1D94CA0;
}

// sub_17E4F90 (0x17E4F90 - 0x17E4F9A): install + return the report handler
static F3XmlReportHandler SetXmlReportHandler(F3XmlReportHandler handler)
{
    g_dword_1D94CA0 = handler;
    return handler;
}

// nullsub_33 (0x775A50): the silent report handler (a bare `ret`)
static int XmlReportNoop(char *message)
{
    (void)message;
    return 0;
}

// 0x1E2D7A0: the LHXML buffer-pool allocation counter.
static DWORD g_dword_1E2D7A0 = 0;

// sub_17E63B0 / sub_17E63C0: the LHXML game-heap alloc / free thunks.
static void *XmlHeapAlloc(unsigned int size) { InterlockedIncrement(&g_dword_1E2D7A0); return LHAlloc(size); }
static void  XmlHeapFree(void *p) { if (p != 0) { LHFree(p); InterlockedDecrement(&g_dword_1E2D7A0); } }

// sub_17E6620 (0x17E6620 - 0x17E66A8): fresh 8-byte {0, 4096-byte buffer} file block
static DWORD *InitXmlFileBlock(DWORD *block)
{
    *block = 0;
    DWORD *buf = (DWORD *)XmlHeapAlloc(4096);
    if (buf != 0)
    {
        buf[0] = 4096;      // capacity
        buf[1] = 0;         // length
        buf[2] = -1;
        buf[3] = 0;
        buf[4] = 0;
        buf[5] = 0;
        buf[6] = 0;
    }
    block[1] = (DWORD)buf;
    return block;
}

// sub_17E68F0 (0x17E68F0 - 0x17E6939): close the 8-byte file block
static void CloseXmlFileBlock(DWORD *block)
{
    // sub_17E70D0(*block, block[1]): close the (0) file handle
    DWORD *buf = (DWORD *)block[1];
    if (buf != 0)
    {
        // sub_17E4930(buf): the buffer finalization (a no-op here)
        XmlHeapFree((void *)buf[4]);    // the inner payload (0 here)
        buf[4] = 0;
        buf[5] = 0;
        buf[6] = 0;
        XmlHeapFree((void *)buf);
    }
}

// sub_17DAA00 + sub_17DC290 (0x17DC290 - 0x17DC2B0): the CXMLReadStream ctor
static void CXMLReadStreamCtor(F3XmlStream *obj)
{
    obj->vtable = (void *)g_CXMLStreamVtable;   // the sub_17DAA00 base ctor
    obj->flags = 0x8000;
    // sub_17E42C0 / sub_17E6390: the one-shot LHXML static initializers (no-op)
    obj->fileBuf = 0;
    obj->section = 0;
    obj->sectionInfo = 0;
    obj->sectionFound = 0;
    obj->vtable = (void *)g_CXMLReadStreamVtable;   // the derived vtable
    if (g_CXMLReadStreamVtable[1] == 0)
        g_CXMLReadStreamVtable[1] = (void *)FindXmlSection;   // vt[1]: enter a section
    if (g_CXMLReadStreamVtable[2] == 0)
        g_CXMLReadStreamVtable[2] = (void *)XmlCheckRootSection;   // vt[2]
    if (g_CXMLReadStreamVtable[3] == 0)
        g_CXMLReadStreamVtable[3] = (void *)XmlAdvance;           // vt[3]
    if (g_CXMLReadStreamVtable[8] == 0)
        g_CXMLReadStreamVtable[8] = (void *)XmlReadAttrValue64;   // vt[8]
    if (g_CXMLReadStreamVtable[10] == 0)
        g_CXMLReadStreamVtable[10] = (void *)XmlReadBoolAttr;     // vt[10]
    if (g_CXMLReadStreamVtable[11] == 0)
        g_CXMLReadStreamVtable[11] = (void *)XmlReadAttrInterned; // vt[11]
}

// sub_17DBD00 (0x17DBD00 - 0x17DBDB8): open + parse the XML file into the stream
static char OpenXmlFile(F3XmlStream *obj, const char *fileName)
{
    DWORD *old = obj->fileBuf;
    if (old != 0)
    {
        CloseXmlFileBlock(old);          // sub_17E68F0
        XmlHeapFree(old);                // sub_17E65B0
    }
    DWORD *block = (DWORD *)XmlHeapAlloc(8);
    obj->fileBuf = (block != 0) ? InitXmlFileBlock(block) : 0;
    // sub_17E6AD0: the CXMLDocumentParser parse. [remaining stub] the LHXML
    // parse engine is out of scope, so no file is ever read and the tree is
    // never populated; the parse is reported to have succeeded only when the
    // file exists (as it does for the game's well-formed config files), and
    // the section search then finds nothing (see FindXmlSection below).
    return (GetFileAttributesA(fileName) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
}

// sub_17E4350 (0x17E4350 - 0x17E438A): resolve an interned XML string handle
static const char *ResolveXmlString(DWORD *handle)
{
    (void)handle;
    return "";   // [remaining stub] no strings are interned in this build
}

// sub_17DC2E0 (0x17DC2E0 - 0x17DC39C): search a section / element node by name
static char SearchXmlSection(F3XmlStream *obj, const char *name, DWORD step, char wrap)
{
    DWORD *node = (DWORD *)obj->section;
    if (node == 0)
    {
        if (wrap == 0 && obj->sectionFound != 0)
            return 0;
        // The root node = *fileBuf[0]: the parse is a remaining stub, so the
        // buffer is always empty and the search never finds a section.
        DWORD root = (obj->fileBuf != 0) ? *obj->fileBuf : 0;
        if (root != 0
            && _mbscmp((const unsigned char *)ResolveXmlString((DWORD *)root + 2),
                       (const unsigned char *)name) == 0)
        {
            obj->section = root;
            obj->sectionInfo = *(DWORD *)root + 5;
            *(DWORD *)&obj->sectionFound = 1;
            return 1;
        }
        return 0;
    }
    if (wrap != 0 && step == 0)
        step = obj->sectionInfo;
    if (step == 0)
        return 0;
    for (DWORD *n = (DWORD *)step; ; n = *(DWORD **)(n + 1))
    {
        if (_mbscmp((const unsigned char *)ResolveXmlString((DWORD *)n + 2),
                    (const unsigned char *)name) == 0)
        {
            obj->section = (DWORD)n;
            obj->sectionInfo = n[5];
            return 1;
        }
        if (*(DWORD **)(n + 1) == 0)
        {
            if (wrap == 0)
                return 0;
            n = (DWORD *)obj->sectionInfo;
        }
        if (n == (DWORD *)step)
            return 0;
    }
}

// sub_17DC6D0 (0x17DC6D0 - 0x17DC6E3): enter the named section of the stream
static char FindXmlSection(F3XmlStream *obj, const char *name)
{
    return SearchXmlSection(obj, name, obj->sectionInfo, 1);
}

// sub_17DC680 (0x17DC680 - 0x17DC693): check the stream's current element for
// the named root (SearchXmlSection with wrap = 0, i.e. no wrap-around). The
// VFS-config root reader (VfsLoadConfigRoot) uses this as vtable[2] to test
// whether the current element is "VFSConfig". [remaining stub] the LHXML parse
// never populates the tree, so the root is never matched (returns 0).
static char XmlCheckRootSection(F3XmlStream *obj, const char *name)
{
    return SearchXmlSection(obj, name, obj->sectionInfo, 0);
}

// sub_17DC3A0 (0x17DC3A0 - 0x17DC3B0): advance the stream to the next sibling
// element (vtable[3]).
//  [BYTE-IDENTICAL] __thiscall (the stream in ECX, plain `retn`); the body is
//  pure pointer arithmetic (no calls / long jumps / high-VA refs), so it is
//  emitted as naked asm matching the original 16 bytes exactly. It loads the
//  current node (obj->section, +0x0C), stores the node's +0x04 field into
//  obj->sectionInfo (+0x10), moves obj->section to the node's +0x10 field, and
//  returns that value. Dispatched as a __thiscall function (the nextSibling
//  member of F3CXMLReadStreamVT and the thiscall casts at the vtable[3] /
//  direct-call sites); the plain declaration below only carries the signature.
//  No element is ever current in this build (the LHXML parse is a remaining
//  stub), so it is never actually reached here.
__declspec(naked) int XmlAdvance(F3XmlStream *obj)
{
    __asm
    {
        mov     eax, [ecx+0Ch]   ; eax = obj->section (the current node)
        mov     edx, [eax+4]     ; edx = node->+0x04
        mov     [ecx+10h], edx   ; obj->sectionInfo = node->+0x04
        mov     eax, [eax+10h]   ; eax = node->+0x10
        mov     [ecx+0Ch], eax   ; obj->section = node->+0x10
        ret                      ; return node->+0x10
    }
}

// sub_17DC470 (0x17DC470 - 0x17DC4A8): read the named boolean attribute of
// the current element into *out (1 when the value is "true") (vtable[10]).
// [NOT byte-identical] placement-dependent (relative calls to
// XmlFindAttrValue and the __mbsicmp IAT slot) and refs the high-VA "true"
// rdata string (0x01BE6254) at its original address. Thiscall + retn 8 in
// the original (the stream in ECX, name + out on the stack), declared as a
// plain C function here (the stream first). Works on the current element
// (obj->section): look up the named attribute's value string through
// XmlFindAttrValue, compare it case-insensitively to "true" (_mbsicmp), and
// write 1 / 0 to *out. Returns 1 when the attribute is present, 0 when no
// element is current or the attribute is absent (leaving *out as is; the
// caller defaults the attribute to true). No element is ever current in this
// build (the LHXML parse is a remaining stub), so it returns 0 here, as the
// original does.
static char XmlReadBoolAttr(F3XmlStream *obj, void *name, BYTE *out)
{
    DWORD section = obj->section;
    if (section == 0)
        return 0;
    const char *value = XmlFindAttrValue((DWORD *)section, (const char *)name);
    if (value == 0)
        return 0;
    *out = (_mbsicmp((const unsigned char *)value, (const unsigned char *)"true") == 0);
    return 1;
}

// ============================================================
//  int64 value reader (vtable[8] = XmlReadAttrValue64)
//
//  The value reader works on the current element (obj->section): it
//  resolves the element's "type" attribute (vtable[11] =
//  XmlReadAttrInterned -> XmlFindAttrValue over the node's attribute list,
//  interned through InternXmlString) into the 8-entry value-type index
//  (invalid/int/uint/float/string/bool/double/unknown), copies the
//  element's value string into the thread-shared scratch buffer (the
//  stru_1E2D778 critical section guards the dword_1E2D790 buffer, sized
//  through the g_dword_1E2D7A0 pool counter on the LH game heap), trims
//  the surrounding whitespace / quotes, and converts it to an int64
//  (decimal via _atoi64, or 0x-prefixed hex via XmlParseHex64). The low
//  32 bits are stored in *out. [NOT byte-identical] placement-dependent
//  (relative calls down the chain). In this build the LHXML parse is a
//  remaining stub, so no element is ever current (obj->section == 0) and
//  the value string is always "" -> *out = 0, exactly the original's
//  behavior here.
// ============================================================

// 0x1E2D778: the scratch-buffer lock (a CRITICAL_SECTION; ends at 0x1E2D790).
static CRITICAL_SECTION g_stru_1E2D778 = { 0 };
// 0x1E2D790 / 0x1E2D794: the thread-shared value scratch buffer + capacity.
static DWORD g_dword_1E2D790 = 0;
static DWORD g_dword_1E2D794 = 0;
// 0x1E2D798 / 0x1E2D799: the in-use / lock-initialized flag bytes.
static BYTE g_byte_1E2D798 = 0;
static BYTE g_byte_1E2D799 = 0;
// 0x1D816A0: the LHXML log/report object base (0x212E240 in the original);
// the hex-conversion error prints to the FILE embedded in the object at
// +0x40 (0x1D816E0).
static DWORD g_dword_1D816A0 = 0;
static FILE  g_file_1D816E0;

// sub_17E6340 (0x17E6340 - 0x17E636D): lazily initialize + enter the lock
static void XmlScratchLockEnter(void)
{
    if (g_byte_1E2D799 == 0)
    {
        InitializeCriticalSection(&g_stru_1E2D778);
        g_byte_1E2D799 = 1;
    }
    EnterCriticalSection(&g_stru_1E2D778);
}

// sub_17E6370 (0x17E6370 - 0x17E6383): leave the lock
static void XmlScratchLockLeave(void)
{
    g_byte_1E2D798 = 0;
    LeaveCriticalSection(&g_stru_1E2D778);
}

// sub_17E63A0 (0x17E63A0 - 0x17E63A6): the current scratch buffer
static DWORD XmlScratchGet(void)
{
    return g_dword_1E2D790;
}

// sub_17E6430 (0x17E6430 - 0x17E6473): grow the scratch buffer to size
// bytes (LHAlloc / LHFree through the pool counter) and return it
static DWORD XmlScratchEnsure(unsigned int size)
{
    DWORD buf = g_dword_1E2D790;
    if (buf != 0)
    {
        if (g_dword_1E2D794 >= size)
            return buf;
        LHFree((void *)buf);
        g_dword_1E2D7A0--;
    }
    g_dword_1E2D7A0++;
    buf = (DWORD)LHAlloc(size);
    g_dword_1E2D790 = buf;
    g_dword_1E2D794 = size;
    return buf;
}

// sub_17E7450 (0x17E7450 - 0x17E7488): the 8-entry value-type table
// (off_1BC5C68) -> index (case-insensitive; 7 = "unknown" on a miss)
static const char *g_xmlValueTypes[8] =
{
    "invalid", "int", "uint", "float", "string", "bool", "double", "unknown"
};
static int XmlValueTypeNameIndex(const char *name)
{
    for (int i = 0; i < 8; ++i)
    {
        if (_mbsicmp((const unsigned char *)name,
                     (const unsigned char *)g_xmlValueTypes[i]) == 0)
            return i;
    }
    return 7;
}

// sub_17DBBB0 (0x17DBBB0 - 0x17DBC35): trim the value string in place
// (surrounding whitespace, one pair of enclosing quotes); returns the
// last significant character (0 for the empty string)
static char *XmlTrimValueString(char *str)
{
    unsigned int len = (unsigned int)strlen(str);
    if (len == 0)
        return 0;
    char *first = str;
    while (*first == ' ' || *first == '\t' || *first == '\n' || *first == '\r')
        ++first;
    if (*first == '"')
        ++first;
    char *last = str + len - 1;
    while (last > first
           && (*last == ' ' || *last == '\t' || *last == '\n' || *last == '\r'))
        --last;
    if (*last == '"')
        --last;
    if (first == str)
    {
        *(last + 1) = 0;
    }
    else
    {
        char *dst = str;
        while (first <= last)
            *dst++ = *first++;
        *dst = 0;
    }
    return last;
}

// sub_17E4250 (0x17E4250 - 0x17E42B6): parse the hex digit string into
// *out (shift-or); 1 on success, 0 on a bad digit
static char XmlParseHex64(const char *str, __int64 *out)
{
    __int64 value = 0;
    *out = 0;
    for (const char *p = str; *p != 0; ++p)
    {
        char c = *p;
        int digit;
        if (c >= 'a' && c <= 'f')       digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')  digit = c - 'A' + 10;
        else if (c >= '0' && c <= '9')  digit = c - '0';
        else return 0;
        value = (value << 4) | digit;
    }
    *out = value;
    return 1;
}

// sub_17DBA90 (0x17DBA90 - 0x17DBAE5): convert the value string to an
// int64 (decimal via _atoi64, or 0x-prefixed hex; a hex parse failure
// reports through the log object's FILE at 0x1D816E0)
static __int64 XmlConvertValueToInt64(const char *str)
{
    if (str[0] == '0' && str[1] == 'x')
    {
        __int64 value = 0;
        if (XmlParseHex64(str + 2, &value) == 0)
            fprintf(&g_file_1D816E0, "unable to convert hex value %s\n", str);
        return value;
    }
    return _atoi64(str);
}

// sub_17E6EF0 (0x17E6EF0 - 0x17E6F31): find the named attribute of the
// node (the attribute list at node+0x20: +0x04 next, +0x08 name handle,
// +0x0C value handle) and return the resolved value string
static const char *XmlFindAttrValue(DWORD *node, const char *name)
{
    DWORD *attr = (DWORD *)node[8];
    if (attr == 0)
        return 0;
    for (;;)
    {
        const char *attrName = ResolveXmlString((DWORD *)(attr + 2));
        if (_mbscmp((const unsigned char *)attrName,
                    (const unsigned char *)name) == 0)
            return ResolveXmlString((DWORD *)(attr + 3));
        attr = (DWORD *)attr[1];
        if (attr == 0)
            return 0;
    }
}

// sub_17DC4B0 (0x17DC4B0 - 0x17DC4E3): read the current element's named
// attribute value, interned (vtable[11]); 1 on success, 0 when no element
// is current or the attribute is absent
static char XmlReadAttrInterned(F3XmlStream *obj, const char *name, DWORD *outHandle)
{
    DWORD node = obj->section;
    if (node == 0)
        return 0;
    const char *value = XmlFindAttrValue((DWORD *)node, name);
    if (value == 0)
        return 0;
    *outHandle = InternXmlString(value);
    return 1;
}

// sub_17DBC50 (0x17DBC50 - 0x17DBCE4): read the current element's value:
// resolve the element's "type" attribute to the value-type index in
// *typeOut, then copy the element's value string (node+0x0C handle) into
// the scratch buffer, trimmed. ctx is the caller's stack scope marker
// (unused; forwarded as ECX in the original). Returns the trim result.
static char *XmlReadElementValue(F3XmlStream *obj, void *ctx, int *typeOut)
{
    (void)ctx;
    InternXmlString("");                    // byte_1BCE523: the empty string
    DWORD handle = 0;
    void **vt = (void **)obj->vtable;
    ((char (*)(F3XmlStream *, const char *, DWORD *))vt[11])(obj, "type", &handle);
    *typeOut = XmlValueTypeNameIndex(ResolveXmlString((DWORD *)handle));
    const char *value = ResolveXmlString((DWORD *)obj->section + 3);
    unsigned int size = (unsigned int)strlen(value) + 1;
    XmlScratchEnsure(size);
    char *buf = (char *)XmlScratchGet();
    strcpy_s(buf, size, value);
    return XmlTrimValueString(buf);
}

// sub_17DBE60 (0x17DBE60 - 0x17DBEA8): read the current element's value as an
// int64 word (vtable[8]). [NOT byte-identical] placement-dependent (relative
// calls to the lock / value reader / converter). See the header above for the
// behavior; no element is ever current in this build, so *out = 0.
static void XmlReadAttrValue64(F3XmlStream *obj, DWORD *out)
{
    char  ctx = 0;            // the original's v5 scope marker
    int   typeIndex = 0;      // the original's v6 (the value-type index)
    XmlScratchLockEnter();
    XmlReadElementValue(obj, &ctx, &typeIndex);
    __int64 value = XmlConvertValueToInt64((const char *)XmlScratchGet());
    *out = (DWORD)value;
    XmlScratchLockLeave();
}

// sub_17DC2B0 + sub_17DAA20 (0x17DC2B0 - 0x17DC2DA): the CXMLReadStream dtor
static void CXMLReadStreamDtor(F3XmlStream *obj)
{
    DWORD *block = obj->fileBuf;
    obj->vtable = (void *)g_CXMLStreamVtable;
    if (block != 0)
    {
        CloseXmlFileBlock(block);
        XmlHeapFree(block);
    }
    obj->vtable = (void *)g_CXMLStreamVtable;   // the sub_17DAA20 base dtor
    // sub_17E4390 / sub_17E63F0: the one-shot LHXML static teardowns (no-op)
}

// sub_17DB6C0 (placeholder for the CXMLWriteStream vtable[1] slot): enter a
// named element on the (write) stream. The LHXML serializer is a remaining
// stub in this build, so entering an element always fails (returns 0) and the
// tree is never populated; ReadVideoOptionsValues then writes nothing, which
// is exactly the original's behavior when the serializer is unavailable.
static char WriteStreamEnterElement(void *stream, const char *name)
{
    (void)stream;
    (void)name;
    return 0;
}

// sub_17DB800 + sub_17DAA00 (0x17DB800 - 0x17DB830): the CXMLWriteStream ctor
//  [NOT byte-identical] placement-dependent (relative calls to the base ctor
//  sub_17DAA00 + the one-shot LHXML static initializers sub_17E42C0 / 17E6390).
static void CXMLWriteStreamCtor(F3XmlStream *obj)
{
    obj->vtable = (void *)g_CXMLStreamVtable;    // the sub_17DAA00 base ctor
    obj->flags = 0x8000;
    // sub_17E42C0 / sub_17E6390: the one-shot LHXML static initializers (no-op)
    obj->fileBuf = 0;
    obj->section = 0;
    obj->sectionInfo = 0;
    obj->vtable = (void *)g_CXMLWriteStreamVtable;    // the derived vtable
    if (g_CXMLWriteStreamVtable[1] == 0)
        g_CXMLWriteStreamVtable[1] = (void *)WriteStreamEnterElement;   // vt[1]
    *(BYTE *)&obj->sectionFound = 1;   // byte 20 = 1 (a write stream starts "found")
}

// sub_17DB830 + sub_17DAA20 (0x17DB830 - 0x17DB84F): the CXMLWriteStream dtor
//  [NOT byte-identical] placement-dependent (relative calls to CloseXmlFileBlock,
//  the LHXML heap-free, and the base dtor sub_17DAA20).
static void CXMLWriteStreamDtor(F3XmlStream *obj)
{
    DWORD *block = obj->fileBuf;
    obj->vtable = (void *)g_CXMLWriteStreamVtable;
    if (block != 0)
    {
        CloseXmlFileBlock(block);              // sub_17E68F0
        XmlHeapFree(block);                    // j_XmlHeapFree_2 (free the 8-byte block)
    }
    obj->vtable = (void *)g_CXMLStreamVtable;   // the sub_17DAA20 base dtor
    // sub_17E4390 / sub_17E63F0: the one-shot LHXML static teardowns (no-op)
}

// sub_17DB2C0 + sub_17E6870 (0x17DB2C0 - 0x17E68A9): serialize the (write)
// stream's XML tree to a file.  [NOT byte-identical] the LHXML tree serializer
// (sub_17E6660) is a remaining stub; for the empty tree this build produces the
// original writes only the constant prologue, so we write the same bytes here
// (sub_17E5B70: fopen "wb" + fwrite + fclose).
static void WriteXmlStreamToFile(F3XmlStream *obj, const char *fileName)
{
    (void)obj;   // the empty-tree serializer writes only the prologue
    FILE *f = fopen(fileName, "wb");
    if (f != 0)
    {
        static const char prologue[] = "<?xml version=\"1.0\"?>\r\n";
        fwrite(prologue, 1, sizeof(prologue) - 1, f);
        fclose(f);
    }
}

// sub_40EDB0 / sub_40EFD0 / sub_40F1F0 (identical in all three types):
// fresh 24-byte sentinel node {node, node, node, 0, 0, 0, color 1, leaf 1}
static void InitOptionMapRoot(F3OptionMap *obj)
{
    F3OptionNode *node = (F3OptionNode *)LHAlloc(24);
    if (node != 0)
    {
        node->link0 = 0;
        node->link1 = 0;
        node->link2 = 0;
    }
    node->color = 1;
    obj->root = node;
    node->leaf = 1;
    node->link1 = node;
    node->link0 = node;
    node->link2 = node;
    obj->count = 0;
}

// sub_40E6D0 / sub_40E780 / sub_40E830 (the three identical option-map
// ctors): the 4-byte wrapper { &obj } into obj+0x00, then the sentinel node.
static void InitOptionMapBase(F3OptionMap *obj, void *a2, void *a3)
{
    (void)a2;
    (void)a3;
    DWORD *w = (DWORD *)LHAlloc(4);
    if (w != 0)
        *w = (DWORD)obj;
    obj->wrapper = w;
    InitOptionMapRoot(obj);
}
static void InitIntOptionMap(F3OptionMap *obj, void *a2, void *a3)    { InitOptionMapBase(obj, a2, a3); }   // sub_40E6D0
static void InitFloatOptionMap(F3OptionMap *obj, void *a2, void *a3)  { InitOptionMapBase(obj, a2, a3); }   // sub_40E780
void InitDetailOptionMap(F3OptionMap *obj, void *a2, void *a3) { InitOptionMapBase(obj, a2, a3); }   // sub_40E830 (declared in gamemain.h for gamethread.cpp)

// sub_1A012C0 (0x1A012C0 - 0x1A0131C): take a ref of the source slot's block
F3SharedBuffer **EngRefSharedSlot(F3SharedBuffer **dst, F3SharedBuffer **src)   // xlip.h
{
    *dst = 0;
    InterlockedIncrement(&g_Addend);
    if (src != dst)
    {
        if (*dst != 0)
        {
            if (InterlockedDecrement(&(*dst)->refcount) <= 0 && *dst != 0)
                SharedBufferFree(*dst);
            *dst = 0;
        }
        if (*src != 0)
        {
            *dst = *src;
            InterlockedIncrement(&(*dst)->refcount);
        }
    }
    return dst;
}

// ============================================================
//  Shared-slot string-build cluster (the "settings file path" helpers that
//  RunFrontEnd and the settings subsystem drive). The game keeps the
//  settings-directory path in one global shared slot and builds concrete file
//  paths by taking a fresh ref of it and appending a literal filename.
//
//  g_dword_1DBE2F0 is the settings-directory shared-slot global (the IDA name
//  is "dword_1DBE2F0"): .bss (starts null), populated by the settings
//  subsystem before the paths below are built. It is also the base slot used
//  by sub_67C910 / MakeAbnormalShutdownPath / the AccessibilitySettings.xml
//  builders. Exposed to the worker module (SaveSettingsBeforeExit,
//  gamemain.h) which rebuilds the AccessibilitySettings.xml path.
// ============================================================
F3SharedBuffer *g_dword_1DBE2F0 = 0;   // 0x1DBE2F0: settings-directory path slot

// sub_4040C0 (0x4040C0 - 0x40413B): index of the highest set bit (floor log2).
static int TopBitIndex(unsigned int x)
{
    if (x == 0)
        return -1;
    if (x >= 0x80000000u)
        return 31;
    int r = 0;
    while (x >= 2) { x >>= 1; ++r; }
    return r;
}

// sub_403DB0 (0x403DB0 - 0x403E50) -- grow the buffer so it can hold `needed`
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc /
//  sub_4040C0 / memcpy_s / memset) and refs the high-VA heap table (0x02075B24).
//
//  If the block's usable capacity (the low 31 bits of F3SharedBuffer::capacity)
//  is already >= (needed+4) aligned, do nothing. Otherwise allocate a fresh
//  aligned buffer, zero the trailing padding, copy the current payload, free
//  the old payload through the heap table, and store the new pointer + the new
//  capacity (preserving the capacity field's sign bit, the "auto-grow" flag).
static void EnsureBufferCapacity(F3SharedBuffer *blk, unsigned int needed)
{
    unsigned int aligned = (needed + 4) & 0xFFFFFFFCu;
    unsigned int cap     = blk->capacity;
    if (aligned <= (cap & 0x7FFFFFFFu))
        return;

    unsigned int newsz;
    if ((cap & 0x80000000u) == 0)
        newsz = aligned;                                  // exact-fit capacity
    else if (aligned == 0)
        newsz = 0;
    else
    {
        unsigned int v = aligned * 2u - 1u;               // 2*aligned - 1 (32-bit wrap)
        newsz = 1u << TopBitIndex(v);                     // next power of two
    }

    char *newdata = (char *)LHAlloc(newsz);
    unsigned int len  = blk->length;
    unsigned int tail = len + 1;
    unsigned int pad  = ((len + 4) & 0xFFFFFFFCu) - tail; // align4(len+4) - (len+1)
    if (pad)
        memset(newdata + tail, 0, pad);
    memcpy(newdata, blk->data, len);
    newdata[len] = 0;
    if (blk->data != 0)
    {
        LHFree(blk->data);
        blk->data = 0;
    }
    blk->data = newdata;
    blk->capacity = (cap & 0x80000000u) | (newsz & 0x7FFFFFFFu);
}

// sub_403D10 (0x403D10 - 0x403D63) -- append (src, len) to the end of the buffer
//  [NOT byte-identical] placement-dependent (relative calls to sub_403DB0 /
//  memcpy_s / memset).
//
//  Grow the buffer to fit length+len, copy src's len bytes after the current
//  payload, null-terminate, bump the length, and zero the trailing padding.
static void SharedBufferAppend(F3SharedBuffer *blk, const char *src, unsigned int len)
{
    EnsureBufferCapacity(blk, blk->length + len);
    char *dest = blk->data + blk->length;
    memcpy(dest, src, len);
    dest[len] = 0;
    blk->length += len;
    unsigned int newlen = blk->length;
    blk->data[newlen] = 0;
    unsigned int tail = newlen + 1;
    unsigned int pad  = ((newlen + 4) & 0xFFFFFFFCu) - tail; // align4(newlen+4) - (newlen+1)
    if (pad)
        memset(blk->data + tail, 0, pad);
}

// sub_1A01500 (0x1A01500 - 0x1A015BE) -- copy-on-write detach a shared slot
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc /
//  SharedBufferAssign / SharedBufferFree + the Interlocked IAT slots).
//
//  Make the slot own a PRIVATE copy of its block. A unique owner (refcount 1
//  before the check) is already private: the check-increment is undone and the
//  block is left in place. A shared block (refcount >= 2) is copied into a
//  fresh 16-byte control block, the old ref is dropped (freeing the old block
//  on zero), and the slot then points at the private copy.
static void DetachSharedSlot(F3SharedBuffer **slot)
{
    F3SharedBuffer *blk = *slot;
    if (blk == 0)
        return;

    if (InterlockedIncrement(&blk->refcount) <= 2)
    {
        InterlockedDecrement(&blk->refcount);              // unique: undo the check
        return;
    }

    InterlockedDecrement(&blk->refcount);                  // shared: undo the check

    F3SharedBuffer *copy = 0;
    F3SharedBuffer *nb = (F3SharedBuffer *)LHAlloc(16);
    if (nb != 0)
    {
        nb->data = 0;
        nb->length = 0;
        nb->capacity = 0;
        unsigned int len = 0;
        if (blk->data != 0 && *blk->data != 0)
            do { ++len; } while (blk->data[len] != 0);
        SharedBufferAssign(nb, blk->data, len);
        nb->refcount = 1;
        copy = nb;
    }

    if (*slot != 0)
    {
        if (InterlockedDecrement(&(*slot)->refcount) <= 0 && *slot != 0)
            SharedBufferFree(*slot);
        *slot = 0;
    }
    *slot = copy;
}

// sub_1A01880 (0x1A01880 - 0x1A01934) -- append (or create) a string on a slot
//  [NOT byte-identical] placement-dependent (relative calls to DetachSharedSlot
//  / LHAlloc / SharedBufferAssign / SharedBufferFree + the Interlocked IAT).
//
//  Append src to the slot's current string: when the slot already holds a
//  block, DetachSharedSlot makes a private copy and SharedBufferAppend appends
//  src. When the slot is empty, a fresh counted buffer is created from src
//  (SharedBufferCreate, 0x1A01360) and any old block the slot holds is
//  released. The live-slot count (g_Addend) is the caller's responsibility
//  (InitSharedSlot / EngRefSharedSlot own it).
void AppendSharedSlot(F3SharedBuffer **slot, const char *src)
{
    if (*slot != 0)
    {
        DetachSharedSlot(slot);
        unsigned int len = 0;
        if (src != 0 && *src != 0)
            do { ++len; } while (src[len] != 0);
        SharedBufferAppend(*slot, src, len);
        return;
    }

    F3SharedBuffer *nb = 0;
    if (src != 0 && *src != 0)
        nb = SharedBufferCreate(src, -1);

    if (*slot != 0)
    {
        if (InterlockedDecrement(&(*slot)->refcount) <= 0 && *slot != 0)
            SharedBufferFree(*slot);
        *slot = 0;
    }
    if (nb != 0)
        *slot = nb;
}

// sub_1A02230 (0x1A02230 - 0x1A02251) -- build a path as (base slot + literal)
//  [NOT byte-identical] placement-dependent (relative calls to EngRefSharedSlot /
//  AppendSharedSlot).
//
//  Ref the base slot into dst (EngRefSharedSlot, 0x1A012C0), then append the
//  literal filename (AppendSharedSlot, 0x1A01880), so dst = base + name as a
//  shared string. Returns dst. dst / base are the addresses of the slot words
//  (typed F3SharedBuffer **); the original passes them as plain int*.
F3SharedBuffer **MakeSettingsFilePath(F3SharedBuffer **dst, F3SharedBuffer **base,
                                             const char *name)
{
    EngRefSharedSlot(dst, base);
    AppendSharedSlot(dst, name);
    return dst;
}

//  MakeVideoOptionsPath  (0x72F7B0 - 0x72F7CC)  -- settings dir + "VideoOptions.xml"
//  [NOT byte-identical] placement-dependent (relative call to MakeSettingsFilePath
//  + the high-VA string literal and the settings-directory global at 0x1DBE2F0).
//
//  Build the video-options settings-file path: ref the settings-directory
//  shared slot (g_dword_1DBE2F0) into obj and append the "VideoOptions.xml"
//  filename, then return obj. One of several identical builders in the
//  settings cluster (sub_67C910 "Controls\\",
//  MakeAbnormalShutdownPath "abnormal_shutdown.tmp", the
//  AccessibilitySettings.xml builders). obj is a fresh caller slot (the
//  RunFrontEnd v70 local); the caller ignores the returned pointer.
static F3SharedBuffer **MakeVideoOptionsPath(F3SharedBuffer **obj)
{
    return MakeSettingsFilePath(obj, &g_dword_1DBE2F0, "VideoOptions.xml");
}

//  MakeAbnormalShutdownPath  (0x67C930 - 0x67C94C)  -- settings dir + "abnormal_shutdown.tmp"
//  [NOT byte-identical] placement-dependent (relative call to MakeSettingsFilePath
//  + the high-VA string literal and the settings-directory global at 0x1DBE2F0).
//
//  Build the abnormal-shutdown marker path: ref the settings-directory shared
//  slot (g_dword_1DBE2F0) into obj and append the "abnormal_shutdown.tmp"
//  filename, then return obj. One of several identical builders in the
//  settings cluster (MakeVideoOptionsPath "VideoOptions.xml", sub_67C910
//  "Controls\\", the AccessibilitySettings.xml builders). RunFrontEnd uses it
//  to report an existing marker file (info box); the worker WM_CLOSE path
//  uses it to delete the marker on a clean exit. Both callers ignore the
//  returned pointer. Declared in gamemain.h (the worker module calls it too).
F3SharedBuffer **MakeAbnormalShutdownPath(F3SharedBuffer **obj)
{
    return MakeSettingsFilePath(obj, &g_dword_1DBE2F0, "abnormal_shutdown.tmp");
}

// sub_487560 (0x487560 - 0x4875AE): fresh 24-byte copy node from the source
static F3OptionNode *AllocOptionCopyNode(F3OptionNode *parent, F3OptionNode *left,
                                         F3OptionNode *right, F3OptionNode *src,
                                         BYTE color)
{
    F3OptionNode *node = (F3OptionNode *)LHAlloc(24);
    node->link1 = left;
    node->link0 = parent;
    node->link2 = right;
    EngRefSharedSlot(&node->key, &src->key);   // sub_1A012C0(node+12, src+12)
    node->keyLen = src->keyLen;
    node->color = color;
    node->leaf = 0;
    return node;
}

// sub_487400 / sub_487470 / sub_4874E0: the recursive option subtree copy
static F3OptionNode *CopyOptionSubtree(F3OptionMap *obj, F3OptionNode *src, F3OptionNode *dst)
{
    F3OptionNode *root = obj->root;
    if (src->leaf != 0)
        return root;
    F3OptionNode *node = AllocOptionCopyNode(root, dst, root, src, src->color);
    if (root->leaf != 0)
        root = node;
    node->link0 = CopyOptionSubtree(obj, src->link0, node);
    node->link2 = CopyOptionSubtree(obj, src->link2, node);
    return root;
}

// sub_487270 / sub_4872F0 / sub_487370: copy the source map's tree into the
// destination map (a fresh sentinel), then re-link the sentinel's links
static void CopyOptionTree(F3OptionMap *dst, const F3OptionMap *src)
{
    F3OptionNode *node = dst->root;
    node->link1 = CopyOptionSubtree(dst, (F3OptionNode *)src->root, node);
    dst->count = src->count;
    F3OptionNode *n = node->link1;
    if (n->leaf != 0)
    {
        node->link0 = node;
        node->link2 = node;
    }
    else
    {
        F3OptionNode *p = n->link0;
        while (p->leaf == 0)
            p = p->link0;
        node->link0 = p;
        F3OptionNode *last = node->link1->link2;
        while (last->leaf == 0)
            last = last->link2;
        node->link2 = last;
    }
}

// sub_776FD0 / sub_777050 / sub_7770D0 (the three identical map copy-ctors):
// fresh wrapper + sentinel in the destination, then the tree copy
static void CopyOptionMapBase(F3OptionMap *obj, const F3OptionMap *src)
{
    DWORD *w = (DWORD *)LHAlloc(4);
    if (w != 0)
        *w = (DWORD)obj;
    obj->wrapper = w;
    F3OptionNode *node = (F3OptionNode *)LHAlloc(24);
    if (node != 0)
    {
        node->link0 = 0;
        node->link1 = 0;
        node->link2 = 0;
    }
    node->color = 1;
    obj->root = node;
    node->leaf = 1;
    node->link1 = node;
    node->link0 = node;
    node->link2 = node;
    obj->count = 0;
    CopyOptionTree(obj, src);
}
static void CopyIntOptionMap(F3OptionMap *obj, const F3OptionMap *src)    { CopyOptionMapBase(obj, src); }   // sub_776FD0
static void CopyFloatOptionMap(F3OptionMap *obj, const F3OptionMap *src)  { CopyOptionMapBase(obj, src); }   // sub_777050
static void CopyDetailOptionMap(F3OptionMap *obj, const F3OptionMap *src) { CopyOptionMapBase(obj, src); }   // sub_7770D0

// sub_410AB0 / sub_410CC0 (the Int / Float recursive subtree destructions;
// the Detail variant sub_410ED0 skips the key release): for each non-leaf
// node, destroy the right subtree, drop the key ref (freeing it at 0), drop
// the live-slot counter, and free the node
static void DestroyOptionSubtree(F3OptionNode *node)
{
    for (F3OptionNode *n = node; n->leaf == 0; )
    {
        DestroyOptionSubtree(n->link2);
        F3SharedBuffer *key = n->key;
        F3OptionNode *next = n->link0;
        if (key != 0)
        {
            if (InterlockedDecrement(&key->refcount) <= 0 && key != 0)
                SharedBufferFree(key);
            n->key = 0;
        }
        InterlockedDecrement(&g_Addend);
        n->key = 0;
        LHFree(n);
        n = next;
    }
}

// sub_40ECC0 / sub_40EEE0 / sub_40F100 (the "find the sentinel node in the
// map" steps, each taking its identity branch here): destroy the sentinel's
// left subtree, reset the sentinel's links, and write the discarded 8-byte
// result. Then free the sentinel node and the wrapper via the game heap and
// null the object's fields.
static void DestroyOptionMap(F3OptionMap *obj)
{
    DestroyOptionSubtree(obj->root->link1);
    obj->root->link1 = obj->root;
    obj->count = 0;
    obj->root->link0 = obj->root;
    obj->root->link2 = obj->root;
    LHFree(obj->root);            // the sentinel node (dword_2075B24 + 16)
    obj->root = 0;
    obj->count = 0;
    LHFree(obj->wrapper);         // the 4-byte wrapper (dword_2075B24 + 16)
}

// sub_40E1F0 (0x40E1F0 - 0x40E320): destroy the three contiguous option maps
static void DestroyOptionMaps(void *obj)
{
    F3OptionMap *base = (F3OptionMap *)obj;
    DestroyOptionMap(base + 2);   // the Detail map (+0x40, sub_40E3D0)
    DestroyOptionMap(base + 1);   // the Float map (+0x20, sub_40E350)
    DestroyOptionMap(base);       // the Int map (+0x00, the inline tail)
}

// sub_1A00C20 (0x1A00C20 - 0x1A00C2F): the C string of a shared slot
const char *GetSlotString(F3SharedBuffer **slot)
{
    F3SharedBuffer *block = *slot;
    return (block != 0) ? block->data : "";   // Str1 (the empty string)
}

// The CXMLReadStream vtable slots the option readers dispatch through
// (the placeholder g_CXMLReadStreamVtable keeps them zeroed in this build).
typedef struct F3CXMLReadStreamVT
{
    void *                                    vt0;
    char (*enterElement)(void *stream, const char *name);  // +0x04
    void *                                    vt2;
    void (__thiscall *nextSibling)(void *stream);           // +0x0C (thiscall, stream in ECX)
    void (*readPath)(void *stream, void *out);              // +0x10
    void (*readBool)(void *stream, BYTE *out);              // +0x14
    void *                                    vt6;
    void (*readFloat)(void *stream, float *out);            // +0x1C
    void (*readInt2)(void *stream, int *out);               // +0x20
    void (*readInt)(void *stream, int *out);                // +0x24
    void *                                    vt10[10];
    void *(*currentNode)(void *stream);                     // +0x50
} F3CXMLReadStreamVT;

// The 12-byte reader region: the 4-byte key slot + the 8-byte interned
// string object (the original's var_C + var_8 pair).
typedef struct F3OptionReaderRegion
{
    F3SharedBuffer *block;    // +0: the key slot
    DWORD           pathObj[2]; // +4: the interned-string object
} F3OptionReaderRegion;

static DWORD g_stubIntOptionValue = 0;
static DWORD g_stubFloatOptionValue = 0;
static F3SharedBuffer *g_stubDetailValueSlot = 0;

// sub_777150 (0x777150 - 0x777220): find / insert the key in the Int map
static DWORD *FindIntOptionValue(F3OptionMap *obj, F3SharedBuffer **keySlot)
{
    (void)obj;
    (void)keySlot;
    // [remaining stub] the tree key lookup / insert (sub_4122E0 / SpeechBankStrCompare /
    // sub_776BF0) is out of scope; the value store lands in a static word.
    return &g_stubIntOptionValue;
}

// sub_777220 (0x777220 - 0x7772A0?): find / insert the key in the Float map
static DWORD *FindFloatOptionValue(F3OptionMap *obj, F3SharedBuffer **keySlot)
{
    (void)obj;
    (void)keySlot;
    // [remaining stub] as FindIntOptionValue; the value store is a static word.
    return &g_stubFloatOptionValue;
}

// sub_75FE80 (0x75FE80 - 0x75FEE0?): find / insert the key in the Detail map
static DWORD *FindDetailOptionValue(F3OptionMap *obj, F3SharedBuffer **keySlot)
{
    (void)obj;
    (void)keySlot;
    // [remaining stub] as FindIntOptionValue; the value slot is a static word.
    return (DWORD *)&g_stubDetailValueSlot;
}

// sub_777360 (0x777360 - 0x77741A): read the IntSettings section's values
static void ReadIntOptionSection(void *stream, F3SharedBuffer *key, void *dest)
{
    const F3CXMLReadStreamVT *vt = (const F3CXMLReadStreamVT *)*(void **)stream;
    if (vt->enterElement(stream, GetSlotString((F3SharedBuffer **)&key)) != 0)
    {
        void *node = vt->currentNode(stream);
        while (node != 0)
        {
            int value = 0;
            vt->readInt(stream, &value);
            F3SharedBuffer *slot = 0;
            SetSharedSlot(&slot, (const char *)node, -1);   // the key = the node's bytes
            *FindIntOptionValue((F3OptionMap *)dest, &slot) = value;
            ReleaseSharedSlot(&slot);
            vt->nextSibling(stream);
            node = vt->currentNode(stream);
        }
        vt->nextSibling(stream);
    }
    ReleaseSharedSlot((F3SharedBuffer **)&key);
}

// sub_777420 (0x777420 - 0x7774DF): read the FloatSettings section's values
static void ReadFloatOptionSection(void *stream, F3SharedBuffer *key, void *dest)
{
    const F3CXMLReadStreamVT *vt = (const F3CXMLReadStreamVT *)*(void **)stream;
    if (vt->enterElement(stream, GetSlotString((F3SharedBuffer **)&key)) != 0)
    {
        void *node = vt->currentNode(stream);
        while (node != 0)
        {
            float value = 0.0f;
            vt->readFloat(stream, &value);
            F3SharedBuffer *slot = 0;
            SetSharedSlot(&slot, (const char *)node, -1);
            *(float *)FindFloatOptionValue((F3OptionMap *)dest, &slot) = value;
            ReleaseSharedSlot(&slot);
            vt->nextSibling(stream);
            node = vt->currentNode(stream);
        }
        vt->nextSibling(stream);
    }
    ReleaseSharedSlot((F3SharedBuffer **)&key);
}

// sub_17E4D10 (0x17E4D10 - 0x17E4DF8): intern a string in the global table,
// returning the 32-bit handle. [remaining stub] the CStringHashMap interning
// is out of scope; the (unreachable) detail reader interns to 0.
static DWORD InternXmlString(const char *src)
{
    (void)src;
    return 0;
}

// sub_775E10 (0x775E10 - 0x775EDA): read the DetailSettings section's paths
static void ReadDetailOptionSection(void *stream, F3SharedBuffer *key, void *dest)
{
    const F3CXMLReadStreamVT *vt = (const F3CXMLReadStreamVT *)*(void **)stream;
    if (vt->enterElement(stream, GetSlotString((F3SharedBuffer **)&key)) != 0)
    {
        void *node = vt->currentNode(stream);
        while (node != 0)
        {
            F3OptionReaderRegion region;
            region.block = 0;
            region.pathObj[0] = InternXmlString("");   // sub_17E4D10(&region.pathObj, "")
            region.pathObj[1] = 0;
            vt->readPath(stream, &region);             // vt[4]: the path string
            SetSharedSlot(&region.block, (const char *)node, -1);
            SetSaveGamePath((F3SharedBuffer **)FindDetailOptionValue((F3OptionMap *)dest, &region.block),
                            ResolveXmlString(region.pathObj));
            ReleaseSharedSlot(&region.block);
            vt->nextSibling(stream);
            node = vt->currentNode(stream);
        }
        vt->nextSibling(stream);
    }
    ReleaseSharedSlot((F3SharedBuffer **)&key);
}

// ============================================================
//  LoadVideoOptions  (0x00775EE0 - 0x00776012)  -- build the video-options
//  triple (Int / Float / Detail maps) from VideoOptionsConfig.xml
//  [NOT byte-identical] placement-dependent (relative calls to the option-
//  map ctors / copies, the LHXML helpers, the option readers, and the game-
//  heap free) plus the high-VA __security_cookie / __security_check_cookie.
//
//  void *LoadVideoOptions(void *obj)
//
//  Build the 96-byte option-map triple inside obj (the Int / Float / Detail
//  settings, at +0x00 / +0x20 / +0x40): the three 32-byte local maps are
//  constructed on the stack, the "ConfigSettings" section of
//  VideoOptionsConfig.xml is read into them (IntSettings / FloatSettings /
//  DetailSettings), and the locals are heap-copied into obj. The locals and
//  the 24-byte stack LHXML stream are destroyed, and obj is returned.
//  In this build the LHXML parse is a remaining stub, so the section search
//  finds nothing and the maps stay empty (exactly the original's behavior
//  when the file is missing); the map layout, the heap traffic, and the copy
//  / destroy paths are all reproduced.
//  Callers: RunFrontEnd (the 100-byte frontend object).
// ============================================================
static void *LoadVideoOptions(void *obj)
{
    F3XmlStream   xml;         // the 24-byte CXMLReadStream (sub_775EE0's v7)
    F3OptionMap   maps[3];     // the three contiguous 32-byte local maps
    char          dummy = 0;   // the 1-byte v6 local (an unused ctor argument)

    InitIntOptionMap(&maps[0], &dummy, &dummy);          // sub_40E6D0
    InitFloatOptionMap(&maps[1], &dummy, &dummy);        // sub_40E780
    InitDetailOptionMap(&maps[2], &dummy, &dummy);       // sub_40E830
    CXMLReadStreamCtor(&xml);                            // sub_17DC290

    if (OpenXmlFile(&xml, "VideoOptionsConfig.xml") != 0        // sub_17DBD00
        && FindXmlSection(&xml, "ConfigSettings") != 0)         // sub_17DC6D0
    {
        F3SharedBuffer *intKey = 0;
        F3SharedBuffer *floatKey = 0;
        F3SharedBuffer *detailKey = 0;
        SetSharedSlot(&intKey, "IntSettings", -1);              // g_Addend++
        ReadIntOptionSection(&xml, intKey, &maps[0]);           // sub_777360 (g_Addend--)
        SetSharedSlot(&floatKey, "FloatSettings", -1);          // g_Addend++
        ReadFloatOptionSection(&xml, floatKey, &maps[1]);       // sub_777420 (g_Addend--)
        SetSharedSlot(&detailKey, "DetailSettings", -1);        // g_Addend++
        ReadDetailOptionSection(&xml, detailKey, &maps[2]);     // sub_775E10 (g_Addend--)
    }

    CopyIntOptionMap((F3OptionMap *)obj, &maps[0]);             // sub_776FD0
    CopyFloatOptionMap((F3OptionMap *)obj + 1, &maps[1]);       // sub_777050
    CopyDetailOptionMap((F3OptionMap *)obj + 2, &maps[2]);      // sub_7770D0
    CXMLReadStreamDtor(&xml);                                   // sub_17DC2B0
    DestroyOptionMaps(&maps[0]);                                // sub_40E1F0
    return obj;
}

// ============================================================
//  Saved video-options values (LoadVideoOptionsValues, 0x775D10)
//
//  The user's saved video settings live in VideoOptions.xml (the path slot
//  MakeVideoOptionsPath built above). LoadVideoOptionsValues fills a 52-byte
//  options record with the defaults (InitVideoOptionsDefaults, 0x19E1380),
//  opens the file in a 24-byte stack CXMLReadStream, and reads the
//  "VideoOptions" element's fields in (ReadVideoOptionsValues, 0x775A60):
//  Version, AdapterOrdinal, ScreenResolutionX / Y, Fullscreen, VSync, the
//  eight TextureDetail .. DrawDistance values, and Calibrated. A silent
//  report handler (nullsub_33) is installed for the duration of the read.
// ============================================================

// The 52-byte saved video-options record (RunFrontEnd's v103..v106 local).
typedef struct F3VideoOptions
{
    DWORD  adapterOrdinal;     // +0x00
    DWORD  screenResolutionX;  // +0x04
    DWORD  screenResolutionY;  // +0x08
    BYTE   fullscreen;         // +0x0C
    BYTE   pad[3];
    DWORD  detail[8];          // +0x10..+0x2F: Texture / Model / Water / Shadow /
                               //      Effects / Environment / Tree / DrawDistance
    BYTE   vsync;              // +0x30
    BYTE   calibrated;         // +0x31
    BYTE   pad2[2];
} F3VideoOptions;              // 52 bytes

//  InitVideoOptionsDefaults  (0x19E1380 - 0x19E13BF)  -- the 1280x720 defaults
//  [NOT byte-identical] the original is a bare store sequence (no calls);
//  the C++ form writes the same words / bytes to the same offsets, in order.
static F3VideoOptions *InitVideoOptionsDefaults(F3VideoOptions *obj)
{
    obj->adapterOrdinal    = 0;
    obj->screenResolutionX = 1280;
    obj->screenResolutionY = 720;
    obj->calibrated        = 0;
    obj->fullscreen        = 1;
    obj->vsync             = 1;
    for (int i = 0; i < 8; ++i)
        obj->detail[i] = 2;
    return obj;
}

// sub_19E1920 (0x19E1920 - 0x19E1964): set the slot to the i-th detail name
static void SetDetailOptionSlotName(F3SharedBuffer **slot, int index)
{
    static const char *detailNames[8] = {
        "TextureDetail", "ModelDetail", "WaterDetail", "ShadowDetail",
        "EffectsDetail", "EnvironmentDetail", "TreeDetail", "DrawDistance"
    };
    SetSharedSlot(slot, detailNames[index], -1);
}

//  ReadVideoOptionsValues  (0x775A60 - 0x775D04)  -- read the "VideoOptions"
//  element's fields into the record
//  [NOT byte-identical] placement-dependent (relative calls to the shared-
//  slot helpers + the vtable dispatches; the /GS cookie refs high-VA data).
//
//  void ReadVideoOptionsValues(F3XmlStream *stream, F3VideoOptions *opts)
//
//  Enter the "VideoOptions" element; for each named child element found, read
//  its value into the matching record field and advance to the next sibling.
//  The 4-byte key slot is reused for every element (SetSharedSlot takes a ref
//  on each set; each ReleaseSharedSlot drops it). In this build the LHXML
//  parse is a remaining stub, so enterElement never finds the element and
//  the record keeps its defaults (exactly the original's behavior for a
//  missing / empty file).
static void ReadVideoOptionsValues(F3XmlStream *stream, F3VideoOptions *opts)
{
    const F3CXMLReadStreamVT *vt = (const F3CXMLReadStreamVT *)stream->vtable;
    if (vt->enterElement(stream, "VideoOptions") == 0)
        return;

    F3SharedBuffer *slot = 0;      // the one 4-byte slot, reused for every element
    int version = 3;               // the Version child (read into a local, unused)

    SetSharedSlot(&slot, "Version", -1);
    if (vt->enterElement(stream, GetSlotString(&slot)) != 0)
    {
        vt->readInt2(stream, &version);          // vt[8]
        vt->nextSibling(stream);                 // vt[3]
    }
    ReleaseSharedSlot(&slot);

    SetSharedSlot(&slot, "AdapterOrdinal", -1);
    if (vt->enterElement(stream, GetSlotString(&slot)) != 0)
    {
        vt->readInt2(stream, (int *)&opts->adapterOrdinal);   // vt[8]
        vt->nextSibling(stream);
    }
    ReleaseSharedSlot(&slot);

    SetSharedSlot(&slot, "ScreenResolutionX", -1);
    if (vt->enterElement(stream, GetSlotString(&slot)) != 0)
    {
        vt->readInt(stream, (int *)&opts->screenResolutionX); // vt[9]
        vt->nextSibling(stream);
    }
    ReleaseSharedSlot(&slot);

    SetSharedSlot(&slot, "ScreenResolutionY", -1);
    if (vt->enterElement(stream, GetSlotString(&slot)) != 0)
    {
        vt->readInt(stream, (int *)&opts->screenResolutionY); // vt[9]
        vt->nextSibling(stream);
    }
    ReleaseSharedSlot(&slot);

    SetSharedSlot(&slot, "Fullscreen", -1);
    if (vt->enterElement(stream, GetSlotString(&slot)) != 0)
    {
        vt->readBool(stream, &opts->fullscreen);  // vt[5]
        vt->nextSibling(stream);
    }
    ReleaseSharedSlot(&slot);

    SetSharedSlot(&slot, "VSync", -1);
    if (vt->enterElement(stream, GetSlotString(&slot)) != 0)
    {
        vt->readBool(stream, &opts->vsync);       // vt[5]
        vt->nextSibling(stream);
    }
    ReleaseSharedSlot(&slot);

    for (int i = 0; i < 8; ++i)
    {
        SetDetailOptionSlotName(&slot, i);        // sub_19E1920
        if (vt->enterElement(stream, GetSlotString(&slot)) != 0)
        {
            vt->readInt(stream, (int *)&opts->detail[i]);        // vt[9]
            vt->nextSibling(stream);
        }
        ReleaseSharedSlot(&slot);
    }

    SetSharedSlot(&slot, "Calibrated", -1);
    if (vt->enterElement(stream, GetSlotString(&slot)) != 0)
    {
        vt->readBool(stream, &opts->calibrated);  // vt[5]
        vt->nextSibling(stream);
    }
    ReleaseSharedSlot(&slot);
    vt->nextSibling(stream);                      // leave the "VideoOptions" element
}

//  LoadVideoOptionsValues  (0x775D10 - 0x775D9B)  -- load the saved values
//  [NOT byte-identical] placement-dependent (relative calls to the report-
//  handler get / set, the defaults init, the CXMLReadStream ctor / dtor, the
//  LHXML open, the values reader, and ReleaseSharedSlot; the /GS cookie and
//  the high-VA XmlReportNoop literal).
//
//  void *LoadVideoOptionsValues(void *obj, F3SharedBuffer **pathSlot)
//
//  Fill the 52-byte saved video-options record (obj) from the settings
//  VideoOptions.xml (the path in pathSlot): install the silent report
//  handler (sub_17E4F90(nullsub_33), the saved one from sub_17E4F80), write
//  the 1280x720 defaults (sub_19E1380), open the file in a 24-byte stack
//  CXMLReadStream (sub_17DC290 / sub_17DBD00), read the "VideoOptions"
//  element's values in (sub_775A60), restore the report handler, destroy the
//  stream (sub_17DC2B0), and release the path slot. The record pointer (obj)
//  is returned. Callers: RunFrontEnd (the v103..v106 frontend local).
static void *LoadVideoOptionsValues(void *obj, F3SharedBuffer **pathSlot)
{
    F3XmlReportHandler saved = GetXmlReportHandler();     // sub_17E4F80
    SetXmlReportHandler(XmlReportNoop);                   // sub_17E4F90(nullsub_33)
    InitVideoOptionsDefaults((F3VideoOptions *)obj);      // sub_19E1380

    F3XmlStream xml;                                      // the 24-byte CXMLReadStream
    CXMLReadStreamCtor(&xml);                             // sub_17DC290
    if (OpenXmlFile(&xml, GetSlotString(pathSlot)) != 0)  // sub_17DBD00
        ReadVideoOptionsValues(&xml, (F3VideoOptions *)obj);   // sub_775A60
    SetXmlReportHandler(saved);                           // sub_17E4F90

    CXMLReadStreamDtor(&xml);                             // sub_17DC2B0
    ReleaseSharedSlot(pathSlot);
    return obj;
}

//  SaveVideoOptionsFile  (0x775DA0 - 0x775E09)  -- write the saved video options
//  to the settings file
//  [NOT byte-identical] placement-dependent (relative calls to the CXMLWriteStream
//  ctor / dtor, EnsureParentDirectory, ReadVideoOptionsValues, GetSlotString,
//  WriteXmlStreamToFile, ReleaseSharedSlot; the /GS cookie refs high-VA data).
//
//  long SaveVideoOptionsFile(F3VideoOptions *opts, F3SharedBuffer **pathSlot)
//
//  The write-side counterpart of LoadVideoOptionsValues: build a 24-byte stack
//  CXMLWriteStream (sub_17DB800), make sure the path's parent directory exists
//  (EnsureParentDirectory), write the "VideoOptions" element's fields from the
//  52-byte record into the stream (ReadVideoOptionsValues, 0x775A60 -- the same
//  generic element accessor used by the reader, dispatched through the write
//  stream's vtable), serialize the tree to the file (sub_17DB2C0), destroy the
//  stream (sub_17DB830), and release the path slot (the long slot count is
//  returned). In this build the LHXML serializer is a remaining stub, so the
//  element is never entered and only the XML prologue reaches the file (exactly
//  the original's behavior with no serializable content). Callers: RunFrontEnd
//  (re-saves the options when the cached resolution / adapter words change) and
//  sub_72F990 (a settings path-builder for VideoOptions.xml).
static long SaveVideoOptionsFile(F3VideoOptions *opts, F3SharedBuffer **pathSlot)
{
    F3XmlStream xml;                                       // the 24-byte CXMLWriteStream
    CXMLWriteStreamCtor(&xml);                             // sub_17DB800
    EnsureParentDirectory(pathSlot);                       // 0x18B1360
    ReadVideoOptionsValues(&xml, opts);                    // 0x775A60
    WriteXmlStreamToFile(&xml, GetSlotString(pathSlot));   // sub_17DB2C0
    CXMLWriteStreamDtor(&xml);                             // sub_17DB830
    return ReleaseSharedSlot(pathSlot);                    // 0x1A01320
}

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
//  SharedSlotCloneWide  (0x1A00580 - 0x1A00615)  -- fresh wide block from a slot
//  [NOT byte-identical] placement-dependent (relative calls to the
//  InterlockedIncrement IAT, LHAlloc, and SharedBufferAssignWide) plus the
//  empty-string "Str1" global (0x01BD44D7) at its original high VA.
//
//  F3SharedBuffer **SharedSlotCloneWide(F3SharedBuffer **destSlot, F3SharedBuffer **srcSlot)
//
//  Build a fresh wide-text counted block holding a char->wide copy of the
//  source slot's string and store it in destSlot: bump the live-wide-slot
//  counter (g_dword_2075C4C), LHAlloc(16) a control block (0 on OOM), zero
//  data / length / capacity, SharedBufferAssignWide it to the source length
//  (the length field of the source block, 0 when the slot holds no block),
//  set the refcount to 1, store the block in destSlot, then copy each source
//  byte into a 16-bit code unit of the new buffer (zero-extended). The source
//  string falls back to the empty string (Str1) when the slot holds no block,
//  but the copy loop is skipped entirely in that case (length 0). Returns
//  destSlot. This is the core of SharedSlotToWide (0x1A02560); the sibling
//  sub_1A00620 wraps the same core with a plain cdecl convention. The copy
//  loop dereferences the new block's buffer unconditionally in the original
//  (it faults on OOM when the length is nonzero; modelled the same).
// ============================================================
static F3SharedBuffer **SharedSlotCloneWide(F3SharedBuffer **destSlot, F3SharedBuffer **srcSlot)
{
    InterlockedIncrement(&g_dword_2075C4C);

    F3SharedBuffer *src = (srcSlot != 0 && *srcSlot != 0) ? *srcSlot : 0;
    unsigned int len = (src != 0) ? src->length : 0;

    F3SharedBuffer *blk = (F3SharedBuffer *)LHAlloc(16);
    if (blk != 0)
    {
        blk->data     = 0;
        blk->length   = 0;
        blk->capacity = 0;
        SharedBufferAssignWide(blk, len);
        blk->refcount = 1;
    }
    else
        blk = 0;

    *destSlot = blk;
    const WCHAR *dst  = (blk != 0) ? (const WCHAR *)blk->data : 0;
    const char  *srcd = (src != 0) ? src->data : "";   // Str1 (0x01BD44D7): empty string
    for (unsigned int i = 0; i < len; i++)
        ((WCHAR *)dst)[i] = (unsigned char)srcd[i];
    return destSlot;
}

// ============================================================
//  SharedSlotToWide  (0x1A02560 - 0x1A02572)  -- convert a slot's string into a
//  fresh wide block
//  [NOT byte-identical] placement-dependent (relative call to
//  SharedSlotCloneWide) and a thiscall + `retn 4` hybrid in the original
//  (source slot in ECX, destination slot on the stack), so it is declared as
//  a plain free function (first param = the thiscall source slot) here.
//
//  F3SharedBuffer **SharedSlotToWide(F3SharedBuffer **thisSlot, F3SharedBuffer **destSlot)
//
//  Convert the source slot's (byte) string into a fresh wide-text block in the
//  destination slot and return the destination slot (SharedSlotCloneWide,
//  0x1A00580). ~150 call sites across the engine, mostly where a shared-slot
//  string (often a lowercased localized-lookup slot) needs to become a wide
//  string for UI / manager dispatch; RunFrontEnd passes the converted slot
//  into the manager "run the game" vtable slot. Declared as a plain free
//  function to match the project convention for thiscall hybrids.
// ============================================================
static F3SharedBuffer **SharedSlotToWide(F3SharedBuffer **thisSlot, F3SharedBuffer **destSlot)
{
    return SharedSlotCloneWide(destSlot, thisSlot);
}

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
//  sub_1329EE0, SaveSettingsBeforeExit, InitAccessibilitySettings.
// ============================================================
int SharedFileExists(F3SharedBuffer **obj)
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
//  DeleteSharedFile  (0x18B0420 - 0x18B0494)
//  [NOT byte-identical] placement-dependent (relative calls to the resolver,
//  SharedBufferFree and the DeleteFileA / InterlockedDecrement IAT slots)
//  plus the high-VA globals Str1 (the empty string at 0x01BD44D7) and
//  g_Addend (0x02075C48).
//
//  int DeleteSharedFile(F3SharedBuffer **slot)
//
//  "Delete the (resolved) path of this shared slot." The slot is resolved
//  through the path resolver (sub_18AFB90), which stores a fresh block
//  holding the process CWD (ANSI text, a trailing backslash appended) in
//  the local slot -- the source slot is effectively ignored (the resolver
//  clears it before re-deriving the path), exactly as in SharedFileExists.
//  DeleteFileA is called on the resolved text (the empty string when the
//  local block is null), then, on both branches, the local block's
//  reference is dropped (InterlockedDecrement; free at zero) and g_Addend
//  is decremented. Because the resolver returns the CWD -- a directory --
//  DeleteFileA always fails and 0 is returned. Callers: SaveSettingsBeforeExit
//  (worker.cpp, the AccessibilitySettings.xml delete) and the worker
//  WM_CLOSE path (worker.cpp, the abnormal_shutdown.tmp marker). Exposed in
//  gamemain.h for the worker module.
// ============================================================
int DeleteSharedFile(F3SharedBuffer **slot)
{
    F3SharedBuffer *local = 0;

    ResolvePathSlot(&local, slot);

    const char *path = (local != 0) ? local->data : "";   // "" = Str1, 0x1BD44D7
    int result = (int)DeleteFileA(path);
    if (local != 0)
    {
        if (InterlockedDecrement(&local->refcount) <= 0)
            SharedBufferFree(local);
    }
    InterlockedDecrement(&g_Addend);
    return result;
}

// ============================================================
//  Accessibility settings (the SystemParametersInfo save/restore values and
//  the AccessibilitySettings.xml reader). The game snapshots the three system
//  parameters it may override (SPI_GETHIDCONFIRM 0x3A, SPI_GETKEYBOARDSPEED
//  0x34, SPI_GETKEYBOARDDelay 0x32) at start-up, applies the saved overrides
//  from AccessibilitySettings.xml, and restores the snapshot on exit (the
//  matching SPI_SET* calls live in SaveSettingsBeforeExit, worker.cpp).
// ============================================================

// 0x1D9E18C / 0x1D9E194: the SPI_GETHIDCONFIRM / SPI_GETKEYBOARDSPEED
// snapshot (the .data initial values stand until start-up overwrites them
// with the live system values). Non-static: SaveSettingsBeforeExit
// (worker.cpp) restores from them (see gamemain.h).
DWORD g_dword_1D9E18C = 8;
DWORD g_dword_1D9E194 = 8;
// 0x1D9E19C: the SPI_GETKEYBOARDDelay snapshot (the 4-byte value block
// passed to SystemParametersInfoA; the .data initial value is 24).
BYTE  g_byte_1D9E19C[4] = { 0x18, 0, 0, 0 };

// 0x1D9E190 / 0x1D9E198 / 0x1D9E1A0 (byte_1D9E19C+4): the shared slots
// (F3SharedBuffer*) of the three accessibility-setting values loaded by
// InitAccessibilitySettings (.bss, start null).
static F3SharedBuffer *g_dword_1D9E190 = 0;
static F3SharedBuffer *g_dword_1D9E198 = 0;
static F3SharedBuffer *g_dword_1D9E1A0 = 0;

// 0x20FC8D8: the accessibility-settings section name slot (F3SharedBuffer*);
// .bss, so empty = the "" (root) section. 0x20FC8DC / 0x20FC8E0 / 0x20FC8E4:
// the name slots of the three accessibility-setting elements (.bss, empty =
// the "" default).
static F3SharedBuffer *g_dword_20FC8D8 = 0;
static F3SharedBuffer *g_dword_20FC8DC = 0;
static F3SharedBuffer *g_dword_20FC8E0 = 0;
static F3SharedBuffer *g_dword_20FC8E4 = 0;

// ============================================================
//  ReadXmlNamedValue  (0x18F6600 - 0x18F6641)  -- sub_18F6600
//  [NOT byte-identical] refs the high-VA empty-string global Str1
//  (0x01BD44D7); the virtual dispatch itself is preserved through the
//  stream's vtable pointer.
//
//  DWORD ReadXmlNamedValue(F3SharedBuffer **nameSlot, F3XmlStream *stream,
//                          DWORD *out)
//
//  Enter the named section of the stream and read its value: the name is the
//  C string held by nameSlot (the empty string when the slot is null). When
//  the stream enters it (vtable[1] = FindXmlSection), the current element's
//  value is read into *out (vtable[8] = sub_17DBE60) and the result is the
//  advance to the next sibling (vtable[3] = XmlAdvance); 0 when the named
//  section is not found. The settings readers/writers (InitAccessibilitySettings
//  / ApplyDisplaySettings and the save-file loaders) use this to pull one
//  int64 value out of the current XML element.
// ============================================================
static DWORD ReadXmlNamedValue(F3SharedBuffer **nameSlot, F3XmlStream *stream,
                               DWORD *out)
{
    const char *name = (nameSlot != 0 && *nameSlot != 0) ? (*nameSlot)->data : "";
    void **vt = (void **)stream->vtable;
    if (((char (*)(F3XmlStream *, const char *))vt[1])(stream, name) != 0)
    {
        ((void (*)(F3XmlStream *, DWORD *))vt[8])(stream, out);
        return (DWORD)((int(__thiscall *)(F3XmlStream *))vt[3])(stream);
    }
    return 0;
}

// ============================================================
//  InitAccessibilitySettings  (0x18F72B0 - 0x18F7455)
//  [NOT byte-identical] placement-dependent (relative calls to
//  RefSharedSlot / AppendSharedSlot / SharedFileExists / SharedBufferFree /
//  CXMLReadStreamCtor / OpenXmlFile / FindXmlSection / ReadXmlNamedValue /
//  XmlAdvance / SaveSettingsBeforeExit / CXMLReadStreamDtor and the
//  SystemParametersInfoA / InterlockedDecrement IAT slots) plus the high-VA
//  globals below, the settings-directory slot g_dword_1DBE2F0, g_Addend and
//  the "AccessibilitySettings.xml" literal.
//
//  LONG InitAccessibilitySettings(void)
//
//  The accessibility-settings start-up load. First the current values of the
//  three system parameters are snapshotted (SPI_GETHIDCONFIRM 0x3A and
//  SPI_GETKEYBOARDSPEED 0x34 into g_dword_1D9E18C / g_dword_1D9E194,
//  SPI_GETKEYBOARDDelay 0x32 into g_byte_1D9E19C) for the later restore in
//  SaveSettingsBeforeExit. Then, when "AccessibilitySettings.xml" exists in
//  the settings directory (a ref of the g_dword_1DBE2F0 slot + the filename,
//  checked with SharedFileExists), a CXMLReadStream is opened on it and, when
//  the section named by g_dword_20FC8D8 (empty by default = the root) is
//  found, each of the three named values (g_dword_20FC8DC / g_dword_20FC8E0 /
//  g_dword_20FC8E4, empty by default) is loaded (ReadXmlNamedValue) into the
//  g_dword_1D9E190 / g_dword_1D9E198 / g_dword_1D9E1A0 slots and the stream
//  advances past it. SaveSettingsBeforeExit then runs, the stream is
//  destroyed, and 0 is returned. When the file does not exist the
//  InterlockedDecrement counter value is returned as-is (the defaults stand).
//  Caller: CWorkerWin32_RunGame (worker.cpp).
// ============================================================
LONG InitAccessibilitySettings(void)
{
    SystemParametersInfoA(0x3A, 8, (PVOID)&g_dword_1D9E18C, 0);   // SPI_GETHIDCONFIRM
    SystemParametersInfoA(0x34, 8, (PVOID)&g_dword_1D9E194, 0);   // SPI_GETKEYBOARDSPEED
    SystemParametersInfoA(0x32, 0x18, (PVOID)g_byte_1D9E19C, 0);  // SPI_GETKEYBOARDDelay

    F3SharedBuffer *path = 0;
    RefSharedSlot(&path, &g_dword_1DBE2F0);
    AppendSharedSlot(&path, "AccessibilitySettings.xml");
    char exists = SharedFileExists(&path);
    if (path != 0)
    {
        if (InterlockedDecrement(&path->refcount) <= 0)
            SharedBufferFree(path);
        path = 0;
    }
    long result = InterlockedDecrement(&g_Addend);

    if (exists != 0)
    {
        F3XmlStream stream;
        CXMLReadStreamCtor(&stream);

        F3SharedBuffer *path2 = 0;
        RefSharedSlot(&path2, &g_dword_1DBE2F0);
        AppendSharedSlot(&path2, "AccessibilitySettings.xml");
        const char *fileName = (path2 != 0) ? path2->data : "";
        char opened = OpenXmlFile(&stream, fileName);
        if (path2 != 0)
        {
            if (InterlockedDecrement(&path2->refcount) <= 0)
                SharedBufferFree(path2);
            path2 = 0;
        }
        InterlockedDecrement(&g_Addend);

        if (opened != 0)
        {
            const char *rootName = (g_dword_20FC8D8 != 0) ? g_dword_20FC8D8->data : "";
            if (FindXmlSection(&stream, rootName) != 0)
            {
                F3SharedBuffer *value = 0;
                ReadXmlNamedValue(&g_dword_20FC8DC, &stream, (DWORD *)&value);
                g_dword_1D9E190 = value;
                ReadXmlNamedValue(&g_dword_20FC8E0, &stream, (DWORD *)&value);
                g_dword_1D9E198 = value;
                ReadXmlNamedValue(&g_dword_20FC8E4, &stream, (DWORD *)&value);
                g_dword_1D9E1A0 = value;
                ((int(__thiscall *)(F3XmlStream *))XmlAdvance)(&stream);
            }
        }

        SaveSettingsBeforeExit();
        CXMLReadStreamDtor(&stream);
        return 0;
    }
    return (LONG)result;
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
//  F3SharedBuffer ** AnsiFromWideSlot(F3SharedBuffer **wide, F3SharedBuffer **out)
//
//  Build a fresh ANSI block holding the low byte of each wide code unit of
//  the source wide block (null-terminated, length + 1 bytes) and store it in
//  the out slot via SetSharedSlot (g_Addend++), freeing the temporary buffer.
//  On allocation failure the out slot is still set through SetSharedSlot with
//  no source (the slot is nulled, g_Addend bumped, no block).
// ============================================================
F3SharedBuffer ** AnsiFromWideSlot(F3SharedBuffer **wide, F3SharedBuffer **out)
{
    // `wide` is a pointer to the F3SharedBuffer slot (matches the original, which
    // loads the control block with `mov eax, [esi]` and then `mov eax, [eax+4]`
    // for the length). Dereference once to reach the control block.
    F3SharedBuffer *blk = (wide != 0) ? *wide : 0;
    unsigned int len    = (blk != 0) ? blk->length : 0;
    const char *data    = (blk != 0) ? blk->data : 0;
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
    AnsiFromWideSlot(&cwdRef, &ansiCwd);     // sub_19FFAB0
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
//  Callers: RunFrontEnd (the abnormal-shutdown file's directory),
//  SaveVideoOptionsFile (0x775DA0), sub_BC9B50, CDiskFileWin32Open,
//  ApplyDisplaySettings.
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

// 0x1E2EDA0: the device display-mode state word in the frontend device
// state block (the .data region around 0x1E2ED40..0x1E2EEF0 image-initializes
// to 0xFFFFFFFF). No code in the exe writes it — the only reference in the
// whole binary is this getter's read (a byte scan for the 4-byte VA over
// .text/.rdata/.data/.idata/.orpc/.rsrc finds just the one cmp) — so the
// initial image value 0xFFFFFFFF is kept and the getter returns 0 in this
// build, exactly as it does before any device write in the original.
static DWORD g_dword_1E2EDA0 = 0xFFFFFFFF;

// ============================================================
//  IsFullScreen  (0x18F02B0 - 0x18F02BC)  -- the device's current
//  display-mode getter  [NOT byte-identical] placement-independent (no
//  calls / relative jumps), but refs the device state word
//  dword_1E2EDA0 (0x01E2EDA0) at its original high VA not present in the
//  build.
//
//  int IsFullScreen(void)   (cdecl, no args, 0 / 1 in EAX)
//
//  Returns (dword_1E2EDA0 == 0) — the device's current fullscreen state
//  word: nonzero (fullscreen) when the word is clear. RunFrontEnd uses it
//  as the "current fullscreen" side of the saved-options comparison (the
//  record's Fullscreen byte, +0x0C) and, when the cached values changed,
//  stores it back into the record before re-saving VideoOptions.xml.
//  Callers: RunFrontEnd only (the two call sites at 0x61CD98 / 0x61CDCA).
//  Replaces the sub_18F02B0 no-op stub in RunFrontEnd.
// ============================================================
static int IsFullScreen(void)
{
    return g_dword_1E2EDA0 == 0;
}

// qword_20FC928 / dword_20FC92C: the current display resolution
// {width, height}, set by the device creation (CreateXloDevice, sub_18F1B30) from the
// display-mode record. Read by GetCurrentResolution (0x18F1100), the
// aspect-ratio getters (sub_18F0AA0 / sub_18F1120) and the 5%/95% split
// helper (sub_18F0AE0).
DWORD g_qword_20FC928[2] = { 0, 0 };

// ============================================================
//  GetCurrentResolution  (0x18F1100 - 0x18F1115)  -- the current display
//  resolution  [NOT byte-identical] refs the high-VA globals
//  qword_20FC928 / dword_20FC92C (0x020FC928 / 0x020FC92C) at their
//  original addresses.
//
//  Copies the current display resolution {width, height} out of the device
//  globals into the 2-DWORD out parameter and returns it (cdecl, out pointer
//  on the stack). Callers: RunFrontEnd (0x61CB70; re-saves the video options
//  when the cached width/height words change) and sub_837D40 (UI aspect
//  computation, alongside the sibling clones sub_18F0140 / sub_18F02C0).
// ============================================================
static DWORD *GetCurrentResolution(DWORD *out)
{
    out[0] = g_qword_20FC928[0];
    out[1] = g_qword_20FC928[1];
    return out;
}

// ============================================================
//  ResetGraphicsSurfaces  (0x18E6D40 - 0x18E700C)  -- re-initialize the
//  frontend graphics surfaces after a device create / reset or a
//  display-mode change.  [NOT byte-identical] placement-dependent
//  (relative calls to the graphics-surface subsystem callees below and
//  the memset IAT slot) and refs the high-VA globals device object
//  (dword_1E2EEF4), surface records (off_1DB12F8 / off_1DB1314), the
//  device-acquired surface (dword_1DB1318), the surface param
//  (dword_1D9B040), the surface flag (byte_2075F05) and the two 0x40-byte
//  state blocks (dword_1E2EF10 / dword_1E2EF60) at their original
//  addresses.
//
//  Flow:
//    1. Reset graphics state (ResetGraphicsEntities(device)) + re-register
//       the graphics state records (ReRegisterGraphicsState).  flags =
//       byte_2075F05 ? 0x200 : 0;  fmt =
//       (byte_2075F05 == 0).
//    2. Create a 1x1 texture (table row 2) into the global CTexture record
//       off_1DB12F8 (CreateTexture, sub_18D33B0); wrap texture slot 0 of the
//       global CSurface record off_1DB12F8 into a frame-local wrapper
//       (RefreshCsurfaceSlot, 0x18D36C0); clear it with the near-black color
//       {0, fmt, 1, 1}/255 (ClearCsurface, 0x18D2940); release the wrapped
//       surface.
//    3. Create a 2x1x1x1 surface (CreateCsurface, 0x1A042B0, ->
//       dword_1DB1334); release
//       the existing device-acquired surface (dword_1DB1318), then
//       acquire a fresh one through device vtable[25] (device, 1, 1,
//       flags, dword_1D9B040, fmt, &dword_1DB1318, 0).
//    4. For texture slots 0..5 of off_1DB1314: wrap (sub_1A046D0), clear
//       with the same near-black color (ClearCsurface), release.
//    5. Zero the 0x40-byte state blocks at 0x1E2EF10 / 0x1E2EF60 and the
//       two qwords at 0x1E2EF50 / 0x1E2EF58.
//
//  Callers: RunFrontEnd (0x61CB70, after the video options are saved) and
//  sub_19D4FC0 (after SetDisplayMode).  A null-guard on the device object
//  is added where the original would fault (pre-device slot); the original
//  reads the device vtable unconditionally.
// ============================================================

// 0x1E2EEF4: the current XLO device object (a vtable-backed pointer).
// Defined once in common/common.cpp and declared in common/common.h
// (included above), so worker.cpp / gamemain.cpp / xlo_device.cpp share it.
// 0x2075F05: surface-creation flag (nonzero -> 0x200 create flag + format 0).
BYTE   g_byte_2075F05   = 0;
// 0x1D9B040: surface param passed to device vtable[25].
static DWORD g_dword_1D9B040  = 0;
// 0x1DB12F8 / 0x1DB1314: global 32-byte CSurface records {vtable, surface,
// type, fmtInfo[4]} used as surface-wrapper source slots.
static DWORD g_off_1DB12F8[8] = {0};
static DWORD g_off_1DB1314[8] = {0};
// 0x1DB1318: the device-acquired frontend surface (device vtable[25]).
static void *g_dword_1DB1318  = 0;
// 0x1DB1334: the frontend CSurface created from a texture source-descriptor
// table row (CreateCsurface, device vtable[3]); released on re-create.
static void *g_dword_1DB1334  = 0;
// 0x1E2EF10 / 0x1E2EF60: 0x40-byte surface state blocks zeroed on reset.
static DWORD g_dword_1E2EF10[16] = {0};
static DWORD g_dword_1E2EF60[16] = {0};
// 0x1E2EF50 / 0x1E2EF58: two qwords between the two state blocks.
static DWORD g_qword_1E2EF50[2]  = {0, 0};

// --- Frontend graphics-entity subsystem globals (ResetGraphicsEntities) ---
// 0x2120420: base of the frontend graphics-entity pool; the first 0x1DF8
// bytes are the entity-pool region (zeroed on reset, then re-initialised by
// the pool builder BuildGraphicsEntityPool).  RegisterGraphicsEntities
// (0x18D04D0) additionally reads 28-byte descriptor records at pool offsets
// up to ~0x3608 (the second-loop table bases 0x2120A08..0x2121DB8 plus the
// high offsets 0x1C24..0x1C58); that tail sits past the 0x1DF8 memset region
// and stays zero (BSS) between resets, so the buffer is sized to cover it.
// Modelled as one static at the original high VA (the memset target).
static DWORD g_dword_2120420[0x3800/4] = {0};
// 0x2073938: count of dirty resource registrations (reset to 0 on reset).
static DWORD g_dword_2073938 = 0;
// 0x2063134: count of entities in the pool (reset to 0 on reset).
static DWORD g_dword_2063134 = 0;
// 0x2073134: the entity index list - 266 DWORD slots, each a pointer into
// the entity pool.  Slot 0 is unused (zero).  BuildGraphicsEntityPool
// (0x18CFA70) fills slots 1..265 (265 entries) and sets g_dword_2063134.
// (In the binary the list is accessed through three overlapping views:
// dword_2073134 / dword_2073138 / dword_207313C.)
static void *g_dword_2073134[266] = {0};
// 0x211A408: the frontend graphics-entity OBJECT pool.  Each object is
// 0x2008 (8200) bytes: a list of 1024 {ptr, key} entries at +0x0000, the
// mask-source word at +0x2000 and the running entry count at +0x2004.
// RegisterGraphicsEntities (0x18D04D0) appends each freshly-registered
// descriptor to the per-object list.  The descriptor index bytes reference
// only objects 0..2, but the index is a full byte so 256 objects are kept to
// cover the whole space (matches the original 8200-byte stride indexing).
struct F3GraphicsEntityObj
{
    DWORD list[2048];  // +0x0000: 1024 x {ptr, key}
    DWORD maskSrc;     // +0x2000: mask source OR'd into the descriptor mask
    DWORD count;       // +0x2004: number of registered {ptr,key} entries
};
static F3GraphicsEntityObj g_objPool[256];
// 0x2073940: the dirty-registration pointer list appended by
// RegisterGraphicsEntities; its length is g_dword_2073938 (224 slots max, one
// per descriptor, so 256 is a safe bound).
static DWORD g_dword_2073940[256] = {0};
// 0x211C402: graphics-entity subsystem state byte (set to 3 on reset).
static BYTE  g_byte_211C402  = 0;
// 0x2075F54: the device object the entities are synced to (vtable-backed).
static void *g_dword_2075F54 = 0;
// 0x2075F4D: entity-sync flag (cleared on reset).
static BYTE  g_byte_2075F4D  = 0;
// 0x1DB4B58: the type-1 graphics-entity value table (indexed by the
// descriptor's +0x14 id; a slot whose value changes notifies the device via
// vtable[57], offset 0xE4).  Sized past the largest id (195) the pool builder
// writes.
static DWORD g_dword_1DB4B58[0x200] = {0};
// 0x1DB4F58: the type-2 graphics-entity value table (indexed 16*slot + code;
// a changed slot notifies the device via vtable[69], offset 0x114).  Sized
// past the largest reachable index (16*260 + 11) the pool builder writes.
static DWORD g_dword_1DB4F58[0x1400] = {0};

// A 32-byte CSurface record / frame-local wrapper: {vtable, surface, type,
// fmtInfo[5]}.  RefreshCsurfaceSlot (0x18D36C0) / sub_1A046D0 refresh a
// frame-local wrapper from a global record; ClearCsurface (0x18D2940)
// clears it (reading the format index at +12).
struct F3CsurfaceSlot
{
    void *vt;          // NGraphics::CSurface::`vftable' after refresh
    void *surface;     // the wrapped surface
    int   type;        // 3 after refresh
    int   fmtInfo[5];  // QueryCsurfaceFormat (0x5A2EF0) output (format index, ...)
};

// The remaining graphics-surface subsystem callees (unimplemented in this
// build; their real bodies live at the original addresses) are stubbed to
// no-ops / trivial returns: the index-wrap (sub_1A046D0) has no visible
// effect (the 2x1x1x1 surface create, CreateCsurface 0x1A042B0, is
// implemented below).  The surface clear
// (ClearCsurface, 0x18D2940), RefreshCsurfaceSlot (0x18D36C0) and its
// format helper QueryCsurfaceFormat (0x5A2EF0) are implemented below: the
// wrap refreshes the
// record (vtable, surface, type 3, the 5-word format block) but leaves the
// wrapped surface null pre-device (the resource is uncreated), so the format
// block stays 0.
// 0x207393C: index of the next state descriptor to write (the pending
// batch's current length).  0x2075F58: the record global the pending
// batch belongs to.
static DWORD g_stateDescCount = 0;
static void *g_stateDescOwner = 0;

// 0x2074140: the 128-entry (0x400-byte) state descriptor table
// (word_2074140 / word_2074142 / byte_2074144..byte_2074147 in IDA).  A
// pending batch is a run of these entries terminated by a {0x00FF, 0,
// 0x11, 0, 0, 0} entry; the device's state method reads the table until the
// terminator.
struct F3StateDescriptor
{
    WORD field;   // +0x0: field index within the record (a3)
    WORD value;   // +0x2: value / offset (a4)
    BYTE code1;   // +0x4: size-class code g_byte_1D9ACD8[a1]
    BYTE code2;   // +0x5: the constant byte_1D9ACBC (0)
    BYTE code3;   // +0x6: format-class code g_byte_1D9AD60[a5]
    BYTE flags;   // +0x7: a6
};
static F3StateDescriptor g_stateDescTable[128] = {{0}};

// --- rdata class tables (the original reads these at their high VAs) ---
// 0x1D9ACD8: descriptor code1 per size class a1.
static const BYTE  g_byte_1D9ACD8[] = { 0x00, 0x04, 0x01, 0x08, 0x02, 0x0C, 0x03 };
// 0x1D9ACDC: value delta per size class a1, added to a4 when the record's
// field slot is written.
static const DWORD g_dword_1D9ACDC[] = { 0x04, 0x01, 0x08, 0x02, 0x0C, 0x03, 0x10 };
// 0x1D9AD60: descriptor code3 per format class a5.
static const BYTE  g_byte_1D9AD60[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0xA, 0xB, 0xC, 0xD };

// ============================================================
//  AddStateRegistration  (0x18CDB70 - 0x18CDC3C)  -- append one field
//  descriptor to the pending state-registration batch.
//  [NOT byte-identical] the original is a register-parameter hybrid
//  (a1 in EDI, a2 in ESI, four stack args, retn 0x10) with a security
//  cookie, and refs the high-VA globals descriptor table word_2074140,
//  count dword_207393C, owner pointer dword_2075F58, and the class tables
//  byte_1D9ACBC / byte_1D9ACD8 / byte_1D9AD60 / dword_1D9ACDC.
//
//  int AddStateRegistration(int a1, DWORD *a2, int a3, int a4, int a5, int a6)
//
//  The device keeps a table of "state field" descriptors that bind global
//  state records to device state.  A2 is the address of a 36-byte state
//  record ({state object, field cursor, ...}); the record's state object is
//  the pending registration's owner.  Behavior:
//    1. If the record's state object (record+0) is nonzero, flush the
//       previous owner first: obj->vt[4](obj, table, &count) with count
//       pre-loaded with the table capacity 0x80 (128), set the descriptor
//       count to count-1, obj->vt[2](obj), then clear record+0.
//    2. Append one descriptor at index dword_207393C: {a3, a4,
//       g_byte_1D9ACD8[a1], 0 (byte_1D9ACBC), g_byte_1D9AD60[a5], a6}.
//    3. Write the record's field slot record[a3+1] = a4 +
//       g_dword_1D9ACDC[a1] (a running registration cursor when a3 == 0),
//       set the owner pointer dword_2075F58 = a2, and increment the
//       count, which is returned.
//
//  ~33 call sites (AddStateRegistration is the batch's field-adder; the
//  state object's vt[4]/vt[2] slots are called with the object as the first
//  stack argument, matching this codebase's device-method convention).
// ============================================================
static int AddStateRegistration(int a1, DWORD *a2, int a3, int a4, int a5, int a6)
{
    void *obj = (void *)a2[0];
    if (obj != 0)
    {
        int count = 0x80;
        ((void (*)(void *, F3StateDescriptor *, int *))(*(void ***)obj)[4])
            (obj, g_stateDescTable, &count);
        obj = (void *)a2[0];          // the original re-reads it after vt[4]
        g_stateDescCount = (DWORD)(count - 1);
        ((void (*)(void *))(*(void ***)obj)[2])(obj);
        a2[0] = 0;
    }

    int idx = (int)g_stateDescCount;
    F3StateDescriptor *e = &g_stateDescTable[idx];
    e->code1 = g_byte_1D9ACD8[a1];
    e->code2 = 0;                          // the constant byte_1D9ACBC (0)
    e->code3 = g_byte_1D9AD60[a5];
    e->flags = (BYTE)a6;
    e->field = (WORD)a3;
    e->value = (WORD)a4;
    a2[a3 + 1] = (DWORD)(a4 + g_dword_1D9ACDC[a1]);
    g_stateDescOwner = (void *)a2;
    g_stateDescCount = (DWORD)(idx + 1);
    return (int)g_stateDescCount;
}

// ============================================================
//  SubmitStateRegistration  (0x18CDE00 - 0x18CDE63)  -- append the
//  terminator entry and hand the pending state-registration batch to the
//  device's state method.
//  [NOT byte-identical] refs the high-VA globals descriptor table
//  word_2074140, count dword_207393C, owner pointer dword_2075F58, and the
//  device object dword_1E2EEF4.  A null-guard on the device object is added
//  where the original would fault (pre-device slot).
//
//  int SubmitStateRegistration(void *record)
//
//  1. Writes the terminator descriptor {0x00FF, 0, 0x11, 0, 0, 0} at the
//     current index (one past the last field entry; the device reads the
//     table until it).
//    2. Calls the device state method, device vtable slot 86 (offset
//       0x158): result = vt[86](device, table, record) with the device
//       object (dword_1E2EEF4) as the first stack argument.
//    3. Resets the batch: owner pointer dword_2075F58 = 0, descriptor
//       count dword_207393C = 0; returns the device method's result.
//
//  ~33 call sites (one per registered state record, paired with its
//  AddStateRegistration fields).
// ============================================================
static int SubmitStateRegistration(void *record)
{
    int idx = (int)g_stateDescCount;
    F3StateDescriptor *e = &g_stateDescTable[idx];
    e->field = 0xFF;
    e->value = 0;
    e->code1 = 0x11;
    e->code2 = 0;
    e->code3 = 0;
    e->flags = 0;

    int result = 0;
    if (g_dword_1E2EEF4 != 0)   // null-guard; the original would fault pre-device
        result = ((int (*)(void *, F3StateDescriptor *, void *))
            (*(void ***)g_dword_1E2EEF4)[86])(g_dword_1E2EEF4, g_stateDescTable, record);
    g_stateDescOwner = 0;
    g_stateDescCount = 0;
    return result;
}

// 36-byte frontend graphics state records (dword_212223C .. dword_212235C
// in IDA, nine 0x24-byte records): {state object, field cursor (+4),
// ...}.  Re-registered with the device by ReRegisterGraphicsState after a
// device create / reset.  In this build they remain zero (no state object
// is ever bound to them, so no flush path runs).
struct F3StateRecord
{
    void  *stateObj;   // +0x00: registered state object (nonzero -> flushed on re-registration)
    DWORD  data[8];    // +0x04: field slots (field N is written at data[N])
};
static F3StateRecord g_dword_212223C;
static F3StateRecord g_dword_2122260;
static F3StateRecord g_dword_2122284;
static F3StateRecord g_dword_21222A8;
static F3StateRecord g_dword_21222CC;
static F3StateRecord g_unk_21222F0;
static F3StateRecord g_unk_2122314;
static F3StateRecord g_dword_2122338;
static F3StateRecord g_dword_212235C;

// ============================================================
//  ReRegisterGraphicsState  (0x18CF6E0 - 0x18CF957)  -- re-register the
//  nine frontend graphics state records with the device after a device
//  create / reset.
//  [NOT byte-identical] placement-dependent (33 relative calls to
//  AddStateRegistration + 9 relative calls to SubmitStateRegistration)
//  and refs the nine high-VA state records (dword_212223C ..
//  dword_212235C) at their original addresses.
//
//  int ReRegisterGraphicsState(void)
//
//  For each of the nine records: the first AddStateRegistration call
//  (size class 2, field 0, value 0, format class 0, flags 0) flushes any
//  pending batch by committing the previously registered state object
//  (record+0), appends the record's header descriptor (writing the field
//  cursor record+4 = 8), the remaining calls append the record's field
//  descriptors (all at field 0, advancing the record+4 cursor to a4 +
//  the size-class delta), and the group ends with
//  SubmitStateRegistration(&record), which terminates the batch and
//  submits it to the device's state method (vtable slot 86).  Returns the
//  last submit's device-method result (ignored by the caller).
//
//  Sole caller: ResetGraphicsSurfaces (0x18E6D40), right after
//  ResetGraphicsEntities -- the "reset render state" step of the
//  frontend graphics-surface re-init.  Replaces the sub_18CF6E0 no-op
//  stub in ResetGraphicsSurfaces.
// ============================================================
static int ReRegisterGraphicsState(void)
{
    AddStateRegistration(2, (DWORD *)&g_dword_2122260, 0, 0,  0, 0);
    AddStateRegistration(4, (DWORD *)&g_dword_2122260, 0, 12, 9, 0);
    SubmitStateRegistration(&g_dword_2122260);

    AddStateRegistration(2, (DWORD *)&g_dword_212223C, 0, 0,  0, 0);
    AddStateRegistration(4, (DWORD *)&g_dword_212223C, 0, 12, 9, 0);
    AddStateRegistration(1, (DWORD *)&g_dword_212223C, 0, 16, 5, 0);
    SubmitStateRegistration(&g_dword_212223C);

    AddStateRegistration(2, (DWORD *)&g_dword_21222A8, 0, 0, 0, 0);
    SubmitStateRegistration(&g_dword_21222A8);

    AddStateRegistration(2, (DWORD *)&g_dword_2122284, 0, 0,  0, 0);
    AddStateRegistration(1, (DWORD *)&g_dword_2122284, 0, 12, 5, 0);
    SubmitStateRegistration(&g_dword_2122284);

    AddStateRegistration(2, (DWORD *)&g_dword_21222CC, 0, 0,  0, 0);
    AddStateRegistration(1, (DWORD *)&g_dword_21222CC, 0, 12, 5, 0);
    AddStateRegistration(1, (DWORD *)&g_dword_21222CC, 0, 20, 5, 1);
    SubmitStateRegistration(&g_dword_21222CC);

    AddStateRegistration(2, (DWORD *)&g_unk_21222F0, 0, 0,  0, 0);
    AddStateRegistration(2, (DWORD *)&g_unk_21222F0, 0, 12, 3, 0);
    AddStateRegistration(3, (DWORD *)&g_unk_21222F0, 0, 24, 5, 0);
    AddStateRegistration(1, (DWORD *)&g_unk_21222F0, 0, 40, 5, 1);
    AddStateRegistration(1, (DWORD *)&g_unk_21222F0, 0, 48, 5, 2);
    SubmitStateRegistration(&g_unk_21222F0);

    AddStateRegistration(2, (DWORD *)&g_unk_2122314, 0, 0,  0, 0);
    AddStateRegistration(2, (DWORD *)&g_unk_2122314, 0, 12, 3, 0);
    AddStateRegistration(3, (DWORD *)&g_unk_2122314, 0, 24, 5, 0);
    AddStateRegistration(1, (DWORD *)&g_unk_2122314, 0, 40, 5, 1);
    AddStateRegistration(1, (DWORD *)&g_unk_2122314, 0, 48, 5, 2);
    AddStateRegistration(1, (DWORD *)&g_unk_2122314, 0, 56, 5, 3);
    SubmitStateRegistration(&g_unk_2122314);

    AddStateRegistration(2, (DWORD *)&g_dword_2122338, 0, 0,  0, 0);
    AddStateRegistration(2, (DWORD *)&g_dword_2122338, 0, 12, 3, 0);
    AddStateRegistration(4, (DWORD *)&g_dword_2122338, 0, 24, 9, 0);
    SubmitStateRegistration(&g_dword_2122338);

    AddStateRegistration(2, (DWORD *)&g_dword_212235C, 0, 0,  0, 0);
    AddStateRegistration(2, (DWORD *)&g_dword_212235C, 0, 12, 3, 0);
    AddStateRegistration(2, (DWORD *)&g_dword_212235C, 0, 24, 6, 0);
    AddStateRegistration(2, (DWORD *)&g_dword_212235C, 0, 36, 7, 0);
    AddStateRegistration(1, (DWORD *)&g_dword_212235C, 0, 48, 5, 0);
    AddStateRegistration(1, (DWORD *)&g_dword_212235C, 0, 56, 5, 1);
    AddStateRegistration(0, (DWORD *)&g_dword_212235C, 0, 64, 5, 2);
    AddStateRegistration(4, (DWORD *)&g_dword_212235C, 0, 68, 9, 0);
    return SubmitStateRegistration(&g_dword_212235C);
}

// ============================================================
//  CreateTexture  (0x18D33B0 - 0x18D346A)  -- create (load) the
//  underlying texture of an NGraphics::CTexture record from the frontend
//  texture source-descriptor table.  [NOT byte-identical] refs the device
//  object (dword_1E2EEF4) and the texture source-descriptor table
//  (dword_1D9AF60, 0x70-byte rows) at their original high VAs, and
//  dispatches two vtable methods: the record's old-texture release (vt[2],
//  offset 8) and the device's create method (vt[23], offset 0x5C).
//
//  The record (obj, 32 bytes, NGraphics::CTexture / CTextureBase):
//    +0  vtable
//    +4  the created texture resource (the device writes here)
//    +8  prop (a6)
//    +12 texture-table row index
//    +16 width
//    +20 height
//    +24 (zeroed)
//
//  bool CreateTexture(void *obj, const unsigned int *dims, int mips,
//                     unsigned int *index, int flags, int prop)
//    1. Release the record's current texture (+4) through vt[2] and clear
//       it, if any.
//    2. If mips <= 0 and !(flags & 0x400), auto-compute the level: halve
//       {width, height} until either reaches <= 4; level = count + 1.
//    3. Device vtable[23] (offset 0x5C), passing the table row
//       &table[*index] and writing the resource into +4:
//         (device, width, height, level, flags, &table[*index], prop,
//          &obj->tex, 0).  A null-guard skips the dispatch pre-device (the
//       original reads the device vtable unconditionally).
//    4. Store prop (+8), *index (+12), width (+16), height (+20); zero +24.
//    5. Return device result >= 0.
// ============================================================

// 0x1D9AF60: texture source-descriptor table (0x70 bytes = 28 DWORDs per
// row, 110 rows ending at off_1D9DF80; the array is sized past that so any
// index the callers use stays in bounds).  Row layout (the fields the
// frontend surface-clear / create paths read):
//   +0x00 format type (the match key QueryCsurfaceFormat looks up)
//   +0x04 element divisor (always 1)
//   +0x08 bits per element (ClearCsurfacePacked: size / this / this)
//   +0x0C..+0x0F bits used per component c0..c3
//   +0x3C..+0x43 left shift per component c0..c3
//   +0x6C clear flags (bit 2 = element-conversion clear mode: 58 = 4x u16,
//         63 = 4x float, handled by ClearCsurface)
DWORD g_dword_1D9AF60[0x200 * 28] = {
   0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 0
   0x00000014, 0x00000001, 0x00000018, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 1
   0x00000015, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 2
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 3
   0x00000016, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 4
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 5
   0x00000017, 0x00000001, 0x00000010, 0x00000005, 0x00000006, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000B, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 6
   0x00000000, 0x00000001, 0x00000010, 0x00000005, 0x00000006, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000B, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 7
   0x00000018, 0x00000001, 0x00000010, 0x00000005, 0x00000005, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 8
   0x00000019, 0x00000001, 0x00000010, 0x00000005, 0x00000005, 0x00000005, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000005, 0x00000000, 0x0000000F, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 9
   0x0000001A, 0x00000001, 0x00000010, 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000004, 0x00000000, 0x0000000C, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 10
   0x0000001B, 0x00000001, 0x00000008, 0x00000003, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000005, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 11
   0x0000001C, 0x00000001, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 12
   0x0000001D, 0x00000001, 0x00000010, 0x00000003, 0x00000003, 0x00000002, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000005, 0x00000002, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 13
   0x0000001E, 0x00000001, 0x00000010, 0x00000004, 0x00000004, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 14
   0x00000023, 0x00000001, 0x00000020, 0x0000000A, 0x0000000A, 0x0000000A, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000014, 0x0000001E, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 15
   0x00000000, 0x00000001, 0x00000020, 0x0000000A, 0x0000000A, 0x0000000A, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000014, 0x0000001E, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 16
   0x00000020, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 17
   0x00000021, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 18
   0x00000022, 0x00000001, 0x00000020, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 19
   0x00000024, 0x00000001, 0x00000040, 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000020, 0x00000030, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 20
   0x00000028, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 21
   0x00000029, 0x00000001, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 22
   0x00000032, 0x00000001, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 23
   0x00000051, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 24
   0x00000033, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 25
   0x00000034, 0x00000001, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000000, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 26
   0x0000003C, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000002,  // row 27
   0x0000003F, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000010, 0x00000018, 0x00000000, 0x00000000, 0x00000002,  // row 28
   0x00000040, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000002,  // row 29
   0x0000006E, 0x00000001, 0x00000040, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000020, 0x00000030, 0x00000000, 0x00000000, 0x00000002,  // row 30
   0x00000075, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000002,  // row 31
   0x0000003D, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000006, 0x00000005, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000000, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000002,  // row 32
   0x0000003E, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000002,  // row 33
   0x00000043, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x0000000A, 0x0000000A, 0x0000000A, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000001E, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000014, 0x00000000, 0x00000000, 0x00000000, 0x00000002,  // row 34
   0x31545844, 0x00000004, 0x00000040, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 35
   0x32545844, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 36
   0x33545844, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 37
   0x34545844, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 38
   0x35545844, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 39
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 40
   0x00000000, 0x00000004, 0x00000040, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 41
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 42
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 43
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 44
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 45
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 46
   0x00000046, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 47
   0x00000047, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 48
   0x00000049, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000F, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000,  // row 49
   0x0000004B, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000018, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000,  // row 50
   0x0000004D, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000,  // row 51
   0x0000004F, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000018, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000,  // row 52
   0x00000052, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004,  // row 53
   0x00000053, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000018, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004,  // row 54
   0x00000050, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 55
   0x0000006F, 0x00000001, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004,  // row 56
   0x00000070, 0x00000001, 0x00000020, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004,  // row 57
   0x00000071, 0x00000001, 0x00000040, 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000020, 0x00000030, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004,  // row 58
   0x00000000, 0x00000001, 0x00000040, 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000020, 0x00000030, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000C,  // row 59
   0x00000072, 0x00000001, 0x00000020, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004,  // row 60
   0x00000000, 0x00000001, 0x00000020, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000C,  // row 61
   0x00000073, 0x00000001, 0x00000040, 0x00000020, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004,  // row 62
   0x00000074, 0x00000001, 0x00000080, 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000020, 0x00000040, 0x00000060, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000004,  // row 63
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010,  // row 64
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010,  // row 65
   0x00000000, 0x00000001, 0x00000020, 0x0000000A, 0x0000000A, 0x0000000A, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000014, 0x0000000A, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010,  // row 66
   0x00000000, 0x00000001, 0x00000020, 0x0000000A, 0x0000000A, 0x0000000A, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000014, 0x0000000A, 0x00000000, 0x0000001E, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010,  // row 67
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 68
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 69
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 70
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 71
   0x00000000, 0x00000001, 0x00000010, 0x00000005, 0x00000006, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000B, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 72
   0x00000000, 0x00000001, 0x00000010, 0x00000005, 0x00000006, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000B, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 73
   0x00000000, 0x00000001, 0x00000010, 0x00000005, 0x00000005, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 74
   0x00000000, 0x00000001, 0x00000010, 0x00000005, 0x00000005, 0x00000005, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000005, 0x00000000, 0x0000000F, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 75
   0x00000000, 0x00000001, 0x00000010, 0x00000004, 0x00000004, 0x00000004, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000004, 0x00000000, 0x0000000C, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 76
   0x00000000, 0x00000001, 0x00000010, 0x00000004, 0x00000004, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 77
   0x00000000, 0x00000001, 0x00000020, 0x0000000A, 0x0000000A, 0x0000000A, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000014, 0x0000001E, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 78
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 79
   0x00000000, 0x00000001, 0x00000020, 0x00000008, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 80
   0x00000000, 0x00000001, 0x00000020, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 81
   0x00000000, 0x00000001, 0x00000040, 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000020, 0x00000030, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 82
   0x00000000, 0x00000004, 0x00000040, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 83
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 84
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 85
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 86
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 87
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 88
   0x00000000, 0x00000004, 0x00000040, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 89
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 90
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 91
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 92
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 93
   0x00000000, 0x00000004, 0x00000080, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000009,  // row 94
   0x00000000, 0x00000001, 0x00000020, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 95
   0x00000000, 0x00000001, 0x00000040, 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000020, 0x00000030, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 96
   0x00000000, 0x00000001, 0x00000040, 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000020, 0x00000030, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 97
   0x00000000, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 98
   0x00000000, 0x00000004, 0x00000040, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 99
   0x00000000, 0x00000004, 0x00000040, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001,  // row 100
   0x00000000, 0x00000001, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 101
   0x00000000, 0x00000001, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 102
   0x00000000, 0x00000001, 0x00000010, 0x00000008, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008,  // row 103
   0x00000000, 0x00000001, 0x00000040, 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000020, 0x00000030, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000C,  // row 104
   0x00000000, 0x00000001, 0x00000020, 0x0000000A, 0x0000000A, 0x0000000A, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000A, 0x00000014, 0x0000001E, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 105
   0x5A544E49, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000018, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000,  // row 106
   0x4C4C554E, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000008, 0x00000000, 0x00000000,  // row 107
   0x36314644, 0x00000001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 108
   0x34324644, 0x00000001, 0x00000020, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,  // row 109
};

// The device's texture-create method (device vtable slot 23, offset 0x5C).
typedef int (__stdcall *F3DeviceCreateTexture)(
    void *device, unsigned int width, unsigned int height, int level,
    int flags, void *srcDesc, int prop, void **outTex, int extra);

static bool CreateTexture(void *objPtr, const unsigned int *dims, int mips,
                          unsigned int *index, int flags, int prop)
{
    DWORD *obj = (DWORD *)objPtr;

    // Release any existing texture resource (vt[2] / offset 8).
    void *old = (void *)obj[1];   // [this+4]
    if (old != 0)
    {
        void **vt = *(void ***)old;
        ((void (*)(void *))vt[2])(old);
        obj[1] = 0;
    }

    // Auto-compute the level from the dimensions when requested.
    int level = mips;
    if (mips <= 0 && (flags & 0x400) == 0)
    {
        unsigned int w = dims[0];
        unsigned int h = dims[1];
        int i = 0;
        while (w > 4)
        {
            if (h <= 4)
                break;
            w >>= 1;
            h >>= 1;
            ++i;
        }
        level = i + 1;
    }

    // Create the texture on the device from the table row; the device
    // writes the created resource into [this+4].
    int result = 0;
    if (g_dword_1E2EEF4 != 0)   // null-guard; the original would fault pre-device
    {
        DWORD *src = &g_dword_1D9AF60[(size_t)(*index) * 28];
        F3DeviceCreateTexture create =
            (F3DeviceCreateTexture)((*(void ***)g_dword_1E2EEF4)[23]);
        result = create(g_dword_1E2EEF4, dims[0], dims[1], level, flags,
                        src, prop, (void **)(obj + 1), 0);
    }

    obj[2] = prop;       // [this+8]
    obj[3] = *index;     // [this+12]
    obj[4] = dims[0];    // [this+16] width
    obj[5] = dims[1];    // [this+20] height
    obj[6] = 0;          // [this+24]
    return result >= 0;
}
// ============================================================
//  RefreshCsurfaceSlot  (0x18D36C0 - 0x18D374C)  -- wrap a source
//  record's texture surface into a 32-byte NGraphics::CSurface wrapper.
//  [NOT byte-identical] placement-dependent (the relative call to
//  QueryCsurfaceFormat below and the __security_check_cookie IAT slot) and
//  refs the high-VA CSurface vtable (??_7CSurface@NGraphics@@6B@,
//  0x01BE384C) at its original address.
//
//  The original is a register/stack hybrid (the wrapper record in EDI, the
//  source-record pointer and the slot index on the stack, retn 8, the record
//  returned in EAX); this build calls it as a plain C function (the record
//  first).
//
//  F3CsurfaceSlot *RefreshCsurfaceSlot(F3CsurfaceSlot *obj, const void *src, int slot)
//
//  1. Pull the surface from the source record's resource (src+4) through its
//     vtable slot 18 (offset 0x48): surface = res->vt[18](res, slot, &surface)
//     (the surface is returned in the out-parameter).  A null-guard skips the
//     dispatch on an uncreated resource, as pre-device in this build.
//  2. Zero the 32-byte wrapper record and set its vtable to
//     NGraphics::CSurface::`vftable'.
//  3. (The original's "release the previous surface" block reads obj->surface,
//     which step 2 just zeroed, so it is dead here.)
//  4. Store the surface (+4) and the wrapper type (3, +8), then refresh the
//     5-word format block (+12 .. +28) from the surface (QueryCsurfaceFormat).
//  5. Return the wrapper record.
//
//  ~45 call sites across the graphics / frontend code (ResetGraphicsSurfaces
//  0x18E6D40, the CTexture ctors sub_18CC4C0 / sub_18CCD40, sub_192C040, ...);
//  its near-identical sibling sub_1A046D0 wraps a texture slot by index with
//  one extra argument to the surface-get.  Replaces the sub_18D36C0 no-op
//  stub in ResetGraphicsSurfaces.
// ============================================================

// NGraphics::CSurface::`vftable' (0x1BE384C in the original) -- a 13-slot
// vtable (slot 0 the release, RTTI at [1]/[7]).  Never dispatched in this
// build, so slot 0 is a no-op and the rest stay null.
static void  F3CsurfaceStubRelease(void *obj) { (void)obj; }
static void *g_CSurfaceVtable[13] = {
    (void *)F3CsurfaceStubRelease, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// ============================================================
//  QueryCsurfaceFormat  (0x5A2EF0 - 0x5A2F60)  -- refresh a CSurface wrapper's
//  5-word format block from a surface's description.
//  [NOT byte-identical] placement-dependent (the __security_check_cookie IAT
//  slot) and refs the high-VA texture source-descriptor table
//  dword_1D9AF60 (0x01D9AF60, 0x70-byte rows) and its end (off_1D9DF80) at
//  their original addresses.
//
//  The original is a register/stack hybrid (the format block in ESI, the
//  surface on the stack, retn 4); this build calls it as a plain C function
//  (the format block first).
//
//  DWORD QueryCsurfaceFormat(int *fmt, void *surface)
//
//  1. Query the surface's description through its vtable slot 12 (offset
//     0x30): desc[8] = surface->vt[12](surface, desc).  Null-guarded
//     pre-device (desc stays zero).
//  2. Find the texture source-descriptor row whose first word matches desc[0]
//     (dword_1D9AF60, 0x70-byte rows); the row index is fmt[0], or 0 when no
//     row matches.
//  3. Write the format block: fmt[1]=desc[5], fmt[2]=desc[6], fmt[3]=desc[3],
//     fmt[4]=desc[1]; return desc[5].
// ============================================================
DWORD QueryCsurfaceFormat(int *fmt, void *surface)
{
    int desc[8] = { 0 };
    if (surface != 0)   // null-guard; the original would fault pre-device
        ((void (*)(void *, int *))(*(void ***)surface)[12])(surface, desc);

    int index = 0;
    for (DWORD *row = g_dword_1D9AF60; row < &g_dword_1D9AF60[110 * 28]; row += 28)
    {
        if (row[0] == (DWORD)desc[0])
            break;
        ++index;
    }
    if (index >= 110)   // not found -> the original resets to row 0
        index = 0;

    fmt[0] = index;
    fmt[1] = desc[5];
    fmt[2] = desc[6];
    fmt[3] = desc[3];
    fmt[4] = desc[1];
    return (DWORD)desc[5];
}

static F3CsurfaceSlot *RefreshCsurfaceSlot(F3CsurfaceSlot *obj, const void *src, int slot)
{
    if (obj == 0)
        return 0;

    // The source record's resource is at +4; pull the surface through its
    // vtable slot 18 (offset 0x48), returned in the out-parameter.
    const DWORD *srcD = (const DWORD *)src;
    void *res = (void *)srcD[1];
    DWORD surface = 0;
    if (res != 0)   // null-guard; the original would fault on an uncreated resource
        ((DWORD (*)(void *, int, DWORD *))(*(void ***)res)[18])(res, slot, &surface);

    // Zero the 32-byte wrapper record and set its vtable.
    for (int i = 0; i < 8; ++i)
        ((DWORD *)obj)[i] = 0;
    obj->vt = (void *)g_CSurfaceVtable;      // NGraphics::CSurface::`vftable'

    // (The original's "release the previous surface" block reads obj->surface,
    // which the zeroing above just cleared, so it never runs.)
    obj->surface = (void *)surface;
    obj->type    = 3;
    QueryCsurfaceFormat(obj->fmtInfo, (void *)surface);
    return obj;
}
// ============================================================
//  CsurfaceGetReadData  (0x18D1FA0 - 0x18D202E)  -- fetch the readable
//  row data of a CSurface wrapper record for the surface-clear paths.
//  [NOT byte-identical] placement-dependent (the original is a stdcall /
//  retn 8 helper with the __security_check_cookie IAT slot).
//
//  DWORD *CsurfaceGetReadData(const F3CsurfaceSlot *rec, DWORD out[4])
//    out[0] = rec->fmtInfo[1]  (+0x10, elements per row)
//    out[1] = rec->fmtInfo[2]  (+0x14, row count)
//    out[2] = row pitch (bytes), out[3] = row buffer.
//
//    The wrapped surface (+4) is locked through its vtable slot 13 (offset
//    0x34): (surface, &pitchBuf[2] (zeroed), &(elements/rows), 0) >= 0;
//    the two out words become out[2] / out[3] on success.  A null-guard
//    leaves out[2] = out[3] = 0 pre-device (the original would fault);
//    the same holds when the lock dispatch fails (the original leaves its
//    caller's out words untouched).
// ============================================================
static DWORD *CsurfaceGetReadData(const F3CsurfaceSlot *rec, DWORD out[4])
{
    DWORD width  = (DWORD)rec->fmtInfo[1];   // +0x10: elements per row
    DWORD height = (DWORD)rec->fmtInfo[2];   // +0x14: row count

    out[0] = width;
    out[1] = height;
    out[2] = 0;
    out[3] = 0;

    void *surface = rec->surface;            // +0x04: the wrapped surface
    if (surface != 0)   // null-guard; the original would fault pre-device
    {
        void **vt  = *(void ***)surface;
        DWORD  buf[2] = { 0, 0 };
        DWORD  dims[2] = { width, height };
        if (((int (*)(void *, DWORD *, DWORD *, int))vt[13])(surface, buf, dims, 0) >= 0)
        {
            out[2] = buf[0];
            out[3] = buf[1];
        }
    }
    return out;
}

// One color component of the packed clear: (c >> (8 - bits)) << shift, with
// the subtraction wrapping through a byte (bits > 8 -> 0) and the left
// shift masked like the x86 `shl` (mod 32).
static DWORD F3PackColorComponent(unsigned char c, DWORD bits, DWORD shift)
{
    unsigned v = ((bits & 0xFF) <= 8) ? (unsigned)(c >> (8 - (int)(bits & 0xFF))) : 0u;
    return (v << (shift & 0x1F));
}

// ============================================================
//  ClearCsurfacePacked  (0x18D2180 - 0x18D22E6)  -- clear a CSurface
//  wrapper record to a color by repacking the four color bytes into the
//  format's native packed value and filling the rows with it.
//  [NOT byte-identical] placement-dependent (relative calls to
//  CsurfaceGetReadData / the surface commit dispatch and the memset IAT
//  slot) and refs the texture source-descriptor table g_dword_1D9AF60 and
//  the high-VA globals at their original addresses.
//
//  The original is __fastcall (a1 = unused, a2 = the record in EDX,
//  a3 = the color bytes on the stack); this build calls it as a plain C
//  function with the unused fastcall word first.
//
//  int ClearCsurfacePacked(DWORD fmtFlags, const F3CsurfaceSlot *rec,
//                          const unsigned char *color)
//    1. row = &g_dword_1D9AF60[28 * rec->fmtInfo[0]];  pack the color:
//       packed = (color[3] >> (8-n3) << s3) | (color[1] >> (8-n1) << s1)
//                | (color[2] >> (8-n2) << s2) | (color[0] >> (8-n0) << s0),
//       n0..n3 = the row's bits-per-component (+0x0C..+0x0F), s0..s3 = the
//       row's left shifts (+0x3C..+0x43) (c0 = the record's R, ...).
//    2. out = CsurfaceGetReadData(rec)  (elements/row, rows, pitch, buffer);
//       bits-per-element = row[+8] / row[+4] / row[+4] (the original
//       divides straight through row 0's zeros; guarded here).
//    3. Per row (advancing the buffer by the pitch): 8-bit -> memset the
//       row with the packed byte; 16-bit -> fill (elements/2) dwords with
//       the 16-bit value repeated, plus one trailing WORD for the odd
//       element; 32-bit -> fill the row's dwords with the packed value.
//    4. return surface->vt[14](surface)  (the commit / unlock dispatch).
//       Null-guarded pre-device (the original would fault).
// ============================================================
static int ClearCsurfacePacked(DWORD fmtFlags, const F3CsurfaceSlot *rec,
                               const unsigned char *color)
{
    (void)fmtFlags;   // the original's unused fastcall word

    const DWORD *row = &g_dword_1D9AF60[(size_t)rec->fmtInfo[0] * 28];
    DWORD packed =
        F3PackColorComponent((unsigned char)color[3], row[6], row[18]) |
        F3PackColorComponent((unsigned char)color[1], row[4], row[16]) |
        F3PackColorComponent((unsigned char)color[2], row[3], row[17]) |
        F3PackColorComponent((unsigned char)color[0], row[5], row[15]);

    DWORD out[4];
    CsurfaceGetReadData(rec, out);

    int bits = 0;   // bits per element (row+8 / row+4 / row+4)
    if (row[1] != 0)   // guard the zero-size row 0; the original divides by zero
        bits = (int)(row[2] / row[1] / row[1]);

    char *buf = (char *)out[3];
    if (bits == 8)
    {
        for (DWORD r = out[1]; r != 0; --r)
        {
            if (out[0] > 0)
                memset(buf, (int)(packed & 0xFF), out[0]);
            buf += out[2];
        }
    }
    else if (bits == 16)
    {
        for (DWORD r = out[1]; r != 0; --r)
        {
            if (out[0] > 0)
            {
                WORD  w  = (WORD)packed;
                DWORD pat = ((DWORD)w << 16) | (DWORD)w;
                DWORD *p32 = (DWORD *)buf;
                for (DWORD i = out[0] >> 1; i != 0; --i)
                    p32[i - 1] = pat;
                WORD *p16 = (WORD *)&p32[out[0] >> 1];
                for (DWORD i = out[0] & 1; i != 0; --i)
                    *p16++ = w;
            }
            buf += out[2];
        }
    }
    else if (bits == 32)
    {
        for (DWORD r = out[1]; r != 0; --r)
        {
            if (out[0] > 0)
            {
                DWORD *p32 = (DWORD *)buf;
                for (DWORD i = out[0]; i != 0; --i)
                    p32[i - 1] = packed;
            }
            buf += out[2];
        }
    }

    void *surface = rec->surface;            // +0x04: the wrapped surface
    if (surface != 0)   // null-guard; the original would fault pre-device
        return ((int (*)(void *))(*(void ***)surface)[14])(surface);
    return 0;
}

// The d3dx9_42.dll import used by the element-conversion clear below
// (bound through the d3dx9_42 import lib, like the other game imports).
extern "C" void D3DXFloat32To16Array(WORD *pOutOut, const float *pInput,
                                     DWORD Count);

// ============================================================
//  ClearCsurface  (0x18D2940 - 0x18D2BF1)  -- clear a CSurface wrapper
//  record to a color and commit the write through the surface's vtable.
//  [NOT byte-identical] placement-dependent (relative calls to
//  CsurfaceGetReadData / ClearCsurfacePacked / the SSE vec setter / the
//  D3DXFloat32To16Array IAT slot / the surface commit dispatch and the
//  __security_check_cookie IAT slot) and refs the high-VA globals the
//  texture source-descriptor table g_dword_1D9AF60 (the row+0x6C clear
//  flags) and the 1/255 float dword_1BF93BC at their original addresses.
//
//  The original is __fastcall (a1 = the color bytes in ECX, a2 = the record
//  in EDX); this build calls it as a plain C function.  The caller passes
//  a 4-word record; only the low byte of each word is read.
//
//  int ClearCsurface(const int *color, const F3CsurfaceSlot *rec)
//    1. row = &g_dword_1D9AF60[28 * rec->fmtInfo[0]];  when the row's
//       clear-flag (row+0x6C) bit 2 is clear, delegate to ClearCsurfacePacked
//       (the packed memset clear) and return its result.
//    2. Build the float color vec = {color[2], color[1], color[0],
//       color[3]} * (1/255)  (the SSE setter sub_41A200).
//    3. out = CsurfaceGetReadData(rec).  Per row (advancing the buffer by
//       the pitch): format index 58 (4x u16 element, e.g. A16B16G16R16)
//       converts the four vec components once with
//       D3DXFloat32To16Array and writes {x, y, z, w} to every element;
//       format index 63 (4x float element, e.g. A32B32G32R32) writes the
//       four floats to every element.
//    4. return surface->vt[14](surface)  (the commit / unlock dispatch).
//       Null-guarded pre-device (the original would fault).
//
//  Callers: ResetGraphicsSurfaces (0x18E6D40, the two near-black clears),
//  sub_19A2D20 (the shadow-map surface init, 21 wrapped surfaces),
//  sub_19B0410 / sub_19B05D0 (the per-frame surface clears with a
//  caller-supplied color).
// ============================================================
static int ClearCsurface(const int *color, const F3CsurfaceSlot *rec)
{
    const DWORD *row = &g_dword_1D9AF60[(size_t)rec->fmtInfo[0] * 28];

    if (((row[27] >> 2) & 1) == 0)   // clear-flag bit 2 not set
        return ClearCsurfacePacked(0, rec, (const unsigned char *)color);

    // 0x1BF93BC: the 1/255 float (0.0039215689f) the color bytes scale by.
    const float S = 0.0039215689f;
    float vec[4] = {
        (float)(color[2] & 0xFF) * S,
        (float)(color[1] & 0xFF) * S,
        (float)(color[0] & 0xFF) * S,
        (float)(color[3] & 0xFF) * S
    };

    DWORD out[4];
    CsurfaceGetReadData(rec, out);

    if (rec->fmtInfo[0] == 58)   // 4x u16 element format
    {
        WORD e[4];
        D3DXFloat32To16Array(&e[0], &vec[0], 1);
        D3DXFloat32To16Array(&e[1], &vec[1], 1);
        D3DXFloat32To16Array(&e[2], &vec[2], 1);
        D3DXFloat32To16Array(&e[3], &vec[3], 1);
        for (DWORD r = out[1]; r != 0; --r)
        {
            if (out[0] > 0)
            {
                WORD *dst = (WORD *)out[3];
                for (DWORD el = out[0]; el != 0; --el)
                {
                    dst[0] = e[0]; dst[1] = e[1];
                    dst[2] = e[2]; dst[3] = e[3];
                    dst += 4;
                }
            }
            out[3] += out[2];
        }
    }
    else if (rec->fmtInfo[0] == 63)   // 4x float element format
    {
        for (DWORD r = out[1]; r != 0; --r)
        {
            if (out[0] > 0)
            {
                float *dst = (float *)out[3];
                for (DWORD el = out[0]; el != 0; --el)
                {
                    dst[0] = vec[0]; dst[1] = vec[1];
                    dst[2] = vec[2]; dst[3] = vec[3];
                    dst += 4;
                }
            }
            out[3] += out[2];
        }
    }

    void *surface = rec->surface;            // +0x04: the wrapped surface
    if (surface != 0)   // null-guard; the original would fault pre-device
        return ((int (*)(void *))(*(void ***)surface)[14])(surface);
    return 0;
}
// ============================================================
//  CreateCsurface  (0x1A042B0 - 0x1A0430A)  --  create the frontend CSurface
//  from a texture source-descriptor table row.
//  [NOT byte-identical] the original is a register/stack hybrid (the 3-word
//  dims record in ESI, the format flag in EDI, the index pointer + create
//  flags on the stack, retn 8); this build calls it as a plain C function.
//
//  BOOL CreateCsurface(int flags, int fmt, int *dims, int index)
//
//  1. Release the existing cached surface (dword_1DB1334) through its
//     vtable slot 2 and clear the global.
//  2. Look up row `index` of the texture source-descriptor table
//     (dword_1D9AF60, 0x70-byte rows) and create the surface through device
//     vtable[3] (offset 0x60): (device, dims[0], dims[1], 1, dims[2], flags,
//     &row, fmt, &dword_1DB1334, 0) -- the surface is returned in the out
//     parameter; null-guarded pre-device (the original would fault).
//  3. Return success (the device result >= 0).
//
//  Sole caller: ResetGraphicsSurfaces (0x18E6D40), with dims {1,1,1}, row 2,
//  the 0x200 create flag and the byte_2075F05 format flag (return discarded).
//  The cached surface is released by the frontend teardowns (sub_18E6870,
//  sub_1AC1090) and zeroed by the frontend reset (sub_5857D0).
// ============================================================
static BOOL CreateCsurface(int flags, int fmt, int *dims, int index)
{
    if (g_dword_1DB1334 != 0)
    {
        ((void (*)(void *))((void **)(*(void **)g_dword_1DB1334))[2])(g_dword_1DB1334);
        g_dword_1DB1334 = 0;
    }

    const DWORD *row = &g_dword_1D9AF60[(size_t)index * 28];
    int r = 0;
    if (g_dword_1E2EEF4 != 0)   // null-guard; the original would fault pre-device
        r = ((int (*)(void *, int, int, int, int, int, const void *, int, void **, int))
                (*(void ***)g_dword_1E2EEF4)[3])(
                g_dword_1E2EEF4, dims[0], dims[1], 1, dims[2], flags, row, fmt,
                &g_dword_1DB1334, 0);
    return r >= 0;
}
static F3CsurfaceSlot *sub_1A046D0(F3CsurfaceSlot *slot, const F3CsurfaceSlot *src, int idx)
    { (void)src; (void)idx; if (slot != 0) slot->surface = 0; return slot; } // wrap texture slot idx

// BuildGraphicsEntityPool  (0x18CFA70 - 0x18D02D5)  --  Rebuild the contents
// of the frontend graphics-entity pool and the entity index list.
// [NOT byte-identical] 2149-byte straight-line constant initializer: it
// writes the high-VA globals (the 0x2120420 pool, the 0x2073134 index list
// and the 0x2063134 counter) through absolute memory operands
// (mov [ecx*4+0x2073138h], ...), which MSVC inline asm cannot emit.  The
// write set (739 non-zero pool writes + 265 index entries) was extracted by
// instruction-level simulation of the original disassembly.
//
// Original body (no calls; two fixed-stride loops):
//   1. 29 pre-loop pool records (24 bytes, 0x18 stride): {+0x00=0, +0x04=0,
//      +0x0E=1, +0x14=id, +0x18=0}, one index entry each.
//   2. Loop 1: four 0x70-byte records (0x70 stride, base pool+0x3B8) with
//      flag bytes +0x0E/+0x2A/+0x46 = 1 and the t0/t1/t2 dwords +0x14/+0x30/
//      +0x4C from the rdata table at 0x1D9DF98 (0x80..0x87, 0xC6..0xCD -
//      one triple per record), four index entries each.
//   3. Loop 2: twenty 0x1C-byte records (0x1C stride, base pool+0xCD8),
//      each associated with eleven 0x230-byte sub-structures (code at
//      +0x0C, k+2 at +0x0E, a value at +0x14), eleven index entries each.
//   4. Index slots 1..265 end up filled and the counter := 265.
//
// The sole caller, ResetGraphicsEntities (0x18D1960), memsets the pool to
// zero first, so only the non-zero field writes are reproduced here
// (memset + non-zero writes == the original's full constant write set).
// The original's return value (a leftover loop-boundary EAX) is discarded
// by the caller.
//
// The remaining graphics-entity callees: RegisterGraphicsEntities
// (0x18D04D0, the dirty registration) and SyncGraphicsEntities
// (sub_18D02E0, the tail-called sync step that commits the descriptor values
// to the device tables, calls SetGraphicsProperty (sub_5A3080) for type-2
// entities and clears the dirty-registration counter) are implemented below.
// Non-zero pool field writes (offset from pool base, size, value).
// The pool is zeroed by ResetGraphicsEntities before the builder runs,
// so only the non-zero writes are listed.  Sizes: 4 = dword, 2 = word,
// 1 = byte (some dword writes are unaligned, hence byte-level application).
static const struct { int off; int sz; unsigned val; } sPoolFieldWrites[] =
{
    {0x000E, 4, 0x00000001},{0x0014, 4, 0x00000008},{0x002A, 4, 0x00000001},{0x0030, 4, 0x000000A8},{0x0046, 4, 0x00000001},
    {0x004C, 4, 0x0000000E},{0x0062, 4, 0x00000001},{0x0068, 4, 0x00000007},{0x007E, 4, 0x00000001},{0x0084, 4, 0x00000017},
    {0x009A, 4, 0x00000001},{0x00A0, 4, 0x00000016},{0x00B6, 4, 0x00000001},{0x00BC, 4, 0x00000019},{0x00D2, 4, 0x00000001},
    {0x00D8, 4, 0x0000001B},{0x00EE, 4, 0x00000001},{0x00F4, 4, 0x0000000F},{0x010A, 4, 0x00000001},{0x0110, 4, 0x00000018},
    {0x0126, 4, 0x00000001},{0x012C, 4, 0x00000013},{0x0142, 4, 0x00000001},{0x0148, 4, 0x00000014},{0x0196, 4, 0x00000001},
    {0x019C, 4, 0x0000009C},{0x01B2, 4, 0x00000001},{0x01B8, 4, 0x0000009A},{0x01CE, 4, 0x00000001},{0x01D4, 4, 0x0000009B},
    {0x01EA, 4, 0x00000001},{0x01F0, 4, 0x000000A6},{0x0222, 4, 0x00000001},{0x0228, 4, 0x000000C3},{0x023E, 4, 0x00000001},
    {0x0244, 4, 0x000000AF},{0x025A, 4, 0x00000001},{0x0260, 4, 0x00000034},{0x0276, 4, 0x00000001},{0x027C, 4, 0x00000035},
    {0x0292, 4, 0x00000001},{0x0298, 4, 0x00000036},{0x02AE, 4, 0x00000001},{0x02B4, 4, 0x00000037},{0x02CA, 4, 0x00000001},
    {0x02D0, 4, 0x00000038},{0x02E6, 4, 0x00000001},{0x02EC, 4, 0x00000039},{0x0302, 4, 0x00000001},{0x0308, 4, 0x0000003A},
    {0x031E, 4, 0x00000001},{0x0324, 4, 0x0000003B},{0x0356, 4, 0x00000001},{0x035C, 4, 0x000000AB},{0x038E, 4, 0x00000001},
    {0x0394, 4, 0x000000C1},{0x03AA, 4, 0x00000001},{0x03B0, 4, 0x000000C2},{0x03C6, 1, 0x00000001},{0x03CC, 4, 0x00000080},
    {0x03E2, 1, 0x00000001},{0x03E8, 4, 0x00000081},{0x03FE, 1, 0x00000001},{0x0404, 4, 0x00000082},{0x041A, 1, 0x00000001},
    {0x0420, 4, 0x00000083},{0x0436, 1, 0x00000001},{0x043C, 4, 0x00000084},{0x0452, 1, 0x00000001},{0x0458, 4, 0x00000085},
    {0x046E, 1, 0x00000001},{0x0474, 4, 0x00000086},{0x048A, 1, 0x00000001},{0x0490, 4, 0x00000087},{0x04A6, 1, 0x00000001},
    {0x04AC, 4, 0x000000C6},{0x04C2, 1, 0x00000001},{0x04C8, 4, 0x000000C7},{0x04DE, 1, 0x00000001},{0x04E4, 4, 0x000000C8},
    {0x04FA, 1, 0x00000001},{0x0500, 4, 0x000000C9},{0x0516, 1, 0x00000001},{0x051C, 4, 0x000000CA},{0x0532, 1, 0x00000001},
    {0x0538, 4, 0x000000CB},{0x054E, 1, 0x00000001},{0x0554, 4, 0x000000CC},{0x056A, 1, 0x00000001},{0x0570, 4, 0x000000CD},
    {0x05F6, 4, 0x00000002},{0x05FC, 2, 0x0000000A},{0x0610, 4, 0x00000001},{0x0612, 4, 0x00000002},{0x0618, 2, 0x0000000A},
    {0x062C, 4, 0x00000002},{0x062E, 4, 0x00000002},{0x0634, 2, 0x0000000A},{0x0648, 4, 0x00000003},{0x064A, 4, 0x00000002},
    {0x0650, 2, 0x0000000A},{0x0664, 4, 0x00000004},{0x0666, 4, 0x00000002},{0x066C, 2, 0x0000000A},{0x0680, 4, 0x00000005},
    {0x0682, 4, 0x00000002},{0x0688, 2, 0x0000000A},{0x069C, 4, 0x00000006},{0x069E, 4, 0x00000002},{0x06A4, 2, 0x0000000A},
    {0x06B8, 4, 0x00000007},{0x06BA, 4, 0x00000002},{0x06C0, 2, 0x0000000A},{0x06D4, 4, 0x00000008},{0x06D6, 4, 0x00000002},
    {0x06DC, 2, 0x0000000A},{0x06F0, 4, 0x00000009},{0x06F2, 4, 0x00000002},{0x06F8, 2, 0x0000000A},{0x070C, 4, 0x0000000A},
    {0x070E, 4, 0x00000002},{0x0714, 2, 0x0000000A},{0x0728, 4, 0x0000000B},{0x072A, 4, 0x00000002},{0x0730, 2, 0x0000000A},
    {0x0744, 4, 0x0000000C},{0x0746, 4, 0x00000002},{0x074C, 2, 0x0000000A},{0x0760, 4, 0x0000000D},{0x0762, 4, 0x00000002},
    {0x0768, 2, 0x0000000A},{0x077C, 4, 0x0000000E},{0x077E, 4, 0x00000002},{0x0784, 2, 0x0000000A},{0x0798, 4, 0x0000000F},
    {0x079A, 4, 0x00000002},{0x07A0, 2, 0x0000000A},{0x07B4, 4, 0x00000010},{0x07B6, 4, 0x00000002},{0x07BC, 2, 0x0000000A},
    {0x07D0, 4, 0x00000011},{0x07D2, 4, 0x00000002},{0x07D8, 2, 0x0000000A},{0x07EC, 4, 0x00000012},{0x07EE, 4, 0x00000002},
    {0x07F4, 2, 0x0000000A},{0x0808, 4, 0x00000013},{0x080A, 4, 0x00000002},{0x0810, 2, 0x0000000A},{0x0826, 4, 0x00000002},
    {0x082C, 2, 0x00000008},{0x0840, 4, 0x00000001},{0x0842, 4, 0x00000002},{0x0848, 2, 0x00000008},{0x085C, 4, 0x00000002},
    {0x085E, 4, 0x00000002},{0x0864, 2, 0x00000008},{0x0878, 4, 0x00000003},{0x087A, 4, 0x00000002},{0x0880, 2, 0x00000008},
    {0x0894, 4, 0x00000004},{0x0896, 4, 0x00000002},{0x089C, 2, 0x00000008},{0x08B0, 4, 0x00000005},{0x08B2, 4, 0x00000002},
    {0x08B8, 2, 0x00000008},{0x08CC, 4, 0x00000006},{0x08CE, 4, 0x00000002},{0x08D4, 2, 0x00000008},{0x08E8, 4, 0x00000007},
    {0x08EA, 4, 0x00000002},{0x08F0, 2, 0x00000008},{0x0904, 4, 0x00000008},{0x0906, 4, 0x00000002},{0x090C, 2, 0x00000008},
    {0x0920, 4, 0x00000009},{0x0922, 4, 0x00000002},{0x0928, 2, 0x00000008},{0x093C, 4, 0x0000000A},{0x093E, 4, 0x00000002},
    {0x0944, 2, 0x00000008},{0x0958, 4, 0x0000000B},{0x095A, 4, 0x00000002},{0x0960, 2, 0x00000008},{0x0974, 4, 0x0000000C},
    {0x0976, 4, 0x00000002},{0x097C, 2, 0x00000008},{0x0990, 4, 0x0000000D},{0x0992, 4, 0x00000002},{0x0998, 2, 0x00000008},
    {0x09AC, 4, 0x0000000E},{0x09AE, 4, 0x00000002},{0x09B4, 2, 0x00000008},{0x09C8, 4, 0x0000000F},{0x09CA, 4, 0x00000002},
    {0x09D0, 2, 0x00000008},{0x09E4, 4, 0x00000010},{0x09E6, 4, 0x00000002},{0x09EC, 2, 0x00000008},{0x0A00, 4, 0x00000011},
    {0x0A02, 4, 0x00000002},{0x0A08, 2, 0x00000008},{0x0A1C, 4, 0x00000012},{0x0A1E, 4, 0x00000002},{0x0A24, 2, 0x00000008},
    {0x0A38, 4, 0x00000013},{0x0A3A, 4, 0x00000002},{0x0A40, 2, 0x00000008},{0x0A56, 4, 0x00000002},{0x0A5C, 2, 0x00000009},
    {0x0A70, 4, 0x00000001},{0x0A72, 4, 0x00000002},{0x0A78, 2, 0x00000009},{0x0A8C, 4, 0x00000002},{0x0A8E, 4, 0x00000002},
    {0x0A94, 2, 0x00000009},{0x0AA8, 4, 0x00000003},{0x0AAA, 4, 0x00000002},{0x0AB0, 2, 0x00000009},{0x0AC4, 4, 0x00000004},
    {0x0AC6, 4, 0x00000002},{0x0ACC, 2, 0x00000009},{0x0AE0, 4, 0x00000005},{0x0AE2, 4, 0x00000002},{0x0AE8, 2, 0x00000009},
    {0x0AFC, 4, 0x00000006},{0x0AFE, 4, 0x00000002},{0x0B04, 2, 0x00000009},{0x0B18, 4, 0x00000007},{0x0B1A, 4, 0x00000002},
    {0x0B20, 2, 0x00000009},{0x0B34, 4, 0x00000008},{0x0B36, 4, 0x00000002},{0x0B3C, 2, 0x00000009},{0x0B50, 4, 0x00000009},
    {0x0B52, 4, 0x00000002},{0x0B58, 2, 0x00000009},{0x0B6C, 4, 0x0000000A},{0x0B6E, 4, 0x00000002},{0x0B74, 2, 0x00000009},
    {0x0B88, 4, 0x0000000B},{0x0B8A, 4, 0x00000002},{0x0B90, 2, 0x00000009},{0x0BA4, 4, 0x0000000C},{0x0BA6, 4, 0x00000002},
    {0x0BAC, 2, 0x00000009},{0x0BC0, 4, 0x0000000D},{0x0BC2, 4, 0x00000002},{0x0BC8, 2, 0x00000009},{0x0BDC, 4, 0x0000000E},
    {0x0BDE, 4, 0x00000002},{0x0BE4, 2, 0x00000009},{0x0BF8, 4, 0x0000000F},{0x0BFA, 4, 0x00000002},{0x0C00, 2, 0x00000009},
    {0x0C14, 4, 0x00000010},{0x0C16, 4, 0x00000002},{0x0C1C, 2, 0x00000009},{0x0C30, 4, 0x00000011},{0x0C32, 4, 0x00000002},
    {0x0C38, 2, 0x00000009},{0x0C4C, 4, 0x00000012},{0x0C4E, 4, 0x00000002},{0x0C54, 2, 0x00000009},{0x0C68, 4, 0x00000013},
    {0x0C6A, 4, 0x00000002},{0x0C70, 2, 0x00000009},{0x0C86, 4, 0x00000002},{0x0C8C, 2, 0x00000007},{0x0CA0, 4, 0x00000001},
    {0x0CA2, 4, 0x00000002},{0x0CA8, 2, 0x00000007},{0x0CBC, 4, 0x00000002},{0x0CBE, 4, 0x00000002},{0x0CC4, 2, 0x00000007},
    {0x0CD8, 4, 0x00000003},{0x0CDA, 4, 0x00000002},{0x0CE0, 2, 0x00000007},{0x0CF4, 4, 0x00000004},{0x0CF6, 4, 0x00000002},
    {0x0CFC, 2, 0x00000007},{0x0D10, 4, 0x00000005},{0x0D12, 4, 0x00000002},{0x0D18, 2, 0x00000007},{0x0D2C, 4, 0x00000006},
    {0x0D2E, 4, 0x00000002},{0x0D34, 2, 0x00000007},{0x0D48, 4, 0x00000007},{0x0D4A, 4, 0x00000002},{0x0D50, 2, 0x00000007},
    {0x0D64, 4, 0x00000008},{0x0D66, 4, 0x00000002},{0x0D6C, 2, 0x00000007},{0x0D80, 4, 0x00000009},{0x0D82, 4, 0x00000002},
    {0x0D88, 2, 0x00000007},{0x0D9C, 4, 0x0000000A},{0x0D9E, 4, 0x00000002},{0x0DA4, 2, 0x00000007},{0x0DB8, 4, 0x0000000B},
    {0x0DBA, 4, 0x00000002},{0x0DC0, 2, 0x00000007},{0x0DD4, 4, 0x0000000C},{0x0DD6, 4, 0x00000002},{0x0DDC, 2, 0x00000007},
    {0x0DF0, 4, 0x0000000D},{0x0DF2, 4, 0x00000002},{0x0DF8, 2, 0x00000007},{0x0E0C, 4, 0x0000000E},{0x0E0E, 4, 0x00000002},
    {0x0E14, 2, 0x00000007},{0x0E28, 4, 0x0000000F},{0x0E2A, 4, 0x00000002},{0x0E30, 2, 0x00000007},{0x0E44, 4, 0x00000010},
    {0x0E46, 4, 0x00000002},{0x0E4C, 2, 0x00000007},{0x0E60, 4, 0x00000011},{0x0E62, 4, 0x00000002},{0x0E68, 2, 0x00000007},
    {0x0E7C, 4, 0x00000012},{0x0E7E, 4, 0x00000002},{0x0E84, 2, 0x00000007},{0x0E98, 4, 0x00000013},{0x0E9A, 4, 0x00000002},
    {0x0EA0, 2, 0x00000007},{0x0EB6, 4, 0x00000002},{0x0EBC, 2, 0x00000001},{0x0ED0, 4, 0x00000001},{0x0ED2, 4, 0x00000002},
    {0x0ED8, 2, 0x00000001},{0x0EEC, 4, 0x00000002},{0x0EEE, 4, 0x00000002},{0x0EF4, 2, 0x00000001},{0x0F08, 4, 0x00000003},
    {0x0F0A, 4, 0x00000002},{0x0F10, 2, 0x00000001},{0x0F24, 4, 0x00000004},{0x0F26, 4, 0x00000002},{0x0F2C, 2, 0x00000001},
    {0x0F40, 4, 0x00000005},{0x0F42, 4, 0x00000002},{0x0F48, 2, 0x00000001},{0x0F5C, 4, 0x00000006},{0x0F5E, 4, 0x00000002},
    {0x0F64, 2, 0x00000001},{0x0F78, 4, 0x00000007},{0x0F7A, 4, 0x00000002},{0x0F80, 2, 0x00000001},{0x0F94, 4, 0x00000008},
    {0x0F96, 4, 0x00000002},{0x0F9C, 2, 0x00000001},{0x0FB0, 4, 0x00000009},{0x0FB2, 4, 0x00000002},{0x0FB8, 2, 0x00000001},
    {0x0FCC, 4, 0x0000000A},{0x0FCE, 4, 0x00000002},{0x0FD4, 2, 0x00000001},{0x0FE8, 4, 0x0000000B},{0x0FEA, 4, 0x00000002},
    {0x0FF0, 2, 0x00000001},{0x1004, 4, 0x0000000C},{0x1006, 4, 0x00000002},{0x100C, 2, 0x00000001},{0x1020, 4, 0x0000000D},
    {0x1022, 4, 0x00000002},{0x1028, 2, 0x00000001},{0x103C, 4, 0x0000000E},{0x103E, 4, 0x00000002},{0x1044, 2, 0x00000001},
    {0x1058, 4, 0x0000000F},{0x105A, 4, 0x00000002},{0x1060, 2, 0x00000001},{0x1074, 4, 0x00000010},{0x1076, 4, 0x00000002},
    {0x107C, 2, 0x00000001},{0x1090, 4, 0x00000011},{0x1092, 4, 0x00000002},{0x1098, 2, 0x00000001},{0x10AC, 4, 0x00000012},
    {0x10AE, 4, 0x00000002},{0x10B4, 2, 0x00000001},{0x10C8, 4, 0x00000013},{0x10CA, 4, 0x00000002},{0x10D0, 2, 0x00000001},
    {0x10E6, 4, 0x00000002},{0x10EC, 4, 0x00000002},{0x1100, 4, 0x00000001},{0x1102, 4, 0x00000002},{0x1108, 4, 0x00000002},
    {0x111C, 4, 0x00000002},{0x111E, 4, 0x00000002},{0x1124, 4, 0x00000002},{0x1138, 4, 0x00000003},{0x113A, 4, 0x00000002},
    {0x1140, 4, 0x00000002},{0x1154, 4, 0x00000004},{0x1156, 4, 0x00000002},{0x115C, 4, 0x00000002},{0x1170, 4, 0x00000005},
    {0x1172, 4, 0x00000002},{0x1178, 4, 0x00000002},{0x118C, 4, 0x00000006},{0x118E, 4, 0x00000002},{0x1194, 4, 0x00000002},
    {0x11A8, 4, 0x00000007},{0x11AA, 4, 0x00000002},{0x11B0, 4, 0x00000002},{0x11C4, 4, 0x00000008},{0x11C6, 4, 0x00000002},
    {0x11CC, 4, 0x00000002},{0x11E0, 4, 0x00000009},{0x11E2, 4, 0x00000002},{0x11E8, 4, 0x00000002},{0x11FC, 4, 0x0000000A},
    {0x11FE, 4, 0x00000002},{0x1204, 4, 0x00000002},{0x1218, 4, 0x0000000B},{0x121A, 4, 0x00000002},{0x1220, 4, 0x00000002},
    {0x1234, 4, 0x0000000C},{0x1236, 4, 0x00000002},{0x123C, 4, 0x00000002},{0x1250, 4, 0x0000000D},{0x1252, 4, 0x00000002},
    {0x1258, 4, 0x00000002},{0x126C, 4, 0x0000000E},{0x126E, 4, 0x00000002},{0x1274, 4, 0x00000002},{0x1288, 4, 0x0000000F},
    {0x128A, 4, 0x00000002},{0x1290, 4, 0x00000002},{0x12A4, 4, 0x00000010},{0x12A6, 4, 0x00000002},{0x12AC, 4, 0x00000002},
    {0x12C0, 4, 0x00000011},{0x12C2, 4, 0x00000002},{0x12C8, 4, 0x00000002},{0x12DC, 4, 0x00000012},{0x12DE, 4, 0x00000002},
    {0x12E4, 4, 0x00000002},{0x12F8, 4, 0x00000013},{0x12FA, 4, 0x00000002},{0x1300, 4, 0x00000002},{0x1316, 4, 0x00000002},
    {0x131C, 2, 0x00000003},{0x1330, 4, 0x00000001},{0x1332, 4, 0x00000002},{0x1338, 2, 0x00000003},{0x134C, 4, 0x00000002},
    {0x134E, 4, 0x00000002},{0x1354, 2, 0x00000003},{0x1368, 4, 0x00000003},{0x136A, 4, 0x00000002},{0x1370, 2, 0x00000003},
    {0x1384, 4, 0x00000004},{0x1386, 4, 0x00000002},{0x138C, 2, 0x00000003},{0x13A0, 4, 0x00000005},{0x13A2, 4, 0x00000002},
    {0x13A8, 2, 0x00000003},{0x13BC, 4, 0x00000006},{0x13BE, 4, 0x00000002},{0x13C4, 2, 0x00000003},{0x13D8, 4, 0x00000007},
    {0x13DA, 4, 0x00000002},{0x13E0, 2, 0x00000003},{0x13F4, 4, 0x00000008},{0x13F6, 4, 0x00000002},{0x13FC, 2, 0x00000003},
    {0x1410, 4, 0x00000009},{0x1412, 4, 0x00000002},{0x1418, 2, 0x00000003},{0x142C, 4, 0x0000000A},{0x142E, 4, 0x00000002},
    {0x1434, 2, 0x00000003},{0x1448, 4, 0x0000000B},{0x144A, 4, 0x00000002},{0x1450, 2, 0x00000003},{0x1464, 4, 0x0000000C},
    {0x1466, 4, 0x00000002},{0x146C, 2, 0x00000003},{0x1480, 4, 0x0000000D},{0x1482, 4, 0x00000002},{0x1488, 2, 0x00000003},
    {0x149C, 4, 0x0000000E},{0x149E, 4, 0x00000002},{0x14A4, 2, 0x00000003},{0x14B8, 4, 0x0000000F},{0x14BA, 4, 0x00000002},
    {0x14C0, 2, 0x00000003},{0x14D4, 4, 0x00000010},{0x14D6, 4, 0x00000002},{0x14DC, 2, 0x00000003},{0x14F0, 4, 0x00000011},
    {0x14F2, 4, 0x00000002},{0x14F8, 2, 0x00000003},{0x150C, 4, 0x00000012},{0x150E, 4, 0x00000002},{0x1514, 2, 0x00000003},
    {0x1528, 4, 0x00000013},{0x152A, 4, 0x00000002},{0x1530, 2, 0x00000003},{0x1546, 4, 0x00000002},{0x154C, 2, 0x00000006},
    {0x1560, 4, 0x00000001},{0x1562, 4, 0x00000002},{0x1568, 2, 0x00000006},{0x157C, 4, 0x00000002},{0x157E, 4, 0x00000002},
    {0x1584, 2, 0x00000006},{0x1598, 4, 0x00000003},{0x159A, 4, 0x00000002},{0x15A0, 2, 0x00000006},{0x15B4, 4, 0x00000004},
    {0x15B6, 4, 0x00000002},{0x15BC, 2, 0x00000006},{0x15D0, 4, 0x00000005},{0x15D2, 4, 0x00000002},{0x15D8, 2, 0x00000006},
    {0x15EC, 4, 0x00000006},{0x15EE, 4, 0x00000002},{0x15F4, 2, 0x00000006},{0x1608, 4, 0x00000007},{0x160A, 4, 0x00000002},
    {0x1610, 2, 0x00000006},{0x1624, 4, 0x00000008},{0x1626, 4, 0x00000002},{0x162C, 2, 0x00000006},{0x1640, 4, 0x00000009},
    {0x1642, 4, 0x00000002},{0x1648, 2, 0x00000006},{0x165C, 4, 0x0000000A},{0x165E, 4, 0x00000002},{0x1664, 2, 0x00000006},
    {0x1678, 4, 0x0000000B},{0x167A, 4, 0x00000002},{0x1680, 2, 0x00000006},{0x1694, 4, 0x0000000C},{0x1696, 4, 0x00000002},
    {0x169C, 2, 0x00000006},{0x16B0, 4, 0x0000000D},{0x16B2, 4, 0x00000002},{0x16B8, 2, 0x00000006},{0x16CC, 4, 0x0000000E},
    {0x16CE, 4, 0x00000002},{0x16D4, 2, 0x00000006},{0x16E8, 4, 0x0000000F},{0x16EA, 4, 0x00000002},{0x16F0, 2, 0x00000006},
    {0x1704, 4, 0x00000010},{0x1706, 4, 0x00000002},{0x170C, 2, 0x00000006},{0x1720, 4, 0x00000011},{0x1722, 4, 0x00000002},
    {0x1728, 2, 0x00000006},{0x173C, 4, 0x00000012},{0x173E, 4, 0x00000002},{0x1744, 2, 0x00000006},{0x1758, 4, 0x00000013},
    {0x175A, 4, 0x00000002},{0x1760, 2, 0x00000006},{0x1776, 4, 0x00000002},{0x177C, 2, 0x00000005},{0x1790, 4, 0x00000001},
    {0x1792, 4, 0x00000002},{0x1798, 2, 0x00000005},{0x17AC, 4, 0x00000002},{0x17AE, 4, 0x00000002},{0x17B4, 2, 0x00000005},
    {0x17C8, 4, 0x00000003},{0x17CA, 4, 0x00000002},{0x17D0, 2, 0x00000005},{0x17E4, 4, 0x00000004},{0x17E6, 4, 0x00000002},
    {0x17EC, 2, 0x00000005},{0x1800, 4, 0x00000005},{0x1802, 4, 0x00000002},{0x1808, 2, 0x00000005},{0x181C, 4, 0x00000006},
    {0x181E, 4, 0x00000002},{0x1824, 2, 0x00000005},{0x1838, 4, 0x00000007},{0x183A, 4, 0x00000002},{0x1840, 2, 0x00000005},
    {0x1854, 4, 0x00000008},{0x1856, 4, 0x00000002},{0x185C, 2, 0x00000005},{0x1870, 4, 0x00000009},{0x1872, 4, 0x00000002},
    {0x1878, 2, 0x00000005},{0x188C, 4, 0x0000000A},{0x188E, 4, 0x00000002},{0x1894, 2, 0x00000005},{0x18A8, 4, 0x0000000B},
    {0x18AA, 4, 0x00000002},{0x18B0, 2, 0x00000005},{0x18C4, 4, 0x0000000C},{0x18C6, 4, 0x00000002},{0x18CC, 2, 0x00000005},
    {0x18E0, 4, 0x0000000D},{0x18E2, 4, 0x00000002},{0x18E8, 2, 0x00000005},{0x18FC, 4, 0x0000000E},{0x18FE, 4, 0x00000002},
    {0x1904, 2, 0x00000005},{0x1918, 4, 0x0000000F},{0x191A, 4, 0x00000002},{0x1920, 2, 0x00000005},{0x1934, 4, 0x00000010},
    {0x1936, 4, 0x00000002},{0x193C, 2, 0x00000005},{0x1950, 4, 0x00000011},{0x1952, 4, 0x00000002},{0x1958, 2, 0x00000005},
    {0x196C, 4, 0x00000012},{0x196E, 4, 0x00000002},{0x1974, 2, 0x00000005},{0x1988, 4, 0x00000013},{0x198A, 4, 0x00000002},
    {0x1990, 2, 0x00000005},{0x19A6, 4, 0x00000002},{0x19AC, 2, 0x00000004},{0x19C0, 4, 0x00000001},{0x19C2, 4, 0x00000002},
    {0x19C8, 2, 0x00000004},{0x19DC, 4, 0x00000002},{0x19DE, 4, 0x00000002},{0x19E4, 2, 0x00000004},{0x19F8, 4, 0x00000003},
    {0x19FA, 4, 0x00000002},{0x1A00, 2, 0x00000004},{0x1A14, 4, 0x00000004},{0x1A16, 4, 0x00000002},{0x1A1C, 2, 0x00000004},
    {0x1A30, 4, 0x00000005},{0x1A32, 4, 0x00000002},{0x1A38, 2, 0x00000004},{0x1A4C, 4, 0x00000006},{0x1A4E, 4, 0x00000002},
    {0x1A54, 2, 0x00000004},{0x1A68, 4, 0x00000007},{0x1A6A, 4, 0x00000002},{0x1A70, 2, 0x00000004},{0x1A84, 4, 0x00000008},
    {0x1A86, 4, 0x00000002},{0x1A8C, 2, 0x00000004},{0x1AA0, 4, 0x00000009},{0x1AA2, 4, 0x00000002},{0x1AA8, 2, 0x00000004},
    {0x1ABC, 4, 0x0000000A},{0x1ABE, 4, 0x00000002},{0x1AC4, 2, 0x00000004},{0x1AD8, 4, 0x0000000B},{0x1ADA, 4, 0x00000002},
    {0x1AE0, 2, 0x00000004},{0x1AF4, 4, 0x0000000C},{0x1AF6, 4, 0x00000002},{0x1AFC, 2, 0x00000004},{0x1B10, 4, 0x0000000D},
    {0x1B12, 4, 0x00000002},{0x1B18, 2, 0x00000004},{0x1B2C, 4, 0x0000000E},{0x1B2E, 4, 0x00000002},{0x1B34, 2, 0x00000004},
    {0x1B48, 4, 0x0000000F},{0x1B4A, 4, 0x00000002},{0x1B50, 2, 0x00000004},{0x1B64, 4, 0x00000010},{0x1B66, 4, 0x00000002},
    {0x1B6C, 2, 0x00000004},{0x1B80, 4, 0x00000011},{0x1B82, 4, 0x00000002},{0x1B88, 2, 0x00000004},{0x1B9C, 4, 0x00000012},
    {0x1B9E, 4, 0x00000002},{0x1BA4, 2, 0x00000004},{0x1BB8, 4, 0x00000013},{0x1BBA, 4, 0x00000002},{0x1BC0, 2, 0x00000004},
    {0x1BD6, 4, 0x00000002},{0x1BDC, 2, 0x0000000B},{0x1BF0, 4, 0x00000001},{0x1BF2, 4, 0x00000002},{0x1BF8, 2, 0x0000000B},
    {0x1C0C, 4, 0x00000002},{0x1C0E, 4, 0x00000002},{0x1C14, 2, 0x0000000B},{0x1C28, 4, 0x00000003},{0x1C2A, 4, 0x00000002},
    {0x1C30, 2, 0x0000000B},{0x1C44, 4, 0x00000004},{0x1C46, 4, 0x00000002},{0x1C4C, 2, 0x0000000B},{0x1C60, 4, 0x00000005},
    {0x1C62, 4, 0x00000002},{0x1C68, 2, 0x0000000B},{0x1C7C, 4, 0x00000006},{0x1C7E, 4, 0x00000002},{0x1C84, 2, 0x0000000B},
    {0x1C98, 4, 0x00000007},{0x1C9A, 4, 0x00000002},{0x1CA0, 2, 0x0000000B},{0x1CB4, 4, 0x00000008},{0x1CB6, 4, 0x00000002},
    {0x1CBC, 2, 0x0000000B},{0x1CD0, 4, 0x00000009},{0x1CD2, 4, 0x00000002},{0x1CD8, 2, 0x0000000B},{0x1CEC, 4, 0x0000000A},
    {0x1CEE, 4, 0x00000002},{0x1CF4, 2, 0x0000000B},{0x1D08, 4, 0x0000000B},{0x1D0A, 4, 0x00000002},{0x1D10, 2, 0x0000000B},
    {0x1D24, 4, 0x0000000C},{0x1D26, 4, 0x00000002},{0x1D2C, 2, 0x0000000B},{0x1D40, 4, 0x0000000D},{0x1D42, 4, 0x00000002},
    {0x1D48, 2, 0x0000000B},{0x1D5C, 4, 0x0000000E},{0x1D5E, 4, 0x00000002},{0x1D64, 2, 0x0000000B},{0x1D78, 4, 0x0000000F},
    {0x1D7A, 4, 0x00000002},{0x1D80, 2, 0x0000000B},{0x1D94, 4, 0x00000010},{0x1D96, 4, 0x00000002},{0x1D9C, 2, 0x0000000B},
    {0x1DB0, 4, 0x00000011},{0x1DB2, 4, 0x00000002},{0x1DB8, 2, 0x0000000B},{0x1DCC, 4, 0x00000012},{0x1DCE, 4, 0x00000002},
    {0x1DD4, 2, 0x0000000B},{0x1DE8, 4, 0x00000013},{0x1DEA, 4, 0x00000002},{0x1DF0, 2, 0x0000000B},
};

// The 265 entity index-list entries (dword_2073134[1..265]); each is an
// offset from the pool base (dword_2120420).  Index 0 is unused (zero).
static const unsigned int sEntityIndexOffsets[265] =
{
    0x0000u,0x001Cu,0x0038u,0x0070u,0x0054u,0x008Cu,0x0118u,0x0134u,0x00C4u,0x00E0u,
    0x00A8u,0x00FCu,0x039Cu,0x0188u,0x01A4u,0x01C0u,0x01DCu,0x0348u,0x0380u,0x024Cu,
    0x0268u,0x0284u,0x02A0u,0x02BCu,0x02D8u,0x02F4u,0x0310u,0x0214u,0x0230u,0x03B8u,
    0x03D4u,0x03F0u,0x040Cu,0x0428u,0x0444u,0x0460u,0x047Cu,0x0498u,0x04B4u,0x04D0u,
    0x04ECu,0x0508u,0x0524u,0x0540u,0x055Cu,0x0C78u,0x0EA8u,0x10D8u,0x1308u,0x1768u,
    0x1538u,0x0818u,0x0A48u,0x1998u,0x05E8u,0x1BC8u,0x0C94u,0x0EC4u,0x10F4u,0x1324u,
    0x1784u,0x1554u,0x0834u,0x0A64u,0x19B4u,0x0604u,0x1BE4u,0x0CB0u,0x0EE0u,0x1110u,
    0x1340u,0x17A0u,0x1570u,0x0850u,0x0A80u,0x19D0u,0x0620u,0x1C00u,0x0CCCu,0x0EFCu,
    0x112Cu,0x135Cu,0x17BCu,0x158Cu,0x086Cu,0x0A9Cu,0x19ECu,0x063Cu,0x1C1Cu,0x0CE8u,
    0x0F18u,0x1148u,0x1378u,0x17D8u,0x15A8u,0x0888u,0x0AB8u,0x1A08u,0x0658u,0x1C38u,
    0x0D04u,0x0F34u,0x1164u,0x1394u,0x17F4u,0x15C4u,0x08A4u,0x0AD4u,0x1A24u,0x0674u,
    0x1C54u,0x0D20u,0x0F50u,0x1180u,0x13B0u,0x1810u,0x15E0u,0x08C0u,0x0AF0u,0x1A40u,
    0x0690u,0x1C70u,0x0D3Cu,0x0F6Cu,0x119Cu,0x13CCu,0x182Cu,0x15FCu,0x08DCu,0x0B0Cu,
    0x1A5Cu,0x06ACu,0x1C8Cu,0x0D58u,0x0F88u,0x11B8u,0x13E8u,0x1848u,0x1618u,0x08F8u,
    0x0B28u,0x1A78u,0x06C8u,0x1CA8u,0x0D74u,0x0FA4u,0x11D4u,0x1404u,0x1864u,0x1634u,
    0x0914u,0x0B44u,0x1A94u,0x06E4u,0x1CC4u,0x0D90u,0x0FC0u,0x11F0u,0x1420u,0x1880u,
    0x1650u,0x0930u,0x0B60u,0x1AB0u,0x0700u,0x1CE0u,0x0DACu,0x0FDCu,0x120Cu,0x143Cu,
    0x189Cu,0x166Cu,0x094Cu,0x0B7Cu,0x1ACCu,0x071Cu,0x1CFCu,0x0DC8u,0x0FF8u,0x1228u,
    0x1458u,0x18B8u,0x1688u,0x0968u,0x0B98u,0x1AE8u,0x0738u,0x1D18u,0x0DE4u,0x1014u,
    0x1244u,0x1474u,0x18D4u,0x16A4u,0x0984u,0x0BB4u,0x1B04u,0x0754u,0x1D34u,0x0E00u,
    0x1030u,0x1260u,0x1490u,0x18F0u,0x16C0u,0x09A0u,0x0BD0u,0x1B20u,0x0770u,0x1D50u,
    0x0E1Cu,0x104Cu,0x127Cu,0x14ACu,0x190Cu,0x16DCu,0x09BCu,0x0BECu,0x1B3Cu,0x078Cu,
    0x1D6Cu,0x0E38u,0x1068u,0x1298u,0x14C8u,0x1928u,0x16F8u,0x09D8u,0x0C08u,0x1B58u,
    0x07A8u,0x1D88u,0x0E54u,0x1084u,0x12B4u,0x14E4u,0x1944u,0x1714u,0x09F4u,0x0C24u,
    0x1B74u,0x07C4u,0x1DA4u,0x0E70u,0x10A0u,0x12D0u,0x1500u,0x1960u,0x1730u,0x0A10u,
    0x0C40u,0x1B90u,0x07E0u,0x1DC0u,0x0E8Cu,0x10BCu,0x12ECu,0x151Cu,0x197Cu,0x174Cu,
    0x0A2Cu,0x0C5Cu,0x1BACu,0x07FCu,0x1DDCu
};
static int BuildGraphicsEntityPool(void)
{
    BYTE *pool = (BYTE *)g_dword_2120420;

    // Pool field initialisation: apply the non-zero field writes (the pool
    // was just zeroed by the caller).  Byte-level copy: some of the
    // original dword writes are unaligned.
    DWORD i;
    for (i = 0; i < sizeof(sPoolFieldWrites) / sizeof(sPoolFieldWrites[0]); i++)
        memcpy(&pool[sPoolFieldWrites[i].off], &sPoolFieldWrites[i].val,
               (size_t)sPoolFieldWrites[i].sz);

    // Entity index list: slot i+1 -> pool + sEntityIndexOffsets[i].
    for (i = 0; i < 265; i++)
        g_dword_2073134[i + 1] = &pool[sEntityIndexOffsets[i]];

    // Entity count (last valid index).
    g_dword_2063134 = 265;
    return 0;
}
// ============================================================
//  RegisterGraphicsEntities  (0x18D04D0 - 0x18D195E)  -- register the
//  frontend graphics-entity descriptors (built by BuildGraphicsEntityPool)
//  into the per-object lists of the graphics-entity object pool (0x211A408)
//  and append each newly-registered descriptor to the dirty list
//  (dword_2073940 / count dword_2073938).
//  [NOT byte-identical] refs the high-VA object pool (0x211A408), the
//  descriptor pool (0x2120420), the dirty list (dword_2073940) and the rdata
//  source values (0x1D9DF8C..0x1D9E0CC) at their original addresses.  No
//  function calls (pure data registration): a straight-line run of 20
//  descriptors, a 4x4 do-while over 16 descriptors, a 20x9 second loop over
//  the descriptor tables, and a final 8-descriptor tail.
//
//  Each descriptor is a 28-byte record in the descriptor pool:
//     +0x00  ptr (self: the record base, stored in the object list / dirty list)
//     +0x04  key   (compared to the expected value; written to expected)
//     +0x08  mask  (OR'd with the object's mask source)
//     +0x0E  idx   (index byte into the object pool)
//     +0x18  flag  (one-shot "already registered" byte)
//  A descriptor is (re)registered when its key != expected: if
//  (object.maskSrc & mask) == 0 the {ptr, key} entry is appended to the
//  object's list (object.maskSrc is NOT modified, only the descriptor's mask
//  field accumulates it), the key is set to expected, and if the flag was
//  clear the ptr is pushed onto the dirty list and the flag set.
//
//  The object pool is only referenced via indices 0..2 at run time, so the
//  {ptr,key} list never overflows (max 112 entries observed).  Return value
//  (the last descriptor's object base) is discarded by the sole caller
//  ResetGraphicsEntities, so 0 is returned.
// ============================================================
static void RegGraphicsEntity(BYTE *pool, int off, DWORD expected)
{
    BYTE *rec = pool + off;
    DWORD key = *(DWORD *)(rec + 4);
    if (key != expected)
    {
        F3GraphicsEntityObj *obj = &g_objPool[rec[14]];   // idx is a byte < 256
        DWORD maskSrc = obj->maskSrc;
        DWORD mask    = *(DWORD *)(rec + 8);
        if ((maskSrc & mask) == 0)
        {
            *(DWORD *)(rec + 8) = maskSrc | mask;
            int c = (int)obj->count;
            obj->list[c * 2]     = (DWORD)rec;
            obj->list[c * 2 + 1] = key;
            obj->count = (DWORD)(c + 1);
        }
        *(DWORD *)(rec + 4) = expected;
        if (rec[24] == 0)
        {
            g_dword_2073940[g_dword_2073938] = (DWORD)rec;
            rec[24] = 1;
            g_dword_2073938++;
        }
    }
}
static int RegisterGraphicsEntities(void)
{
    BYTE *pool = (BYTE *)g_dword_2120420;

    // Straight-line descriptors (order == the original's execution order).
    RegGraphicsEntity(pool, 0x0054, 1);             // 0x2120474
    RegGraphicsEntity(pool, 0x0038, 1);             // 0x2120458
    RegGraphicsEntity(pool, 0x0070, 4);             // 0x2120490  (dword_1D9E004)
    RegGraphicsEntity(pool, 0x0000, 3);             // 0x2120420  (dword_1D9E0C0)
    RegGraphicsEntity(pool, 0x00E0, 0);             // 0x2120500
    RegGraphicsEntity(pool, 0x00FC, 0x80);          // 0x212051C
    RegGraphicsEntity(pool, 0x00A8, 8);             // 0x21204C8  (dword_1D9DFF4)
    RegGraphicsEntity(pool, 0x0118, 2);             // 0x2120538  (dword_1D9E088)
    RegGraphicsEntity(pool, 0x0134, 1);             // 0x2120554  (dword_1D9E084)
    RegGraphicsEntity(pool, 0x008C, 3);             // 0x21204AC  (dword_1D9E0CC)
    RegGraphicsEntity(pool, 0x00C4, 0);             // 0x21204E4
    RegGraphicsEntity(pool, 0x001C, 15);            // 0x212043C
    RegGraphicsEntity(pool, 0x0214, 0);             // 0x2120634
    RegGraphicsEntity(pool, 0x0230, 0);             // 0x2120650
    RegGraphicsEntity(pool, 0x0188, 0);             // 0x21205A8
    RegGraphicsEntity(pool, 0x01A4, 0x42800000);    // 0x21205C4
    RegGraphicsEntity(pool, 0x01C0, 0x3F800000);    // 0x21205E0
    RegGraphicsEntity(pool, 0x01DC, 0x42800000);    // 0x21205FC
    RegGraphicsEntity(pool, 0x0348, 1);             // 0x2120768  (dword_1D9E050)
    RegGraphicsEntity(pool, 0x0380, 0xFFFFFFFF);    // 0x21207A0

    // Do-while: 4 iterations x 112-byte blocks == 16 contiguous 28-byte
    // descriptors starting at 0x21207D8 (expected key 0 for all).
    RegGraphicsEntity(pool, 0x03B8, 0);
    RegGraphicsEntity(pool, 0x03D4, 0);
    RegGraphicsEntity(pool, 0x03F0, 0);
    RegGraphicsEntity(pool, 0x040C, 0);
    RegGraphicsEntity(pool, 0x0428, 0);
    RegGraphicsEntity(pool, 0x0444, 0);
    RegGraphicsEntity(pool, 0x0460, 0);
    RegGraphicsEntity(pool, 0x047C, 0);
    RegGraphicsEntity(pool, 0x0498, 0);
    RegGraphicsEntity(pool, 0x04B4, 0);
    RegGraphicsEntity(pool, 0x04D0, 0);
    RegGraphicsEntity(pool, 0x04EC, 0);
    RegGraphicsEntity(pool, 0x0508, 0);
    RegGraphicsEntity(pool, 0x0524, 0);
    RegGraphicsEntity(pool, 0x0540, 0);
    RegGraphicsEntity(pool, 0x055C, 0);

    // Second loop: 20 iterations x 9 descriptor tables.  The iteration
    // offset is 28*i for i<16 and 28*i+0x1A5C for i>=16 (the 4 high blocks).
    int i;
    for (i = 0; i < 20; i++)
    {
        int off = 28 * i + (i >= 16 ? 0x1A5C : 0);
        RegGraphicsEntity(pool, 0x1538 + off, 2);   // 0x2121958 (dword_1D9E03C)
        RegGraphicsEntity(pool, 0x1768 + off, 2);   // 0x2121B88 (dword_1D9E03C)
        RegGraphicsEntity(pool, 0x0EA8 + off, 3);   // 0x21212C8 (dword_1D9DF8C)
        RegGraphicsEntity(pool, 0x10D8 + off, 3);   // 0x21214F8 (dword_1D9DF8C)
        RegGraphicsEntity(pool, 0x1308 + off, 3);   // 0x2121728 (dword_1D9DF8C)
        RegGraphicsEntity(pool, 0x0818 + off, 0);   // 0x2120C38 (xmm1 == 0)
        RegGraphicsEntity(pool, 0x0C78 + off, 2);   // 0x2121098 (dword_1D9E048)
        RegGraphicsEntity(pool, 0x05E8 + off, 1);   // 0x2120A08
        RegGraphicsEntity(pool, 0x1998 + off, 0);   // 0x2121DB8
    }

    // Tail descriptors.
    RegGraphicsEntity(pool, 0x024C, 0);             // 0x212066C
    RegGraphicsEntity(pool, 0x0268, 1);             // 0x2120688  (dword_1D9E064)
    RegGraphicsEntity(pool, 0x0284, 1);             // 0x21206A4  (dword_1D9E064)
    RegGraphicsEntity(pool, 0x02A0, 1);             // 0x21206C0  (dword_1D9E064)
    RegGraphicsEntity(pool, 0x02BC, 8);             // 0x21206DC  (dword_1D9E034)
    RegGraphicsEntity(pool, 0x02D8, 0);             // 0x21206F8
    RegGraphicsEntity(pool, 0x02F4, 0xFFFFFFFF);    // 0x2120714
    RegGraphicsEntity(pool, 0x0310, 0xFFFFFFFF);    // 0x2120730

    return 0;   // original returns the last descriptor's object base (ignored)
}
// ============================================================
//  SetGraphicsProperty  (sub_5A3080, 0x5A3080 - 0x5A30C6)  -- record a
//  type-2 frontend graphics-entity property value in the per-(slot,code)
//  value table and, on change, notify the device.
//  [NOT byte-identical] refs the high-VA globals device object
//  (dword_2075F54) and the value table (dword_1DB4F58) at their original
//  addresses, and null-guards the device vtable dereference (the original
//  faults on an unbound device).
//
//  int SetGraphicsProperty(int value, unsigned int slot, int code)
//
//  slot is the entity id (a full WORD; a value >= 0x101 wraps by -241 for the
//  table write, matching the device-slot tables in worker.cpp), code is the
//  sub-field index.  Behavior:
//    1. If the compare slot table[16*slot + code] already equals value,
//       return 0.
//    2. Else write table[16*slot' + code] = value (slot' = slot if slot <
//       0x101 else slot - 241) and return the device's property setter, device
//       vtable slot 69 (offset 0x114): vt[69](device, slot, code, value) with
//       the device object as the first stack argument.
//
//  Called from the type-2 branch of SyncGraphicsEntities below (and, in the
//  original, from the sibling committers sub_18D0360 / sub_18D03E0).  Returns
//  the device-method result (ignored by the caller here).
// ============================================================
static int SetGraphicsProperty(int value, unsigned int slot, int code)
{
    void *dev = g_dword_2075F54;
    unsigned int cmpIdx = 16u * slot + (unsigned)code;
    if (g_dword_1DB4F58[cmpIdx] == (DWORD)value)
        return 0;
    unsigned int writeSlot = (slot >= 0x101) ? (slot - 241) : slot;
    g_dword_1DB4F58[16u * writeSlot + (unsigned)code] = (DWORD)value;
    int result = 0;
    if (dev != 0)   // null-guard; the original would fault pre-device
        result = ((int (*)(void *, unsigned int, int, int))
            (*(void ***)dev)[69])(dev, slot, code, value);
    return result;
}

// ============================================================
//  SyncGraphicsEntities  (sub_18D02E0, 0x18D02E0 - 0x18D0357)  -- commit
//  every frontend graphics-entity descriptor's value to its device table and
//  notify the device of any change.
//  [NOT byte-identical] refs the high-VA globals the entity index list
//  (dword_2073138), the pool count (dword_2063134), the device object
//  (dword_2075F54), the type-1 value table (dword_1DB4B58) and the
//  dirty-registration counter (dword_2073938) at their original addresses,
//  makes a relative call to SetGraphicsProperty (sub_5A3080), and
//  null-guards the device vtable dereference (the original faults on an
//  unbound device).
//
//  int SyncGraphicsEntities(void)
//
//  The index list (dword_2073138 == g_dword_2073134 + 1) is 0-indexed in the
//  original, so entity k sits at g_dword_2073134[k + 1]; iterate k = 0..
//  count-1.  For each entity, keyed by the descriptor's type byte (+0x0E):
//    - type 1: if the type-1 table slot (+0x14) differs from the descriptor
//      value (+0x04), write it and notify the device, device vtable slot 57
//      (offset 0xE4): vt[57](device, index, value).
//    - type 2: record the value through SetGraphicsProperty (the per- (slot,
//      code) table + device vtable slot 69).
//    In every case clear the descriptor's dirty byte (+0x18) and copy the
//    value (+0x04) into the applied-value slot (+0x00).  Finally clear the
//    dirty-registration counter (dword_2073938).
//
//  Tail-called / called from ResetGraphicsEntities (0x18D1960) and, in the
//  original, sub_19CE6E0 (end) and sub_19D8510 (per-frame commit).  Returns a
//  value the callers ignore (the original returns a leftover loop EAX), so 0.
//  Replaces the sub_18D02E0 no-op stub that only cleared the counter.
// ============================================================
static int SyncGraphicsEntities(void)
{
    DWORD count = g_dword_2063134;
    DWORD i;
    for (i = 1; i <= count; i++)
    {
        BYTE  *d     = (BYTE *)g_dword_2073134[i];
        int    type  = d[0x0E];
        DWORD  value = *(DWORD *)(d + 0x04);
        DWORD  index = *(DWORD *)(d + 0x14);
        d[0x18] = 0;
        if (type == 1)
        {
            if (g_dword_1DB4B58[index] != value)
            {
                g_dword_1DB4B58[index] = value;
                if (g_dword_2075F54 != 0)   // null-guard; fault pre-device
                    ((void (*)(void *, DWORD, DWORD))
                        (*(void ***)g_dword_2075F54)[57])(g_dword_2075F54, index, value);
            }
        }
        else if (type == 2)
        {
            SetGraphicsProperty((int)value, (unsigned)(*(unsigned short *)(d + 0x0C)),
                                (int)index);
        }
        *(DWORD *)d = value;
    }
    g_dword_2073938 = 0;
    return 0;
}

// ============================================================
//  ResetGraphicsEntities  (0x18D1960 - 0x18D19AE)  -- reset and rebuild the
//  frontend graphics-entity pool and rebind it to the device.
//  [NOT byte-identical] placement-dependent (relative calls to the
//  graphics-entity subsystem callees BuildGraphicsEntityPool /
//  RegisterGraphicsEntities / SyncGraphicsEntities
//  below and the memset IAT slot) and refs the high-VA globals (the
//  dword_2120420 pool, the dword_2073938 / dword_2063134 counters, byte_211C402,
//  dword_2075F54, byte_2075F4D) at their original addresses.
//
//  Flow (exactly the original 78-byte body):
//    1. memset(&dword_2120420, 0, 0x1DF8)  -- zero the entity pool.
//    2. dword_2073938 = 0; dword_2063134 = 0;  -- reset the two counters.
//    3. BuildGraphicsEntityPool();  -- rebuild the pool + index list.
//    4. byte_211C402 = 3;  -- set the subsystem state.
//    5. dword_2075F54 = device;  -- bind the device object.
//    6. byte_2075F4D = 0;  -- clear the sync flag.
//    7. RegisterGraphicsEntities();  -- register the dirty entities.
//    8. return SyncGraphicsEntities();  -- sync/commit the entities to the
//       device (tail-called in the original).
//
//  Called by ResetGraphicsSurfaces (0x18E6D40) with the current device object
//  (dword_1E2EEF4) as the argument.  The return value is forwarded from the
//  tail-called sync step (SyncGraphicsEntities) and ignored by the caller.  Replaces
//  the sub_18D1960 no-op stub in ResetGraphicsSurfaces.
// ============================================================
static int ResetGraphicsEntities(void *device)
{
    memset(&g_dword_2120420, 0, 0x1DF8);
    g_dword_2073938 = 0;
    g_dword_2063134 = 0;
    BuildGraphicsEntityPool();
    g_byte_211C402  = 3;
    g_dword_2075F54 = device;
    g_byte_2075F4D  = 0;
    RegisterGraphicsEntities();
    return SyncGraphicsEntities();
}

static void ResetGraphicsSurfaces(void)
{
    int  flags = 0;
    BOOL fmt   = 0;

    ResetGraphicsEntities(g_dword_1E2EEF4);
    ReRegisterGraphicsState();
    flags = g_byte_2075F05 != 0 ? 0x200 : 0;
    fmt   = (g_byte_2075F05 == 0);

    // Create a 1x1 texture (texture-table row 2) into the global CTexture
    // record off_1DB12F8 (CreateTexture, 0x18D33B0).
    unsigned int texDims[2] = { 1, 1 };
    unsigned int texIndex   = 2;
    CreateTexture((void *)g_off_1DB12F8, texDims, /*mips*/1, &texIndex,
                  flags, fmt);

    // {2, fmt, 1, 1, extra} record reused in place as the near-black clear
    // color {0, fmt, 1, 1} once the first element is zeroed.
    int rec[5] = { 2, fmt, 1, 1, 0 };

    F3CsurfaceSlot slot;
    slot.vt = 0; slot.surface = 0; slot.type = 0;
    slot.fmtInfo[0] = slot.fmtInfo[1] = slot.fmtInfo[2] = slot.fmtInfo[3] = slot.fmtInfo[4] = 0;

    rec[0] = 0;
    F3CsurfaceSlot *v1 = RefreshCsurfaceSlot(&slot, g_off_1DB12F8, /*slot*/0);
    ClearCsurface(rec, v1);
    if (slot.surface != 0)
        ((void (*)(void *))((void **)(*(void **)slot.surface))[2])(slot.surface);

    int csurfDims[3] = { 1, 1, 1 };
    CreateCsurface(flags, fmt, csurfDims, /*index*/2);
    if (g_dword_1DB1318 != 0)
    {
        ((void (*)(void *))((void **)(*(void **)g_dword_1DB1318))[2])(g_dword_1DB1318);
        g_dword_1DB1318 = 0;
    }
    if (g_dword_1E2EEF4 != 0)   // null-guard; the original would fault pre-device
        ((void (*)(void *, int, int, int, DWORD, BOOL, void **, int))
            (*(void ***)g_dword_1E2EEF4)[25])(
            g_dword_1E2EEF4, 1, 1, flags, g_dword_1D9B040, fmt, &g_dword_1DB1318, 0);

    for (int i = 0; i < 6; ++i)
    {
        rec[0] = 0;
        F3CsurfaceSlot *v2 = sub_1A046D0(&slot, (const F3CsurfaceSlot *)g_off_1DB1314, i);
        ClearCsurface(rec, v2);
        if (slot.surface != 0)
            ((void (*)(void *))((void **)(*(void **)slot.surface))[2])(slot.surface);
    }

    memset(&g_dword_1E2EF10, 0, 0x40);
    memset(&g_dword_1E2EF60, 0, 0x40);
    g_qword_1E2EF50[0] = 0;
    g_qword_1E2EF50[1] = 0;
}

// ============================================================
//  Game-frame steps driven by RunGameLoop (0x619D70), the frontend's
//  per-frame device pump.
//
//  The XLO device vtable slots used here (device in ECX, stdcall-style push
//  args): [3]  = status query (0x88760868 / 0x88760869 == device lost),
//  [17] = frame flush/submit, [41] = begin (first frame only),
//  [42] = end (when the frame counter wraps to 0),
//  [43] = present (the 4-byte device-slot state packed little-endian + a
//  trailing zeroed 8-byte float slot).
// ============================================================
static DWORD g_unk_1DB12B4   = 0xFF000000;  // 0x1DB12B4  device-slot state (worker.cpp keeps its own copy)
int   g_dword_1E2ED7C = 0;           // 0x1E2ED7C  frame-nesting counter
static CRITICAL_SECTION g_stru_20FC948;     // 0x20FC948  device critical section
static int   g_stru_20FC948Init = 0;
static double g_dbl_20FC960  = 0.0;         // 0x20FC960  last frame time (under the device CS)
static BYTE  g_unk_1DB21F0[0x3418] = { 0 }; // 0x1DB21F0  per-frame graphics-state block
static BYTE  g_unk_2063030[0x40]   = { 0 }; // 0x2063030
static BYTE  g_unk_2063070[0x40]   = { 0 }; // 0x2063070

// sub_18DDDB0 -- per-frame graphics-state flush (device-table vtable slots,
// the 0x2000-byte state-region 0xFE mask reset).  Remaining stub: it is its
// own unimplemented cluster, called once per frame by BeginFrame.
static void sub_18DDDB0(void) {}
// sub_18CB6A0 -- flush the pending device-operation lists (0x715-byte
// cluster over the 0x20FC98C..0x20FC9C8 object lists + the FPS records at
// 0x2074548/0x20B5F70).  Remaining stub, called by FlushRenderFrame.
static void sub_18CB6A0(void) {}
// sub_18F0460 -- interpolated frame time from the per-slot time records
// (sub_18CB350 + the 0x1E2ED74/0x1E2ED78 scalars).  Remaining stub.
static double sub_18F0460(void) { return 0.0; }

//  PresentFrame  (0x18F1060 - 0x18F10A7)  -- present the previous frame.
//  stdcall @4, arg = pointer to the 4-byte device-slot state (packed
//  little-endian into one DWORD).  Dispatches device vtable[43] (0xAC):
//    (device, 0, 0, 1, packedSlotState, 0.0)
//  where the trailing value is the 8-byte zeroed float slot the original
//  writes with `fstp` (0x0 as both a float and a double).
//  [NOT byte-identical] placement-dependent (relative call, the vtable
//  dispatch through the high-VA device global) and refs g_dword_1E2EEF4 / the
//  slot-state byte at their original high VAs; null-guarded (the original
//  would fault pre-device).
static void PresentFrame(const DWORD *slotState)
{
    if (g_dword_1E2EEF4 == 0)
        return;
    ((void (*)(void *, int, int, int, DWORD, double))
        (*(void ***)g_dword_1E2EEF4)[43])(
        g_dword_1E2EEF4, 0, 0, 1, *slotState, 0.0);
}

//  BeginFrame  (0x18F0B60 - 0x18F0BBE)  -- per-frame bookkeeping.
//  On the first frame (frame-nesting counter 0) dispatches device vtable[41]
//  (0xA4) with the device as its single argument; increments the counter;
//  flushes the per-frame graphics state (sub_18DDDB0, stub); and 0xFE-poisons
//  the three state blocks (0x1DB21F0 x 0x3418, 0x2063070 x 0x40,
//  0x2063030 x 0x40).
//  [NOT byte-identical] placement-dependent (relative calls + the memset IAT
//  slot at its original high VA) and refs the high-VA state globals;
//  null-guarded (the original would fault pre-device).
static void BeginFrame(void)
{
    if (g_dword_1E2ED7C == 0 && g_dword_1E2EEF4 != 0)
        ((void (*)(void *))(*(void ***)g_dword_1E2EEF4)[41])(g_dword_1E2EEF4);
    ++g_dword_1E2ED7C;
    sub_18DDDB0();
    memset(g_unk_1DB21F0, 0xFE, 0x3418);
    memset(g_unk_2063070, 0xFE, 0x40);
    memset(g_unk_2063030, 0xFE, 0x40);
}

//  IsDeviceLost  (0x18EFFA0 - 0x18EFFC1)  -- query the device status
//  (vtable[3], 0x0C) and report 1 when it is 0x88760868 or 0x88760869
//  (the device's lost/reset-pending states).  [NOT byte-identical] refs the
//  high-VA device global; null-guarded (pre-device counts as "lost" so the
//  loop skips XLiveRender before the XLO device exists).
static int IsDeviceLost(void)
{
    if (g_dword_1E2EEF4 == 0)
        return 1;
    int st = (int)(DWORD)((int (*)(void *))(*(void ***)g_dword_1E2EEF4)[3])(g_dword_1E2EEF4);
    return (st == 0x88760868 || st == 0x88760869);
}

//  EndFrame  (0x18F0100 - 0x18F011A)  -- decrement the frame-nesting counter
//  and, when it wraps to 0, dispatch device vtable[42] (0xA8) with the device
//  as its single argument.  [NOT byte-identical] placement-dependent and refs
//  the high-VA device global; null-guarded (the original would fault
//  pre-device).
static void EndFrame(void)
{
    if (--g_dword_1E2ED7C == 0 && g_dword_1E2EEF4 != 0)
        ((void (*)(void *))(*(void ***)g_dword_1E2EEF4)[42])(g_dword_1E2EEF4);
}

//  FlushRenderFrame  (0x18F1F60 - 0x18F1FB4)  -- per-frame device check, flush
//  and frame-time update.  Dispatches device vtable[17] (0x44) with
//  (device, 0, 0, 0, 0); when the result is 0x88760868 (lost) it returns 0
//  (the loop then closes and waits on the worker).  Otherwise it flushes the
//  pending device operations (sub_18CB6A0, stub) and, under the device
//  critical section stru_20FC948, stores the current frame time
//  (sub_18F0460, stub) into dbl_20FC960, and returns 1.
//  [NOT byte-identical] placement-dependent (relative calls + the
//  Enter/LeaveCriticalSection IAT slots at their original high VAs) and refs
//  the high-VA device global / critical section / time slot; null-guarded (a
//  missing device reads as "no problem", so the worker is not closed
//  pre-device).
static int FlushRenderFrame(void)
{
    if (g_dword_1E2EEF4 == 0)
        return 1;
    int st = (int)(DWORD)((int (*)(void *, int, int, int, int))
        (*(void ***)g_dword_1E2EEF4)[17])(g_dword_1E2EEF4, 0, 0, 0, 0);
    if (st == 0x88760868)
        return 0;
    sub_18CB6A0();
    if (!g_stru_20FC948Init)
    {
        InitializeCriticalSection(&g_stru_20FC948);
        g_stru_20FC948Init = 1;
    }
    EnterCriticalSection(&g_stru_20FC948);
    g_dbl_20FC960 = sub_18F0460();
    LeaveCriticalSection(&g_stru_20FC948);
    return 1;
}

//  WaitForWorkerClose  (0x18F6C10 - 0x18F6C3C)  -- thiscall stop-worker step.
//  Posts WM_CLOSE (0x8000) to the worker window (worker+0x238) and blocks on
//  the worker's thread-exit event (worker+0x1A4, auto-reset) with an INFINITE
//  timeout, returning the wait status.  [NOT byte-identical] refs the Post /
//  Wait IAT slots at their original high VAs; null-guarded (a missing worker
//  is a no-op).
static DWORD WaitForWorkerClose(void *worker)
{
    if (worker == 0)
        return 0;
    PostMessageA(*(HWND *)((char *)worker + 0x238), 0x8000, 0, 0);
    return WaitForSingleObjectEx(*(HANDLE *)((char *)worker + 0x1A4), INFINITE, FALSE);
}

// ============================================================
//  SetFrontendEvent is defined below with its WaitForEvent sibling (the
//  tail-jump target); declared here so the loop can call it.
static BOOL SetFrontendEvent(void *obj);

// ============================================================
//  RunGameLoop  (0x619D70 - 0x619DD3)  -- the frontend game/render loop.
//  [NOT byte-identical] placement-dependent (relative calls to the frame
//  steps above, the XLiveRender IAT thunk, and the Sleep IAT slot at its
//  original high VA 0x01AD5xxx) and refs the high-VA globals byte_1DBD9E0
//  (0x01DBD9E0) and the frontend object dword_1DBD9E8 (0x01DBD9E8).
//
//  The _beginthread entry RunFrontEnd starts after `g_byte_1DBD9E0 = 1`.
//  While the "entered" flag is nonzero, each iteration: presents the previous
//  frame (PresentFrame on the device-slot state at 0x1DB12B4), runs the
//  per-frame bookkeeping (BeginFrame), pumps the XNA live render
//  (XLiveRender) unless the device is in a lost state (IsDeviceLost), runs
//  the end-of-frame step (EndFrame), checks/flushes the frame device
//  (FlushRenderFrame -- when it reports the lost state 0x88760868 the loop
//  closes the worker window and waits for the worker thread to exit, via
//  WaitForWorkerClose on GetWorker()), and sleeps 1 ms.  On exit it tail-
//  jumps to SetFrontendEvent (0x18C4540), which signals the frontend event
//  (SetEvent on dword_1DBD9E8+0x80) to unblock RunFrontEnd's WaitForEvent
//  gate.
// ============================================================
static void RunGameLoop(void *unused)
{
    (void)unused;
    while (g_byte_1DBD9E0 != 0)
    {
        PresentFrame(&g_unk_1DB12B4);
        BeginFrame();
        if (IsDeviceLost() == 0)
            XLiveRender();
        EndFrame();
        if (FlushRenderFrame() == 0)
            WaitForWorkerClose(GetWorker());
        Sleep(1);
    }
    SetFrontendEvent(g_unk_1DBD9E8);
}
// ============================================================
//  Forward declarations for helpers defined further down in this file that
//  the Permanent Bank Mounter thread (MountPermanentBanks, below) drives:
//  the 36-byte F3WString heap-buffer drop idiom, the global file-subsystem
//  getter (sub_1801C20), the VFS-config load (sub_1801580), and the
//  placeholder vtable slot-0 release (the stub-constructed member objects).
// ============================================================
static void  F3WStringFree(F3WString *s);
static void  FileSubsysLoadVfsConfig(void *fs, void *path);
static void  F3StubRelease(void *obj, int freeIt);
//  GetFileSubsystem (sub_1801C20) is declared in xlip.h (extern, defined
//  further down in this file) -- no local forward declaration needed.

//  F3CsGuard  -- the 8-byte local critical-section guard pair (the
//  this=guard thiscall helpers sub_1A22480 / sub_1A22460 below operate
//  on it): { the CRITICAL_SECTION, the held flag }.
typedef struct F3CsGuard
{
    CRITICAL_SECTION *cs;
    int               held;
} F3CsGuard;

//  CsGuardAcquire  (0x1A22460 pair: 0x1A22480 - 0x1A224B6)  -- enter a
//  critical section into a local guard: store the section, then either
//  TryEnterCriticalSection (the held flag = success) or the blocking
//  EnterCriticalSection (the held flag = 1).  The original is thiscall +
//  retn 8 (guard in ECX, section + flag on the stack); here a plain free
//  function per project convention.  [NOT byte-identical] refs the
//  Enter / TryEnter IAT slots at their original high VAs.
static F3CsGuard CsGuardAcquire(F3CsGuard *g, CRITICAL_SECTION *cs, char tryIt)
{
    g->cs = cs;
    if (tryIt != 0)
        g->held = (TryEnterCriticalSection(cs) != 0);
    else
    {
        EnterCriticalSection(cs);
        g->held = 1;
    }
    return *g;
}

//  CsGuardRelease  (0x1A22460 - 0x1A22478)  -- leave the guard's section
//  when the held flag is set and clear the flag.  [NOT byte-identical]
//  refs the Leave IAT slot at its original high VA.
static void CsGuardRelease(F3CsGuard *g)
{
    if (g->held != 0)
    {
        LeaveCriticalSection(g->cs);
        g->held = 0;
    }
}

//  SlotPairSet  (0x61FDE0 - 0x61FE34)  -- atomically swap the 8-byte
//  { value, 12-byte wrapper } slot pair: drop a reference on the current
//  wrapper (running its { refcount, dtor, obj } destructor block + LHFree
//  when the last reference goes), store the new pair, and take a reference
//  on the new wrapper.  The original is thiscall + retn 4 (the slot in
//  ECX, the { value, wrapper } pointer on the stack); here a plain free
//  function.  [NOT byte-identical] refs the InterlockedDecrement /
//  InterlockedIncrement IAT slots at their original high VAs.
static void *SlotPairSet(void *slot, void **pair)
{
    void *value    = pair[0];
    void *wrapper  = pair[1];
    void *curWrap  = *(void **)((char *)slot + 4);
    if (curWrap != wrapper)
    {
        if (curWrap != 0 && InterlockedDecrement((volatile LONG *)curWrap) == 0)
        {
            (*(void (*)(void *))(*(void **)((char *)curWrap + 4)))(
                *(void **)((char *)curWrap + 8));
            LHFree(curWrap);
        }
        *(void **)slot          = value;
        *(void **)((char *)slot + 4) = wrapper;
        if (wrapper != 0)
            InterlockedIncrement((volatile LONG *)wrapper);
    }
    return slot;
}

// F3InvalidParam (defined lower, in the sorted-map cluster) is the no-op
// stand-in for the original's CRT __invalid_parameter_noinfo() debug-iterator
// checks; forward-declare it so the bank string-collection destructor below
// can use it.
void F3InvalidParam(void);

//  BankStringsDtor  (0x624B10 - 0x624BEC)  -- the bank container's +0x1C
//  string-collection destructor (the sub_72C220 init counterpart).  Frees
//  every non-null element of the element array (each element's +0x0C data
//  buffer first, then the element itself), then the element array, the +0x1C
//  self-tag block, the +0x10 block, and the +0x04 self-tag block, zeroing the
//  array / block fields.  thiscall (obj in ECX).
//  [NOT byte-identical] placement-dependent (the relative E8 calls to LHFree
//  and the __invalid_parameter_noinfo IAT slot).
//
//  void BankStringsDtor(void *obj)
//
//  obj (the +0x1C member, 0x3C bytes) is the MSVC debug-mode dynamic array:
//  +0x04 a self-referencing 4-byte tag block (freed unconditionally), +0x10..
//  +0x18 a block (freed when non-null, then zeroed), +0x1C a self-referencing
//  4-byte tag block (freed unconditionally, the array-manager deref in the
//  per-element bounds checks), +0x28 / +0x2C the element-array start / end,
//  +0x30 zeroed.  Each element (a DWORD in the array) is a pointer to a heap
//  object whose +0x0C holds a separately-allocated data buffer.
static void BankStringsDtor(void *obj)
{
    DWORD *o = (DWORD *)obj;

    // The element-array bounds (a debug-iterator invariant; the original
    // __invalid_parameter_noinfo's and continues on a violation).
    if (o[0x28 / 4] > o[0x2C / 4])
        F3InvalidParam();

    DWORD arrStart = o[0x28 / 4];
    DWORD arrEnd   = o[0x2C / 4];
    DWORD tag      = o[0x1C / 4];       // the self-tag block, captured at entry

    for (DWORD i = arrStart; ; i += 4)
    {
        if (o[0x28 / 4] > o[0x2C / 4])
            F3InvalidParam();
        if (tag == 0 || tag != o[0x1C / 4])
            F3InvalidParam();
        if (i == arrEnd)
            break;

        // The tag-block deref (the null branch is the error path).
        DWORD m;
        if (tag != 0)
            m = *(DWORD *)tag;
        else
        {
            F3InvalidParam();
            m = 0;
        }
        if (i >= *(DWORD *)(m + 0x10))
            F3InvalidParam();

        DWORD el = *(DWORD *)i;
        if (el != 0)
        {
            LHFree((void *)(*(DWORD *)(el + 0x0C)));   // the element's data buffer
            LHFree((void *)el);
        }

        DWORD m2;
        if (tag != 0)
            m2 = *(DWORD *)tag;
        else
        {
            F3InvalidParam();
            m2 = 0;
        }
        if (i >= *(DWORD *)(m2 + 0x10))
            F3InvalidParam();
    }

    if (arrStart != 0)
        LHFree((void *)arrStart);
    o[0x28 / 4] = 0;
    o[0x2C / 4] = 0;
    o[0x30 / 4] = 0;
    LHFree((void *)o[0x1C / 4]);       // the self-tag block (unconditional)

    DWORD block = o[0x10 / 4];
    if (block != 0)
        LHFree((void *)block);
    o[0x10 / 4] = 0;
    o[0x14 / 4] = 0;
    o[0x18 / 4] = 0;

    LHFree((void *)o[0x04 / 4]);       // the +0x04 self-tag block (unconditional)
}

//  DestroyBankContainer  (0x626EF0 - 0x62700C)  -- the 240-byte bank
//  container's destructor (the InitBankContainer counterpart): reset the
//  three word-collections (+0xCC via BankTreeEraseRange, +0x94 via
//  NameMapEraseRange, +0x74 via SubNodeEraseRange), each followed by the LHFree of
//  the +0x18 sentinel node + the +0x0 head node and the +0x18 / +0x1C zero; DeleteCritical
//  Section the +0xB4 section; free the +0x68 word (when non-null) and zero
//  +0x68 / +0x6C / +0x70; free the +0x5C back-reference node; clear the
//  +0x1C string collection (BankStringsDtor); free the +0x04 string buffer
//  when +0x18 >= 0x10, and reset +0x14 = 0 / +0x18 = 0x0F / the +0x04 flag = 0.
//  thiscall (obj in ECX).  [NOT byte-identical] placement-dependent (the
//  E8 calls) and refs the LHFree / DeleteCriticalSection high-VA slots.
static void DestroyBankContainer(void *obj)
{
    char *o = (char *)obj;
    DWORD *w = (DWORD *)o;

    // +0xCC word-collection (the bank-tree range erase, cleared to empty).
    {
        DWORD tmp[2];
        void *head = *(void **)(o + 0xCC);          // the tree's head node
        void *sent = *(void **)(o + 0xE4);          // the tree's sentinel node
        BankTreeEraseRange(o + 0xCC, tmp, head, *(void **)(sent), head, sent);
        LHFree((void *)w[0xE4 / 4]);
        LHFree((void *)w[0xCC / 4]);
        w[0xE4 / 4] = 0;
        w[0xE8 / 4] = 0;
    }
    DeleteCriticalSection((CRITICAL_SECTION *)(o + 0xB4));

    // +0x94 word-collection (the u32 -> name map range erase, cleared to
    // empty).
    {
        DWORD tmp[2];
        void *head = *(void **)(o + 0x94);          // the tree's head node
        void *sent = *(void **)(o + 0xAC);          // the tree's sentinel node
        NameMapEraseRange(o + 0x94, tmp, head, *(void **)(sent), head, sent);
        LHFree((void *)w[0xAC / 4]);
        LHFree((void *)w[0x94 / 4]);
        w[0xAC / 4] = 0;
        w[0xB0 / 4] = 0;
    }

    // +0x74 word-collection (the SubNode range erase, cleared to empty).
    {
        DWORD tmp[2];
        void *head = *(void **)(o + 0x74);          // the tree's head node
        void *sent = *(void **)(o + 0x8C);          // the tree's sentinel node
        SubNodeEraseRange(o + 0x74, tmp, head, *(void **)(sent), head, sent);
        LHFree((void *)w[0x8C / 4]);
        LHFree((void *)w[0x74 / 4]);
        w[0x8C / 4] = 0;
        w[0x90 / 4] = 0;
    }

    if (w[0x68 / 4] != 0)
        LHFree((void *)w[0x68 / 4]);
    w[0x68 / 4] = 0;
    w[0x6C / 4] = 0;
    w[0x70 / 4] = 0;
    LHFree((void *)w[0x5C / 4]);

    BankStringsDtor(o + 0x1C);
    if (w[0x18 / 4] >= 0x10u)
        LHFree((void *)w[4 / 4]);
    w[0x14 / 4] = 0;
    w[0x18 / 4] = 0x0F;
    o[4] = 0;
}

//  ReleaseBankObject  (0x627060 - 0x62707B)  -- the 12-byte wrapper block's
//  stored destructor for the bank container: destroy the container
//  (DestroyBankContainer) and LHFree it.  cdecl (the container pointer on
//  the stack).
static void ReleaseBankObject(void *obj)
{
    if (obj != 0)
    {
        DestroyBankContainer(obj);
        LHFree(obj);
    }
}

// ============================================================
//  LHSFS::CLegacyMemoryFile (16 bytes: { vtable, wide-path slot, open-file
//  handle, +0xC word }) -- the ctor (sub_17FE8B0), the open-setup
//  (sub_17FE290, vtable slot 1), the deleting dtor (sub_18004B0, vtable
//  slot 0), and the LHVFS file-system singletons / object classes they
//  reach.  The deep IFile resolve/read chain (the CFileSystem singletons,
//  sub_18A9440, the wrapper's vt[4] path resolve) is out of scope and
//  stubbed below -- the resolve stub hands back a null source, so the file
//  handle stays unset (the video never opens, as before).
// ============================================================

// 0x1E2D7B0: the global file-system object's init flag (bit 0 = first use).
static BYTE  g_FileSystemFlags = 0;
// 0x1E2D7AC: the global LHVFS::CWin32FileSystem wrapper object (created
// lazily on first use, released at exit).
static DWORD g_FileSystemObject = 0;
// 0x1E2D7BC: the shape-spawn directory prefix slot (one word; the image
// initial -1 is overwritten to null by the startup init sub_1AA6490, which
// runs InitSharedSlot on it, and the atexit sub_1AD38C0 releases it).
static F3SharedBuffer *g_ShapeSpawnPrefixSlot = 0;
// 0x1BD44D7: the default prefix string the open-setup compares against
// (the image holds the wide "UseShapeSpawnDir"; read as an ANSI string it
// is empty, so an empty slot makes the prefix check a no-op).
static const char g_ShapeSpawnDefaultStr[1] = { 0 };
// 0x1E2D7A8: the legacy-file (CLegacyFile, CLegacyFileOpen) wrapper's init flag
// (bit 0 = first use).  Distinct from 0x1E2D7B0 (the CLegacyMemoryFileOpen
// path's flag); a different handler (ReleaseLegacyFileSystem).
static BYTE  g_LegacyFileSystemFlags = 0;
// 0x1E2D7A4: the legacy-file (CLegacyFile, CLegacyFileOpen) wrapper object (created
// lazily on first use, released at exit).  Distinct from 0x1E2D7AC.
static DWORD g_LegacyFileSystemObject = 0;
// 0x1E2D7C4: the global data-path prefix slot (a pointer to the 8-byte
// {str, len} record the open-setup strncmp-compares against; null = no
// prefix).  The startup writer (sub_17FD600, ResolveDataPathSlot) is not
// yet implemented, so this stays null and the prefix check is a no-op.
static DWORD g_LegacyDataPathRecord = 0;

// sub_1A01BE0 (0x1A01BE0 - 0x1A01C38) -- copy `src`'s block into `out`
// (RefSharedSlot: out is nulled, g_Addend bumped, the block shared with a
// refcount bump), DetachSharedSlot it (a private copy), lowercase the
// payload in place, and return `out`.  `src` is left holding its original
// block untouched.  [NOT byte-identical] placement-dependent (the
// RefSharedSlot / DetachSharedSlot / tolower calls).
static F3SharedBuffer **DetachToLowerSharedSlot(F3SharedBuffer **src,
                                                F3SharedBuffer **out)
{
    RefSharedSlot(out, src);
    DetachSharedSlot(out);
    F3SharedBuffer *b = *out;
    if (b != 0)
    {
        unsigned int i = 0;
        for (i = 0; i < b->length; i++)
            b->data[i] = (char)tolower((int)(unsigned char)b->data[i]);
    }
    return out;
}

// sub_1A01C40 (0x1A01C40 - 0x1A01CBE) -- substring: build a fresh counted
// buffer over [start, end) of the string in the block held at *blockLoc
// (defaulting to the empty default string for an empty location) and swap
// it into the out slot (releasing the slot's old block first).  [NOT
// byte-identical] placement-dependent (the SharedBufferCreate /
// SharedBufferFree calls) plus the Interlocked IAT slots.
static F3SharedBuffer **SubstringSharedSlot(F3SharedBuffer **blockLoc,
                                            F3SharedBuffer **out,
                                            int start, int end)
{
    *out = 0;
    InterlockedIncrement(&g_Addend);
    const char *str = (*blockLoc != 0) ? (*blockLoc)->data : g_ShapeSpawnDefaultStr;
    const char *seg = str + start;
    F3SharedBuffer *created = 0;
    if (seg != 0 && *seg != 0)
        created = SharedBufferCreate(seg, end - start);
    if (*out != 0)
    {
        if (InterlockedDecrement(&(*out)->refcount) <= 0 && *out != 0)
            SharedBufferFree(*out);
        *out = 0;
    }
    if (created != 0)
        *out = created;
    return out;
}

// sub_19FF9E0 (0x19FF9E0 - 0x19FFA14) -- the WIDE shared-slot copy-assign
// (CopySharedSlotWide's sibling minus the up-front null + counter bump):
// when src is a different non-empty slot, drop dst's block (the wide
// release sub_19FF9B0 -- refcount down, free via SharedBufferFree on zero)
// and share src's (copy the block, refcount up).  [NOT byte-identical]
// placement-dependent plus the Interlocked IAT slots.
static F3SharedBuffer **AssignSharedSlotWide(F3SharedBuffer **dst,
                                             F3SharedBuffer **src)
{
    if (src != dst && *src != *dst)
    {
        if (*dst != 0)
        {
            if (InterlockedDecrement(&(*dst)->refcount) <= 0 && *dst != 0)
                SharedBufferFree(*dst);
            *dst = 0;
        }
        if (*src != 0)
        {
            *dst = *src;
            InterlockedIncrement(&(*dst)->refcount);
        }
    }
    return dst;
}

// sub_42D0D0 / sub_6C9520 (0x42D0D0 - 0x42D0FF, 0x6C9520 - 0x6C954F) --
// the byte-identical refcounted pointer swap: ref the source object
// (vtable slot 1), release the old destination object (vtable slot 2),
// store the source, return dst.  One static stands in for both.  [NOT
// byte-identical] placement-dependent.
static void *SharedRefAssign(void **dst, void **src)
{
    void *s = *src;
    if (s != 0)
        ((void (WINAPI *)(void *))F3_VT(s)[1])(s);
    void *d = *dst;
    if (d != 0)
        ((void (WINAPI *)(void *))F3_VT(d)[2])(d);
    *dst = s;
    return dst;
}

// ---- LHVFS::CMemoryFile (76 bytes) -- the in-memory IFile the open-setup
//  builds over an opened file:
//    +0x00 vtable  +0x04 refcount  +0x08 / +0x09 flag bytes
//    +0x0C / +0x14 buffer start/end  +0x1C / +0x20 position words
//    +0x24 IFile / stream member     +0x28 critical section (24 bytes)
//    +0x40 / +0x44 / +0x48 tail words

// [remaining stub] the original CMemoryFile dtor: release the +0x24 member,
// LHFree the buffer, drop the critical section.
static void WINAPI MemoryFileDtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    if (o[9] != 0)
    {
        ((void (WINAPI *)(void *))F3_VT((void *)o[9])[2])((void *)o[9]);
        o[9] = 0;
    }
    if (o[5] != 0)
    {
        LHFree((void *)o[5]);
        o[5] = 0;
        o[3] = 0;
    }
    DeleteCriticalSection((CRITICAL_SECTION *)(o + 10));
}

static void WINAPI MemoryFileAddRef(void *obj)
{
    InterlockedIncrement((volatile LONG *)((char *)obj + 4));
}

static void WINAPI MemoryFileRelease(void *obj)
{
    DWORD *o = (DWORD *)obj;
    if (InterlockedDecrement((volatile LONG *)(o + 1)) == 0)
    {
        MemoryFileDtor(obj);
        LHFree(obj);
    }
}

// the CMemoryFile vtable (original 0x19A9xxx `??_7CMemoryFile@LHVFS@@6B@`):
// dtor / AddRef (vt+4) / Release (vt+8); the IFile read/seek method slots
// are remaining stubs.
static void *g_MemoryFileVtable[8] =
{
    (void *)MemoryFileDtor,      // [0]
    (void *)MemoryFileAddRef,    // [1]
    (void *)MemoryFileRelease,   // [2]
    0, 0, 0, 0, 0                // [remaining stub]
};

// sub_18A92A0 (0x18A92A0 - 0x18A932A) -- the CMemoryFile ctor (the size
// variant): store the CMemoryFile vtable, zero the object, set the flag
// bytes (+0x08 = 1, +0x09 = 0), the position words to the size, initialize
// the critical section at +0x28, store the caller flag at +0x44, and
// LHAlloc(size) the buffer at +0x0C / +0x14.  (The ALLOC-log tail is
// skipped -- the verbose flag dword_20B5FA8 is off.)  [NOT byte-identical]
// placement-dependent (the LHAlloc / InitializeCriticalSection IAT calls).
static void *MemoryFileCtor(void *obj, DWORD size, int flag)
{
    DWORD *o = (DWORD *)obj;
    BYTE  *b = (BYTE *)obj;
    o[0]  = (DWORD)g_MemoryFileVtable;
    o[1]  = 0;
    b[8]  = 1;
    b[9]  = 0;
    o[3]  = 0; o[4] = 0; o[5] = 0; o[6] = 0;
    o[7]  = size; o[8] = size;
    o[9]  = 0;
    InitializeCriticalSection((CRITICAL_SECTION *)(o + 10));
    o[16] = 0; o[17] = (DWORD)flag; o[18] = 0;
    void *buf = LHAlloc(size);
    o[3] = (DWORD)buf;
    o[5] = (DWORD)buf;
    return obj;
}

// sub_18A9330 (0x18A9330 - 0x18A9366) -- create an EMPTY CMemoryFile:
// LHAlloc(76), MemoryFileCtor (size 0, flag 0), store the object in the
// out slot and take a reference on it.  [NOT byte-identical]
// placement-dependent (the LHAlloc / sub_18A92A0 relative calls).
static void *CreateEmptyMemoryFile(void **out)
{
    void *mem = LHAlloc(76);
    void *r = (mem != 0) ? MemoryFileCtor(mem, 0, 0) : 0;
    *out = r;
    if (r != 0)
        MemoryFileAddRef(r);
    return out;
}

// sub_18A9440 (0x18A9440 - 0x18A96E7) -- read the source IFile into the
// CMemoryFile buffer (the deep IFile read / offset / stream chain).
// [remaining stub]
static void *MemoryFileReadFromSource(void *mem, void *src, int flag)
{ (void)mem; (void)src; (void)flag; return 0; }

// sub_18A9770 (0x18A9770 - 0x18A97BB) -- open a CMemoryFile over a source
// file: when src is null, null the out slot; otherwise LHAlloc(76), build
// the CMemoryFile from the source (sub_18A9440, remaining stub), store the
// object in the out slot and take a reference.  [NOT byte-identical]
// placement-dependent (the LHAlloc / sub_18A9440 relative calls).
static void *OpenMemoryFile(void **out, void *src, int flag)
{
    if (src == 0)
    {
        *out = 0;
        return out;
    }
    void *mem = LHAlloc(76);
    void *r = 0;
    if (mem != 0)
        r = MemoryFileReadFromSource(mem, src, flag);
    *out = r;
    if (r != 0)
        MemoryFileAddRef(r);
    return out;
}

// ---- LHVFS::CWin32FileSystem wrapper (48 bytes) -- the global
//  "file-system facade" object the open-setup resolves paths through
//  (created by CreateFileSystemWrapper, stored at g_FileSystemObject).

// Win32FileSystemDtor  (0x18AAC30 - 0x18AAC9A)  -- the LHVFS::
// CWin32FileSystem complete-object dtor body.  It runs from the deleting
// dtor (sub_5AF660, the wrapper vtable's vt[10]) when the refcount drops
// to zero: it releases the three ref-counted members -- the +0x24
// path-description, the +0x28 file-system sub-object, and the +0x2C 32-byte
// CFileManifest -- through each member's own vt[2] (Release), highest
// offset first (the original unwind order); then drops the embedded SSO
// wide-path string's heap buffer (cap at +0x20, data pointer at +0x0C) and
// resets the string fields.  [NOT byte-identical] the member Release
// dispatches and the SSO-buffer free are relative calls, and the free goes
// through the high-VA heap table (dword_2075B24 + 16), so this is a C++
// reimplementation.  The original also swaps the vtable to the base
// CAddRelQueryImp<IFileSystem,...> vtable on exit; the stub frees the object
// right after, so it is never re-dispatched through the old vtable.
static void WINAPI Win32FileSystemDtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    int i;
    // release the +0x24 / +0x28 / +0x2C members via each member's vt[2]
    for (i = 11; i >= 9; i--)
    {
        if (o[i] != 0)
        {
            ((void (WINAPI *)(void *))F3_VT((void *)o[i])[2])((void *)o[i]);
            o[i] = 0;
        }
    }
    // drop the embedded SSO wide-path string's heap buffer, then reset it
    if (o[8] >= 8)
        LHFree((void *)o[3]);
    o[8] = 7;                        // +0x20
    o[7] = 0;                        // +0x1C
    *(WORD *)((char *)obj + 0x0C) = 0;  // +0x0C (low word of the data ptr)
}

static void WINAPI Win32FileSystemAddRef(void *obj)
{
    InterlockedIncrement((volatile LONG *)((char *)obj + 4));
}

static void WINAPI Win32FileSystemRelease(void *obj)
{
    DWORD *o = (DWORD *)obj;
    if (InterlockedDecrement((volatile LONG *)(o + 1)) == 0)
    {
        Win32FileSystemDtor(obj);
        LHFree(obj);
    }
}

// forward decls for the 36-byte wide-string helpers (defined further
// below in this file)
static wchar_t *F3WStringData(F3WString *s);
static void F3WStringReset(F3WString *s);
static void F3WStringFree(F3WString *s);
static void F3WStringClear(F3WString *s);
static void F3WStringAssign(F3WString *s, const wchar_t *src, unsigned int len);
static void F3WStringNormalizeSeparators(F3WString *s);
static void XApiErrorReport(int code, int subCode);

// sub_56DFD0 (0x56DFD0 - 0x56E023) -- the cached FNV wide-string hash
// (GetWideStringHash): now implemented below with the F3WString family,
// declared in gamemain.h (shared with engine/luavm.cpp).

// The VFS verbose-logging pair (the "OPEN: ..." / "CLOSE: ..." printer):
// the flag byte (dword_20B5FA8 & 1) gates the printf-style function at
// dword_20B5FB0 (MSVC printf -- "%S" prints a wide string).
static unsigned char g_VerboseLogFlag = 0;   // 0x20B5FA8
static void (WINAPI *g_VerboseLogFn)(const char *, ...) = 0;  // 0x20B5FB0

// Win32FileSystemGetPath  (0x5AF5E0 - 0x5AF62F)  -- copy the wrapper's
// embedded wide root path (data at +0x0C / SSO, cap at +0x20) into the
// 36-byte out string: reset (cap 7 / len 0 / SSO NUL) then assign.
// [NOT byte-identical] placement-dependent (the F3WStringAssign relative
// call in the original).
static F3WString *WINAPI Win32FileSystemGetPath(void *obj, F3WString *out)
{
    DWORD *o = (DWORD *)obj;
    const wchar_t *src = (o[8] < 8) ? (const wchar_t *)(o + 3)
                                    : (const wchar_t *)o[3];
    F3WStringReset(out);
    F3WStringAssign(out, src, (unsigned int)wcslen(src));
    return out;
}

// ---- LHVFS::CWin32File (72 bytes) -- the disk IFile the open (vt[4])
//  builds over the CreateFileW handles: +0x08 read handle, +0x0C write
//  handle, +0x10/+0x14 block words (0x1000 / -0x1000), +0x1C path
//  description (ref), +0x20 file-system sub-object (ref), +0x30/+0x34
//  file position, +0x38 wide-path pointer, +0x3C mode selector.
//  Win32FileInit  (0x18A3960 - 0x18A39CE)  -- build the 72-byte CWin32File
//  over the caller's LHAlloc'd block: store the CWin32File vtable (the
//  original first stores the base CAddRelQueryImp<IFile,...> vtable, which
//  is immediately overwritten), zero the refcount, store the handles and
//  AddRef the two ref-counted members.  The original reads its +0x0C and
//  +0x3C slots from its own caller's return-address stack slot (a compiler
//  placement artifact of the single-word push at the one call site), so
//  they end up holding that return address (0x18AB679 in the original
//  binary): the write handle is never a real handle (Write / Seek fail) and
//  the +0x3C mode selector is always >= 2 (Read takes the synchronous
//  path).  Reproduced with the caller return address here.  [NOT
//  byte-identical] placement-dependent (the member AddRef dispatches).
static __declspec(naked) DWORD * Win32FileCallerReturnAddress(void)
{
    __asm
    {
        mov eax, [esp]
        ret
    }
}

//  Win32FileDtor  (0x18A38F0)  -- the CWin32File dtor body (run
//  by the deleting dtor when the refcount drops to zero): CloseHandle the
//  +0x08 read handle, close the +0x0C write handle when it is a distinct
//  handle (not -1), the verbose "CLOSE: ..." line, then Release the +0x20
//  and +0x1C members.  [NOT byte-identical] the member Release dispatches
//  are relative calls.
static void WINAPI Win32FileDtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    CloseHandle((HANDLE)o[2]);
    if (o[3] != 0xFFFFFFFF && o[3] != o[2])
        CloseHandle((HANDLE)o[3]);
    if ((g_VerboseLogFlag & 1) != 0 && g_VerboseLogFn != 0)
        g_VerboseLogFn("CLOSE: #%08x #%08x", o[14], (DWORD)obj);
    if (o[8] != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)o[8])[2])((void *)o[8]);
    if (o[7] != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)o[7])[2])((void *)o[7]);
    // the original swaps to the base CAddRelQueryImp<IFile,...> vtable
    // here; the stub release frees the object right after, so it is never
    // re-dispatched through the old vtable.
}

//  Win32FileDeletingDtor  (0x5C1550)  -- the CWin32File deleting dtor
//  (the wrapper vtable's vt[11], run by Win32FileRelease at refcount 0).
static void WINAPI Win32FileDeletingDtor(void *obj)
{
    Win32FileDtor(obj);
    LHFree(obj);
}

//  sub_5B5700 (0x5B5700) -- CWin32File AddRef (vt[1]).
static LONG WINAPI Win32FileAddRef(void *obj)
{
    return InterlockedIncrement((volatile LONG *)((DWORD *)obj + 1));
}

//  sub_5B5740 (0x5B5740) -- CWin32File Release (vt[2]): decrement the
//  refcount and run the deleting dtor (vt[11]) at zero.
static LONG WINAPI Win32FileRelease(void *obj)
{
    LONG n = InterlockedDecrement((volatile LONG *)((DWORD *)obj + 1));
    if (n == 0 && obj != 0)
        ((void (WINAPI *)(void *, int))F3_VT(obj)[11])(obj, 1);
    return n;
}

// the CWin32File vtable (original `??_7CWin32File@LHVFS@@6B@`, 13 slots:
// the base vtable's QueryInterface / AddRef / Release, the 8 IFile methods
// (sub_18A37C0 / sub_18A39D0 / sub_18A3BA0 / sub_18A3D30 / sub_18A3730 /
// sub_18A36C0 / sub_18A37A0 / sub_18A36B0 -- remaining stubs), the
// deleting dtor (sub_5C1550), and the IFile base-class descriptor).
static void *g_Win32FileVtable[13] =
{
    0,                          // [0] sub_5B57A0 (QueryInterface -- stub)
    (void *)Win32FileAddRef,    // [1] sub_5B5700
    (void *)Win32FileRelease,   // [2] sub_5B5740
    0, 0, 0, 0, 0, 0, 0, 0,     // [3..10] the IFile methods (remaining stubs)
    (void *)Win32FileDeletingDtor,  // [11] sub_5C1550
    0                           // [12] the IFile base-class descriptor
};

static void *Win32FileInit(void *obj, DWORD h, DWORD h2, DWORD pathDesc,
                           DWORD fsSub, DWORD relPath)
{
    (void)h2;  // the original pushes the write handle, but the ctor reads its
               // own caller's return address into +0x0C instead -- see below
    DWORD *o = (DWORD *)obj;
    // The original's +0x0C and +0x3C slots both end up holding the ctor
    // caller's return address (0x18AB679 in the original binary): a compiler
    // placement artifact of the single-word push at the one call site.  The
    // write handle is therefore never a real handle (Write / Seek fail) and
    // the +0x3C mode selector is always >= 2 (Read takes the synchronous
    // path).  Capture the caller return address once and store it in both.
    DWORD ra = (DWORD)Win32FileCallerReturnAddress();
    o[0]  = (DWORD)g_Win32FileVtable;   // (base-vtable store folded in)
    o[1]  = 0;                          // refcount
    o[2]  = h;                          // +0x08 read handle
    o[3]  = ra;                         // +0x0C (caller retaddr, NOT the handle)
    o[4]  = 0x1000;                     // +0x10
    o[5]  = 0xFFFFF000;                 // +0x14
    o[6]  = 0;                          // +0x18
    o[7]  = pathDesc;                   // +0x1C (the +0x24 path description)
    if (pathDesc != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)pathDesc)[1])((void *)pathDesc);
    o[8]  = fsSub;                      // +0x20 (the +0x28 sub-object)
    if (fsSub != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)fsSub)[1])((void *)fsSub);
    o[9]  = 0; o[10] = 0; o[11] = 0;    // +0x24..0x2F
    o[12] = 0; o[13] = 0;               // +0x30..0x37 file position
    o[14] = relPath;                    // +0x38 wide-path pointer
    o[15] = ra;                         // +0x3C (caller retaddr, NOT the mode)
    ((BYTE *)obj)[0x40] = 0;            // +0x40 (LHAlloc zeroes the rest)
    return obj;
}

//  Win32FileSystemReportOpenFailure  (0x18AB350 - 0x18AB3BB)  -- the
//  CreateFileW-failure report (the open's else branch): classify
//  GetLastError (2/3/5 = access family -> 2; 15/18/21/32/38/161 -> 0;
//  anything else -> 1) and, when the wrapper's +0x28 file-system
//  sub-object is live, dispatch its vt[6] (sub, category, path hash)
//  which returns the status; otherwise XApiErrorReport the code and
//  report success iff the category was 2.  stdcall, 2 args (retn 8).
static int WINAPI Win32FileSystemReportOpenFailure(void *obj,
                                                   const F3WString *path)
{
    DWORD err = GetLastError();
    int hash = GetWideStringHash((F3WString *)path);
    int category;
    switch (err)
    {
    case 2: case 3: case 5:
        category = 2;
        break;
    case 15: case 18: case 21: case 32: case 38: case 161:
        category = 0;
        break;
    default:
        category = 1;
        break;
    }
    DWORD *o = (DWORD *)obj;
    if (o[10] != 0)
        return ((int (WINAPI *)(void *, int, int))F3_VT((void *)o[10])[6])
                   ((void *)o[10], category, hash);
    XApiErrorReport(err, 0);
    return (category == 2);
}

//  Win32FileSystemOpen  (0x18AB470 - 0x18AB768)  -- the CWin32FileSystem
//  vtable slot 4 (vt+16): the wide-path resolve / open.  It builds the
//  normalized wide path (sub_5AF180) into a local 36-byte string, then the
//  disk path: a relative path containing a ':' is used as is, otherwise
//  the wrapper's embedded root path is prefixed.  The path is converted to
//  ANSI and CreateFileW'd with up to 10 attempts (Sleep backoff between
//  failures), the mode selecting the access / disposition / flag combo
//  (0 = read-write OPEN_ALWAYS, 1 = read, 2 = read-write, 3 = read no-
//  buffering-light, 4 = read no-buffering, 5 = read-write CREATE_ALWAYS,
//  anything else = no open).  On success it LHAlloc(72) a CWin32File
//  (Win32FileInit), AddRefs it, Releases the out slot's previous object,
//  prints the verbose "OPEN: ... DISK" line, and stores it in the out
//  slot; on failure it reports through the +0x28 sub-object (or
//  XApiErrorReport) and leaves the out slot null.  Returns the out slot.
//  The original is thiscall + retn 0Ch (the wrapper in ECX, the out slot /
//  path / mode on the stack); here a plain free function per the project
//  convention.  [NOT byte-identical] placement-dependent (the CreateFileW
//  / Sleep / LHAlloc IAT + E8 calls) plus the high-VA verbose globals
//  (dword_20B5FA8 / dword_20B5FB0) and the 0x18AB679 return-address
//  artifact the CWin32File ctor bakes in.
static void *WINAPI Win32FileSystemOpen(void *obj, void **out,
                                        const F3WString *relPath, int mode)
{
    DWORD *o = (DWORD *)obj;

    // sub_5AF180: build the normalized wide string (reset, assign, zero
    // the +28/+32 words, the '/' -> '\\' separator pass).
    F3WString str;
    const wchar_t *src = F3WStringData((F3WString *)relPath);
    F3WStringReset(&str);
    F3WStringAssign(&str, src, (unsigned int)wcslen(src));
    F3WStringNormalizeSeparators(&str);

    // compose the disk path (0x800 wchars) from the root + relative path
    wchar_t destination[0x800];
    if (src[wcscspn(src, L":")] != 0)
        wcsncpy_s(destination, 0x800, src, 0x800);
    else
    {
        const wchar_t *root = (o[8] < 8) ? (const wchar_t *)(o + 3)
                                         : (const wchar_t *)o[3];
        wcsncpy_s(destination, 0x800, root, 0x800);
        wcscat_s(destination, 0x800, src);
    }
    // the original converts the path to ANSI here (a dead store in this
    // build -- the open uses the wide path) and is reproduced for fidelity
    char dst[0x800];
    wcstombs_s(0, dst, 0x800, destination, (size_t)-1);

    // the 10-attempt CreateFileW retry loop with the Sleep backoff (the
    // wide path is what the original opens)
    DWORD h = 0xFFFFFFFF, h2 = 0xFFFFFFFF;
    int retry = 10;
    for (;;)
    {
        --retry;
        switch (mode)
        {
        case 1:
            h = (DWORD)CreateFileW(destination, 0x80000000, 3, 0, 3,
                                   0x48000080, 0);
            break;
        case 4:
            h = (DWORD)CreateFileW(destination, 0x80000000, 3, 0, 3,
                                   0x60000080, 0);
            break;
        case 3:
            h = (DWORD)CreateFileW(destination, 0x80000000, 3, 0, 3,
                                   0x00800080, 0);
            break;
        case 0:
            h = h2 = (DWORD)CreateFileW(destination, 0xC0000000, 3, 0, 2,
                                        0x48000080, 0);
            break;
        case 2:
            h = h2 = (DWORD)CreateFileW(destination, 0xC0000000, 3, 0, 3,
                                        0x00800080, 0);
            break;
        case 5:
            h = h2 = (DWORD)CreateFileW(destination, 0xC0000000, 3, 0, 4,
                                        0x08000000, 0);
            break;
        default:
            break;
        }
        if (h != 0xFFFFFFFF)
            break;
        Sleep((retry >> 1) + 1);
        if (retry == 0)
            break;
    }

    *out = 0;
    void *file = 0;
    if (h != 0xFFFFFFFF)
    {
        void *mem = LHAlloc(72);
        if (mem != 0)
        {
            file = Win32FileInit(mem, h, h2, o[9], o[10], (DWORD)relPath);
            if (file != 0)
                ((void (WINAPI *)(void *))F3_VT(file)[1])(file);   // AddRef
        }
        if (*out != 0)
            ((void (WINAPI *)(void *))F3_VT(*out)[2])(*out);       // Release
        if ((g_VerboseLogFlag & 1) != 0 && g_VerboseLogFn != 0)
        {
            F3WString tmp;
            Win32FileSystemGetPath(obj, &tmp);
            g_VerboseLogFn("OPEN: #%08x \"%S\" #%08x DISK",
                           GetWideStringHash(&str),
                           (const char *)F3WStringData(&tmp), (DWORD)file);
            F3WStringClear(&tmp);
        }
        *out = file;
    }
    else
    {
        Win32FileSystemReportOpenFailure(obj, (const F3WString *)relPath);
    }

    F3WStringFree(&str);
    return out;
}

//  Win32FileSystemDeletingDtor  (0x5AF660)  -- the CWin32FileSystem
//  deleting dtor (the wrapper vtable's vt[10]): the dtor body plus the
//  heap free.
static void *WINAPI Win32FileSystemDeletingDtor(void *obj)
{
    Win32FileSystemDtor(obj);
    LHFree(obj);
    return obj;
}

// the wrapper vtable (original `??_7CWin32FileSystem@LHVFS@@6B@`, 11
// slots: the base vtable's QueryInterface / AddRef / Release, the vt[3]
// hash resolve (sub_18AACA0 -- a stub here), the vt[4] open, the vt[5..7]
// methods (sub_18AB770 / sub_18AB010 / sub_18AAB50 -- remaining stubs),
// the vt[8] root-path copy, the vt[9] method (sub_18AAB60 -- a stub), and
// the vt[10] deleting dtor).
static void *g_Win32FileSystemVtable[11] =
{
    (void *)Win32FileSystemDtor,     // [0] sub_5AFC10 (base dtor shape)
    (void *)Win32FileSystemAddRef,   // [1] sub_5AFB70 (AddRef)
    (void *)Win32FileSystemRelease,  // [2] sub_5AFBB0 (Release)
    0,                               // [3] sub_18AACA0 (remaining stub)
    (void *)Win32FileSystemOpen,     // [4] sub_18AB470 (vt+16, the open)
    0, 0, 0,                         // [5..7] remaining stubs
    (void *)Win32FileSystemGetPath,  // [8] sub_5AF5E0 (root-path copy)
    0,                               // [9] sub_18AAB60 (remaining stub)
    (void *)Win32FileSystemDeletingDtor  // [10] sub_5AF660
};

// sub_1AD3900 (0x1AD3900 - 0x1AD3912) -- the atexit release of the global
// file-system object.
static void ReleaseGlobalFileSystem(void)
{
    void *o = (void *)g_FileSystemObject;
    if (o != 0)
        ((void (WINAPI *)(void *))F3_VT(o)[2])(o);
}

// ReleaseLegacyFileSystem (0x1AD38E0 - 0x1AD38F2) -- the atexit release of the global
// legacy-file system object (the CLegacyFile open-setup's own wrapper,
// distinct from the CLegacyMemoryFileOpen path's ReleaseGlobalFileSystem).
static void ReleaseLegacyFileSystem(void)
{
    void *o = (void *)g_LegacyFileSystemObject;
    if (o != 0)
        ((void (WINAPI *)(void *))F3_VT(o)[2])(o);
}

// sub_17FDDF0 (0x17FDDF0 - 0x17FDE20) -- the global file-system slot
// getter with lazy init: first use sets the flag byte, nulls the object,
// and registers ReleaseGlobalFileSystem at exit; returns
// &g_FileSystemObject.  [NOT byte-identical] placement-dependent (the
// atexit call) plus the original 0x1E2D7xx high-VA globals.
static DWORD *GetGlobalFileSystemSlot(void)
{
    if ((g_FileSystemFlags & 1) == 0)
    {
        g_FileSystemFlags |= 1;
        g_FileSystemObject = 0;
        atexit(ReleaseGlobalFileSystem);
    }
    return &g_FileSystemObject;
}

// sub_18ABA60 (0x18ABA60 - 0x18ABB45) -- build the 48-byte
// CWin32FileSystem wrapper: LHAlloc(48), reference the two singletons
// (sub_18C4270 -> the dword_212304C path description; sub_18A4670 -> the
// +0x20 sub-object of the 304-byte CFileSystem at dword_20B5FB4), build
// the wrapper around them (sub_18AB850), store it in the out slot, and
// take a reference.  [remaining stub] the singleton getters / path resolve
// / CFileManifest are out of scope, so the stub builds a zero-member
// wrapper (the vtable + refcount only).
static void *CreateFileSystemWrapper(void **out)
{
    void *w = LHAlloc(48);
    void *r = 0;
    if (w != 0)
    {
        DWORD *o = (DWORD *)w;
        o[0] = (DWORD)g_Win32FileSystemVtable;
        o[1] = 0;
        int i = 2;
        for (i = 2; i < 12; i++)
            o[i] = 0;
        r = w;
    }
    *out = r;
    if (r != 0)
        Win32FileSystemAddRef(r);
    return out;
}

// forward decls (defined further below in this file)
static void F3WStringReset(F3WString *s);
static void F3WStringClear(F3WString *s);

// the CLegacyMemoryFile vtable (original 0x1BC7FBC, 12 slots): the dtor
// (sub_18004B0), the open-setup (sub_17FE290), and the IFile method slots
// (sub_17FDC60 / sub_17FDC80 / sub_17FDCD0 / sub_17FDD20 / sub_17FDD30 /
// sub_17FD6F0 / sub_17FDD50 / sub_17FDD60 / sub_17FD700 -- remaining
// stubs; the stub build never dispatches through them).
static int LegacyMemoryFileStubMethod(void *obj) { (void)obj; return 0; }
static void *CLegacyMemoryFileDtorAddress(void);
static int CLegacyMemoryFileOpen(void *obj, F3SharedBuffer **slot, int mode, int a4);

static void *g_CLegacyMemoryFileVtable[12] =
{
    CLegacyMemoryFileDtorAddress(),      // [0] sub_18004B0
    (void *)CLegacyMemoryFileOpen,       // [1] sub_17FE290
    (void *)LegacyMemoryFileStubMethod,  // [2] sub_17FDC60
    (void *)LegacyMemoryFileStubMethod,  // [3] sub_17FDC80
    (void *)LegacyMemoryFileStubMethod,  // [4] sub_17FDCD0
    (void *)LegacyMemoryFileStubMethod,  // [5] sub_17FDD20
    (void *)LegacyMemoryFileStubMethod,  // [6] sub_17FDD30
    (void *)LegacyMemoryFileStubMethod,  // [7] sub_17FDD40
    (void *)LegacyMemoryFileStubMethod,  // [8] sub_17FD6F0 (nullsub)
    (void *)LegacyMemoryFileStubMethod,  // [9] sub_17FDD50
    (void *)LegacyMemoryFileStubMethod,  // [10] sub_17FDD60
    (void *)LegacyMemoryFileStubMethod   // [11] sub_17FD700
};

// The 16-byte LHSFS::CLegacyMemoryFile object.  The dtor is a real member
// function so its vtable slot keeps the original thiscall + retn 4
// dispatch ABI (ReleaseVideoFile calls vt[0](obj, 1) through the vtable).
struct CLegacyMemoryFile
{
    const void *    vtable;   // +0x00
    F3SharedBuffer *path;     // +0x04 the wide path slot
    DWORD           file;     // +0x08 the open file handle (an LHVFS IFile)
    DWORD           extra;    // +0x0C (zeroed by the open-setup)

    // sub_18004B0 (0x18004B0 - 0x1800502) -- the deleting dtor (thiscall +
    // retn 4, the free-flag on the stack): re-store the CLegacyMemoryFile
    // vtable, release the file handle (vtable slot 2) and null it, release
    // the wide path slot (ReleaseSharedSlotWide), swap the vtable to
    // LHFile::CAFile, and LHFree the object when flag bit 0 is set.
    int Dtor(int freeFlag)
    {
        DWORD *o = (DWORD *)this;
        o[0] = (DWORD)g_CLegacyMemoryFileVtable;
        if (o[2] != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)o[2])[2])((void *)o[2]);
        o[2] = 0;
        ReleaseSharedSlotWide((F3SharedBuffer **)(o + 1));
        o[0] = (DWORD)g_CAFileVtable;
        if (freeFlag & 1)
            LHFree(this);
        return (int)(DWORD)this;
    }
};

// sub_17FE290 (0x17FE290 - 0x17FE46B) -- the open-setup (vtable slot 1):
// take the caller's path-slot block into a private lowercased block (the
// original aliases its a2 parameter location; modelled with a local),
// mirror it into the
// object's +4 wide path slot, and -- when the path starts with the global
// shape-spawn prefix (an empty slot -> empty prefix -> always matches) --
// rebuild it as the prefix-stripped substring.  The mode selects: 1/2 =
// ensure the global CWin32FileSystem wrapper exists, convert the path to
// the wide string, resolve it through the wrapper's vtable slot 4 (which
// writes the source IFile back into the mode argument), and when a source
// came back, open it as a CMemoryFile into +8; 3/4 = swap a fresh EMPTY
// CMemoryFile into +8; otherwise do nothing.  +0xC is zeroed and the path
// block released in all cases.  The original is thiscall + retn 0Ch (the
// object in ECX, the three args on the stack, callee-cleans); here a plain
// free function per the project convention.  [NOT byte-identical]
// placement-dependent (the E8 calls) plus the high-VA globals
// (g_ShapeSpawnPrefixSlot 0x1E2D7BC, the 0x1BD44D7 default string, and the
// Interlocked IAT slots).
static int CLegacyMemoryFileOpen(void *obj, F3SharedBuffer **slot, int mode, int a4)
{
    (void)a4;
    DWORD *o = (DWORD *)obj;

    // The original aliases its own a2 parameter location here: the slot's
    // block is copied into the (a2) parameter location as a private
    // lowercased block while the caller's slot keeps its original block.
    // Modelled with a local slot location.
    F3SharedBuffer *privateBlock = 0;
    DetachToLowerSharedSlot(slot, &privateBlock);
    F3SharedBuffer **blockLoc = &privateBlock;

    F3SharedBuffer *wide = 0;
    SharedSlotToWide(blockLoc, &wide);
    AssignSharedSlotWide((F3SharedBuffer **)(o + 1), &wide);
    ReleaseSharedSlotWide(&wide);

    // The prefix check: compare the path against the global prefix
    // (strncmp over the prefix length; empty slot -> empty prefix).
    DWORD prefixLen = (g_ShapeSpawnPrefixSlot != 0) ? g_ShapeSpawnPrefixSlot->length : 0;
    const char *prefixStr = (g_ShapeSpawnPrefixSlot != 0) ? g_ShapeSpawnPrefixSlot->data : g_ShapeSpawnDefaultStr;
    const char *pathStr = (*blockLoc != 0) ? (*blockLoc)->data : g_ShapeSpawnDefaultStr;
    int prefixMatch = (strncmp(pathStr, prefixStr, prefixLen) == 0);
    if (prefixMatch)
    {
        DWORD pathLen = (*blockLoc != 0) ? (*blockLoc)->length : 0;
        F3SharedBuffer *sub = 0;
        SubstringSharedSlot(blockLoc, &sub, (int)prefixLen, (int)pathLen);
        CopySharedSlot(blockLoc, &sub);
        ReleaseSharedSlot(&sub);
    }

    // The original re-reads the a2 location for the wide-string build (the
    // prefix branch swapped the block, freeing the pre-branch string).
    pathStr = (*blockLoc != 0) ? (*blockLoc)->data : g_ShapeSpawnDefaultStr;

    switch (mode)
    {
    case 1:
    case 2:
    {
        DWORD *fsSlot = GetGlobalFileSystemSlot();
        if (*fsSlot == 0)
        {
            DWORD wrapper = 0;
            CreateFileSystemWrapper((void **)&wrapper);
            SharedRefAssign((void **)fsSlot, (void **)&wrapper);
            if (wrapper != 0)
                ((void (WINAPI *)(void *))F3_VT((void *)wrapper)[2])((void *)wrapper);
        }
        F3WString wpath;
        F3WStringReset(&wpath);
        F3WStringFromAnsi(&wpath, pathStr);
        // The original writes the resolved source IFile back into the mode
        // argument location; modelled with a local.
        void *src = 0;
        if (*fsSlot != 0)
            // the original dispatches the wrapper's vt[4] as a virtual
            // (thiscall) method: the wrapper in ECX, the out slot / path /
            // mode (1 = read) on the stack.
            ((void (WINAPI *)(void *, void **, F3WString *, int))
                F3_VT((void *)*fsSlot)[4])((void *)*fsSlot, &src, &wpath, 1);
        F3WStringClear(&wpath);
        if (src != 0)
        {
            DWORD handle = 0;
            OpenMemoryFile((void **)&handle, src, 1);
            SharedRefAssign((void **)(o + 2), (void **)&handle);
            if (handle != 0)
                ((void (WINAPI *)(void *))F3_VT((void *)handle)[2])((void *)handle);
        }
        break;
    }
    case 3:
    case 4:
    {
        DWORD memFile = 0;
        CreateEmptyMemoryFile((void **)&memFile);
        if (memFile != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)memFile)[1])((void *)memFile);
        if (o[2] != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)o[2])[2])((void *)o[2]);
        o[2] = memFile;
        if (memFile != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)memFile)[2])((void *)memFile);
        break;
    }
    default:
        break;
    }

    o[3] = 0;
    return (int)(DWORD)ReleaseSharedSlot(blockLoc);
}

// the dtor's code address (the member-function-pointer -> void* bit copy;
// a non-virtual member function pointer is a plain code address)
static void *CLegacyMemoryFileDtorAddress(void)
{
    union
    {
        int (CLegacyMemoryFile::*pm)(int);
        void *p;
    } u;
    u.pm = &CLegacyMemoryFile::Dtor;
    return u.p;
}

// sub_17FE8B0 (0x17FE8B0 - 0x17FE8E4) -- the LHSFS::CLegacyMemoryFile ctor
// over the 16-byte object: store the CLegacyMemoryFile vtable, run the
// wide null-ctor on the +4 path slot (InitSharedSlotWide, sub_19FF580),
// zero the +8 file handle, then run the open-setup (CLegacyMemoryFileOpen,
// sub_17FE290) over (path slot, mode, flag) and return the object.  The
// original is thiscall + retn 0Ch (the object in ECX, the three args on
// the stack, callee-cleans); here a plain free function per the project
// convention.  [NOT byte-identical] placement-dependent (the E8 calls to
// the wide null-ctor and the open-setup) plus the CLegacyMemoryFile
// vtable ref (original 0x1BC7FBC).
static void *CLegacyMemoryFileCtor(void *obj, F3SharedBuffer **slot, int mode, int a4)
{
    DWORD *o = (DWORD *)obj;
    o[0] = (DWORD)g_CLegacyMemoryFileVtable;
    InitSharedSlotWide((F3SharedBuffer **)(o + 1));
    o[2] = 0;
    CLegacyMemoryFileOpen(obj, slot, mode, a4);
    return obj;
}

// CLegacyFileOpen (0x17FE0A0 - 0x17FE271) -- the LHSFS::CLegacyFile open-setup:
// take the caller's path-slot
// block into a private lowercased block (the original aliases its a2
// parameter location; modelled with a local), mirror it into the object's
// +4 path slot, and -- when the path starts with the global data-path
// prefix (an empty/null record -> empty prefix -> always matches) --
// rebuild it as the prefix-stripped substring.  The mode selects the
// vt[4] open mode (1 -> the caller's flag decides 4 or 1; 2/3 -> 5; 4 ->
// 0; anything else -> the slot value).  Then the one-time atexit registration
// (ReleaseLegacyFileSystem) and the lazy 48-byte file-system wrapper, resolves the path through the wrapper's vt[4] (which writes the
// resolved source IFile into the out slot), stores the resolved source in
// +8, and releases the path block.  [NOT byte-identical] placement-
// dependent (E8 calls) plus the original 0x1E2D7xx high-VA globals and
// the Interlocked IAT slots.
static int CLegacyFileOpen(void *obj, F3SharedBuffer **slot, int mode, int a4)
{
    DWORD *o = (DWORD *)obj;

    // the original aliases its own a2 parameter location: the slot's block
    // is copied into the (a2) parameter location as a private lowercased
    // block while the caller's slot keeps its original block.  Modelled
    // with a local slot location.
    F3SharedBuffer *privateBlock = 0;
    DetachToLowerSharedSlot(slot, &privateBlock);
    F3SharedBuffer **blockLoc = &privateBlock;

    CopySharedSlot((F3SharedBuffer **)(o + 1), blockLoc);

    // the prefix check: strncmp over the prefix length (empty record ->
    // empty prefix) against the (lowercased) path.
    const char *prefixStr = g_ShapeSpawnDefaultStr;
    DWORD prefixLen = 0;
    if (g_LegacyDataPathRecord != 0)
    {
        DWORD *rec = (DWORD *)g_LegacyDataPathRecord;
        prefixStr = (const char *)rec[0];
        prefixLen = (DWORD)rec[1];
        if (prefixStr == 0)
            prefixStr = g_ShapeSpawnDefaultStr;
    }
    const char *pathStr = (*blockLoc != 0) ? (*blockLoc)->data : g_ShapeSpawnDefaultStr;
    if (strncmp(pathStr, prefixStr, prefixLen) == 0)
    {
        DWORD pathLen = (*blockLoc != 0) ? (*blockLoc)->length : 0;
        F3SharedBuffer *sub = 0;
        SubstringSharedSlot(blockLoc, &sub, (int)prefixLen, (int)pathLen);
        CopySharedSlot(blockLoc, &sub);
        ReleaseSharedSlot(&sub);
    }

    // the mode value: 1 -> (a4 & 8) ? 4 : 1; 2/3 -> 5; 4 -> 0; default ->
    // the slot value (the private block pointer).
    int v11;
    switch (mode)
    {
    case 1:
        v11 = (a4 & 8) ? 4 : 1;
        break;
    case 2:
    case 3:
        v11 = 5;
        break;
    case 4:
        v11 = 0;
        break;
    default:
        v11 = (int)*blockLoc;
        break;
    }

    // one-time atexit registration (the flag + the handler).
    if ((g_LegacyFileSystemFlags & 1) == 0)
    {
        g_LegacyFileSystemFlags |= 1;
        g_LegacyFileSystemObject = 0;
        atexit(ReleaseLegacyFileSystem);
    }

    // the lazy 48-byte wrapper (first call only): add a ref, release the
    // old global (dead in the first-call path), store the new wrapper,
    // and release the slot-held reference.
    if (g_LegacyFileSystemObject == 0)
    {
        DWORD wrapper = 0;
        CreateFileSystemWrapper((void **)&wrapper);
        if (wrapper != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)wrapper)[1])((void *)wrapper);
        if (g_LegacyFileSystemObject != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)g_LegacyFileSystemObject)[2])((void *)g_LegacyFileSystemObject);
        g_LegacyFileSystemObject = wrapper;
        if (wrapper != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)wrapper)[2])((void *)wrapper);
    }

    // the wide string build (re-read the block -- the prefix branch may
    // have swapped it), then the wrapper's vt[4] resolve: the resolved
    // source IFile is written into the out slot.
    pathStr = (*blockLoc != 0) ? (*blockLoc)->data : g_ShapeSpawnDefaultStr;
    F3WString wstr;
    F3WStringReset(&wstr);
    F3WStringFromAnsi(&wstr, pathStr);
    void *source = 0;
    if (g_LegacyFileSystemObject != 0)
        ((void (WINAPI *)(void *, void **, F3WString *, int))
            F3_VT((void *)g_LegacyFileSystemObject)[4])((void *)g_LegacyFileSystemObject, &source, &wstr, v11);
    F3WStringFree(&wstr);

    if (source != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)source)[1])((void *)source);   // AddRef
    if (o[2] != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)o[2])[2])((void *)o[2]);       // release old +8
    o[2] = (DWORD)source;
    if (source != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)source)[2])((void *)source);   // release the out-slot copy

    return (int)(DWORD)ReleaseSharedSlot(blockLoc);
}

// ============================================================
//  The +0x30 video vector  -- a 24-byte object holding an array of 8-byte
//  {obj, refblock} pairs (the mounted videos).  Layout (dword offsets):
//    +0x00  buffer   a 4-byte self-referencing wrapper; buffer[0] == &vector
//    +0x0C  begin    first element (storage base)
//    +0x10  current  one past the last element
//    +0x14  cap_end  one past the end of the allocation
//
//  Each refblock is an F3SehBlock {refcount, dtor, obj}; releasing it
//  decrements the refcount and, at zero, runs dtor(obj) then LHFree's the
//  block.  CGameEngineCtor sets buffer to the self-ref wrapper and zeroes
//  begin / current / cap_end.  The original methods are thiscall + `retn N`
//  (the vector in ECX, the rest on the stack, callee-cleans); here they are
//  plain free functions with the vector as the first argument, so they are
//  [NOT byte-identical] but functionally equivalent.
// ============================================================

// sub_620120 (thiscall) -- destruct one 8-byte {obj, refblock} pair: release
// the refblock (zero -> dtor + LHFree), then zero both words.
static void *SehVecDestructElem(void *elem)
{
    DWORD *t = (DWORD *)elem;
    DWORD rb = t[1];
    if (rb != 0 && InterlockedDecrement((volatile LONG *)rb) == 0)
    {
        void **b = (void **)rb;
        ((void (*)(void *))b[1])(b[2]);
        LHFree((void *)rb);
    }
    t[0] = 0;
    t[1] = 0;
    return 0;
}

// sub_61E6A0 (thiscall) -- copy-assign the 8-byte pair `this = a2`, taking a
// refblock reference (the shared-pair copy-assign).
static void *SehVecCopyElem(void *elem, const void *a2)
{
    DWORD *t = (DWORD *)elem;
    t[0] = *(DWORD *)a2;
    t[1] = *(DWORD *)((char *)a2 + 4);
    if (t[1] != 0)
        InterlockedIncrement((volatile LONG *)t[1]);
    return elem;
}

// sub_6223A0 (cdecl) -- copy the elements in [srcBegin, srcEnd) to dstBegin,
// taking a refblock reference on each.  Returns the new dst end.
static void *SehVecCopy(void *srcBegin, void *srcEnd, void *dstBegin)
{
    char *s = (char *)srcBegin, *e = (char *)srcEnd, *d = (char *)dstBegin;
    if (s == e)
        return dstBegin;
    do
    {
        if (d != 0)
        {
            *(void **)d       = *(void **)s;
            void *rb          = *(void **)(s + 4);
            *(void **)(d + 4) = rb;
            if (rb != 0)
                InterlockedIncrement((volatile LONG *)rb);
        }
        s += 8;
        d += 8;
    } while (s != e);
    return d;
}

// sub_622BB0 (cdecl) -- fill count 8-byte slots at dstBegin with the value
// pair, taking a refblock reference on each.
static void SehVecFill(void *dstBegin, int count, const void *value)
{
    char   *d      = (char *)dstBegin;
    void   *vobj   = *(void **)value;
    void   *vrb    = *(void **)((char *)value + 4);
    for (int i = 0; i < count; ++i)
    {
        if (d != 0)
        {
            *(void **)d       = vobj;
            *(void **)(d + 4) = vrb;
            if (vrb != 0)
                InterlockedIncrement((volatile LONG *)vrb);
        }
        d += 8;
    }
}

// sub_623320 (stdcall) -- fill count slots, return the new dst end.
static void * __stdcall SehVecFillRet(void *dstBegin, int count, const void *value)
{
    SehVecFill(dstBegin, count, value);
    return (char *)dstBegin + 8 * count;
}

// sub_624910 (stdcall) -- copy [srcBegin, srcEnd) to dstBegin, return the new dst end.
static void * __stdcall SehVecCopyRet(void *srcBegin, void *srcEnd, void *dstBegin)
{
    return SehVecCopy(srcBegin, srcEnd, dstBegin);
}

// sub_621970 (cdecl) -- assign the value pair over the [a1, a2) slots, first
// releasing each old refblock that differs from the new one.
static void SehVecAssignRange(void *a1, void *a2, const void *value)
{
    char  *s       = (char *)a1, *e = (char *)a2;
    void  *newObj  = *(void **)value;
    void  *newRb   = *(void **)((char *)value + 4);
    while (s != e)
    {
        void *oldRb = *(void **)(s + 4);
        if (oldRb != newRb)
        {
            if (oldRb != 0 && InterlockedDecrement((volatile LONG *)oldRb) == 0)
            {
                void **b = (void **)oldRb;
                ((void (*)(void *))b[1])(b[2]);
                LHFree(oldRb);
            }
            *(void **)s       = newObj;
            *(void **)(s + 4) = newRb;
            if (newRb != 0)
                InterlockedIncrement((volatile LONG *)newRb);
        }
        s += 8;
    }
}

// sub_61F580 (cdecl) -- allocate 8 * count bytes; throws stdext::bad_alloc when
// the size overflows (0xFFFFFFFF / count < 8).  That path is unreachable here,
// so the throw is emulated with a plain C++ throw.
static void *SehVecAlloc(unsigned int count)
{
    if (count != 0 && 0xFFFFFFFFu / count < 8u)
        throw 1;   // the original throws stdext::bad_alloc
    return LHAlloc(8 * count);
}

// sub_624480 (thiscall) -- throw std::length_error("vector<T> too long").
// Unreachable for a video vector this size; emulated with a plain C++ throw.
static void SehVecThrowLengthError()
{
    throw 1;   // the original throws std::length_error("vector<T> too long")
}

// sub_624D60 (thiscall + retn 0x10) -- insert `count` copies of the `value`
// 8-byte pair at byte position `position`, growing the allocation when needed.
//   obj       = the 24-byte video vector
//   position  = byte offset of the insertion point (== current for push_back)
//   count     = number of elements to insert
//   value     = pointer to the 8-byte {obj, refblock} pair to insert
static void *SehVecInsert(void *obj, void *position, int count, void *value)
{
    DWORD *v       = (DWORD *)obj;
    DWORD  begin   = v[3];
    DWORD  current = v[4];
    DWORD  capEnd  = v[5];
    DWORD  capacity = (begin != 0) ? (capEnd - begin) / 8 : 0;

    if (count == 0)
        return obj;

    DWORD n = (current - begin) / 8;
    if (0x1FFFFFFFu - n < (DWORD)count)
        SehVecThrowLengthError();
    DWORD newCount = n + (DWORD)count;

    DWORD local[2];   // the insert value; {value pointer, 0} until copied
    local[0] = (DWORD)value;
    local[1] = 0;

    char *pos = (char *)position;
    char *cur = (char *)current;

    if (capacity >= newCount)
    {
        // In-place insert: shift [pos, cur) right by count*8 (taking a
        // reference on the moved elements), fill the vacated [pos, pos+count*8)
        // (and any slots past the old current) with the value, advance current.
        // Unreachable for push_back (the grow path always reallocates).
        SehVecCopyElem(local, value);
        DWORD tail = (DWORD)(cur - pos) / 8;
        SehVecCopyRet(pos, cur, pos + 8 * count);
        SehVecAssignRange(pos, pos + 8 * count, local);
        if ((DWORD)count > tail)
            SehVecFill(cur, (int)((DWORD)count - tail), local);
        v[4] = (DWORD)cur + 8 * count;
        SehVecDestructElem(local);
        return obj;
    }

    // Reallocation: grow by 1.5x (clamped to newCount, overflow -> bad_alloc),
    // fill the value at the insertion point, move the prefix and suffix into the
    // new storage, destroy + free the old storage, repoint the vector.
    DWORD newCap;
    if (0x1FFFFFFFu - capacity / 2 >= capacity)
    {
        DWORD grown = capacity / 2 + capacity;
        newCap = (grown < newCount) ? newCount : grown;
    }
    else
    {
        newCap = newCount;
    }
    char *newBuf = (char *)SehVecAlloc(newCap);
    if (newBuf == 0)
        return obj;
    DWORD posIndex = (DWORD)(pos - (char *)begin) / 8;
    SehVecFill(newBuf + 8 * posIndex, count, value);
    SehVecCopy((char *)begin, pos, newBuf);
    SehVecCopy(pos, cur, newBuf + 8 * ((DWORD)count + posIndex));
    if (begin != 0)
    {
        // destroy the old elements [begin, current): release each refblock
        // (zero -> dtor + LHFree).  (The original does this via
        // SehBlockArrayDtor; the memory is freed immediately after, so the
        // element zeroing that helper also performs is omitted.)
        for (char *i = (char *)begin; i != (char *)current; i += 8)
        {
            void *block = *(void **)(i + 4);
            if (block != 0 && InterlockedDecrement((volatile LONG *)block) == 0)
            {
                void **b = (void **)block;
                ((void (*)(void *))b[1])(b[2]);
                LHFree(block);
            }
        }
        LHFree((void *)begin);
    }
    v[3] = (DWORD)newBuf;
    v[4] = (DWORD)newBuf + 8 * newCount;
    v[5] = (DWORD)newBuf + 8 * newCap;
    SehVecDestructElem(local);
    return obj;
}

// sub_6213F0 (thiscall + retn 8) -- bounds-checked element access.  `obj` is
// the {buffer, begin} pair (buffer = the self-ref wrapper, begin = the storage
// base); the original bounds-checks begin + 8*index against [vector+12, +16]
// (== [begin, current], reached through buffer[0] == &vector) and, on success,
// writes {buffer, begin + 8*index} to `out` and returns it.
static void *SehVecAt(void *obj, void **out, int index)
{
    DWORD *pair = (DWORD *)obj;
    DWORD  buffer = pair[0];
    if (buffer == 0)
        return 0;   // the original's __invalid_parameter_noinfo (unreachable)
    DWORD vec      = *(DWORD *)buffer;
    DWORD indexPos = pair[1] + 8 * (DWORD)index;
    if (indexPos < vec + 12 || indexPos > vec + 16)
        return 0;   // the original's __invalid_parameter_noinfo (out of range)
    out[0] = (void *)buffer;
    out[1] = (void *)indexPos;
    return out;
}

// sub_625600 (thiscall + retn 0x10) -- the push_back grow path: insert one
// element at `current` (growing), then return the iterator to it.  The original
// also validates (current-begin) alignment / ordering; those pass here.
static void *SehVecPushBackGrow(void *obj, void **out, void *value)
{
    DWORD *v = (DWORD *)obj;
    int index = ((DWORD)v[4] - (DWORD)v[3]) / 8;   // the new element's index
    out[0] = 0;
    SehVecInsert(obj, (void *)v[4], 1, value);    // insert at current (grow)
    void *pair[2];
    pair[0] = (void *)v[0];   // buffer (self-ref wrapper)
    pair[1] = (void *)v[3];   // begin (re-read after the grow)
    return SehVecAt(pair, out, index);
}

// sub_625E20 (thiscall + retn 4) -- push_back: append the 8-byte {obj, refblock}
// pair to the video vector, taking a refblock reference.  When the vector is not
// full the element is written in place; otherwise the grow path reallocates.
static void SehVecPushBack(void *obj, void *elem)
{
    DWORD *v = (DWORD *)obj;
    DWORD  begin    = v[3];
    DWORD  capacity = (begin != 0) ? (v[5] - begin) / 8 : 0;
    DWORD  n        = (v[4] - begin) / 8;
    if (n >= capacity)
    {
        void *dummy[2];   // the grow path's caller-side iterator out-buffer
        SehVecPushBackGrow(obj, (void **)dummy, elem);
        return;
    }
    DWORD current = v[4];
    if (current != 0)
    {
        *(DWORD *)current       = *(DWORD *)elem;
        DWORD rb                = *(DWORD *)((char *)elem + 4);
        *(DWORD *)(current + 4) = rb;
        if (rb != 0)
            InterlockedIncrement((volatile LONG *)rb);
    }
    v[4] = current + 8;
}

//  ReleaseVideoFile  (0x61E230 - 0x61E241)  -- the 16-byte video
//  (CLegacyMemoryFile) wrapper dtor: run the object's vtable slot 0
//  (release, the free-it flag = 1) when the pointer is nonzero.  cdecl.
static int ReleaseVideoFile(void *obj)
{
    if (obj != 0)
    {
        void **vt = (void **)(*(void **)obj);
        return (int)((int(__thiscall *)(void *, int))vt[0])(obj, 1);
    }
    return 0;
}

//  MountVideoFile  (0x61BFE0 - 0x61C079)  -- mount a video by shared-slot
//  file id: build the 16-byte CLegacyMemoryFile (CLegacyMemoryFileCtor
//  over the path slot with the 1 / 2 flags), wrap it in the 12-byte
//  { refcount, dtor = ReleaseVideoFile, obj } block, append the value to
//  the +0x30 video vector (SehVecPushBack), then drop the local wrapper
//  reference (the InterlockedDecrement-to-zero -> dtor -> LHFree idiom).
//  The original is thiscall + retn 4 (the owner in ECX, the file id on the
//  stack); here a plain free function.  [NOT byte-identical]
//  placement-dependent (the E8 calls above) and refs the
//  InterlockedDecrement IAT slot at its original high VA.
static int MountVideoFile(void *obj, F3SharedBuffer **slotId)
{
    void *video = 0;
    void *buf = LHAlloc(16);
    if (buf != 0)
        video = (void *)CLegacyMemoryFileCtor(buf, slotId, 1, 2);
    void *value   = video;
    void *wrapper = 0;
    if (video != 0)
    {
        void *w = LHAlloc(12);
        if (w != 0)
        {
            *(DWORD *)w = 1;
            *(void **)((char *)w + 4) = (void *)ReleaseVideoFile;
            *(void **)((char *)w + 8) = video;
            wrapper = w;
        }
    }
    SehVecPushBack((char *)obj + 0x30, &value);
    int result = 0;
    if (wrapper != 0)
    {
        result = (int)InterlockedDecrement((volatile LONG *)wrapper);
        if (result == 0)
        {
            (*(void (*)(void *))(*(void **)((char *)wrapper + 4)))(
                *(void **)((char *)wrapper + 8));
            LHFree(wrapper);
        }
    }
    return result;
}

// 0x2075EAC: the GDB loader wrapper (published by CreateGdbLoader).
static void *g_dword_2075EAC = 0;

// sub_50A540 (unresolved in this build) -- the 56-byte GDB file-reader ctor
// (thiscall, the reader in ECX).  Out of scope.  Stub: returns the object.
//  [remaining stub]
static void *sub_50A540(void *obj) { return obj; }

//  CreateGdbLoader  (0x1A19D20 - 0x1A19D69)  -- allocate the 4-byte wrapper
//  holding the 56-byte GDB file reader (sub_50A540 [remaining stub]) and
//  publish the wrapper in dword_2075EAC; returns the wrapper (0 on
//  allocation failure, in which case the global is cleared).
//  [NOT byte-identical] placement-dependent (the LHAlloc E8 calls) and refs
//  the high-VA global dword_2075EAC (0x02075EAC).
static void *CreateGdbLoader(void)
{
    void *wrapper = LHAlloc(4);
    if (wrapper != 0)
    {
        void *reader = LHAlloc(0x38);
        *(void **)wrapper = (reader != 0) ? sub_50A540(reader) : 0;
        g_dword_2075EAC = wrapper;
        return wrapper;
    }
    g_dword_2075EAC = 0;
    return 0;
}

// 0x1DBD032: the dev-build flag (the dev_roots_built.txt gate in
// LoadGlobalGdbFiles); zero in this build.
static BYTE g_byte_1DBD032 = 0;

static void CopyFileToSharedSlot(void *file, F3SharedBuffer **out);

// sub_1A19E10 (0x1A19E10 - 0x1A19F76) -- load one GDB file through the
// reader (the sub_1A19D70 file lookup, the sub_402F00 hash build, the
// SetSharedSlot + LegacyFileCtor memory-file open, the reader vtable slot 10
// load dispatch, the sub_17FD970 close).  Deep dependency chain -- out of
// scope.  Stub: returns 0 (the file did not load).  [remaining stub]
static long sub_1A19E10(void *loader, const char *path, int a3)
{ (void)loader; (void)path; (void)a3; return 0; }

//  LoadGlobalGdbFiles  (0x61A100 - 0x61A211)  -- load the five permanent
//  global GDB files through the GDB loader: Globals.gdb, gui.gdb,
//  SpeechAction.gdb, InteractiveCutscenes.gdb, and animatedcutscenes.gdb,
//  then -- only in the dev build (byte_1DBD032 set, not running from disk)
//  -- reads Globals\\dev_roots_built.txt through the memory-file trio and
//  loads Entity.gdb when it begins with "true".  The last load's result is
//  returned.  The original is thiscall + retn 4 (the ECX this is never used
//  -- the loader from the stack arg is threaded into every load); here a
//  plain free function.  [NOT byte-identical] placement-dependent (the E8
//  calls) and refs the high-VA string literals + byte_1DBD032 / byte_1DBD02A.
static int LoadGlobalGdbFiles(void *loader)
{
    long result = sub_1A19E10(loader, "data\\Globals\\Globals.gdb", 0);
    sub_1A19E10(loader, "data\\Art\\GUI\\gui.gdb", 0);
    sub_1A19E10(loader, "data\\Globals\\SpeechAction.gdb", 0);
    sub_1A19E10(loader, "data\\InteractiveCutscenes\\InteractiveCutscenes.gdb", 0);
    result = sub_1A19E10(loader, "data\\animatedcutscenes\\animatedcutscenes.gdb", 0);
    if (g_byte_1DBD032 != 0 && g_byte_1DBD02A == 0)
    {
        F3SharedBuffer *outSlot  = 0;
        F3SharedBuffer *fileSlot = 0;
        F3SharedBuffer *memBuf   = 0;
        InitSharedSlot(&outSlot);
        SetSharedSlot(&fileSlot, "Globals\\dev_roots_built.txt", -1);
        void *memFile = LegacyFileCtor(&memBuf, &fileSlot, 1, 2);
        CopyFileToSharedSlot(memFile, &outSlot);
        LegacyFileDtor(&memBuf);
        ReleaseSharedSlot(&fileSlot);
        const char *s = (outSlot != 0 && outSlot->data != 0) ? outSlot->data : "";
        int isTrue = (strncmp(s, "true", 4) == 0);
        result = ReleaseSharedSlot(&outSlot);
        if (isTrue != 0)
            result = sub_1A19E10(loader, "data\\Entity\\Entity.gdb", 0);
    }
    return (int)result;
}

// sub_72C220 -- the 192-byte bank container's +0x1C string init.
// Deep dependency chain -- out of scope.  Stub.  [remaining stub]
// (sub_7281F0 is now InitSubNodeTree, engine/subnode.cpp;
// sub_5AA8F0 is now InitNameMapTree, engine/namemap.cpp;
// sub_6527D0 is now InitBankTree, engine/banktree.cpp.)
static void sub_72C220(void *obj, int capacity) { (void)obj; (void)capacity; }

//  InitBankContainer  (0x7201B0 - 0x720248)  -- construct the 192-byte
//  bank container in place: +0x18 = 15 (the string SSO capacity), +0x14 / +4
//  = 0, the +0x1C string init (sub_72C220 [remaining stub] with the 0x4000
//  word), the 4-byte back-reference node into +0x5C, the +0x68 / +0x6C /
//  +0x70 zero words, the +0x74 / +0x94 / +0xCC collection inits (InitSubNodeTree
//  / InitNameMapTree / InitBankTree seeded with the 0x4000
//  word), the +0xB4 critical section (the CRT InitializeCriticalSection
//  thunk, unknown_libname_2585), and the +0xEC flag zero.  Returns this.
//  The original is thiscall + retn 8 (the 0x32 word is unused); here a
//  plain free function.  [NOT byte-identical] placement-dependent (the E8
//  calls) and refs the InitializeCriticalSection IAT slot.
static void *InitBankContainer(void *obj, int unused, int capacity)
{
    (void)unused;
    char *o = (char *)obj;
    *(DWORD *)(o + 0x18) = 15;
    *(DWORD *)(o + 0x14) = 0;
    o[4] = 0;
    sub_72C220(o + 0x1C, capacity);
    void *node = LHAlloc(4);
    if (node != 0)
        *(void **)node = o + 0x5C;
    *(void **)(o + 0x5C) = node;
    *(DWORD *)(o + 0x68) = 0;
    *(DWORD *)(o + 0x6C) = 0;
    *(DWORD *)(o + 0x70) = 0;
    InitSubNodeTree(o + 0x74);
    InitNameMapTree(o + 0x94);
    InitializeCriticalSection((CRITICAL_SECTION *)(o + 0xB4));
    InitBankTree(o + 0xCC);
    o[0xEC] = 0;
    return obj;
}

// Placeholder vtable for the stub-constructed NUI::CGUI member (the engine's
// +0xB8 object): the original ctor (sub_6AFD50) stores the real NUI::CGUI
// vtable (0x6AFEE0), whose slot 0 (release, the free-it flag) and slot 1
// (the activate dispatch RebuildGuiObject tail-calls) are unimplemented in
// this build, so both are callable no-ops.
static int F3StubInt(void *obj) { (void)obj; return 0; }
static void *g_CGuiVtable[2] = { (void *)F3StubRelease, (void *)F3StubInt };

// sub_6AFD50 (0x6AFD50 - 0x6AFFD0) -- the NUI::CGUI ctor over the 400-byte
// object (the full GUI / options / packet-callback sub-construct).  Deep
// dependency chain -- out of scope.  Stub: store the placeholder vtable and
// return this.  [remaining stub]
static void *InitGuiObject(void *obj)
{
    *(void **)obj = g_CGuiVtable;
    return obj;
}

//  RebuildGuiObject  (0x61A7C0 - 0x61A832)  -- replace the engine's +0xB8
//  NUI::CGUI object: run the old object's vtable slot 4 (the shutdown
//  dispatch) then slot 0 (release, free-it = 1) and clear the slot, build a
//  fresh 400-byte CGUI (LHAlloc + sub_6AFD50 [remaining stub]), release the
//  now-old value again (the free-it flag) if it reappeared, store the new
//  object, and tail-call its vtable slot 1 (the activate dispatch).
//  The original is thiscall (the engine in ECX); here a plain free function.
//  [NOT byte-identical] placement-dependent (the E8 calls + the tail jump).
static int RebuildGuiObject(void *obj)
{
    char *o = (char *)obj;
    void *cur = *(void **)(o + 0xB8);
    if (cur != 0)
    {
        void **vt = (void **)(*(void **)cur);
        ((void(__thiscall *)(void *))vt[4])(cur);
        if (*(void **)cur != 0)
            ((void(__thiscall *)(void *, int))(void *)((void **)(*(void **)cur))[0])(cur, 1);
        *(void **)(o + 0xB8) = 0;
    }
    void *buf = LHAlloc(0x190);
    void *fresh = (buf != 0) ? InitGuiObject(buf) : 0;
    void *cur2 = *(void **)(o + 0xB8);
    if (cur2 != 0)
        ((void(__thiscall *)(void *, int))(void *)((void **)(*(void **)cur2))[0])(cur2, 1);
    *(void **)(o + 0xB8) = fresh;
    void **vt2 = (void **)(*(void **)fresh);
    return (int)((int(__thiscall *)(void *))vt2[1])(fresh);
}

//  FileSubsysMountCollection  (0x1801620 - 0x18016F3)  -- the file-subsystem
//  collection mount (the +0x4C self-tag / +0x58-begin / +0x5C-end debug-
//  iterator loop over the file-entry collection).  [NOT byte-identical]
//  placement-dependent (the E8 calls to the __invalid_parameter_noinfo IAT
//  slot and the entry vtable slot dispatches).
//
//  (void *)FileSubsysMountCollection(void *fs, void **out,
//                                    unsigned char (__cdecl *callback)(int, int),
//                                    int data)
//
//  Iterates the file-entry collection [begin = +0x58, end = +0x5C) of the
//  0x8C-byte file-subsystem object, one element per 4-byte slot: for each
//  entry it locks the entry (vtable slot 1, the original's stdcall dispatch),
//  then calls the mount callback with (entry, data); on a callback failure
//  (returns 0) the entry is stored in *out, re-locked + unlocked (vtable slots
//  1 and 2) and returned via *out.  When the collection runs to the end the
//  whole collection was processed and *out = 0 is written.  The MSVC
//  debug-iterator bounds checks (the live begin/end words and the self-tag
//  block at +0x4C) are reproduced as `F3InvalidParam` calls, matching the
//  original __invalid_parameter_noinfo sequence.
//  The original is thiscall + retn 0Ch (the object in ECX, the out slot in
//  EDX, the callback + data on the stack, callee cleans via retn 12); it is
//  declared as a plain free function here (the project convention).
static void *FileSubsysMountCollection(void *fs, void **out,
                                       unsigned char (__cdecl *callback)(int, int),
                                       int data)
{
    DWORD *o = (DWORD *)fs;
    DWORD tag = o[0x4C / 4];      // the +0x4C self-tag block pointer
    DWORD begin = o[0x58 / 4];    // the collection begin (a 4-byte element ptr)
    DWORD end = o[0x5C / 4];      // the collection end

    if (begin > end)
        F3InvalidParam();

    DWORD element = 0;            // the slot written to *out below
    for (DWORD i = begin; ; i += 4)
    {
        end = o[0x5C / 4];
        if (o[0x58 / 4] > end)
            F3InvalidParam();
        if (tag == 0 || tag != o[0x4C / 4])
            F3InvalidParam();
        if (i == end)
        {
            *out = 0;
            return out;
        }

        DWORD m;
        if (tag != 0)
            m = *(DWORD *)tag;
        else
        {
            F3InvalidParam();
            m = 0;
        }
        if ((DWORD)i >= *(DWORD *)(m + 0x10))
            F3InvalidParam();

        element = *(DWORD *)i;
        if (element != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)element)[1])((void *)element);
        if (callback((int)element, data) == 0)
            break;
        if (element != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)element)[2])((void *)element);

        DWORD m2;
        if (tag != 0)
            m2 = *(DWORD *)tag;
        else
        {
            F3InvalidParam();
            m2 = 0;
        }
        if ((DWORD)i >= *(DWORD *)(m2 + 0x10))
            F3InvalidParam();
    }

    *out = (void *)(DWORD)element;
    if (element != 0)
    {
        ((void (WINAPI *)(void *))F3_VT((void *)element)[1])((void *)element);
        ((void (WINAPI *)(void *))F3_VT((void *)element)[2])((void *)element);
    }
    return out;
}

//  ReloadTextBookBank  (0x719CA0 - 0x719D64)  -- the file-subsystem mount
//  callback for the bank container: reload "language\\text\\book.babel"
//  through the VFS archive collection (GetVfsArchiveCollection +
//  VfsReloadFileEntry) and rebuild the 240-byte bank container over the
//  reloaded entry (InitBankContainer + sub_721F60 + sub_7217B0 +
//  DestroyBankContainer + the entry's vtable slot 2).  The original is
//  cdecl with a __security_cookie prologue.  The two deep leaves
//  (sub_721F60 / sub_7217B0 -- the container-load / container-sync steps)
//  are remaining no-op stubs; the container-build block is unreachable in
//  this build because the VFS reload never matches a provider (VfsReloadFileEntry
//  writes a null out slot).  [NOT byte-identical] placement-dependent
//  (the E8 calls + the __security_cookie prologue).
static void *GetVfsArchiveCollection(void);   // sub_180D9B0 (defined below)
static void *VfsReloadFileEntry(void *collection, void **out, void *entry, void *path); // sub_180D220 (defined below)
//  sub_721F60 -- the deep "reload the container over the reloaded entry"
//  step: takes the entry (the mount's file-entry handle), re-reads it and
//  folds it into the just-built bank container.  Deep dependency chain
//  (sub_18A6660 / sub_4C7540 / sub_721840 / sub_4C8020 -- VM / SEH / container
//  internals) -- out of scope.  Stub.  [remaining stub]
static char ReloadBankContainerEntry(void *container, int entry) { (void)container; (void)entry; return 0; }
//  sub_7217B0 -- the deep "sync the impl's bank container with the just-built
//  stack container" step: CsGuardAcquire the impl's +0xB4 critical section,
//  then replay the +0x1C / +0x58 / +0x74 / +0x94 members.  Deep dependency
//  chain (CsGuardAcquire / sub_72C5F0 / sub_720250 / sub_71FD20 / sub_720010) --
//  out of scope.  Stub.  [remaining stub]
static char SyncImplWithBankContainer(int impl, void *container) { (void)impl; (void)container; return 1; }

static char ReloadTextBookBank(int entry, int container)
{
    void *collection = GetVfsArchiveCollection();                 // sub_180D9B0
    F3WString name;
    F3WStringFromAnsi(&name, "language\\text\\book.babel");       // sub_5AF180 + sub_4357C0
    void *outEntry = 0;
    VfsReloadFileEntry(collection, &outEntry, (void *)entry, &name);   // sub_180D220
    if (name.cap >= 8)                                            // the cap >= 8 LHFree idiom
        LHFree((void *)name.u.ptr);
    if (outEntry != 0)
    {
        char buf[0xF0];
        InitBankContainer(buf, 50, 0x4000);                       // sub_7201B0
        ReloadBankContainerEntry(buf, (int)outEntry);             // sub_721F60 [remaining stub]
        SyncImplWithBankContainer(container, buf);                // sub_7217B0 [remaining stub]
        DestroyBankContainer(buf);                                // sub_626EF0
        if (outEntry != 0)
            ((void (WINAPI *)(void *))F3_VT(outEntry)[2])(outEntry);   // the entry's vtable slot 2 (stdcall)
    }
    return 1;
}

//  RegisterBankContainer  (0x719D70 - 0x719D9D)  -- register the bank
//  container with the global file subsystem: the collection mount
//  (FileSubsysMountCollection (0x1801620) with the ReloadTextBookBank
//  callback), and when the mount produced an entry, dispatch its vtable slot 2
//  (thiscall, the entry also as the stack arg).  Returns the dispatch (or the
//  entry pointer).
//  [NOT byte-identical] placement-dependent (the E8 calls + the entry vtable
//  dispatch).
static int RegisterBankContainer(int impl)
{
    void *fs = GetFileSubsystem(0);
    void *out = 0;
    FileSubsysMountCollection(fs, &out, (unsigned char (__cdecl *)(int, int))ReloadTextBookBank, impl);
    if (out != 0)
    {
        void **vt = (void **)(*(void **)out);
        return (int)((int(__thiscall *)(void *, int))vt[2])(out, (int)out);
    }
    return (int)out;
}

// 0x20FD138 (dword_20FD138): the data-archive shared slot -- the slot the
// MountAnimsBank publish (SetSharedSlot / CopySharedSlot / ReleaseSharedSlot)
// writes the "data\\" archive into.  Sibling of the dword_1DBE2CC settings
// base (g_unk_1DBE2CC).
static F3SharedBuffer *g_dword_20FD138 = 0;

// 0x1DBD0E8 (unk_1DBD0E8): the 4-byte global the GlobalVfsRegistration
// thunk reads -- the original types it as char ** (the slot stores a pointer
// to the ANSI string the registration converts).  Left null in this build
// (the caller never sets it), so the registration always takes its default
// (empty) string path.
static char **g_unk_1DBD0E8 = 0;

//  the forward declarations for the deep leaves (declared later in the file)
//  sub_56FB30 (0x56FB30 - 0x56FBA8) -- the record-array append helper: takes
//  the 64-byte record at +0x28 of the out block (the 4-byte self-ref wrapper),
//  and the out slot for the record's new value (&v10).  Writes the record
//  through the array, releasing the old value's vtable slot 1 (AddRef) on the
//  overflow path (sub_571B10).  Deep dependency chain (sub_571B10 -- the
//  archive-manager reload step) -- out of scope.  Stub.  [remaining stub]
static DWORD RecordArrayAppend(DWORD *rec, DWORD *outSlot)
{
    (void)rec; (void)outSlot;
    return 0;
}

static void *GetVfsArchiveCollection(void);
static void *VfsEntryLoad(void *collection, void **out, int a2, F3WString *path);
static void *LookupBnk(void *out, void *cwdSlot, void *nameBuf, int flags);
static F3SharedBuffer **GetArchiveSlot(F3SharedBuffer **out);

//  sub_61B730 (0x61B730 - 0x61B851) -- AnimsBankMount: the per-entry
//  "fable2_anims.bnk" mount callback: takes the file-entry handle and the
//  64-byte out block, resolves the archive slot, registers the entry against
//  the wide outblock (the GetVfsArchiveCollection / sub_180D3F0 / sub_1806060
//  / GetArchiveSlot / LHFree / vtable-dispatch chain), and writes the
//  wide-outblock record over the caller's 64-byte out block.  The deep leaves
//  (sub_180D3F0 / sub_1806060 / sub_56FB30 -- the VFS load / bank lookup /
//  record-array append) are [remaining stubs]; in this build the archive slot
//  is null and the bank handle is 0, so the whole slot-10 dispatch block is
//  dormant (identical to the no-op tail the original falls through to).
//  [NOT byte-identical] placement-dependent (the E8 calls to
//  GetVfsArchiveCollection / GetArchiveSlot / LHFree / the stubbed leaves +
//  the __security_cookie prologue).
//  IDA renamed sub_61B730 -> AnimsBankMount.
static unsigned char AnimsBankMount(int entry, int out)
{
    //  the 64-byte dest block the per-entry load builds (the wide outblock)
    BYTE dest[64];
    memset(dest, 0, sizeof(dest));

    GetVfsArchiveCollection();                                     // the collection

    //  the per-entry VFS load: hash the path, look the entry up in the
    //  collection, and (when it matches a data entry) write the resolved
    //  64-byte block over the dest.  Stub -- the deep chain is out of scope.
    VfsEntryLoad(GetVfsArchiveCollection(), (void **)&dest, entry, (F3WString *)dest);   // sub_180D3F0 [remaining stub]

    DWORD slotOut = 0;
    DWORD v2 = (DWORD)LookupBnk((void *)&slotOut, (void *)(DWORD)entry, (void *)dest,
                                (DWORD)((DWORD *)out)[0x24 / 4]);   // sub_1806060 [remaining stub]
    DWORD v10 = v2;

    if (v2 != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)v2)[1])((void *)v2);   // the bank handle's slot 1 (AddRef)
    if (slotOut != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)slotOut)[2])((void *)slotOut);   // the slot's slot 2 (Release)

    if (v2 != 0)
    {
        DWORD v3 = (DWORD)(*GetArchiveSlot((F3SharedBuffer **)&slotOut));   // sub_180AC90

        //  the wide-string name block (v6): pre-seed the empty SSO layout
        //  (cap 7 / len 0 / SSO word 0).
        BYTE nameBlock[36];
        memset(nameBlock, 0, sizeof(nameBlock));
        *(WORD *)(nameBlock + 0x04) = 0;
        ((DWORD *)nameBlock)[0x14 / 4] = 0;                            // len = 0
        ((DWORD *)nameBlock)[0x18 / 4] = 7;                            // cap = 7

        //  the slot-10 ("add archive") thiscall dispatch on the archive
        //  manager -- dead in this build (the archive slot is null, the
        //  success path is never reached).  Faithful reproduction.
        typedef void (__thiscall *F3ArchiveAddArchiveFn)(void *this_, DWORD handle,
                                                         F3WString *name, int f1, int f2);
        F3ArchiveAddArchiveFn fn = (F3ArchiveAddArchiveFn)F3_VT((void *)(DWORD)v3)[10];
        fn((void *)(DWORD)v3, v2, (F3WString *)nameBlock, 2, 1);

        if (((DWORD *)nameBlock)[0x18 / 4] >= 8)
            LHFree((void *)((DWORD *)nameBlock)[0x04 / 4]);

        //  reset to the empty SSO layout
        ((DWORD *)nameBlock)[0x14 / 4] = 0;
        ((DWORD *)nameBlock)[0x18 / 4] = 7;
        *(WORD *)(nameBlock + 0x04) = 0;

        if (slotOut != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)slotOut)[2])((void *)slotOut);   // the slot's slot 2 (Release)

        RecordArrayAppend((DWORD *)((BYTE *)out + 0x28), &v10);              // sub_56FB30 [remaining stub]
        if (v10 != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)v10)[2])((void *)v10);   // the record's slot 2 (Release)
    }

    //  the tail: the original reads the 64-byte dest block's cap field
    //  (+0x18) and frees the block's +0x04 pointer when it is >= 8 -- the
    //  same idiom as OutBlockDtor.  In this build the dest is zero-filled
    //  (the stubs never allocate), so the cap is 0 and the LHFree is never
    //  taken.
    if (((DWORD *)dest)[0x18 / 4] >= 8)
        LHFree((void *)((DWORD *)dest)[0x04 / 4]);

    return 1;
}


//  SpeechBankWideAssign (sub_435B20) is defined later in the file; forward
//  declare it for the out-block init and the record build below.
static F3WString *SpeechBankWideAssign(F3WString *dst, const F3WString *src,
                                       unsigned int offset, unsigned int count);

//  OutBlockInit  (0x625D10 - 0x625D82)  -- the 64-byte out-block init: the
//  36-byte F3WString wide-string build folded into the out block.  Pre-seeds
//  the +0x14 (len) / +0x18 (cap) / +0x04 (SSO buf word) fields, assigns the
//  source wide string into the block (SpeechBankWideAssign, the string-assign
//  core -- the original's thiscall this=ctx, src/offset/count on the stack),
//  then copies the source's e1/e2 words (+0x1C / +0x20), stores the third
//  stack arg (a3) at +0x24, builds the 4-byte self-ref wrapper block at +0x28
//  (LHAlloc(4): the wrapper holds &block.selfRef, the block's selfRef = the
//  wrapper; NULL -> selfRef = 0), zeroes +0x34 / +0x38 / +0x3C, and returns
//  the block.  [NOT byte-identical] placement-dependent (the E8 calls to
//  SpeechBankWideAssign / LHAlloc).
static F3WString *OutBlockInit(F3WString *obj, const F3WString *src, int a3)
{
    DWORD *this_ = (DWORD *)obj;
    this_[0x18 / 4] = 7;                            // +0x18 = 7 (cap pre-seed)
    this_[0x14 / 4] = 0;                            // +0x14 = 0 (len pre-seed)
    *(WORD *)((BYTE *)this_ + 0x04) = 0;            // +0x04 = 0 (word, SSO buf)

    SpeechBankWideAssign(obj, src, 0, 0xFFFFFFFFu);

    this_[0x1C / 4] = src->e1;                      // +0x1C = the source's e1
    this_[0x20 / 4] = src->e2;                      // +0x20 = the source's e2
    this_[0x24 / 4] = a3;                           // +0x24 = a3

    BYTE *selfRefBlock = (BYTE *)obj + 0x28;
    void *blk = LHAlloc(4);
    if (blk != 0)
    {
        *(void **)blk = selfRefBlock;               // the wrapper holds &obj.selfRef
        *(void **)selfRefBlock = blk;               // the block's selfRef = blk
    }
    else
    {
        *(void **)selfRefBlock = 0;
    }

    this_[0x34 / 4] = 0;                             // +0x34 = 0
    this_[0x38 / 4] = 0;                             // +0x38 = 0
    this_[0x3C / 4] = 0;                             // +0x3C = 0
    return obj;
}

//  GlobalZeroInit  (0x18E3860 - 0x18E3BF5)  -- the global-zero-init: memsets the
//  0x200000-byte region at dword_1E63030, sets dword_1E2F028 = &dword_1E63030,
//  dword_1E63030/34 = 0, dword_1E63038 = 0x200000, memsets 0x4000 at dword_1E3F030
//  and 0x20000 at unk_1E43030, links the self-referential 8-byte pointers from
//  &unk_1E4304C up to &dword_1E6302C (*result = result + 1, step 8), sets
//  dword_1E2F024 = &unk_1E43030, and returns the final result pointer.
//  Deep dependency chain (the 2MB/2-MB high-VA regions + the self-referencing
//  link writes) -- out of scope.  Stub.  [remaining stub]
static int GlobalZeroInit(void) { return 0; }

//  InitGlobalsOnce  (0x18EBEB0 - 0x18EBEC6)  -- the one-shot global-init thunk:
//  reads byte_2075F28; when it is clear, runs the sub_18E3860 global-zero-init
//  (GlobalZeroInit, stubbed) once, then sets byte_2075F28 = 1.  The return value
//  is the init's result (0 while the callee is stubbed -- the caller ignores it).
//  [NOT byte-identical] placement-dependent (the E8 call to the init callee +
//  the absolute store/load of the high-VA flag byte g_byte_2075F28).
static int InitGlobalsOnce(void)
{
    int result = 0;
    if (g_byte_2075F28 == 0)
        result = GlobalZeroInit();
    g_byte_2075F28 = 1;
    return result;
}

//  GlobalVfsRegistration  (0x18ED640 - 0x18EDA79)  -- the global VFS
//  registration thunk: LHAlloc(84) + VirtualFileSystemCtor (the 84-byte VFS
//  object), publish it in the 0x20FD080 slot (the previous holder's release is
//  dormant here -- the fresh VFS carries a placeholder vtable), build the
//  ANSI string from the caller's slot (defaulting to an empty string), build
//  the 68-byte record (sub_56E2A0, implemented below), free the source
//  wide-string buffer (the cap >= 8 LHFree idiom), mount the file-subsystem
//  collection over the record (FileSubsysMountCollection with the
//  sub_18EC4D0 callback -- stubbed), and release the record's heap buffer.
//  The vt[6] collection-iterator loop and the vt[4] per-entry dispatch are
//  dormant in this build (the fresh VFS carries a placeholder vtable, so
//  they cannot be safely performed), as is the per-entry refcount chain
//  (sub_18A8C30 / sub_18DD8A0 / sub_18DC400 / sub_575680 / sub_570D30 /
//  sub_56E380) and the mount callback itself (sub_18EC4D0).
//  [NOT byte-identical] placement-dependent (the E8 calls to LHAlloc /
//  VirtualFileSystemCtor / F3WStringFromAnsi / FileSubsysMountCollection /
//  LHFree + the __security_cookie prologue).
static void *VirtualFileSystemCtor(void *obj);

static DWORD g_dword_20FD080 = 0;   // 0x20FD080 -- the published VFS object
static BYTE  g_byte_2122F24 = 0;     // 0x2122F24 -- the legacy-streaming flag
static DWORD g_dword_2122F20 = 0;    // 0x2122F20 -- the per-entry counter
static DWORD g_dword_20FD164 = 0;    // 0x20FD164 -- the collection range begin
static DWORD g_dword_20FD168 = 0;    // 0x20FD168 -- the collection range end

//  sub_56E2A0 (0x56E2A0 - 0x56E323) -- the 68-byte record builder: the wide
//  string is copied into the record (as an F3WString at +0x00), the e1/e2
//  words ride along (+0x1C / +0x20), +0x24 = 1, +0x28 = the VFS object, a
//  4-byte self-ref wrapper block is allocated at +0x2C, and the +0x38 / +0x3C
//  / +0x40 words are zeroed.  The original's vt[1] dispatch on the VFS object
//  is dormant in this build (the fresh VFS carries a placeholder vtable).
static void SpeechBankRecordBuild(F3WString *src, BYTE *rec, DWORD vfsObj)
{
    DWORD *r = (DWORD *)rec;
    r[0x18 / 4] = 7;                               // +0x18 = 7 (the cap word pre-seed)
    r[0x14 / 4] = 0;                               // +0x14 = 0
    *(WORD *)((BYTE *)rec + 0x04) = 0;             // +0x04 = 0 (word)
    SpeechBankWideAssign((F3WString *)rec, src, 0, 0xFFFFFFFFu);
    r[0x1C / 4] = src->e1;                         // +0x1C = the source's e1
    r[0x20 / 4] = src->e2;                         // +0x20 = the source's e2
    r[0x24 / 4] = 1;                               // +0x24 = 1
    r[0x28 / 4] = vfsObj;                          // +0x28 = the VFS object
    // (the original's vt[1] dispatch on vfsObj -- dormant here: placeholder vtable)
    BYTE *selfRefBlock = (BYTE *)rec + 0x2C;
    void *blk = LHAlloc(4);
    if (blk != 0)
    {
        *(void **)blk = selfRefBlock;              // the wrapper holds &rec.selfRef
        *(void **)selfRefBlock = blk;              // the record's selfRef = blk
    }
    else
    {
        *(void **)selfRefBlock = 0;
    }
    r[0x38 / 4] = 0;                               // +0x38 = 0
    r[0x3C / 4] = 0;                               // +0x3C = 0
    r[0x40 / 4] = 0;                               // +0x40 = 0
}

//  sub_18EC4D0 -- the per-entry mount callback (the GetVfsArchiveCollection /
//  sub_180D3F0 / sub_1806060 / sub_56FB30 chain).  Deep dependency chain --
//  out of scope.  Stub.  [remaining stub]
static unsigned char SpeechBankMountCallback(int entry, int record)
{
    (void)entry; (void)record; return 1;
}

//  sub_575FD0 -- the tail tree-walk over the 0x20FD164/0x20FD168 collection
//  range.  Deep dependency chain -- out of scope.  Stub.  [remaining stub]
static int Sub_575FD0(int a1, int a2, int a3, int a4, int a5, int a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; return 0;
}

static int GlobalVfsRegistration(char ***a1)
{
    // 1. the 0x84-byte VFS object ctor
    char *v1 = (char *)LHAlloc(84);                       // 0x84 = 84
    void *v2 = 0;
    if (v1 != 0)
    {
        v2 = VirtualFileSystemCtor(v1);
        // (The two vt[1] dispatches on the fresh VFS -- the AddRef pair --
        //  are dormant in this build: the fresh VFS carries a placeholder
        //  vtable, so they are not performed.)
    }

    // 2. release the previous holder -- dormant (placeholder vtable)
    if (g_dword_20FD080 != 0)
    {
        // (the original dispatches the previous holder's vtable slot 2 --
        //  dormant: placeholder vtable in this build)
    }
    g_dword_20FD080 = (DWORD)v2;
    // (the vt[2] dispatch on the fresh VFS -- dormant here)

    // 3. the ANSI string (the slot is always null in this build -> default)
    char **slot = (char **)*a1;                           // *a1 = the slot content (char **)
    const char *src = (slot != 0) ? *slot : "";             // **a1 = the ANSI string
    F3WString str;
    F3WStringFromAnsi(&str, src);

    // 4. the 68-byte record build (sub_56E2A0)
    BYTE rec[68];
    memset(rec, 0, sizeof(rec));
    SpeechBankRecordBuild(&str, rec, g_dword_20FD080);

    // 5. the source wide-string buffer (the cap >= 8 LHFree idiom)
    if (str.cap >= 8)
        LHFree((void *)str.u.ptr);

    // 6. the file-subsystem mount collection (sub_18EC4D0 -- stubbed)
    void *fs = GetFileSubsystem(0);                       // sub_1801C20
    void *entry = 0;
    FileSubsysMountCollection(fs, &entry,
                              (unsigned char (__cdecl *)(int, int))SpeechBankMountCallback,
                              (int)rec);                  // sub_1801620
    if (entry != 0)
        ((void (WINAPI *)(void *))F3_VT(entry)[2])(entry);   // the entry's vtable slot 2 (stdcall)

    // 7. the vt[6] collection-iterator loop -- dormant in this build: the
    //    fresh VFS carries a placeholder vtable, so the original's vt[6]
    //    dispatch (the collection iterator) cannot be performed safely.  The
    //    per-entry vt[4] walk, the per-entry refcount chain, and the sub_18EC4D0
    //    mount callback are all dormant for the same reason.

    // 8. the tail tree-walk (sub_575FD0) over the empty collection range
    Sub_575FD0((g_dword_20FD168 - g_dword_20FD164) >> 3, 0, 0,
               g_dword_20FD164, 0, g_dword_20FD168);

    // 9. the collection release -- skipped (the collection is unavailable)

    // 10. release the record's heap buffer (the cap >= 8 LHFree idiom)
    DWORD recCap = ((DWORD *)rec)[0x18 / 4];               // +0x18 = the record's cap
    if (recCap >= 8)
        LHFree((void *)((DWORD *)rec)[0x04 / 4]);          // +0x04 = the record's data ptr

    return 0;                                              // the LHFree result (opaque)
}

//  BankCacheRangeRelease (0x573390) is defined below this block (the
//  bank-cache slot helper cluster); forward-declare it for the release idiom.
static void BankCacheRangeRelease(DWORD *start, DWORD *end);

//  OutBlockDtor  (0x6240F0 - 0x624153)  -- the 64-byte out-block destructor:
//  releases the bank-cache range at +0x34 (BankCacheRangeRelease over
//  [+0x34, +0x38)) and frees the range pointer, frees the 4-byte self-ref
//  wrapper block at +0x28, zeroes +0x34/+0x38/+0x3C, frees the +0x04 field
//  when +0x18 is >= 8, then resets +0x14=0 / +0x18=7 / +0x04=0 (word).
//  The original is a thiscall function -- ECX = obj, returns eax (the last
//  LHFree call result, opaque; kept as 0 here).
//  [NOT byte-identical] placement-dependent (the relative E8 calls to
//  BankCacheRangeRelease / LHFree).
static int OutBlockDtor(void *obj)
{
    DWORD *this_ = (DWORD *)obj;
    DWORD v2 = this_[0x34 / 4];                 // [this+0x34]
    if (v2 != 0)
    {
        BankCacheRangeRelease((DWORD *)v2, (DWORD *)this_[0x38 / 4]);
        LHFree((void *)this_[0x34 / 4]);        // re-read [this+0x34] after the release
    }

    DWORD selfRefBlock = this_[0x28 / 4];       // [this+0x28] (the 4-byte self-ref wrapper)
    this_[0x34 / 4] = 0;
    this_[0x38 / 4] = 0;
    this_[0x3C / 4] = 0;
    LHFree((void *)selfRefBlock);

    if (this_[0x18 / 4] >= 8)
    {
        DWORD w = this_[0x04 / 4];              // the disasm loads a full dword at +0x04
        LHFree((void *)w);
    }

    this_[0x14 / 4] = 0;
    this_[0x18 / 4] = 7;
    *(WORD *)((BYTE *)this_ + 0x04) = 0;         // the word store (mov [edi+4], dx)

    return 0;                                    // the LHFree result (opaque)
}

//  MountAnimsBank  (0x61B860 - 0x61B91A)  -- mount the "fable2_anims.bnk" bank
//  and register the "data\\" archive over the engine: the wide-string build
//  (OutBlockInit), the file-subsystem FileSubsysMountCollection (0x1801620)
//  mount with the AnimsBankMount callback, the entry vtable slot
//  2 dispatch, the one-shot InitGlobalsOnce registration, the "data\\" shared-slot
//  publish into dword_20FD138 (SetSharedSlot / CopySharedSlot /
//  ReleaseSharedSlot), the GlobalVfsRegistration registration, and the out-block dtor
//  (OutBlockDtor).  [NOT byte-identical] placement-dependent (the E8 calls to the
//  wide-string build / FileSubsysMountCollection / InitGlobalsOnce / SetSharedSlot /
//  CopySharedSlot / ReleaseSharedSlot / GlobalVfsRegistration / OutBlockDtor + the
//  __security_cookie prologue).
static int MountAnimsBank(void *obj)
{
    (void)obj;

    F3WString ws;
    F3WStringFromAnsi(&ws, "fable2_anims.bnk");                 // sub_5AF180 + sub_4357C0

    char outblock[64];                                           // the 64-byte out block (v6)
    OutBlockInit((F3WString *)outblock, &ws, 0);                  // 0x625D10 -> OutBlockInit

    if (ws.cap >= 8)                                              // the cap >= 8 LHFree idiom
        LHFree((void *)ws.u.ptr);

    void *fs = GetFileSubsystem(0);                               // sub_1801C20
    void *entry = 0;                                               // the mount out slot (v3)
    FileSubsysMountCollection(fs, &entry,
                              (unsigned char (__cdecl *)(int, int))AnimsBankMount,
                              (int)outblock);                    // sub_1801620
    if (entry != 0)
        ((void (WINAPI *)(void *))F3_VT(entry)[2])(entry);        // the entry's vtable slot 2 (stdcall)

    InitGlobalsOnce();                                             // InitGlobalsOnce

    F3SharedBuffer *slot = 0;
    SetSharedSlot(&slot, "data\\", -1);                          // sub_1A01CC0
    CopySharedSlot(&g_dword_20FD138, &slot);                       // sub_1A01270
    ReleaseSharedSlot(&slot);                                      // sub_1A01320

    GlobalVfsRegistration(&g_unk_1DBD0E8);                          // sub_18ED640 -> GlobalVfsRegistration

    return OutBlockDtor(outblock);
}

// sub_17FDE20 (0x17FDE20 - 0x17FDE5D) -- release the legacy-file object:
// re-store the CLegacyMemoryFile vtable, release the +8 file handle through
// vtable slot 2 (vt[2]), null +8, release the +4 wide path slot
// (ReleaseSharedSlotWide), swap the vtable to LHFile::CAFile, and return the
// ReleaseSharedSlotWide result.  (The original's second vtable-slot-2 block
// is unreachable -- +8 is nulled immediately after the first dispatch, so the
// re-read of +8 can never be non-null -- so it is omitted.)  [NOT
// byte-identical] placement-dependent (the E8 calls) + the high-VA vtable refs.
static LONG ReleaseLegacyMemoryFile(void *obj)
{
    DWORD *o = (DWORD *)obj;
    o[0] = (DWORD)g_CLegacyMemoryFileVtable;
    if (o[2] != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)o[2])[2])((void *)o[2]);
    o[2] = 0;
    LONG r = ReleaseSharedSlotWide((F3SharedBuffer **)(o + 1));
    o[0] = (DWORD)g_CAFileVtable;
    return r;
}

// 0x20FC310 (dword_20FC310): the wide-load state slot -- a 4-byte length word
// at +4 (the wide-load block: NULL pre-init, a F3SharedBuffer * in this build).
static F3SharedBuffer *g_dword_20FC310 = 0;
// 0x1DB1114 (dword_1DB1114): the 4-byte element published by WideLoadStream
// (the byte-swapped stream word).
static DWORD g_dword_1DB1114 = 0;
// 0x1DB113C (dword_1DB113C): the owner-list container -- self-referencing:
// +0 = &container, +4 = the head node (cleared to the container itself after
// the sub_5493E0 pass), +8 = &container.
static DWORD g_dword_1DB113C[3] = { 0 };
// 0x1DB1140 (dword_1DB1140): the wide-load flag -- 0 while the container is
// armed, written 0 by WideLoadStream.
static DWORD g_dword_1DB1140 = 0;

// 0x403AF0 (SharedBufferBlocksEqual) -- the counted-buffer equality helper
// (sub_403AF0): the +4 lengths are equal and the data strings compare equal
// (the generic _strcmp byte-wise C string compare).  Replicated here (the
// original's call target) because the XLip module's copy is static to it.
static int BlocksEqual(F3SharedBuffer *a, F3SharedBuffer *b)
{
    return (a->length == b->length) && (strcmp(a->data, b->data) == 0);
}

// sub_1893EB0 (0x1893EB0 - 0x1893EB0)  [remaining stub] -- the wide-load block
// build over the wide-load state slot (the SetSharedSlot / vftable dispatch /
// heap-table free chain is out of scope).  Writes 0 into the out slot.
static void sub_1893EB0(F3SharedBuffer **out, int a2)
{ (void)a2; *out = 0; }

// sub_1891540 (0x1891540 - 0x1891540)  [remaining stub] -- the 4-byte element
// load over the 40-byte stream object (the memcpy / vtable dispatch chain is
// out of scope).  Writes 0 into the out slot.
static int sub_1891540(void *this_, DWORD *out, int a3)
{ (void)this_; (void)a3; *out = 0; return 0; }

// sub_19230E0 / sub_1913F40 / sub_1913690 / sub_1912E10 / sub_19124C0 (the
// wide-load dispatch chain over the stream object)  [remaining stubs] -- the
// per-family owner-list destroys + the +0x14/+0x0C/+0x04 4-byte consumption.
// Out of scope; no observable effect.
static void sub_19230E0(void *this_) { (void)this_; }
static void sub_1913F40(void *this_) { (void)this_; }
static void sub_1913690(void *this_) { (void)this_; }
static void sub_1912E10(void *this_) { (void)this_; }
static void sub_19124C0(void *this_) { (void)this_; }

// sub_5493E0 (0x5493E0 - 0x5493F1)  [remaining stub] -- the owner-list
// destroyer: walks the head node (the stack arg) and its +8 children, freeing
// the counted blocks at +0x14 and the node itself through the heap table.
// Out of scope -- the node model is not implemented in this build.
static void sub_5493E0(void *head) { (void)head; }

//  WideLoadStreamWork  -- the work path of WideLoadStream (the part reached
//  when the freshly-built block still equals the stored one): the 4-byte
//  element consumption (the +0x14 count / +0x0C data pointer / +0x04 counter),
//  the +0x18 flag byte-swap, the dword_1DB1114 publish, the five-dispatch
//  chain, the owner-list cleanup, and the freshly-built block's refcount drop.
static void WideLoadStreamWork(DWORD *o, F3SharedBuffer *v3)
{
    DWORD v4 = o[5];                                             // +0x14: the 4-byte element count
    DWORD element;
    if (v4 < 4)
    {
        element = 0;
        sub_1891540(o, &element, 4);                             // sub_1891540 [remaining stub]
    }
    else
    {
        element = *(DWORD *)o[3];                                // +0x0C: the first element (a 4-byte word)
        o[1] += 4;                                               // +0x04: the element counter
        o[3] = (DWORD)((BYTE *)o[3] + 4);                        // +0x0C: the data pointer + 4
        o[5] = v4 - 4;                                           // +0x14: the count
    }

    LONG v8 = (LONG)element;
    if (*(BYTE *)((BYTE *)o + 24) != 0)
    {
        BYTE e0 = (BYTE)element;
        BYTE e1 = (BYTE)(element >> 8);
        BYTE e2 = (BYTE)(element >> 16);
        BYTE e3 = (BYTE)(element >> 24);
        v8 = (LONG)(e3 | (e2 << 8) | (e1 << 16) | ((DWORD)e0 << 24));
    }
    g_dword_1DB1114 = (DWORD)v8;

    sub_19230E0(o);                                              // sub_19230E0 [remaining stub]
    sub_1913F40(o);                                              // sub_1913F40 [remaining stub]
    sub_1913690(o);                                              // sub_1913690 [remaining stub]
    sub_1912E10(o);                                              // sub_1912E10 [remaining stub]
    sub_19124C0(o);                                              // sub_19124C0 [remaining stub]

    sub_5493E0((void *)g_dword_1DB113C[1]);                      // sub_5493E0 [remaining stub]
    g_dword_1DB113C[1] = (DWORD)g_dword_1DB113C;                 // +4 = &container
    g_dword_1DB1140 = 0;
    g_dword_1DB113C[0] = (DWORD)g_dword_1DB113C;                 // +0 = &container
    g_dword_1DB113C[2] = (DWORD)g_dword_1DB113C;                 // +8 = &container

    if (v3 != 0 && InterlockedDecrement((volatile LONG *)((BYTE *)v3 + 12)) <= 0)
        SharedBufferFree(v3);
}

//  WideLoadStream  (0x1922280 - 0x19223F2)  -- the wide-load step over the
//  40-byte NFile::CFileDataInputStream object (the dword_20FC310 state read
//  + the sub_1893EB0 block build + SharedBufferBlocksEqual + the 4-byte
//  element consumption (sub_1891540) + the byte-swap on the +0x18 flag + the
//  five sub-dispatch chain (sub_19230E0 / sub_1913F40 / sub_1913690 / sub_1912E10 /
//  sub_19124C0) + the sub_5493E0 owner-list cleanup over dword_1DB113C + the
//  freshly-built block's refcount drop).  The original is __thiscall + retn 0
//  (this in ECX, no stack args, result in EAX) and uses the __security_cookie
//  prologue.  The deep dispatch chain is [remaining stub]s in this build, so
//  the function reduces to: read the wide-load state, build the block (stub
//  -> null), take the null-return path (or, when the freshly-built block still
//  equals the stored one, the field-consuming path with a dormant swizzle),
//  and drop the live-slot counter.  [NOT byte-identical] placement-dependent
//  (the E8 calls to sub_1893EB0 / sub_1891540 / the dispatch chain / sub_5493E0
//  + the InterlockedDecrement IAT slot + the high-VA globals).
static LONG WideLoadStream(void *this_)
{
    DWORD *o = (DWORD *)this_;
    LONG v2 = (g_dword_20FC310 != 0) ? (LONG)*(DWORD *)((BYTE *)g_dword_20FC310 + 4) : 0;
    F3SharedBuffer *v9 = 0;
    sub_1893EB0(&v9, v2);                                        // sub_1893EB0 [remaining stub]
    F3SharedBuffer *v3 = v9;                                     // the built block (for the refcount drop)
    g_dword_1DB1114 = 0;
    if ((F3SharedBuffer *)v9 != g_dword_20FC310)
    {
        if (v9 == 0)
            return InterlockedDecrement(&g_Addend);
        if (g_dword_20FC310 != 0 && BlocksEqual(v9, g_dword_20FC310))
        {
            WideLoadStreamWork(o, v3);                          // the 4-byte consume + dispatch chain
        }
        else
        {
            if (InterlockedDecrement((volatile LONG *)((BYTE *)v3 + 12)) <= 0)
                SharedBufferFree(v3);
        }
        return InterlockedDecrement(&g_Addend);
    }

    WideLoadStreamWork(o, v3);                                   // v9 == stored: the work path directly
    return InterlockedDecrement(&g_Addend);
}


//  FileDataInputStreamDtor  (0x18917A0 - 0x1891801)  -- the NFile::CFileDataInputStream
//  finalize / destructor step.
//  [NOT byte-identical] placement-independent, but refs the heap-table pointer
//  global (0x02075B24) at the original high VA not present in this build.
//
//  int FileDataInputStreamDtor(void *obj)
//
//  Frees the 0x8000-byte read buffer (+0x20) through the heap-table free slot
//  (the +0x24 size word rides along and is nulled too), then -- when the
//  legacy-file object at +0x1C answers non-zero from its vtable slot 10
//  (offset 0x28) -- reads the stream's vtable slot 2 (offset 8, thiscall on
//  the stream) and hands the result to the legacy-file object's vtable slot 5
//  (offset 0x14, thiscall with one stack arg).  Nulls +0x1C / +0x04 / +0x08 /
//  +0x0C / +0x10 / +0x14 and returns the result.  The original body (97 bytes,
//  56 8B F1 ... C3):
//      push esi; mov esi, ecx
//      mov eax, [esi+20h]
//      push edi; xor edi, edi
//      cmp eax, edi
//      jz short loc_18917C1
//      push eax
//      mov eax, dword_2075B24
//      mov ecx, [eax+10h]
//      call ecx           ; the heap-table free slot (+16), with +0x20 as the arg
//      add esp, 4
//      mov [esi+20h], edi
//      mov [esi+24h], edi
//  loc_18917C1:
//      mov ecx, [esi+1Ch]
//      cmp ecx, edi
//      jz short loc_18917EC
//      mov edx, [ecx]         ; legacy-file vftable
//      mov eax, [edx+28h]     ; vt slot 10 (offset 0x28)
//      call eax               ; thiscall: the legacy-file object as `this`
//      test al, al
//      jz short loc_18917EC
//      mov ecx, [esi+1Ch]
//      mov edx, [esi]         ; the stream's vftable
//      mov eax, [edx+8]       ; vt slot 2 (offset 8)
//      push ebx
//      mov ebx, [ecx]         ; the legacy-file vftable
//      mov ecx, esi           ; the stream object as `this`
//      call eax               ; result = vt[2](stream)
//      mov ecx, [esi+1Ch]
//      mov edx, [ebx+14h]     ; vt slot 5 (offset 0x14)
//      push eax               ; the vt[2] result as the stack arg
//      call edx               ; result = vt[5](legacyFile, vt[2](stream))
//      pop ebx
//  loc_18917EC:
//      mov [esi+1Ch], edi
//      mov [esi+4], edi
//      mov [esi+8], edi
//      mov [esi+10h], edi
//      mov [esi+14h], edi
//      mov [esi+0Ch], edi
//      pop edi
//      pop esi
//      retn
//
//  Note: the original returns the free-slot call's int result when the +0x1C
//  branch is skipped (or never reached); the project's heap-table model (the
//  common/ module) is a void free slot, so that return path yields 0.  The
//  second branch's dispatches are reproduced exactly (vtables, slot order,
//  this/stack args, eax as `result`).
// ============================================================
static int FileDataInputStreamDtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    int result = 0;

    int buffer = o[8];                 // +0x20
    if (buffer != 0)
    {
        LHFree((void *)buffer);        // the heap-table free slot (+16)
        o[8] = 0;                      // +0x20
        o[9] = 0;                      // +0x24
    }

    void *legacyFile = (void *)o[7];    // +0x1C
    if (legacyFile != 0)
    {
        int v10 = (int)((int (WINAPI *)(void *))F3_VT(legacyFile)[10])(legacyFile);
        if ((BYTE)v10 != 0)
        {
            int v5 = (int)((int (WINAPI *)(void *))F3_VT(obj)[2])(obj);
            result = (int)((int (WINAPI *)(void *, int))F3_VT(legacyFile)[5])(legacyFile, v5);
        }
        else
        {
            result = v10;
        }
    }

    o[1] = 0;                          // +0x04
    o[2] = 0;                          // +0x08
    o[3] = 0;                          // +0x0C
    o[4] = 0;                          // +0x10
    o[5] = 0;                          // +0x14
    o[7] = 0;                          // +0x1C
    return result;
}

// Placeholder vtables for the stream object's dead vtable stores (the original
// refs the NFile::CFileDataInputStream / NTools::CBaseClass vftables at their
// .rdata VAs; the stream object is discarded before anything dispatches
// through it).
static void *g_CFileDataInputStreamVtable[10] = { 0 };   // 0x1BF90CC
static void *g_NToolsCBaseClassVtable[10]    = { 0 };    // 0x1AF6E74

//  StreamWindowAdvance  (0x1891170 - 0x18911BF)  -- the stream-window advance
//  over the 40-byte NFile::CFileDataInputStream object (the window-reset
//  idiom): given a new absolute offset a2, either shifts the stream window
//  forward -- the data pointer (+0x0C) advances by (a2 - the +0x04 counter),
//  the remaining count (+0x14) shrinks, and the counter (+0x04) becomes a2 --
//  when the buffer is non-null and a2 lies within [+0x10, +0x04 + +0x14], or
//  resets the window (+0x0C / +0x10 / +0x14 -> 0, +0x04 -> a2) otherwise.
//  Returns the pre-call +0x0C (the buffer pointer, or the new data pointer in
//  the accept branch); the caller drops it.  [NOT byte-identical] (the C++
//  body's operand order / register allocation differs from the original prologue
//  and refs no high-VA globals beyond the object layout).
static unsigned int StreamWindowAdvance(void *stream, unsigned int a2)
{
    DWORD *o = (DWORD *)stream;
    unsigned int result = o[3];                                     // +0x0C: the data pointer
    if (result != 0 && a2 >= o[4] && a2 <= o[1] + o[5])             // +0x10 <= a2 <= +0x04 + +0x14
    {
        unsigned int v3 = o[1];                                     // +0x04: the counter
        unsigned int v4 = v3 + o[5];                                // the window end
        o[5] = v4 - a2;                                            // +0x14: the remaining count
        result = a2 + result - v3;                                  // +0x0C: the new data pointer
        o[3] = result;                                              // +0x0C
        o[1] = a2;                                                  // +0x04: the new counter
    }
    else
    {
        o[3] = 0;                                                   // +0x0C
        o[4] = 0;                                                   // +0x10
        o[5] = 0;                                                   // +0x14
        o[1] = a2;                                                  // +0x04
    }
    return result;
}

//  FileDataInputStreamCtor  (0x1892420 - 0x1892496)  -- the NFile::CFileDataInputStream
//  read-buffer ctor: zero the object, install the CFileDataInputStream vftable
//  and the legacy-file object pointer (+0x1C), and -- when size > 0 -- allocate
//  the size-byte read buffer through LHAlloc, store it at +0x20 / +0x24, dispatch
//  the legacy-file object's vtable slot 9 (offset 0x24) and store the result at
//  +0x08, point +0x0C at the new buffer, reset the +0x04/+0x10/+0x14 counters,
//  and hand the legacy-file object's vtable slot 7 (offset 0x1C) result to
//  StreamWindowAdvance.  The stream object is discarded at the end of
//  OpenDataStreamFile (the wide-load + finalize steps run against it).
//  [NOT byte-identical] placement-dependent (the E8 calls to LHAlloc, the two
//  vt[9]/vt[7] thiscall dispatches, and the StreamWindowAdvance call) + refs
//  the CFileDataInputStream vftable placeholder at its .rdata VA.
static void *FileDataInputStreamCtor(void *stream, void *legacyFile, int size)
{
    DWORD *o = (DWORD *)stream;
    o[1] = 0;                                                       // +0x04
    o[2] = 0;                                                       // +0x08
    o[3] = 0;                                                       // +0x0C
    o[4] = 0;                                                       // +0x10
    o[5] = 0;                                                       // +0x14
    *(BYTE *)((BYTE *)stream + 24) = 0;                             // +0x18
    o[0] = (DWORD)g_CFileDataInputStreamVtable;                     // +0x00
    o[7] = (DWORD)legacyFile;                                       // +0x1C
    o[8] = 0;                                                       // +0x20
    o[9] = 0;                                                       // +0x24
    if (size > 0)
    {
        void *buffer = LHAlloc((unsigned int)size);                 // LHAlloc(size)
        o[8] = (DWORD)buffer;                                       // +0x20
        o[9] = (DWORD)size;                                         // +0x24
        int v4 = (int)((int (WINAPI *)(void *))F3_VT(legacyFile)[9])(legacyFile);   // vt[9] slice
        o[3] = o[8];                                               // +0x0C = the buffer
        o[2] = (DWORD)v4;                                          // +0x08
        o[1] = 0;                                                   // +0x04
        o[4] = 0;                                                   // +0x10
        o[5] = 0;                                                   // +0x14
        int v5 = (int)((int (WINAPI *)(void *))F3_VT(legacyFile)[7])(legacyFile);   // vt[7] slice
        StreamWindowAdvance(stream, (unsigned int)v5);
    }
    return stream;
}


// OpenDataStreamFile  (0x1926050 - 0x1926119)  -- open the settings-directory
// "particle_bank.bnk" path (the slot's block data) as a data-stream file:
// build the ANSI shared slot from the path (or the empty default string when
// the slot is null), open it as an LHSFS::CLegacyMemoryFile (CLegacyMemoryFileCtor,
// sub_17FE8B0, mode 1 + flag 2), drop the local slot reference (the
// InterlockedDecrement-to-zero -> SharedBufferFree idiom), drop the global
// live-slot counter, then release the legacy-file object (ReleaseLegacyMemoryFile,
// sub_17FDE20) and return its result.  The stream-build step (FileDataInputStreamCtor,
// 0x1892420) is implemented here -- the 40-byte NFile::CFileDataInputStream object,
// the 0x8000-byte LHAlloc read buffer, and the vt[9] / vt[7] thiscall dispatches
// over the legacy-file object (the vt[9] / vt[7] slots are unimplemented stubs in
// this build -- LegacyMemoryFileStubMethod returns 0) and the StreamWindowAdvance
// window-advance helper (0x1891170).  The wide-load step (WideLoadStream, 0x1922280)
// is implemented
// here -- its deep dispatch chain (sub_1893EB0 / sub_1891540 / sub_19230E0 /
// sub_1913F40 / sub_1913690 / sub_1912E10 / sub_19124C0 / sub_5493E0) is [remaining
// stub]s, so the stream-consuming path stays dormant.  The stream-object finalize
// step (FileDataInputStreamDtor, 0x18917A0) is implemented.  The stream object is
// local and discarded at the end of
// the function, so the stubs keep the call graph honest without fabricating
// the file reads.  The caller (MountPermanentBanks) discards the return value.
// [NOT byte-identical] placement-dependent (the E8 calls + the Interlocked
// Decrement IAT slot + the high-VA global g_Addend).
static LONG OpenDataStreamFile(F3SharedBuffer **pathSlot)
{
    const char *src = (*pathSlot != 0) ? (*pathSlot)->data : g_ShapeSpawnDefaultStr;   // empty default

    F3SharedBuffer *slot = 0;
    SetSharedSlot(&slot, src, -1);                                                     // sub_1A01CC0

    CLegacyMemoryFile v4;                                                               // the 16-byte object
    CLegacyMemoryFileCtor(&v4, &slot, 1, 2);                                            // sub_17FE8B0

    if (slot != 0)
    {
        if (InterlockedDecrement((volatile LONG *)&slot->refcount) <= 0 && slot != 0)
            SharedBufferFree(slot);
        slot = 0;
    }
    InterlockedDecrement(&g_Addend);                                                    // (0x2075C48)

    DWORD stream[10];                                                                   // the 40-byte stream object (discarded)
    FileDataInputStreamCtor(&stream, &v4, 0x8000);                                   // 0x1892420
    WideLoadStream(&stream);                                                             // 0x1922280
    stream[0] = (DWORD)g_CFileDataInputStreamVtable;                                     // the CFileDataInputStream vftable
    FileDataInputStreamDtor(&stream);                                                 // 0x18917A0
    stream[0] = (DWORD)g_NToolsCBaseClassVtable;                                         // the CBaseClass vftable (dead write)

    return ReleaseLegacyMemoryFile(&v4);                                                 // sub_17FDE20
}

//  FindBankCacheEntry  (0x622E70 - 0x622F16)  -- the bank-cache lookup over the
//  engine's +0xC8 sub-object (the 44-byte sentinel/head block + 4-byte head node +
//  the +0x1C count, the hash-keyed red-black tree the UpdateBankCacheEntry /
//  UpdateBankCacheSlot update side descends into): walks the tree keyed by the wide-string hash
//  (the dword at +0x0C of each node; the tree is empty at this point, so the
//  sentinel -- flagged by byte at +0x29 -- terminates the loop immediately) and
//  writes the {node, block} pair it finds into *out.  The mount path
//  (MountLipsyncBank) gates its work on that pair: out[0] must equal the +0xC8
//  node and out[1] must equal the block pointer.  [NOT byte-identical]
//  placement-dependent (the relative calls to F3InvalidParam / the tree walk).
static DWORD *FindBankCacheEntry(void *obj, DWORD *out, const int *hash)
{
    DWORD *sub = (DWORD *)obj;                  // the +0xC8 sub-object
    DWORD *block = (DWORD *)sub[6];             // the 44-byte sentinel block (+0x18)
    DWORD *node = block;                        // v6: the node this lookup last visited
    DWORD *cur = (DWORD *)(block[1]);           // v5: the second dword of the block (the sentinel, self-ref)

    while (*(BYTE *)((BYTE *)cur + 0x29) == 0)
    {
        if ((unsigned int)cur[3] >= (unsigned int)*hash)
        {
            node = cur;
            cur = (DWORD *)cur[0];
        }
        else
        {
            cur = (DWORD *)cur[2];
        }
    }

    DWORD v7 = sub[0];                           // the +0xC8 node (back-referencing)
    if (v7 == 0)
        F3InvalidParam();                        // original: __invalid_parameter_noinfo (no return)

    DWORD v13[2] = { v7, (DWORD)block };
    DWORD v14[2] = { v7, (DWORD)node };
    DWORD *v9;

    if (node == block || (unsigned int)*hash < (unsigned int)node[3])
        v9 = v13;
    else
        v9 = v14;

    out[0] = v9[0];
    out[1] = v9[1];
    return out;
}

//  sub_624FA0  (0x624FA0 - 0x625019)  [remaining stub] -- the bank-cache node
//  init: the LHAlloc(4) self-ref block (*block = the this pointer, this[0] = the
//  block), the BankCacheSlotReinit capacity reset over the (v16[4] - v16[3]) >> 2
//  count, and the sub_621AD0 node copy.  Deep dependency chain (sub_621AD0) -- out
//  of scope; the this pointer is kept unchanged.  [remaining stub]
static DWORD *sub_624FA0(DWORD *this_block, DWORD *owner_wrapper)
{
    (void)this_block; (void)owner_wrapper;
    return this_block;
}

//  sub_626AA0  (0x626AA0 - 0x626C35)  [remaining stub] -- the bank-cache node
//  build: the tree insert over the +0xC8 sub-object (sub_626510 / sub_626910 /
//  sub_61E430 / sub_61E160 / sub_61E610 -- all unimplemented).  Keeps the out-block
//  at the tree-walk result so the caller's final validity checks stay in-bounds.  [remaining stub]
static DWORD *sub_626AA0(void *obj, DWORD *out_block, DWORD *a2, DWORD *a3, DWORD *a4)
{
    (void)a2; (void)a4;
    out_block[0] = (DWORD)obj;
    out_block[1] = (DWORD)a3;
    return out_block;
}

//  UpdateBankCacheEntry  (0x626D20 - 0x626EB2)  -- the bank-cache update (the
//  +0xC8 sub-object tree insert: the 4-byte owner wrapper, the sub_624FA0 node
//  init, the sub_626AA0 node build, the BankCacheRangeRelease + LHFree release
//  idioms).  The write-side sibling of FindBankCacheEntry (0x622E70): walks the
//  wide-string hash over the hash-keyed red-black tree (the dword at +0x0C of each
//  node; the sentinel -- flagged by byte at +0x29 -- terminates the walk
//  immediately when the tree is empty) and, when the walk lands on the block or
//  the hash does not fit the current use, rebuilds the cache node: the 4-byte
//  owner wrapper (LHAlloc(4), *wrapper = &v16, v16[0] = wrapper, self-referenced,
//  freed at the end), the sub_624FA0 node init, the sub_626AA0 node build, the
//  re-zero + LHFree idioms, and the final validity checks.  [NOT byte-identical]
//  placement-dependent (the relative E8 calls to LHAlloc / LHFree /
//  BankCacheRangeRelease / sub_624FA0 / sub_626AA0 + the __security_cookie
//  prologue).
static DWORD *UpdateBankCacheEntry(void *obj, DWORD *hash_ptr)
{
    DWORD *sub = (DWORD *)obj;                  // the +0xC8 sub-object
    DWORD *block = (DWORD *)sub[6];             // the 44-byte sentinel block (+0x18)
    DWORD *node = block;                         // the node this walk last visited
    DWORD *cur = (DWORD *)block[1];             // the sentinel node (self-ref)
    int hash = *(int *)hash_ptr;                 // the wide-string hash

    while (*(BYTE *)((BYTE *)cur + 0x29) == 0)
    {
        if ((unsigned int)cur[3] >= (unsigned int)hash)
        {
            node = cur;
            cur = (DWORD *)cur[0];
        }
        else
        {
            cur = (DWORD *)cur[2];
        }
    }

    DWORD *v7 = (DWORD *)sub[0];                // the +0xC8 node (back-referencing)
    if (v7 == 0)
        F3InvalidParam();                       // original: __invalid_parameter_noinfo (no return)

    DWORD *v25 = (DWORD *)node;                  // the tree-walk node

    if (node == block || (unsigned int)hash < (unsigned int)node[3])
    {
        // === the owner wrapper (the 4-byte self-ref block, freed at the end) ===
        DWORD v16[4] = { 0, 0, 0, 0 };
        DWORD *v9 = (DWORD *)LHAlloc(4);
        if (v9 != 0)
        {
            *(DWORD **)v9 = (DWORD *)v16;
            v16[0] = (DWORD)v9;
        }

        // === the this block (the block sub_624FA0 fills; this = &thisBlock) ===
        DWORD thisBlock[5] = { 0, 0, 0, 0, 0 };

        // === the out-param slot (passed to sub_626AA0 as a4) + the hash slot ===
        DWORD outParam = 0;                      // the out-param (the disasm's [esp+0x2C] slot)
        DWORD hashSlot = (DWORD)hash;            // the hash slot (the disasm's [esp+0x30] slot)

        sub_624FA0(thisBlock, v16);              // the node init [remaining stub]

        DWORD outBlock[2] = { 0, 0 };            // the out-block (a1) -- overwritten by sub_626AA0
        sub_626AA0(sub, outBlock, v7, node, &outParam);  // the node build [remaining stub]

        v7 = (DWORD *)outBlock[0];
        v25 = (DWORD *)outBlock[1];

        // === the release idioms (the disasm's release-and-free sequence) ===
        if (thisBlock[2] != 0)
        {
            BankCacheRangeRelease((DWORD *)thisBlock[2], (DWORD *)thisBlock[3]);
            LHFree((void *)thisBlock[2]);
        }
        thisBlock[3] = 0;
        thisBlock[4] = 0;

        // === the hash slot (the disasm's second LHFree call) ===
        LHFree((void *)hashSlot);

        // === the owner wrapper free ===
        if (v16[0] != 0)
            LHFree((void *)v16[0]);
    }

    if (v7 != 0)
        v7 = (DWORD *)*v7;                       // deref the out-block's "this"-ish pointer
    else
        F3InvalidParam();

    if (v25 == (DWORD *)v7[6])
        F3InvalidParam();

    return v25 + 4;                              // the original returns v14 + 4 (the disasm: +0x10)
}


//  BankCacheRangeRelease  (0x573390 - 0x5733BA)  -- release a range of DWORD
//  slots: for each non-null element, call its vtable slot 2 (vt+8, Release)
//  with the element pointer (a stdcall wrapper in the original, kept as a free
//  function).  [NOT byte-identical] placement-dependent (the relative E8 call
//  to the vt[2] dispatch).
static void BankCacheRangeRelease(DWORD *start, DWORD *end)
{
    for (DWORD *p = start; p != end; ++p)
    {
        if (*p != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)*p)[2])((void *)*p);
    }
}

//  BankCacheCopyRange  (0x61F940 - 0x61F98A)  -- the range copy with ownership
//  transfer: AddRef each source element through its vtable slot 1 (vt+4),
//  Release the old destination element through slot 2 (vt+8), then store the
//  pointer into the destination.  Returns the new destination end.  [NOT
//  byte-identical] placement-dependent (the relative E8 calls to the vt[1] /
//  vt[2] dispatches).
static DWORD *BankCacheCopyRange(const DWORD *src, const DWORD *srcEnd, DWORD *dest)
{
    DWORD *v3 = (DWORD *)src;
    if (v3 == (DWORD *)srcEnd)
        return dest;
    DWORD *v4 = dest;
    do
    {
        if (*v3 != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)*v3)[1])((void *)*v3);   // AddRef the source
        if (*v4 != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)*v4)[2])((void *)*v4);   // Release the old dest slot
        *v4++ = *v3++;
    }
    while (v3 != (DWORD *)srcEnd);
    return v4;
}

//  BankCacheCopyRangeEnd  (0x621870 - 0x6218B5)  -- copy the range into the
//  destination and return the new end (the original returns dest + 4 *
//  ((srcEnd - src) >> 2) with byte-based addresses; for a DWORD-aligned range
//  that is dest + element count).  [NOT byte-identical]
//  placement-dependent (the relative E8 call to BankCacheCopyRange).
static DWORD *BankCacheCopyRangeEnd(const DWORD *src, const DWORD *srcEnd, DWORD *dest)
{
    BankCacheCopyRange(src, srcEnd, dest);
    return dest + (srcEnd - src);
}

//  BankCacheAppendRange  (0x5772D0 - 0x57730E)  -- append a range of element
//  pointers into a destination block at the end of the current use: for each
//  source element, store it into the slot and AddRef it through its vtable
//  slot 1 (vt+4).  Returns the new end.  [NOT byte-identical]
//  placement-dependent (the relative E8 call to the vt[1] dispatch).
static DWORD *BankCacheAppendRange(const DWORD *src, const DWORD *srcEnd, DWORD *dest)
{
    DWORD *v3 = (DWORD *)src;
    DWORD *v4 = dest;
    if (v3 == (DWORD *)srcEnd)
        return v4;
    do
    {
        if (v4 != 0)                                             // defensive: dest is always non-null here
        {
            DWORD v5 = *v3;
            *v4 = v5;
            if (v5 != 0)
                ((void (WINAPI *)(void *))F3_VT((void *)v5)[1])((void *)v5);  // AddRef the source
        }
        ++v3;
        ++v4;
    }
    while (v3 != (DWORD *)srcEnd);
    return v4;
}

//  BankCacheAppendBlock  (0x622A10 - 0x622A3C)  -- the stdcall shallow wrapper
//  over BankCacheAppendRange (a1, a2, a3) -> new end.  [NOT byte-identical]
//  placement-dependent (the relative E8 call).
static DWORD *BankCacheAppendBlock(const DWORD *src, const DWORD *srcEnd, DWORD *dest)
{
    return BankCacheAppendRange(src, srcEnd, dest);
}

//  BankCacheAllocElements  (0x575A00 - 0x575A6E)  -- allocate 4 * count bytes
//  through the game heap; the original throws stdext::bad_alloc when the size
//  overflows (0xFFFFFFFF / count < 4).  Unreachable for sane bank slots;
//  emulated with a plain C++ throw.
static int BankCacheAllocElements(unsigned int count)
{
    unsigned int v1 = count;
    if (count != 0)
    {
        if (0xFFFFFFFFu / count < 4u)
            throw 1;   // the original throws stdext::bad_alloc
    }
    else
    {
        v1 = 0;
    }
    return (int)LHAlloc(4 * v1);
}

//  BankCacheSlotReinit  (0x571BB0 - 0x571BFF)  -- reset the slot's capacity
//  block: zero the +0x0C/+0x10/+0x14 list words, then bind the slot to a fresh
//  4-byte-per-element buffer of `count` elements (this + 4 * count = the new
//  end).  Returns 0 when count == 0, 1 on success.  The original's
//  length_error path (count > 0x3FFFFFFF) and the bad_alloc overflow are
//  emulated with a plain C++ throw.  [NOT byte-identical]
//  placement-dependent (the relative E8 calls to BankCacheAllocElements + the
//  LHAlloc dispatch).
static char BankCacheSlotReinit(DWORD *obj, unsigned int count)
{
    obj[3] = 0;
    obj[4] = 0;
    obj[5] = 0;
    if (count == 0)
        return 0;
    if (count > 0x3FFFFFFFu)
        throw 1;   // the original throws std::length_error("vector<T> too long")
    DWORD v4 = (DWORD)BankCacheAllocElements(count);
    obj[3] = v4;
    obj[4] = v4;
    obj[5] = v4 + 4 * count;
    return 1;
}

//  BankCacheSlotReset  (0x623420 - 0x62345F)  -- reset the slot to the empty
//  state: validate (obj, obj+0, obj+3, obj+4), then, when the current range
//  is non-empty, Release the elements over [obj+3, obj+4) and point the end
//  word at the start.  The original folds in the sub_622FF0 idiom (the dead
//  8-byte out-param is discarded by its only caller, so it carries no
//  observable state).  [NOT byte-identical] placement-dependent (the relative
//  E8 call to BankCacheRangeRelease).
static DWORD *BankCacheSlotReset(DWORD *obj)
{
    if (obj == 0 || obj[3] > obj[4])
        F3InvalidParam();                        // original: __invalid_parameter_noinfo
    if (obj[0] == 0)
        F3InvalidParam();                        // original: __invalid_parameter_noinfo
    if (obj[3] != obj[4])
    {
        BankCacheRangeRelease(obj + 3, obj + 4);
        obj[4] = obj[3];
    }
    return obj;
}

//  UpdateBankCacheSlot  (0x6254B0 - 0x6255B4)  -- the bank-cache slot helper:
//  the dest/source copy-or-grow over the +0x0C/+0x10/+0x14 list words
//  (+0x0C start / +0x10 end / +0x14 capacity end).  When the new source range
//  is empty, reset the slot; when it fits the current use, copy the new range
//  and Release the old tail; when it fits the remaining capacity, copy the
//  prefix and append the remainder; otherwise Release and free the old range,
//  re-bind the slot to a fresh allocation, and append the full range.  [NOT
//  byte-identical] placement-dependent (the relative E8 calls to
//  BankCacheRangeRelease / BankCacheCopyRangeEnd / BankCacheAppendBlock /
//  BankCacheSlotReinit / BankCacheSlotReset / LHFree).
static DWORD *UpdateBankCacheSlot(DWORD *obj, DWORD *srcList)
{
    if (obj == srcList)
        return obj;

    DWORD srcStart = srcList[3];
    DWORD srcEnd = srcList[4];
    DWORD newCount = (srcEnd - srcStart) >> 2;

    if (newCount == 0)
    {
        BankCacheSlotReset(obj);
        return obj;
    }

    DWORD curStart = obj[3];
    DWORD curCount = (obj[4] - obj[3]) >> 2;

    if (newCount <= curCount)
    {
        DWORD *newEnd = BankCacheCopyRangeEnd((const DWORD *)srcStart, (const DWORD *)srcEnd, obj + 3);
        BankCacheRangeRelease(newEnd, obj + 4);
        obj[4] = obj[3] + 4 * newCount;
        return obj;
    }

    DWORD capacity;
    if (curStart != 0)
        capacity = (obj[5] - curStart) >> 2;
    else
        capacity = 0;

    if (newCount <= capacity)
    {
        DWORD srcSplit = srcStart + 4 * curCount;
        BankCacheCopyRangeEnd((const DWORD *)srcStart, (const DWORD *)srcSplit, obj + 3);
        obj[4] = (DWORD)BankCacheAppendBlock((const DWORD *)srcSplit, (const DWORD *)srcEnd, obj + 4);
        return obj;
    }

    if (curStart != 0)
    {
        BankCacheRangeRelease(obj + 3, obj + 4);
        LHFree((void *)(obj + 3));
    }

    if (BankCacheSlotReinit(obj, (unsigned int)newCount) != 0)
        obj[4] = (DWORD)BankCacheAppendBlock((const DWORD *)srcStart, (const DWORD *)srcEnd, obj + 3);

    return obj;
}

//  MountLipsyncBank  (0x61C170 - 0x61C236)  -- mount one .bnk speech / lipsync bank
//  over the engine's +0xC8 sub-object: the GetWideStringHash cache lookup
//  (FindBankCacheEntry), the OutBlockInit wide-string build, the file-subsystem
//  FileSubsysMountCollection (0x1801620) mount with the AnimsBankMount
//  callback, the entry vtable slot 2 dispatch, and the cache update
//  (UpdateBankCacheEntry / UpdateBankCacheSlot).  The deep dependency chain
//  (AnimsBankMount's stubbed leaves) remains stubbed; the out-block dtor runs
//  as the OutBlockDtor implemented above.  [NOT byte-identical] placement-dependent
//  (the relative E8 calls to GetWideStringHash / FindBankCacheEntry / GetFileSubsystem
//  / OutBlockInit / FileSubsysMountCollection / UpdateBankCacheEntry / UpdateBankCacheSlot /
//  OutBlockDtor + the entry vtable slot 2 dispatch + the __security_cookie prologue).
static void MountLipsyncBank(void *obj, F3WString *ws, int a3)
{
    CGameEngine *g = (CGameEngine *)obj;
    char *base = (char *)obj;

    int hash = GetWideStringHash(ws);            // sub_56DFD0

    DWORD v9[2] = { 0, 0 };
    FindBankCacheEntry(base + 0xC8, v9, &hash);   // 0x622E70

    if (v9[0] == 0 || v9[0] != (DWORD)g->sub0xC8Node)
        F3InvalidParam();                         // original: __invalid_parameter_noinfo (no return)

    if (v9[1] == (DWORD)g->sub0xE0Block)
    {
        void *fs = GetFileSubsystem((void *)v9[0]);   // sub_1801C20
        char outblock[64];                              // the 64-byte out block (v10)
        memset(outblock, 0, 64);
        OutBlockInit((F3WString *)outblock, ws, a3);    // 0x625D10 -> OutBlockInit

        FileSubsysMountCollection(fs, (void **)&v9[0],
                                  (unsigned char (__cdecl *)(int, int))AnimsBankMount,
                                  (int)outblock);        // sub_1801620

        if (v9[0] != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)v9[0])[2])((void *)v9[0]);  // the entry's vtable slot 2 (stdcall)

        void *cacheNode = UpdateBankCacheEntry(base + 0xC8, (DWORD *)&hash);   // 0x626D20
        BYTE v11[24];                                       // the uninitialized 24-byte source block (dead)
        memset(v11, 0, 24);
        UpdateBankCacheSlot((DWORD *)cacheNode, (DWORD *)v11);   // 0x6254B0

        OutBlockDtor(outblock);                                // the out-block dtor
    }
}

//  sub_180D3F0 (0x180D3F0 - 0x180D571) -- the per-entry VFS load: hash the path
//  (GetWideStringHash / sub_56DFD0), look the entry up in the archive collection
//  (sub_180F0E0 tree search), and, when the entry is a matching data entry,
//  dispatch the matched provider's reload (the provider vtable slot 4, thiscall:
//  (entry, providerIndex, ...)) with the provider's own ref handling.  Deep
//  dependency chain (GetWideStringHash / sub_180F0E0 / sub_18014F0 / sub_56E080
//  / GetFileSubsystem / GetArchiveSlot / the provider vtable dispatch) -- out of
//  scope.  Stub.  [remaining stub]
static void *VfsEntryLoad(void *collection, void **out, int a2, F3WString *path)
{
    (void)collection; (void)a2; (void)path;
    *out = 0;
    return out;
}

//  sub_18A5490 (0x18A5490 - 0x18A5485) -- the 60-byte out-block builder:
//  LHAlloc(60), then sub_18A53A0(a2), then the vtable-slot-1 dispatch on the
//  result (+0x04, stdcall: (result)).  Deep dependency chain (sub_18A53A0) --
//  out of scope.  Stub.  [remaining stub]
static void *RecordAllocBlock(void **out, int a2)
{
    (void)a2;
    *out = 0;
    return out;
}

//  sub_18A5410 (0x18A5410 - 0x18A5420) -- the 28-byte record-collection op: the
//  region-bound sanity check, the vtable-slot-1 dispatch (+0x04, stdcall:
//  (result)), the sub_5B6670 helper, and the vtable-slot-2 dispatch (+0x08).
//  Deep dependency chain (sub_5B6670) -- out of scope.  Stub.  [remaining stub]
static char RecordCollectionOp(void *handle, int a2)
{
    (void)handle; (void)a2;
    return 0;
}

//  sub_71BDF0 (0x71BDF0 - 0x71BDFE) -- the record-array walk: take the slot at
//  +0 of the record, the [start, end) range of 28-byte entries, and the
//  through-record-array dispatch (the sub_71BD50 helper).  Deep dependency
//  chain (sub_71BD50) -- out of scope.  Stub.  [remaining stub]
static DWORD RecordArrayWalk(void **slot)
{
    (void)slot;
    return 0;
}

//  LookupBnk  (sub_1806060, 0x1806060 - 0x1806067)  -- the bank lookup: the
//  per-entry "levels.bnk" / "streaming.bnk" rdata wide string looked up in the
//  archive.  Declared here (defined below) so the SpeechCallback cluster can
//  call it.  [remaining stub]
static void *LookupBnk(void *out, void *cwdSlot, void *nameBuf, int flags);
static F3SharedBuffer **GetArchiveSlot(F3SharedBuffer **out);

//  SpeechCallback  (0x719A00 - 0x719C7D)  -- the global speech callback:
//  rebuilds the speech / gui state by loading the
//  "language/gui/gui_texture_headers.bnk" and "language/gui/gui_textures.bnk"
//  archives over the engine's VFS archive collection (F3WStringFromAnsi +
//  GetVfsArchiveCollection + VfsEntryLoad + the cap >= 8 LHFree idiom for
//  each build), then registers each entry with the bank lookup (LookupBnk, the
//  sub_1806060 stand-in) -- the returned bank handle is AddRef'd (vtable slot 1)
//  and released (vtable slot 2).  When BOTH banks resolve (bank1 && bank2
//  non-null): allocate the 60-byte record out-block (RecordAllocBlock), run
//  the record-collection op on each bank (RecordCollectionOp), take the archive
//  manager handle from its slot (GetArchiveSlot, AddRef'd) and dispatch the
//  slot-10 ("add archive") thiscall method (archive, record, &wide string, 2, 1)
//  -- then reset the wide string to the empty SSO layout (cap 7 / len 0 / first
//  wchar 0), release the archive handle (vtable slot 2), walk the record array
//  (RecordArrayWalk) and release it (vtable slot 2), then release each bank
//  handle (vtable slot 2) and return 0.  Otherwise (either bank missing):
//  release the resolvable bank handle(s) and return 1.  The end-of-block SSO
//  teardown (blocks A and B) is reproduced inline but never fires in this build
//  (the stubbed callees never allocate, so the caps are always 7/0).  The
//  slot-10 dispatch is dead in this build (the archive slot is null, the
//  success path is never reached) -- it is reproduced faithfully but would
//  crash on a null manager handle exactly like the original.
//  [NOT byte-identical] placement-dependent (the E8 calls to F3WStringFromAnsi /
//  GetVfsArchiveCollection / VfsEntryLoad / LookupBnk / RecordAllocBlock /
//  RecordCollectionOp / GetArchiveSlot / RecordArrayWalk + the vtable dispatches
//  + the __security_cookie prologue of the callee).
static char SpeechCallback(void *this_, int a2, int a3)
{
    (void)a3;

    F3WString ws;                                   // the wide string (headers + textures)
    DWORD entry1 = 0;                               // the per-entry out slot (headers)
    DWORD entry2 = 0;                               // the per-entry out slot (textures)
    DWORD bank1 = 0;                                // the per-bank out slot (headers)
    DWORD bank2 = 0;                                // the per-bank out slot (textures)
    DWORD record = 0;                               // the 60-byte out-block (sub_18A5490's out)
    DWORD archive = 0;                              // the archive handle (GetArchiveSlot's out)
    DWORD thisSlot = (DWORD)this_;                  // the v7[1] this-slot
    DWORD v7[2] = { 0, thisSlot };                  // the 8-byte out-block: v7[0] = handle, v7[1] = this

    // block A / block B -- the end-of-block SSO-like out-blocks (never
    // allocated in this build -- the stubbed callees never write them).
    DWORD blockACap = 7, blockALen = 0, blockAPtr = 0;
    DWORD blockBCap = 0, blockBPtr = 0;

    // --- build 1: "language/gui/gui_texture_headers.bnk" ---
    F3WStringFromAnsi(&ws, "language/gui/gui_texture_headers.bnk");
    GetVfsArchiveCollection();
    VfsEntryLoad(GetVfsArchiveCollection(), (void **)&entry1, a2, &ws);   // sub_180D3F0 [remaining stub]
    if (ws.cap >= 8)                                                      // the cap >= 8 LHFree idiom
        LHFree((void *)ws.u.ptr);

    // --- build 2: "language/gui/gui_textures.bnk" ---
    F3WStringFromAnsi(&ws, "language/gui/gui_textures.bnk");
    GetVfsArchiveCollection();
    VfsEntryLoad(GetVfsArchiveCollection(), (void **)&entry2, a2, &ws);   // sub_180D3F0 [remaining stub]
    if (ws.cap >= 8)                                                      // the cap >= 8 LHFree idiom
        LHFree((void *)ws.u.ptr);

    // --- the bank lookup for each entry ---
    bank1 = (DWORD)LookupBnk(&bank1, (void *)(DWORD)a2, (void *)(DWORD)&entry1, 0);  // sub_1806060 [remaining stub]
    if (bank1 != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)bank1)[1])((void *)bank1);          // the bank's vtable slot 1 (AddRef)
    if (bank1 != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)bank1)[2])((void *)bank1);          // the bank's vtable slot 2 (Release)
    bank2 = (DWORD)LookupBnk(&bank2, (void *)(DWORD)a2, (void *)(DWORD)&entry2, 0);  // sub_1806060 [remaining stub]
    if (bank2 != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)bank2)[1])((void *)bank2);          // the bank's vtable slot 1 (AddRef)
    if (bank2 != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)bank2)[2])((void *)bank2);          // the bank's vtable slot 2 (Release)

    if (bank1 != 0)
    {
        if (bank2 != 0)
        {
            RecordAllocBlock((void **)&record, 0);        // sub_18A5490 [remaining stub]
            RecordCollectionOp((void *)(DWORD)bank1, 0);  // sub_18A5410 [remaining stub]
            RecordCollectionOp((void *)(DWORD)bank2, 0);  // sub_18A5410 [remaining stub]

            archive = (DWORD)(*GetArchiveSlot((F3SharedBuffer **)v7));    // sub_180AC90

            // the reset idiom: empty SSO layout (cap 7 / len 0 / first wchar 0)
            ws.cap = 7; ws.len = 0; ws.u.buf[0] = 0;

            // the slot-10 ("add archive") thiscall dispatch on the archive
            // manager -- dead in this build (the archive slot is null)
            {
                typedef void (__thiscall *F3ArchiveAddArchiveFn)(void *this_, DWORD handle, F3WString *name, int f1, int f2);
                F3ArchiveAddArchiveFn fn = (F3ArchiveAddArchiveFn)F3_VT((void *)archive)[10];
                fn((void *)(DWORD)archive, record, &ws, 2, 1);
            }

            if (ws.cap >= 8)                               // the cap >= 8 LHFree idiom
                LHFree((void *)ws.u.ptr);
            ws.cap = 7; ws.len = 0; ws.u.buf[0] = 0;       // the reset idiom again

            if (v7[0] != 0)
                ((void (WINAPI *)(void *))F3_VT((void *)v7[0])[2])((void *)v7[0]);   // the archive handle's vtable slot 2

            RecordArrayWalk((void **)&record);            // sub_71BDF0 [remaining stub]
            if (record != 0)
                ((void (WINAPI *)(void *))F3_VT((void *)record)[2])((void *)record);   // the record's vtable slot 2

            ((void (WINAPI *)(void *))F3_VT((void *)bank2)[2])((void *)bank2);         // the bank's vtable slot 2
            ((void (WINAPI *)(void *))F3_VT((void *)bank1)[2])((void *)bank1);         // the bank's vtable slot 2

            // the end-of-block SSO reset idiom (blocks A and B) -- dead in this
            // build (the stubbed callees never allocate, so cap is 7/0)
            if (blockACap >= 8)
                LHFree((void *)blockAPtr);
            blockACap = 7; blockALen = 0; blockAPtr = blockAPtr & 0xFFFF0000u;
            if (blockBCap >= 8)
                LHFree((void *)blockBPtr);

            return 0;
        }
    }
    else if (bank2 != 0)
    {
        ((void (WINAPI *)(void *))F3_VT((void *)bank2)[2])((void *)bank2);             // the bank's vtable slot 2
    }

    if (bank1 != 0)
        ((void (WINAPI *)(void *))F3_VT((void *)bank1)[2])((void *)bank1);             // the bank's vtable slot 2

    // the end-of-block SSO reset idiom (blocks A and B) -- dead in this build
    if (blockACap >= 8)
        LHFree((void *)blockAPtr);
    blockACap = 7; blockALen = 0; blockAPtr = blockAPtr & 0xFFFF0000u;
    if (blockBCap >= 8)
        LHFree((void *)blockBPtr);

    return 1;
}

//  FileSubsysInvokeCallback  (0x71B6A0 - 0x71B78B)  -- the file-subsystem
//  collection callback walk: iterate the [begin, end) range at +0x58 / +0x5C
//  of the 0x8C-byte file-subsystem object under the +0x4C self-tag block,
//  locking each entry (vtable slot 1, AddRef), invoking the callback over
//  it (thiscall: ECX = the tag, the locked entry + the data word on the
//  stack), and releasing the entry (vtable slot 2).  A callback return of 0
//  releases the entry and returns; a non-zero return releases the entry and
//  continues the walk.  The MSVC debug-iterator bounds checks (the live
//  begin/end words and the self-tag block at +0x4C) are reproduced as
//  F3InvalidParam calls, matching the original __invalid_parameter_noinfo
//  sequence.  The wrapper at +0x4C is the "tag" kept live for the whole walk.
static DWORD *GetDword1DC27D4(void);   // 0x717C20 (defined below, after MountPermanentBanks)

static void FileSubsysInvokeCallback(void *fs, int tag, char (__thiscall *callback)(void *, int, int), int data)
{
    DWORD *o = (DWORD *)fs;
    DWORD begin = o[0x58 / 4];
    DWORD end = o[0x5C / 4];
    if (begin > end)
        F3InvalidParam();

    DWORD wrapper0 = o[0x4C / 4];
    for (DWORD i = begin; ; i += 4)
    {
        end = o[0x5C / 4];
        if (o[0x58 / 4] > end)
            F3InvalidParam();
        DWORD wrapper = o[0x4C / 4];
        if (wrapper0 == 0 || wrapper0 != wrapper)
            F3InvalidParam();
        if (i == end)
            break;

        DWORD m = *(DWORD *)wrapper;                 // the wrapper's pointee: fs + 0x4C
        if ((DWORD)i >= *(DWORD *)(m + 0x10))        // i >= end (dead in practice)
            F3InvalidParam();

        DWORD element = *(DWORD *)i;
        if (element != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)element)[1])((void *)element);  // vtable slot 1 (Lock)
        if (callback((void *)tag, (int)element, data) == 0)
        {
            if (element != 0)
                ((void (WINAPI *)(void *))F3_VT((void *)element)[2])((void *)element);  // vtable slot 2 (Release)
            return;
        }
        if (element != 0)
            ((void (WINAPI *)(void *))F3_VT((void *)element)[2])((void *)element);  // vtable slot 2 (Release)

        if ((DWORD)i >= *(DWORD *)(m + 0x10))
            F3InvalidParam();
    }
}

//  RegisterSpeechCallback  (0x719C80 - 0x719C9C)  -- the global speech
//  callback registration driver: ensure the global file subsystem is
//  initialized (GetFileSubsystem, 0x1801C20), then invoke the collection
//  callback over the [+0x58, +0x5C) collection with the dword_1DC27D4 state
//  as the tag and data, and the speech callback (SpeechCallback, the
//  sub_719A00 stand-in).  Returns 0 (the original's return slot is left
//  holding whatever the collection walk leaves behind -- the caller
//  discards it).  [NOT byte-identical] placement-dependent (the E8 calls
//  to GetFileSubsystem / the collection walk + the __security_cookie
//  prologue of the callee).
static int RegisterSpeechCallback(void)
{
    void *fs = GetFileSubsystem(0);           // sub_1801C20
    FileSubsysInvokeCallback(fs, (int)GetDword1DC27D4(),
                            (char (__thiscall *)(void *, int, int))SpeechCallback,
                            (int)GetDword1DC27D4());
    return 0;
}

// 0x1DBD0F8 / 0x1DBD0FC: the speech-bank record-array bounds (the [start,
// end) range of 36-byte records SpeechBankTablePass walks).  .bss (both 0
// in this build, so the loop is dead).
static DWORD g_dword_1DBD0F8 = 0;   // 0x1DBD0F8
static DWORD g_dword_1DBD0FC = 0;   // 0x1DBD0FC
// 0x1DDC830: the main (80/4096) allocation-cache object (0x58: the +0x18
// init flag / +0x34 critical section / the +0x10/+0x14 fields).  The SpeechBank
// record-reload chain (SpeechBankRecordReloadAudioBank) references this
// cache before InitVirtualFileSystem runs, so it is declared up here with
// the other speech-bank globals.
static BYTE  g_unk_1DDC830[0x58];   // 0x1DDC830
// 0x1D6A6E8: the audio-comms LoadAudioBank counter (read + incremented by
// the SpeechBankRecordReloadAudioBank chain; the reload writes the
// pre-increment value into the record's +0x08 word).
static DWORD g_dword_1D6A6E8 = 0;   // 0x1D6A6E8
// 0x1C86C50: the "speech-bank table pass" flag byte read by the
// SpeechBankFlagRead prologue.  .rdata-ish; 1 in the original, 0 here (the read is discarded
// by the caller anyway).
static char  g_byte_1C86C50 = 0;    // 0x1C86C50

// SpeechBankFlagRead (0x616650, formerly sub_616650) -- the flag reader: returns byte_1C86C50.
static char SpeechBankFlagRead(void) { return g_byte_1C86C50; }

// SpeechBankStrCompare (0x403F10, formerly sub_403F10) -- the generic byte-wise
// C string compare: 0 equal / -1 less / 1 greater (signed 8-bit bytes
// compared pairwise, length is not part of the comparison).
static int SpeechBankStrCompare(const char *a1, const char *a2)
{
    while (1)
    {
        char v4 = *a1;
        if (*a1 == 0 && *a2 == 0)
            return 0;
        if (v4 < *a2)
            return -1;
        if (v4 > *a2)
            break;
        ++a1;
        ++a2;
    }
    return 1;
}

// SpeechBankPrefixTest (0x1A00E00, formerly sub_1A00E00) -- the per-record prefix test: ECX is the record's
// +4 slot address (holding the address of the name char*); when the slot
// holds an address the test compares the record's name string against the
// argument, else it compares the argument against the empty string
// (byte_1BCE523, len 0).
static char SpeechBankPrefixTest(const char **nameSlotAddr, const char *a2)
{
    if (*nameSlotAddr != 0)
        return (char)(SpeechBankStrCompare(*(const char **)*nameSlotAddr, a2) != 0);
    return (char)(strcmp(a2, "") != 0);
}

//  0x1DBE2B4 / 0x1DBDA70: the two shared slots SpeechBankMount concats
//  into the speech-bank mount path (SharedSlotConcat, 0x1A02260).  .bss;
//  both null in this build (the original's static initializers do not
//  run here).
static F3SharedBuffer *g_dword_1DBE2B4 = 0;   // 0x1DBE2B4
static F3SharedBuffer *g_dword_1DBDA70 = 0;   // 0x1DBDA70

//  forward decl for a file-local helper defined below this point.
static F3SharedBuffer **SharedSlotAppendSlot(F3SharedBuffer **dst, F3SharedBuffer **src);

//  TruncateSharedBlockTo  (sub_403A70, 0x403A70 - 0x403AE6)  -- truncate a
//  counted block to a shorter payload in place.  The original: when the new
//  length is smaller than the block's current length, allocate a fresh
//  aligned payload ((len + 4) & ~3), memcpy_s the first newLength bytes,
//  null-terminate, free the old payload through the heap-table free slot
//  (dword_2075B24+0x10), flip the capacity flag pair ((old & 0x80000000) |
//  (new & 0x7FFFFFFF)), then re-terminate and zero the tail padding
//  (memset(new + newLen + 1, 0, aligned - (newLen + 1))).  [NOT byte-identical]
//  placement-dependent (relative calls to LHAlloc / memcpy_s / memset / LHFree).
static void TruncateSharedBlockTo(unsigned int newLen, F3SharedBuffer *blk)
{
    if (newLen >= blk->length)
        return;

    unsigned int aligned = (newLen + 4u) & ~3u;
    char *newData = (char *)LHAlloc(aligned);
    memcpy_s(newData, newLen, blk->data, newLen);
    newData[newLen] = 0;

    if (blk->data != 0)
    {
        LHFree(blk->data);               // the heap-table free slot (dword_2075B24+0x10)
        blk->data = 0;
    }

    blk->capacity ^= (aligned ^ blk->capacity) & 0x7FFFFFFFu;
    blk->data   = newData;
    blk->length = newLen;
    newData[newLen] = 0;
    unsigned int pad = aligned - (newLen + 1u);
    if (pad != 0)
        memset(newData + newLen + 1, 0, pad);
}

//  DetachToUpperSharedSlot  (sub_1A01B80, 0x1A01B80 - 0x1A01BD7)  -- the
//  ANSI shared-slot "private copy + upcase": RefSharedSlot into out,
//  DetachSharedSlot (a private copy), then toupper (0x80) the payload in
//  place.  Returns the out slot.  Used by NormalizeSpeechPath to build the
//  uppercase hash suffixes.  [NOT byte-identical] placement-dependent
//  (relative calls to RefSharedSlot / DetachSharedSlot / toupper).
static F3SharedBuffer **DetachToUpperSharedSlot(F3SharedBuffer **src,
                                                F3SharedBuffer **out)
{
    RefSharedSlot(out, src);
    DetachSharedSlot(out);
    F3SharedBuffer *b = *out;
    if (b == 0)
        return out;
    for (unsigned int i = 0; i < b->length; i++)
        if ((unsigned char)b->data[i] < 0x80u)
            b->data[i] = (char)toupper((unsigned char)b->data[i]);
    return out;
}

//  NormalizeSpeechPath  (sub_18C4AE0, 0x18C4AE0 - 0x18C4F1E)  -- the
//  speech-bank mount path normalizer: split the lowercased source slot into
//  the three parts of the final mount path -- the prefix (the drive +
//  "data\" root, or just "data\" for a data-rooted path), the directory
//  (everything up to and including the last backslash) and the filename
//  (everything after it) -- and concat them into the destination slot.
//  a1 = the destination slot, a2 = the source slot.  When the filename is
//  longer than 40 chars it is truncated to 32 and an uppercase 8-hex-digit
//  FNV-32 hash of the whole (lowercased) source string is appended to it.
//  [NOT byte-identical] placement-dependent (relative calls to
//  SubstringSharedSlot / CopySharedSlot / ReleaseSharedSlot /
//  SetSaveGamePath / AppendSharedSlot / DetachSharedSlot /
//  SharedSlotAppendSlot / sprintf_s + the Interlocked IAT slots).
static F3SharedBuffer **NormalizeSpeechPath(F3SharedBuffer **a1,
                                            F3SharedBuffer **a2)
{
    F3SharedBuffer *slot36 = 0;   // v36: the lowercased source (private copy)
    DetachToLowerSharedSlot(a2, &slot36);

    F3SharedBuffer *slot39 = 0;   // v39: the prefix slot
    SetSharedSlot(&slot39, "", -1);

    const char *v3 = (slot36 != 0) ? slot36->data : "";

    if (strncmp(v3, "data\\", 5u) == 0)
    {
        // the data-rooted case: strip the "data\" prefix
        unsigned int len = (slot36 != 0) ? slot36->length : 0;
        F3SharedBuffer *slot33 = 0;
        SubstringSharedSlot(&slot36, &slot33, 5, (int)len);
        CopySharedSlot(&slot36, &slot33);
        if (slot33 != 0)
        {
            if (InterlockedDecrement(&slot33->refcount) <= 0 && slot33 != 0)
                SharedBufferFree(slot33);
            slot33 = 0;
        }
        InterlockedDecrement(&g_Addend);
        SetSaveGamePath(&slot39, "data\\");
    }
    else
    {
        const char *v6 = v3;   // the whole (lowercased) string
        const char *v7 = strchr(v6, ':');
        if (v7 != 0)
        {
            int v8 = (int)(v7 - v6);   // the drive offset
            F3SharedBuffer *slot34 = 0;
            if (strstr(v7, ":\\data\\") != 0)
            {
                int v9 = v8 + 7;
                SubstringSharedSlot(&slot36, &slot34, 0, v9);
                CopySharedSlot(&slot39, &slot34);
                ReleaseSharedSlot(&slot34);
                unsigned int len = (slot36 != 0) ? slot36->length : 0;
                SubstringSharedSlot(&slot36, &slot34, v9, (int)len);
            }
            else
            {
                int v12 = v8 + 2;
                SubstringSharedSlot(&slot36, &slot34, 0, v12);
                CopySharedSlot(&slot39, &slot34);
                ReleaseSharedSlot(&slot34);
                unsigned int len = (slot36 != 0) ? slot36->length : 0;
                SubstringSharedSlot(&slot36, &slot34, v12, (int)len);
            }
            CopySharedSlot(&slot36, &slot34);
            ReleaseSharedSlot(&slot34);
        }
    }

    // the strrchr section -- the dir / filename split
    const char *v15 = (slot36 != 0) ? slot36->data : "";
    const char *v16 = strrchr(v15, '\\');

    F3SharedBuffer *slot37 = 0;   // v37: the dir slot
    SetSharedSlot(&slot37, "", -1);

    F3SharedBuffer *slot38 = 0;   // v38: the filename slot
    RefSharedSlot(&slot38, &slot36);

    if (v16 != 0)
    {
        int v17 = (int)(v16 - v15);   // the dir offset (the backslash position)

        CopySharedSlot(&slot37, &slot36);
        unsigned int v18len = (slot37 != 0) ? slot37->length : 0;
        if ((int)v18len > v17)
        {
            if (v17 != 0)
            {
                DetachSharedSlot(&slot37);
                TruncateSharedBlockTo((unsigned int)v17, slot37);
            }
            else
            {
                SetSaveGamePath(&slot37, "");
            }
        }
        AppendSharedSlot(&slot37, "\\");
        SetSaveGamePath(&slot38, &v15[v17 + 1]);
    }

    F3SharedBuffer *v19 = slot38;
    if (slot38 != 0 && slot38->length > 0x28u)
    {
        // the long-filename case: truncate to 32, hash the source string
        if (slot38->length > 32u)
        {
            DetachSharedSlot(&slot38);
            TruncateSharedBlockTo(32u, slot38);
        }
        const char *src = (slot36 != 0) ? slot36->data : "";
        DWORD hash = 0x811C9DC5u;
        while (*src != 0)
        {
            hash = (hash * 16777619u) ^ (DWORD)(unsigned char)*src;
            ++src;
        }
        char buf[12];
        sprintf_s(buf, sizeof(buf), "%08x", hash);
        F3SharedBuffer *slot34 = 0;
        SetSharedSlot(&slot34, buf, -1);
        F3SharedBuffer *slot33 = 0;   // v33: the uppercase hash slot
        DetachToUpperSharedSlot(&slot34, &slot33);
        SharedSlotAppendSlot(&slot38, &slot33);
        if (slot33 != 0)
        {
            if (InterlockedDecrement(&slot33->refcount) <= 0 && slot33 != 0)
                SharedBufferFree(slot33);
            slot33 = 0;
        }
        InterlockedDecrement(&g_Addend);
        if (slot34 != 0)
        {
            if (InterlockedDecrement(&slot34->refcount) <= 0 && slot34 != 0)
                SharedBufferFree(slot34);
            slot34 = 0;
        }
        InterlockedDecrement(&g_Addend);
        v19 = slot38;
    }

    // the final (prefix + dir + filename) assembly
    F3SharedBuffer *slot33 = 0;   // v33: the concat slot
    InterlockedIncrement(&g_Addend);
    CopySharedSlot(&slot33, &slot39);
    SharedSlotAppendSlot(&slot33, &slot37);
    *a1 = 0;
    InterlockedIncrement(&g_Addend);
    CopySharedSlot(a1, &slot33);
    SharedSlotAppendSlot(a1, &slot38);

    if (slot33 != 0)
    {
        if (InterlockedDecrement(&slot33->refcount) <= 0 && slot33 != 0)
            SharedBufferFree(slot33);
        slot33 = 0;
    }
    InterlockedDecrement(&g_Addend);
    if (v19 != 0)
    {
        if (InterlockedDecrement(&v19->refcount) <= 0 && v19 != 0)
            SharedBufferFree(v19);
        v19 = 0;
    }
    InterlockedDecrement(&g_Addend);
    if (slot37 != 0)
    {
        if (InterlockedDecrement(&slot37->refcount) <= 0 && slot37 != 0)
            SharedBufferFree(slot37);
        slot37 = 0;
    }
    InterlockedDecrement(&g_Addend);
    if (slot39 != 0)
    {
        if (InterlockedDecrement(&slot39->refcount) <= 0 && slot39 != 0)
            SharedBufferFree(slot39);
        slot39 = 0;
    }
    InterlockedDecrement(&g_Addend);
    if (slot36 != 0)
    {
        if (InterlockedDecrement(&slot36->refcount) <= 0 && slot36 != 0)
            SharedBufferFree(slot36);
        slot36 = 0;
    }
    InterlockedDecrement(&g_Addend);

    return a1;
}

//  sub_616660  (0x616660 - 0x616699)  -- the speech-bank mount error report:
//  builds the "Lua DLC startup script has an error in it" slot, dispatches the
//  Lua-module error report (sub_6281C0 -- the deep Lua VM chain:
//  KoreFormatValue / KoreValueEq / KoreHashTableInsert / sub_18AFB90 /
//  sub_604CF0), and releases the slot.  Deep dependency chain --
//  out of scope.  Stub.  [remaining stub]
static long SpeechBankMountReportError(size_t Size, int a2, int a3, F3SharedBuffer **a4)
{
    (void)Size; (void)a2; (void)a3; (void)a4;
    return 0;
}

//  SpeechBankMount  (0x61ACA0 - 0x61AE6B)  -- the file-subsystem mount callback for the
//  speech-bank table pass: concat the two speech-bank path slots (SharedSlotConcat),
//  normalize the combined path through NormalizedCopySlot (sub_18AED30) +
//  NormalizeSpeechPath (sub_18C4AE0), RefSharedSlot it, then -- for a
//  "data\"-rooted path -- strip the prefix.  The path is converted to wide
//  (SharedSlotToWide + GetLocalizedString + sub_5AF180) and dispatched through
//  the entry's vtable slot 4 (the mount); when the mount wrote an entry,
//  the entry's slot 7 (thiscall) reads the write size, the entry's slot 3
//  (thiscall) writes the mount buffer (LHAlloc'd), the entry's slot 2
//  (stdcall) releases it.  Returns 1 unconditionally.
//  [NOT byte-identical] placement-dependent (E8 calls + the __security_cookie
//  prologue + the vtable dispatches).
static unsigned char SpeechBankMount(int entry, int data)
{
    if (entry != 0)
    {
        // the path slot: concat the two speech-bank slots
        F3SharedBuffer *slotConcat = 0;   // v15
        SharedSlotConcat(&slotConcat, &g_dword_1DBE2B4, &g_dword_1DBDA70);

        // normalize the combined path (sub_18AED30 + sub_18C4AE0)
        F3SharedBuffer *slotNorm = 0;     // v14
        NormalizedCopySlot(&slotNorm, &slotConcat);

        F3SharedBuffer *slotPath = 0;     // v12
        NormalizeSpeechPath(&slotPath, &slotNorm);

        F3SharedBuffer *slotPath10 = 0;   // v10
        RefSharedSlot(&slotPath10, &slotPath);

        const char *v2 = GetSlotString(&slotPath10);

        if (strncmp(v2, "data\\", 5u) == 0)
        {
            unsigned int len = (slotPath10 != 0) ? slotPath10->length : 0;
            F3SharedBuffer *slot13 = 0;
            SubstringSharedSlot(&slotPath10, &slot13, 5, (int)len);
            CopySharedSlot(&slotPath10, &slot13);
            ReleaseSharedSlot(&slot13);
        }

        // build the wide path string (SharedSlotToWide + GetLocalizedString + sub_5AF180)
        F3SharedBuffer *wideSlot = 0;
        SharedSlotToWide(&slotPath10, &wideSlot);
        const WCHAR *localized = GetLocalizedString((const WCHAR ***)&wideSlot);
        F3WString wstr;
        F3WStringReset(&wstr);
        F3WStringAssign(&wstr, localized, (unsigned int)wcslen(localized));
        F3WStringNormalizeSeparators(&wstr);
        ReleaseSharedSlotWide(&wideSlot);

        // the mount dispatch: the entry's vtable slot 4 (thiscall)
        typedef void (__thiscall *EntryMountFn)(int self, int **outSlot, F3WString *wide, int flag);
        int v11 = 0;
        ((EntryMountFn)F3_VT((void *)entry)[4])(entry, (int **)&v11, &wstr, 1);

        if (v11 != 0)
        {
            // the entry's slot 7 (thiscall): the write size
            typedef int (__thiscall *EntrySizeFn)(int self);
            int v7 = ((EntrySizeFn)F3_VT((void *)v11)[7])(v11);

            if (v7 > 0)
            {
                // heap the mount buffer (v7 + 1) bytes
                void *v8 = LHAlloc((unsigned int)(v7 + 1));
                DWORD blockPair[2];
                blockPair[0] = (DWORD)v8;
                blockPair[1] = 0;

                // the entry's slot 3 (thiscall): the write
                typedef int (__thiscall *EntryWriteFn)(int self, DWORD **block, DWORD, DWORD, int, DWORD);
                if ((int)((EntryWriteFn)F3_VT((void *)v11)[3])(v11, (DWORD **)blockPair, 0, 0, v7, 0) == 0)
                    SpeechBankMountReportError((size_t)data, (int)v8, v7, (F3SharedBuffer **)&slotPath);
                LHFree(v8);
            }

            if (v11 != 0)
            {
                // the entry's slot 2 (stdcall): release
                typedef void (__stdcall *EntryReleaseFn)(int self);
                ((EntryReleaseFn)F3_VT((void *)v11)[2])(v11);
            }
        }

        // the wide-string cleanup: the free idiom + the reset idiom
        if (wstr.cap >= 8)
            LHFree((void *)wstr.u.ptr);
        F3WStringReset(&wstr);
        ReleaseSharedSlot(&slotPath10);
        ReleaseSharedSlot(&slotPath);
        ReleaseSharedSlot(&slotNorm);
        ReleaseSharedSlot(&slotConcat);
    }
    return 1;
}

// SpeechBankRecordMountCallback  (sub_61B920, 0x61B920 - 0x61B981) -- the file-subsystem
// collection mount callback for the speech-bank record mount: reload the mounted
// entry through the VFS archive collection (GetVfsArchiveCollection + VfsReloadFileEntry)
// and, on a successful match, release the entry (vtable slot 2, stdcall).  The
// per-record entry update (sub_626240 -- a 44-byte entry-array sync via sub_625720 /
// sub_625020 / sub_621300, all deep-chain stubs) is a no-op in this build: the VFS
// reload never matches a provider, so outEntry is always null and the branch never runs.
// The original is cdecl with a __security_cookie prologue.
static unsigned char SpeechBankRecordMountCallback(int entry, int data)
{
    void *collection = GetVfsArchiveCollection();                       // sub_180D9B0
    void *outEntry = 0;
    VfsReloadFileEntry(collection, &outEntry, (void *)entry, (void *)data);   // sub_180D220
    if (outEntry != 0)
    {
        // sub_626240(this: (unsigned int *)(data + 36), a2: (unsigned int *)&outEntry);
        // -- the per-record entry update (deep-chain stub: sub_625720 -- sub_625020 --
        // sub_621300); never executed in this build (outEntry is always null here).
        if (outEntry != 0)
            ((void (__stdcall *)(int))(*(DWORD *)outEntry + 8))((int)outEntry);   // vtable slot 2 (stdcall) release
    }
    return 1;
}

// SpeechBankWideResize  (sub_436730, 0x436730 - 0x4367F6) -- the wide-string grow:
// allocate a fresh buffer for `newLen` wide chars (the original's 1.5x grow),
// copy the existing content over, release the outgoing heap buffer, and write the
// terminator.  The original is __thiscall.
static void SpeechBankWideResize(F3WString *s, unsigned int newLen)
{
    unsigned int cap = newLen | 7u;
    if (cap <= 0x7FFFFFFEu)
    {
        unsigned int half = s->cap >> 1;
        if (cap / 3u < half && s->cap <= 2147483646u - half)
            cap = half + s->cap;       // the original's 1.5x grow
    }
    else
    {
        cap = newLen;
    }
    wchar_t *newBuf = (wchar_t *)LHAlloc(2u * (cap + 1u));
    if (s->len != 0)
        memcpy_s(newBuf, 2u * (cap + 1u), F3WStringData(s), 2u * s->len);
    if (s->cap >= 8)
        LHFree((void *)s->u.ptr);
    s->u.ptr = (DWORD)newBuf;
    s->cap = cap;
    newBuf[s->len] = 0;
}

// SpeechBankWideAssign  (sub_435B20, 0x435B20 - 0x435C0D) -- the wide-string assign
// core: copy `count` wide chars (starting at `offset`) from the source wide string
// into the destination wide string.  The original is __thiscall + retn 0Ch (the
// string in ECX, the source/offset/count on the stack).  The self-assign path
// (dst == src) is never taken here (the destination is always a fresh empty
// wide string) and is documented as a no-op.
static F3WString *SpeechBankWideAssign(F3WString *dst, const F3WString *src,
                                       unsigned int offset, unsigned int count)
{
    if (src->len < offset)
        F3InvalidParam();                       // the original's sub_1519722 (out-of-range offset)
    unsigned int v5 = src->len - offset;
    if (count < v5)
        v5 = count;

    if (dst == src)
    {
        // the self-assign path (sub_435A80(offset + v5, -1) + sub_435A80(0, offset)):
        // truncate the string to `offset + v5` chars.  Never taken here; documented.
        return dst;
    }
    if (v5 > 0x7FFFFFFEu)
        F3InvalidParam();                       // the original's sub_15196EA (overflow)

    if (dst->cap >= v5)
    {
        if (v5 == 0)
        {
            dst->len = 0;
            if (dst->cap < 8)
                dst->u.buf[0] = 0;
            else
                *(wchar_t *)dst->u.ptr = 0;
            return dst;
        }
    }
    else
    {
        SpeechBankWideResize(dst, v5);
    }
    if (v5 != 0)
    {
        const wchar_t *srcData = (const wchar_t *)F3WStringData((F3WString *)src);
        wchar_t *dstData = F3WStringData(dst);
        memcpy_s(dstData, 2u * dst->cap, srcData + offset, 2u * v5);
        dst->len = v5;
        dstData[v5] = 0;
    }
    return dst;
}

// SpeechBankRecordMountFinalize  (sub_624A10, 0x624A10 - 0x624A72) -- the wide-string
// finalize: release the self-ref wrapper block (the 4-byte LHAlloc(4) block: the
// original releases it unconditionally -- LHFree(0) is a no-op) and the wide string's
// heap buffer (if cap >= 8), then reset the string to the empty SSO layout.
// The dead per-element loop's three iterator slots (v38/v39/v40 -- begin / end /
// the third iterator, always 0 in this build) and the entry-release loop
// (sub_6230B0) are no-ops; the conditional LHFree of the begin iterator can never
// fire here.
static void SpeechBankRecordMountFinalize(F3WString *v34, DWORD *selfRef)
{
    if (selfRef != 0)
        LHFree((void *)selfRef);                     // the self-ref wrapper block (v37)
    if (v34->cap >= 8)
        LHFree((void *)v34->u.ptr);                  // the wide string's heap buffer
    v34->len = 0;
    v34->cap = 7;
    v34->u.buf[0] = 0;
}

// SpeechBankRecordMountReload  (sub_18FAED0, 0x18FAED0 - 0x18FAF7E) -- the reload
// path's per-record dispatch: the +0x1C heap-manager slot + the audio-pack building
// chain (sub_145E6A0 -- the audio-pack builder) plus the shared-slot free path
// (sub_145D520 -- CopySharedSlot).  Deep dependency chain -- out of scope here.
// Stub: returns the null out-slot (the same result the original produces in this
// build -- no provider is ever matched).
static F3SharedBuffer **SpeechBankRecordMountReload(char **recordNameSlot)
{
    (void)recordNameSlot;
    return 0;   // no provider matched (empty archive collection)
}

// SpeechBankRecordMountReloadDispatch  (sub_42D3D0, 0x42D3D0 - 0x42D366) -- the reload
// path's vector-insert dispatch: the +0x14/+0x18/+0x1C record-array steps
// (sub_42E790 -- sub_42FD30 vector insert) and the plain append (*v4 = *a2;
// v1[6] = v4 + 1).  Deep dependency chain (sub_42E790 -- sub_42FD30) -- out of scope.
// Stub: the v1[6] append + the vtable release of the out-slot entry.
static DWORD SpeechBankRecordMountReloadDispatch(F3SharedBuffer **outSlot)
{
    (void)outSlot;
    return 0;
}

// SpeechBankRecordMount  (0x61B990 - 0x61BCB4) -- the per-record "speech.adb" mount +
// entry dispatch (the record-name walk over the [1DBD0F8, 1DBD0FC) table of 36-byte
// records, the name compare, the "speech.adb" shared-slot build + the per-position
// toupper prefix test, the collection mount (FileSubsysMountCollection with the
// sub_61B920 callback), the per-element heap-manager / entry dispatches (dead: the
// begin/end entry iterators are always 0), and the reload path (sub_18FAED0 +
// sub_42D3D0).  Deep dependency chain (sub_145E440 / sub_145E6A0 / sub_625720 /
// sub_42FD30 / sub_145D520) -- the per-element dispatch and the reload path are
// documented stubs.
//  [NOT byte-identical] placement-dependent (the E8 calls + the entry vtable
//  slot-2 dispatch + the __security_cookie prologue).
static void SpeechBankRecordMount(const char **nameSlotAddr)
{
    // === the record-find loop (the name compare)
    DWORD *v1 = (DWORD *)g_dword_1DBD0F8;   // the record-array start

    if (g_dword_1DBD0F8 != g_dword_1DBD0FC)   // the table is empty -> return immediately
    {
        for (;;)
        {
            const char *v2 = "";
            if (*nameSlotAddr != 0)
                v2 = *(const char **)*nameSlotAddr;
            const char *v4 = "";
            const char **slot = (const char **)((char *)v1 + 4);   // the record's +4 word (the name-slot pointer)
            if (*slot != 0)
                v4 = *(const char **)*slot;
            if (stricmp(v4, v2) == 0)
                break;
            v1 += 9;                                               // += 0x24 (36 bytes)
            if (v1 == (DWORD *)g_dword_1DBD0FC)
                return;                                             // not found
        }

        // === the "speech.adb" shared-slot build
        F3SharedBuffer *slotSpeech = 0;
        SetSharedSlot(&slotSpeech, "speech.adb", -1);   // the original's size arg 0xFFFFFFFF

        // === the prefix test (the mount decision)
        const char *v5 = "";
        if (*nameSlotAddr != 0)
            v5 = *(const char **)*nameSlotAddr;
        const char *v6 = GetSlotString(&slotSpeech);   // the "speech.adb" string (or "" fallback)
        size_t v7 = strlen(v6);
        char v8;
        if (v7 != 0)
        {
            if (*v5 != 0)
            {
                v8 = 1;
                const char *pos = v5;
                for (;;)
                {
                    if (toupper((unsigned char)*pos) == toupper((unsigned char)*v6)
                        && strnicmp(pos, v6, v7) == 0)
                        break;
                    if (*++pos == 0)
                    {
                        v8 = 0;
                        break;
                    }
                }
            }
            else
            {
                v8 = 0;
            }
        }
        else
        {
            v8 = 1;
        }
        ReleaseSharedSlot(&slotSpeech);

        if (v8 != 0)
        {
            // === the mounted path
            F3WString mountWide;                 // v34 -- the mount wide string
            F3WStringFromAnsi(&mountWide, "");   // the empty wide string

            DWORD *selfRef = (DWORD *)LHAlloc(4);   // v37 -- the wrapper slot (the block pointer)
            if (selfRef != 0)
                *selfRef = (DWORD)&selfRef;          // the block contains the wrapper slot's address
            // (the not-allocated case leaves selfRef == 0 -- the original's var_1C = 0)

            F3WString recordWide;                  // v27 -- the record-name wide string
            const char **recordSlot = (const char **)((char *)v1 + 4);   // the record's +4 word
            const char *recordNameStr = (*recordSlot != 0) ? *(const char **)*recordSlot : "";
            F3WStringFromAnsi(&recordWide, recordNameStr);
            SpeechBankWideAssign(&mountWide, &recordWide, 0, 0xFFFFFFFFu);
            // the record-name wide string's e1/e2 fields are carried into the mount
            // wide string (the original's v35 = v29 / v36 = v30 -- the e1/e2 copy)
            mountWide.e1 = recordWide.e1;
            mountWide.e2 = recordWide.e2;

            // the record-name wide string's heap-buffer free (the original's
            // "if (v28 >= 8) LHFree()" -- the cap check + the heap-buffer free)
            if (recordWide.cap >= 8)
                LHFree((void *)recordWide.u.ptr);

            // === the collection mount (FileSubsysMountCollection with sub_61B920)
            void *fs = GetFileSubsystem(0);
            void *outEntry = 0;
            FileSubsysMountCollection(fs, &outEntry,
                                      (unsigned char (__cdecl *)(int, int))SpeechBankRecordMountCallback,
                                      (int)&mountWide);
            if (outEntry != 0)
                ((void (__stdcall *)(void *))F3_VT(outEntry)[2])(outEntry);   // vtable slot 2

            // === the dead per-element loop head -- the wrapper null check fires
            // (the not-allocated case); the loop itself is dead (begin == end == 0).
            if (selfRef == 0)
                F3InvalidParam();                    // the original fires this when the wrapper is null

            // === the wide-string finalize (sub_624A10)
            SpeechBankRecordMountFinalize(&mountWide, selfRef);
        }
        else
        {
            // === the reload path (v8 == 0 -- the record name does not prefix-match)
            void *hm = GetHeapManager();            // the 144-byte manager (getter)
            (void)hm;
            // sub_18FAED0(a1: v1 + 1) -- the per-record reload (deep-chain stub:
            // sub_145E6A0 -- the audio-pack building / OpenMemoryFile chain)
            F3SharedBuffer **outEntry = SpeechBankRecordMountReload((char **)((char *)v1 + 4));
            // sub_42D3D0(a1: &v26) -- the reload dispatch (deep-chain stub: the vector
            // insert core via sub_42E790 -- sub_42FD30)
            (void)SpeechBankRecordMountReloadDispatch(outEntry);
        }
    }
}


// ---- forward declarations for the reload chain (defined below in this file)
static void *VfsAllocCacheAlloc(void *cache, int wait);
static int VfsAllocCacheFinalize(void *cache);
static void *EventObjCtor(void *obj, int manualReset, int initialState);
static CRITICAL_SECTION *MgrCs(void);
static int WaitForEvent(void *obj, int dwMilliseconds, int bAlertable);

// CLoadAudioBankDtor  (sub_14634F0, 0x14634F0 - 0x1463514) -- the CLoadAudioBank
// destructor (thiscall + retn 4): the shared-slot release at this+0x0C
// (ReleaseSharedSlot), the ICommand vftable install, and the LHFree when the
// free flag is set.
static void *CLoadAudioBankDtor(void *obj, char a2)
{
    F3SharedBuffer **slot = (F3SharedBuffer **)((char *)obj + 0x0C);
    ReleaseSharedSlot(slot);
    *(DWORD *)obj = 0x01B6F458;   // &NLHAudio::NThreading::ICommand::`vftable' (original VA)
    if (a2 & 1)
        LHFree(obj);
    return obj;
}

// CLoadAudioBankSize  (sub_14621A0, 0x14621A0 - 0x14621A7) -- the size getter
// (no-arg, returns 20 = the 20-byte staged object).
static int CLoadAudioBankSize(void) { return 20; }

// CLoadAudioBankAtExitInit  (sub_14602D0, 0x14602D0 - 0x14602F5) -- the at-exit
// init (thiscall + retn 4): the deep file-subsystem mount chain (sub_1465720 --
// sub_146C2D0 / FileSubsysMountCollection / sub_146C410 / ...) is out of scope
// here.  [STUB]
static int CLoadAudioBankAtExitInit(void *obj) { (void)obj; return 0; }

// the CLoadAudioBank vftable (0x1B6F7C0): the destructor / size getter / at-exit
// init / rdata marker (same shape as the XAudio2System vftables).
static const void *kCLoadAudioBankVftable[] = {
    (const void *)CLoadAudioBankDtor,      // 0x14634F0
    (const void *)CLoadAudioBankSize,      // 0x14621A0
    (const void *)CLoadAudioBankAtExitInit, // 0x14602D0 (stub)
    (const void *)0x1C410D4                // rdata entry (original VA; garbage marker)
};

// SpeechBankRecordReloadAudioBank  (sub_18FADD0, 0x18FADD0 - 0x18FAE16) -- the
// reload-path audio-comms dispatch: the critical-section enter (stru_20FC8AC,
// lazy-initialised via MgrCs()), the record's name read (the slot-0 deref with
// the empty-string fallback), the sub_1460BC0 chain (EventObjCtor(this=var_88,
// 1, 0) -> the 0x84-byte event wrapper; VfsAllocCacheAlloc(&unk_1DDC830, 1) ->
// stage a 1-slot allocation; on success: the CLoadAudioBank vftable install +
// InitSharedSlot(this=+0x0C) + +4 = the counter (pre-increment) + +8 = the flag
// int + SetSaveGamePath(this=+0x0C, Source) + +0x10 = nullptr (a4 != 0) +
// VfsAllocCacheFinalize), the event close (sub_18C45E0 / CloseHandle on the event
// handle at +0x80), and the counter++ (the sub_145D550 tail).  Returns the
// pre-increment counter value (the same value the original stores into the
// record's +0x08 word).
//  [NOT byte-identical] placement-dependent (relative calls to the shared-slot
//  family / VfsAllocCacheAlloc / VfsAllocCacheFinalize / EventObjCtor /
//  CloseHandle / the EnterCriticalSection IAT thunk + the __security_cookie
//  prologue).
static int SpeechBankRecordReloadAudioBank(const char **recordStart, int a2, char a3)
{
    EnterCriticalSection(MgrCs());              // EnterCriticalSection(&stru_20FC8AC)

    DWORD *staged = 0;
    const char *source = "";
    if (*recordStart != 0)
        source = *(const char **)*recordStart;  // v3 = **record (or Str1)

    DWORD counter = g_dword_1D6A6E8;              // the a1 int param (captured pre-chain)

    BYTE eventObj[0x84];                          // var_88 -- the 0x84-byte event wrapper
    EventObjCtor(eventObj, 1, 0);                 // sub_1460BC0's step 1
    staged = (DWORD *)VfsAllocCacheAlloc(g_unk_1DDC830, 1);
    if (staged != 0)
    {
        staged[0] = (DWORD)kCLoadAudioBankVftable;               // the vftable install
        InitSharedSlot((F3SharedBuffer **)((char *)staged + 0x0C));  // the null shared slot
        *((DWORD *)staged + 2) = (DWORD)a2;                       // +8  = the flag-byte int (a3 of sub_1460BC0)
        *((DWORD *)staged + 1) = counter;                          // +4  = the counter (pre-increment)
        SetSaveGamePath((F3SharedBuffer **)((char *)staged + 0x0C), source);   // thiscall
        *((DWORD *)staged + 4) = (a3 != 0) ? 0 : (DWORD)eventObj;  // +0x10 = nullptr when a4 (the char) != 0
        VfsAllocCacheFinalize(g_unk_1DDC830);
        if (a3 == 0)
            WaitForEvent(eventObj, 0xFFFFFFFFu, 0);               // the original's a4 == 0 wait
    }
    CloseHandle(*(HANDLE *)((char *)eventObj + 0x80));            // sub_18C45E0 (CloseHandle(*(var_88 + 0x80)))

    int ret = (int)g_dword_1D6A6E8;
    g_dword_1D6A6E8++;                                            // the sub_145D550 counter++

    LeaveCriticalSection(MgrCs());
    return ret;
}

// SpeechBankRecordReload  (0x61A9B0 - 0x61AA1E)  -- the per-record "reload the
// current record" dispatch: the record-name walk over the [1DBD0F8, 1DBD0FC)
// 36-byte record table (0x24-byte steps, the name slot at record+0), the +0x21
// flag byte check (the byte == 0 -> the int 1), the GetHeapManager call (the
// original passes the 144-byte manager as ecx to sub_18FADD0 -- a dead load,
// fnv-discarded here), and the SpeechBankRecordReloadAudioBank re-write of the
// record's +0x08 word.  [NOT byte-identical] placement-dependent (the E8 calls
// to __stricmp / GetHeapManager / SpeechBankRecordReloadAudioBank + the
// __security_cookie prologue).
static void SpeechBankRecordReload(const char **nameSlotAddr)
{
    DWORD *v1 = (DWORD *)g_dword_1DBD0F8;   // the record-array start

    if (g_dword_1DBD0F8 != g_dword_1DBD0FC)   // the table is empty -> return immediately
    {
        for (;;)
        {
            const char *v2 = "";
            if (*nameSlotAddr != 0)
                v2 = *(const char **)*nameSlotAddr;
            const char *v3 = "";
            if (*v1 != 0)
                v3 = *(const char **)*v1;
            if (stricmp(v3, v2) == 0)
                break;
            v1 += 9;                                    // += 0x24 (36 bytes)
            if (v1 == (DWORD *)g_dword_1DBD0FC)
                return;                                  // not found
        }

        // === the +0x21 flag byte check + the reload dispatch
        int v4 = (*(BYTE *)((char *)v1 + 0x21) == 0) ? 1 : 0;
        void *hm = GetHeapManager();                     // the 144-byte manager (getter)
        (void)hm;
        *(DWORD *)((char *)v1 + 8) = SpeechBankRecordReloadAudioBank((const char **)v1, v4, 1);
    }
}

//  SpeechBankTablePass  (0x61C080 - 0x61C119)  -- the speech-bank table pass:
//  the SpeechBankFlagRead flag read, the file-subsystem FileSubsysMountCollection
//  (0x1801620) mount with the SpeechBankMount (sub_61ACA0) callback over the
//  engine's +0xBC slot, and the [g_dword_1DBD0F8, g_dword_1DBD0FC) 36-byte
//  record loop (the prefix tests SpeechBankPrefixTest with the per-record
//  SpeechBankRecordMount / SpeechBankRecordReload dispatches).  The record
//  globals are 0 in this build, so the loop is dead.  [NOT byte-identical]
//  placement-dependent (the E8 calls to GetFileSubsystem /
//  FileSubsysMountCollection / SpeechBankPrefixTest + the entry
//  vtable slot-2 dispatch + the __security_cookie prologue).
static int SpeechBankTablePass(void *obj)
{
    (void)SpeechBankFlagRead();                       // the flag read (result discarded)
    void *fs = GetFileSubsystem(obj);                 // sub_1801C20
    int entry = 0;
    FileSubsysMountCollection(fs, (void **)&entry,
                              (unsigned char (__cdecl *)(int, int))SpeechBankMount,
                              *(DWORD *)((char *)obj + 0xBC));   // sub_1801620
    int result = entry;
    if (entry != 0)
        result = (int)((int (WINAPI *)(void *))F3_VT((void *)entry)[2])((void *)entry);

    DWORD v5 = g_dword_1DBD0F8;
    if (v5 != g_dword_1DBD0FC)
    {
        DWORD v6 = v5 + 4;
        do
        {
            result = (int)SpeechBankPrefixTest((const char **)(DWORD *)v6, "");
            if ((BYTE)result != 0)
                SpeechBankRecordMount((const char **)(DWORD *)v6);
            if (*(BYTE *)((char *)v6 + 0x1C) == 0)
            {
                result = (int)SpeechBankPrefixTest((const char **)(DWORD *)v5, "");
                if ((BYTE)result != 0)
                    SpeechBankRecordReload((const char **)(DWORD *)v5);
            }
            v5 += 36;
            v6 += 36;
        }
        while (v5 != g_dword_1DBD0FC);
    }
    return result;
}

// 0x1DBE2CC: the settings-directory base shared slot (the sibling of
// g_dword_1DBE2F0); .bss (starts null, populated by the settings init that
// is not built yet).
static F3SharedBuffer *g_unk_1DBE2CC = 0;

// ============================================================
//  MountPermanentBanks  (0x61C320 - 0x61C521)  -- the "Permanent Bank
//  Mounter" thread entry (the _beginthread callback RunFrontEnd starts with
//  the CGameEngine as its argument, on a 0x40000-stack thread).
//
//  void MountPermanentBanks(void *obj)   (obj = the CGameEngine, 356 bytes)
//
//  The original is cdecl (the engine pointer on the stack, hoisted into
//  ESI) with a __security_cookie prologue.  The thread, in order:
//    1. announces its name (SetThreadName "Permanent Bank Mounter") and
//       mounts the fable2_anims.bnk bank (MountAnimsBank);
//    2. under the engine's +0x60 critical section (the CsGuardAcquire /
//       CsGuardRelease guard), builds the
//       "data\\art\\videos\\lionhead_logo.bik" shared slot and mounts it as
//       a video file (MountVideoFile);
//    3. builds the settings-directory "particle_bank.bnk" path
//       (MakeSettingsFilePath over the unk_1DBE2CC settings base) and opens
//       it as a data stream (OpenDataStreamFile);
//    4. creates the GDB loader (CreateGdbLoader) and, when created, loads
//       the five permanent global GDB files (LoadGlobalGdbFiles);
//    5. allocates + constructs the 192-byte bank container
//       (InitBankContainer), wraps it in the 12-byte { refcount, dtor =
//       ReleaseBankObject, obj } block, publishes both words into the
//       engine's +0xA0 slot pair (SlotPairSet), registers the container
//       with the file subsystem (RegisterBankContainer), and drops the
//       local reference (the InterlockedDecrement-to-zero -> dtor -> LHFree
//       release idiom);
//    6. rebuilds the engine's +0xB8 NUI::CGUI object (RebuildGuiObject);
//    7. mounts the two lipsync banks (MountLipsyncBank over
//       "language/lipsync/lipsync.bnk" and
//       "language/lipsync/expressions.bnk", each freed after the mount),
//       registers the speech callback (RegisterSpeechCallback), loads
//       "language/speech/cutscenes.vfsconfig" into the file subsystem
//       (FileSubsysLoadVfsConfig), and runs the speech-bank table pass
//       (SpeechBankTablePass);
//    8. sets the engine's +0x84 "banks mounted" flag under the +0x48
//       critical section.
//  [NOT byte-identical] placement-dependent: the relative E8 calls to every
//  step above, the InterlockedDecrement IAT slot at its original high VA,
//  and the __security_cookie prologue.
// ============================================================
static void MountPermanentBanks(void *p)
{
    CGameEngine *obj = (CGameEngine *)p;
    char *base = (char *)obj;

    SetThreadName("Permanent Bank Mounter");
    MountAnimsBank(obj);

    F3CsGuard guard2;
    CsGuardAcquire(&guard2, &obj->cs2, 0);
    {
        F3SharedBuffer *slot = 0;
        F3SharedBuffer **fileId = SetSharedSlot(&slot, "data\\art\\videos\\lionhead_logo.bik", -1);
        MountVideoFile(obj, fileId);
        ReleaseSharedSlot(&slot);
    }
    CsGuardRelease(&guard2);

    {
        F3SharedBuffer *slot = 0;
        F3SharedBuffer **dst = MakeSettingsFilePath(&slot, &g_unk_1DBE2CC, "particle_bank.bnk");
        OpenDataStreamFile(dst);
        ReleaseSharedSlot(&slot);
    }

    void *loader = CreateGdbLoader();
    if (loader != 0)
        LoadGlobalGdbFiles(loader);

    void *container = 0;
    void *buf = LHAlloc(0xF0);
    if (buf != 0)
        container = InitBankContainer(buf, 0x32, 0x4000);
    void *pair[2] = { container, 0 };
    if (container != 0)
    {
        void *w = LHAlloc(12);
        if (w != 0)
        {
            *(DWORD *)w = 1;
            *(void **)((char *)w + 4) = (void *)ReleaseBankObject;
            *(void **)((char *)w + 8) = container;
            pair[1] = w;
        }
    }
    SlotPairSet(base + 0xA0, pair);
    RegisterBankContainer((int)container);
    if (pair[1] != 0)
    {
        if (InterlockedDecrement((volatile LONG *)pair[1]) == 0)
        {
            (*(void (*)(void *))(*(void **)((char *)pair[1] + 4)))(
                *(void **)((char *)pair[1] + 8));
            LHFree(pair[1]);
        }
    }

    RebuildGuiObject(obj);

    F3WString ws;
    F3WStringFromAnsi(&ws, "language/lipsync/lipsync.bnk");
    MountLipsyncBank(obj, &ws, 0);
    F3WStringFree(&ws);
    F3WStringFromAnsi(&ws, "language/lipsync/expressions.bnk");
    MountLipsyncBank(obj, &ws, 0);
    F3WStringFree(&ws);

    RegisterSpeechCallback();

    F3WStringFromAnsi(&ws, "language/speech/cutscenes.vfsconfig");
    FileSubsysLoadVfsConfig(GetFileSubsystem(0), &ws);
    F3WStringFree(&ws);

    SpeechBankTablePass(obj);

    F3CsGuard guard1;
    CsGuardAcquire(&guard1, &obj->cs1, 0);
    obj->member0x84 = 1;
    CsGuardRelease(&guard1);
}
// sub_6A66C0 is the canonical GetNetManager (netinit.cpp / netinit.h);
// sub_6A6960 is the canonical CNetManagerGetUserManager (netinit.cpp);
// sub_6A69A0 is the canonical CNetManagerGetVoiceManager (netinit.cpp).
// sub_772190 is now CNetUserManagerRefreshSignin (netinit.cpp).
static void *g_dword_2075ED8 = 0;   // 0x2075ED8: the 144-byte manager object
static CRITICAL_SECTION g_cs_20FC8AC;   // 0x20FC8AC: the manager critical section (zeroed)
//  Lazy-init the manager critical section.  The original relies on stru_20FC8AC
//  being initialised at startup; here we InitialiseCriticalSection on first use.
static CRITICAL_SECTION *MgrCs(void)
{
    static int inited = 0;
    if (!inited) { InitializeCriticalSection(&g_cs_20FC8AC); inited = 1; }
    return &g_cs_20FC8AC;
}
static F3SharedBuffer **GetArchiveSlot(F3SharedBuffer **out);   // sub_180AC90 (defined below)

// The manager / VFS / player objects in this build never carry a real
// vtable (their +0x00 slot is 0), so these dispatchers only execute against
// a live object. They model the original's `mov ecx,[obj]; mov edx,[ecx+4*slot];
// push obj; call edx` -- a thiscall to vtable[slot] with the object as the
// single stack argument.
static inline void F3CallVtable(void *obj, int slot)
{
    void **vt = (void **)(*(void **)obj);
    ((void(__thiscall *)(void *))(void *)vt[slot])(obj);
}
static inline int F3CallVtableRet(void *obj, int slot)
{
    void **vt = (void **)(*(void **)obj);
    return ((int(__thiscall *)(void *))(void *)vt[slot])(obj);
}
// ============================================================
//  VfsDescDefaultCtor  (0x145DC20 - 0x145DC8A)  -- the virtual
//  filesystem descriptor default constructor
//
//  void *VfsDescDefaultCtor(void *obj)   -- thiscall in ECX
//
//  Fills the 72-byte VFS descriptor record (obj) with the default field
//  values, read straight from the original byte stream:
//    +0x00 = 0            +0x14 = offset unk_2000000 (descriptor vtable)
//    +0x04 = 50 (0x32)    +0x18 = 66 (0x42)
//    +0x08 = 0            +0x1C = 6
//    +0x0C = 0x6000       +0x20 = 0x8000
//    +0x10 = 3            +0x24 = 72 (0x48)
//                         +0x28 = 0x40000
//                         +0x2C = 10 (0x0A)
//                         +0x30 = 0
//                         +0x34 = 0
//                         +0x38 = 2
//                         +0x3C = 0 (byte)
//                         +0x40 = 1
//                         +0x44 = 0
//  Three other call sites (0x14835F5 / 0x1516646) use the same defaults.
static void *VfsDescDefaultCtor(void *obj)
{
    DWORD *d = (DWORD *)obj;
    BYTE  *b = (BYTE *)obj;
    d[0x00 / 4] = 0;
    d[0x04 / 4] = 50;               // 0x32
    d[0x08 / 4] = 0;
    d[0x0C / 4] = 0x6000;
    d[0x10 / 4] = 3;
    d[0x14 / 4] = 0x2000000;        // offset unk_2000000: descriptor vtable
    d[0x18 / 4] = 66;               // 0x42
    d[0x1C / 4] = 6;
    d[0x20 / 4] = 0x8000;
    d[0x24 / 4] = 72;               // 0x48
    d[0x28 / 4] = 0x40000;
    d[0x2C / 4] = 10;               // 0x0A
    d[0x30 / 4] = 0;
    d[0x34 / 4] = 0;
    d[0x38 / 4] = 2;
    b[0x3C] = 0;                    // byte at +0x3C
    d[0x40 / 4] = 1;
    d[0x44 / 4] = 0;
    return obj;
}

// ============================================================
//  CreateVirtualFilesystem  (0x145DD80 - 0x145DECE)  -- bring up the
//  COM-backed virtual filesystem (LHVFS) for the 80-byte VFS descriptor
//
//  int CreateVirtualFilesystem(void *desc)   -- cdecl, one stack pointer.
//  The original also captures ECX on entry into a BYREF slot (seeded with the
//  register, then used only as the out-pointer for the fallback-VFS object --
//  it is overwritten by CreateFallbackVfs before it is ever read, so no
//  caller-supplied register value is needed here).
//
//  1.  One-shot: if the VFS is already initialised (byte_1DDC7E0) return 0.
//  2.  On first use, CoInitializeEx(0, 0) and CoCreateInstance over the
//      system Core Audio MMDeviceEnumerator CLSID / IMMDeviceEnumerator IID;
//      on success drive the two interface methods on the created object
//      (slot 4 = GetDefaultAudioInterface into the 0x1DDC7EC buffer, slot 6
//      = RegisterEndpointNotificationCallback with the 0x1D6A768 client).
//      ppv is kept globally (released elsewhere).
//  3.  If the descriptor's +0x00 object (the archive manager) is null, report
//      "Virtual filesystem can not be null" via SetSaveGamePath and return 0.
//  4.  Otherwise: clamp the descriptor (+0x04 -> 1 if 0, +0x08 -> 31 if >=32),
//      copy-assign it into the global descriptor (VfsDescAssign), build the
//      CFallbackVFS object (CreateFallbackVfs), install it into dword_1DDC7F4
//      (releasing the old one), and run the VFS initialisation
//      (InitVirtualFileSystem).  On success, install the 4-byte refcount
//      wrapper (VfsRefCountCtor) at dword_1DDC7E4, register the VFS provider
//      groups, mark byte_1DDC7E0 = 1 and return 1.
//  [NOT byte-identical] placement-dependent (relative calls to the VFS
//  callees, the LHAlloc / CoInitializeEx / CoCreateInstance IAT thunks) and
//  refs the high-VA globals (the byte_1DDC7E0 cluster, dword_1D6A720, the
//  CLSID/IID, off_1D6A768).
// ============================================================

// The audio-fallback VFS COM + global cluster (original addresses kept in
// comments).  The LHVFS audio virtual filesystem backs onto the *system* Core
// Audio COM server: the CLSID / IID are the Windows MMDeviceEnumerator /
// IMMDeviceEnumerator pair, so CoCreateInstance succeeds on a normal machine.
static const CLSID g_rclsid_1D6A6F0 = { 0xBCDE0395, 0xE52F, 0x467C,
    { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };   // 0x1D6A6F0: MMDeviceEnumerator CLSID
static const IID   g_riid_1D6A700  = { 0xA95664D2, 0x9614, 0x4F35,
    { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };    // 0x1D6A700: IMMDeviceEnumerator IID
static BYTE  g_byte_1DDC7E0 = 0;      // 0x1DDC7E0: VFS initialised flag
static DWORD g_dword_1DDC7E4 = 0;     // 0x1DDC7E4: 4-byte refcount wrapper
static void *g_ppv_1DDC7E8 = 0;       // 0x1DDC7E8: the IMMDeviceEnumerator (ppv)
static BYTE  g_unk_1DDC7EC[16] = { 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF };   // 0x1DDC7EC: GetDefaultAudioInterface out buffer
static F3SharedBuffer *g_dword_1DDC7F0 = 0;   // 0x1DDC7F0: "cannot be null" msg slot
static DWORD g_dword_1DDC7F4 = 0;     // 0x1DDC7F4: installed fallback-VFS object
// An IMMNotificationClient kept in this build (the real client the game
// registers with the enumerator -- vtable 0x01B6F408 -- is not implemented).
// Its first field is a live vtable so RegisterEndpointNotificationCallback
// AddRefs it (any audio-event callback resolves to safe code); the first
// field of the object (the vtable) must be a mapped address, not
// 0x01B6F408.  QueryInterface (F3AudioNotifQI, 0x145DB60), AddRef
// (F3AudioNotifAddRef, 0x145DBC0), Release (F3AudioNotifRelease, 0x145E400)
// and Added (F3AudioNotifAdded, 0x145DB00) are implemented for real; the
// remaining methods are plain __stdcall no-op entries with the exact
// parameter arity so they clean the right number of stack bytes.
struct F3AudioPropKey { int d1; short d2, d3; char d4[8]; int d5; };   // 20-byte PROPERTYKEY (by value)
// The two IIDs the QI accessor accepts, kept at their original addresses
// (0x1BC57FC = IUnknown, 0x1D6A710 = the IMMNotificationClient).
static const IID   g_riid_1BC57FC  = { 0, 0, 0, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };   // 0x1BC57FC: IUnknown IID
static const IID   g_riid_1D6A710  = { 0x7991eec9, 0x7e89, 0x4d85,
    { 0x83, 0x90, 0x6c, 0x70, 0x3c, 0xec, 0x60, 0xc0 } };    // 0x1D6A710: IMMNotificationClient IID
// ===============================================================
// Wide-slot array machinery (the IMMNotificationClient's slot array).
//
// These helpers implement the resize / insert path that
// F3AudioNotifStateChanged (sub_145E560) drives through
// sub_145FD10 (0x145FD10 - 0x145FD77).  The wide-slot array lives at
// self+0x14 (start, void**), self+0x18 (end, void**), self+0x1C (spare,
// void**) inside the sub-struct at self+8; each element is 8 bytes
// ([0] = F3SharedBuffer*, [1] = state DWORD).  All are [NOT
// byte-identical] placement-dependent (the LHAlloc / LHFree IAT slots and the
// CopySharedSlotWide / ReleaseSharedSlotWide family).
//
//   sub_404A40 (0x404A40 - 0x404A65)  -- the wide-string sort comparator.
static int CompareWideStrings(const WCHAR *a1, const WCHAR *a2)
{
    for (;;)
    {
        WCHAR v = *a1;
        if (*a1 == 0 && *a2 == 0)
            return 0;
        if (v < *a2)
            return -1;
        if (v > *a2)
            break;
        ++a1;
        ++a2;
    }
    return 1;
}

//   sub_145E9F0 (0x145E9F0 - 0x145E98E)  -- the wide array allocator.
//   8*count bytes via LHAlloc; throws stdext::bad_alloc on overflow.
static BYTE *WideArrayAlloc(unsigned int count)
{
    if (count != 0 && 0xFFFFFFFFu / count < 8u)
        throw 1;   // the original throws stdext::bad_alloc
    return (BYTE *)LHAlloc(8 * count);
}

//   sub_145F4F0 (0x145F4F0 - 0x145F52A)  -- fill `count` slots at `dest` with
//   the content of the source slot (block pointer + state).
static long WideSlotFill(BYTE *dest, int count, F3SharedBuffer **src)
{
    long result = 0;
    for (int i = count; i != 0; --i)
    {
        if (dest != 0)
        {
            CopySharedSlotWide((F3SharedBuffer **)dest, src);
            result = (long)((DWORD *)src)[1];
            ((DWORD *)dest)[1] = (DWORD)result;
        }
        dest += 8;
    }
    return result;
}

//   sub_145F320 (0x145F320 - 0x145F358)  -- copy [start, end) into `dest`
//   (8 bytes per slot).
static BYTE *WideSlotCopy(BYTE *start, BYTE *end, BYTE *dest)
{
    if (start == end)
        return dest;
    BYTE *p = dest;
    for (BYTE *s = start; s != end; s += 8)
    {
        if (p != 0)
        {
            CopySharedSlotWide((F3SharedBuffer **)p, (F3SharedBuffer **)s);
            ((DWORD *)p)[1] = ((DWORD *)s)[1];
        }
        p += 8;
    }
    return p;
}

//   sub_145F710 (0x145F710 - 0x145F735)  -- release the wide slots in
//   [start, end) (8 bytes per slot).
static long ReleaseWideSlots(F3SharedBuffer **start, F3SharedBuffer **end)
{
    long result = 0;
    for (F3SharedBuffer **p = start; p != end; p += 2)
        result = ReleaseSharedSlotWide(p);
    return result;
}

//   sub_145F940 (0x145F940 - 0x145FBA4)  -- the wide-slot array resize engine.
//   obj = the sub-struct pointer (self+8); newSlot = the 8-byte wide slot to
//   add ([0] = block pointer, [1] = state).  Allocates a new array, copies the
//   old slots, fills the new array at index `used` with the new slot, frees
//   the old array, and re-points the sub-struct at the new array.
//   [NOT byte-identical] placement-dependent E8 calls to sub_145E9F0 /
//   sub_145F4F0 / sub_145F320 / sub_145F710 / LHFree.
static void ResizeWideArray(BYTE *obj, F3SharedBuffer **newSlot)
{
    BYTE *start = *(BYTE **)(obj + 0x0C);
    BYTE *spare = *(BYTE **)(obj + 0x14);
    BYTE *end   = *(BYTE **)(obj + 0x10);
    unsigned int capacity = start != 0 ? ((unsigned int)(spare - start) >> 3) : 0u;
    unsigned int used = (unsigned int)(end - start) >> 3;

    if (0x1FFFFFFFu - used < 1u)
        throw 1;   // the original throws std::length_error

    // new capacity: 1.5x, clamped to used + 1
    unsigned int newCount;
    if (0x1FFFFFFF - (capacity >> 1) >= capacity)
        newCount = capacity + (capacity >> 1);
    else
        newCount = 0;
    if (newCount < used + 1u)
        newCount = used + 1u;

    BYTE *newArray = WideArrayAlloc(newCount);

    // fill the new slot at index `used` (the next empty slot)
    WideSlotFill(newArray + 8 * used, 1, newSlot);

    // copy the old slots into the new array
    WideSlotCopy(start, end, newArray);

    // release and free the old array
    if (start != 0)
    {
        ReleaseWideSlots((F3SharedBuffer **)start, (F3SharedBuffer **)end);
        LHFree(start);
    }

    // repoint the array
    *(BYTE **)(obj + 0x0C) = newArray;                 // start
    *(BYTE **)(obj + 0x14) = newArray + 8 * newCount;  // spare
    *(BYTE **)(obj + 0x10) = newArray + 8 * (used + 1);// end
}

//   sub_145FC60 (0x145FC60 - 0x145FC98)  -- the resize wrapper called from the
//   RESIZE branch of sub_145FD10.  Reproduces the fall-through guard block
//   (boundary checks against the notification-object cluster) and then calls
//   sub_145F940 (ResizeWideArray).  The caller discards the return value
//   (the resize result block) -- the side effects are the resize itself.
static void ResizeWideArrayWrap(BYTE *obj, F3SharedBuffer **newSlot)
{
    BYTE *start = *(BYTE **)(obj + 0x0C);
    BYTE *end   = *(BYTE **)(obj + 0x10);
    const BYTE *notif = *(BYTE **)obj;   // the notification object pointer

    // (end - start) is always a multiple of 8 for a well-formed array; the
    // misalignment guard is never taken on this path.
    if ((end - start) & ~7u)
    {
        if (start > end)
            F3InvalidParam();
        if (notif == 0)
            F3InvalidParam();
    }

    // the notification-object pointer is always valid here (the original
    // warns via the v11/v13 checks below and falls through).
    if (notif == 0)
        F3InvalidParam();

    ResizeWideArray(obj, newSlot);

    // boundary checks on the new start (fall-through warnings)
    BYTE *newStart = *(BYTE **)(obj + 0x0C);
    if (newStart > *(BYTE **)(obj + 0x10))
        F3InvalidParam();

    const BYTE *nObj = *(BYTE **)obj;
    if (nObj != 0)
    {
        // the notification object's first DWORD is a pointer; the original
        // compares the new start against [firstField + 0x10] and [firstField + 0x0C]
        BYTE *firstField = *(BYTE **)nObj;
        unsigned int v12 = (unsigned int)newStart;
        if (v12 > *(DWORD *)(firstField + 0x10))
            F3InvalidParam();
        if (v12 < *(DWORD *)(firstField + 0x0C))
            F3InvalidParam();
    }
}

//   sub_145FD10 (0x145FD10 - 0x145FD77)  -- the wide-slot array insert/resize.
//   obj = the sub-struct pointer (self+8); newSlot = the 8-byte wide slot to
//   add ([0] = block pointer, [1] = state).  Returns the state of the new
//   slot (or 0 on the resize path).
static DWORD WideArrayInsert(BYTE *obj, F3SharedBuffer **newSlot)
{
    BYTE *start = *(BYTE **)(obj + 0x0C);
    BYTE *spare = *(BYTE **)(obj + 0x14);
    BYTE *end   = *(BYTE **)(obj + 0x10);

    unsigned int capacity = start != 0 ? ((unsigned int)(spare - start) >> 3) : 0u;
    unsigned int used = (unsigned int)(end - start) >> 3;

    if (used >= capacity)
    {
        // RESIZE path -- the array is full
        if (start > end)
            F3InvalidParam();
        ResizeWideArrayWrap(obj, newSlot);
        return 0;
    }

    // INSERT path -- the array has room
    DWORD result = 0;
    if (end != 0)
    {
        CopySharedSlotWide((F3SharedBuffer **)end, newSlot);
        result = ((DWORD *)newSlot)[1];
        ((DWORD *)end)[1] = result;
    }
    *(BYTE **)(obj + 0x10) = end + 8;
    return result;
}

// F3AudioNotifQI  (0x145DB60 - 0x145DBC0)  -- QueryInterface for the IMMNotificationClient.
// stdcall, 3 stack args (self / riid / ppv), retn 0x0C.  If ppv is NULL it
// returns 0x80070057; otherwise *ppv is cleared, then if riid matches neither
// accepted IID the call returns 0x80004002 (E_NOINTERFACE).  On a match, *ppv
// is set to the caller and the object's vtable[1] (AddRef) is dispatched, and
// S_OK (0) is returned.  [NOT byte-identical] placement-dependent (relative
// E8 call to the 16-byte IID compare sub_5AE060 + the indirect AddRef dispatch
// through the object's vtable + the high-VA IID globals).
static long __stdcall F3AudioNotifQI(void *self, const void *riid, void **ppv)
{
    if (ppv == NULL)
        return (long)0x80070057;   // 0x80070057: no ppv
    *ppv = 0;
    if (memcmp(riid, &g_riid_1BC57FC, sizeof(IID)) != 0 &&
        memcmp(riid, &g_riid_1D6A710, sizeof(IID)) != 0)
        return (long)0x80004002;   // E_NOINTERFACE
    *ppv = self;
    ((void (WINAPI *)(void *))F3_VT(self)[1])(self);   // AddRef (vtable[1])
    return 0;                                          // S_OK
}
// F3AudioNotifAddRef  (0x145DBC0 - 0x145DBD5)  -- AddRef on the IMMNotificationClient.
// stdcall, 1 stack arg (self), retn 4.  InterlockedIncrement the LONG at self+4
// and return the new value (read back from memory, as the original does).
// [NOT byte-identical] (placement-dependent E8 call to the InterlockedIncrement
// IAT slot -- the original 21-byte body is push esi / mov esi,[esp+4] / add esi,4
// / push esi / call __imp_InterlockedIncrement / mov eax,[esi] / pop esi / retn 4).
static long __stdcall F3AudioNotifAddRef(void *self)
{
    LONG *p = (LONG *)((BYTE *)self + 4);
    InterlockedIncrement(p);
    return (long)(*p);
}
// F3AudioNotifSubStructDtor  (0x145F770 - 0x145F7B6)  -- destroy the wide
// slot-array sub-struct inside the IMMNotificationClient (thiscall in the
// original: this in ecx, plain `retn`; declared as a free function here).
// The sub-struct lives at self+8. Its fields: [+0x0C] = the wide-slot array
// start (void**), [+0x10] = the array end (void**), [+0x14] = a spare pointer.
// When the start is non-null the array is walked (8 bytes per 8-byte element,
// the slot at element offset 0) and each slot is released via
// ReleaseSharedSlotWide, then the array itself is freed with LHFree. The three
// fields are always zeroed. [NOT byte-identical] (placement-dependent E8 calls
// to ReleaseSharedSlotWide / LHFree).
static void F3AudioNotifSubStructDtor(BYTE *obj)
{
    BYTE *start = *(BYTE **)(obj + 0x0C);
    if (start != 0)
    {
        BYTE *end = *(BYTE **)(obj + 0x10);
        for (BYTE *p = start; p != end; p += 8)
            ReleaseSharedSlotWide((F3SharedBuffer **)p);
        LHFree(start);
    }
    *(DWORD *)(obj + 0x0C) = 0;
    *(DWORD *)(obj + 0x10) = 0;
    *(DWORD *)(obj + 0x14) = 0;
}
// F3AudioNotifRelease  (0x145E400 - 0x145E43C)  -- Release on the
// IMMNotificationClient (vtable slot 2, 0x01B6F408).
// stdcall, 1 stack arg (self), retn 4.  InterlockedDecrement the LONG at
// self+4; if the new value is 0 the sub-struct at self+8 is torn down
// (F3AudioNotifSubStructDtor), the inner 4-byte heap block at self+8 is freed
// via LHFree, and the whole object is freed via LHFree. Returns the
// decremented refcount.  [NOT byte-identical] (placement-dependent E8 calls to
// the InterlockedDecrement IAT slot, F3AudioNotifSubStructDtor, and LHFree).
static long __stdcall F3AudioNotifRelease(void *self)
{
    LONG v1 = InterlockedDecrement((LONG *)((BYTE *)self + 4));
    if (*(DWORD *)((BYTE *)self + 4) == 0)
    {
        F3AudioNotifSubStructDtor((BYTE *)self + 8);
        LHFree(*(void **)((BYTE *)self + 8));
        LHFree(self);
    }
    return v1;
}
// F3AudioNotifAdded  (0x145DB00 - 0x145DB1E)  -- IMMNotificationClient "Added" callback (vtable slot 4, 0x01B6F408).
// stdcall, 2 stack args (self / id), retn 8.  The first argument is a wide
// string carried in the notification; the second is the (ignored) notification
// id.  The body is the sub_1A00310 wide-slot ctor followed immediately by the
// ReleaseSharedSlotWide destructor:
//   1. the slot (the address of the first parameter) is nulled, and the wide
//      live-slot counter is bumped (InterlockedIncrement on g_WideSlotCount);
//   2. when the wide string is non-empty (non-NULL and not an empty string),
//      a fresh 16-byte wide block is built from it via SharedBlockFromWide
//      (LHAlloc(16), the block's data buffer, length = wcslen -- the -1 case)
//      and stored in the slot;
//   3. the slot is destroyed on the spot by ReleaseSharedSlotWide -- the
//      block's refcount (1) is dropped, on zero it is freed through the
//      family's SharedBufferFree, and the slot is nulled -- so the build/tear-
//      down nets to a no-op that returns 0 (the wide counter goes +1/-1).
// The sub_1A00310 ctor's old-block-release branch (the `*this != 0` test
// after the null-store) is dead at this point -- the slot is always 0 there --
// so it is not reproduced.  [NOT byte-identical] placement-dependent (relative
// E8 calls to the InterlockedIncrement IAT slot, SharedBlockFromWide and the
// ReleaseSharedSlotWide/SharedBufferFree family -- the original 0x1E-byte body
// is `mov eax,[esp+arg_4]` / `push 0xFFFFFFFFh` / `push eax` / `lea ecx,
// [esp+8+arg_4]` / `call sub_1A00310` / `lea ecx,[esp+arg_4]` / `call
// ReleaseSharedSlotWide` / `xor eax,eax` / `retn 8`).
static long __stdcall F3AudioNotifAdded(void *self, void *id)
{
    WCHAR *wide = (WCHAR *)self;   // the wide string carried in the first arg
    self = 0;                       // ctor: *this = 0
    InterlockedIncrement(&g_WideSlotCount);
    if (wide != 0 && wide[0] != 0)
        self = (void *)SharedBlockFromWide(wide, -1);
    // (the ctor's old-block release branch is dead: the slot is 0 here)
    ReleaseSharedSlotWide((F3SharedBuffer **)&self);
    (void)id;
    return 0;
}
// F3AudioNotifStateChanged (sub_145E560)  -- IMMNotificationClient "StateChanged" callback.
// stdcall, 3 stack args (self / id / st), retn 0x0C.  The first parameter is the
// IMMNotificationClient object (self); the second (id) is the wide-string
// notification payload carried by the event; the third (st) is the state word.
// The body is:
//   1. sub_1A00310 (the wide-slot ctor): the id slot (the address of the second
//      parameter) is nulled, the wide live-slot counter is bumped
//      (InterlockedIncrement on g_WideSlotCount), and when the wide string is
//      non-empty a fresh 16-byte wide block is built from it via
//      SharedBlockFromWide (the -1 auto-count case) and stored in the slot.
//   2. The sub-struct at self+8 is walked: [self+0x14] (start) is validated
//      against [self+0x18] (end); each slot is compared against the fresh id
//      block (by block pointer, or by length + data contents via
//      CompareWideStrings).  On a match the slot's state (second DWORD) is
//      overwritten with the state word; the fresh id block is released and
//      the function returns 0.
//   3. No match (the slot is empty / the scan walked past `end`) -- the ADDED
//      path: a fresh local wide slot is initialised (InitSharedSlotWide), the
//      id's block is assigned into it (AssignSharedSlotWide), the state is
//      stored, the slot-array sub-struct is resized to add the new slot
//      (sub_145FD10 / ResizeWideArray, grow-new-array path), the local new
//      slot and the fresh id block are released, and the function returns 0.
// [NOT byte-identical] placement-dependent (the InterlockedIncrement IAT slot,
// SharedBlockFromWide, the CopySharedSlotWide / ReleaseSharedSlotWide family,
// LHAlloc / LHFree and the wide-slot array resize helpers).
static long __stdcall F3AudioNotifStateChanged(void *self, void *id, unsigned st)
{
    // the wide-slot ctor (sub_1A00310): null the id slot, bump the counter,
    // build a fresh wide block from the id (the -1 auto-count case).
    WCHAR *wide = (WCHAR *)id;
    id = 0;
    InterlockedIncrement(&g_WideSlotCount);
    if (wide != 0 && wide[0] != 0)
        id = (void *)SharedBlockFromWide(wide, -1);

    BYTE *start = *(BYTE **)((BYTE *)self + 0x14);
    BYTE *end   = *(BYTE **)((BYTE *)self + 0x18);
    BYTE *raw   = *(BYTE **)((BYTE *)self + 8);   // the notification object pointer
    F3SharedBuffer *newSlot = (F3SharedBuffer *)id;

    if ((unsigned int)start > (unsigned int)end)
        F3InvalidParam();
    if (*(DWORD *)((BYTE *)self + 0x14) > (unsigned int)end)
        F3InvalidParam();

    for (;;)
    {
        if (raw == 0)
            F3InvalidParam();
        if (start == end)
        {
            // ADDED path -- no matching slot; the array is empty or the scan
            // walked past `end`.  Allocate a fresh local wide slot, seed it
            // with the fresh id block, set the state, and resize the array.
            BYTE localSlot[8];
            InitSharedSlotWide((F3SharedBuffer **)localSlot);
            AssignSharedSlotWide((F3SharedBuffer **)localSlot, (F3SharedBuffer **)&id);
            ((DWORD *)localSlot)[1] = (DWORD)st;
            WideArrayInsert((BYTE *)self + 8, (F3SharedBuffer **)localSlot);
            ReleaseSharedSlotWide((F3SharedBuffer **)localSlot);
            ReleaseSharedSlotWide((F3SharedBuffer **)&id);
            return 0;
        }
        // the boundary check against the notification-object cluster
        // (fall-through warning)
        if (raw != 0 && (unsigned int)start >= *(DWORD *)(*(BYTE **)raw + 0x10))
            F3InvalidParam();
        F3SharedBuffer *slot = *(F3SharedBuffer **)start;
        if (slot == newSlot ||
            (slot != 0 && newSlot != 0 &&
             ((DWORD *)slot)[1] == ((DWORD *)newSlot)[1] &&
             CompareWideStrings((WCHAR *)((F3SharedBuffer *)slot)->data,
                                (WCHAR *)((F3SharedBuffer *)newSlot)->data) == 0))
        {
            // matched -- the state slot is updated in place
            break;
        }
        // the boundary check again (fall-through warning)
        if (raw != 0 && (unsigned int)start >= *(DWORD *)(*(BYTE **)raw + 0x10))
            F3InvalidParam();
        start += 8;
    }

    // the matched slot: write the new state word and release the fresh id block
    if (raw == 0)
        F3InvalidParam();
    if (raw != 0 && (unsigned int)start >= *(DWORD *)(*(BYTE **)raw + 0x10))
        F3InvalidParam();
    *(DWORD *)(start + 4) = (DWORD)st;
    ReleaseSharedSlotWide((F3SharedBuffer **)&id);
    return 0;
}
// F3AudioNotifRemoved  (0x145DB20 - 0x145DB3E)  -- IMMNotificationClient "Removed" callback (vtable slot 5, 0x01B6F408).
// stdcall, 2 stack args (self / id), retn 8.  The first argument (self) is the
// notification object/client side; the original body never touches it (the
// `int a1` in the pseudocode is deliberately ignored).  The second argument
// (id) is the wide string carried in the removal event.  The body is the
// sub_1A00310 wide-slot ctor followed immediately by the ReleaseSharedSlotWide
// destructor -- the same 0x1E-byte body as F3AudioNotifAdded:
//   1. the slot (the address of the second parameter) is nulled, and the wide
//      live-slot counter is bumped (InterlockedIncrement on g_WideSlotCount);
//   2. when the wide string is non-empty (non-NULL and not an empty string),
//      a fresh 16-byte wide block is built from it via SharedBlockFromWide
//      (the -1 auto-count case) and stored in the slot;
//   3. the slot is destroyed on the spot by ReleaseSharedSlotWide -- the
//      block's refcount (1) is dropped, on zero it is freed through the
//      family's SharedBufferFree, and the slot is nulled -- so the build/tear-
//      down nets to a no-op that returns 0 (the wide counter goes +1/-1).
// The sub_1A00310 ctor's old-block-release branch (the `*this != 0` test
// after the null-store) is dead at this point -- the slot is always 0 there --
// so it is not reproduced.  [NOT byte-identical] placement-dependent (relative
// E8 calls to the InterlockedIncrement IAT slot, SharedBlockFromWide and the
// ReleaseSharedSlotWide/SharedBufferFree family -- the original 0x1E-byte body
// is `mov eax,[esp+arg_4]` / `push 0xFFFFFFFFh` / `push eax` / `lea ecx,
// [esp+8+arg_4]` / `call sub_1A00310` / `lea ecx,[esp+arg_4]` / `call
// ReleaseSharedSlotWide` / `xor eax,eax` / `retn 8`).
// NOTE on the arg convention: the IDA pseudocode is
// `int __stdcall sub_145DB20(int a1, _WORD *a2)` -- the wide string is the
// SECOND stack argument (and the client/notification side is the ignored
// FIRST stack argument).  This is the faithful reading of the original body
// (the identical 0x1E-byte shape as F3AudioNotifAdded, but with the same
// _WORD * slot); in this build the callbacks never fire (neither Added nor
// Removed is reachable from the engine test), so the distinction is latent.
static long __stdcall F3AudioNotifRemoved(void *self, void *id)
{
    (void)self;
    WCHAR *wide = (WCHAR *)id;   // the wide string carried in the second arg
    id = 0;                       // ctor: *this = 0
    InterlockedIncrement(&g_WideSlotCount);
    if (wide != 0 && wide[0] != 0)
        id = (void *)SharedBlockFromWide(wide, -1);
    // (the ctor's old-block release branch is dead: the slot is 0 here)
    ReleaseSharedSlotWide((F3SharedBuffer **)&id);
    return 0;
}
// F3AudioNotifDefaultRole  (0x145DB40 - 0x145DB5F)  -- IMMNotificationClient
// "DefaultRole" callback (vtable slot 7, 0x01B6F408).  thiscall, 7 stack args
// (obj / a2 / wide-string / a4..a8), callee cleans the args (retn 0x1C).
// This is the faithful reading of the original body (0x1F bytes: push ecx /
// mov eax,[esp+0x0C] / push 0xFFFFFFFFh / push eax / lea ecx,[esp+8] / call
// sub_1A00310 / lea ecx,[esp] / call ReleaseSharedSlotWide / xor eax,eax /
// pop ecx / retn 0x1C) -- the same wide-slot ctor + destructor pattern as
// F3AudioNotifAdded / F3AudioNotifRemoved (stdcall, 2 stack args), but thiscall
// with 7 stack args instead.  The this pointer (ECX) is never used by the
// body -- it is saved and restored (the saved-this cell is repurposed as the
// 4-byte slot cell).  The wide string is the second stack arg; the other
// stack args are ignored.  The body:
//   1. the slot (the saved-this cell, nulled by the ctor) is built from the
//      wide string passed in the second stack arg -- the slot is nulled, and
//      the wide live-slot counter is bumped (InterlockedIncrement on
//      g_WideSlotCount);
//   2. when the wide string is non-empty (non-NULL and not an empty string),
//      a fresh 16-byte wide block is built from it via SharedBlockFromWide
//      (the -1 auto-count case) and stored in the slot;
//   3. the slot is destroyed on the spot by ReleaseSharedSlotWide -- the
//      block's refcount (1) is dropped, on zero it is freed through the
//      family's SharedBufferFree, and the slot is nulled -- so the build/tear-
//      down nets to a no-op that returns 0 (the wide counter goes +1/-1).
// The sub_1A00310 ctor's old-block-release branch (the `*this != 0` test
// after the null-store) is dead at this point -- the slot is always 0 there --
// so it is not reproduced.  [NOT byte-identical] placement-dependent
// (relative E8 calls to the InterlockedIncrement IAT slot, SharedBlockFromWide
// and the ReleaseSharedSlotWide/SharedBufferFree family -- the original
// 0x1F-byte body is the same wide-slot ctor + destructor sequence as
// F3AudioNotifRemoved).
static long F3AudioNotifDefaultRole(void *obj, unsigned a2, void *wide,
                                   unsigned a4, unsigned a5, unsigned a6,
                                   unsigned a7, unsigned a8)
{
    (void)obj; (void)a2; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    WCHAR *w = (WCHAR *)wide;   // the wide string carried in the second stack arg
    wide = 0;                   // ctor: slot = 0
    InterlockedIncrement(&g_WideSlotCount);
    if (w != 0 && w[0] != 0)
        wide = (void *)SharedBlockFromWide(w, -1);
    // (the ctor's old-block release branch is dead: the slot is 0 here)
    ReleaseSharedSlotWide((F3SharedBuffer **)&wide);
    return 0;
}
// F3AudioNotifPropertyChanged  -- IMMNotificationClient "OnPropertyValueChanged" callback (vtable slot 8, 0x01B6F408).
// DESIGN NOTE -- there is no binary counterpart to decompile: the original
// vtable at 0x01B6F408 has exactly 8 slots (0x145DB60/0x145DBC0/0x145E400/0x145E560/0x145DB00/0x145DB20/0x145E040/0x145DB40),
// the 9th slot (0x1B6F428) is the ASCII string "Could not find specified document" (referenced by sub_145E440's
// SetSaveGamePath, not a function), and the SDK's IMMNotificationClient
// (IID 0x7991EEC9-7E89-4D85-8390-6C703CEC60C0) has exactly 8 slots -- slot 7 is OnPropertyValueChanged, implemented
// in the binary as 0x145DB40 (F3AudioNotifDefaultRole) -- so this slot is a PHANTOM safety filler added by the
// original C++ code, not an interface method.  The faithful behavior is therefore an empty no-op that returns 0
// (S_OK): the caller's arity must clean the right number of stack bytes so any accidental vtable dispatch does
// not corrupt the stack.  The full SDK signature is OnPropertyValueChanged(LPCWSTR pwstrDeviceId,
// const PROPERTYKEY key) -- LPCWSTR (4 bytes) + a 20-byte PROPERTYKEY -- with `this` in ECX; the game passes the
// notification client as the first stack arg (stdcall convention, like F3AudioNotifDefaultFlow / F3AudioNotifQI),
// so the stub carries (self, id, key) with callee-clean args (retn 0x1C).  All three arguments are deliberately
// ignored: the original implementation (slot 7 / 0x145DB40, F3AudioNotifDefaultRole) is itself the wide-slot
// ctor + destructor no-op, and no slot 8 exists in the original binary at all.
static long __stdcall F3AudioNotifPropertyChanged(void *self, void *id, F3AudioPropKey key)
{
    (void)self; (void)id; (void)key;
    return 0;
}
static void F3AudioNotifInitXAudio2System(void);
static void F3AudioNotifShutdownXAudio2System(void);
static long __stdcall F3AudioNotifDefaultFlow(void *self, unsigned a, unsigned b, void *c);
static const void *F3AudioNotifVtable[] = {
    (const void *)F3AudioNotifQI, (const void *)F3AudioNotifAddRef,
    (const void *)F3AudioNotifRelease, (const void *)F3AudioNotifStateChanged,
    (const void *)F3AudioNotifAdded, (const void *)F3AudioNotifRemoved,
    (const void *)F3AudioNotifDefaultFlow, (const void *)F3AudioNotifDefaultRole,
    (const void *)F3AudioNotifPropertyChanged
};
static void *g_off_1D6A768 = (void *)F3AudioNotifVtable;   // 0x1D6A768: the IMMNotificationClient (vtable = first field)
// The global 80-byte VFS descriptor (0x1D6A720), default-initialised exactly
// like VfsDescDefaultCtor, plus d[18] = 0x01B6F408 and d[19] = 0.
static DWORD g_dword_1D6A720[20] = { 0, 0x32, 0, 0x6000, 3, 0x2000000, 0x42, 6,
    0x8000, 0x40000, 0x48, 0x0A, 0, 0, 2, 0, 1, 0, 0x01B6F408, 0 };


// VfsDescAssign  (0x145EC90 - 0x145ED25)  -- copy-assign an 80-byte VFS
//  descriptor (thiscall + retn 4: this in ECX, the source on the stack).
//  Releases the source's +0x00 object (vtable slot 1) and the destination's
//  +0x00 object (vtable slot 2), then copies d[0..14], the 0x3C byte, and
//  d[16..17].  Returns this.
static void *VfsDescAssign(void *obj, void *src)
{
    DWORD *o = (DWORD *)obj;
    DWORD *s = (DWORD *)src;
    int i;
    if (s[0] != 0) F3CallVtable((void *)s[0], 1);
    if (o[0] != 0) F3CallVtable((void *)o[0], 2);
    for (i = 0; i <= 14; ++i) o[i] = s[i];     // dwords 0x00..0x38
    ((BYTE *)obj)[0x3C] = ((BYTE *)src)[0x3C]; // the 0x3C byte
    o[0x40 / 4] = s[0x40 / 4];                  // d[16]
    o[0x44 / 4] = s[0x44 / 4];                  // d[17]
    return obj;
}

// VirtualFileSystemCtor  (0x18A98F0 - 0x18A9961)  -- construct the 88-byte
//  LHVFS::CVirtualFileSystem object (thiscall in ECX).  Installs the
//  CAddRelQueryImp / CVirtualFileSystem vtables, initialises the critical
//  section at +8, and builds the two 20-byte intrusive list heads at +0x20 /
//  +0x38 (each with a 4-byte heap back-pointer).  Returns this.
static void *VirtualFileSystemCtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    BYTE  *b = (BYTE *)obj;
    o[0] = 0x1BF77B4;   // &LHCOMUtils::CAddRelQueryImp<IVirtualFileSystem,...>::vftable
    o[1] = 0;
    InitializeCriticalSection((LPCRITICAL_SECTION)((char *)obj + 8));
    o[0] = 0x1BF7778;   // &LHVFS::CVirtualFileSystem::vftable
    DWORD *n1 = (DWORD *)((char *)obj + 0x20);
    void *h1 = LHAlloc(4); if (h1 != 0) *(void **)h1 = n1;
    n1[0x00 / 4] = (DWORD)h1;
    n1[0x0C / 4] = 0; n1[0x10 / 4] = 0; n1[0x14 / 4] = 0;
    DWORD *n2 = (DWORD *)((char *)obj + 0x38);
    void *h2 = LHAlloc(4); if (h2 != 0) *(void **)h2 = n2;
    n2[0x00 / 4] = (DWORD)h2;
    n2[0x0C / 4] = 0; n2[0x10 / 4] = 0; n2[0x14 / 4] = 0;
    b[0x51] = 0;
    b[0x50] = 1;
    return obj;
}

// CreateFallbackVfs  (0x145EF80 - 0x145EFC8)  -- allocate (LHAlloc 88) and
//  construct the NLHAudio::CFallbackVFS object, storing its pointer at *out.
//  Sets the +0x54 slot to archiveObj (taking a reference: vtable slot 1 on
//  archiveObj when non-null), then takes a reference on the new object
//  (vtable slot 1).  Returns out.
static void *CreateFallbackVfs(void *out, DWORD archiveObj)
{
    void *obj = LHAlloc(0x58);   // 88
    if (obj != 0) {
        VirtualFileSystemCtor(obj);                       // 0x18A98F0
        *(void **)obj = (void *)0x1B6F390;                // &NLHAudio::CFallbackVFS::vftable
        *(DWORD *)((char *)obj + 0x54) = archiveObj;
        if (archiveObj != 0) F3CallVtable((void *)archiveObj, 1);
    }
    *(void **)out = obj;
    if (obj != 0) F3CallVtable(obj, 1);
    return out;
}

// VfsComponentMapCtor  (0x14B2460)  -- remaining stub: the three NSupport
//  intrusive CMap containers (the 48-byte wrapper built by VfsRefCountCtor).
//  Not implemented in this build; returns its (ECX) argument unchanged.
static void *VfsComponentMapCtor(void *obj) { return obj; }

// VfsRefCountCtor  (0x146DEC0 - 0x146DEEC)  -- construct the 4-byte VFS
//  refcount wrapper (thiscall in ECX): *this = 0, then LHAlloc(48) and build
//  the component map (VfsComponentMapCtor) into the slot (0 on failure).
static void *VfsRefCountCtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    o[0] = 0;
    void *mem = LHAlloc(0x30);   // 48
    o[0] = (mem != 0) ? (DWORD)VfsComponentMapCtor(mem) : 0;
    return obj;
}

// ============================================================
//  InitVirtualFileSystem  (0x1460650 - 0x1460701)  -- the LHVFS audio
//  virtual-filesystem bring-up that CreateVirtualFilesystem runs after the
//  fallback VFS is published (reached only when the descriptor's +0x00 archive
//  slot is non-null).  It is a one-time (g_byte_1DDC828) chain that, in order:
//    1. brings up the 7-thread worker pool       (VfsWorkerPoolInit,        0x1476B50)
//    2. initialises the small allocation cache   (VfsAllocCacheInit,        0x14743F0)
//    3. identity-probes the descriptor cache     (VfsAllocCacheIdentity,    0x146D870,
//       + the concurrency reader-writer-lock init, VfsCacheQueryInit        0x146D8E0 -> 0x14972C0)
//    4. builds the audio process delegate        (AudioProcessDelegateInit, 0x147A750)
//    5. runs the two provider capability queries (VfsQueryProviderPair,     0x1477990)
//    6. allocates + builds the 16-byte CProcessDelegateThiscall object into
//       g_dword_1DDC820, builds the 80/4096 main allocation cache into
//       g_unk_1DDC830, and registers the {vtable, VfsGetQueryTable} pair with
//       worker slot 0 (VfsWorkerCallbackRegister,                          0x1476D00)
//  returning 1 (success) only when every step succeeds; 0 otherwise (and on a
//  repeat call, g_byte_1DDC828 already set).  desc[1]/desc[2] (the descriptor's
//  +0x04/+0x08) are the worker count / ideal-processor affinity.
//
//  [NOT byte-identical] placement-dependent: ten relative E8 calls (the worker-
//  pool / allocation-cache / process-delegate / provider-query / query-table
//  subsystem callees + the LHAlloc IAT thunk) and the high-VA globals
//  g_byte_1DDC828 / g_dword_1DDC820 / g_unk_1DDC830 / g_off_1D6A80C.
//
//  The deep leaves of the audio/VFS subsystem (the engine fader CMap, the
//  provider buffer allocation, the query-table build / drain, the worker
//  add-callback) are documented stubs returning their fresh-state success
//  values, matching the original's happy path when given a valid VFS descriptor.

// ---- audio/VFS bring-up globals (original BSS addresses kept) ----
static const DWORD kCProcessDelegateThiscallVftable = 0x01B6FCC0;  // ??_7 CProcessDelegateThiscall<CEngine>::`vftable'
static const DWORD kCProcessDelegateStaticVftable   = 0x01B6F768;  // ??_7 CProcessDelegateStatic::`vftable'

static F3SharedBuffer *g_dword_1DDC818 = 0;   // 0x1DDC818: the audio mastering-profile path slot
static F3SharedBuffer *g_dword_1DDC81C = 0;   // 0x1DDC81C: the GAME path slot (the ApplyMasteringProfile path)
static BYTE  g_byte_1DDC828 = 0;              // 0x1DDC828: InitVirtualFileSystem done flag
static DWORD g_dword_1DDC820 = 0;             // 0x1DDC820: the CProcessDelegateThiscall object (16 bytes)
static DWORD g_dword_1DDC824 = 0;             // 0x1DDC824: query-table mode (2 = drain)
static DWORD g_off_1D6A80C[2] = { kCProcessDelegateStaticVftable, 0 };  // 0x1D6A80C: {vtable, VfsGetQueryTable}
static DWORD g_off_1D6E9C8[2] = { kCProcessDelegateStaticVftable, 0 };  // 0x1D6E9C8: {vtable, AudioProcessDelegateTick}
static BYTE  g_byte_1DDCF28 = 0;              // 0x1DDCF28: slot-1 callback registered flag
static DWORD g_dword_1DDCF2C = 0;             // 0x1DDCF2C: slot-1 callback pair value
static BYTE  g_unk_1DDCF30 = 0;               // 0x1DDCF30: slot-1 callback pair (address taken)
static BYTE  g_byte_1DDCFB0 = 0;              // 0x1DDCFB0: slot-2 callback registered flag
static DWORD g_dword_1DDCFB4 = 0;             // 0x1DDCFB4: slot-2 callback pair value
static BYTE  g_unk_1DDCFB8 = 0;               // 0x1DDCFB8: slot-2 callback pair (address taken)
static BYTE  g_unk_1DDCF00[0x20] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};  // 0x1DDCF00: the second 0x20-byte audio queue control block (all 0xFF = empty)
static BYTE  g_unk_1DDCF88[0x20] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};  // 0x1DDCF88: the first 0x20-byte audio queue control block (all 0xFF = empty)

// worker-pool / allocation-cache state (BSS, zero-initialised here)
static BYTE  g_byte_1DDC9C0 = 0;              // 0x1DDC9C0: worker pool initialised
static BYTE  g_byte_1DDC9C1 = 0;              // 0x1DDC9C1: worker pool high-bit
static DWORD g_dword_1DDCB50 = 0;             // 0x1DDCB50: pool worker count (desc[1])
static DWORD g_unk_1DDCB38[7 * 9];            // 0x1DDCB38: the seven 36-byte worker objects
static DWORD g_dword_1DDC9C8[7 * 7];          // 0x1DDC9C8: per-worker callback-list head
static DWORD g_dword_1DDC9DC[7 * 7];          // 0x1DDC9DC: per-worker callback-list data
static BYTE  g_byte_1DDC94C = 0;              // 0x1DDC94C: small (48/2048) allocation-cache init flag
static BYTE  g_unk_1DDC950[28];               // 0x1DDC950: the 48/2048 allocation-cache object
static DWORD g_dword_1DDCCD0 = 0;             // 0x1DDCCD0: the audio-engine object (340 bytes)
static BYTE  g_unk_1DDCC40[72];               // 0x1DDCC40: provider query cache #1
static BYTE  g_unk_1DDCC88[72];               // 0x1DDCC88: provider query cache #2
static DWORD g_dword_1DDCD74 = 0;             // 0x1DDCD74: the concurrency reader-writer-lock object
static DWORD g_dword_1DDCCD8 = 0xffffffff;   // 0x1DDCCD8: the concurrency RW-lock critical-section source object (initialized at startup; 0xffffffff placeholder in the file image)
static DWORD g_dword_1DDCD34 = 0;             // 0x1DDCD34: the commit-command counter (incremented by CommitCommand)
static BYTE  g_byte_1DDCCE0 = 0;              // 0x1DDCCE0: the SetProcessedCommitNumber gate flag
static BYTE  g_unk_1DDCCE8[28];               // 0x1DDCCE8: the audio-comms command-wrapper allocation cache object (28 bytes)
static const DWORD kCSetProcessedCommitNumberVftable = 0x01B71390;  // ??_7 CSetProcessedCommitNumber::`vftable'
static const DWORD kCCommitCommandVftable     = 0x01B71450;  // ??_7 CCommitCommand::`vftable'

// GetVfsDesc  (0x145DAF0 - 0x145DAF5)  -- return the global 80-byte VFS descriptor.
static DWORD *GetVfsDesc(void) { return g_dword_1D6A720; }

// VfsWorkerCtor  (0x18C5A60 - 0x18C5A89)  -- start one worker: mark the +0x0C
//  state byte 1 (2 on the sub_18C59D0 failure).  [STUB] the thread-spawn callee
//  sub_18C59D0 is unimplemented, treated as success.
static int VfsWorkerCtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    if (o[0x0C / 4] == 1) return 0;
    o[0x0C / 4] = 1;
    return 1;
}

// VfsWorkerSetAffinity  (0x18C5970 - 0x18C597D)  -- SetThreadIdealProcessor on
//  the worker's +0x04 thread handle (0 pre-spawn), so this is a no-op here.
static DWORD VfsWorkerSetAffinity(void *obj, DWORD proc)
{
    DWORD *o = (DWORD *)obj;
    return SetThreadIdealProcessor((HANDLE)o[0x04 / 4], proc);
}

// VfsWorkerPoolInit  (0x1476B50 - 0x1476BA8)  -- bring up the 7-thread VFS
//  worker pool (one-time, g_byte_1DDC9C0).  Seeds g_dword_1DDCB50 = a2, builds
//  the seven 36-byte workers (g_unk_1DDCB38 + i*0x24, +0x14 = i) each via
//  VfsWorkerCtor + VfsWorkerSetAffinity(dwIdealProcessor), sets the flag and
//  returns 1; returns 0 when already initialised.
static int VfsWorkerPoolInit(DWORD dwIdealProcessor, int a2)
{
    if (g_byte_1DDC9C0 != 0)
        return 0;
    g_byte_1DDC9C1 = 0;
    g_dword_1DDCB50 = (DWORD)a2;
    for (int i = 0; i < 7; ++i) {
        DWORD *w = &g_unk_1DDCB38[i * 9];
        w[0x14 / 4] = i;                          // +0x14: the worker index
        VfsWorkerCtor(w);                         // 0x18C5A60
        VfsWorkerSetAffinity(w, dwIdealProcessor); // 0x18C5970
    }
    g_byte_1DDC9C0 = 1;
    return 1;
}

// VfsAllocCacheBlockCtor  (0x14741A0 - 0x147421A)  -- [STUB] build the per-
//  block metadata for one allocation-cache block; returns its (ECX) argument.
static void *VfsAllocCacheBlockCtor(void *obj) { return obj; }

// VfsAllocCacheCtor  (0x1473CF0 - 0x1473D64)  -- the 28-byte LHVFS allocation-
//  cache constructor (thiscall, retn 8: obj in ECX, a2/a3 on the stack).  No-op
//  (returns 0) when the +0x18 init flag is already set; otherwise records the
//  capacity (a3) at +0x10 and element size (a2+4) at +0x14, allocates the
//  contiguous block (a3*(a2+4)+272 + a2+4 bytes) into +0x00/+0x08, then the
//  268-byte control block (VfsAllocCacheBlockCtor over block+(a2+4)); a null
//  control block is tolerated (set to 0).  Sets the +0x18 flag and returns 1.
static int VfsAllocCacheCtor(void *obj, int a2, int a3)
{
    DWORD *o = (DWORD *)obj;
    if (o[0x18 / 4] != 0)
        return 0;
    o[0x10 / 4] = a3;
    int blockSize = a3 * (a2 + 4) + 272;
    o[0x14 / 4] = a2 + 4;
    void *block = LHAlloc(blockSize + a2 + 4);
    o[0] = (DWORD)block;
    o[2] = (DWORD)block;
    void *cache = LHAlloc(268);
    o[1] = (cache != 0) ? (DWORD)VfsAllocCacheBlockCtor((char *)block + (a2 + 4)) : 0;
    o[0x18 / 4] = 1;
    return 1;
}

// VfsAllocCacheInit  (0x14743F0 - 0x1474418)  -- one-time (g_byte_1DDC94C) init
//  of the small 48/2048 allocation cache (g_unk_1DDC950); returns 1 on the first
//  successful build, 0 otherwise.
static int VfsAllocCacheInit(void)
{
    if (g_byte_1DDC94C != 0)
        return 0;
    if (VfsAllocCacheCtor(g_unk_1DDC950, 48, 2048) == 0)
        return 0;
    g_byte_1DDC94C = 1;
    return 1;
}

// VfsAllocCacheIdentity  (0x146D870 - 0x146D872)  -- identity thiscall.
static void *VfsAllocCacheIdentity(void *obj) { return obj; }

// VfsAllocCacheAlloc  (0x1473D70 - 0x1473E12)  -- stage an allocation from a
//  28-byte LHVFS allocation cache (thiscall, retn 4: the cache in ECX, the
//  wait flag on the stack).  Returns 0 (no critical section taken) when the
//  cache is not initialised (+0x18 == 0).  Otherwise, under the cache
//  critical section (+0x1C, sub_1A00A60 / sub_1A00A50 wrappers): when no
//  staged object is pending (+0x0C == 0), free = cb[+0x100] - cb[+0x10C]
//  (cb = the 272-byte control block pointed to by the cache's +0x04 slot);
//  with the wait flag an alertable SleepEx(0, 1) spins until free >= the
//  element size (+0x14).  When there is room, the staged pointer =
//  cb + 0x110 + cb[+0x104] (the cursor) is stored in +0x0C and returned --
//  leaving the critical section LOCKED (VfsAllocCacheFinalize releases it).
//  0 otherwise (section released).  96 audio-comms command wrappers use the
//  pair; returns 0 in this build (g_unk_1DDC830 is zero BSS pre-VFS-init).
//  [NOT byte-identical] placement-dependent (relative calls to the
//  critical-section / SleepEx IAT thunks) and refs the high-VA cache object.
static void *VfsAllocCacheAlloc(void *cache, int wait)
{
    DWORD *o = (DWORD *)cache;
    if (o[0x18 / 4] == 0)
        return 0;
    EnterCriticalSection((LPCRITICAL_SECTION)((char *)cache + 0x1C));
    void *staged = 0;
    if (o[0x0C / 4] == 0) {
        DWORD *cb = (DWORD *)o[1];          // the +0x04 slot
        cb = (DWORD *)cb[0];                // ...points to the 272-byte control block
        DWORD free = cb[0x100 / 4] - cb[0x10C / 4];
        if (wait) {
            while (free < o[0x14 / 4]) {
                SleepEx(0, 1);
                free = cb[0x100 / 4] - cb[0x10C / 4];
            }
        }
        if (free >= o[0x14 / 4]) {
            staged = (char *)cb + 0x110 + cb[0x104 / 4];
            o[0x0C / 4] = (DWORD)staged;
            return staged;                  // the CS stays locked on success
        }
    }
    LeaveCriticalSection((LPCRITICAL_SECTION)((char *)cache + 0x1C));
    return 0;
}

// VfsAllocCacheFree  (0x1474240 - 0x14742AF)  -- return a staged slot to the
//  pool (thiscall, retn 4: the cache's +0x04 slot in ECX, the size on the
//  stack).  newCursor = cb[+0x104] + size (cb = the control block the slot
//  points at); `int 3` when it would run past the limit cb[+0x100] (a ring),
//  wrap the cursor to 0 exactly at the limit, store it at cb[+0x104], add
//  the size to the used counter (cb[+0x10C], InterlockedExchangeAdd), and
//  return SetEvent(*(slot + 0x84)) (0x18C4540) to wake a sleeping allocator.
//  [NOT byte-identical] placement-dependent (relative calls to the SetEvent
//  IAT thunk).
static int VfsAllocCacheFree(void *slot, unsigned size)
{
    DWORD *cb = (DWORD *)slot;
    cb = (DWORD *)cb[0];                    // the 272-byte control block
    DWORD cur = cb[0x104 / 4] + size;
    if (cur > cb[0x100 / 4])
        __debugbreak();
    cb[0x104 / 4] = (cur == cb[0x100 / 4]) ? 0 : cur;
    InterlockedExchangeAdd((volatile LONG *)&cb[0x10C / 4], (LONG)size);
    return SetEvent(*(HANDLE *)((char *)slot + 0x84));   // 0x18C4540
}

// VfsAllocCacheFinalize  (0x1473E20 - 0x1473E5F)  -- commit / release the
//  staged object (thiscall in ECX, plain retn).  No-op (the EAX is left to
//  the caller in the original; 0 here) when the cache is not initialised
//  (+0x18 == 0) or nothing is staged (+0x0C == 0).  Otherwise r = the staged
//  object's ICommand slot 1 (the size getter); when r <= the element size
//  (+0x14) the slot is returned to the pool (VfsAllocCacheFree), +0x0C is
//  cleared, and the critical section taken by VfsAllocCacheAlloc is released
//  (tail call into sub_1A00A50); when r > the element size +0x0C is simply
//  cleared (the original leaves the critical section locked on that path).
//  96 audio-comms command wrappers use the pair with VfsAllocCacheAlloc.
//  [NOT byte-identical] placement-dependent (relative calls to
//  VfsAllocCacheFree / the critical-section IAT thunks).
static int VfsAllocCacheFinalize(void *cache)
{
    DWORD *o = (DWORD *)cache;
    if (o[0x18 / 4] == 0)
        return 0;
    DWORD staged = o[0x0C / 4];
    if (staged == 0)
        return 0;
    int elem = o[0x14 / 4];
    void **vt = *(void ***)staged;
    int r = ((int (*)(void *))vt[1])((void *)staged);                    // ICommand slot 1
    if (r <= elem) {
        int ret = VfsAllocCacheFree((void *)o[1], (unsigned)elem);
        o[0x0C / 4] = 0;
        LeaveCriticalSection((LPCRITICAL_SECTION)((char *)cache + 0x1C));
        return ret;
    }
    o[0x0C / 4] = 0;                         // the CS stays locked (as in the original)
    return r;
}

// F3AudioNotifInitXAudio2System  (0x1461BF0 - 0x1461C14)  -- the
// InitializeXAudio2System ICommand thunk (CInitializeXAudio2System).
// No-arg, stdcall, retn 0.  Stage a 1-slot allocation in the audio allocation
// cache (unk_1DDC830, g_unk_1DDC830 -- the 28-byte cache object the
// InitVirtualFileSystem path builds) via VfsAllocCacheAlloc (wait = 1) --
// returns 0 when the cache is not initialised (+0x18 == 0, the only path live
// in this build, where g_unk_1DDC830 is zero BSS because InitVirtualFileSystem
// is never reached, the archive slot being null).  On success, install the
// CInitializeXAudio2System vftable into the staged object, then finalize the
// cache (VfsAllocCacheFinalize -- the staged object's ICommand slot 1 reports
// its 4-byte size, which fits the element, so the slot is returned to the
// pool and the critical section released).
// [NOT byte-identical] placement-dependent (relative calls to the
// VfsAllocCacheAlloc / VfsAllocCacheFinalize callees) and refs the high-VA
// global g_unk_1DDC830 (0x01DDC830).
static void *F3AudioNotifXAudio2SystemDtor(void *obj, int free)
{
    *(DWORD *)obj = 0x01B6F458;   // &NLHAudio::NThreading::ICommand::`vftable' (original VA)
    if (free & 1)
        LHFree(obj);
    return obj;
}
static int F3AudioNotifXAudio2SystemSize() { return 4; }     // sub_1462740 / sub_1462120
static void F3AudioNotifXAudio2SystemAtExitInit() { }        // sub_14601F0 -- [STUB] the atexit dtor
static void F3AudioNotifXAudio2SystemAtExitShutdown() { }    // sub_1460200 -- [STUB] the atexit dtor
static const void *kCInitializeXAudio2SystemVftable[] = {
    (const void *)F3AudioNotifXAudio2SystemDtor,   // 0x1463450
    (const void *)F3AudioNotifXAudio2SystemSize,   // 0x1462740
    (const void *)F3AudioNotifXAudio2SystemAtExitInit,   // 0x14601F0
    (const void *)0x534c4146                        // rdata entry (original VA; garbage marker)
};
static const void *kCShutdownXAudio2SystemVftable[] = {
    (const void *)F3AudioNotifXAudio2SystemDtor,   // 0x1463470
    (const void *)F3AudioNotifXAudio2SystemSize,   // 0x1462120
    (const void *)F3AudioNotifXAudio2SystemAtExitShutdown,   // 0x1460200
    (const void *)0x1C40F94                         // rdata entry, original VA
};
static void F3AudioNotifInitXAudio2System()
{
    DWORD *obj = (DWORD *)VfsAllocCacheAlloc(g_unk_1DDC830, 1);
    if (obj != 0) {
        obj[0] = (DWORD)kCInitializeXAudio2SystemVftable;
        VfsAllocCacheFinalize(g_unk_1DDC830);
    }
}
// F3AudioNotifShutdownXAudio2System  (0x14609E0 - 0x1460A04)  -- the
// ShutdownXAudio2System ICommand thunk (CShutdownXAudio2System).  The
// identical shape to F3AudioNotifInitXAudio2System, installing the
// CShutdownXAudio2System vftable.
static void F3AudioNotifShutdownXAudio2System()
{
    DWORD *obj = (DWORD *)VfsAllocCacheAlloc(g_unk_1DDC830, 1);
    if (obj != 0) {
        obj[0] = (DWORD)kCShutdownXAudio2SystemVftable;
        VfsAllocCacheFinalize(g_unk_1DDC830);
    }
}
// F3AudioNotifDefaultFlow  (0x145E040 - 0x145E21B)  -- IMMNotificationClient
// "DefaultFlow" callback (vtable slot 6, 0x01B6F408).  stdcall, 4 stack args
// (self / a / b / c), retn 0x10.  The first parameter is the notification
// object (self); the second and third are flag words (a, b -- the walk only
// happens when both are 0); the fourth is the wide string carried in the
// notification event.  The body:
//   1. sub_1A00310 (the wide-slot ctor, inlined): the c slot is nulled, the
//      wide live-slot counter is bumped (InterlockedIncrement on
//      g_WideSlotCount), and when the wide string is non-empty (non-NULL and
//      not an empty string) a fresh 16-byte wide block is built from it via
//      SharedBlockFromWide (the -1 auto-count case) and stored in the slot.
//   2. When both flags are 0 and the wide string arg is non-NULL: the
//      sub-struct at self+8 is walked -- [self+0x14] (start) is validated
//      against [self+0x18] (end); each 8-byte slot is compared against the
//      fresh id block (by block pointer, or by length + data contents via
//      CompareWideStrings).  On a match, the slot's state word (second DWORD)
//      is tested: 1 -> F3AudioNotifInitXAudio2System (the InitializeXAudio2System
//      thunk), anything else -> F3AudioNotifShutdownXAudio2System (the
//      ShutdownXAudio2System thunk); the self+0x20 word is set to the matched
//      array element pointer; the fresh id block is released and the function
//      returns 0.
//   3. When both flags are 0 and the wide string arg is NULL (the v4 == 0
//      case): F3AudioNotifShutdownXAudio2System is called and the self+0x20
//      word is cleared.
//   4. The fresh id block is released (ReleaseSharedSlotWide) and the
//      function returns 0 (whether the walk matched, found nothing, or was
//      skipped because a flag was set).
// [NOT byte-identical] placement-dependent (relative E8 calls to the wide
// slot ctor / SharedBlockFromWide / the ReleaseSharedSlotWide family, the
// CompareWideStrings comparator, the VfsAllocCacheAlloc / Finalize pair, and
// the InitializeXAudio2System / ShutdownXAudio2System thunks -- the original
// body walks the slot array and dispatches through the pair).
static long __stdcall F3AudioNotifDefaultFlow(void *self, unsigned a, unsigned b, void *c)
{
    WCHAR *wide = (WCHAR *)c;   // the wide string carried in the fourth arg
    c = 0;                      // ctor: slot = 0
    InterlockedIncrement(&g_WideSlotCount);
    if (wide != 0 && wide[0] != 0)
        c = (void *)SharedBlockFromWide(wide, -1);

    if (a == 0 && b == 0)
    {
        if (wide != 0)
        {
            BYTE *start = *(BYTE **)((BYTE *)self + 0x14);
            BYTE *end   = *(BYTE **)((BYTE *)self + 0x18);
            BYTE *raw   = *(BYTE **)((BYTE *)self + 8);   // the notification object pointer
            F3SharedBuffer *fresh = (F3SharedBuffer *)c;

            if ((unsigned int)start > (unsigned int)end)
                F3InvalidParam();
            if ((unsigned int)(*(DWORD *)((BYTE *)self + 0x14)) > (unsigned int)end)
                F3InvalidParam();

            for (;;)
            {
                if (raw == 0)
                    F3InvalidParam();
                if (start == end)
                    break;
                if (raw != 0 && (unsigned int)start >= *(DWORD *)(*(BYTE **)raw + 0x10))
                    F3InvalidParam();
                F3SharedBuffer *slot = *(F3SharedBuffer **)start;
                if (slot == fresh ||
                    (slot != 0 && fresh != 0 &&
                     ((DWORD *)slot)[1] == ((DWORD *)fresh)[1] &&
                     CompareWideStrings((WCHAR *)((F3SharedBuffer *)slot)->data,
                                        (WCHAR *)((F3SharedBuffer *)fresh)->data) == 0))
                {
                    // matched
                    if (raw != 0 && (unsigned int)start >= *(DWORD *)(*(BYTE **)raw + 0x10))
                        F3InvalidParam();
                    if (((DWORD *)start)[1] == 1)
                        F3AudioNotifInitXAudio2System();
                    else
                        F3AudioNotifShutdownXAudio2System();
                    if (raw != 0 && (unsigned int)start >= *(DWORD *)(*(BYTE **)raw + 0x10))
                        F3InvalidParam();
                    *(DWORD *)((BYTE *)self + 0x20) = (DWORD)start;
                    ReleaseSharedSlotWide((F3SharedBuffer **)&c);
                    return 0;
                }
                if (raw != 0 && (unsigned int)start >= *(DWORD *)(*(BYTE **)raw + 0x10))
                    F3InvalidParam();
                start += 8;
            }
        }
        else
        {
            F3AudioNotifShutdownXAudio2System();
            *(DWORD *)((BYTE *)self + 0x20) = 0;
        }
    }

    ReleaseSharedSlotWide((F3SharedBuffer **)&c);
    return 0;
}

// VfsCacheQueryImpl  (0x14972C0 - 0x149731C)  -- one-time (g_dword_1DDCD74)
//  concurrency init: the 8-byte Concurrency reader-writer lock + the 56-byte
//  query state.  [STUB] the lock / state builders (sub_148D220 / sub_1496E90)
//  are unimplemented, treated as their success values.
static int VfsCacheQueryImpl(void)
{
    if (g_dword_1DDCD74 != 0)
        return 0;
    void *lock = LHAlloc(8);              // the Concurrency RWLock (8 bytes)
    (void)lock;                           // sub_148D220 (bind lock) -- stub, success
    void *q = LHAlloc(56);                // the 56-byte query state
    g_dword_1DDCD74 = (q != 0) ? (DWORD)q : 0;   // sub_1496E90 (identity) -- stub
    return 1;
}

// VfsCacheQueryInit  (0x146D8E0 - 0x146D8E7)  -- the retn-4 wrapper around
//  VfsCacheQueryImpl (the (ECX) argument is unused).
static int VfsCacheQueryInit(int unused) { (void)unused; return VfsCacheQueryImpl(); }

// AudioNodeListInit  (sub_147D080 -> 0x147D080 - 0x147D0F2)  -- thiscall.  The
//  32-byte provider/audio node-list block: +0x00 = a 4-byte wrapper holding
//  the node pointer (0 on OOM); +0x18 = a 20-byte self-linked block whose
//  [0]/[1]/[2] DWORDs all point back into itself, with bytes +0x10 and +0x11
//  set to 1 (0 on OOM); +0x1C = 0.  Returns the node.  [NOT byte-identical]
//  placement-dependent (the LHAlloc E8 call).
static void *AudioNodeListInit(void *node)
{
    DWORD *n = (DWORD *)node;

    DWORD *wrapper = (DWORD *)LHAlloc(4);
    if (wrapper != 0)
        *wrapper = (DWORD)node;
    else
        wrapper = 0;
    n[0] = (DWORD)wrapper;

    DWORD *blk = (DWORD *)LHAlloc(20);
    if (blk != 0)
        blk[0] = 0;
    if ((DWORD)blk != (DWORD)-4)
        blk[1] = 0;
    if ((DWORD)blk != (DWORD)-8)
        blk[2] = 0;
    if (blk != 0)
    {
        ((BYTE *)blk)[0x10] = 1;
        n[6] = (DWORD)blk;
        ((BYTE *)blk)[0x11] = 1;
        blk[1] = (DWORD)blk;
        blk[0] = (DWORD)blk;
        blk[2] = (DWORD)blk;
    }
    n[7] = 0;
    return node;
}

// VfsProviderAlloc  (0x1477DA0 - 0x1477E55)  -- thiscall.  Fill the caller-
//  provided 32-byte provider-query block (the this in ECX): +0x00 = count,
//  +0x04 = totalBytes, +0x08 = totalBytes / count, +0x0C = the data buffer
//  (LHAlloc(totalBytes + (a6 ? (totalBytes / count) * a5 : 0))), +0x10 = 0,
//  +0x14 = totalBytes / count, +0x18 (byte) = a6, +0x19 (byte) = a4,
//  +0x1C = a5; then the buffer is filled (bound = totalBytes / count; for
//  v9 = 0 .. bound-2: *(buffer + v9 * count + (a6 ? v9 * a5 : 0)) = v9 + 1)
//  and -1 is written at buffer + (bound-1) * count + (a6 ? (bound-1) * a5 : 0).
//  On allocation failure the original int 3 (debug break); this build returns
//  the block with the data pointer zeroed (the original's OOM crash is not
//  reproducible through the C++ heap path).  Returns the block.  [NOT
//  byte-identical] placement-dependent (the LHAlloc call).
static void *VfsProviderAlloc(void *obj, unsigned int totalBytes, unsigned int count, int a4, unsigned int a5, int a6)
{
    DWORD *blk = (DWORD *)obj;
    DWORD bound = (count != 0) ? totalBytes / count : 0;

    blk[0] = count;
    blk[1] = totalBytes;
    blk[2] = bound;
    blk[4] = 0;
    ((BYTE *)blk)[0x18] = (BYTE)a6;
    ((BYTE *)blk)[0x19] = (BYTE)a4;
    blk[7] = a5;
    blk[5] = bound;

    DWORD v7 = (a6 != 0) ? bound * a5 : 0;
    unsigned int bytes = totalBytes + v7;
    void *buf = (bytes != 0) ? LHAlloc(bytes) : 0;
    blk[3] = (DWORD)buf;

    if (buf != 0 && bound >= 1)
    {
        if (bound >= 2)
        {
            DWORD v9 = 0;
            do
            {
                DWORD v10 = (a6 != 0) ? v9 * (DWORD)a5 : 0;
                *(DWORD *)((char *)buf + (DWORD)v9 * count + v10) = v9 + 1;
                v9++;
            }
            while (v9 + 1 < bound);
        }
        DWORD v12 = bound - 1;
        DWORD v13 = (a6 != 0) ? v12 * (DWORD)a5 : 0;
        *(DWORD *)((char *)buf + (DWORD)v12 * count + v13) = (DWORD)-1;
    }
    return obj;
}

// VfsProviderQueryCtor  (0x14778E0 - 0x147794A)  -- the 72-byte provider-
//  capability query cache (thiscall, retn 8).  No-op (0) when the +0x44 flag is
//  set; otherwise records the descriptor dimension at +0x04/+0x08, allocates the
//  32-byte provider buffer (VfsProviderAlloc over the descriptor's +0x3C byte),
//  and sets the +0x44 flag + returns 1.
static int VfsProviderQueryCtor(void *obj, int a2, int a3)
{
    DWORD *o = (DWORD *)obj;
    if (o[0x44 / 4] != 0)
        return 0;
    o[1] = a3;
    o[2] = a3;
    DWORD *v5 = GetVfsDesc();
    void *block = LHAlloc(32);
    void *p = block ? (void *)VfsProviderAlloc(block, (unsigned int)(a3 * a2), (unsigned int)a2, 1, (unsigned int)((BYTE *)v5)[60], 0) : 0;
    o[0] = (DWORD)p;
    if (p == 0)
        return 0;
    o[0x44 / 4] = 1;
    return 1;
}

// VfsQueryProviderPair  (0x1477990 - 0x14779C8)  -- run the two provider
//  capability queries over the descriptor's (+0x20,+0x28) and (+0x24,+0x2C)
//  dimension pairs (into g_unk_1DDCC40 / g_unk_1DDCC88); returns the AND of the
//  two results.
static int VfsQueryProviderPair(void)
{
    DWORD *v0 = GetVfsDesc();
    int r1 = VfsProviderQueryCtor(g_unk_1DDCC40, v0[8], v0[10]);
    if (r1 != 0)
        return VfsProviderQueryCtor(g_unk_1DDCC88, v0[9], v0[11]) != 0;
    return r1;
}

// VfsWorkerAddCallback  (0x1477840)  -- [STUB] append a callback to a worker's
//  list (the 12-byte node + the 0x1477720 spin); no-op here.
static void VfsWorkerAddCallback(int worker, int data, void *fn) { (void)worker; (void)data; (void)fn; }

// VfsWorkerCallbackRegister  (0x1476D00 - 0x1476D5D)  -- register an 8-byte
//  {vtable, function} callback pair with worker slot `index` (0..6) under that
//  worker's critical section (the CsGuardAcquire/CsGuardRelease pair (sub_1A22480/sub_1A22460) over stru_1DDCA90 +
//  index*24) and its per-worker list (g_dword_1DDC9C8 / g_dword_1DDC9DC).
//  No-op for index >= 7; the add is the VfsWorkerAddCallback stub above (the CS
//  enter/leave wraps it but is omitted -- the workers are not spawned in this
//  build, so their CS blocks are uninitialised BSS).
static void VfsWorkerCallbackRegister(void *pair, int index)
{
    if (index < 0 || index >= 7)
        return;
    VfsWorkerAddCallback(g_dword_1DDC9C8[index * 7], g_dword_1DDC9DC[index * 7], pair);
}

// ============================================================
//  CEngineFader (the 976-byte NLHAudio::NFramework::CEngineFader) and the
//  small container helpers its constructor drives.
// ============================================================
//  CCharStringCtor  (0x1488CC0 / 0x14761A0 / 0x1489000)  -- thiscall + retn 8.
//  Builds a 32-byte LHString::CCharString at obj: a 4-byte heap owner node
//  (holding a back-pointer to obj) into obj+0x00, and a `capacity`-byte heap
//  buffer whose three leading words are self-linked (an empty circular list)
//  with a 0x01/0x01 flag pair at its tail (byte capacity-4 / capacity-3),
//  stored into obj+0x18 (count obj+0x1C = 0).  `capacity` is 0x18 (24) for the
//  0x1488CC0 variant, 0x1C (28) for 0x14761A0, 0x2C (44) for 0x1489000.
//  [NOT byte-identical] two relative LHAlloc calls; the OOM branch is
//  replicated (the unguarded tail writes deref the buffer, as the original).
static void *CCharStringCtor(void *obj, unsigned capacity)
{
    DWORD *o = (DWORD *)obj;
    void *node = LHAlloc(4);
    if (node) *(DWORD *)node = (DWORD)obj;
    o[0] = (DWORD)node;
    DWORD *buf = (DWORD *)LHAlloc(capacity);
    if (buf) { buf[0] = 0; buf[1] = 0; buf[2] = 0; }
    BYTE *b = (BYTE *)buf;
    b[capacity - 3] = 0;
    b[capacity - 4] = 1;
    o[6] = (DWORD)buf;               // obj + 0x18
    b[capacity - 3] = 1;
    buf[1] = (DWORD)buf;
    buf[0] = (DWORD)buf;
    buf[2] = (DWORD)buf;
    o[7] = 0;                         // obj + 0x1C
    return obj;
}

//  CCharStringTripleCtor  (0x1474D30)  -- thiscall.  The +0x60 member block of
//  CEngineFader: three 28-byte CCharStrings at base+0x00/+0x20/+0x40, then a
//  zero dword at base+0x60 and a zero byte at base+0x64.
static void *CCharStringTripleCtor(void *base)
{
    char *b = (char *)base;
    CCharStringCtor(b + 0x00, 0x1C);   // 0x14761A0
    CCharStringCtor(b + 0x20, 0x1C);
    CCharStringCtor(b + 0x40, 0x1C);
    *(DWORD *)(b + 0x60) = 0;
    *(BYTE  *)(b + 0x64) = 0;
    return base;
}

//  CMapBaseCtor  (0x150C5A0)  -- thiscall.  The NSupport::Containers::Intrusive
//  CMap base: two 20-byte heap nodes (5 words each) built into a circular
//  2-node list.  node1 = {node1,node1,node1,0,0}; node2 = {node1,node1,node1,0,0};
//  this[0] = node2, this[1] = node1.  [NOT byte-identical] two LHAlloc calls.
static void *CMapBaseCtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    DWORD *n1 = (DWORD *)LHAlloc(20);
    if (n1) { n1[0] = 0; n1[1] = 0; n1[2] = 0; n1[4] = 0; }
    o[1] = (DWORD)n1;
    n1[2] = (DWORD)n1;
    n1[1] = n1[2];
    n1[0] = n1[1];
    n1[3] = 0;
    n1[4] = 0;
    DWORD *n2 = (DWORD *)LHAlloc(20);
    if (n2) { n2[0] = 0; n2[1] = 0; n2[2] = 0; n2[4] = 0; }
    o[0] = (DWORD)n2;
    n2[1] = (DWORD)n1;
    n2[0] = n2[1];
    n2[2] = n2[0];
    n2[3] = 0;
    n2[4] = 0;
    return obj;
}

//  AudioFloatBlockInit  (0x150E180)  -- thiscall.  Fills the 476-byte (0x1DC,
//  words 0..118) float-parameter block at obj+0x118: words 0..4 = 0, then a run
//  of float pairs alternating {1.0,1.0}/{0,0} from word 5 through word 118
//  (word 1AE4618 = 0x3F800000 = 1.0f is the constant).
static void *AudioFloatBlockInit(void *obj)
{
    DWORD *o = (DWORD *)obj;
    const DWORD one = 0x3F800000;    // 1.0f
    o[0] = o[1] = o[2] = o[3] = o[4] = 0;
    o[5] = one;  o[6] = one;
    o[7] = 0;    o[8] = 0;
    o[9] = one;  o[10] = one;
    o[11] = 0;   o[12] = 0;
    o[13] = one; o[14] = one;
    o[15] = 0;   o[16] = 0;
    o[17] = one; o[18] = one;
    // the original's second loop (its first zero loop over words 19..68 is
    // subsumed by this one, which covers the full 19..118 range):
    for (int j = 0; j < 25; ++j) {
        int base = 22 + j * 4;
        o[base - 1] = one;
        o[base]     = one;
        o[base - 3] = 0;
        o[base - 2] = 0;
    }
    return obj;
}

//  EventObjCtor  (0x18C46E0)  -- thiscall + retn 8.  The 0x84-byte Win32 event
//  wrapper: obj+0x00 (byte) = 0, obj+0x80 = CreateEventA(NULL, manualReset==1,
//  initialState!=0, NULL).  [NOT byte-identical] the CreateEventA IAT slot is
//  called directly rather than at its original high VA.
static void *EventObjCtor(void *obj, int manualReset, int initialState)
{
    BYTE *o = (BYTE *)obj;
    o[0] = 0;
    *(HANDLE *)(o + 0x80) = CreateEventA(NULL, (manualReset == 1) ? 1 : 0,
                                         (initialState != 0) ? 1 : 0, NULL);
    return obj;
}

//  ResetEventObj  (0x18C4550)  -- thiscall.  ResetEvent on the handle at
//  obj+0x80.  [NOT byte-identical] the ResetEvent IAT slot is called directly.
static BOOL ResetEventObj(void *obj)
{
    return ResetEvent(*(HANDLE *)((char *)obj + 0x80));
}

//  SIMDVectorInit3  (0x147F6B0)  -- stdcall (retn 0x0C).  Writes three 16-byte
//  (float4) vectors: a1 = {0,0,0,0}, a2 = {0,1.0,0,0}, a3 = {0,0,1.0,0}.
static void SIMDVectorInit3(void *a1, void *a2, void *a3)
{
    float *v1 = (float *)a1;
    float *v2 = (float *)a2;
    float *v3 = (float *)a3;
    v1[0] = v1[1] = v1[2] = v1[3] = 0.0f;
    v2[0] = 0.0f; v2[1] = 1.0f; v2[2] = 0.0f; v2[3] = 0.0f;
    v3[0] = 0.0f; v3[1] = 0.0f; v3[2] = 1.0f; v3[3] = 0.0f;
}

//  AudioEngineFaderCtor  (0x14843C0 - 0x14844FD)  -- thiscall.  Constructs the
//  976-byte (0x3D0) CEngineFader: six 32-byte CCharStrings (three 24-byte, then
//  the +0x60 triple of 28-byte strings + 0x1C=0 at +0xC0/+0xC4), the
//  CMap<CEngineFader, CCharString, GetName, 8, CCompareNoCase> at +0xCC (CBase
//  vtable, CMap data, CMap vtable, bucket[8]=0), a 44-byte CCharString at +0xF8,
//  the 476-byte float block at +0x118, the CMap<CDataObjectHolder, GetThis, 0>
//  at +0x2F4, the 1.0f scalar at +0x330, the -1/0 pair at +0x334/+0x338, the
//  0x84-byte event wrapper at +0x33C, the two zero bytes at +0x3C0/+0x3C1, the
//  three float4 vectors at +0x300/+0x310/+0x320, and finally a ResetEvent of the
//  wrapper.  Returns obj.  [NOT byte-identical] nine relative E8 calls plus the
//  high-VA CBase/CMap vftables (0x1B70xxx) and the 1.0f global (dword_1AE4618).
static void *AudioEngineFaderCtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    char *b = (char *)obj;
    CCharStringCtor(b + 0x00, 0x18);              // 0x1488CC0
    CCharStringCtor(b + 0x20, 0x18);
    CCharStringCtor(b + 0x40, 0x18);
    CCharStringTripleCtor(b + 0x60);              // 0x1474D30
    o[0xCC / 4] = 0x1B70958;                         // CBase<CEngineFader> vftable
    CMapBaseCtor(b + 0xD0);                        // 0x150C5A0
    o[0xCC / 4] = 0x1B70AE4;                         // CMap<CEngineFader> vftable
    for (int i = 0xD8 / 4; i <= 0xF4 / 4; ++i) o[i] = 0;   // bucket[8] = 0
    CCharStringCtor(b + 0xF8, 0x2C);              // 0x1489000
    AudioFloatBlockInit(b + 0x118);               // 0x150E180
    o[0x2F4 / 4] = 0x1B70984;                        // CBase<CDataObjectHolder> vftable
    CMapBaseCtor(b + 0x2F8);                       // 0x150C5A0
    *(float *)(b + 0x330) = 1.0f;                  // movss dword_1AE4618
    o[0x2F4 / 4] = 0x1B70BCC;                        // CMap<CDataObjectHolder> vftable
    o[0x334 / 4] = 0xFFFFFFFF;                       // +0x334 = -1
    o[0x338 / 4] = 0;                                 // +0x338 = 0
    EventObjCtor(b + 0x33C, 1, 0);                 // 0x18C46E0
    *(BYTE *)(b + 0x3C0) = 0;
    *(BYTE *)(b + 0x3C1) = 0;
    SIMDVectorInit3(b + 0x300, b + 0x310, b + 0x320);  // 0x147F6B0
    ResetEventObj(b + 0x33C);                      // 0x18C4550
    return obj;
}

//  sub_14833E0  (0x14833E0 - 0x1483612)  -- the CProcessDelegateThiscall engine
//  destructor (thiscall): the 562-byte teardown.  Reads the +0x3C1 flag byte --
//  if non-zero, returns it.  Otherwise runs the full teardown: the
//  sub_146DA70(this+768/784/800, +0xCC) / sub_147F9C0 / sub_14806C0(this+756)
//  calls, the +0xE0 block list free (sub_1480CD0, via the +0xD8 node chain),
//  the +0xD8 node-list teardown (vtable slot 4 dispatch + vtable slot 2
//  AddRef/Release + LHFree-style unlinking), the +0x338 object release
//  (sub_1474830), the +0x18/+0x00-tag walk (sub_1482170 / sub_1480290 /
//  sub_14860A0, __invalid_parameter_noinfo checks), the SetFrontendEvent(+0x33C)
//  when +0x3C0 is set and +0xD8/+0x1C are empty, then the VfsDescDefaultCtor
//  scratch block (+0x480 bytes on the stack) + sub_145DC90's release of the
//  +0x338 slot.  Every callee above is an unimplemented deep stub in this
//  build -- [remaining stub].  Returns 0.
static int AudioEngineFaderDtor(void *obj) { (void)obj; return 0; }

//  ProcessDelegateDtor  (0x146DFA0 - 0x146DFA7)  -- the CProcessDelegateThiscall
//  deleter: load the +0x0C engine pointer and tail-jump into the engine
//  destructor.  [NOT byte-identical] E9 jmp (placement-dependent).
static int ProcessDelegateDtor(void *obj)
{
    DWORD child = *(DWORD *)((char *)obj + 0x0C);
    return AudioEngineFaderDtor((void *)child);
}

// CProcessDelegateThiscallCtor  (0x146E220 - 0x146E275)  -- the 16-byte
//  CProcessDelegateThiscall<CEngine> constructor (thiscall).  Installs its
//  vtable, zeroes +0x04/+0x08/+0x0C, allocates the 976-byte engine
//  (AudioEngineFaderCtor) into +0x0C (0 on OOM), sets +0x04 = this and
//  +0x08 = ProcessDelegateDtor, registers this with worker slot 0, returns this.
static void *CProcessDelegateThiscallCtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    o[0] = kCProcessDelegateThiscallVftable;
    o[1] = 0;
    o[2] = 0;
    o[3] = 0;
    void *mem = LHAlloc(976);
    DWORD sub = (mem != 0) ? (DWORD)AudioEngineFaderCtor(mem) : 0;
    o[3] = sub;
    o[1] = (DWORD)obj;
    o[2] = (DWORD)ProcessDelegateDtor;
    VfsWorkerCallbackRegister(obj, 0);   // 0x1476D00
    return obj;
}

// AudioEngineCtor  (0x1479440 - 0x14795A0)  -- thiscall.  The 340-byte
//  (0x154) audio-engine build: nine 32-byte node-list blocks (at +0x00, the
//  six loop blocks at +0x20..+0xC0, and +0xE0/+0x100), then the provider
//  query buffer (the 32-byte block allocated at +0x120, filled by
//  VfsProviderAlloc) and the a2/a3/a4 fields (+0x12C/+0x130/+0x140 etc.).
//  Returns obj.  [NOT byte-identical] placement-dependent (the LHAlloc /
//  VfsProviderAlloc / GetVfsDesc calls, the sub_147D080 helper's LHAlloc).
static void *AudioEngineCtor(void *obj, int a2, int a3, int a4)
{
    DWORD *o = (DWORD *)obj;

    AudioNodeListInit(obj);                    // +0x00
    char *node = (char *)obj + 0x20;
    for (int i = 0; i < 6; ++i)
    {
        AudioNodeListInit(node);               // +0x20..+0xC0 (six blocks)
        node += 0x20;
    }
    AudioNodeListInit((char *)obj + 0xE0);     // +0xE0
    AudioNodeListInit((char *)obj + 0x100);    // +0x100

    o[0x12C / 4] = (DWORD)a3;                   // +0x12C = a3
    o[0x134 / 4] = (DWORD)a3;                   // +0x134 = a3
    o[0x120 / 4] = 0;                           // +0x120 = 0 (then the provider block)
    o[0x128 / 4] = 0;                           // +0x128 = 0
    o[0x130 / 4] = (DWORD)a4;                   // +0x130 = a4
    o[0x138 / 4] = (DWORD)a4;                   // +0x138 = a4
    o[0x13C / 4] = 0;                           // +0x13C = 0
    o[0x140 / 4] = (DWORD)a2;                   // +0x140 = a2

    DWORD *v0 = GetVfsDesc();
    DWORD v12 = v0[4] * v0[3];                  // VfsDesc[3] * VfsDesc[4]
    o[0x124 / 4] = v12;                         // +0x124 = v12
    DWORD v13 = v12 * (v0[6] + v0[7]);          // v12 * (VfsDesc[6] + VfsDesc[7])

    void *block = LHAlloc(32);
    if (block != 0)
        block = VfsProviderAlloc(block, (unsigned int)v13, (unsigned int)v12, 1, 0x10000, 0);
    else
        block = 0;
    o[0x120 / 4] = (DWORD)block;                // +0x120 = the provider block
    o[0x148 / 4] = 0;                           // +0x148 = 0
    o[0x14C / 4] = 0;                           // +0x14C = 0
    o[0x150 / 4] = 0;                           // +0x150 = 0
    *(WORD *)((char *)obj + 0x144) = 0;         // +0x144 (WORD) = 0
    return obj;
}

//  GetRwLockCriticalSection  (0x148D350 - 0x148D35C)  -- extract the critical
//  section of the concurrency RW-lock object: load dword_1DDCCD8, call vtable[1]
//  (thiscall, offset 0x4 -- the +0x4 / vtable[1] thiscall slot), and return its
//  result (the CRITICAL_SECTION pointer as an int).
//  [NOT byte-identical] refs the high-VA global dword_1DDCCD8.
static int GetRwLockCriticalSection(void)
{
    DWORD obj = g_dword_1DDCCD8;   // 0x1DDCCD8 -- dword_1DDCCD8
    return ((int (*)(DWORD))(*(DWORD *)obj + 4))(obj);
}

//  SyncLockedStateChains  (0x1496FA0 - 0x1497122)  -- the RWLock-guarded tick
//  state-update (thiscall, obj in ECX).  Acquires the RW-lock critical section
//  (GetRwLockCriticalSection above), commits *(obj + 8) = the value at *(obj +
//  0x28), then syncs two element chains:
//    chain 1: obj[4] (the obj+0x10 pointer) followed while the node's +0x1C is
//      the marker (obj+0x10); each node (v5) copies the float at v7 = *(v5 +
//      0x60) into v5+0x30, overwrites the int at v5+0x38 with *(v7 + 4) when the
//      committed value is >= v5+0x3C, copies the float at v7+8 into v5+0x40, and
//      overwrites the int at v5+0x48 with *(v7 + 12) when the committed value
//      is >= v5+0x4C, then advances through +0x14 while the node's +0x1C is the
//      marker (a node with a non-marker +0x1C ends the walk; a null +0x14 also
//      ends it).
//    chain 2: obj[1] (the obj+0x04 pointer) followed while the node's +0x10 is
//      the marker (obj+0x04); each node (v13) copies the float at v15 = *(v13
//      + 0x48) into v13+0x20, overwrites the int at v13+0x28 with *(v15 + 4)
//      when the committed value is >= v13+0x2C, walks the nested inner chain
//      (the node at v13+0x30, marker v13+0x30, each step copying the float at
//      v19 = *(v17+0x38) into v17+0x20 and the int at v17+0x28 with *(v19+4)
//      when the committed value is >= v17+0x2C), then copies the six float
//      slots (v15+0x0C/+0x08/+0x10 -> v13+0x88/+0x84/+0x8C), the int slots
//      (v15+0x18/+0x20/+0x24/+0x28/+0x2C/+0x30 -> v13+0x94/+0xA0/+0xA4/+0xA8/
//      +0xAC/+0xB8) and the byte flags (v15+0x1C/+0x1D/+0x1E/+0x1F ->
//      v13+0xF2/+0xF3/+0xF5/+0xF7), then advances through +0x08 while the
//      node's +0x10 is the marker (a node with a non-marker +0x10 ends the
//      walk; a null +0x08 also ends it).
//  [NOT byte-identical] relative E8 calls to GetRwLockCriticalSection /
//  CsGuardAcquire / CsGuardRelease plus the x87 float semantics (fld/fstp).
static void SyncLockedStateChains(void *obj)
{
    F3CsGuard guard;
    DWORD cs = GetRwLockCriticalSection();
    CsGuardAcquire(&guard, (CRITICAL_SECTION *)cs, 0);
    DWORD *o = (DWORD *)obj;
    o[2] = *(DWORD *)o[10];      // *(this + 8) = the committed value at *(this + 0x28)
    DWORD marker1 = (DWORD)(o + 4);   // this + 0x10
    DWORD *v5 = (DWORD *)o[4];        // the obj+0x10 pointer
    while (v5 != 0)
    {
        DWORD v7 = *(DWORD *)((char *)v5 + 0x60);
        if (v7 != 0)
        {
            *(float *)((char *)v5 + 0x30) = *(float *)v7;
            if (o[2] >= *(DWORD *)((char *)v5 + 0x3C))
                *(DWORD *)((char *)v5 + 0x38) = *(DWORD *)(v7 + 4);
            *(float *)((char *)v5 + 0x40) = *(float *)(v7 + 8);
            if (o[2] >= *(DWORD *)((char *)v5 + 0x4C))
                *(DWORD *)((char *)v5 + 0x48) = *(DWORD *)(v7 + 12);
        }
        if (*(DWORD *)((char *)v5 + 0x1C) == marker1)
        {
            v5 = (DWORD *)*(DWORD *)((char *)v5 + 0x14);
            if (v5 == 0)
                break;
            if (*(DWORD *)((char *)v5 + 0x1C) == marker1)
                continue;
        }
        v5 = 0;
    }
    DWORD marker2 = (DWORD)(o + 1);   // this + 0x04
    DWORD *v13 = (DWORD *)o[1];       // the obj+0x04 pointer
    while (v13 != 0)
    {
        DWORD v15 = *(DWORD *)((char *)v13 + 0x48);
        *(float *)((char *)v13 + 0x20) = *(float *)v15;
        if (o[2] >= *(DWORD *)((char *)v13 + 0x2C))
            *(DWORD *)((char *)v13 + 0x28) = *(DWORD *)(v15 + 4);
        DWORD v17 = *(DWORD *)((char *)v13 + 0x30);
        if (v17 != 0)
        {
            DWORD marker3 = (DWORD)((char *)v13 + 0x30);
            do
            {
                DWORD v19 = *(DWORD *)((char *)v17 + 0x38);
                *(float *)((char *)v17 + 0x20) = *(float *)v19;
                if (o[2] >= *(DWORD *)((char *)v17 + 0x2C))
                    *(DWORD *)((char *)v17 + 0x28) = *(DWORD *)(v19 + 4);
                if (*(DWORD *)((char *)v17 + 0x1C) != marker3)
                    break;
                v17 = *(DWORD *)((char *)v17 + 0x14);
                if (v17 == 0)
                    break;
            } while (*(DWORD *)((char *)v17 + 0x1C) == marker3);
        }
        *(float *)((char *)v13 + 0x84) = *(float *)(v15 + 8);
        *(float *)((char *)v13 + 0x88) = *(float *)(v15 + 12);
        *(float *)((char *)v13 + 0x8C) = *(float *)(v15 + 16);
        *(DWORD *)((char *)v13 + 0x94) = *(DWORD *)(v15 + 24);
        *(BYTE *)((char *)v13 + 0xF2) = *(BYTE *)(v15 + 28);
        *(BYTE *)((char *)v13 + 0xF3) = *(BYTE *)(v15 + 29);
        *(DWORD *)((char *)v13 + 0xA0) = *(DWORD *)(v15 + 32);
        *(DWORD *)((char *)v13 + 0xA4) = *(DWORD *)(v15 + 36);
        *(DWORD *)((char *)v13 + 0xA8) = *(DWORD *)(v15 + 40);
        *(DWORD *)((char *)v13 + 0xAC) = *(DWORD *)(v15 + 44);
        *(DWORD *)((char *)v13 + 0xB8) = *(DWORD *)(v15 + 48);
        *(BYTE *)((char *)v13 + 0xF5) = *(BYTE *)(v15 + 30);
        *(BYTE *)((char *)v13 + 0xF7) = *(BYTE *)(v15 + 31);
        if (*(DWORD *)((char *)v13 + 0x10) == marker2)
        {
            v13 = (DWORD *)*(DWORD *)((char *)v13 + 0x08);
            if (v13 == 0)
                break;
            if (*(DWORD *)((char *)v13 + 0x10) == marker2)
                continue;
        }
        v13 = 0;
    }
    CsGuardRelease(&guard);
}

// AudioTickEnterStep  (0x146D930 - 0x146D93C)  -- the audio tick enter step:
//  loads the concurrency reader-writer-lock object (g_dword_1DDCD74, via the
//  sub_1495860 getter 0x1495860) and tail-calls SyncLockedStateChains(this=lock)
//  (0x1496FA0).
//  [NOT byte-identical] -- a relative E8 call + an E9 tail-jmp to
//  SyncLockedStateChains (placement-dependent) plus the high-VA global.
static int AudioTickEnterStep(void)
{
    DWORD lock = g_dword_1DDCD74;   // sub_1495860 (0x1495860): the RWLock getter
    SyncLockedStateChains((void *)lock);   // 0x1496FA0 (thiscall) -- tail-call
    return 0;
}

//  AudioDelegateTickStep  (0x1478F20 - 0x1478FE1)  -- the audio delegate tick
//  step (thiscall, obj in ECX; the audio-engine object).  [STUB] -- the deep
//  vtable-dispatch + __invalid_parameter_noinfo chain through two unimplemented
//  callees (AudioNodeConsumedWalk 0x147AF40 and sub_147CC80 0x147CC80); the tick is never
//  dispatched in this build -- the worker pool is never spawned, so no tick
//  caller is reached.  What it does, for reference:
//
//  Walks the element pointer-chain at the obj+0x100 header:
//    v1  = **(_DWORD **)(obj + 0x118)   // the current element
//    v2  = (_DWORD *)(obj + 0x100)      // the header
//    v3  = *(_DWORD **)(obj + 0x100)    // the iterator pointer
//    v10 = v1; v9 = v3;
//  in a while (1) loop:
//    v4 = v2[6];                         // = *(DWORD*)(obj + 0x118) -- sentinel
//    if (v3 == nullptr || v3 != (_DWORD *)*v2) __invalid_parameter_noinfo();
//    if (v1 == v4) break;
//    v5 = (v3 != nullptr) ? *v3 : 0;     // (+ __invalid_parameter_noinfo in the null case)
//    if (v1 == *(_DWORD *)(v5 + 24)) __invalid_parameter_noinfo();
//    v6 = *(_DWORD *)(v1 + 12);
//    if (*(_BYTE *)(v6 + 206) != 0 || (vtable dispatch via v6+0x10; v14 != v13)) {
//        AudioNodeConsumedWalk(&v9);   // updates the saved v10 in place (v10 = consumed element)
//        v1 = v10;
//        v3 = v9;             // v9 itself is unchanged by the callee
//    } else {
//        v7 = sub_147CC80(&v11, v3, v1);  // returns a 2-DWORD buffer
//        v3 = *(_DWORD **)v7;
//        v1 = *(_DWORD *)(v7 + 4);
//        v8 = vtable[18] via *(_DWORD *)v6;  // thiscall(v6, 1)
//        v9 = v3; v10 = v1;
//        v8(v6, 1);
//    }
//  [NOT byte-identical] placement-dependent (the relative E8 calls to
//  __invalid_parameter_noinfo / AudioNodeConsumedWalk / sub_147CC80 + the indirect
//  vtable dispatch through the v6+0x10 pointer and the v6 vtable); the deep
//  callee chain is the documented no-op stub.
static void AudioDelegateTickStep(void *obj) { (void)obj; }

// sub_5BC1C0  (0x5BC1C0 - 0x5BC1D4)  -- the audio queue consumer (thiscall, obj
//  in ECX, this = the queue control block; tail does _InterlockedCompareExchange64
//  + _InterlockedDecrement + worker-callback (dword_2075B24 + 16) + the
//  sub_18A6010/sub_18A5F20 chain) [STUB, NOT IMPLEMENTED].  Returns the popped
//  item (non-zero) or 0 when the queue is drained.  Remaining stub: sub_18A6010 /
//  sub_18A5F20 / dword_2075B24 are unimplemented.
static int sub_5BC1C0(void *obj) { (void)obj; return 0; }

// AudioQueuePoll  (0x150D380 - 0x150D399)  -- the audio queue poll: the sub_5BC1C0
//  pair at the unk_1DDCF88 / unk_1DDCF00 slots; returns the first queue's popped
//  item, or the second queue's when the first drains (0 = no pending item).
//  [NOT byte-identical] placement-dependent (the relative E8 calls to
//  sub_5BC1C0 + the high-VA globals g_unk_1DDCF88 / g_unk_1DDCF00); the deep
//  callee sub_5BC1C0 is a documented remaining stub.
static int AudioQueuePoll(void)
{
    int result = sub_5BC1C0((void *)&g_unk_1DDCF88);   // 0x150D380: call sub_5BC1C0(&unk_1DDCF88)
    if (result != 0)
        return result;                                // 0x150D398: retn
    return sub_5BC1C0((void *)&g_unk_1DDCF00);         // 0x150D395: tail-call sub_5BC1C0(&unk_1DDCF00)
}

// AudioQueueBlockRelease  (0x147AA20 - 0x147AA86)  -- the audio queue block
//  release helper (thiscall, slot in ECX = the +0x20 block slot, block on the
//  stack): when the block's +8 back-pointer equals the slot, hand off / repoint
//  the slot and null the block's three header DWORDs (+0/+4/+8).  The block's
//  two optional out-pointers (+0 = a source object, +4 = a release target)
//  follow a four-case contract:
//    +0 == 0, +4 == 0  -- [slot] = 0, [slot + 4] = 0
//    +0 != 0, +4 == 0  -- [slot] = +0 (repoint), [+0 + 4] = 0
//    +0 == 0, +4 != 0  -- [slot + 4] = +4, [+4] = 0
//    +0 != 0, +4 != 0  -- [+4] = +0 and [+0 + 4] = +4 (the two-way handoff)
//  [NOT byte-identical] placement-dependent (the relative call site from
//  AudioQueueItemProcess + the plain vtable-less body); declared as a plain
//  free function (thiscall hybrid in the original).
static void AudioQueueBlockRelease(char *slot, DWORD *v4)
{
    if (v4[2] != (DWORD)(DWORD)slot)
        return;                    // [v4 + 8] != slot: nothing to release
    DWORD v2 = v4[1];              // +0x04: the release-target object, if any
    DWORD v3 = v4[0];              // +0x00: the source object, if any
    if (v2 != 0)
    {
        if (v3 != 0)
        {
            *(DWORD *)v2 = v3;         // [v2] = v3
            *(DWORD *)(v3 + 4) = v2;    // [v3 + 4] = v2
        }
        else
        {
            *(DWORD *)(slot + 4) = v2;  // [slot + 4] = v2
            *(DWORD *)v2 = 0;
        }
        v4[1] = 0;
        v4[0] = 0;
        v4[2] = 0;
    }
    else
    {
        if (v3 != 0)
        {
            *(DWORD *)slot = v3;        // [slot] = v3
            *(DWORD *)(v3 + 4) = 0;
        }
        else
        {
            *(DWORD *)slot = 0;         // [slot] = 0
            *(DWORD *)(slot + 4) = 0;   // [slot + 4] = 0
        }
        v4[1] = 0;
        v4[0] = 0;
        v4[2] = 0;
    }
}

// AudioQueueItemEmitFn  -- the object's vtable slot 11 (offset 0x2C): thiscall
//  (this = +0x10 object, five stack words).  Used by AudioQueueItemProcess.
typedef void (__thiscall *AudioQueueItemEmitFn)(DWORD self, DWORD a2, int a3,
                                                DWORD a4, DWORD a5, char a6);

// AudioQueueItemProcess  (0x1478820 - 0x1478977)  -- the audio queue item
//  processor (thiscall, obj in ECX = the audio queue item object, a2 on the
//  stack = the queue item; retn 4).  Zeroes the +0xCE flag, then, when the item
//  byte at +0x2C is clear: accumulates the item's +0x1C/+0x20 counters into the
//  heap block held at +0x20 (+0x14 sum / +0x18 difference), carries +0x1C into
//  +0x88, subtracts (+0x1C, +0x20) from the 64-bit counter at +0x70 (signed),
//  releases the block (AudioQueueBlockRelease + vtable[1] + LHFree, incrementing
//  +0x80), then, when the outer condition holds, dispatches the object's
//  vtable slot 11 (the audio-engine "emit" method) with the item's
//  +0x10/+0x18/+0x24/+0x16 values and the release flag, accumulates +0x88 into
//  the 64-bit counter at +0x78, and computes the ring cursor +0x84 as
//  (+0x84 + 1) % (GetVfsDesc()[4] * ((+0x38 - +0x34) >> 2)).
//  [NOT byte-identical] placement-dependent (the relative calls to
//  AudioQueueBlockRelease / LHFree / GetVfsDesc, the indirect vtable
//  dispatches, the high-VA globals and the retn 4 hybrid); declared as a plain
//  free function.
static void AudioQueueItemProcess(void *obj, int a2)
{
    char *o = (char *)obj;
    DWORD *objd = (DWORD *)obj;
    DWORD *a2d = (DWORD *)a2;

    *(BYTE *)(o + 206) = 0;                // +0xCE = 0
    if (*(BYTE *)((char *)a2 + 44) != 0)   // +0x2C != 0: the early-out
        return;

    DWORD *v4 = *(DWORD **)(o + 32);        // +0x20 -> the block
    if (v4 == 0)
        return;

    v4[5] += a2d[7];                      // +0x14 += a2+0x1C
    v4[5] += a2d[8];                      // +0x14 += a2+0x20
    v4[6] -= a2d[7];                      // +0x18 -= a2+0x1C
    int v13 = 0;
    v4[6] -= a2d[8];                      // +0x18 -= a2+0x20
    objd[136 / 4] += a2d[7];              // +0x88 += a2+0x1C

    // the 64-bit counter at +0x70 -= (a2+0x1C) (unsigned) then -= sign-extended
    // a2+0x20 (signed)
    DWORD lo = objd[112 / 4];             // +0x70 (lo)
    DWORD hi = objd[116 / 4];             // +0x74 (hi)
    DWORD v5 = a2d[7];                    // a2+0x1C
    DWORD borrow = (lo < v5) ? 1u : 0u;   // CF of `lo -= v5`
    lo -= v5;
    hi -= borrow;
    int v12 = 0;
    DWORD v8 = a2d[8];                    // a2+0x20
    DWORD sext = ((signed int)v8 < 0) ? 0xFFFFFFFFu : 0u;
    borrow = (lo < v8) ? 1u : 0u;         // CF of `lo -= v8`
    lo -= v8;
    hi -= sext + borrow;
    objd[112 / 4] = lo;
    objd[116 / 4] = hi;

    DWORD v7 = v4[6];                     // +0x18
    if (v7 == 0 || a2d[6] != 0)           // a2+0x18 != 0 (or the difference was 0)
    {
        if (*(WORD *)(o + 148) == 353 && v4[3] == 0)
            v13 = 1;                      // +0x94 == 0x161 && +0x0C == 0
        borrow = (lo < v7) ? 1u : 0u;     // CF of `lo -= v7`
        lo -= v7;
        hi -= borrow;
        objd[112 / 4] = lo;
        objd[116 / 4] = hi;
        AudioQueueBlockRelease(o + 32, v4);   // 0x14788B8
        DWORD v8b = v4[4];                 // +0x10
        if (v8b != 0)
            ((void (__thiscall *)(DWORD))(*(DWORD *)v8b + 4))(v8b);   // vtable[1]
        v4[4] = 0;                        // +0x10 = 0
        v4[3] = 0;                        // +0x0C = 0
        LHFree(v4);                       // free the block
        objd[128 / 4] -= 1;               // +0x80 -= 1
        v12 = 1;
    }

    DWORD v9 = objd[136 / 4];             // +0x88
    if (v9 == objd[140 / 4]               // v9 == +0x8C
        || v13 != 0                       // the release flag
        || *(WORD *)(o + 148) == 353      // +0x94 == 0x161
        || objd[32 / 4] == 0)             // +0x20 == 0
    {
        // the object's vtable slot 11 (offset 0x2C): (this = +0x10,
        // item+0x10, v9, item+0x28, item+0x24, v12)
        DWORD p = *(DWORD *)(o + 16);     // +0x10 = the object
        DWORD q = *(DWORD *)p;            // *p = its vtable
        AudioQueueItemEmitFn fn = (AudioQueueItemEmitFn)(*(DWORD *)(q + 44));
        fn(p, a2d[4], v9, a2d[10], a2d[9], (char)v12);

        // the 64-bit add at +0x78 (unsigned), re-reading +0x88 after the dispatch
        DWORD _lo = objd[120 / 4];        // +0x78
        DWORD _hi = objd[124 / 4];        // +0x7C
        DWORD _v9 = objd[136 / 4];        // +0x88 (re-read)
        DWORD _carry = (_lo + _v9 < _lo) ? 1u : 0u;
        objd[120 / 4] = _lo + _v9;
        objd[124 / 4] = _hi + _carry;

        DWORD diff = objd[56 / 4] - objd[52 / 4];   // +0x38 - +0x34
        DWORD v10;
        if ((diff & 0xFFFFFFFCu) != 0)
            v10 = (DWORD)((int)(GetVfsDesc()[4]) * ((int)diff >> 2));   // sar 2
        else
            v10 = 0;
        DWORD v11 = (objd[132 / 4] + 1u) % v10;      // +0x84 = (+0x84 + 1) % v10
        objd[136 / 4] = 0;                           // +0x88 = 0
        objd[132 / 4] = v11;                         // +0x84 = v11
    }
}

// AudioTickFairnessPass  (0x1478B30 - 0x1478B9A)  -- the audio tick fairness /
// fall-through pass (thiscall, obj in ECX).  Reads the counter pair at
// +0x128 / +0x138 and the quota at +0x130; when the pair's sum is below the
// quota, subtracts the sum from the quota, then walks the delegate chain with
// the guard at +0x1C: when the guard is clear, the element holder at +0x18 is
// dereferenced to the element pointer (b) and validated against *((*obj) + 0x18)
// (the __invalid_parameter_noinfo check -- the null case and the equality case
// both reproduce the original check, the null case calling the check-then-zero
// path), the delegate object at (b + 0x0C) is then probed for its +0x38 function
// pointer, and the delegate is invoked as a thiscall (self = the delegate
// object, stack args (1, 0)); the word at +0x96 of the delegate object is
// subtracted from the remaining quota and the walk repeats until the quota is
// exhausted or the guard clears.  [NOT byte-identical] the body calls
// __invalid_parameter_noinfo (reproduced here as the no-op F3InvalidParam) and
// performs an indirect thiscall dispatch (the +0x38 slot) -- placement-dependent.
static DWORD AudioTickFairnessPass(void *obj)
{
    BYTE *o = (BYTE *)obj;
    DWORD v2 = *(DWORD *)(o + 0x138) + *(DWORD *)(o + 0x128);
    DWORD result = *(DWORD *)(o + 0x130);
    if ((unsigned int)v2 < result)
    {
        result -= v2;
        DWORD remaining = result;
        if ((int)remaining > 0)
        {
            for (;;)
            {
                if (*(DWORD *)(o + 0x1C) == 0)
                    break;
                BYTE *a = *(BYTE **)(o + 0x18);       // the element holder
                BYTE *b = *(BYTE **)a;                 // the element pointer
                BYTE *c = *(BYTE **)o;                 // *obj
                BYTE *d;
                if (c != 0)
                    d = *(BYTE **)c;
                else
                {
                    F3InvalidParam();                  // the null-obj debug check
                    d = 0;
                }
                if (b == *(BYTE **)((BYTE *)d + 0x18))
                    F3InvalidParam();                  // the equality debug check
                BYTE *e = *(BYTE **)((BYTE *)b + 0x0C);  // the delegate object
                BYTE *f = *(BYTE **)e;                 // the vtable holder
                void *fn = *(void **)((BYTE *)f + 0x38); // the +0x38 slot
                result = (DWORD)((int (__thiscall *)(void *, int, int))(void *)fn)(e, 1, 0);
                WORD w = *(WORD *)((BYTE *)e + 0x96);
                remaining -= w;
                if ((int)remaining <= 0)
                    break;
            }
        }
    }
    return result;
}

//  AudioDelegateNodeAudioStep  (0x1478600 - 0x1478680)  -- the per-node audio
//  step (thiscall, this in ECX = the consumed node, a2 on the stack = the
//  +0x140 slot value).  The vtable[13] dispatch through the node's vtable holder
//  (+0x10, slot 13 = byte +0x34) with the float scratch slot on the stack;
//  when the slot float is >= 0.003 (dword_1BD1618) the +0xC0 counter is bumped
//  (clamped to 0xBB9) and the +0xC4 counter is cleared, otherwise the +0xC4
//  counter is bumped (clamped to 0x65) and the +0xC0 counter is cleared.
//  Returns the vtable[13] call's result.
static int AudioDelegateNodeAudioStep(void *node, DWORD a2)
{
    DWORD node_addr = (DWORD)node;
    DWORD holder = *(DWORD *)(node_addr + 0x10);
    typedef int (__thiscall *VTabCall13)(DWORD holder, float *v);
    float v = 0.0f;
    int result = ((VTabCall13)((DWORD *)(*(DWORD *)holder))[13])(holder, &v);
    if (v >= 0.003000000026077032f)
    {
        *(DWORD *)(node_addr + 0xC0) += (DWORD)a2;
        if (*(DWORD *)(node_addr + 0xC0) > 0xBB8u)
            *(DWORD *)(node_addr + 0xC0) = 0xBB9u;
        *(DWORD *)(node_addr + 0xC4) = 0;
    }
    else
    {
        *(DWORD *)(node_addr + 0xC4) += (DWORD)a2;
        if (*(DWORD *)(node_addr + 0xC4) > 0x64u)
            *(DWORD *)(node_addr + 0xC4) = 0x65u;
        *(DWORD *)(node_addr + 0xC0) = 0;
    }
    return result;
}

//  AudioEngineChainSelect  (0x1478260 - 0x14782A0)  -- the per-status chain
//  selector (thiscall, this in ECX = the audio-engine object, a2 on the stack =
//  the status byte).  Switches the status: 1 -> the tick-counter chain
//  (this + (word[this+0x144] + 1) * 32), 2 -> this, 3 -> this + 0xE0,
//  4 -> this + 0x100, anything else -> 0.
static DWORD AudioEngineChainSelect(DWORD obj, int a2)
{
    switch (a2)
    {
    case 1: return obj + (DWORD)(((DWORD)(unsigned int)(*(WORD *)(obj + 0x144)) + 1u) * 32u);
    case 2: return obj;
    case 3: return obj + 0xE0;
    case 4: return obj + 0x100;
    default: return 0;
    }
}

//  GetProcessDelegate  (0x14601D0 - 0x14601D2)  -- the CProcessDelegateThiscall
//  object getter (returns dword_1DDC820).
static int GetProcessDelegate(void) { return (int)g_dword_1DDC820; }

//  sub_14796A0  -- [STUB] the node-consume destroy path (the full node-list
//  teardown: the (v9 != 4) / (v9 == 2) / AudioEngineChainSelect / sub_147DF10 / sub_147DDE0
//  / sub_1483D70 chain).  Deep dependency chain -- out of scope.
static DWORD sub_14796A0(void *obj) { (void)obj; return 0; }

//  sub_1478D20  -- [STUB] the audio-queue insertion (the per-node audio-queue
//  builder: the +0x130 / +0x138 fields + the sub_1477EA0 block check + the
//  sub_147C9B0 insertion).  Deep dependency chain -- out of scope.
static DWORD sub_1478D20(DWORD obj, DWORD a2, DWORD a3) { (void)obj; (void)a2; (void)a3; return 0; }

//  sub_147DF10  -- [STUB] the path builder (the stack-slot-backed path setter:
//  the sub_147C530 / sub_147C2B0 / sub_147D100 chain).  Deep dependency chain --
//  out of scope.
static DWORD sub_147DF10(DWORD *a1, DWORD a2) { (void)a1; (void)a2; return 0; }

//  sub_147DDE0  -- [STUB] the node-list walk / chain update (the self-linked
//  block walk + the sub_1477F10 node-release / sub_147D1E0 pair).  Deep
//  dependency chain -- out of scope.
static DWORD sub_147DDE0(DWORD obj, DWORD *a2) { (void)obj; (void)a2; return 0; }

//  sub_1483D70  -- [STUB] the delegate invoke (the sub_1488800 / sub_14815E0
//  batch-release chain).  Deep dependency chain -- out of scope.
static DWORD sub_1483D70(DWORD obj, DWORD a2, DWORD a3) { (void)obj; (void)a2; (void)a3; return 0; }

//  sub_1478980  -- [STUB] the +0x34-slot full-drain step (the GetVfsDesc /
//  AudioQueueBlockRelease / LHFree chain).  Deep dependency chain -- out of
//  scope.
static DWORD sub_1478980(void *obj) { (void)obj; return 0; }

//  sub_1478500  -- [STUB] the +0x34-slot incremental-drain step (the
//  sub_147A800 / sub_150D320 chain).  Deep dependency chain -- out of scope.
static DWORD sub_1478500(void *obj) { (void)obj; return 0; }

//  AudioDelegateNodeProcess  (0x1479800 - 0x1479A57)  -- the audio delegate
//  node-list per-node audio processing (thiscall, obj in ECX = the consumed
//  node, a2 on the stack = the +0x140 slot value).  The per-node audio step:
//  (1) the +0xDC gate: when the byte at +0xDC is clear, the vtable[9] dispatch
//      (slot 9 = byte +0x24) + the vtable[14] dispatch (slot 14 = byte +0x38;
//      the +0xD0 / +0xD4 accumulate / reset at +0xD8 / +0xDC);
//  (2) the always-called audio step (AudioDelegateNodeAudioStep: the node's
//      +0xC0 / +0xC4 counter updates driven by the vtable[13] dispatch);
//  (3) the +0xCE / +0xCF byte dispatches (+0x6C arm / sub_14796A0 teardown);
//  (4) the tail block (the vtable[7] dispatch at slot 7 = byte +0x1C; the
//      node-node vtable[2] call at the +0x0C node; the audio-queue insertion
//      via sub_1478D20; the +0xC8 status / decrement chain over the audio-
//      engine +0x148 status table; the audio-chain selector via
//      AudioEngineChainSelect; the +0x128 fader-counter subtraction; the
//      status = 1 write; the tick-counter advance + sub_147DDE0; the +0xEC
//      delegate invoke (GetProcessDelegate + sub_1483D70); the +0xEC reset);
//  (5) the final +0x20 / +0x00-[0x34] dispatch chain (sub_1478980 / sub_1478500).
//  The deep callees (sub_14796A0 / sub_1478D20 / sub_147DF10 / sub_147DDE0 /
//  sub_1483D70 / sub_1478980 / sub_1478500) are remaining no-op stubs;
//  AudioDelegateNodeAudioStep / AudioEngineChainSelect / GetProcessDelegate are implemented.
//  [NOT byte-identical] placement-dependent (the E8 calls + the indirect
//  vtable dispatches + the high-VA audio-engine global dword_1DDCCD0).
static DWORD AudioDelegateNodeProcess(void *obj, DWORD a2)
{
    DWORD node = (DWORD)obj;
    DWORD result;

    // ---- (1) the +0xDC gate + the vtable[9] / vtable[14] dispatch chain
    if (*(BYTE *)(node + 0xDC) == 0)
    {
        if (*(BYTE *)(node + 0xCD) != 0 || *(DWORD *)(node + 0xC8) == 3u)
        {
            DWORD holder = *(DWORD *)(node + 0x10);
            typedef unsigned char (__thiscall *VTabCall9)(DWORD holder);
            if ((unsigned char)((VTabCall9)((DWORD *)(*(DWORD *)holder))[9])(holder) == 0)
            {
                int v14 = 0, v15 = 0;
                typedef void (__thiscall *VTabCall14)(DWORD holder, int *slot);
                ((VTabCall14)((DWORD *)(*(DWORD *)holder))[14])(holder, &v14);
                if (*(DWORD *)(node + 0xD0) == (DWORD)v14 && *(DWORD *)(node + 0xD4) == (DWORD)v15)
                {
                    *(DWORD *)(node + 0xD8) += (DWORD)a2;
                    if (*(DWORD *)(node + 0xD8) > (*(WORD *)(node + 0x94) != 353u ? 7000u : 12000u))
                        *(BYTE *)(node + 0xDC) = 1;
                }
                else
                {
                    *(DWORD *)(node + 0xD0) = (DWORD)v14;
                    *(DWORD *)(node + 0xD4) = (DWORD)v15;
                    *(DWORD *)(node + 0xD8) = 0;
                    *(BYTE *)(node + 0xDC) = 0;
                }
            }
        }
    }

    // ---- (2) the always-called audio step
    result = (DWORD)AudioDelegateNodeAudioStep((void *)node, a2);

    // ---- (3) the +0xCE / +0xCF byte dispatches
    if (*(BYTE *)(node + 0xCE) != 0)
    {
        if (*(BYTE *)(node + 0xDC) != 0)
            *(BYTE *)(node + 0x6C) = 1;
        return result;
    }
    if (*(BYTE *)(node + 0xCF) != 0)
        return sub_14796A0((void *)node);

    // ---- (4) the tail block
    DWORD node_holder = *(DWORD *)(node + 0x10);
    typedef unsigned char (__thiscall *VTabCall7)(DWORD holder);
    result = (DWORD)(unsigned char)((VTabCall7)((DWORD *)(*(DWORD *)node_holder))[7])(node_holder);
    if (*(DWORD *)(node + 0xC8) == 3u && (result & 0xFFu) != 0u)
    {
        // the node-node call (the vtable[2] on the +0x0C node)
        DWORD v6 = *(DWORD *)(node + 0x20);
        if (v6 != 0)
        {
            DWORD v7 = *(DWORD *)(v6 + 0x0C);
            if (v7 != 0)
            {
                DWORD vt7 = *(DWORD *)v7;
                typedef void (__thiscall *VTabCall2)(DWORD v7);
                ((VTabCall2)((DWORD *)vt7)[2])(v7);
            }
        }
        // the audio-queue insertion (the +0x96 word + the &(node + 0x28) arg)
        result = sub_1478D20(g_dword_1DDCCD0, (DWORD)(unsigned int)(*(WORD *)(node + 0x96)), node + 0x28);
        DWORD v8 = *(DWORD *)(node + 0xC8);
        if (v8 != 1u)
        {
            DWORD v9 = g_dword_1DDCCD0;
            if (v8 == 2u)
            {
                DWORD v10 = *(DWORD *)(node + 0xBC);
                DWORD v11 = 0;
                if ((int)v10 > 1)
                    v11 = (v10 - 2u > 0x3E5u) ? 2u : 1u;
                DWORD *slot = (DWORD *)(v9 + 4u * v11 + 328u);
                if (*slot != 0)
                    *slot = *slot - 1u;
            }
            result = AudioEngineChainSelect(v9, (int)v8);
            if (result != 0)
            {
                DWORD slot2 = node;
                result = sub_147DF10(&slot2, result);
            }
            if (v8 == 3u)
            {
                DWORD target = v9 + 0x128u;
                if (target != 0)
                {
                    *(DWORD *)target -= (DWORD)(unsigned int)(*(WORD *)(node + 0x96));
                }
                result = target;
            }
            *(DWORD *)(node + 0xC8) = 1u;
            DWORD tick_addr = v9 + (DWORD)((unsigned int)(*(WORD *)(v9 + 0x144)) + 1u) * 32u;
            if (tick_addr != 0)
            {
                DWORD slot3 = node;
                result = sub_147DDE0(tick_addr, &slot3);
            }
            if (*(BYTE *)(node + 0xEC) != 0)
            {
                DWORD dlg = (DWORD)GetProcessDelegate();
                DWORD vE0 = *(DWORD *)(node + 0xE0);
                DWORD vE4 = *(DWORD *)(node + 0xE4);
                result = sub_1483D70(*(DWORD *)(dlg + 0x0C), vE0, vE4);
            }
            *(BYTE *)(node + 0xEC) = 0;
        }
    }

    // ---- (5) the +0x20 check + the [node + 0x00] +0x34 dispatch
    if (*(DWORD *)(node + 0x20) != 0)
    {
        DWORD v = *(DWORD *)node;
        DWORD fn = *(DWORD *)(v + 0x34);
        typedef unsigned char (__thiscall *NodeFn)(DWORD node);
        if ((unsigned char)((NodeFn)fn)(node) != 0)
            return sub_1478980((void *)node);
        return sub_1478500((void *)node);
    }
    return result;
}


// AudioNodeConsumedWalk  (0x147AF40)  -- the audio delegate node-list
//  consume walk (thiscall, this in ECX = the &v3_current slot).  Reads the
//  saved sentinel at the slot + 4 (the saved v2), chain-checks it (the +0x11
//  byte checks at +0, +4, +8), and writes the updated sentinel back into the
//  slot + 4 (the original returns void and writes *(this + 4) in place).  Two
//  paths, selected by the +0x11 byte check at +8:
//    branch A (byte at [v7 + 8] != 0) -- the chain walk: i = *(v7 + 4);
//      while byte at [i + 17] == 0: if v7 != *(i + 8) break; v7 = i; i = *(i + 4);
//      v7 = i (the last i).
//    branch B (byte at [v7 + 8] == 0) -- the pointer walk: j = *(v7 + 8);
//      while byte at [j + 17] == 0: v3 = j; j = *j;
//      v7 = v3 (the last j).
//  Called by AudioDelegateNodeListPass (0x1479A90).
//  [NOT byte-identical] placement-dependent (the relative E8 call to
//  __invalid_parameter_noinfo).
static DWORD AudioNodeConsumedWalk(DWORD v3_current, DWORD v7)
{
    if (v3_current == 0)
        F3InvalidParam();                     // original: __invalid_parameter_noinfo (no return)
    if (*(BYTE *)(v7 + 17) != 0)
    {
        F3InvalidParam();                     // original: __invalid_parameter_noinfo (no return)
        return v7;
    }
    DWORD v3 = *(DWORD *)(v7 + 8);
    if (*(BYTE *)(v3 + 17) != 0)
    {
        // branch A -- the chain walk
        DWORD i = *(DWORD *)(v7 + 4);
        while (*(BYTE *)(i + 17) == 0)
        {
            if (v7 != *(DWORD *)(i + 8))
                break;
            v7 = i;
            i = *(DWORD *)(i + 4);
        }
        v7 = i;
    }
    else
    {
        // branch B -- the pointer walk
        DWORD v3_pseudo = v3;
        for (DWORD j = *(DWORD *)v3_pseudo; *(BYTE *)(j + 17) == 0; j = *(DWORD *)j)
            v3_pseudo = j;
        v7 = v3_pseudo;
    }
    return v7;
}

// AudioDelegateNodeListPass  (0x1479A90)  -- the audio delegate node-list pass
//  (thiscall, obj in ECX = the audio engine, a2 on the stack = the element
//  holder).  Walks the node-list chain in a fixed-point loop: the saved
//  sentinel (v2 = *(a2[6])) is updated by AudioNodeConsumedWalk each iteration
//  and the loop breaks when the updated sentinel equals a2[6] (the sentinel
//  pointer).  Per iteration: the invalid check (v3 == 0 or != a2[0]), the
//  fixed-point break check, the chain dereference (v3 = *v3), the fixed-point
//  invalid check (v2 == *(v3 + 0x18)), the capture of *(v2 + 0x0C), the node
//  consume walk (AudioNodeConsumedWalk), and the per-node audio processing
//  (AudioDelegateNodeProcess).  v2 (the sentinel) is the only loop-carried state;
//  v3 (the iterator pointer) is unchanged across iterations.
//  [NOT byte-identical] placement-dependent (the relative E8 calls to
//  __invalid_parameter_noinfo / AudioNodeConsumedWalk / AudioDelegateNodeProcess + the high
//  -VA obj +0x140 slot read).
static void AudioDelegateNodeListPass(void *obj, void *a2)
{
    BYTE *obj_p = (BYTE *)obj;
    DWORD v4 = *(DWORD *)((DWORD)a2 + 0x18);      // a2[6]: the sentinel pointer
    DWORD v2 = *(DWORD *)v4;                      // v2 = *(a2[6]): the saved sentinel
    DWORD v3_current = *(DWORD *)a2;              // v3 = a2[0]: the iterator pointer
    DWORD v7 = v2;

    for (;;)
    {
        // the head of the loop: the invalid check (v3 == 0 or != a2[0])
        if (v3_current == 0 || v3_current != *(DWORD *)a2)
            F3InvalidParam();                    // original: __invalid_parameter_noinfo (no return)

        // the fixed-point break check
        if (v2 == v4)
            break;

        // the chain dereference (v3 = *v3)
        DWORD esi = v3_current;
        if (esi != 0)
            esi = *(DWORD *)esi;
        else
            F3InvalidParam();                    // original: __invalid_parameter_noinfo (no return)

        // the fixed-point invalid check
        if (v2 == *(DWORD *)(esi + 0x18))
            F3InvalidParam();                    // original: __invalid_parameter_noinfo (no return)

        // capture esi = *(v2 + 0x0C), then the node consume walk (updates v7)
        DWORD new_v3 = *(DWORD *)(v2 + 0x0C);
        v7 = AudioNodeConsumedWalk(v3_current, v7);

        // the per-node audio processing (AudioDelegateNodeProcess) -- edx re-read each iteration
        DWORD edx = *(DWORD *)(obj_p + 0x140);    // edx = *(this + 0x140)
        AudioDelegateNodeProcess((void *)new_v3, (DWORD)edx);

        // the loop-back: v2 = v7; v3_current unchanged
        v2 = v7;
    }
}

// AudioDelegateQueueDrain  (0x1478BA0 - 0x1478D16)  -- the audio delegate queue-drain
//  pass (thiscall, obj in ECX = the audio-engine object) [IMPLEMENTED].  Two fixed-point
//  passes over the element pointer chain, each seeded from a different slot of the
//  object:
//    pass 1 -- the obj+0 chain (this = obj): the saved iterator is *(obj + 0x00) and the
//      saved sentinel value is **(obj + 0x18).  Loop head: the sentinel pointer is
//      *(obj + 0x18) (re-read each iteration), the iterator invariant is
//      iterator == *(obj + 0x00) (re-read each iteration), the loop breaks when the saved
//      sentinel value equals the sentinel pointer.  Per iteration: the non-null/equality
//      checks, the dereference (iterator = *iterator), the fixed-point invalid check
//      (sentinel == *(dereferenced iterator + 0x18)), the capture of *(sentinel + 0x0C),
//      the node consume walk (AudioNodeConsumedWalk -- rewrites the saved sentinel in
//      place) and the per-node audio processing (vtable[14] dispatch thiscall(node, 1, 0))
//      when the node's +0xCC byte is non-zero and the node's +0xC8 status word is 2
//      (gate: +0xC0 >= 0xBB8) or 1 (gate: +0xC4 < 0x64) or anything else (no gate -- the
//      dispatch runs unconditionally) and the gate condition holds.
//    pass 2 -- the obj + 32*(word[obj+0x144] + 1) chain (the second node-list slot):
//      the saved sentinel is seeded from **(obj + 32*(word+1) + 0x18) and the saved
//      iterator from *(obj + 32*(word+1)).  Loop head: the tick counter is re-read each
//      iteration (word[obj+0x144]), the sentinel pointer is
//      *(obj + 32*(word+1) + 0x18), the iterator invariant is
//      iterator == *(obj + 32*(word+1)), the loop breaks when the saved sentinel value
//      equals the sentinel pointer.  Per iteration: the same checks and the capture of
//      *(sentinel + 0x0C), the node consume walk, and the per-node audio processing
//      (vtable[14] dispatch thiscall(node, 0, 1000)) when the node's +0xCC byte is zero
//      -- the dispatch runs directly -- or the +0xC8 status word is 2 (gate: +0xC0 >=
//      0xBB8) or 1 (gate: +0xC4 < 0x64) and the gate condition fails (the dispatch runs);
//      a status neither 2 nor 1 skips the dispatch entirely.
//  The +0x144 tick counter is READ but never written here (the tick increments it after
//  this pass returns).
//  [NOT byte-identical] placement-dependent (the relative E8 calls to
//  __invalid_parameter_noinfo / AudioNodeConsumedWalk and the indirect vtable dispatch
//  through the node's vtable[14]).
static void AudioDelegateQueueDrain(void *obj)
{
    DWORD iterator;         // the saved iterator pointer (constant across iterations)
    DWORD sentinel;         // the saved sentinel value (updated in place by the callee)

    // ---------------- pass 1 -- the obj+0 chain ----------------
    iterator = *(DWORD *)obj;                                   // *(obj + 0x00)
    sentinel = *(DWORD *)*(DWORD *)((BYTE *)obj + 0x18);        // **(obj + 0x18)
    for (;;)
    {
        DWORD sentinel_ptr = *(DWORD *)((BYTE *)obj + 0x18);    // the sentinel POINTER
        DWORD obj_iterator = *(DWORD *)obj;                      // the obj+0 field
        if (iterator == 0 || iterator != obj_iterator)
            F3InvalidParam();                                   // original: __invalid_parameter_noinfo (no return)
        if (sentinel == sentinel_ptr)
            break;                                              // the fixed-point break

        DWORD deref = iterator;                                 // the dereference
        if (deref != 0)
            deref = *(DWORD *)deref;
        else
            F3InvalidParam();                                   // original: __invalid_parameter_noinfo (no return)
        if (sentinel == *(DWORD *)(deref + 0x18))
            F3InvalidParam();                                   // original: __invalid_parameter_noinfo (no return)

        DWORD node = *(DWORD *)(sentinel + 0x0C);               // the per-node audio processing node
        sentinel = AudioNodeConsumedWalk(iterator, sentinel);    // rewrites the saved sentinel in place

        if (*(BYTE *)(node + 0xCC) != 0)
        {
            DWORD status = *(DWORD *)(node + 0xC8);
            DWORD gate = 1u;
            if (status == 2)
                gate = (*(DWORD *)(node + 0xC0) >= 0xBB8u) ? 1u : 0u;
            else if (status == 1)
                gate = (*(DWORD *)(node + 0xC4) < 0x64u) ? 1u : 0u;
            // (status neither 2 nor 1): no gate -- the dispatch runs unconditionally
            if (gate)
            {
                void **vt = (void **)*(void **)node;
                ((void (__thiscall *)(void *, int, int))vt[14])((void *)node, 1, 0);
            }
        }
        // loop-back: iterator stays constant (the callee never rewrites the iterator slot)
    }

    // ---------------- pass 2 -- the obj + 32*(word[obj+0x144] + 1) chain ----------------
    DWORD offset0 = (DWORD)((unsigned int)(*(WORD *)((BYTE *)obj + 0x144)) + 1u) * 32u;
    sentinel = *(DWORD *)*(DWORD *)((BYTE *)obj + offset0 + 0x18);   // **(obj + offset + 0x18)
    iterator = *(DWORD *)((BYTE *)obj + offset0);                    // *(obj + offset)
    for (;;)
    {
        DWORD tick = (unsigned int)(*(WORD *)((BYTE *)obj + 0x144));
        DWORD offset = (tick + 1u) * 32u;
        DWORD sentinel_ptr = *(DWORD *)((BYTE *)obj + offset + 0x18);
        DWORD obj_iterator = *(DWORD *)((BYTE *)obj + offset);
        if (iterator == 0 || iterator != obj_iterator)
            F3InvalidParam();                                   // original: __invalid_parameter_noinfo (no return)
        if (sentinel == sentinel_ptr)
            break;                                              // the fixed-point break

        DWORD deref = iterator;                                 // the dereference
        if (deref != 0)
            deref = *(DWORD *)deref;
        else
            F3InvalidParam();                                   // original: __invalid_parameter_noinfo (no return)
        if (sentinel == *(DWORD *)(deref + 0x18))
            F3InvalidParam();                                   // original: __invalid_parameter_noinfo (no return)

        DWORD node = *(DWORD *)(sentinel + 0x0C);               // the per-node audio processing node
        sentinel = AudioNodeConsumedWalk(iterator, sentinel);    // rewrites the saved sentinel in place

        if (*(BYTE *)(node + 0xCC) == 0)
        {
            // byte == 0: the dispatch runs directly
            void **vt = (void **)*(void **)node;
            ((void (__thiscall *)(void *, int, int))vt[14])((void *)node, 0, 1000);
            continue;
        }

        DWORD status = *(DWORD *)(node + 0xC8);
        if (status == 2)
        {
            if (*(DWORD *)(node + 0xC0) >= 0xBB8u)
                continue;   // gate true: skip the dispatch, loop back
        }
        else if (status == 1)
        {
            if (*(DWORD *)(node + 0xC4) < 0x64u)
                continue;   // gate true: skip the dispatch, loop back
        }
        else
        {
            continue;       // status neither 2 nor 1: skip the dispatch, loop back
        }

        // gate false: the dispatch runs
        void **vt = (void **)*(void **)node;
        ((void (__thiscall *)(void *, int, int))vt[14])((void *)node, 0, 1000);
    }
}

// SetProcessedCommitNumber  (0x148E290 - 0x148E2C0)  -- the CSetProcessedCommitNumber
//  command-object wrapper (cdecl, 1 arg).  When the SetProcessedCommitNumber gate flag
//  (g_byte_1DDCCE0) is set, stages a slot from the command-wrapper allocation cache
//  (g_unk_1DDCCE8, 0x1DDCCE8, VfsAllocCacheAlloc wait = 1), stores the command's
//  vtable (kCSetProcessedCommitNumberVftable, 0x01B71390) in the slot, and stores the
//  commit number (the +4 payload); the staged object is finalized (VfsAllocCacheFinalize).
//  Gated off in this build (g_byte_1DDCCE0 is zero BSS) and, when the cache is not
//  initialised, the staged pointer is 0 -- the original would store the payload to
//  address 4, which this implementation does not reproduce (the original's unconditional
//  `v1[1] = a1` is dead on the 0-staged path and would fault; same null-guard convention
//  as AudioProcessDelegateTick).  [NOT byte-identical] placement-dependent (relative
//  E8 calls to the pool / slot callees) and refs the high-VA globals g_byte_1DDCCE0 /
//  g_unk_1DDCCE8.
static void SetProcessedCommitNumber(DWORD a1)
{
    if (g_byte_1DDCCE0 == 0)
        return;
    DWORD *v1 = (DWORD *)VfsAllocCacheAlloc(g_unk_1DDCCE8, 1);
    if (v1 != 0) {
        v1[0] = kCSetProcessedCommitNumberVftable;
        v1[1] = a1;
    }
    VfsAllocCacheFinalize(g_unk_1DDCCE8);
}

// CommitCommand  (0x148E500 - 0x148E534)  -- the CCommitCommand command-object wrapper
//  (cdecl, 0 args).  Stages a slot from the command-wrapper allocation cache
//  (g_unk_1DDCCE8, 0x1DDCCE8), stores the command's vtable (kCCommitCommandVftable,
//  0x01B71450) in the slot when one is staged, finalizes the stage (VfsAllocCacheFinalize)
//  and returns the new commit counter: the InterlockedExchangeAdd(1) of the commit
//  counter (g_dword_1DDCD34, 0x1DDCD34) -- the original's `lock xadd` + `inc eax`.
//  [NOT byte-identical] placement-dependent (relative E8 calls to the pool / slot
//  callees) and refs the high-VA globals g_unk_1DDCCE8 / g_dword_1DDCD34.
static int CommitCommand(void)
{
    DWORD *v1 = (DWORD *)VfsAllocCacheAlloc(g_unk_1DDCCE8, 1);
    if (v1 != 0)
        v1[0] = kCCommitCommandVftable;
    VfsAllocCacheFinalize(g_unk_1DDCCE8);
    return (int)(InterlockedExchangeAdd((volatile LONG *)&g_dword_1DDCD34, 1) + 1);
}

// AudioTickExitStep  (0x146D9D0 - 0x146D9DC)  -- the audio tick exit step: the RWLock
//  getter (g_dword_1DDCD74) + the SetProcessedCommitNumber / CommitCommand command
//  pair + SyncLockedStateChains.  Loads the concurrency reader-writer-lock object (via the
//  sub_1495860 getter 0x1495860), stages the commit number at [lock+0x1c] through
//  SetProcessedCommitNumber (0x148E290, cdecl), increments the counter, stages the
//  commit command (CommitCommand 0x148E500, cdecl -- result discarded) and tail-calls
//  SyncLockedStateChains(this=lock).
//  [NOT byte-identical] placement-dependent (relative E8 calls + an E9 tail-jmp to
//  SyncLockedStateChains) and refs the high-VA globals.
static int AudioTickExitStep(void)
{
    DWORD lock = g_dword_1DDCD74;   // sub_1495860 (0x1495860): the RWLock getter
    DWORD counter = *(DWORD *)(lock + 0x1c);
    SetProcessedCommitNumber(counter);           // 0x148E290 (cdecl)
    *(DWORD *)(lock + 0x1c) = counter + 1;       // ++[lock+0x1c]
    CommitCommand();                             // 0x148E500 (cdecl) -- result discarded
    SyncLockedStateChains((void *)lock);            // 0x1496FA0 (thiscall) -- tail-call
    return 0;
}

// AudioProcessDelegateTick  (0x147A740 - 0x147A74B)  -- the registered audio
//  process tick.  The stub loads ECX = the audio-engine object (dword_1DDCCD0)
//  and tail-jumps into the real body at 0x147A210: enter (AudioTickEnterStep), the
//  delegate step (AudioDelegateTickStep), the pending-queue item loop (AudioQueuePoll ->
//  AudioQueueItemProcess), the fairness pass (AudioTickFairnessPass), the three node-list passes
//  (AudioDelegateNodeListPass: this=v0 / v0 + 32*(word[v0+0x144] + 1) / v0 + 0xE0), the
//  queue-drain pass (AudioDelegateQueueDrain), the +0x144 tick counter ((word + 1) % 6),
//  then tail-calls the exit (AudioTickExitStep) and returns its result.
//  [NOT byte-identical] placement-dependent (the relative E8 calls + the high-
//  VA audio-engine global dword_1DDCCD0); the deep callee chain is stubbed.
static int AudioProcessDelegateTick(void)
{
    DWORD v0 = g_dword_1DDCCD0;   // 0x1DDCCD0: the audio-engine object (340 bytes)
    if (v0 == 0)
        return 0;                // [guard] the original dereferences v0 unconditionally
    AudioTickEnterStep();         // 0x146D930 -- unused result
    AudioDelegateTickStep((void *)v0);   // 0x1478F20 (thiscall)
    for (int i = AudioQueuePoll(); i != 0; i = AudioQueuePoll())
        AudioQueueItemProcess((void *)*(DWORD *)i, i);   // 0x1478820 (thiscall + stack arg)
    AudioTickFairnessPass((void *)v0);   // 0x1478B30 (thiscall, result unused)
    AudioDelegateNodeListPass((void *)v0, (void *)v0);     // 0x1479A90 (this=v0, a2=v0)
    AudioDelegateNodeListPass((void *)v0, (void *)(v0 + 32 * ((unsigned int)(*(WORD *)(v0 + 0x144) + 1))));
    AudioDelegateNodeListPass((void *)v0, (void *)(v0 + 0xE0));   // 0x1479A90 (this=v0, a2=v0+0xE0)
    AudioDelegateQueueDrain((void *)v0);   // 0x1478BA0 (thiscall)
    *(WORD *)(v0 + 0x144) = (WORD)((unsigned int)(*(WORD *)(v0 + 0x144) + 1) % 6);
    return AudioTickExitStep();      // 0x146D9D0 -- tail-call, result returned
}

// AudioFrameworkInit  (0x150D180 - 0x150D1D8)  -- one-time audio framework
//  bring-up: register the two worker-callback pairs with VfsWorkerCallbackRegister
//  -- slot 1: &unk_1DDCF30 with the pair value dword_1DDCF2C = 1, slot 2:
//  &unk_1DDCFB8 with the pair value dword_1DDCFB4 = 2 -- each pair guarded by its
//  own done-flag (byte_1DDCF28 / byte_1DDCFB0).  The pair value is stored BEFORE
//  the call and the done-flag AFTER it.  Returns 1 when both pairs are freshly
//  registered, 0 when the first flag is already set (or the second one is).
//  [NOT byte-identical] two relative E8 calls to VfsWorkerCallbackRegister and
//  the high-VA globals g_byte_1DDCF28 / g_dword_1DDCF2C / g_unk_1DDCF30 /
//  g_byte_1DDCFB0 / g_dword_1DDCFB4 / g_unk_1DDCFB8.
static int AudioFrameworkInit(void)
{
    if (g_byte_1DDCF28 != 0)
        return 0;
    g_dword_1DDCF2C = 1;                                     // 0x1DDCF2C
    VfsWorkerCallbackRegister((void *)&g_unk_1DDCF30, 1);   // 0x150D19A
    g_byte_1DDCF28 = 1;                                      // 0x1DDCF28
    if (g_byte_1DDCFB0 != 0)
        return 0;
    g_dword_1DDCFB4 = 2;                                     // 0x1DDCFB4
    VfsWorkerCallbackRegister((void *)&g_unk_1DDCFB8, 2);   // 0x150D1C3
    g_byte_1DDCFB0 = 1;                                      // 0x1DDCFB0
    return 1;
}

// AudioProcessDelegateInit  (0x147A750 - 0x147A7AC)  -- build the audio process
//  delegate: AudioFrameworkInit, then LHAlloc(340) + AudioEngineCtor(desc[+0x04],
//  desc[+0x18], desc[+0x1C]) into g_dword_1DDCCD0 (0 on OOM), register the
//  {vtable, AudioProcessDelegateTick} pair into worker slot 0; returns the
//  (void) registration (0).
static int AudioProcessDelegateInit(void)
{
    DWORD *v0 = GetVfsDesc();
    AudioFrameworkInit();                    // 0x150D180
    void *mem = LHAlloc(340);
    g_dword_1DDCCD0 = (mem != 0) ? (DWORD)AudioEngineCtor(mem, v0[1], v0[6], v0[7]) : 0;
    g_off_1D6E9C8[1] = (DWORD)AudioProcessDelegateTick;   // 0x1D6E9CC
    VfsWorkerCallbackRegister(g_off_1D6E9C8, 0);         // 0x1476D00
    return 0;
}

static int VfsQueryTableDrain(void *obj, int elem);   // defined below (0x1474320)

//  VfsQueryTableBuild  (0x1473E70 - 0x1473F30)  -- build the query table:
//  drain the pending ring into the cache under the +0x34 critical section.
//  thiscall (obj in ECX, timeout on the stack, retn 4); void.  Under the CS
//  (a 8-byte F3CsGuard over the cache object's +0x34), when the cache is
//  initialised (+0x18 != 0): count = the ring's used counter (the control
//  block's +0x10C word) / the element size (+0x14); when count > 0, loop,
//  processing the slot at the ring cursor (+0x108): call its vtable[1] then
//  vtable[0](slot, 0), drain one query (VfsQueryTableDrain), and break out
//  once clock() - start > timeout (the unsigned comparison).  The timeout is
//  the `a2` stack argument (the descriptor's +0x04 = the worker count).
//  [NOT byte-identical] placement-dependent (the relative E8 calls to the
//  clock / VfsQueryTableDrain / CsGuardAcquire / CsGuardRelease / the
//  SetEvent IAT thunk, the high-VA cache global g_unk_1DDC830, and the
//  control-block wrapper at o+4 -- an indirect dispatch through the slot's
//  vtable, all of which relocate).
static void VfsQueryTableBuild(void *obj, unsigned int timeout)
{
    BYTE *o = (BYTE *)obj;
    F3CsGuard guard;
    CsGuardAcquire(&guard, (CRITICAL_SECTION *)(o + 0x34), 0);
    if (o[0x18] != 0)
    {
        BYTE *wrapper = *(BYTE **)(o + 4);           // o[1]: the block wrapper
        BYTE *ring = *(BYTE **)wrapper;              // the ring base (v7[0])
        DWORD used = *(DWORD *)(ring + 0x10C);       // the used counter
        DWORD elem = *(DWORD *)(o + 0x14);           // the element size
        unsigned int count = used / elem;            // the original divides unguarded
        clock_t start = clock();
        if ((int)count > 0)
        {
            for (unsigned int i = 0; i < count; ++i)
            {
                unsigned int cursor = *(DWORD *)(ring + 0x108);   // the ring cursor
                BYTE *slot = ring + 0x110 + cursor;              // the ring-slot object
                void **slotVtable = *(void ***)slot;
                void (*slot1)(void *) = (void (*)(void *))slotVtable[1];
                void (*slot0)(void *, unsigned int) = (void (*)(void *, unsigned int))slotVtable[0];
                slot1(slot);                                     // vtable slot 1 (thiscall)
                slot0(slot, 0);                                   // vtable slot 0 (thiscall + arg)
                VfsQueryTableDrain((void *)wrapper, (int)elem);   // drain one query
                DWORD elapsed = clock() - start;
                if (elapsed > timeout)                           // unsigned compare
                    break;
            }
        }
    }
    CsGuardRelease(&guard);
}

//  VfsQueryTableDrain  (0x1474320 - 0x147439F)  -- drain one pending query:
//  advance the ring cursor by the element size (wrapping to 0 at the limit,
//  __debugbreak on an overrun), decrement the used counter, and signal the
//  frontend event; returns the SetEvent result.  thiscall (obj in ECX =
//  the block wrapper, elemSize on the stack, retn 4).  The ring base is
//  v7[0] (the wrapper's first word); the limit is at +0x100, the cursor at
//  +0x108, the used counter at +0x10C, and the event handle is at wrapper
//  +0x88 (the second of the two EventObjCtor event wrappers -- +0x80 within
//  it, via SetFrontendEvent).
//  [NOT byte-identical] placement-dependent (the relative calls to
//  InterlockedExchangeAdd / SetFrontendEvent / clock IAT thunks, the high-
//  VA wrapper global, and the vtable dispatch in the caller).
static int VfsQueryTableDrain(void *obj, int elem)
{
    BYTE *wrapper = (BYTE *)obj;
    BYTE *ring = *(BYTE **)wrapper;              // the ring base (v7[0])
    DWORD limit = *(DWORD *)(ring + 0x100);
    DWORD cursor = *(DWORD *)(ring + 0x108);
    DWORD newCursor = cursor + (unsigned int)elem;
    if (newCursor > limit)
        __debugbreak();
    newCursor = (newCursor == limit) ? 0u : newCursor;
    *(DWORD *)(ring + 0x108) = newCursor;
    InterlockedExchangeAdd((volatile LONG *)(ring + 0x10C), -(LONG)elem);
    return (int)SetFrontendEvent((void *)(wrapper + 0x88));
}

// VfsGetQueryTable  (0x1460210 - 0x1474011)  -- the VFS query-table getter
//  registered as the worker-slot-0 callback.  In drain mode (g_dword_1DDC824 ==
//  2) it walks the cache's +0x10C counter, draining pending queries
//  (VfsQueryTableDrain) until caught up; otherwise it (re)builds the table via
//  VfsQueryTableBuild over the descriptor's +0x04 count.  Returns the result
//  pointer (the cache's +0x10C in drain mode; void/0 otherwise).
static DWORD *VfsGetQueryTable(void)
{
    if (g_dword_1DDC824 == 2) {
        void *wrapper = *(void **)((char *)g_unk_1DDC830 + 4);   // 0x1DDC834: the block wrapper (o[1])
        BYTE *ring = *(BYTE **)wrapper;                          // the ring base (v7[0])
        DWORD *result = (ring != 0) ? (DWORD *)(ring + 0x10C) : 0;  // the used counter
        while (ring != 0 &&
               *(DWORD *)(ring + 0x10C) > *(DWORD *)((char *)g_unk_1DDC830 + 0x14)) {
            VfsQueryTableDrain(wrapper, *(DWORD *)((char *)g_unk_1DDC830 + 0x14));
        }
        return result;
    }
    DWORD *v0 = GetVfsDesc();
    VfsQueryTableBuild(g_unk_1DDC830, v0[1]);
    return 0;
}

// InitVirtualFileSystem  (0x1460650 - 0x1460701)  -- see the header above.
static int InitVirtualFileSystem(void *descIn)
{
    DWORD *desc = (DWORD *)descIn;
    if (g_byte_1DDC828 != 0)
        return 0;
    if (VfsWorkerPoolInit(desc[2], desc[1]) == 0)              // 0x1476B50
        return 0;
    if (VfsAllocCacheInit() == 0)                              // 0x14743F0
        return 0;
    DWORD slot = 0;                                             // the [esp-4] BYREF slot
    void *probe = VfsAllocCacheIdentity((char *)&slot + 3);     // 0x146D870 (identity)
    if (VfsCacheQueryInit((int)probe) == 0)                    // 0x146D8E0
        return 0;
    AudioProcessDelegateInit();                                // 0x147A750
    if (VfsQueryProviderPair() == 0)                           // 0x1477990
        return 0;
    void *p = LHAlloc(16);                                     // the 16-byte process delegate
    g_dword_1DDC820 = (p != 0) ? (DWORD)CProcessDelegateThiscallCtor(p) : 0;  // 0x146E220
    if (VfsAllocCacheCtor(g_unk_1DDC830, 80, 4096) == 0)       // 0x1473CF0
        return 0;
    g_off_1D6A80C[1] = (DWORD)VfsGetQueryTable;                // 0x1D6A810
    VfsWorkerCallbackRegister(g_off_1D6A80C, 0);               // 0x1476D00
    g_byte_1DDC828 = 1;
    return 1;
}

// RegisterVfsGroup1..4  (0x146F130 / 0x146F200 / 0x146F240 / 0x146F250)  --
//  register the per-provider VFS module groups (audio / video / input /
//  codec-registration providers) after a successful init.
//
//  RegisterVfsGroup1/2/3/4 are implemented (below).
//
//  The 15 VFS source-program component vftables, the 4 effect-program component
//  vftables and the CRegisteredComponent/CRegisteredEffectProgramComponent block
//  layout (original high-VA addresses kept in comments).  Each component is a
//  bare vftable holder built by LHAlloc; the 40-byte registration block is built
//  by the registration helper (VfsRegisterSourceComponent /
//  VfsRegisterEffectProgramComponent).  Group 4's three audio codec components
//  (PCM / XWMA / Crescendo) are bare 4-byte vftable holders built by LHAlloc;
//  the 20-byte CRegisteredCodec block is built by VfsRegisterCodecComponent.
static const DWORD kVfsNAtmosComponentVftable         = 0x01B73B34;  // 0x1B73B34: ??_7CComponent@NAtmos@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsNCrescendoComponentVftable     = 0x01B73A38;  // 0x1B73A38: ??_7CComponent@NCrescendo@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsNMapSolverComponentVftable     = 0x01B738D8;  // 0x1B738D8: ??_7CComponent@NMapSolver@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsNMusicSolverComponentVftable   = 0x01B73730;  // 0x1B73730: ??_7CComponent@NMusicSolver@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsNChannelMachinesComponentVftable = 0x01B73500;  // 0x1B73500: ??_7CComponent@NChannelMachines@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsMusicalNotesComponentVftable   = 0x01B73368;  // 0x1B73368: ??_7CComponent@MusicalNotes@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsQueuedSequenceComponentVftable = 0x01B73150;  // 0x1B73150: ??_7CComponent@QueuedSequence@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsQueuePopperComponentVftable    = 0x01B72FE4;  // 0x1B72FE4: ??_7CComponent@QueuePopper@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsSequenceComponentVftable       = 0x01B72E20;  // 0x1B72E20: ??_7CComponent@Sequence@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsNSVComponentVftable            = 0x01B72D24;  // 0x1B72D24: ??_7CComponent@NSV@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsSmartVariationsComponentVftable = 0x01B72B50;  // 0x1B72B50: ??_7CComponent@SmartVariations@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsNSourceBlenderComponentVftable = 0x01B72954;  // 0x1B72954: ??_7CComponent@NSourceBlender@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsSplittersComponentVftable      = 0x01B72704;  // 0x1B72704: ??_7CComponent@Splitters@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsCWaveformsComponentVftable     = 0x01B72558;  // 0x1B72558: ??_7CWaveforms@NSourcePrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsCSpeechWaveformsComponentVftable = 0x01B72450;  // 0x1B72450: ??_7CSpeechWaveforms@NSourcePrograms@NFramework@NLHAudio@@6B@

static const DWORD kCRegisteredSourceProgramComponentVftable = 0x01B72414;  // 0x1B72414: ??_7CRegisteredSourceProgramComponent@NFramework@NLHAudio@@6B@
static const DWORD kCMapTableLookupVftable = 0x01B723D0;  // 0x1B723D0: ??_7?$CMap@VCTableLookupEntry@NFramework@NLHAudio@@...$0BM@@Intrusive@Containers@NSupport@@6B@
static const DWORD kVfsCRegisteredEffectProgramComponentVftable = 0x01B72408;  // 0x1B72408: ??_7CRegisteredEffectProgramComponent@NFramework@NLHAudio@@6B@
static const DWORD kVfsNFaderDuckerComponentVftable   = 0x01B74104;  // 0x1B74104: ??_7CComponent@NFaderDucker@NEffectPrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsNFaderPitchDuckerComponentVftable = 0x01B73F70;  // 0x1B73F70: ??_7CComponent@NFaderPitchDucker@NEffectPrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsNLinkToFaderComponentVftable   = 0x01B73E60;  // 0x1B73E60: ??_7CComponent@NLinkToFader@NEffectPrograms@NFramework@NLHAudio@@6B@
static const DWORD kVfsNRandomLevelModulatorComponentVftable = 0x01B73C98;  // 0x1B73C98: ??_7CComponent@NRandomLevelModulator@NEffectPrograms@NFramework@NLHAudio@@6B@
static const DWORD kCTestFilterFactoryVftable = 0x01B7422C;  // 0x1B7422C: ??_7CTestFilterFactory@NTestFilter@NAudioFilter@NLHAudio@@6B@

// ---- group 4: the three audio-codec components (PCM / XWMA / Crescendo) and
//  the 20-byte codec registration block (original high-VA addresses kept in
//  comments; group 1/2's component vftables are the source/effect-program ones).
static const DWORD kVfsCodecPcmVftable            = 0x01B743B4;  // 0x1B743B4: ??_7CodecPCM@NCodecPCM@NCodec@NLHAudio@@6B@
static const DWORD kVfsCodecXwmaVftable           = 0x01B7433C;  // 0x1B7433C: ??_7CodecXWMA@NCodecXWMA@NCodec@NLHAudio@@6B@
static const DWORD kVfsCodecCrescendoVftable      = 0x01B742C0;  // 0x1B742C0: ??_7CodecCrescendo@NCodecCrescendo@NCodec@NLHAudio@@6B@
static const DWORD kVfsCodecCBaseVftable          = 0x01B70744;  // 0x1B70744: ??_7?$CBase@VICodec@NCodec@NLHAudio@@...@@Allocated@Containers@NSupport@@6B@
static const DWORD kVfsCodecIntrusiveCBaseVftable = 0x01B70700;  // 0x1B70700: ??_7?$CBase@V?$CIterator@VICodec@NCodec@NLHAudio@@...@@Intrusive@Containers@NSupport@@6B@
static const DWORD kVfsCodecAllocatedCMapVftable  = 0x01B7074C;  // 0x1B7074C: ??_7?$CMap@VICodec@NCodec@NLHAudio@@...@@Allocated@Containers@NSupport@@6B@

static DWORD g_dword_1DDCCD4 = 0;               // 0x1DDCCD4: the 20-byte codec registration block (lazy init, shared across the three calls)
static DWORD g_dword_1DDCE7C = 0;               // 0x1DDCE7C: the XWMA worker-callback registration count (refcount)
static BYTE  g_byte_1DDCE78 = 0;                // 0x1DDCE78: the XWMA worker-callback registered flag
static DWORD g_dword_1D80B70 = 0;               // 0x1D80B70: the XWMA worker callback pointer (sub_150B110 -- stubbed)
static DWORD g_off_1D80B6C[2] = { kCProcessDelegateStaticVftable, 0 };  // 0x1D80B6C: the {vtable, type_info} pair (vtable slot real, fn slot high-VA stubbed)

//  VfsAllocSourceProgramComponent  -- the shared body of the 15 per-provider
//  allocation subs (sub_14F8BC0 / sub_14F1570 / ... / sub_14B3870): LHAlloc(4)
//  a bare 4-byte vftable holder; nullptr on allocation failure.
static void *VfsAllocSourceProgramComponent(DWORD vftable)
{
    void *p = LHAlloc(4);
    if (p != 0)
        *(DWORD *)p = vftable;
    return p;
}

//  VfsRegisterSourceComponent  (0x146DF00 + 0x14B2740)  -- the thiscall
//  registration helper: build the 40-byte CRegisteredSourceProgramComponent
//  block over the 4-byte component and register it into the VFS refcount wrapper.
//  The block: [0] CRegisteredSourceProgramComponent vtable (the temp
//  CRegisteredComponent slot is overwritten), [4] CMap vtable (the temp CBase
//  vtable is overwritten), [8..28] the two-node intrusive CMap base via
//  CMapBaseCtor (0x150C5A0), [16..24]/[32] words 0-2-4-8 zeroed, [36] the
//  component.  The deep leaves are documented stubs in this build: the 48-byte
//  wrapper-list node build (sub_14B2350) and the intrusive CMap insert
//  (sub_150C060, itself with its sub_150BAF0 / sub_150BA10 chain).  The original
//  stores the component at block[9] unconditionally, which would crash on
//  allocation failure; this build skips the registration (returns 0) on OOM.
static int VfsRegisterSourceComponent(void *wrapper, void *component)
{
    DWORD block = (DWORD)LHAlloc(40);   // 40-byte CRegisteredSourceProgramComponent
    (void)wrapper;
    if (block != 0) {
        DWORD *b = (DWORD *)block;
        b[0] = kCRegisteredSourceProgramComponentVftable;
        b[1] = kCMapTableLookupVftable;
        CMapBaseCtor((void *)(block + 8));                 // 0x150C5A0
        b[4] = 0; b[5] = 0; b[6] = 0; b[8] = 0;            // sub_150C560: words 0-2-4
        b[9] = (DWORD)component;                          // [block+36] = component
    }
    // (sub_14B2350(this=block, arg=wrapper+0x18) and sub_150C060(this=wrapper+4,
    //  args={block, wrapper}) are the documented no-op stubs here; the original
    //  registers the component into the wrapper's 48-byte component map through
    //  them.  No observable effect in this build.)
    return 0;
}

//  RegisterVfsGroup1  (0x146F130 - 0x146F1FA)  -- allocate the fifteen
//  VFS source-program components (NAtmos .. CSpeechWaveforms) and register each
//  into the refcount wrapper passed in (the 4-byte VfsRefCountCtor object at
//  dword_1DDC7E4, cdecl, one stack DWORD) through VfsRegisterSourceComponent.
//  Returns the result of the final registration (the original returns the last
//  sub_146DF00's result).  [NOT byte-identical] placement-dependent (the 15
//  relative E8 alloc calls plus the thiscall registration call) and refs the
//  original high-VA component vftables.
static int RegisterVfsGroup1(DWORD wrapper)
{
    int result;
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsNAtmosComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsNCrescendoComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsNMapSolverComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsNMusicSolverComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsNChannelMachinesComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsMusicalNotesComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsQueuedSequenceComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsQueuePopperComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsSequenceComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsNSVComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsSmartVariationsComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsNSourceBlenderComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsSplittersComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsCWaveformsComponentVftable));
    result = VfsRegisterSourceComponent((void *)wrapper, VfsAllocSourceProgramComponent(kVfsCSpeechWaveformsComponentVftable));
    return result;
}

//  VfsAllocEffectProgramComponent4  -- the shared body of the three 4-byte
//  effect-program component alloc subs (sub_15046B0 / sub_15022B0 / sub_1500100):
//  LHAlloc(4), store the class's vftable; nullptr on allocation failure.
static void *VfsAllocEffectProgramComponent4(DWORD vftable)
{
    void *p = LHAlloc(4);
    if (p != 0)
        *(DWORD *)p = vftable;
    return p;
}

//  VfsAllocEffectProgramComponent36  -- the sub_1505770 body (NFaderDucker):
//  LHAlloc(36), store the class's vftable; nullptr on allocation failure.  The
//  original additionally builds a throwaway stack-local 32-byte scratch block
//  (two heap objects, never referenced or returned -- the net effect on the
//  returned component is just the allocation + the vftable store), so it is not
//  reproduced here.
static void *VfsAllocEffectProgramComponent36(DWORD vftable)
{
    void *p = LHAlloc(36);
    if (p != 0)
        *(DWORD *)p = vftable;
    return p;
}

//  VfsRegisterEffectProgramComponent  (0x146DEF0 + 0x14B27F0)  -- the thiscall
//  registration helper for the effect-program components: build the 40-byte
//  CRegisteredEffectProgramComponent block over the component and register it
//  into the VFS refcount wrapper.  The block: [0] CRegisteredEffectProgram
//  Component vtable (the temp CRegisteredComponent slot is overwritten), [4] CMap
//  vtable (the temp CBase vtable is overwritten), [8..28] the two-node intrusive
//  CMap base via CMapBaseCtor (0x150C5A0), [16..24]/[32] words 0-2-4-8 zeroed
//  (sub_150C560), [36] the component.  The deep leaves are documented stubs in
//  this build: the 48-byte wrapper-list node build (sub_14B2350, called as
//  sub_14B2350(this=block, a2=componentMap+0x24)) and the intrusive CMap insert
//  (sub_150C060, this=componentMap+0x10 / a2=componentMap+0x0C / a3=block), both
//  whose deep bodies are out of scope; here componentMap is the first DWORD of
//  the 4-byte wrapper (*wrapper, the 48-byte component-map block built by
//  VfsComponentMapCtor).  The original stores the component at block[9]
//  unconditionally, which would crash on allocation failure; this build skips
//  the registration (returns 0) on OOM.
static int VfsRegisterEffectProgramComponent(void *wrapper, void *component)
{
    DWORD block = (DWORD)LHAlloc(40);   // 40-byte CRegisteredEffectProgramComponent
    (void)wrapper;
    if (block != 0) {
        DWORD *b = (DWORD *)block;
        b[0] = kVfsCRegisteredEffectProgramComponentVftable;
        b[1] = kCMapTableLookupVftable;
        CMapBaseCtor((void *)(block + 8));                 // 0x150C5A0
        b[4] = 0; b[5] = 0; b[6] = 0; b[8] = 0;            // sub_150C560: words 0-2-4
        b[9] = (DWORD)component;                          // [block+36] = component
    }
    // (sub_14B2350(this=block, a2=componentMap+0x24) and sub_150C060(this=
    //  componentMap+0x10, a2=componentMap+0x0C, a3=block) are the documented
    //  no-op stubs here; the original registers the component into the wrapper's
    //  48-byte component map through them.  No observable effect in this build.)
    return 0;
}

//  RegisterVfsGroup2  (0x146F200 - 0x146F23B)  -- allocate the four
//  VFS effect-program components (NEffectPrograms: NFaderDucker /
//  NFaderPitchDucker / NLinkToFader / NRandomLevelModulator) and register each
//  into the refcount wrapper passed in (the 4-byte VfsRefCountCtor object at
//  dword_1DDC7E4, cdecl, one stack DWORD) through
//  VfsRegisterEffectProgramComponent.  Returns the result of the final
//  registration (the original returns the last sub_146DEF0's result).  [NOT
//  byte-identical] placement-dependent (the four relative E8 alloc calls plus
//  the thiscall registration call) and refs the original high-VA component
//  vftables.
static int RegisterVfsGroup2(DWORD wrapper)
{
    int result;
    result = VfsRegisterEffectProgramComponent((void *)wrapper, VfsAllocEffectProgramComponent36(kVfsNFaderDuckerComponentVftable));
    result = VfsRegisterEffectProgramComponent((void *)wrapper, VfsAllocEffectProgramComponent4(kVfsNFaderPitchDuckerComponentVftable));
    result = VfsRegisterEffectProgramComponent((void *)wrapper, VfsAllocEffectProgramComponent4(kVfsNLinkToFaderComponentVftable));
    result = VfsRegisterEffectProgramComponent((void *)wrapper, VfsAllocEffectProgramComponent4(kVfsNRandomLevelModulatorComponentVftable));
    return result;
}

//  VfsAllocTestFilterComponent  (0x15091F0 - 0x1509207)  -- the sub_15091F0
//  body: LHAlloc(4) a bare 4-byte CTestFilterFactory vftable holder; nullptr
//  on allocation failure.
static void *VfsAllocTestFilterComponent(void)
{
    void *p = LHAlloc(4);
    if (p != 0)
        *(DWORD *)p = kCTestFilterFactoryVftable;
    return p;
}

//  RegisterVfsGroup3  (0x146F240 - 0x146F24D)  -- allocate the
//  CTestFilterFactory component (sub_15091F0) and hand it to the registration
//  stub sub_15090C0, which ignores its argument and always returns 0.  The
//  original pushes the component, calls the stub, discards it (pop ecx) and
//  returns 0 -- the component allocation is not freed (leaked) and never
//  referenced again, exactly as in the original.  [NOT byte-identical]
//  placement-dependent (the relative E8 calls) and refs the original high-VA
//  vftable.
static int RegisterVfsGroup3(void)
{
    void *component = VfsAllocTestFilterComponent();
    (void)component;   // the sub_15090C0 stub ignores its argument
    return 0;
}

//  VfsAllocCodecComponent  -- the shared body of the PCM / Crescendo
//  allocation subs (sub_150B6B0 / sub_15094F0): LHAlloc(4), store the class's
//  vftable; nullptr on allocation failure.
static void *VfsAllocCodecComponent(DWORD vftable)
{
    void *p = LHAlloc(4);
    if (p != 0)
        *(DWORD *)p = vftable;
    return p;
}

//  VfsAllocCodecXwmaComponent  (0x150AFF0)  -- the sub_150AFF0 body: LHAlloc(4),
//  store the XWMA class's vftable; nullptr on allocation failure.  On the first
//  call (the refcount at dword_1DDCE7C == 0 and the flag at byte_1DDCE78 == 0)
//  the XWMA worker callback is registered as well: the sub_150B110 callback
//  pointer is stored at dword_1D80B70 and the static {vtable, type_info} pair
//  at off_1D80B6C is handed to VfsWorkerCallbackRegister(pair, 6); the refcount
//  is then bumped and the flag set.  On a repeat call only the refcount is
//  bumped.
static void *VfsAllocCodecXwmaComponent(void)
{
    void *p = LHAlloc(4);
    if (p == 0)
        return 0;
    *(DWORD *)p = kVfsCodecXwmaVftable;
    if (g_dword_1DDCE7C == 0 && g_byte_1DDCE78 == 0) {
        g_dword_1D80B70 = 0;                          // the sub_150B110 callback pointer (0 -- stubbed)
        VfsWorkerCallbackRegister(g_off_1D80B6C, 6);  // 0x1476D00
        g_dword_1DDCE7C += 1;
        g_byte_1DDCE78 = 1;
    } else {
        g_dword_1DDCE7C += 1;
    }
    return p;
}

//  VfsRegisterCodecComponent  (0x147E880)  -- the thiscall registration helper:
//  build the 20-byte codec registration block (only once -- the lazy init in
//  dword_1DDCCD4) and, for a non-null component, register it into the block.
//  The block: [0] the CBase<ICodec,...> vftable (overwritten with the CMap
//  vftable below), [8] the Intrusive::CBase vftable, [12..20] the two-node
//  intrusive CMap base via CMapBaseCtor (0x150C5A0), [0] overwritten with the
//  Allocated::CMap vftable, and the block stored in dword_1DDCCD4.  [NOT
//  byte-identical] placement-dependent (the relative LHAlloc call and the
//  thiscall registration call) and refs the original high-VA codec vftables.
//  The deep leaves -- the component's vtable[0] virtual dispatch (the original
//  high-VA codec vftable), the sub_150BD30 intrusive-map search (its
//  sub_150C560/sub_150C060 chain) and the sub_147EEF0 node registration
//  (LHAlloc(0x18) + set [node+0x14] = component + the sub_150C060 insert) --
//  are documented no-op stubs in this build, exactly like the deep leaves of
//  RegisterVfsGroup1/2.  This build returns 0 for a null component; the PCM /
//  XWMA / Crescendo results of the first two calls are discarded by the
//  caller, so its observable effect is the 20-byte block + the XWMA
//  callback-registration side effects.
static int VfsRegisterCodecComponent(void *component)
{
    if (g_dword_1DDCCD4 == 0) {
        DWORD block = (DWORD)LHAlloc(20);   // 20-byte codec registration block
        if (block != 0) {
            DWORD *b = (DWORD *)block;
            b[0] = kVfsCodecCBaseVftable;
            b[1] = 0;                              // [+4]: never written in the original; zeroed here
            b[2] = kVfsCodecIntrusiveCBaseVftable;
            CMapBaseCtor((void *)(block + 12));    // 0x150C5A0
            b[0] = kVfsCodecAllocatedCMapVftable;  // overwrite the temp CBase vftable
            g_dword_1DDCCD4 = block;
        } else {
            g_dword_1DDCCD4 = 0;
        }
    }
    (void)g_dword_1DDCCD4;
    if (component == 0)
        return 0;
    // deep leaves stubbed (see the header comment): the PCM/XWMA/Crescendo
    // components are never actually spliced into the map and the component is
    // never registered -- the original returns 1 after the deep leaves.
    return 1;
}

//  RegisterVfsGroup4  (0x146F250 - 0x146F279)  -- allocate the three audio
//  codec components (PCM / XWMA / Crescendo) and register each into the VFS
//  codec registration block through VfsRegisterCodecComponent.  cdecl, no
//  args, returning the last registration's result (the original returns the
//  final sub_147E880's result).  [NOT byte-identical] placement-dependent (the
//  three relative E8 alloc calls and the thiscall registration call) and refs
//  the original high-VA component vftables.
static int RegisterVfsGroup4(void)
{
    int result;
    void *pcm = VfsAllocCodecComponent(kVfsCodecPcmVftable);        // 0x150B6B0
    VfsRegisterCodecComponent(pcm);                                 // 0x147E880
    void *xwma = VfsAllocCodecXwmaComponent();                      // 0x150AFF0
    VfsRegisterCodecComponent(xwma);                                // 0x147E880
    void *crescendo = VfsAllocCodecComponent(kVfsCodecCrescendoVftable);  // 0x15094F0
    result = VfsRegisterCodecComponent(crescendo);                  // 0x147E880
    return result;
}


// CreateVirtualFilesystem  (0x145DD80 - 0x145DECE)  -- see the header above.
static int CreateVirtualFilesystem(void *descIn)
{
    DWORD *desc = (DWORD *)descIn;
    DWORD vfsObj = 0;   // the BYREF slot the original seeds from ECX
    if (g_byte_1DDC7E0 != 0)
        return 0;
    if (g_ppv_1DDC7E8 == 0) {
        CoInitializeEx(NULL, 0);   // COINIT_APARTMENTTHREADED
        CoCreateInstance(g_rclsid_1D6A6F0, NULL, 0x17,
            g_riid_1D6A700, &g_ppv_1DDC7E8);   // dwClsContext 0x17 (original)
        if (g_ppv_1DDC7E8 != 0) {
            void **vt = (void **)(*(void **)g_ppv_1DDC7E8);
            ((void (WINAPI *)(void *, unsigned, unsigned, void *))vt[4])(
                g_ppv_1DDC7E8, 0, 0, g_unk_1DDC7EC);
            ((void (WINAPI *)(void *, void *))vt[6])(
                g_ppv_1DDC7E8, &g_off_1D6A768);
        }
    }
    if (desc[0] != 0) {
        if (desc[1] == 0) desc[1] = 1;
        if (desc[2] >= 0x20u) desc[2] = 31;
        VfsDescAssign(g_dword_1D6A720, desc);
        CreateFallbackVfs(&vfsObj, g_dword_1D6A720[0]);
        if (vfsObj != 0) F3CallVtable((void *)vfsObj, 1);
        if (g_dword_1DDC7F4 != 0) F3CallVtable((void *)g_dword_1DDC7F4, 2);
        g_dword_1DDC7F4 = vfsObj;
        if (vfsObj != 0) F3CallVtable((void *)vfsObj, 2);
        int result = InitVirtualFileSystem(desc);
        if (result != 0) {
            void *mem = LHAlloc(4);
            DWORD obj = (mem != 0) ? (DWORD)VfsRefCountCtor(mem) : 0;
            g_dword_1DDC7E4 = obj;
            RegisterVfsGroup1(g_dword_1DDC7E4);
            RegisterVfsGroup2(g_dword_1DDC7E4);
            RegisterVfsGroup3();
            RegisterVfsGroup4();
            g_byte_1DDC7E0 = 1;
            return 1;
        }
        return result;
    } else {
        SetSaveGamePath(&g_dword_1DDC7F0, "Virtual filesystem can not be null");
        return 0;
    }
}

// ============================================================
//  ManagerListCtor  (0x55B4F0 - 0x55B53A)  -- initialize the manager's
//  head node list (the 144-byte manager object built by CreateHeapManager)
//
//  void *ManagerListCtor(void *obj)   -- usercall: the object comes in ESI
//
//  +0x00 heap node: LHAlloc(4), *node = obj (0 on allocation failure);
//  +0x14 head: LHAlloc(12), head[0] = head[1] = head (self-linked empty
//  list); +0x18 count 0; +0x28 / +0x2C 0. Returns obj.
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc).
static void *ManagerListCtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    void *node = LHAlloc(4);
    if (node != 0)
        *(void **)node = obj;
    o[0x00 / 4] = (DWORD)node;   // the heap node (0 on failure)
    void *head = LHAlloc(12);
    if (head != 0)
        *(void **)head = head;
    if (head != 0)
        *(void **)((char *)head + 4) = head;
    o[0x14 / 4] = (DWORD)head;
    o[0x18 / 4] = 0;
    o[0x28 / 4] = 0;
    o[0x2C / 4] = 0;
    return obj;
}

// SoundByteCtor  (0x145D500 - 0x145D503)  -- the 1-byte sound-system
//  object's null constructor: thiscall, `mov eax, ecx; retn`. Returns this.
static void *SoundByteCtor(void *obj) { return obj; }

// CreateMusicManager  (0x18F93E0 - 0x18F94F7)  -- the 552-byte (0x228)
//  NSound music manager ctor (usercall EDI): +0x1C count 7, the 72-byte
//  7-entry controller table (sub_401620), the CFMVMusicController /
//  CGuideMusicController slots, and the five 116-byte music controllers.
//  Remaining stub: the NSound controller objects are not implemented in
//  this build, so the record is zeroed and obj is returned (the original
//  returns the ctor result stored at +0x20 by CreateHeapManager).
static void *CreateMusicManager(void *obj)
{
    memset(obj, 0, 0x228);
    return obj;
}

// Vec4Set  (0x415350 - 0x41537C)  -- store one SSE vector: the SSE thiscall
//  `*this = {x, y, z, 0.0f}` for the 16-byte vector slot in ECX.
static void Vec4Set(void *out, float x, float y, float z)
{
    float *v = (float *)out;
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 0.0f;
}

// QuerySoundSystem  (0x145DD40 - 0x145DD54)  -- the sound-system query
//  (calls sub_146EAA0 on the sound object, returns the object back). The
//  audio subsystem is a remaining stub in this build, so it reports no
//  sound object (0).
static void *QuerySoundSystem(void *soundObj, void **out)
{
    (void)soundObj;
    if (out) *out = 0;   // no sound slot while the audio subsystem is stubbed
    return 0;
}

// ============================================================
//  CSoundPlayerCtor  (0x18FA020 - 0x18FA0EE)  -- the 80-byte
//  NSound::CSoundPlayer constructor (usercall ESI, one &vec4 on the stack,
//  `retn 4`)
//
//  +0x00 the NSound::CSoundPlayer vtable (0 until NSound lands), +0x10..
//  +0x1C the four vec4 words, +0x20..+0x2C 0, +0x30 a 4-byte heap node
//  (*node = +0x30), +0x34 a 24-byte self-linked head, +0x38 0. The +0x20/
//  +0x24 sound slot pair is then (re)bound from QuerySoundSystem on the
//  manager's +0x1C object; while that is stubbed to 0 the pair stays 0
//  (the original would bind the live sound slot here).
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc /
//  QuerySoundSystem) and the NSound::CSoundPlayer vtable refs high-VA data.
static void *CSoundPlayerCtor(void *obj, const float *vec)
{
    DWORD *o = (DWORD *)obj;
    o[0x00 / 4] = 0;   // &NSound::CSoundPlayer::`vftable' (0 until NSound lands)
    o[0x20 / 4] = 0;
    o[0x24 / 4] = 0;
    o[0x28 / 4] = 0;
    o[0x2C / 4] = 0;
    void *node = LHAlloc(4);
    if (node != 0)
        *(void **)node = (char *)obj + 0x30;
    o[0x30 / 4] = (DWORD)node;
    void *head = LHAlloc(24);
    if (head != 0)
        *(void **)head = head;
    if (head != 0)
        *(void **)((char *)head + 4) = head;
    o[0x44 / 4] = (DWORD)head;
    o[0x48 / 4] = 0;
    o[0x10 / 4] = (DWORD)vec[0];
    o[0x14 / 4] = (DWORD)vec[1];
    o[0x18 / 4] = (DWORD)vec[2];
    o[0x1C / 4] = (DWORD)vec[3];
    void *slot = 0;
    void *r = QuerySoundSystem(*(void **)((char *)g_dword_2075ED8 + 0x1C), &slot);
    if (r != 0 && r != (char *)obj + 0x20)
    {
        void *r1 = *(void **)((char *)r + 4);
        if (r1 != 0)
            F3CallVtable(r1, 0);                          // release the previous slot object
        if (o[0x24 / 4] != 0)
            F3CallVtable((void *)o[0x24 / 4], 1);
        o[0x20 / 4] = (DWORD)*(void **)r;
        o[0x24 / 4] = (DWORD)*(void **)((char *)r + 4);
        if (slot != 0)
            F3CallVtable(slot, 1);
    }
    return obj;
}

// ============================================================
//  RegisterManagerPlayer  (0x18FB9B0 - 0x18FBA1F)  -- build the 80-byte
//  NSound::CSoundPlayer for a vec4 and register it at the head of the
//  manager's 12-byte-node list (thiscall, one &vec4 on the stack)
//
//  void *RegisterManagerPlayer(void *mgr, const float *vec)
//
//  Under the manager critical section (stru_20FC8AC): allocate the player
//  (CSoundPlayerCtor), allocate a 12-byte node {head, old first, player},
//  increment the list count at mgr+0x18 (sub_55BC70, "list<T> too long" at
//  0x3FFFFFFF), and link the node in front of the old first (head[1] =
//  node, oldFirst[0] = node). Returns the new first node's value -- the
//  player (the original's sub_55B6D0 returns &firstNode[2], whose word is
//  the value; a null / empty list would trip __invalid_parameter_noinfo).
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc /
//  CSoundPlayerCtor / the KERNEL32 IAT) and refs the high-VA critical
//  section stru_20FC8AC (0x020FC8AC).
static void *RegisterManagerPlayer(void *mgr, const float *vec)
{
    EnterCriticalSection(MgrCs());
    void *player = 0;
    void *mem = LHAlloc(80);
    if (mem != 0)
        player = CSoundPlayerCtor(mem, vec);
    DWORD *head = *(DWORD **)((char *)mgr + 0x14);
    DWORD oldFirst = head[1];
    DWORD *node = (DWORD *)LHAlloc(12);
    if (node != 0)
    {
        node[0] = (DWORD)head;
        node[1] = oldFirst;
        node[2] = (DWORD)player;
    }
    unsigned *count = (unsigned *)((char *)mgr + 0x18);   // sub_55BC70
    if (0x3FFFFFFFu - *count < 1u)
        throw std::length_error("list<T> too long");
    *count += 1u;
    head[1] = (DWORD)node;
    if (oldFirst != 0)
        *(DWORD *)oldFirst = (DWORD)node;
    LeaveCriticalSection(MgrCs());
    return player;   // *(firstNode + 8)
}

// ============================================================
//  InitManagerFields  (0x18FBBA0 - 0x18FBCF5)  -- the 144-byte manager's
//  field initializer (reads the manager global, no arguments in the original)
//
//  void InitManagerFields(void)   -- reads g_dword_2075ED8 into ESI (ESI
//  survives into the callees in the original, i.e. it is the implicit this)
//
//  +0x1C: the 1-byte sound-system object (LHAlloc(1) + SoundByteCtor, 0 on
//  failure); +0x20: the 552-byte music manager (LHAlloc(0x228) +
//  CreateMusicManager, 0 on failure); +0x24: the vec4 {0,0,0,0} player
//  (RegisterManagerPlayer); +0x2C: the previous slot object released
//  (vtable slot 1) then 0; +0x28: 0; +0x30 the {0,1,0,0} axis, +0x40 the
//  {0,0,1,0} axis, +0x50 the {0,0,0,0} vec, +0x60..+0x7C the 1.0f scalars,
//  +0x78 / +0x79 0 bytes, +0x80 2, +0x84 / +0x88 0. The +0x6C level is left
//  1.0f here -- SetManagerLevel (sub_18FAA00) writes 0.9f right after.
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc and
//  the manager callees) and refs the high-VA global g_dword_2075ED8
//  (0x02075ED8) + the high-VA critical section stru_20FC8AC.
static void InitManagerFields(void)
{
    void *g = g_dword_2075ED8;
    void *mem = LHAlloc(1);
    void *sound = (mem != 0) ? SoundByteCtor(mem) : 0;
    *(void **)((char *)g + 0x1C) = sound;
    void *m2 = LHAlloc(0x228);
    void *music = (m2 != 0) ? CreateMusicManager(m2) : 0;
    *(void **)((char *)g + 0x20) = music;
    float v0[4];
    Vec4Set(v0, 0.0f, 0.0f, 0.0f);
    *(void **)((char *)g + 0x24) = RegisterManagerPlayer(g, v0);
    void *old2C = *(void **)((char *)g + 0x2C);
    if (old2C != 0)
        F3CallVtable(old2C, 1);
    *(void **)((char *)g + 0x2C) = 0;
    *(void **)((char *)g + 0x28) = 0;
    float va[4], vb[4], vc[4];
    Vec4Set(va, 0.0f, 1.0f, 0.0f);
    memcpy((char *)g + 0x30, va, 16);
    Vec4Set(vb, 0.0f, 0.0f, 1.0f);
    memcpy((char *)g + 0x40, vb, 16);
    Vec4Set(vc, 0.0f, 0.0f, 0.0f);
    memcpy((char *)g + 0x50, vc, 16);
    *(float *)((char *)g + 0x60) = 1.0f;
    *(float *)((char *)g + 0x64) = 1.0f;
    *(float *)((char *)g + 0x68) = 1.0f;
    *(float *)((char *)g + 0x6C) = 1.0f;
    *(float *)((char *)g + 0x70) = 1.0f;
    *(float *)((char *)g + 0x74) = 1.0f;
    *(float *)((char *)g + 0x7C) = 1.0f;
    *(BYTE *)((char *)g + 0x78) = 0;
    *(BYTE *)((char *)g + 0x79) = 0;
    *(DWORD *)((char *)g + 0x80) = 2;
    *(DWORD *)((char *)g + 0x84) = 0;
    *(DWORD *)((char *)g + 0x88) = 0;
}

// SetFadeRate  (0x469ED0 - 0x469EFC)  -- audio fade request (forwards to
//  sub_40DFC0). The audio subsystem is a remaining stub in this build.
static int SetFadeRate(float, float) { return 0; }

// SoundSetChannelLevel  (0x145D810 - 0x145D828)  -- audio channel level
//  setter (forwards to sub_14610F0). Remaining stub in this build.
static int SoundSetChannelLevel(int, float, int) { return 0; }

// ============================================================
//  ApplyManagerFadeRates  (0x18FA8C0 - 0x18FA996)  -- the manager's
//  fade-rate recompute (usercall: the manager in ESI)
//
//  void ApplyManagerFadeRates(void *obj)
//
//  Under the manager critical section (stru_20FC8AC): for the two level
//  scalars at +0x70 and +0x6C, rate = (1.0 - level) * -60.0 / 20.0 (double
//  arithmetic, float rate argument) -> SetFadeRate(10.0f, rate); then
//  SoundSetChannelLevel(2, 100.0f, 0) (the trivial sub_145D7C0 / nullsub_124
//  stack-byte touchups are side-effect free and omitted). [NOT
//  byte-identical] placement-dependent (relative calls to the audio stubs /
//  the KERNEL32 IAT) and refs the high-VA critical section stru_20FC8AC.
static void ApplyManagerFadeRates(void *obj)
{
    EnterCriticalSection(MgrCs());
    double r1 = (1.0 - (double)*(float *)((char *)obj + 0x70)) * -60.0;
    SetFadeRate(10.0f, (float)(r1 / 20.0));
    double r2 = (1.0 - (double)*(float *)((char *)obj + 0x6C)) * -60.0;
    SetFadeRate(10.0f, (float)(r2 / 20.0));
    SoundSetChannelLevel(2, 100.0f, 0);
    LeaveCriticalSection(MgrCs());
}

// ============================================================
//  SetManagerLevel  (0x18FAA00 - 0x18FAA59)  -- clamp + store the manager's
//  level scalar (thiscall + `retn 4` in the original: the manager in ECX,
//  one float on the stack)
//
//  void SetManagerLevel(void *obj, float level)
//
//  Under the manager critical section (stru_20FC8AC): clamp level to
//  [0.0f, 1.0f] (NaN passes the comparisons through unchanged), store it
//  at +0x6C, and recompute the fade rates (ApplyManagerFadeRates). [NOT
//  byte-identical] placement-dependent (relative calls to the KERNEL32 IAT /
//  ApplyManagerFadeRates) and refs the high-VA critical section stru_20FC8AC.
static void SetManagerLevel(void *obj, float level)
{
    EnterCriticalSection(MgrCs());
    if (level < 0.0f)
        level = 0.0f;
    else if (level > 1.0f)
        level = 1.0f;
    *(float *)((char *)obj + 0x6C) = level;
    ApplyManagerFadeRates(obj);
    LeaveCriticalSection(MgrCs());
}

// ============================================================
//  CreateHeapManager  (0x18FBD00 - 0x18FBE09)  -- (re)build the global 144-
//  byte manager object (g_dword_2075ED8, GetHeapManager) and re-create the
//  virtual filesystem
//
//  int CreateHeapManager(int flag)   -- cdecl, one byte on the stack
//
//  1.  Build the 80-byte VFS descriptor on the stack (VfsDescDefaultCtor,
//      sub_145DC20) with the overrides +0x08 = 5, +0x14 = the descriptor
//      vtable (unk_2000000), +0x24 = 0x38000, +0x2C = 8, +0x38 = 2, +0x3C
//      = the flag byte, +0x44 = 1, +0x48 = 0.
//  2.  Take the archive manager out of its slot (GetArchiveSlot, sub_180AC90
//      -- the dword_1E2D864 slot with a vtable-slot-1 reference), close it
//      (vtable slot 1), stash it in the descriptor's +0x00 object slot,
//      and drop the temporary reference (vtable slot 2).
//  3.  Create the virtual filesystem for the descriptor
//      (CreateVirtualFilesystem, sub_145DD80).
//  4.  LHAlloc(144) the manager and initialize its node list
//      (ManagerListCtor, sub_55B4F0) -> g_dword_2075ED8 (0 on failure).
//  5.  Initialize the manager's fields (InitManagerFields, sub_18FBBA0).
//  6.  Set the level scalar to 0.9f (SetManagerLevel, sub_18FAA00 -- the
//      original loads flt_1BD7E48, 0x01BD7E48).
//  Returns the vtable-slot-2 release result of the descriptor's +0x00 slot
//  (sub_145DC90; 0 while the archive slot is empty). Both callers
//  (RunFrontEnd and the 0x619AE0 vtable thunk that passes g_byte_1DBD030
//  != 0) discard the return value.
//  [NOT byte-identical] placement-dependent (relative calls to the manager
//  / VFS callees and the LHAlloc IAT thunk) and refs the high-VA globals
//  g_dword_2075ED8 (0x02075ED8) and flt_1BD7E48 (0x01BD7E48).
static int CreateHeapManager(int flag)
{
    DWORD d[20];   // the 80-byte VFS descriptor
    VfsDescDefaultCtor(d);            // sub_145DC20
    d[0x08 / 4] = 5;
    d[0x14 / 4] = 0x2000000;          // &unk_2000000: the descriptor vtable
    d[0x2C / 4] = 8;
    d[0x24 / 4] = 0x38000;
    d[0x38 / 4] = 2;
    if (flag & 1)
        d[0x3C / 4] = 1;   // [esp+58h+var_14] = 1: DWORD at descriptor +0x3C
    DWORD old = 0;
    GetArchiveSlot((F3SharedBuffer **)&old);   // sub_180AC90: the archive manager slot
    if (old != 0)
        F3CallVtable((void *)old, 1);                       // vtable slot 1 (close)
    if (d[0x00 / 4] != 0)
        F3CallVtable((void *)d[0x00 / 4], 2);
    d[0x00 / 4] = old;
    if (old != 0)
        F3CallVtable((void *)old, 2);                       // vtable slot 2 (release)
    d[0x44 / 4] = 1;
    d[0x48 / 4] = 0;
    CreateVirtualFilesystem(d);       // sub_145DD80
    void *mem = LHAlloc(144);
    g_dword_2075ED8 = (mem != 0) ? ManagerListCtor(mem) : 0;   // sub_55B4F0
    InitManagerFields();              // sub_18FBBA0
    SetManagerLevel(g_dword_2075ED8, 0.9f);   // sub_18FAA00 (flt_1BD7E48 = 0.9f)
    int r = 0;
    if (d[0x00 / 4] != 0)             // sub_145DC90: release the +0x00 slot
        r = F3CallVtableRet((void *)d[0x00 / 4], 2);
    return r;
}
static void  nullsub_7(void) {}
// ============================================================
//  CGameEngineClearNodeList  (0x620F30 - 0x620F65)  -- clear the
//  CGameEngine's main node list
//  [NOT byte-identical] placement-dependent (relative E8 call to LHFree).
//
//  void *CGameEngineClearNodeList(void *obj)   -- thiscall: obj in ECX
//
//  Resets the engine object's main node list (head block at +0x14, node
//  count at +0x18) to empty: captures the first node, re-links the
//  sentinel (head.next = head.prev = head), zeroes the count, then
//  LHFrees every list node in the cycle (the sentinel itself is kept).
//  Returns the sentinel (the caller discards it).
// ============================================================
static void *CGameEngineClearNodeList(void *obj)
{
    char *base = (char *)obj;
    void *head = *(void **)(base + 0x14);
    void *node = *(void **)head;
    *(void **)head               = head;
    *(void **)((char *)head + 4) = head;
    *(void **)(base + 0x18) = 0;
    while (node != head)
    {
        void *next = *(void **)node;
        LHFree(node);
        node = next;
    }
    return head;
}
// ============================================================
//  SortedMapHeadInit  (0x579280 - 0x5792C4)  -- initialize the embedded
//  sorted node list of a sorted-map object (head node at obj+0x18, node
//  count at obj+0x1C)
//
//  void *SortedMapHeadInit(void)   -- usercall: the object comes in ESI
//
//  Allocates the 24-byte sentinel head node from the game heap (LHAlloc),
//  zeroes its first three dwords, marks its +0x14 / +0x15 flag bytes 1,
//  stores it at obj+0x18, self-links next / prev / right, and zeroes the
//  node count at obj+0x1C. Returns the head node.
//
//  [NOT byte-identical] placement-dependent: the body is the original's
//  exact instruction sequence (__declspec(naked) inline asm, the object in
//  ESI), but the relative E8 call to LHAlloc displaces to a different
//  offset in this build's layout than in the original.
//
//  Shared list initializer for the 0x38-byte sorted map (CSortedMapCtor,
//  0x18EADB0) and the CS-less 0x28-byte variants (0x577BA0 / 0x5785D0);
//  the same sequence is inlined into sub_1979610.
// ============================================================
static __declspec(naked) void *SortedMapHeadInit(void)
{
    __asm {
        push 18h
        call LHAlloc
        xor edx, edx
        add esp, 4
        cmp eax, edx
        jz short f3_smhi_skip1
        mov [eax], edx
    f3_smhi_skip1:
        lea ecx, [eax+4]
        cmp ecx, edx
        jz short f3_smhi_skip2
        mov [ecx], edx
    f3_smhi_skip2:
        lea ecx, [eax+8]
        cmp ecx, edx
        jz short f3_smhi_skip3
        mov [ecx], edx
    f3_smhi_skip3:
        mov cl, 1
        mov [eax+14h], cl
        mov [esi+18h], eax
        mov [eax+15h], cl
        mov eax, [esi+18h]
        mov [eax+4], eax
        mov eax, [esi+18h]
        mov [eax], eax
        mov eax, [esi+18h]
        mov [eax+8], eax
        mov [esi+1Ch], edx
        ret
    }
}

// ============================================================
//  CSortedMapCtor  (0x18EADB0 - 0x18EADD C)  -- the 0x38-byte CS-guarded
//  sorted-map object's constructor (game-engine member 0x98)
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc and
//  SortedMapHeadInit) and refs the InitializeCriticalSection IAT slot
//  (ds:__imp_InitializeCriticalSection) at its original high VA
//  0x01AD531C.
//
//  void *CSortedMapCtor(void *obj)   -- thiscall, returns obj
//
//  The object is a critical-section-guarded sorted map: a red-black tree
//  held as a circular doubly-linked list of 24-byte nodes ({next +0, prev
//  +4, right +8, key +0xC, refcounted object +0x10, flags +0x14/+0x15}),
//  keyed on the node's 32-bit key; the binary search walks the in-order
//  next/prev links and right pointers (sub_5780D0 / sub_577C10 family).
//  Layout (56 bytes): +0x00 a 4-byte heap owner tag (LHAlloc(4) block
//  holding a back-pointer to the object; NULL on allocation failure),
//  +0x04..+0x17 class data not initialized here, +0x18 the sentinel head
//  node, +0x1C the node count, +0x20..+0x37 the CRITICAL_SECTION.
//  Allocates the owner tag, initializes the list head / count
//  (SortedMapHeadInit), initializes the critical section at +0x20,
//  and returns obj. Sole caller: RunFrontEnd (LHAlloc(0x38) + this),
//  stored into engine member 0x98 (released by the CSortedMapDtor
//  (0x18EB1A0) + LHFree in RunFrontEnd / CGameEngineDestroy).
// ============================================================
static void *CSortedMapCtor(void *obj)
{
    void *tag = LHAlloc(4);
    if (tag != 0)
        *(void **)tag = obj;
    *(void **)obj = tag;
    __asm {
        mov esi, obj
        call SortedMapHeadInit
    }
    InitializeCriticalSection((CRITICAL_SECTION *)((char *)obj + 0x20));
    return obj;
}
// The original's sorted-map debug-iterator checks call the CRT's
// __invalid_parameter_noinfo(), which reports to the (unset) invalid-parameter
// handler and returns normally, so the original keeps executing after each
// check. A no-op with the same return path is functionally equivalent here
// (the VS2008 msvcrt import library does not carry the symbol).
void F3InvalidParam(void) {}

// ============================================================
//  SortedMapNodeTreeDestroy  (0x578660 - 0x5786EE)  -- recursive in-order
//  destroy of the sorted map's red-black tree
//  [NOT byte-identical] placement-dependent (the relative recursive call)
//  and refs the heap table (dword_2075B24) at its original high VA (the node
//  frees go through its +16 slot, i.e. LHFree).
//
//  void SortedMapNodeTreeDestroy(void *obj, void *node)
//
//  Original: thiscall + `retn 4` (obj in ECX, the node on the stack). Destroys
//  the 24-byte tree nodes starting at `node` in in-order: recurse into the
//  node's right subtree (+8), release the node's refcounted entry object
//  (+0x10: InterlockedDecrement its refcount at +4, call the object's vtable
//  slot 1 at zero, then null the slot), and free the node through the heap
//  table; then advance to the node's in-order successor (+0) and repeat.
//  Stops (without freeing) at the sentinel -- the flag byte at +0x15 is set
//  on the sentinel node, which the caller frees separately.
// ============================================================
static void SortedMapNodeTreeDestroy(void *obj, void *node)
{
    for (;;)
    {
        char *n = (char *)node;
        if (*(BYTE *)(n + 0x15) != 0)      // the sentinel
            break;

        SortedMapNodeTreeDestroy(obj, *(void **)(n + 8));    // the right subtree

        void *entry = *(void **)(n + 0x10);    // captured before the node free
        void *next  = *(void **)n;
        if (entry != 0)
        {
            if (InterlockedDecrement((LONG *)entry + 1) == 0)
                ((void (*)(void *))((void **)*(void **)entry)[1])(entry);
            *(void **)(n + 0x10) = 0;
        }
        LHFree(node);
        node = next;
    }
}

// ============================================================
//  SortedMapIterFixup  (0x579A70 - 0x579AD1)  -- debug-iterator fixup for the
//  sorted map's {tag, node} iterator pair
//  [NOT byte-identical] placement-dependent (the relative call/jmp to the
//  __invalid_parameter_noinfo IAT slot).
//
//  void SortedMapIterFixup(void *pair)   -- original: usercall (pair in ESI,
//                                            plain `retn`)
//
//  `pair` is an adjacent {tag, node} DWORD pair (the original passes the
//  address of the caller's stack slots). The tag must be non-zero and the
//  node must not be the sentinel (flag byte +0x15) -- the original calls
//  __invalid_parameter_noinfo on failure (the tag check returns; the
//  sentinel-node check is a `jmp`, so it never returns). Otherwise the
//  pair's node is re-pointed at a tree-adjacent node: when the node's right
//  child (+8) is real, the node becomes the last non-sentinel node of the
//  in-order successor chain starting at the right child; when the right
//  child is the sentinel, the node walks the prev chain (+4) up as long as
//  each ancestor's right child (+8) is the previous node, and becomes the
//  first ancestor that fails the test (or the sentinel).
// ============================================================
static void SortedMapIterFixup(void *pair)
{
    char *pr = (char *)pair;
    if (*(void **)pr == 0)
        F3InvalidParam();
    void *node = *(void **)(pr + 4);
    if (*(BYTE *)((char *)node + 0x15) != 0)
    {
        F3InvalidParam();
        return;        // original: jmp __invalid_parameter_noinfo (no return)
    }

    void *right = *(void **)((char *)node + 8);
    if (*(BYTE *)((char *)right + 0x15) == 0)
    {
        void *cur = right;
        void *nxt = *(void **)cur;
        while (*(BYTE *)((char *)nxt + 0x15) == 0)
        {
            cur = nxt;
            nxt = *(void **)cur;
        }
        *(void **)(pr + 4) = cur;
    }
    else
    {
        void *anc = *(void **)((char *)node + 4);
        for (;;)
        {
            if (*(BYTE *)((char *)anc + 0x15) != 0)
                break;
            if (*(void **)(pr + 4) != *(void **)((char *)anc + 8))
                break;
            *(void **)(pr + 4) = anc;
            anc = *(void **)((char *)anc + 4);
        }
        *(void **)(pr + 4) = anc;
    }
}

// sub_577D00 (0x577D00) -- the 918-byte single-node red-black erase from the
// sorted map's tree: rebalances through the sub_578990 / sub_578940 /
// sub_5789F0 rotation family, releases the node's refcounted entry object
// (+0x10), frees the node through the heap table, decrements the map's node
// count (+0x1C) when non-zero, and writes the {tag, node} out-iterator pair.
// Remaining stub: it is only reached from SortedMapEraseRange's non-empty
// erase loop, which this build never runs (the map is constructed empty and
// nothing is ever inserted into it).
static void SortedMapNodeErase(void *obj, void *out, void *tag, void *node)
{ (void)obj; (void)out; (void)tag; (void)node; }

// ============================================================
//  SortedMapEraseRange  (0x579190 - 0x579275)  -- erase the sorted map's
//  in-order node range [node1, node3)
//  [NOT byte-identical] placement-dependent (the relative calls to
//  SortedMapNodeTreeDestroy / SortedMapIterFixup / SortedMapNodeErase and
//  the __invalid_parameter_noinfo checks).
//
//  void *SortedMapEraseRange(void *obj, void *out, void *tag0, void *node1,
//                            void *tag2, void *node3)
//
//  Original: thiscall + `retn 0x10` (obj in ECX, the 8-byte {tag, node}
//  out-iterator scratch in EDX, the two debug-iterator bounds tag0/node1 and
//  tag2/node3 on the stack). The bounds must match the map's owner tag at
//  +0x00 (non-zero bounds must be equal to it; __invalid_parameter_noinfo
//  otherwise) and node1 must be the map's first node ([head]). When the
//  range is empty (node3 is the sentinel head itself) it takes the direct
//  path: SortedMapNodeTreeDestroy from the sentinel's predecessor, reset the
//  list to empty (self-linked head, count 0), and write {tag, head} to the
//  out scratch. Otherwise it loops: fix up the {tag0, node1} iterator pair
//  (the original passes the two adjacent stack slots) and erase one node
//  (SortedMapNodeErase into a second 8-byte local scratch), until node1
//  reaches node3, then writes {tag, node1} to the out scratch. Returns the
//  out scratch (the caller discards it).
// ============================================================
static void *SortedMapEraseRange(void *obj, void *out, void *tag0, void *node1,
                                 void *tag2, void *node3)
{
    char *m = (char *)obj;
    void *tag   = *(void **)m;
    void *head  = *(void **)(m + 0x18);
    void *first = *(void **)head;

    if (tag0 != 0 && tag0 != tag)
        F3InvalidParam();
    if (node1 == first && tag2 != 0 && tag2 != tag)
        F3InvalidParam();
    if (node1 == first && node3 == first)
    {
        // Direct path (empty range): destroy from the sentinel's
        // predecessor and reset the list to empty.
        SortedMapNodeTreeDestroy(obj, *(void **)((char *)head + 4));
        head = *(void **)(m + 0x18);
        *(void **)((char *)head + 4) = head;
        *(int *)(m + 0x1C) = 0;
        *(void **)head = head;
        *(void **)((char *)head + 8) = head;
        *(void **)out = 0;
        ((void **)out)[1] = *(void **)head;
        *(void **)out = tag;
        return out;
    }

    DWORD next[2] = { 0, 0 };      // the original's v15 out-iterator scratch
    for (;;)
    {
        if (tag0 != 0 && tag0 != tag2)
            F3InvalidParam();
        if (node1 == node3)
            break;
        DWORD iter[2] = { (DWORD)tag0, (DWORD)node1 };   // the original's adjacent {tag0, node1} pair
        SortedMapIterFixup((void *)iter);
        tag0  = (void *)iter[0];
        node1 = (void *)iter[1];
        SortedMapNodeErase(obj, (void *)next, tag0, node1);
    }
    ((void **)out)[0] = tag;
    ((void **)out)[1] = node1;
    return out;
}

// ============================================================
//  SortedMapTreeDestroy  (0x578A40 - 0x578A7D)  -- destroy the sorted map's
//  whole tree (all nodes + the sentinel)
//  [NOT byte-identical] placement-dependent (the relative call to
//  SortedMapEraseRange) and refs the heap table (dword_2075B24) at its
//  original high VA (the sentinel free goes through its +16 slot, i.e. LHFree).
//
//  void SortedMapTreeDestroy(void *obj)   -- original: usercall (obj in ESI,
//                                            plain `retn`)
//
//  Erases the whole in-order range (first node .. sentinel) through
//  SortedMapEraseRange (passing an 8-byte {tag, node} stack scratch as the
//  out iterator, which is discarded), then frees the 24-byte sentinel head
//  node and zeroes the map's head (+0x18) and node count (+0x1C).
// ============================================================
static void SortedMapTreeDestroy(void *obj)
{
    char *m = (char *)obj;
    void *head = *(void **)(m + 0x18);
    void *tag  = *(void **)m;
    DWORD out[2] = { 0, 0 };        // the original's 8-byte EDX stack scratch

    SortedMapEraseRange(obj, (void *)out, tag, *(void **)head, tag, head);

    LHFree(*(void **)(m + 0x18));
    *(void **)(m + 0x18) = 0;
    *(int *)(m + 0x1C) = 0;
}

// ============================================================
//  CSortedMapDtor  (0x18EB1A0 - 0x18EB1C5)  -- the 0x38-byte CS-guarded
//  sorted map object's destructor (the counterpart of CSortedMapCtor)
//  [NOT byte-identical] placement-dependent (the relative call to
//  SortedMapTreeDestroy) and refs the DeleteCriticalSection IAT slot
//  (ds:__imp_DeleteCriticalSection, 0x01AD52E8) and the heap table
//  (dword_2075B24) at their original high VAs.
//
//  void CSortedMapDtor(void *obj)   -- original: thiscall (obj in ECX, plain
//                                      `retn`; the free result is in EAX)
//
//  Deletes the critical section at +0x20, destroys the tree's nodes and
//  sentinel (SortedMapTreeDestroy, 0x578A40), and frees the 4-byte heap
//  owner tag at +0x00 through the heap table's free slot (the original
//  passes the tag as both the this register and the stack argument; the
//  free ignores this, so LHFree is equivalent). The 0x38-byte object block
//  itself is freed by the caller (RunFrontEnd / CGameEngineDestroy each
//  LHFree engine member 0x98 after this).
// ============================================================
void CSortedMapDtor(void *obj)
{
    char *m = (char *)obj;
    DeleteCriticalSection((CRITICAL_SECTION *)(m + 0x20));
    SortedMapTreeDestroy(obj);
    LHFree(*(void **)m);
}

// Placeholder vtables for the frontend's stub-constructed engine members.
// The original constructors store real vtables (NUI::CLoadingScreen, 0x7330F0;
// CHeroUniqueID, 0xC1E6A0; CGameRenderer, 0x72FB30; NUI::CFullScreenMoviePlayer,
// 0x731B80; 0x767360: CPackageManager); those tables are unimplemented in this
// build, but slot 0 (the `vt[0](obj, 1)` release dispatch used by RunFrontEnd's
// old-value frees and by CGameEngineDestroy) must be a callable no-op so the
// teardown does not dispatch through unconstructed memory.
static void F3StubRelease(void *obj, int freeIt) { (void)obj; (void)freeIt; }
static void *g_CLoadingScreenVtable[2]         = { (void *)F3StubRelease, 0 };
static void *g_CGameRendererVtable[2]          = { (void *)F3StubRelease, 0 };
static void *g_CFullScreenMoviePlayerVtable[2] = { (void *)F3StubRelease, 0 };
static void *g_CPackageManagerVtable[2]        = { (void *)F3StubRelease, 0 };
static void *g_CHeroUniqueIDVtable[2]         = { (void *)F3StubRelease, 0 };

// 0x18984D0 -- the NTools::CBaseClassNonCopyable base ctor (also implemented
// as a static in gamethread.cpp): store the base vtable, return obj.
static void *g_NToolsCBaseClassNonCopyableVtable[1] = { 0 };
static void *F3BaseClassNonCopyableCtor(void *obj)
{ *(void **)obj = g_NToolsCBaseClassNonCopyableVtable; return obj; }

// ============================================================
//  F3ListStructCtor  (sub_76C680 / sub_76C700 / sub_76C780,
//  0x76C680 - 0x76C7F2)  -- the 28-byte { node, head, size } struct ctor
//  [NOT byte-identical] relative calls to LHAlloc.
//
//  void F3ListStructCtor(void *obj, unsigned headSize, unsigned flagOffset)
//
//  The three originals are identical except for the head-block size (28 / 24
//  / 40) and the byte offset of the flag pair (0x18 / 0x14 / 0x20), so one
//  parameterized function covers them. Original calling convention: thiscall
//  + two unused stack args, callee-cleanup (retn 8). It builds:
//    +0x00 node   LHAlloc(4); *node = obj (0 on allocation failure)
//    +0x18 head   LHAlloc(headSize); head[0] = head[1] = head[2] = head
//                 (self-referencing); the original first stores 0 at
//                 byte flagOffset+1, stores 1 at byte flagOffset, then
//                 re-stores 1 at byte flagOffset (so both bytes end 1)
//    +0x1C size   0
//  The 20 bytes between the node and the head, and the rest of the head
//  block (bytes 12..flagOffset-1 and flagOffset+2..headSize-1), are left
//  as the heap returned them. The original crashes on a failed head
//  allocation (unguarded writes through a null head); the guards above
//  keep the node/head stores faithful while the unguarded byte writes
//  mirror the original.
// ============================================================
static void F3ListStructCtor(void *obj, unsigned headSize, unsigned flagOffset)
{
    DWORD *m = (DWORD *)obj;
    void *node = LHAlloc(4);
    if (node != 0)
        *(void **)node = obj;
    else
        node = 0;
    m[0] = (DWORD)node;
    DWORD *head = (DWORD *)LHAlloc(headSize);
    if (head != 0)
        head[0] = 0;
    if (head != (DWORD *)-1)
        head[1] = 0;
    if (head != (DWORD *)-2)
        head[2] = 0;
    ((BYTE *)head)[flagOffset + 1] = 0;
    ((BYTE *)head)[flagOffset] = 1;
    m[6] = (DWORD)head;
    ((BYTE *)head)[flagOffset + 1] = 1;   // the original's [eax+19h] = 1
    head[1] = (DWORD)head;
    head[0] = (DWORD)head;
    head[2] = (DWORD)head;
    m[7] = 0;
}

// ============================================================
//  CPackageDataStateReset  (sub_764930, 0x764930 - 0x7649A4)  -- reset the
//  package-data subobject's handle / range fields
//  [NOT byte-identical] relative calls to CloseHandle / _memmove_s (the
//  memmove branch is dead code: its size always computes to 0).
//
//  int CPackageDataStateReset(void *obj)
//
//  Thiscall (plain retn), returns 0. Zeros +0x78; if +0x7C is not
//  INVALID_HANDLE_VALUE, CloseHandle it and set +0x7C = -1; zeros +0x80..
//  +0x9B (24 bytes + the +0x98 word); if +0x70 != +0x6C, stores +0x6C into
//  +0x70 (the original does this through a memmove_s whose size computes to
//  0, i.e. just the word store); finally zeros +0x60.
// ============================================================
static int CPackageDataStateReset(void *obj)
{
    DWORD *m = (DWORD *)obj;
    DWORD h = m[0x7C / 4];
    m[0x78 / 4] = 0;
    if (h != 0xFFFFFFFF)
    {
        CloseHandle((HANDLE)h);
        m[0x7C / 4] = 0xFFFFFFFF;
    }
    *(void **)((char *)obj + 0x80) = 0;
    *(void **)((char *)obj + 0x88) = 0;
    *(void **)((char *)obj + 0x90) = 0;
    m[0x98 / 4] = 0;
    if (m[0x70 / 4] != m[0x6C / 4])
        m[0x70 / 4] = m[0x6C / 4];
    m[0x60 / 4] = 0;
    return 0;
}

// ============================================================
//  CPackageDataCtor  (sub_766430, 0x766430 - 0x7664EE)  -- the 188-byte
//  (0xBC) package-data subobject ctor
//  [NOT byte-identical] relative calls to LHAlloc / F3ListStructCtor /
//  CPackageDataStateReset.
//
//  void *CPackageDataCtor(void *obj)
//
//  Thiscall (plain retn), returns obj. It only writes the words below;
//  the other words (the struct middles at +0x08..+0x1F / +0x44..+0x5F /
//  +0xA0..+0xBB, +0x28..+0x37, +0x00) are left as the heap returned them:
//    +0x04 struct A  F3ListStructCtor (28-byte head)
//    +0x24 node      LHAlloc(4), *node = obj+0x24 (0 on failure)
//    +0x38 head      LHAlloc(40), head[0] = head[1] = head (self)
//    +0x3C 0
//    +0x40 struct B  F3ListStructCtor (24-byte head)
//    +0x60 +0x6C +0x70 +0x74 +0x78  0
//    +0x7C 0xFFFFFFFF (INVALID_HANDLE_VALUE)
//    +0x80..+0x97  0 (24 bytes)
//    +0x9C struct C  F3ListStructCtor (40-byte head)
//    +0x98 0
//  then CPackageDataStateReset(obj) (every branch a no-op at construction:
//  the handle is already -1 and the range words already equal).
// ============================================================
static void *CPackageDataCtor(void *obj)
{
    DWORD *m = (DWORD *)obj;
    char *c = (char *)obj;
    F3ListStructCtor(c + 4, 28, 0x18);
    void *node = LHAlloc(4);
    if (node != 0)
        *(void **)node = c + 0x24;
    else
        node = 0;
    m[0x24 / 4] = (DWORD)node;
    DWORD *head = (DWORD *)LHAlloc(40);
    if (head != 0)
        head[0] = (DWORD)head;
    if (head != (DWORD *)-1)
        head[1] = (DWORD)head;
    m[0x38 / 4] = (DWORD)head;
    m[0x3C / 4] = 0;
    F3ListStructCtor(c + 0x40, 24, 0x14);
    m[0x60 / 4] = 0;
    m[0x6C / 4] = 0;
    m[0x70 / 4] = 0;
    m[0x74 / 4] = 0;
    m[0x78 / 4] = 0;
    m[0x7C / 4] = 0xFFFFFFFF;
    *(void **)(c + 0x80) = 0;
    *(void **)(c + 0x88) = 0;
    *(void **)(c + 0x90) = 0;
    F3ListStructCtor(c + 0x9C, 40, 0x20);
    m[0x98 / 4] = 0;
    CPackageDataStateReset(obj);
    return obj;
}

// ============================================================
//  F3ListSizeAdd  (sub_76CE80, 0x76CE80 - 0x76CF90)  -- list size increment
//  [NOT byte-identical] refs the std::length_error / exception machinery
//  and the high-VA global dword_1DDCFC8 (0x01DDCFC8) in the overflow path.
//
//  int F3ListSizeAdd(void *obj, int n)
//
//  Thiscall + `retn 4`, with an extra (unused) EBX argument (the list index
//  the caller holds in EBX). The sibling of ListCountInc (0x624360,
//  threshold 0x3FFFFFFF) with threshold 0x1FFFFFFF: when
//  0x1FFFFFFF - size < n (unsigned) it builds
//  std::length_error("list<T> too long") (invoking the non-NULL callback
//  at dword_1DDCFC8 first, then std::_Throw); otherwise it adds n to the
//  size word at obj+0x18 and returns n.
// ============================================================
static void (*g_dword_1DDCFC8)(const void *) = 0;   // 0x01DDCFC8: exception hook (0 here)
static int F3ListSizeAdd(void *obj, int n)
{
    unsigned *size = (unsigned *)((char *)obj + 0x18);
    if (0x1FFFFFFFu - *size < (unsigned)n)
    {
        std::length_error err("list<T> too long");
        if (g_dword_1DDCFC8 != 0)
            g_dword_1DDCFC8(&err);
        throw err;
    }
    *size += (unsigned)n;
    return n;
}

// ============================================================
//  CPackageManagerCtor  (sub_767360, 0x767360 - 0x76746E)  -- the 48-byte
//  (0x30) CPackageManager ctor
//  [NOT byte-identical] relative calls (base ctor, LHAlloc,
//  CPackageDataCtor, F3ListSizeAdd) and the CPackageManager vtable store
//  (the placeholder g_CPackageManagerVtable in this build).
//
//  void *CPackageManagerCtor(void *obj)
//
//  Thiscall (plain retn), returns obj. Runs the NTools base ctor, stores
//  the CPackageManager vtable, then:
//    +0x04/+0x08  0
//    +0x0C node   LHAlloc(4), *node = obj+0x0C (0 on failure)
//    +0x20 head   LHAlloc(16), head[0] = head[1] = head (the self-
//                 referencing circular list head); +0x24 (the list size) and
//                 +0x28 = 0
//    +0x2C sub    LHAlloc(0xBC) + CPackageDataCtor (0 on allocation failure)
//  then it allocates nine 16-byte { prev, next, id } nodes into that
//  circular list. Each node is inserted directly after the head (prev =
//  head, next = the old first node, whose prev becomes the new node), so
//  the final list order is node 8 .. node 0, with id =
//  0x4D5308D6_0CCF0000 + i (the 64-bit value at the node's +8). The list
//  size (obj+0x24, via F3ListSizeAdd on obj+0x0C) is incremented once per
//  node.
// ============================================================
static void *CPackageManagerCtor(void *obj)
{
    DWORD *m = (DWORD *)obj;
    char *c = (char *)obj;
    F3BaseClassNonCopyableCtor(obj);
    m[0] = (DWORD)g_CPackageManagerVtable;
    m[1] = 0;
    m[2] = 0;
    void *node = LHAlloc(4);
    if (node != 0)
        *(void **)node = c + 0x0C;
    else
        node = 0;
    m[3] = (DWORD)node;
    DWORD *head = (DWORD *)LHAlloc(16);
    if (head != 0)
        head[0] = (DWORD)head;
    if (head != (DWORD *)-1)
        head[1] = (DWORD)head;
    m[8] = (DWORD)head;
    m[9] = 0;
    m[10] = 0;
    void *sub = LHAlloc(0xBC);
    m[11] = (sub != 0) ? (DWORD)CPackageDataCtor(sub) : 0;
    for (int i = 0; i < 9; i++)
    {
        DWORD h = m[8];
        DWORD next = ((DWORD *)h)[1];
        DWORD *n = (DWORD *)LHAlloc(16);
        if (n != 0)
            n[0] = h;
        if (n != (DWORD *)-1)
            n[1] = next;
        DWORD lo = (DWORD)i + 0x0CCF0000;
        n[2] = lo;
        n[3] = 0x4D5308D6 + (lo < 0x0CCF0000);
        F3ListSizeAdd(c + 0x0C, 1);
        ((DWORD *)h)[1] = (DWORD)n;
        ((DWORD *)n[1])[0] = (DWORD)n;
    }
    return obj;
}
// ============================================================
//  CVersionTable  (sub_762490, 0x762490 - 0x76252E)  -- the 288-byte (0x120)
//  version table: CPackageManager + three CGameVersions + summary flags
//  [NOT byte-identical] relative calls to CGameVersionCtor /
//  CGameVersionReset / CVersionTableRefresh.
//
//  The version table is built over the CPackageManager (engine+0xAC): the
//  object holds the CPackageManager pointer at +0x00, three CGameVersion
//  sub-objects (92 bytes each) at +0x04 / +0x60 / +0xBC, a flag byte at
//  +0x118, an auxiliary byte at +0x119, and a 32-bit summary CRC at +0x11C.
//
//  RunFrontEnd swaps this object into engine+0xB0: the old table is
//  destroyed (CVersionTableDtor (sub_6234A0) + LHFree) and the new one
//  stored in its place.
//
//  The engine's shared-string (F3SharedBuffer) and 12-byte node-list helpers
//  below (SharedSlotFormat / SharedSlotAppendSlot / SharedSlotConcatLiteral /
//  SharedSlotCrc32 / F3NodeList*) and the package-entry lookup chain
//  (PackageEntry*) are the callees of the refresh / summary path. In the
//  test environment no packages are loaded, so every package list is empty
//  and only the allocation / loop-break / empty-string paths execute.
// ============================================================

// The 7-word (28-byte) entry array CVersionTableRefresh / BuildSummary use:
//   +0x00  node   4-byte heap node (2-element circular with *base)
//   +0x14  head   12-byte self-linked sentinel {first, last}
//   +0x18  count  number of 12-byte entry nodes (payload = package id at +8)
typedef struct F3EntryArray
{
    DWORD  node;    // +0x00  4-byte heap node
    DWORD  pad[4];  // +0x04 .. +0x0F
    DWORD  head;    // +0x14  12-byte self-linked sentinel block
    DWORD  count;   // +0x18  entry count
} F3EntryArray;

// Compile-time check: the entry array must be exactly 28 bytes (7 words).
typedef char F3EntryArraySizeCheck[(sizeof(F3EntryArray) == 28) ? 1 : -1];

// sub_EEB860 (0xEEB860 - 0xEEB865): the package id of an entry object
static int PackageEntryGetId(const void *entry)
{
    return *(const int *)((const char *)entry + 0x14);
}

// sub_EEB880 / sub_EEB8B0 / sub_EEB980 (4 bytes each): the entry flag bytes
static int PackageEntryGetFlag24(const void *entry)
{
    return *(const unsigned char *)((const char *)entry + 0x18);
}
static int PackageEntryGetFlag25(const void *entry)
{
    return *(const unsigned char *)((const char *)entry + 0x19);
}
static int PackageEntryGetFlag26(const void *entry)
{
    return *(const unsigned char *)((const char *)entry + 0x1A);
}

// sub_769250 (0x769250 - 0x7692B2): advance a 28-byte node-list iterator
// sub_769340 (0x769340 - 0x7693A2): the twin (advances only when the node's
//   byte+0x19 alive flag is exactly 1, otherwise a no-op)
//
//  void *PackageEntryAdvance(void *obj)
//
//  thiscall. obj is a 2-dword iterator {cursor node, current 28-byte node};
//  the current node follows its +0x08 link (rewriting node+0 and the next
//  node's +4 to keep the circular links consistent), or falls back to the
//  cursor node when the link is null. [not exercised in the test -- the
//  lists are empty]
static void *PackageEntryAdvance(void *obj)
{
    DWORD *itd = (DWORD *)obj;
    if (itd[0] != 0)
    {
        DWORD *cur28 = (DWORD *)itd[1];
        if (cur28[6] != 0)
        {
            DWORD *next = cur28 + 2;
            if (next != 0)
            {
                *cur28 = (DWORD)next;
                next[1] = (DWORD)cur28;
            }
            else
            {
                *cur28 = (DWORD)obj;
                next = 0;
            }
            itd[1] = (DWORD)next;
        }
    }
    return obj;
}

static void *PackageEntryAdvanceNext(void *obj)
{
    DWORD *itd = (DWORD *)obj;
    if (itd[0] != 0)
    {
        DWORD *cur28 = (DWORD *)itd[1];
        if (cur28[6] == 1)
        {
            DWORD *next = cur28 + 2;
            if (next != 0)
            {
                *cur28 = (DWORD)next;
                next[1] = (DWORD)cur28;
            }
            else
            {
                *cur28 = (DWORD)obj;
                next = 0;
            }
            itd[1] = (DWORD)next;
        }
    }
    return obj;
}

// sub_489F10 (0x489F10 - 0x489F8C): add n to a 12-byte node-list count
//
//  int F3NodeListSizeAdd(void *obj, int n)
//
//  thiscall + retn 4. Throws std::length_error("list<T> too long") when the
//  count would exceed 0x3FFFFFFF; otherwise adds n to the count word at
//  obj+0x18 and returns n.
static int F3NodeListSizeAdd(void *obj, int n)
{
    unsigned int *count = (unsigned int *)((char *)obj + 0x18);
    if (0x3FFFFFFFu - *count < (unsigned int)n)
    {
        std::length_error err("list<T> too long");
        throw err;
    }
    *count += (unsigned int)n;
    return n;
}

// sub_489030 (0x489030 - 0x4890B8): unlink one 12-byte node from a list
//
//  void F3NodeListUnlink(void *obj, void *out2, void *head, void *cur)
//
//  thiscall + retn 0xC. Unlinks cur from the 12-byte circular list (the
//  node's prev/next are re-linked), drops the node through the heap table's
//  free slot (the original's dword_2075B24+16 thunk, LHFree here),
//  decrements the list count at obj+0x18, and leaves {*obj, head[0]} in the
//  2-dword out2. [not exercised in the test -- only reached when a search
//  finds a node in a non-empty list]
static void F3NodeListUnlink(void *obj, void *out2, void *head, void *cur)
{
    void *prev = *(void **)cur;
    void *next = *(void **)((char *)cur + 4);
    *(void **)next = prev;
    *(void **)((char *)prev + 4) = next;
    LHFree(cur);
    if (obj != 0)
        (*(unsigned int *)((char *)obj + 0x18)) -= 1;
    if (out2 != 0)
    {
        *(void **)out2 = (obj != 0) ? *(void **)obj : 0;
        ((void **)out2)[1] = (head != 0) ? *(void **)head : 0;
    }
}

// sub_488580 (0x488580 - 0x488672): remove a payload from a node-list section
//
//  void F3NodeListRemove(void *obj, int *value)
//
//  thiscall + retn 4. Searches the 12-byte node list of the section (node
//  slot at +0, 12-byte sentinel at +0x14) for a node whose payload (+8)
//  equals *value and unlinks it (F3NodeListUnlink). A freshly constructed
//  section has an empty list (sentinel self-linked), so the search breaks
//  immediately -- the executed test path.
static void F3NodeListRemove(void *obj, int *value)
{
    char *sec = (char *)obj;
    void *node = *(void **)sec;
    void *head = *(void **)(sec + 0x14);
    void *cur = *(void **)head;
    if (node != 0 && node == *(void **)sec)
    {
        while (cur != head)
        {
            if (node == 0)
                break;
            node = *(void **)node;
            if (cur == *(void **)((char *)node + 0x18))
                break;   // original: __invalid_parameter_noinfo guard
            if (*(int *)((char *)cur + 8) == *value)
            {
                F3NodeListUnlink(sec, 0, head, cur);
                cur = *(void **)((char *)cur + 4);
            }
            else
            {
                cur = *(void **)cur;
            }
        }
    }
}

// sub_488FD0 (0x488FD0 - 0x489026): append a 12-byte entry node to a list
//
//  void F3NodeListAppend(void *obj, void *head, int *value)
//
//  thiscall + retn 0xC. Allocates a 12-byte node {head, last, *value} and
//  links it in after the sentinel's last pointer (head[1] = new, last[0] =
//  new), then bumps the count via F3NodeListSizeAdd(obj, 1).
static void F3NodeListAppend(void *obj, void *head, int *value)
{
    DWORD *h = (DWORD *)head;
    DWORD last = h[1];
    DWORD *n = (DWORD *)LHAlloc(12);
    if (n != 0)
    {
        n[0] = (DWORD)head;
        n[1] = last;
        n[2] = (DWORD)*value;
    }
    F3NodeListSizeAdd(obj, 1);
    h[1] = (DWORD)n;
    ((DWORD *)last)[0] = (DWORD)n;
}

// sub_72DC40 (0x72DC40 - 0x72DC46): set the CGameVersion flag byte
static void CGameVersionSetFlag(void *obj, int flag)
{
    *(char *)((char *)obj + 4) = (char)flag;
}

// sub_72DC30 (0x72DC30 - 0x72DC36): read the CGameVersion flag byte
static int CGameVersionGetFlag(const void *obj)
{
    return *(const char *)((const char *)obj + 4);
}

// sub_72DC20 (0x72DC20 - 0x72DC26): the third 12-byte node-list section
static void *CGameVersionThirdSection(void *obj)
{
    return (char *)obj + 0x40;
}

// sub_72E460 (0x72E460 - 0x72E48A) / sub_72E490 (0x72E490 - 0x72E4BA) /
// sub_72E4C0 (0x72E4C0 - 0x72E4EA): ensure a version-list entry
//
//  void CGameVersionInsertN(void *obj, int id, int doInsert)
//
//  thiscall + retn 8. Removes any entry whose payload is id from the
//  section, then (when doInsert is non-zero) appends a fresh entry with
//  that payload. Section N bases: +0x08 / +0x24 / +0x40.
static void CGameVersionInsert1(void *obj, int id, int doInsert)
{
    char *sec = (char *)obj + 8;
    F3NodeListRemove(sec, &id);
    if (doInsert != 0)
        F3NodeListAppend(sec, *(void **)(sec + 0x14), &id);
}
static void CGameVersionInsert2(void *obj, int id, int doInsert)
{
    char *sec = (char *)obj + 0x24;
    F3NodeListRemove(sec, &id);
    if (doInsert != 0)
        F3NodeListAppend(sec, *(void **)(sec + 0x14), &id);
}
static void CGameVersionInsert3(void *obj, int id, int doInsert)
{
    char *sec = (char *)obj + 0x40;
    F3NodeListRemove(sec, &id);
    if (doInsert != 0)
        F3NodeListAppend(sec, *(void **)(sec + 0x14), &id);
}

// sub_EEC760 (0xEEC760 - 0xEEC858): find an id in an entry group's sub-list
//
//  void *PackageEntryMatch(void *obj, void *out2, int id)
//
//  thiscall + retn 8. obj is an entry-group container: its 28-byte node
//  sub-list has the 28-byte sentinel at obj+0x18 and the 4-byte node slot
//  at obj+0x30. Walks the sub-list; each node's entry object (node+0x10)
//  is Interlocked-refcounted through node+0x14, and when the entry's id
//  (PackageEntryGetId) matches, {entry, ref} is stored in out2 (the match
//  path's increment / decrement pair nets to zero, leaving the caller's
//  one ref). A miss stores {0, 0} in out2. Returns out2.
//  [not exercised in the test -- the sub-lists are empty]
static void *PackageEntryMatch(void *obj, void *out2, int id)
{
    char *o = (char *)obj;
    char *out = (char *)out2;
    void *head = *(void **)(o + 0x18);
    void *slot = *(void **)(o + 0x30);
    void *cur = *(void **)slot;

    if (head == 0 || head != slot)
        return out2;   // original: __invalid_parameter_noinfo (a no-op here)

    if (cur != slot)
    {
        DWORD it[2];
        it[0] = (DWORD)slot;
        it[1] = (DWORD)cur;
        for (;;)
        {
            char *n28 = (char *)it[1];
            void *ref = *(void **)(n28 + 0x14);
            void *entry2 = *(void **)(n28 + 0x10);
            if (ref != 0)
                InterlockedIncrement((volatile LONG *)ref);
            if (PackageEntryGetId(entry2) == id)
            {
                *(void **)out = entry2;
                *(void **)(out + 4) = ref;
                if (ref != 0)
                {
                    InterlockedIncrement((volatile LONG *)ref);
                    if (InterlockedDecrement((volatile LONG *)ref) == 0)
                    {
                        void (*fn)(void *) = *(void (**)(void *))((char *)ref + 4);
                        fn(*(void **)((char *)ref + 8));
                        LHFree(ref);
                    }
                }
                break;
            }
            if (ref != 0 && InterlockedDecrement((volatile LONG *)ref) == 0)
            {
                void (*fn)(void *) = *(void (**)(void *))((char *)ref + 4);
                fn(*(void **)((char *)ref + 8));
                LHFree(ref);
            }
            PackageEntryAdvanceNext(it);
            if (it[1] == (DWORD)slot)
                break;
        }
    }
    return out2;
}

// sub_7632E0 (0x7632E0 - 0x7634F6): find an id in the CPackageData group list
//
//  void PackageEntryLookup(void *out2, void *container, int id)
//
//  stdcall + retn 0xC. container is the CPackageData+4 group list (4-byte
//  node slot at +0, 28-byte sentinel at +0x18). Walks the group's 28-byte
//  nodes, dispatching each group container (node+0x10) to PackageEntryMatch;
//  a hit stores {entry, ref} in out2 (the hit path's increment / decrement
//  pair nets to zero), a miss stores {0, 0}.
//  [not exercised in the test -- the group list is empty]
static void PackageEntryLookup(void *out2, void *container, int id)
{
    char *out = (char *)out2;
    char *ctr = (char *)container;
    void *node = *(void **)ctr;
    void *head = *(void **)(ctr + 0x18);
    void *cur = *(void **)head;

    if (node == 0 || node != *(void **)ctr)
        return;

    DWORD it[2];
    it[0] = (DWORD)node;
    it[1] = (DWORD)cur;
    for (;;)
    {
        if (cur == head)
            break;
        if (node == 0)
            break;
        node = *(void **)node;
        if (cur == *(void **)((char *)node + 0x18))
            break;   // original: __invalid_parameter_noinfo guard

        int res[2] = { 0, 0 };
        void *entry = *(void **)((char *)cur + 0x10);
        if (PackageEntryMatch(entry, res, id) == res && res[0] != 0)
        {
            *(void **)out = *(void **)res;
            *(void **)(out + 4) = *(void **)(res + 4);
            if (res[1] != 0)
            {
                InterlockedIncrement((volatile LONG *)res[1]);
                if (InterlockedDecrement((volatile LONG *)res[1]) == 0)
                {
                    void (*fn)(void *) = *(void (**)(void *))((char *)res[1] + 4);
                    fn(*(void **)((char *)res[1] + 8));
                    LHFree((void *)res[1]);
                }
            }
            return;
        }
        if (res[1] != 0 && InterlockedDecrement((volatile LONG *)res[1]) == 0)
        {
            void (*fn)(void *) = *(void (**)(void *))((char *)res[1] + 4);
            fn(*(void **)((char *)res[1] + 8));
            LHFree((void *)res[1]);
        }
        it[0] = (DWORD)node;
        it[1] = (DWORD)cur;
        PackageEntryAdvance(it);
        cur = (void *)it[1];
    }
    *(void **)out = 0;
    *(void **)(out + 4) = 0;
}

// sub_763DC0 / sub_763F20 / sub_764080 (165 bytes each): the per-entry
// flag predicates
//
//  int PackageEntryHasFlagN(void *obj, int id)
//
//  thiscall + retn 4. Looks up id in the CPackageData+4 group list
//  (PackageEntryLookup), returns the entry's flag byte (24 / 25 / 26) when
//  found -- 0 when not found for 24 / 25, 1 for 26 -- and releases the
//  lookup's ref in every case.
static int PackageEntryHasFlag24(void *obj, int id)
{
    int res[2] = { 0, 0 };
    PackageEntryLookup(res, (char *)obj + 4, id);
    int r = (res[0] != 0) ? PackageEntryGetFlag24(*(void **)res) : 0;
    if (res[1] != 0 && InterlockedDecrement((volatile LONG *)res[1]) == 0)
    {
        void (*fn)(void *) = *(void (**)(void *))((char *)res[1] + 4);
        fn(*(void **)((char *)res[1] + 8));
        LHFree((void *)res[1]);
    }
    return r;
}
static int PackageEntryHasFlag25(void *obj, int id)
{
    int res[2] = { 0, 0 };
    PackageEntryLookup(res, (char *)obj + 4, id);
    int r = (res[0] != 0) ? PackageEntryGetFlag25(*(void **)res) : 0;
    if (res[1] != 0 && InterlockedDecrement((volatile LONG *)res[1]) == 0)
    {
        void (*fn)(void *) = *(void (**)(void *))((char *)res[1] + 4);
        fn(*(void **)((char *)res[1] + 8));
        LHFree((void *)res[1]);
    }
    return r;
}
static int PackageEntryHasFlag26(void *obj, int id)
{
    int res[2] = { 0, 0 };
    PackageEntryLookup(res, (char *)obj + 4, id);
    int r = (res[0] != 0) ? PackageEntryGetFlag26(*(void **)res) : 1;
    if (res[1] != 0 && InterlockedDecrement((volatile LONG *)res[1]) == 0)
    {
        void (*fn)(void *) = *(void (**)(void *))((char *)res[1] + 4);
        fn(*(void **)((char *)res[1] + 8));
        LHFree((void *)res[1]);
    }
    return r;
}

// sub_7646B0 / sub_7646D0 / sub_7646F0 (8 bytes each): the CPackageManager
// wrappers -- extract the CPackageData sub-object (obj+0x2C) and forward to
// the matching flag predicate.
static int PackageEntryTest24(void *obj, int id)
{
    return PackageEntryHasFlag24(*(void **)((char *)obj + 0x2C), id);
}
static int PackageEntryTest25(void *obj, int id)
{
    return PackageEntryHasFlag25(*(void **)((char *)obj + 0x2C), id);
}
static int PackageEntryTest26(void *obj, int id)
{
    return PackageEntryHasFlag26(*(void **)((char *)obj + 0x2C), id);
}

// sub_764F60 (0x764F60 - 0x765281): flatten the CPackageData group list
//
//  void PackageEntryArrayBuild(void *obj, void *out)
//
//  thiscall + retn 4. obj is the CPackageData sub-object. out is a fresh
//  7-word F3EntryArray: allocates the 4-byte node (out+0, a 2-element
//  circular with out), the 12-byte self-linked sentinel (out+0x14) and the
//  zero count (out+0x18), then walks the CPackageData+4 group list (28-byte
//  nodes): each group container (node+0x10) owns a 28-byte sub-list (28-byte
//  sentinel at container+0x18, 4-byte node slot at container+0x30) whose
//  entries are refcounted through node+0x14; every sub-entry whose flag
//  byte +0x18 is set contributes a 12-byte node {sentinel, last, id} to out
//  (count++ via F3NodeListSizeAdd). Refs that fall to zero free the entry
//  through its destructor thunk. [the test environment has an empty group
//  list: only the allocations and the immediate loop break run]
static void PackageEntryArrayBuild(void *obj, void *out)
{
    F3EntryArray *a = (F3EntryArray *)out;
    void *node = LHAlloc(4);
    if (node != 0)
        *(void **)node = out;
    if (node != (void *)-1)
        a->node = (DWORD)node;

    void *head = LHAlloc(12);
    if (head != 0)
    {
        *(void **)head = head;
        *(void **)((char *)head + 4) = head;
    }
    if (head != (void *)-1)
    {
        a->head = (DWORD)head;
        a->count = 0;
    }

    void *slot = *(void **)obj;
    void *head28 = *(void **)((char *)obj + 0x1C);
    void *cur = *(void **)head28;
    if (slot == 0 || slot != *(void **)obj)
        return;

    DWORD it[2];
    it[0] = (DWORD)slot;
    it[1] = (DWORD)cur;
    for (;;)
    {
        if (cur == head28)
            break;
        if (slot == 0)
            break;
        slot = *(void **)slot;
        if (cur == *(void **)((char *)slot + 0x18))
            break;   // original: __invalid_parameter_noinfo guard

        char *n28 = (char *)cur;
        void *container = *(void **)(n28 + 0x10);
        void *ref = *(void **)((char *)container + 0x14);
        if (ref != 0)
            InterlockedIncrement((volatile LONG *)ref);

        void *subhead = *(void **)((char *)container + 0x18);
        void *subslot = *(void **)((char *)container + 0x30);
        if (subhead != 0 && subslot != 0 && subhead != subslot)
        {
            void *cur28 = *(void **)subslot;
            if (cur28 != subslot)
            {
                DWORD it2[2];
                it2[0] = (DWORD)subslot;
                it2[1] = (DWORD)cur28;
                for (;;)
                {
                    char *m28 = (char *)it2[1];
                    void *e2 = *(void **)(m28 + 0x10);
                    if (PackageEntryGetFlag24(e2) != 0)
                    {
                        int id = PackageEntryGetId(e2);
                        F3NodeListAppend(out, (void *)a->head, &id);
                    }
                    void *ref2 = *(void **)(m28 + 0x14);
                    if (ref2 != 0 && InterlockedDecrement((volatile LONG *)ref2) == 0)
                    {
                        void (*fn)(void *) = *(void (**)(void *))((char *)ref2 + 4);
                        fn(*(void **)((char *)ref2 + 8));
                        LHFree(ref2);
                    }
                    PackageEntryAdvanceNext(it2);
                    if (it2[1] == (DWORD)subslot)
                        break;
                }
            }
        }

        if (ref != 0 && InterlockedDecrement((volatile LONG *)ref) == 0)
        {
            void (*fn)(void *) = *(void (**)(void *))((char *)ref + 4);
            fn(*(void **)((char *)ref + 8));
            LHFree(ref);
        }
        it[0] = (DWORD)slot;
        it[1] = (DWORD)cur;
        PackageEntryAdvance(it);
        cur = (void *)it[1];
    }
}

// sub_7661E0 (0x7661E0 - 0x7661F4): the CPackageManager wrapper
//
//  void CPackageManagerCollectEntries(void *obj, void *out)
//
//  stdcall + retn 4. Extracts the CPackageData sub-object (obj+0x2C) and
//  builds the entry array (PackageEntryArrayBuild).
static void CPackageManagerCollectEntries(void *obj, void *out)
{
    PackageEntryArrayBuild(*(void **)((char *)obj + 0x2C), out);
}

// sub_1A01670 (0x1A01670 - 0x1A01A3C): format into a fresh shared slot
//
//  F3SharedBuffer **SharedSlotFormat(F3SharedBuffer **out, const char *fmt, ...)
//
//  cdecl. vsprintf_s's the varargs into a temporary heap buffer (a
//  strlen(fmt) + 1032 byte allocation carrying the 56797 magic word 8 bytes
//  before the text), then, when the format did not report truncation, builds
//  a fresh 16-byte control block around the formatted C string (refcount 1)
//  via SharedBufferAssign, releases the block the slot held (if any), and
//  stores the new block in out. The temporary is freed in every case.
//  Returns out.
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

// sub_1A01940 (0x1A01940 - 0x1A019A1): append the src slot's string to dst
//
//  F3SharedBuffer **SharedSlotAppendSlot(F3SharedBuffer **dst, F3SharedBuffer **src)
//
//  thiscall + retn 4. When dst holds a block, DetachSharedSlot makes it
//  private and the src slot's C string is appended (SharedBufferAppend);
//  when dst is empty, the src block is shared (refcount++) instead. Returns
//  dst.
static F3SharedBuffer **SharedSlotAppendSlot(F3SharedBuffer **dst, F3SharedBuffer **src)
{
    if (*dst != 0)
    {
        DetachSharedSlot(dst);
        const char *s = GetSlotString(src);
        unsigned int len = 0;
        while (s[len] != 0)
            len++;
        SharedBufferAppend(*dst, s, len);
    }
    else if (src != dst)
    {
        F3SharedBuffer *b = *src;
        if (b != 0)
        {
            *dst = b;
            InterlockedIncrement(&b->refcount);
        }
    }
    return dst;
}

// sub_1A02200 (0x1A02200 - 0x1A02228): set dst to a literal, then append src
//
//  F3SharedBuffer **SharedSlotConcatLiteral(F3SharedBuffer **dst, const char *lit, F3SharedBuffer **src)
//
//  cdecl. SetSharedSlot(dst, lit, -1) followed by SharedSlotAppendSlot(dst,
//  src); returns dst. (The version-table summary loop uses it to build the
//  ",id" segments.)  Exposed (non-static) for the CLuaManager serialise-script
//  path (engine/luavm.cpp); declared in gamemain.h.
F3SharedBuffer **SharedSlotConcatLiteral(F3SharedBuffer **dst, const char *lit, F3SharedBuffer **src)
{
    SetSharedSlot(dst, lit, -1);
    SharedSlotAppendSlot(dst, src);
    return dst;
}

// sub_1A02260 (0x1A02260 - 0x1A02292): concat two source slots into a fresh dst
//
//  F3SharedBuffer **SharedSlotConcat(F3SharedBuffer **dst, F3SharedBuffer **src1, F3SharedBuffer **src2)
//
//  cdecl. Zeroes dst, bumps the live-slot count (g_Addend), copy-assigns src1
//  (CopySharedSlot, 0x1A01270), then appends src2 (SharedSlotAppendSlot,
//  0x1A01940); returns the dst slot address.  The two-source sibling of
//  SharedSlotConcatLiteral (0x1A02200, literal + slot) and of
//  MakeSettingsFilePath (0x1A02230, slot + literal).  [NOT byte-identical]
//  (placement-dependent relative calls to CopySharedSlot /
//  SharedSlotAppendSlot, the Interlocked IAT slot, and the high-VA global
//  g_Addend, 0x02075C48).  Exposed (non-static) for the CLuaManager
//  serialise-script path (engine/luavm.cpp); declared in gamemain.h.
F3SharedBuffer **SharedSlotConcat(F3SharedBuffer **dst, F3SharedBuffer **src1, F3SharedBuffer **src2)
{
    *dst = 0;
    InterlockedIncrement(&g_Addend);
    CopySharedSlot(dst, src1);
    SharedSlotAppendSlot(dst, src2);
    return dst;
}

// sub_1A15800 (0x1A15800 - 0x1A15855): CRC-32 of a slot's C string
//
//  unsigned int SharedSlotCrc32(const F3SharedBuffer **slot, unsigned int seed)
//
//  cdecl + retn 8. Runs the reflected CRC-32 (table at 0x1BCE700, the
//  0xEDB88320 table transcribed below) over the slot block's C string
//  (data / length). An empty / null slot hashes the empty string (the seed
//  is returned unchanged).
static const unsigned int g_CrcTable[256] =
{
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

static unsigned int SharedSlotCrc32(F3SharedBuffer **slot, unsigned int seed)
{
    const F3SharedBuffer *blk = *slot;
    const char *s = (blk != 0) ? blk->data : "";
    unsigned int len = (blk != 0) ? blk->length : 0;
    unsigned int crc = seed;
    unsigned int i = 0;
    while (len != 0 && i < len)
    {
        crc = g_CrcTable[(crc ^ (unsigned char)s[i]) & 0xFFu] ^ (crc >> 8);
        i++;
    }
    return crc;
}

// sub_59C860 (0x59C860 - 0x59C8EE): the 7-word entry-array ctor
//
//  void F3EntryArrayCtor(void *obj, void *srcSection)
//
//  thiscall + retn 4. Allocates the 4-byte node (obj+0, back-referencing
//  obj), the 12-byte self-linked sentinel (obj+0x14) and the zero count
//  (obj+0x18), then copies the source section's 12-byte payload list into
//  the new array (sub_59DCF0: each source payload becomes a fresh 12-byte
//  node {sentinel, last, payload} appended to the end, count++).
//  [the test environment copies from a freshly constructed empty section --
//  the copy loop never iterates]
static void F3EntryArrayCtor(void *obj, void *srcSection)
{
    F3EntryArray *a = (F3EntryArray *)obj;
    void *node = LHAlloc(4);
    if (node != 0)
        *(void **)node = obj;
    a->node = (DWORD)node;

    void *head = LHAlloc(12);
    if (head != 0)
    {
        *(void **)head = head;
        *(void **)((char *)head + 4) = head;
    }
    a->head = (DWORD)head;
    a->count = 0;

    void *srcHead = *(void **)((char *)srcSection + 0x14);
    void *cur = *(void **)srcHead;
    while (cur != srcHead)
    {
        int id = *(int *)((char *)cur + 8);
        F3NodeListAppend(obj, head, &id);
        cur = *(void **)cur;
    }
}

// sub_762A90 (0x762A90 - 0x762E6C): the summary entry-array sort
//
//  void CVersionTableSortEntries(void *obj)
//
//  cdecl. An 884-byte binary-insertion sort over the F3EntryArray's 12-byte
//  nodes (up to 26 buckets, moving nodes with sub_762910 and re-linking the
//  circular list as it goes, then freeing the temporary nodes / sentinels).
//  The summary array is empty in the test environment (count 0 < 2), so the
//  original returns immediately; the sort body is only reachable with loaded
//  packages.
static void CVersionTableSortEntries(void *obj)
{
    if (*(unsigned int *)((char *)obj + 0x18) < 2)
        return;
    // [sort body omitted -- not exercised in the test environment]
}

// sub_762120 (0x762120 - 0x7622CD): build the "RequiredPackages:" summary
//
//  void CVersionTableBuildSummary(void *obj)
//
//  thiscall. Copies CV1's third 12-byte node-list section (CGameVersion+
//  0x40, via CGameVersionThirdSection) into a temporary 7-word entry array
//  (F3EntryArrayCtor), sorts it (CVersionTableSortEntries), and builds the
//  shared string "RequiredPackages:<flag>[,<id>...]" -- <flag> is CV1's
//  flag byte (CGameVersionGetFlag, 53 after refresh), one ",<id>" segment
//  (SharedSlotConcatLiteral with "," + a "%u"-formatted id) per array entry
//  -- then stores the CRC-32 of that string (SharedSlotCrc32, seed 0) at
//  obj+0x11C. Every temporary slot and the array (nodes, sentinel, 4-byte
//  node) are released on exit.
static void CVersionTableBuildSummary(void *obj)
{
    char *base = (char *)obj;
    char *cv1 = base + 4;

    F3EntryArray arr;
    F3EntryArrayCtor(&arr, CGameVersionThirdSection(cv1));
    CVersionTableSortEntries(&arr);

    F3SharedBuffer *slotStr = 0;
    SetSharedSlot(&slotStr, "RequiredPackages:", -1);

    unsigned int count = (unsigned char)CGameVersionGetFlag(cv1);
    F3SharedBuffer *slotNum = 0;
    InitSharedSlot(&slotNum);
    SharedSlotFormat(&slotNum, "%u", count);
    SharedSlotAppendSlot(&slotStr, &slotNum);
    ReleaseSharedSlot(&slotNum);

    void *head = (void *)arr.head;
    void *cur = *(void **)head;
    while (cur != head)
    {
        int id = *(int *)((char *)cur + 8);
        F3SharedBuffer *slotId = 0;
        InitSharedSlot(&slotId);
        SharedSlotFormat(&slotId, "%u", id);
        F3SharedBuffer *slotCma = 0;
        SharedSlotConcatLiteral(&slotCma, ",", &slotId);
        SharedSlotAppendSlot(&slotStr, &slotCma);
        ReleaseSharedSlot(&slotCma);
        ReleaseSharedSlot(&slotId);
        cur = *(void **)cur;
    }

    *(DWORD *)(base + 0x11C) = SharedSlotCrc32(&slotStr, 0);
    ReleaseSharedSlot(&slotStr);

    // release the temporary array: re-link the sentinel, free each 12-byte
    // node, then the sentinel and the 4-byte node
    void *first = *(void **)head;
    *(void **)head = head;
    *(void **)((char *)head + 4) = head;
    arr.count = 0;
    while (first != head)
    {
        void *n = first;
        first = *(void **)first;
        LHFree(n);
    }
    LHFree(head);
    LHFree((void *)arr.node);
}

// sub_7622D0 (0x7622D0 - 0x762505): refresh the version table from packages
//
//  int CVersionTableRefresh(void *obj)
//
//  thiscall. Resets CV1 (+0x04) and CV3 (+0xBC) (CGameVersionReset), sets
//  CV1's flag to 0x35 (53) and CV3's flag to 4 (CGameVersionSetFlag),
//  collects the package entry array from the CPackageManager
//  (CPackageManagerCollectEntries), and for each entry id:
//    - when the entry has flag 24 (PackageEntryTest24): re-add id to CV1's
//      and CV3's first sections (CGameVersionInsert1)
//    - re-add id to the second sections gated on flag 25 (CGameVersionInsert2)
//    - re-add id to the third sections gated on flag 26 (CGameVersionInsert3)
//  (the flag lookups are each issued twice, once per version sub-object,
//  exactly as in the original). Then builds the summary
//  (CVersionTableBuildSummary), sets obj+0x118 = 1, releases the temporary
//  entry array, and returns 1. [the test environment's entry array is
//  empty, so the per-entry loop never iterates]
static int CVersionTableRefresh(void *obj)
{
    char *base = (char *)obj;
    char *cv1 = base + 4;
    char *cv3 = base + 0xBC;

    CGameVersionReset(cv1);
    CGameVersionReset(cv3);
    CGameVersionSetFlag(cv1, 0x35);
    CGameVersionSetFlag(cv3, 4);

    F3EntryArray entries;
    CPackageManagerCollectEntries(*(void **)base, &entries);

    void *head = (void *)entries.head;
    void *cur = *(void **)head;
    while (cur != head)
    {
        int id = *(int *)((char *)cur + 8);
        if (PackageEntryTest24(*(void **)base, id) != 0)
        {
            CGameVersionInsert1(cv1, id, 1);
            CGameVersionInsert1(cv3, id, 1);
        }
        int f25 = PackageEntryTest25(*(void **)base, id);
        CGameVersionInsert2(cv1, id, f25);
        f25 = PackageEntryTest25(*(void **)base, id);
        CGameVersionInsert2(cv3, id, f25);
        int f26 = PackageEntryTest26(*(void **)base, id);
        CGameVersionInsert3(cv1, id, f26);
        f26 = PackageEntryTest26(*(void **)base, id);
        CGameVersionInsert3(cv3, id, f26);
        cur = *(void **)cur;
    }

    CVersionTableBuildSummary(base);
    *(unsigned char *)(base + 0x118) = 1;

    void *first = *(void **)head;
    *(void **)head = head;
    *(void **)((char *)head + 4) = head;
    entries.count = 0;
    while (first != head)
    {
        void *n = first;
        first = *(void **)first;
        LHFree(n);
    }
    LHFree(head);
    LHFree((void *)entries.node);
    return 1;
}

// sub_762490 (0x762490 - 0x76252E): the version-table ctor
//
//  void *CVersionTableCtor(void *obj, void *pkgMgr)
//
//  thiscall + retn 4. Stores the CPackageManager pointer at +0x00,
//  constructs CV1 (+0x04), CV2 (+0x60) and CV3 (+0xBC) (CGameVersionCtor),
//  zeroes +0x118 / +0x119 / +0x11C, resets CV1 / CV2 / CV3 again
//  (CGameVersionReset -- redundant after the ctor but present in the
//  original), then runs CVersionTableRefresh. Returns obj.
void *CVersionTableCtor(void *obj, void *pkgMgr)
{
    char *base = (char *)obj;
    *(void **)base = pkgMgr;
    CGameVersionCtor(base + 4);
    CGameVersionCtor(base + 0x60);
    CGameVersionCtor(base + 0xBC);
    *(unsigned char *)(base + 0x118) = 0;
    *(unsigned char *)(base + 0x119) = 0;
    *(DWORD *)(base + 0x11C) = 0;
    CGameVersionReset(base + 4);
    CGameVersionReset(base + 0x60);
    CGameVersionReset(base + 0xBC);
    CVersionTableRefresh(base);
    return obj;
}

// sub_489E90 (0x489E90 - 0x489ECA): clear the entry array's node list
//
//  void F3EntryArrayReset(void *obj)
//
//  thiscall. Re-links the sentinel (head.first = head.last = head), zeroes
//  the count, and LHFrees every 12-byte entry node of the array (the
//  sentinel itself is kept). [NOT byte-identical] placement-dependent
//  (relative call to LHFree) and refs the heap table (dword_2075B24 + 16)
//  at the original high VA.
static void F3EntryArrayReset(void *obj)
{
    F3EntryArray *a   = (F3EntryArray *)obj;
    void *head        = (void *)a->head;
    void *first       = *(void **)head;
    *(void **)head              = head;
    *(void **)((char *)head + 4) = head;
    a->count = 0;
    for (void *node = first; node != head; node = *(void **)node)
        LHFree(node);
}

// sub_59F010 (0x59F010 - 0x59F04B): copy-assign an entry array's node list
//
//  void F3EntryArrayCopyFrom(void *obj, const void *src)
//
//  thiscall + retn 0x14. The original takes five stack args -- the source
//  section's 4-byte wrapper, its first node (the sentinel's field-0), the
//  wrapper again, the 12-byte sentinel, and the owning CGameVersion (all
//  four used as argument checks for the inner F3EntryArrayCopy call, the
//  last one unused) -- but the copy needs only the two 28-byte sections.
//  Resets obj's node list (F3EntryArrayReset), then walks the source's
//  circular list (sentinel, following field 0) and appends every 12-byte
//  node's 4-byte payload (node+8) to obj (F3NodeListAppend, whose count
//  bump is F3NodeListSizeAdd(obj, 1) with the 0x3FFFFFFF length_error
//  guard). [NOT byte-identical] placement-dependent (relative calls to
//  F3EntryArrayReset / F3EntryArrayCopy / F3NodeListAppend / LHAlloc).
static void F3EntryArrayCopyFrom(void *obj, const void *src)
{
    F3EntryArrayReset(obj);
    const char *s    = (const char *)src;
    void       *head = *(void **)(s + 0x14);
    for (void *node = *(void **)head; node != head;
         node = *(void **)node)
        F3NodeListAppend(obj, head, (int *)((const char *)node + 8));
}

// sub_72EDF0 (0x72EDF0 - 0x72EE6A): copy-assign a CGameVersion
//
//  void *CGameVersionCopyFrom(void *obj, const void *src)
//
//  thiscall + retn 4. The CGameVersion copy-assign: when src != obj, copy
//  the flag byte (+0x04) and copy-assign each of the three 28-byte
//  F3EntryArray sections (+0x08 / +0x24 / +0x40) through
//  F3EntryArrayCopyFrom (reset the destination list, then re-append every
//  source entry node). Returns obj. The original's per-section comparisons
//  (&obj+0x08 vs &src+0x08, likewise +0x24 and +0x40) are each implied by
//  the leading src != obj test, so all three copies run when the objects
//  differ. [NOT byte-identical] placement-dependent (relative calls to
//  F3EntryArrayCopyFrom / F3EntryArrayReset / F3NodeListAppend / LHAlloc).
static void *CGameVersionCopyFrom(void *obj, const void *src)
{
    char        *d = (char *)obj;
    const char  *s = (const char *)src;
    if (s != d)
    {
        d[4] = s[4];
        F3EntryArrayCopyFrom(d + 8,    s + 8);
        F3EntryArrayCopyFrom(d + 0x24, s + 0x24);
        F3EntryArrayCopyFrom(d + 0x40, s + 0x40);
    }
    return obj;
}

// sub_4890E0 (0x4890E0 - 0x489103): the 28-byte (7-word) entry-array dtor
//
//  void F3EntryArrayDtor(void *obj)
//
//  thiscall. Clears the array's node list (F3EntryArrayReset), then frees
//  the 12-byte sentinel (obj+0x14) through the heap table's free slot and
//  zeroes the head slot. The caller frees the 4-byte wrapper node
//  (obj+0x00). [NOT byte-identical] placement-dependent (relative call to
//  F3EntryArrayReset; the sentinel free goes through the heap table at the
//  original high VA, i.e. LHFree).
static void F3EntryArrayDtor(void *obj)
{
    F3EntryArray *a = (F3EntryArray *)obj;
    F3EntryArrayReset(obj);
    LHFree((void *)a->head);
    a->head = 0;
}

// sub_6234A0 (0x6234A0 - 0x623573): the 288-byte CVersionTable dtor
//
//  void CVersionTableDtor(void *obj)
//
//  thiscall. The destructor of the version table built by CVersionTableCtor
//  (sub_762490): for each of the nine 28-byte F3EntryArray members -- the
//  three sections of each CGameVersion sub-object at +0x04 / +0x60 / +0xBC
//  (arrays at +0x0C / +0x28 / +0x44, +0x68 / +0x84 / +0xA0 and
//  +0xC4 / +0xE0 / +0xFC) -- runs F3EntryArrayDtor (clear the list + free
//  the sentinel) and then LHFrees the array's 4-byte wrapper node
//  (obj+0x00). The CPackageManager pointer at +0x00 is a reference only
//  (not released here); the 288-byte block itself is freed by the caller.
//  [NOT byte-identical] placement-dependent (relative calls to
//  F3EntryArrayDtor and LHFree; the wrapper frees ref the heap table at
//  the original high VA).
void CVersionTableDtor(void *obj)
{
    static const int offs[9] = {
        0x0C, 0x28, 0x44, 0x68, 0x84, 0xA0, 0xC4, 0xE0, 0xFC
    };
    for (int i = 0; i < 9; i++)
    {
        char *arr = (char *)obj + offs[i];
        F3EntryArrayDtor(arr);
        LHFree(*(void **)arr);
    }
}

// ============================================================
//  The package-collection load cluster -- CPackageManagerLoadCollections
//  (sub_7672E0), CPackageManagerLoadPackageCollections (sub_767160) and the
//  import / merge / version-build / offer-data chain they drive
// ============================================================

// Forward references into the rest of the file (defined below):
static void CPackageDataBuildCollectionEntry(void *obj, int *out, int a3,
                                             void *bufferEntry);   // sub_765330
static void *F3ListRangeMovePair(void *obj, DWORD node, DWORD head0,
                                 DWORD node2, DWORD head);         // sub_76E7B0
static char ReopenXmlFileBlock(F3XmlStream *stream, void *entry);  // sub_17DBD50

// sub_61A310 (0x61A310 - 0x61A316): the 6-byte version-table getter.
// IDA labels it with an unrelated Concurrency mangled name; it simply
// returns *(this+0xB0) -- the CVersionTable pointer off the game engine.
static void *GetGameEngineVersionTable(void *obj)
{
    return *(void **)((char *)obj + 0xB0);
}

// 0x1DBD03D -- image-initialized to 0xFF; gates the XContent enumeration in
// XContentEnumeratePackageCollections (the enumeration runs only when it is
// 0, so in this build it always returns 0 with *count untouched).
static unsigned char g_byte_1DBD03D = 0xFF;

// The 28-byte list layouts the package-collection work lists use. There are
// two distinct shapes (same total size, different field offsets):
//   F3List28 (plain, F3ListStructCtor16 / sub_76C010):
//     node@+0x00 (a 4-byte cursor block with *node = the list),
//     head@+0x14 (a 16-byte block: head[0] = first entry or the head itself
//     when empty, head[1] = last entry), size@+0x18, pad@+0x1C.
//   F3ListTree28 (tree, the F3ListStructCtor family / sub_76C680 et al.):
//     node@+0x00, head@+0x18 (a 28-byte block, head[0] = first or self when
//     empty, head[1] = last, byte +0x18 = 1, byte +0x19 = 1), size@+0x1C.
// Plain entries are blocks { next@+0x00, pad@+0x04, pad@+0x08,
// refBlock@+0x0C } forming a circular chain through the head block.
typedef struct F3List28
{
    DWORD   node;     // +0x00
    DWORD   pad0[4];  // +0x04 .. +0x13
    DWORD * head;     // +0x14
    DWORD   size;     // +0x18
} F3List28;          // 28 bytes
typedef char F3List28SizeCheck[(sizeof(F3List28) == 28) ? 1 : -1];
typedef struct F3ListTree28
{
    DWORD   node;     // +0x00
    DWORD   pad0[5];  // +0x04 .. +0x17
    DWORD * head;     // +0x18
    DWORD   size;     // +0x1C
} F3ListTree28;      // 32 bytes (head block itself is 28 bytes)
typedef char F3ListTree28SizeCheck[(sizeof(F3ListTree28) == 32) ? 1 : -1];

// 0x1C86BE4 -- the marketplace user-settings block (image-initialized to
// 0xFFFFFFFF); returned by GetMarketplaceUserSettings (sub_771460).
static void *g_dword_1C86BE4 = (void *)0xFFFFFFFF;

// sub_771460 (0x771460 - 0x771466): the user-settings block getter.
static void *GetMarketplaceUserSettings(void *obj)
{
    (void)obj;
    return g_dword_1C86BE4;
}

// sub_18C42C0 (0x18C42C0 - 0x18C4368): the XAPI error-report logger (builds
// the message through the shared-slot / report machinery, logs it, and
// InterlockedDecrement's the global live-slot count). [remaining stub] the
// error paths are unreachable in this build (g_byte_1DBD03D stays 0xFF), so
// it degrades to a no-op.
static void XApiErrorReport(int code, int subCode)
{
    (void)code; (void)subCode;
}

// sub_762EB0 (0x762EB0 - 0x7630B8?): XContent package-collection enumeration
//
//  int XContentEnumeratePackageCollections(void *buffer, DWORD *count, int n)
//
//  stdcall, 3 stack args (retn 0Ch). Gated on g_byte_1DBD03D == 0: builds
//  the 56-byte XCONTENTCREATEENUMERATOR block ({1, 0, userSettings, 0, 0, 0,
//  0x4D53090A, 2, ...} with userSettings from the current CNetManager),
//  XContentCreateEnumerator(n, block, &size, &handle) it, and on success
//  XEnumerate(handle, buffer, size, count, 0) + kernel32 CloseHandle; both
//  failures go to XApiErrorReport. A size > 32*n skips the enumeration
//  (count untouched). Always returns 0.
//  [NOT byte-identical] placement-dependent (calls to the xlive IAT slots).
static int XContentEnumeratePackageCollections(void *buffer, DWORD *count, int n)
{
    if (g_byte_1DBD03D == 0)
    {
        HANDLE hObject = INVALID_HANDLE_VALUE;
        DWORD   size = 0, num = 0;
        DWORD   st[14] = { 0 };
        memset(&st[1], 0, 0x34);
        st[0] = 1;
        st[2] = (DWORD)GetMarketplaceUserSettings(
                    CNetManagerGetUserManager(GetNetManager()));
        st[6] = 0x4D53090A;
        st[7] = 2;
        int r = XContentCreateEnumerator((DWORD)n, st, &size, &hObject);
        if (r != 0)
            XApiErrorReport(r, 0);
        else if (size <= (DWORD)(32 * n))
        {
            r = XEnumerate(hObject, buffer, size, &num, 0);
            CloseHandle(hObject);                     // kernel32 CloseHandle
            if (r != 0)
                XApiErrorReport(r, 0);
            else
                *count = num;
        }
    }
    return 0;
}

// The ref-block release idiom of the list entries / ref pairs: an 8-byte
// (16-byte for the F3SharedBuffer family) counted block whose refcount at
// +0x0C is InterlockedDecrement'd; on zero the block's stored release
// function at +0x04 is called thiscall-style with [block+0x08] as its
// `this`, and the block is LHFreed.
static void F3ReleaseRefBlock(DWORD block)
{
    if (block == 0)
        return;
    if (InterlockedDecrement((volatile long *)(block + 0xC)) != 0)
        return;
    typedef void (__thiscall *F3ReleaseFn)(void *obj);
    F3ReleaseFn fn = *(F3ReleaseFn *)(block + 4);
    fn(*(void **)(block + 8));
    LHFree((void *)block);
}

// sub_76C010 (0x76C010 - 0x76C04F): the 28-byte plain list ctor
//
//  void *F3ListStructCtor16(void *obj)
//
//  thiscall (plain retn), returns obj. Builds the F3List28 layout above:
//  node = LHAlloc(4) with *node = obj (0 on allocation failure),
//  head = LHAlloc(16) with head[0] = head[1] = head (self-referencing),
//  size = 0.
//  [NOT byte-identical] relative calls to LHAlloc.
static void *F3ListStructCtor16(void *obj)
{
    F3List28 *l = (F3List28 *)obj;
    void *node = LHAlloc(4);
    l->node = (DWORD)node;
    if (node != 0)
        *(DWORD *)node = (DWORD)obj;
    DWORD *head = (DWORD *)LHAlloc(16);
    if (head != 0)
    {
        head[0] = (DWORD)head;
        head[1] = (DWORD)head;
    }
    l->head = (DWORD *)head;
    l->size = 0;
    return obj;
}

// sub_76C110 (0x76C110 - 0x76C176): release a plain list's entries
//
//  void F3ListEntriesRelease(void *obj)
//
//  thiscall (plain retn). Resets the list to empty (head[0] = head[1] =
//  head, size = 0), then walks the entry chain from the original first
//  entry to the head, releasing each entry's ref block (InterlockedDecrement
//  at +0x0C, the [block+0x04](block+0x08) release on zero, the LHFree),
//  zeroing the entry's +0x08/+0x0C words, and LHFreeing the entry.
//  [NOT byte-identical] placement-dependent.
static void F3ListEntriesRelease(void *obj)
{
    F3List28 *l = (F3List28 *)obj;
    if (l->head == 0)
        return;                       // guard (the original is unguarded)
    DWORD *head = (DWORD *)l->head;
    DWORD *e = (DWORD *)(DWORD)head[0];
    head[0] = (DWORD)head;
    head[1] = (DWORD)head;
    l->size = 0;
    while (e != head)
    {
        DWORD next = e[0];
        F3ReleaseRefBlock(e[3]);
        e[2] = 0;                     // +0x08
        e[3] = 0;                     // +0x0C
        LHFree((void *)e);
        e = (DWORD *)(DWORD)next;
    }
}

// sub_76C5D0 (0x76C5D0 - 0x76C5F4): the 28-byte plain list dtor
//
//  void F3ListStructDtor16(void *obj)
//
//  thiscall (plain retn). F3ListEntriesRelease, then LHFree the head block
//  (slot zeroed) and the node block.
//  [NOT byte-identical] placement-dependent.
static void F3ListStructDtor16(void *obj)
{
    F3List28 *l = (F3List28 *)obj;
    F3ListEntriesRelease(l);
    DWORD head = (DWORD)l->head;
    l->head = 0;
    LHFree((void *)head);
    DWORD node = l->node;
    l->node = 0;
    LHFree((void *)node);
}

// sub_76A020 (0x76A020 - 0x76A055): reset a plain list, freeing its entries
//
//  void *F3ListReset(void *obj)
//
//  thiscall (plain retn), returns obj. Resets the list to empty (head[0] =
//  head[1] = head, size = 0), then walks the chain LHFreeing each 4-byte
//  entry block until the head. (The ref blocks are NOT released here --
//  callers hold them.)
//  [NOT byte-identical] relative call to LHFree.
static void *F3ListReset(void *obj)
{
    F3List28 *l = (F3List28 *)obj;
    if (l->head == 0)
        return obj;                   // guard (the original is unguarded)
    DWORD *head = (DWORD *)l->head;
    DWORD *e = (DWORD *)(DWORD)head[0];
    head[0] = (DWORD)head;
    head[1] = (DWORD)head;
    l->size = 0;
    while (e != head)
    {
        DWORD next = *e;
        LHFree((void *)e);
        e = (DWORD *)(DWORD)next;
    }
    return obj;
}

// sub_76C420 / sub_76C380 (0x76C420 / 0x76C380): the recursive tree-node
// destructor (the MSVC std::set sentinel walk)
//
//  void F3ListTreeDtor(void *node)
//
//  stdcall, 1 arg (retn 4). While the node's is_nil byte (+0x19) is 0,
//  recurse on the right child (node+0x08), release the node's ref block at
//  +0x14 (the InterlockedDecrement / [block+4](block+8) / LHFree idiom),
//  zero the node's +0x10/+0x14 words, LHFree the node, and continue up the
//  parent chain (node+0x00). The sentinel's +0x19 byte is 1 (the list
//  ctors set it), so the walk stops there. The original reads +0x19 past
//  the end of the 16-byte plain-list heads; the plain heads are built
//  28-byte here (see F3ListStructCtor16's note) so the read is in-bounds
//  and the walk stops at the head exactly as it does on the original heap.
//  [NOT byte-identical] placement-dependent.
static void F3ListTreeDtor(void *node)
{
    DWORD *i = (DWORD *)node;
    while (*((BYTE *)i + 0x19) == 0)
    {
        DWORD *parent = (DWORD *)(DWORD)i[0];
        F3ReleaseRefBlock(i[5]);
        i[4] = 0;                     // +0x10
        i[5] = 0;                     // +0x14
        LHFree((void *)i);
        i = parent;
    }
}

// sub_76E980 (0x76E980 - 0x76EA60): release a 28-byte tree-list range
//
//  void *F3ListRangeRelease(void *obj, void *out, DWORD node, DWORD head0,
//                           DWORD node2, DWORD head)
//
//  thiscall + 5 stack args (retn 0x14), returns out. The list is obj
//  (F3ListTree28); the args are sanity-checked against node / head[0] /
//  head. When the range covers the whole list (head == head[0]), it runs
//  F3ListTreeDtor on head[1] (releasing every entry's ref block), resets
//  the list to empty (head[1] = head[0] = head[2] = head, size = 0), and
//  stores out = {0, head[0]} then out[0] = node. Otherwise it walks the
//  range erasing each entry from the source (PackageEntryAdvance +
//  F3ListEraseRange, sub_76DEF0) and stores out = {node, end}. In this
//  build only the whole-list path executes (the lists are empty).
//  [NOT byte-identical] placement-dependent.
static void *F3ListRangeRelease(void *obj, void *out, DWORD node, DWORD head0,
                                DWORD node2, DWORD head)
{
    F3ListTree28 *l = (F3ListTree28 *)obj;
    (void)node; (void)node2; (void)head0;
    if (head == (DWORD)l->head && head == l->head[0])
    {
        F3ListTreeDtor((void *)l->head[1]);
        l->head[1] = (DWORD)l->head;
        l->size = 0;
        l->head[0] = (DWORD)l->head;
        l->head[2] = (DWORD)l->head;
        *(DWORD *)out = 0;
        *(DWORD *)((char *)out + 4) = l->head[0];
        *(DWORD *)out = l->node;
        return out;
    }
    // [remaining stub] the range-walk path (PackageEntryAdvance /
    // F3ListEraseRange) is unreachable in this build (the lists are empty).
    *(DWORD *)out = l->node;
    *(DWORD *)((char *)out + 4) = (DWORD)l->head;
    return out;
}

// sub_76FB50 (0x76FB50 - 0x76FB8D): the 28-byte tree-list dtor (the
// F3ListStructCtor family, head@+0x18 / size@+0x1C)
//
//  void F3ListStructDtor28(void *obj)
//
//  thiscall (plain retn). F3ListRangeRelease(obj, &out, node, head[0],
//  node, head) (sub_76E980), then LHFree the head block (head slot and
//  size zeroed) and the node block.
//  [NOT byte-identical] placement-dependent.
static void F3ListStructDtor28(void *obj)
{
    F3ListTree28 *l = (F3ListTree28 *)obj;
    DWORD out[2] = { 0, 0 };
    if (l->head != 0)
        F3ListRangeRelease(l, out, l->node, l->head[0], l->node,
                           (DWORD)l->head);        // guard (original unguarded)
    LHFree((void *)l->head);
    l->head = 0;
    l->size = 0;
    LHFree((void *)l->node);
    l->node = 0;
}

// sub_76FCB0 (0x76FCB0 - 0x76FD0A) + sub_76FB10 (0x76FB10 - 0x76FB4D):
// copy a source plain list into the destination
//
//  void *F3ListCopyRange(void *out, void *srcList)
//
//  stdcall, 2 args. The original's six-arg shape is (out, node, head[0],
//  node, head, dest): it walks the source from head[0] to head, splicing
//  each entry's payload into the destination (F3ListRangeSplice on
//  dest's node/head), then stores out = dest. The walk is a no-op for an
//  empty source (the test path), so the splice helper is not needed here.
//  [NOT byte-identical] placement-dependent.
static void *F3ListCopyRange(void *out, void *srcList)
{
    (void)srcList;
    // The original sub_76FCB0 splices the source entries into the
    // destination in place (via sub_76FB10) and returns the destination
    // pointer WITHOUT writing to it (`mov eax, edi; retn`). For an empty
    // source (the test path) the splice is a no-op, so the destination is
    // left untouched. Writing to *out here would clobber the destination's
    // node pointer (see sub_766B60's call: out is the local list, and its
    // node/head must survive the later F3ListStructDtor16 teardown).
    return out;
}

// 0x1DC4A90..0x1DC4AA0 -- the offerData.mkt XML name slots. The original
// initializes them in its CRT static ctors (sub_1A5F220 / sub_1A5F240 /
// sub_1A5F260 / sub_1A5F280 / sub_1A5F2A0: SetSharedSlot(slot, name, -1)
// + an atexit ReleaseSharedSlot each) and releases them at exit (sub_1AC2D90
// .. sub_1AC2DD0). They image-initialize to 0xFFFFFFFF (an unaddressable
// slot value), so they are only ever reachable through those ctors; this
// build mirrors the ctor half in a static initializer (the atexit releases
// are elided). The slots hold: "marketplace" (root element), "offer" (item
// element), "offerId" / "packageCollection" / "licenseMask" (attributes).
static F3SharedBuffer *g_dword_1DC4A90 = 0;
static F3SharedBuffer *g_dword_1DC4A94 = 0;
static F3SharedBuffer *g_dword_1DC4A98 = 0;
static F3SharedBuffer *g_dword_1DC4A9C = 0;
static F3SharedBuffer *g_dword_1DC4AA0 = 0;

struct F3OfferDataSlotsInit
{
    F3OfferDataSlotsInit()
    {
        SetSharedSlot(&g_dword_1DC4A90, "marketplace", 0xFFFFFFFF);
        SetSharedSlot(&g_dword_1DC4A94, "offer", 0xFFFFFFFF);
        SetSharedSlot(&g_dword_1DC4A98, "offerId", 0xFFFFFFFF);
        SetSharedSlot(&g_dword_1DC4A9C, "packageCollection", 0xFFFFFFFF);
        SetSharedSlot(&g_dword_1DC4AA0, "licenseMask", 0xFFFFFFFF);
    }
};
static F3OfferDataSlotsInit g_offerDataSlotsInit;

// sub_717C20 (0x717C20 - 0x717C26): return &dword_1DC27D4 (the caller
// discards the result -- it is a marker store in the original's context).
static DWORD *GetDword1DC27D4(void)
{
    static DWORD g_dword_1DC27D4 = 0xFFFFFFFF;
    return &g_dword_1DC27D4;
}

// The 28-byte front-end wide string sub_5AF180 builds from an ANSI literal
// (the package-collection filename / path builder): ptr@+0x00 (the heap
// buffer when cap >= 8), 8-wchar SSO buffer@+0x04, len@+0x14, cap@+0x18.
typedef struct F3WString28
{
    DWORD   ptr;      // +0x00
    wchar_t sso[8];   // +0x04 (16 bytes, ends at +0x14)
    DWORD   pad0;     // +0x14 (len)
    DWORD   cap;      // +0x18
} F3WString28;       // 28 bytes
typedef char F3WString28SizeCheck[(sizeof(F3WString28) == 28) ? 1 : -1];

// sub_5AF180 (0x5AF180 - 0x5AF2C0?): build the 28-byte wide string from an
// ANSI source (mbstowcs into the SSO buffer; a heap buffer + ptr when the
// length exceeds 7 wchars). [NOT byte-identical] placement-dependent
// (relative calls to the CRT / LHAlloc).
static F3WString28 *F3WString28FromAnsi(F3WString28 *s, const char *src)
{
    wchar_t dst[0x200];
    int n = (int)mbstowcs(dst, src, 0x200);
    if (n < 0)
        n = 0;
    if ((unsigned)n > 7)
    {
        s->ptr = (DWORD)LHAlloc(2 * (n + 1));
        s->cap = (DWORD)(n + 1);
        wchar_t *p = (wchar_t *)s->ptr;
        for (int i = 0; i <= n; i++)
            p[i] = (i < n) ? dst[i] : 0;
        s->sso[0] = 0;
    }
    else
    {
        s->ptr = 0;
        s->cap = 7;
        for (int i = 0; i <= n; i++)
            s->sso[i] = (i < n) ? dst[i] : 0;
    }
    s->pad0 = (DWORD)n;
    return s;
}

// The F3WString28 free idiom ("if (cap >= 8) LHFree(ptr)")
static void F3WString28Free(F3WString28 *s)
{
    if (s->cap >= 8)
        LHFree((void *)s->ptr);
}

// sub_765990 (0x765990 - 0x765BE0?): import the enumerated package
// collections into the CPackageData sub-object
//
//  int CPackageDataImportCollections(void *obj, void *buffer, int count,
//                                    void *list15)
//
//  thiscall + 3 stack args (retn 0Ch), returns 0. When count == 0 it
//  returns immediately (the test path -- XContent enumeration produces no
//  entries). Otherwise it resets the CPackageData+0x24 package list
//  (F3ListReset) and, for each 32-byte buffer entry, builds a collection
//  entry (CPackageDataBuildCollectionEntry / sub_765330), reads its
//  package_collection_info.xml (falling back to package_collection_info.xmb
//  through the entry's vtable slot 4), validates it (sub_EED360), and on
//  success splices it into the list at a size+1 key. The loop body is
//  unreachable in this build.
//  [NOT byte-identical] placement-dependent.
static int CPackageDataImportCollections(void *obj, void *buffer, int count,
                                         void *list15)
{
    (void)list15;
    if (count == 0)
        return 0;
    F3ListReset((char *)obj + 0x24);
    for (int i = count; i > 0; --i, buffer = (char *)buffer + 32)
    {
        // [remaining stub] the per-entry import (sub_765330 + the
        // package_collection_info read / validate / splice) is unreachable
        // in this build (count is always 0).
        CPackageDataBuildCollectionEntry(obj, (int *)0, 0, buffer);
    }
    return 0;
}

// sub_765330 (0x765330 - 0x7654AE): build one package-collection entry from
// an enumerated buffer entry. [remaining stub] no-op in this build (the
// import loop never runs).
static void CPackageDataBuildCollectionEntry(void *obj, int *out, int a3,
                                             void *bufferEntry)
{
    (void)obj; (void)a3; (void)bufferEntry;
    *out = 0;
}

// sub_764AB0 (0x764AB0 - 0x764EE0?): merge the imported package entries into
// the package-data lists
//
//  int CPackageDataMergeCollections(int slot, void *list15, void *list16,
//                                   void *list17)
//
//  stdcall, 4 args (retn 0x10), returns 0. Walks list15's tree entries; for
//  each collection it walks the container's package entries, ref-counting
//  the ref pair, and when the slot flags admit the entry (sub_EEBAA0 /
//  sub_EEC6B0 & sub_EEB8A0) it searches list17 for a matching id
//  (PackageEntryGetId) and either drops the newer entry (splicing it out
//  through F3ListSplicePair / F3ListSplicePairErase, sub_76C180 / sub_76C050)
//  or splices the entry into list17 / list16 (F3ListRangeSplice / sub_76E660)
//  per the entry's flags. An empty list15 (the test path) returns 0.
//  [NOT byte-identical] placement-dependent.
static int CPackageDataMergeCollections(int slot, void *list15, void *list16,
                                        void *list17)
{
    (void)slot; (void)list16; (void)list17;
    F3ListTree28 *l = (F3ListTree28 *)list15;
    if (l->head == 0 || l->head[0] == (DWORD)l->head)
        return 0;                     // empty list15 -- the test path
    // [remaining stub] the per-entry merge (the container walk, the id
    // match against list17, and the splices) is unreachable in this build
    // (list15 is always empty -- XContent enumeration produces no entries).
    return 0;
}

// sub_765BB0 (0x765BB0 - 0x765E40?): reconcile the package list against the
// CPackageData+0x04 entries
//
//  int CPackageDataCompareCollections(void *obj, void *list15)
//
//  thiscall + 1 stack arg (retn 4), returns 1. An empty list15 (the test
//  path) returns 1 immediately. Otherwise it walks list15's entries, and
//  for each it searches the CPackageData+0x04 list for a matching interned
//  name (sub_EEC630 + the [block+4] word compare + SpeechBankStrCompare); a match
//  releases / re-binds the entry's ref pair (vt[1] / vt[9], sub_7649C0,
//  sub_EEC700) and, when the entry's flag byte (+0x14 bits 0/1) is set,
//  moves the pair into the entry (sub_EED4C0 / sub_EED4D0); an unmatched
//  entry copies its ref pair in (sub_76F7C0 / sub_768E80).
//  [NOT byte-identical] placement-dependent.
static int CPackageDataCompareCollections(void *obj, void *list15)
{
    (void)obj;
    F3ListTree28 *l = (F3ListTree28 *)list15;
    if (l->head == 0 || l->head[0] == (DWORD)l->head)
        return 1;                     // empty list15 -- the test path
    // [remaining stub] the per-entry reconcile is unreachable in this build
    // (list15 is always empty).
    return 1;
}

// sub_766740 (0x766740 - 0x766E00?): write the VFS config for the package
// set
//
//  int CPackageDataWriteConfig(void *obj, void *listA, void *fileSubsys)
//
//  thiscall + 2 stack args (retn 8), returns the accumulated flag (0 on
//  the test path). It builds a local 28-byte tree work list (sub_76C600 /
//  the F3ListStructCtor family), then the first loop walks listA (empty
//  -> break) tracking whether any entry's flag byte +0x15 bit 0 is set
//  (sub_EEB940; it would then run sub_717C20 + ReleaseLipCultures). The
//  second loop walks the local tree list's entries: for each, it looks the
//  id up in the CPackageData+0x04 list (PackageEntryLookup); a match with
//  a clear sub_EEB940 flag loads "startup.vfsconfig" through the file
//  subsystem (sub_1801990 + F3WStringFromAnsi + sub_1801350) and sets the
//  return's bit 0. Finally it folds the local list back together
//  (F3ListRangeMovePair / sub_76E7B0) and frees its head + node. Both
//  loops are empty on the test path, so it returns 0.
//  [NOT byte-identical] placement-dependent.
static int CPackageDataWriteConfig(void *obj, void *listA, void *fileSubsys)
{
    (void)obj; (void)listA; (void)fileSubsys;
    int flags = 0;
    F3ListTree28 localB;
    F3ListStructCtor(&localB, 28, 0x18);       // the sub_76C600 local tree list
    // [remaining stub] the per-entry config write (both loops, the
    // sub_717C20 / ReleaseLipCultures marker, and the startup.vfsconfig
    // load) is unreachable in this build (the local lists are empty).
    F3ListRangeMovePair(&localB, localB.node, localB.head[0],
                        localB.node, (DWORD)localB.head);
    LHFree((void *)localB.head);
    localB.size = 0;
    LHFree((void *)localB.node);
    return flags;
}

// sub_763810 (0x763810 - 0x763A78?): collect the version flags
//
//  int CPackageDataCollectVersionFlags(void *obj, void *listB)
//
//  thiscall + 1 stack arg (retn 4), returns the accumulated flags (0 on
//  the test path). An empty CPackageData+0x04 list (the test path) returns
//  the accumulator immediately. Otherwise it walks the CPackageData+0x04
//  entries and their containers; for each package entry it searches listB
//  for a matching id with the sub_EEB910 version compare and a set
//  SpeechBankStrCompare flag (PackageEntryGetFlag24) and, on a match, ORs bit 1 into
//  the accumulator (sub_EEB8C0(1)).
//  [NOT byte-identical] placement-dependent.
static int CPackageDataCollectVersionFlags(void *obj, void *listB)
{
    (void)listB;
    F3ListTree28 *l = (F3ListTree28 *)((char *)obj + 0x04);
    int flags = 0;
    if (l->head == 0 || l->head[0] == (DWORD)l->head)
        return flags;                // empty CPackageData+0x04 list -- test path
    // [remaining stub] the per-entry flag collection is unreachable in this
    // build (the CPackageData+0x04 list is empty at front-end entry).
    return flags;
}

// sub_76E7B0 (0x76E7B0 - 0x76E890): release a 28-byte tree-list range
// (the sub_766740 local-list fold)
//
//  void *F3ListRangeMovePair(void *obj, DWORD node, DWORD head0, DWORD node2,
//                            DWORD head)
//
//  thiscall + 4 stack args (retn 0x14), returns obj. The list is obj
//  (F3ListTree28); the args are sanity-checked against node / head[0] /
//  head. When the range covers the whole list (head == head[0], the test
//  path), it runs F3ListTreeDtor on head[1] (releasing every entry's ref
//  block), resets the list to empty (head[1] = head[0] = head[2] = head,
//  size = 0), and stores the out pair = {0, head[0]} then out[0] = node.
//  The other path walks the range splicing each entry (PackageEntryAdvance
//  + F3ListSplice, sub_76DB90).
//  [NOT byte-identical] placement-dependent.
static void *F3ListRangeMovePair(void *obj, DWORD node, DWORD head0,
                                 DWORD node2, DWORD head)
{
    F3ListTree28 *l = (F3ListTree28 *)obj;
    (void)node; (void)node2;
    if (head == (DWORD)l->head && head == l->head[0])
    {
        F3ListTreeDtor((void *)l->head[1]);
        l->head[1] = (DWORD)l->head;
        l->size = 0;
        l->head[0] = (DWORD)l->head;
        l->head[2] = (DWORD)l->head;
        return obj;
    }
    // [remaining stub] the range-walk path (PackageEntryAdvance /
    // F3ListSplice) is unreachable in this build (the lists are empty).
    return obj;
}

// sub_766B60 (0x766B60 - 0x766F80?): build the version-table entries from
// the package set
//
//  int CPackageDataBuildVersionEntries(void *obj, int slot, void *list15,
//                                      void *list16, void *list17, int flag1,
//                                      int flag2)
//
//  thiscall + 6 stack args (retn 0x18), returns the accumulated flag (0 on
//  the test path). It bumps the CPackageData+0x00 counter, builds two 28B
//  plain work lists (node + 16-byte head each), copies list17 into the
//  second (F3ListCopyRange), then walks list16's entries: for each it
//  looks the id up in the CPackageData+0x04 list (PackageEntryLookup) and
//  runs the sub_763A80 id match against the second work list; an unmatched
//  / flag-24-clear entry either sets bit 0x20 (the sub_EEB9A0 match, when
//  slot bit 2 + sub_EEB9A0 hold) or bit 0x08, or sets bit 0x01 and splices
//  the entry into the first work list (F3ListRangeSplice); a matched entry
//  with the sub_EEB910 version compare set is dropped, and one with
//  sub_EEB8E0 set adds bit 0x08 (sub_768D70 releases the pair). When
//  flag1 || flag2 it runs CPackageDataCompareCollections(list15), and for
//  flag1 CPackageDataWriteConfig (early-exit when the result | v31 has bit
//  0x80) and for flag2 CPackageDataCollectVersionFlags, ORing the results
//  together. Finally it tears down both work lists (F3ListEntriesRelease +
//  the node / head LHFree) and returns the flag. Both loops are empty on
//  the test path, so it returns CPackageDataCollectVersionFlags's 0.
//  [NOT byte-identical] placement-dependent.
static int CPackageDataBuildVersionEntries(void *obj, int slot, void *list15,
                                           void *list16, void *list17,
                                           int flag1, int flag2)
{
    F3List28 *l16 = (F3List28 *)list16;
    (void)l16; (void)list17;
    *(int *)obj += 1;                 // the CPackageData+0x00 counter

    F3List28 localA, localB;
    F3ListStructCtor16(&localA);
    F3ListStructCtor16(&localB);
    F3ListEntriesRelease(&localA);    // sub_76C110 on the fresh localA (no-op)
    F3ListCopyRange(&localB, list17); // sub_76FCB0 -- empty source, no-op

    // [remaining stub] the per-entry version build (the list16 walk, the
    // PackageEntryLookup / sub_763A80 match, the flag accumulation, and the
    // splices) is unreachable in this build (list16 is always empty).

    int result = 0;
    if (flag1 != 0 || flag2 != 0)
    {
        CPackageDataCompareCollections(obj, list15);
        if (flag1 != 0)
        {
            void *fs = GetFileSubsystem(0);          // sub_1801C20
            int w = CPackageDataWriteConfig(obj, &localA, fs);
            result = w | 0;
            if ((result & 0x80) != 0)
                goto cleanup;
        }
        else
            result = 0;
        if (flag2 != 0)
            result |= CPackageDataCollectVersionFlags(obj, &localB);
    }
    goto cleanup;
cleanup:
    F3ListStructDtor16(&localB);
    F3ListStructDtor16(&localA);
    return result;
}

// sub_7654B0 (0x7654B0 - 0x7657E2): parse the marketplace offer data
//
//  int CPackageDataLoadOfferData(void *obj)
//
//  thiscall (plain retn). Returns 128 when the archive read fails, the
//  ReopenXmlFileBlock parse fails, or the root-section check fails; 0
//  otherwise (attribute parse failures set a separate local, not the
//  return). It takes the archive manager out of its slot (GetArchiveSlot),
//  builds the "miscellaneous\\offerData.mkt" wide string (sub_5AF180), and
//  reads it through the archive's vtable slot 4 (a null slot -- the test
//  path, g_dword_1E2D864 is 0 -- yields no data and returns 128). On
//  success it CXMLReadStreamCtor's the stream, ReopenXmlFileBlock's the
//  file block, XmlCheckRootSection's against the "marketplace" slot, and
//  (when found) walks the "offer" items reading their "offerId" /
//  "packageCollection" / "licenseMask" attributes (XmlReadAttrInterned,
//  sub_17DC4B0), resolving
//  the two date strings (sub_1A00C80 hex parse), and splicing an 8-byte
//  encoded-date record into the CPackageData+0x9C list (sub_76F9E0). The
//  teardown releases the file block (vtable slot 2), the CXMLReadStream,
//  and the read-out handle.
//  [NOT byte-identical] placement-dependent.
static int CPackageDataLoadOfferData(void *obj)
{
    (void)obj;
    int result = 0;
    F3SharedBuffer *slot = 0;
    GetArchiveSlot(&slot);
    F3WString28 name;
    F3WString28FromAnsi(&name, "miscellaneous\\offerData.mkt");

    DWORD fileData = 0, fileHandle = 0;
    if (slot != 0)
    {
        void **vt = (void **)(*(void **)slot);
        (*(void (__thiscall *)(void *, DWORD *, void *, int))vt[4])
            (slot, &fileData, &name, 1);
    }
    else
        fileData = 0;                // the original dereferences the slot unconditionally
    F3WString28Free(&name);

    if (fileData == 0)
    {
        result = 128;                // no archive data -- the test path
        goto done;
    }

    F3XmlStream stream;
    CXMLReadStreamCtor(&stream);
    if (ReopenXmlFileBlock(&stream, (void *)fileData) == 0)
    {
        result = 128;
        goto done2;
    }
    if (XmlCheckRootSection(&stream, GetSlotString(&g_dword_1DC4A90)) == 0)
    {
        result = 128;
        goto done2;
    }
    // [remaining stub] the "offer" item walk (sub_17DC640 / sub_17DC140 /
    // XmlReadAttrInterned (sub_17DC4B0) / sub_1A00C80 / sub_76F9E0) is
    // unreachable in this build
    // (the LHXML parse is a stub, so the root section is never found).
done2:
    if (fileData != 0)
        F3CallVtable((void *)fileData, 2);         // the file block Release
    CXMLReadStreamDtor(&stream);
    if (fileHandle != 0)
        F3CallVtable((void *)fileHandle, 2);       // the read-out handle Release
done:
    return result;
}

// sub_767160 (0x767160 - 0x7672DA): the package-collection load
//
//  int CPackageManagerLoadPackageCollections(void *obj, int slot, int flag1,
//                                            int flag2)
//
//  thiscall + 3 stack args (retn 0Ch). Bumps the CPackageManager+0x28
//  counter, then (with this = the CPackageData sub-object at
//  CPackageManager+0x2C) it XContentEnumeratePackageCollections's up to 16
//  collections into a 0x204-byte zeroed buffer (count out), CPackageDataImportCollections's
//  them into the 28B tree list (F3ListStructCtor28 / sub_76C680), builds
//  two 28B plain work lists (F3ListStructCtor16), CPackageDataMergeCollections's,
//  CPackageDataBuildVersionEntries's (with slot / flag1 / flag2), and on
//  success CVersionTableRefresh's the game engine's version table
//  (GetGameEngine + GetGameEngineVersionTable) and CPackageDataLoadOfferData's.
//  Any negative step (0x80 error flag) tears down the two work lists + the
//  tree list (F3ListStructDtor16 x2 + F3ListStructDtor28) and returns 128;
//  the success path tears them down the same way and returns the OR of the
//  step results.
//  [NOT byte-identical] placement-dependent.
static int CPackageManagerLoadPackageCollections(void *obj, int slot, int flag1,
                                                int flag2)
{
    DWORD *m = (DWORD *)obj;
    m[0x28 / 4] += 1;                // the CPackageManager+0x28 counter
    void *cpd = *(void **)((char *)obj + 0x2C);   // the CPackageData sub-object

    BYTE   buffer[0x204];
    DWORD  count = 0;
    F3ListTree28 list15;            // the 28B tree list (F3ListStructCtor family)
    F3ListStructCtor(&list15, 28, 0x18);
    F3List28 list16, list17;
    F3ListStructCtor16(&list16);
    F3ListStructCtor16(&list17);

    int r = XContentEnumeratePackageCollections(buffer, &count, 16);
    int err = (r < 0) ? 0x80 : 0;
    if (err == 0)
    {
        int ir = CPackageDataImportCollections(cpd, buffer, (int)count,
                                               &list15);
        err = (ir < 0) ? 0x80 : 0;
    }
    if (err == 0)
    {
        int mr = CPackageDataMergeCollections(slot, &list15, &list16, &list17);
        err = (mr < 0) ? 0x80 : 0;
    }
    int result = 0;
    if (err == 0)
    {
        result = CPackageDataBuildVersionEntries(cpd, slot, &list15, &list16,
                                                 &list17, flag1, flag2);
        if (result < 0)
            err = 0x80;
    }
    if (err == 0)
    {
        CVersionTableRefresh(GetGameEngineVersionTable(GetGameEngine()));
        int or_ = CPackageDataLoadOfferData(cpd);
        if (or_ < 0)
            err = 0x80;
        else
            result |= or_;
    }

    F3ListStructDtor16(&list17);
    F3ListStructDtor16(&list16);
    F3ListStructDtor28(&list15);
    return (err != 0) ? 128 : result;
}

// sub_7672E0 (0x7672E0 - 0x7672EC): the package-collection load thunk
//
//  int CPackageManagerLoadCollections(void *obj)
//
//  thiscall (plain retn): push 1; push 1; push 0x0B; call sub_767160 --
//  passes (this, slot = 11, flag1 = 1, flag2 = 1) through to
//  CPackageManagerLoadPackageCollections. The only caller is RunFrontEnd
//  (0x61D3AB), which constructs the new CPackageManager (LHAlloc(0x30) +
//  CPackageManagerCtor) into dw[0xAC/4] and then dispatches it here.
//  [NOT byte-identical] relative call to the loader.
static int CPackageManagerLoadCollections(void *obj)
{
    return CPackageManagerLoadPackageCollections(obj, 11, 1, 1);
}

// ============================================================
//  CVersionTableGetVersion1  (0x760FA0 - 0x760FA4)  -- the first
//  CGameVersion sub-object (CV1, +0x04) of the version table
//
//  void *CVersionTableGetVersion1(void *obj)   (obj = the CVersionTable,
//  in ECX in the original)
//
//  thiscall (plain retn). The 4-byte accessor `lea eax, [ecx+4]; retn` --
//  no calls, no globals, no absolute refs.
//
//  [BYTE-IDENTICAL] placement-independent (no calls / relative jumps /
//  high-VA refs). `__declspec(naked)` inline asm so /Ob2 doesn't swap the
//  out-of-line body for a 5-byte stack-argument thunk. The table's CV1
//  (+0x04) is the local machine's package version (flag 0x35 after a
//  refresh); the sub_760FC0 / sub_761CC0 / CGameVersionCopyFrom
//  version-compare / copy family takes it. Sibling accessors: sub_760FB0
//  returns CV3 (+0xBC).
// ============================================================
__declspec(naked)
void *CVersionTableGetVersion1(void *obj)
{
    __asm {
        lea eax, [ecx+4]
        retn
    }
}

// ============================================================
//  StageLoadMasteringProfiles  (0x14611B0 - 0x14611EA)  -- stage the
//  8-byte CLoadMasteringProfiles command object into the main (80/4096)
//  allocation cache (g_unk_1DDC830, built by InitVirtualFileSystem)
//
//  int StageLoadMasteringProfiles(const char *source)   -- cdecl
//
//  Stage an 84-byte pool slot (VfsAllocCacheAlloc, sub_1473D70, wait = 1):
//  on success install the CLoadMasteringProfiles vtable, null-ctor the +4
//  path slot (InitSharedSlot) and store the source path (SetSaveGamePath),
//  then finalize (VfsAllocCacheFinalize, sub_1473E20 -- the original is a
//  tail call): the staged object's ICommand slot 1 reports its 8-byte size,
//  which fits the 84-byte element, so the slot is returned to the pool
//  (VfsAllocCacheFree) and the cache critical section released.  Returns 0
//  when the pool is not initialised (VfsAllocCacheAlloc returns 0 -- the
//  only path live in this build, where g_unk_1DDC830 is zero BSS because
//  InitVirtualFileSystem is never reached, the archive slot being null).
//  [NOT byte-identical] placement-dependent (relative calls to the pool /
//  slot callees) and refs the high-VA globals g_unk_1DDC830 (0x01DDC830) and
//  the CLoadMasteringProfiles vtable at its original VA (0x01B6F8D0).
//
//  The lambda class is `NLHAudio::NFramework::NComms::
//  LoadMasteringProfiles(char const *)'::`2'::CLoadMasteringProfiles` -- an
//  8-byte ICommand: {+0 vtable, +4 the path slot (an F3SharedBuffer
//  pointer)}.  Slot 0 is the deleting destructor (sub_1463730, thiscall
//  retn 4, the free flag on the stack): release the path slot, overwrite
//  the vtable with ICommand's (0x01B6F458), LHFree the object when the free
//  flag is set, return this.  Slot 1 (sub_14623C0) reports the object size
//  (8).  Slot 2 (sub_1460510) executes the command through the process
//  delegate g_dword_1DDC820 (sub_146E150 with the path slot as argument) --
//  [STUB] the delegate invoke is unimplemented here, treated as its 0
//  return value.  Slot 3 is the rdata
//  entry 0x01C41624 at its original VA (the next vtable at 0x01B6F8E0 belongs
//  to a neighboring lambda class: dtor sub_1463760 / size sub_14623E0 /
//  exec sub_1460520 / rdata 0x01C41674).
static void *CLoadMasteringProfilesDtor(void *obj, int free)
{
    ReleaseSharedSlot((F3SharedBuffer **)((DWORD *)obj + 1));
    *(DWORD *)obj = 0x01B6F458;   // &NLHAudio::NThreading::ICommand::`vftable' (original VA)
    if (free & 1)
        LHFree(obj);
    return obj;
}
static int CLoadMasteringProfilesSize(void) { return 8; }     // sub_14623C0 / sub_14623E0
static int CLoadMasteringProfilesExec(void *path)             // sub_1460510 / sub_1460520 (stub)
{ (void)path; return 0; }
static const void *kCLoadMasteringProfilesVftable[] = {
    (const void *)CLoadMasteringProfilesDtor,   // 0x1463730
    (const void *)CLoadMasteringProfilesSize,   // 0x14623C0
    (const void *)CLoadMasteringProfilesExec,   // 0x1460510
    (const void *)0x01C41624                     // rdata entry, original VA
};

static int StageLoadMasteringProfiles(const char *source)
{
    DWORD *obj = (DWORD *)VfsAllocCacheAlloc(g_unk_1DDC830, 1);
    if (obj != 0) {
        obj[0] = (DWORD)kCLoadMasteringProfilesVftable;
        InitSharedSlot((F3SharedBuffer **)(obj + 1));
        SetSaveGamePath((F3SharedBuffer **)(obj + 1), source);
        return VfsAllocCacheFinalize(g_unk_1DDC830);
    }
    return 0;
}

//  LoadMasteringProfiles  (0x145D870 - 0x145D88B)  -- the NLHAudio::
//  NFramework::NComms::LoadMasteringProfiles(char const*) stdcall wrapper
//  (the ECX the caller stages is ignored):
//
//  int LoadMasteringProfiles(const char *source)   -- stdcall, retn 4
//
//  Publish the profile path into the global slot (dword_1DDC818) via
//  SetSaveGamePath, then stage the loader command
//  (StageLoadMasteringProfiles) and return its result.  Sole caller:
//  LoadAudioMasteringProfiles (sub_18FA3F0).
//  [NOT byte-identical] placement-dependent (relative calls to
//  SetSaveGamePath / StageLoadMasteringProfiles) and refs the high-VA global
//  g_dword_1DDC818 (0x01DDC818).
static int LoadMasteringProfiles(const char *source)
{
    SetSaveGamePath(&g_dword_1DDC818, source);
    return StageLoadMasteringProfiles(source);
}

// ============================================================
//  LoadAudioMasteringProfiles  (0x18FA3F0 - 0x18FA42D)  -- load the audio
//  mastering profiles from "audio/profiles.amp"
//
//  int LoadAudioMasteringProfiles(void)   -- cdecl (plain retn)
//
//  The compiler-generated empty local (the identity ctor sub_145D7C0
//  `mov eax, ecx; retn` + the nullsub_124 `retn` dtor on a 1-byte stack
//  object) is side-effect free and omitted, as is the __security_cookie
//  frame check.  Sets the profile path (the global slot g_dword_1DDC818)
//  and stages the CLoadMasteringProfiles ICommand into the audio-comms
//  allocation pool (LoadMasteringProfiles, sub_145D870), with the literal
//  "audio/profiles.amp" (0x01BD359C); returns its result (0 while the pool
//  is uninitialised).  Callers: RunFrontEnd (0x61D422 -- right after the
//  Havok aligned-alloc/free thunks are stored in off_1D8C850 / off_1D8C854)
//  and the vtable thunk 0x619B00 (both discard the value); replaces the
//  sub_18FA3F0 no-op stub in RunFrontEnd.
//  [NOT byte-identical] placement-dependent (relative calls to
//  LoadMasteringProfiles + the __security_check_cookie IAT slot) and refs
//  the high-VA "audio/profiles.amp" string (0x01BD359C) at its original
//  address.
// ============================================================
static int LoadAudioMasteringProfiles(void)
{
    return LoadMasteringProfiles("audio/profiles.amp");
}

// ============================================================
//  CApplyMasteringProfile / StageApplyMasteringProfile / ApplyMasteringProfile /
//  LoadApplyMasteringProfile  (0x18FB930 - 0x18FB9AC frontend, 0x145D890 stdcall
//  wrapper, 0x14611F0 staging, 0x1473AB0 slot-string hash, 0x1B6F8E0 vftable)
//
//  The CApplyMasteringProfile lambda class is the sibling of the CLoadMasteringProfiles
//  one (0x14611B0): the NLHAudio::NFramework::NComms::ApplyMasteringProfile
//  (char const *) lambda builds an 8-byte ICommand {vtable, 4-byte payload} instead
//  of the 8-byte {vtable, path slot} of CLoadMasteringProfiles.  The payload is the
//  FNV hash (seed 0x811C9DC5, prime 0x01000193) of the source string -- computed in
//  this build as the seed only, because the compiler-generated identity
//  copy-then-release dance inside the hash helper nulls the temp slot before the
//  hash loop (the slot is nulled and re-released on the empty string), so the
//  loop is dead and the seed is the only value the function can return.
static void *CApplyMasteringProfileDtor(void *obj, int free)
{
    *(DWORD *)obj = 0x01B6F458;   // &NLHAudio::NThreading::ICommand::`vftable' (original VA)
    if (free & 1)
        LHFree(obj);
    return obj;
}
static int CApplyMasteringProfileSize(void) { return 8; }      // sub_14623E0
static int CApplyMasteringProfileExec(void *path)              // sub_1460520 (stub)
{ (void)path; return 0; }
static const void *kCApplyMasteringProfileVftable[] = {
    (const void *)CApplyMasteringProfileDtor,   // 0x1463760
    (const void *)CApplyMasteringProfileSize,   // 0x14623E0
    (const void *)CApplyMasteringProfileExec,   // 0x1460520
    (const void *)0x01C41674                     // rdata entry, original VA
};

//  HashSlotContent  (0x1473AB0 - 0x1473AF3)  -- the slot-string FNV hash
//  [NOT byte-identical] placement-dependent (relative calls to the identity
//  RefSharedSlot / GetSlotString / ReleaseSharedSlot callees).
//
//  int HashSlotContent(F3SharedBuffer **slot)   -- this: the slot to hash
//
//  The compiler-generated identity copy-then-release dance nulls the temp
//  slot and takes a live-slot reference; the toupper pass and the hash loop
//  are dead (the temp slot holds no block), so the function always returns
//  the FNV seed (0x811C9DC5).  The temp slot is released afterwards (the
//  live-slot count is dropped), balancing the +1 above.
// ============================================================
static int HashSlotContent(F3SharedBuffer **slot)
{
    F3SharedBuffer *v5 = (F3SharedBuffer *)slot;   // v5 = this: the slot pointer
    RefSharedSlot(&v5, &v5);                        // identity copy: null the temp slot, +1 g_Addend
    int hash = (int)0x811C9DC5u;                    // the loop is dead (the slot holds no block)
    (void)GetSlotString(&v5);                       // the dead-path empty-string read
    ReleaseSharedSlot(&v5);                         // drop the counter (the slot is null, no free)
    return hash;
}

//  StageApplyMasteringProfile  (0x14611F0 - 0x146122F)  -- stage the 8-byte
//  CApplyMasteringProfile ICommand into the main (80/4096) allocation cache
//  (g_unk_1DDC830, built by InitVirtualFileSystem, never reached here).
//
//  void StageApplyMasteringProfile(const char *source)   -- cdecl
//
//  On success, install the CApplyMasteringProfile vtable, assign the source
//  into a temp slot, store the slot-string hash as the +4 payload, release the
//  temp slot and finalize the cache.
//  [NOT byte-identical] placement-dependent (relative calls to the pool / slot
//  callees) and refs the high-VA global g_unk_1DDC830 (0x01DDC830) and the
//  CApplyMasteringProfile vtable at its original VA (0x01B6F8E0).
static void StageApplyMasteringProfile(const char *source)
{
    F3SharedBuffer *v2 = 0;
    DWORD *obj = (DWORD *)VfsAllocCacheAlloc(g_unk_1DDC830, 1);
    if (obj != 0) {
        obj[0] = (DWORD)kCApplyMasteringProfileVftable;
        SetSharedSlot(&v2, source, -1);
        obj[1] = (DWORD)HashSlotContent(&v2);
        ReleaseSharedSlot(&v2);
        VfsAllocCacheFinalize(g_unk_1DDC830);
    }
}

//  ApplyMasteringProfile  (0x145D890 - 0x145D8C9)  -- the NLHAudio::
//  NFramework::NComms::ApplyMasteringProfile(char const*) stdcall wrapper
//  (the ECX the caller stages is ignored):
//
//  void ApplyMasteringProfile(F3SharedBuffer **dstSlot, const char *source, int a3)
//
//  Register the global GAME-path slot's block into the caller's temp slot
//  (RefSharedSlot, 0x1A012C0), stage the ApplyMasteringProfile command
//  (StageApplyMasteringProfile, 0x14611F0), and -- when a3 != 0 -- publish the
//  source path into the global slot (dword_1DDC81C) via SetSaveGamePath
//  (0x1A019B0).  Sole caller: LoadApplyMasteringProfile (sub_18FB930).
//  [NOT byte-identical] placement-dependent (relative calls to the slot / pool
//  / SetSaveGamePath callees) and refs the high-VA global g_dword_1DDC81C
//  (0x01DDC81C).
static void ApplyMasteringProfile(F3SharedBuffer **dstSlot, const char *source, int a3)
{
    RefSharedSlot(dstSlot, &g_dword_1DDC81C);
    StageApplyMasteringProfile(source);
    if (a3 != 0)
        SetSaveGamePath(&g_dword_1DDC81C, source);
}

// ============================================================
//  LoadApplyMasteringProfile  (0x18FB930 - 0x18FB9AC)  -- load the GAME
//  mastering profile from "GAME"
//
//  int LoadApplyMasteringProfile(void)   -- cdecl (plain retn)
//
//  The compiler-generated empty local (the identity ctor sub_145D7C0 + the nullsub_124
//  `retn` dtor on a 1-byte stack object) is side-effect free and omitted, as is the
//  __security_cookie frame check.  Copies the global GAME-path slot's block into a
//  local slot (RefSharedSlot), stages the CApplyMasteringProfile ICommand into the
//  audio-comms allocation pool (ApplyMasteringProfile, sub_145D890), with the literal
//  "GAME" (0x01BD35B0), then sets the global slot (dword_1DDC81C) to the new path and
//  releases the copied block: decrements its refcount and frees it on zero.  Returns
//  the live-slot count after the final g_Addend decrement (the original returns the
//  InterlockedDecrement result through the nullsub_124 `retn` dtor).
//  Callers: RunFrontEnd (0x61D701 -- right after the nullsub_29 atexit pair) and the
//  vtable thunk 0x619B10 (both discard the value); replaces the sub_18FB930 no-op
//  stub in RunFrontEnd.
//  [NOT byte-identical] placement-dependent (relative calls to the slot / pool
//  callees + the __security_check_cookie IAT slot) and refs the high-VA "GAME"
//  string (0x01BD35B0) at its original address and the GAME path slot dword_1DDC81C.
// ============================================================
static int LoadApplyMasteringProfile(void)
{
    F3SharedBuffer *v1 = 0;

    ApplyMasteringProfile(&v1, "GAME", 1);

    if (v1 != 0)
    {
        if (InterlockedDecrement(&v1->refcount) <= 0 && v1 != 0)
            SharedBufferFree(v1);
        v1 = 0;
    }
    return (int)InterlockedDecrement(&g_Addend);
}
//  0x619990 (the Havok aligned-allocate thunk) and 0x6199A0 (the aligned-
//  free thunk) are now the real JmpLHAllocAligned / JmpLHFreeAligned in
//  common/common.cpp.

//  WaitForEvent  (0x18C4560 - 0x18C45B3)  -- block on the event stored in
//  the frontend object (handle at obj+0x80; sibling SetFrontendEvent does
//  SetEvent on the same slot, sub_18C45E0 does CloseHandle). Wraps
//  WaitForSingleObjectEx(hEvent, dwMilliseconds, bAlertable != 0) and maps
//  the wait status: 0 = WAIT_TIMEOUT (0x102), 1 = 0xC0, 2 = WAIT_OBJECT_0 /
//  WAIT_ABANDONED, and the raw bAlertable argument otherwise.
//  [NOT byte-identical] the original calls the WaitForSingleObjectEx IAT slot
//  at its original high VA (call ds:__imp_WaitForSingleObjectEx, 0x01ADC52F).
static int WaitForEvent(void *obj, int dwMilliseconds, int bAlertable)
{
    HANDLE hEvent = *(HANDLE *)((char *)obj + 0x80);
    DWORD  status = WaitForSingleObjectEx(hEvent, (DWORD)dwMilliseconds,
                                          (BOOL)(bAlertable != 0));
    if (status > 0xC0) {
        if (status == WAIT_TIMEOUT)
            return 0;
    }
    else {
        if (status == 0xC0)
            return 1;
        if (status == WAIT_OBJECT_0 || status == WAIT_ABANDONED)
            return 2;
    }
    return bAlertable;
}
//  SetFrontendEvent  (0x18C4540 - 0x18C454E)  -- thiscall sibling of
//  WaitForEvent: signal the event stored at obj+0x80 of the frontend object
//  (the EventObjCtor (0x18C46E0) constructor's CreateEventA handle; 0 in this
//  build, so
//  SetEvent fails quietly).  The tail jump of RunGameLoop (0x619D70) lands
//  here with the frontend object (dword_1DBD9E8) in ECX.  [NOT byte-
//  identical] the original calls the SetEvent IAT slot at its original high
//  VA (call ds:__imp_SetEvent, 0x01AD5xxx).
static BOOL SetFrontendEvent(void *obj)
{
    return SetEvent(*(HANDLE *)((char *)obj + 0x80));
}
// 0x18FA3E0 / 0x18FA3D0: the heap-manager getters. The original sub_18FA3E0
// takes no argument (callers push a value on the stack for a later stdcall
// call, which the decompiler mis-attributes to it): it returns the 144-byte
// manager object (dword_2075ED8), built by CreateHeapManager (0x18FBD00).
// sub_18FA3D0 is the thiscall `mov eax, [ecx+1Ch]; retn` getter for the
// manager's +0x1C slot (the 1-byte sound-system object), declared as a plain
// free function here. [NOT byte-identical] both ref the high-VA manager
// global (0x02075ED8).
void *GetHeapManager(void) { return g_dword_2075ED8; }
void *GetHeapManagerSlot1C(void *hm) { printf("TRACE GetHeapManagerSlot1C(hm=%p)\n", hm); return *(void **)((char *)hm + 0x1C); }
// 0x145D7B0  (0x145D7B0 - 0x145D7BC)  -- SetSoundHWND: __stdcall with one
// pushed int: store the window handle into dword_1DDC934 and return it.
//  [NOT byte-identical] refs the high-VA global dword_1DDC934 (0x01DDC934).
//  The original 12 bytes:
//    8B 44 24 04    mov eax, [esp+arg_0]
//    A3 34 C9 DD 01 mov dword_1DDC934, eax
//    C2 04 00       retn 4
// dword_1DDC934 is the HWND the lazy DirectSound8 init (sub_146D480, which
// reads it at 0x146D4A5) passes to SetCooperativeLevel (dwLevel = 2,
// DSSCL_NORMAL). The only caller is RunFrontEnd (0x61D47B).
static DWORD g_dword_1DDC934 = 0;   // 0x1DDC934: DirectSound SetCooperativeLevel window handle
static int __stdcall SetSoundHWND(int hwnd)
{
    g_dword_1DDC934 = (DWORD)hwnd;
    return (int)g_dword_1DDC934;
}
// CNetVoiceManagerInit (sub_741E50 - 0x741F1B) is the CNetVoiceManager's voice
// bring-up (XNotifyCreateListener + XHVCreateEngine + the 4-user voice enable/
// reset pass); it is implemented in netinit.cpp and called from the frontend
// below as CNetVoiceManagerInit(CNetManagerGetVoiceManager(GetNetManager())).

// Worker callback functions the frontend installs (their addresses are stored
// in the worker at +0x25C/+0x260/+0x264/+0x268 by the sub_18F6Cxx setters).
// PauseGameOnVMState  (0x61A330 - 0x61A407)
// -- the VM-state-driven worker callback (registered by RunFrontEnd on the
// worker at +0x25C via SetPauseGameOnVMState).  Reads the VM core's state byte (0x674320);
// while the VM reports something active, dispatches the entity's vtable[1] step.
// On the pause-worthy result (10) — or, when the result is not 2 and the
// one-shot release flag is clear, releases the previous pause-debug instance
// (LHAlloc(0x44) + the +0x40/+0x41/+0x59 setup) and then runs the pause
// sequence.  The pause sequence (shared by both) re-reads the net manager
// twice, sends the pause packet through sub_687D90(v, 1, 1) and tail-calls
// sub_685520 with the +0x10 sub-object.  Returns the send's result; returns
// the raw state byte when the VM reports nothing.  Calls that are no-ops in
// this build (the worker callback is stored at +0x25C via SetPauseGameOnVMState
// but never invoked: the worker's pump loop does not read the slot).  Calls
// that are no-ops in this build are left as documented stubs.
//   [NOT byte-identical] refs the high-VA globals g_dword_1DBDD4C (via
// KoreGetVMCore) + byte_1DBD9E1 (0x1DBD9E1, kept static here) and ~13
// unimplemented engine callees (marked [STUB] below); placement-dependent
// (relative E8 calls throughout).
static BYTE g_byte_1DBD9E1 = 0;   // 0x1DBD9E1: the one-shot pause release flag

// sub_674320 (0x674320 - 0x674336) -- reads the VM core's state byte:
// *(VMCore + 0x0C -> +0xC2); 0 when there is no live VM.
static int VMStateByte(void)
{
    int core = KoreGetVMCore();
    if (core == 0)
        return 0;
    return (int)(signed char)*(unsigned char *)(*(DWORD *)(core + 0x0C) + 0xC2);
}

// sub_673F70 (0x673F70 - 0x673F77) -- the entity address read: *(VMCore+0x0C)+0x30.
static int VMEntityOfCore(int core)
{
    return (int)*(DWORD *)(*(DWORD *)(core + 0x0C) + 0x30);
}

// sub_6E2BB0 (0x6E2BB0 - 0x6E2BF2) -- EntitySubObject (thiscall, plain retn):
// the entity sub-object build/validate.  Reads *(this + 4) as the sub-object
// pointer, validates it, then returns the live value at *(v4 + 0x8) of the
// sub-object it produced.  [NOT byte-identical] placement-dependent (relative
// E8 calls to __invalid_parameter_noinfo) so C++ reimplementation.
//
// The body (three invalid-parameter checks, each reports via F3InvalidParam and
// continues):
//   v1 = this->+4;  v2 = *v1;
//   if (v2 == 0) -> invalid-parameter (the original is noreturn here; this build
//                  reports and returns 0 so we do not wild-read below)
//   v3 = *(v1 + 0x14);  v4 = *(v3 + 4);
//   v5 = *v2;  if (v4 == *(v5 + 0x14)) -> invalid-parameter
//   v2 = *v2;  if (v4 == *(v2 + 0x14)) -> invalid-parameter (identical check to
//                  the previous one: the re-deref yields the same value)
//   return *(v4 + 0x8);
static int EntitySubObject(void *obj)
{
    DWORD v1 = ((DWORD *)obj)[1];       // this + 4
    DWORD v2 = *(DWORD *)v1;            // *v1 (ESI = the sub-object pointer)
    if (v2 == 0)
    {
        F3InvalidParam();
        return 0;                      // original: __invalid_parameter_noinfo (no return)
    }
    DWORD v3 = *(DWORD *)(v1 + 0x14);  // *(v1 + 0x14) (EDI = the guard word)
    DWORD v4 = *(DWORD *)(v3 + 4);     // v4 = *(v3 + 4)
    DWORD v5 = *(DWORD *)v2;           // EAX = *v2 (ESI != 0 here)
    if (v4 == *(DWORD *)(v5 + 0x14))
        F3InvalidParam();
    v2 = *(DWORD *)v2;                  // re-deref (ESI = *ESI)
    if (v4 == *(DWORD *)(v2 + 0x14))
        F3InvalidParam();
    return *(DWORD *)(v4 + 8);
}

// ===============================================================
//  PauseDebugInstance -- the one-time pause-debug instance
//  (unk_1DBE300, 0x1DBE300..0x1DBE3A8).  [NOT byte-identical] refs the
//  high-VA globals (unk_1DBE300 / dword_1DBE3A8) and the relative E8 calls
//  (InitSharedSlot / InitSharedSlotWide / LHAlloc / the CRT
//  InitializeCriticalSection / GetNetManager / PauseDebugRegisterPacket).
//
//  The pause-debug instance is a 0xA8-byte object, built on first use by
//  GetPauseDebugInstance (sub_67E460): two -1 owner words, the shared slot
//  at +0x08, the wide slot at +0x50, the critical section at +0x34, the 1.0f
//  floats at +0x5C / +0x60, the byte flags at +0x54..+0x5A, the linked
//  owner block at +0x68 (a 4-byte owner block holding &obj+0x68, plus a 24-byte
//  self-linked sentinel block at +0x7C), the CNetPacketCallback DNameNode
//  base at +0x88, and the +0x90 self back-pointer -- plus the two packet
//  registrations (519 / 520) on the net manager's packet manager.
// ===============================================================
static BYTE g_unk_1DBE300[0xA8] = { 0 };   // 0x1DBE300: pause-debug instance
static DWORD g_dword_1DBE3A8 = 0;          // 0x1DBE3A8: one-time-install flag

// The packet manager is the CNetManager's +0x04 slot (sub_6A6820): mov eax,
// [ecx+4]; retn.  Static copy (the same one-liner lives in gamethread.cpp,
// unexported).  NULL net manager -> NULL packet manager (the real game reads
// it unconditionally; a null manager here is possible pre-start, so the NULL
// is carried through, matching the CNetSaveGameCtor idiom).
static void *PauseDebugPacketManager(void *obj)
{
    return (void *)*(DWORD *)((char *)obj + 4);
}

// sub_77DB70 (0x77DB70 - 0x77DB78) -- the 8-byte _ReaderWriterLock ctor:
// zero both words.  The original is thiscall; a C call cannot stage ECX, so
// zero the passed buffer directly (identical result).
static void PauseDebugLockCtor(void *lock)
{
    DWORD *dw = (DWORD *)lock;
    dw[0] = 0;
    dw[1] = 0;
}

// ===============================================================
//  The CNetPacketCallback registration cluster (PauseDebugRegisterPacket + helpers)
// ===============================================================

// Placeholder vtable for the CNetPacketCallback DNameNode base at +0x88 and
// the derived vftable the registration cluster stores into the {vftable, node}
// slots (the helpers below and the +0x88 base at PauseDebugInstanceCtor).
// Nothing dispatches through it in this build (CNetPacketCallbackDtor does
// not virtual-call), so the zeroed 8-slot placeholder preserves the
// observable behavior (the netinit.cpp g_CNetPacketCallbackVtable pattern).
static void *g_PauseDebugPacketCallbackVtable[8] = { 0 };   // NPacketCallback::CNetPacketCallback::`vftable'

// Placeholder for the base CBaseTrackingPointer vftable (the base vftable the
// DNameNode base ctor (PauseDebugDNameNodeCtor) stores into a freshly constructed object
// before the derived CNetPacketCallback vftable overwrite).  Zeroed, like
// the other placeholder vtables in this build.
static void *g_BaseTrackingPointerVtable[1] = { 0 };   // NTools::CBaseTrackingPointer::`vftable'

// Placeholder for the CNetPlayFMVPacket vftable (slot 0 = the packet-filling
// callee sub_680160, out of scope in this build) and the CApplicationPacket
// vftable (the reset-of-vftable thunk sub_9EDB40 writes back after the packet
// is serialized, also not dispatched in this build).  Zeroed, like the other
// placeholder vtables in this build.
static void *g_CNetPlayFMVPacketVtable[6] = { 0 };   // NTools::CNetPlayFMVPacket::`vftable'
static void *g_CNetStopFMVPacketVtable[6] = { 0 };   // NTools::CNetStopFMVPacket::`vftable' (0x1AEF740)
static void *g_CApplicationPacketVtable[6] = { 0 };   // NTools::CApplicationPacket::`vftable'

// PauseDebugDNameNodeCtor (0x1898300 - 0x189830F) -- DNameNode::DNameNode
// the 8-byte base ctor: store the base CBaseTrackingPointer vftable, zero the
// node word.  [NOT byte-identical] placement-dependent (references the base
// vftable at a high VA).
static void PauseDebugDNameNodeCtor(DWORD *obj)
{
    obj[0] = (DWORD)g_BaseTrackingPointerVtable;
    obj[1] = 0;
}

// PauseDebugSharedNodeFinalize (0x1898350 - 0x1898395) -- the shared-object node finalizer:
// store the base CBaseTrackingPointer vftable, decrement the node's refcount
// (freeing the block when it hits zero and the stored value is null), zero
// the node word, and return the old node.
static DWORD PauseDebugSharedNodeFinalize(DWORD *obj)
{
    DWORD result = obj[1];
    obj[0] = (DWORD)g_BaseTrackingPointerVtable;
    if (result != 0)
    {
        InterlockedDecrement((volatile long *)((char *)result + 4));
        result = obj[1];
        if (*(DWORD *)((char *)result + 4) == 0 && *(DWORD *)result == 0)
            LHFree((void *)result);
        obj[1] = 0;
    }
    obj[1] = 0;
    return result;
}

// PauseDebugSharedNodeRelease (0x1898310 - 0x1898348) -- the shared-object node release:
// decrement the node's refcount (freeing the block when it hits zero and the
// stored value is null), zero the node word, and return the old node.
static DWORD PauseDebugSharedNodeRelease(DWORD *obj)
{
    DWORD result = obj[1];
    if (result != 0)
    {
        InterlockedDecrement((volatile long *)((char *)result + 4));
        result = obj[1];
        if (*(DWORD *)((char *)result + 4) == 0 && *(DWORD *)result == 0)
            LHFree((void *)result);
        obj[1] = 0;
    }
    return result;
}

// PauseDebugSharedNodeRef (0x18983A0 - 0x1898427) -- the shared-object add/release helper
// (the 2-word {vftable, node} shared-object slot a DNameNode points at).
// Releases the old node (refcount drop + optional free) and, when a handler
// is non-null, shares its existing shared-object (refcount increment) or
// builds a fresh 8-byte {handler, refcount} block.  Returns the new refcount
// (InterlockedIncrement) or the old node value.
//
// this = the DNameNode object (2 words), a2 = the handler (the callback).
static DWORD PauseDebugSharedNodeRef(DWORD *obj, DWORD a2)
{
    DWORD result = obj[1];
    if (result != 0)
    {
        if (*(DWORD *)result == a2 && a2 != 0)
            return result;
        InterlockedDecrement((volatile long *)((char *)result + 4));
        result = obj[1];
        if (*(DWORD *)((char *)result + 4) == 0 && *(DWORD *)result == 0)
            LHFree((void *)result);
        obj[1] = 0;
    }
    if (a2 != 0)
    {
        DWORD v4 = *(DWORD *)((char *)a2 + 4);
        if (v4 != 0)
        {
            obj[1] = v4;
            return (DWORD)InterlockedIncrement((volatile long *)((char *)v4 + 4));
        }
        DWORD *v5 = (DWORD *)LHAlloc(8);
        obj[1] = (DWORD)v5;
        if (v5 != 0)
        {
            *v5 = a2;
            *(DWORD *)((char *)v5 + 4) = 1;
        }
        result = obj[1];
        *(DWORD *)((char *)a2 + 4) = result;
    }
    return result;
}

// PauseDebugCompare (0x77DD10 - 0x77DD2C) -- the 2-word compare: the manager's
// +0x58 object must equal the lock's 2-word shape (both words) for the
// fast-path branch to fall through.
static BOOL PauseDebugCompare(const DWORD *a, const DWORD *b)
{
    return *a == *b && a[1] == b[1];
}

// PauseDebugRegisterCopy (0xA1D080 - 0xA1D0E1) -- the registration copy: build the
// holder object ({lock[0], lock[1], id, DNameNode{vftable, node}, ser, recv})
// from the lock and the registration object, sharing the callback's
// shared-object.  [NOT byte-identical] placement-dependent.
//
// this = the 7-word holder (7 dwords), a2 = the lock, a3 = the id,
// a4 = the registration object (the 1st DNameNode).
static DWORD *PauseDebugRegisterCopy(DWORD *holder, DWORD *lock, DWORD id, DWORD *obj1)
{
    holder[0] = lock[0];
    holder[1] = lock[1];
    holder[2] = id;

    // The DNameNode base ctor at holder+3, then the derived overwrite.
    PauseDebugDNameNodeCtor(&holder[3]);
    holder[3] = (DWORD)g_PauseDebugPacketCallbackVtable;   // derived vftable overwrite

    // The shared-object node for the callback: release the old slot, then
    // share the existing shared-object (or build a fresh one).
    DWORD v6 = obj1[1];
    DWORD v7 = 0;
    if (v6 != 0)
    {
        if (*(DWORD *)v6 != 0)
            v7 = *(DWORD *)v6;                              // the callback
        else
        {
            PauseDebugSharedNodeRelease(obj1);              // PauseDebugSharedNodeRelease(&obj1)
        }
    }
    PauseDebugSharedNodeRef(&holder[3], v7);

    holder[5] = obj1[2];   // the ser handler
    holder[6] = obj1[3];   // the recv handler
    return holder;
}

// PauseDebugA1FE30 (0xA1FE30) -- [STUB] the packet-table-entry shift callee
// (depends on sub_A1FB40 / sub_A1CA90 -- both unimplemented).  Out of scope
// for this build (the packet manager is 0, so the +0x44 table is never
// reached); the arguments are accepted and ignored, matching the observable
// behavior (the original would route through a dead slot).
static DWORD PauseDebugA1FE30(DWORD *pm, DWORD holder)
{
    (void)pm; (void)holder;
    return 0;
}

// PauseDebugA1A530 (0xA1A530) -- [STUB] the one-shot (fast-path) registration
// (depends on ~8 unimplemented VM-ish callees: sub_A1D9A0 / sub_A1F330 /
// sub_A1E140 / sub_A20E50 / sub_A1C720 / sub_A1FED0 / sub_A1DA40 /
// sub_A20700 -- all out of scope).  Out of scope for this build (the packet
// manager is 0, so no packets are ever delivered); the arguments are
// accepted and ignored, preserving the observable behavior.
static void PauseDebugA1A530(void *pm, unsigned int id, DWORD *lock, DWORD *obj1)
{
    (void)pm; (void)id; (void)lock; (void)obj1;
}

// PauseDebugRegisterPacket (0xA1AA30 - 0xA1AB5B) -- the packet-callback registration on the
// net manager's packet manager.  [NOT byte-identical] placement-dependent
// (relative E8 calls / high-VA globals / the relative E8 chain through the
// shared-object helpers).
//
// thiscall in the original (the packet manager in ECX); the obj is passed
// explicitly.  Faithful port of the original control flow: the id/callback
// guard, the 1st DNameNode (the registration object) + the {ser, recv}
// handler-pair area, the 2nd DNameNode + the shared-object add-ref on the
// callback (PauseDebugSharedNodeRef), the +0x60 fast-path / +0x58 compare gate, the
// PauseDebugRegisterCopy + PauseDebugA1FE30 + release sequence (normal path) or the
// PauseDebugA1A530 one-shot (fast path), then the two finalizers (PauseDebugSharedNodeFinalize)
// whose return value is the function's result.
//
// [NOT byte-identical] and not byte-identical: refs the high-VA vtables and
// the relative E8 calls.  The NULL packet manager carrying NULL through is a
// documented safe deviation: the original reads [esi+60h] unconditionally
// (the original game always has a live packet manager), so this build guards
// it (the packet manager is 0, so no packets are ever delivered).
static DWORD PauseDebugRegisterPacket(void *pm, int id, void *lock,
                                      void *callback, void *recv, void *ser)
{
    DWORD result = (DWORD)lock;
    if ((unsigned short)id < 0xFE || callback == 0)
        return result;

    // The packet manager is 0 in this build (the net manager's +0x04 slot is
    // never constructed), so the original's unconditional [esi+60h] flag read
    // would dereference a null pointer.  Carry the NULL through (the project's
    // established null-net-manager idiom).
    if (pm == 0)
        return result;

    BYTE *pmbyte = (BYTE *)pm;

    // The 1st DNameNode (the registration object): the base ctor + derived
    // overwrite.  16 bytes: {vftable, node, ser, recv}.
    DWORD obj1[4];
    PauseDebugDNameNodeCtor(obj1);
    obj1[0] = (DWORD)g_PauseDebugPacketCallbackVtable;

    // The 2nd DNameNode (the shared-object holder): the base ctor + derived
    // overwrite, then the shared-object add/release on the callback.
    DWORD obj2[2];
    PauseDebugDNameNodeCtor(obj2);
    obj2[0] = (DWORD)g_PauseDebugPacketCallbackVtable;

    // PauseDebugSharedNodeRef(&obj2, callback) -- the shared-object add/release.
    PauseDebugSharedNodeRef(obj2, (DWORD)callback);

    // eax = obj2[1] (the shared-object node); branch the same way the
    // original does (release the node when its stored value is null, else
    // share the pointed-to object).
    DWORD eax = obj2[1];
    if (eax != 0)
    {
        if (*(DWORD *)eax == 0)
        {
            PauseDebugSharedNodeRelease(obj2);
            eax = 0;
        }
        else
        {
            eax = *(DWORD *)eax;
        }
    }
    else
    {
        eax = 0;
    }

    // PauseDebugSharedNodeRef(&obj1, eax) -- the registration object's shared-object.
    PauseDebugSharedNodeRef(obj1, eax);

    // The _ReaderWriterLock ctor zeroes the {ser, recv} handler-pair area
    // (the original zeroes the lock area first, then writes the handlers in).
    // The original packs the pair via sub_11CAD00; the observable effect is
    // the same as writing the two handlers into the registration object.
    obj1[2] = (DWORD)ser;
    obj1[3] = (DWORD)recv;

    // The +0x60 fast-path / +0x58 compare gate.
    if (pmbyte[0x60] == 0 ||
        !PauseDebugCompare((DWORD *)((char *)pm + 0x58), (DWORD *)lock))
    {
        // The PauseDebugA1A530 one-shot registration (out of scope -- stubbed).
        PauseDebugA1A530(pm, (unsigned int)id, (DWORD *)lock, obj1);
    }
    else
    {
        // The PauseDebugRegisterCopy registration copy, then the +0x44 table write, then
        // the v12 finalizer.
        DWORD holder38[7];
        PauseDebugRegisterCopy(holder38, (DWORD *)lock, (DWORD)id, obj1);
        PauseDebugA1FE30((DWORD *)((char *)pm + 0x44), (DWORD)holder38);

        // PauseDebugSharedNodeFinalize(&v12) -- the 16-byte v12 buffer finalizer (zeroed: the
        // original reads the uninitialized stack slot free; zeroing is the
        // safe equivalent).
        DWORD v12[4];
        memset(v12, 0, sizeof(v12));
        PauseDebugSharedNodeFinalize(v12);
    }

    // The two finalizers (the return value of the second is the function's
    // result).
    PauseDebugSharedNodeFinalize(obj2);
    return PauseDebugSharedNodeFinalize(obj1);
}

// The receive / serialize handler pairs 519 and 520 of the pause-debug
// instance.  519: receive (wrapper + documented [STUB] state loader) +
// serialize (implemented).  520: both implemented (the receive handler is a
// tail-jump into the implemented stop-FMV state machine; the serialize
// handler is a faithful port of the original outer control flow).

// sub_67E030 (0x67E030 - 0x67E2CE) -- the FMV packet state loader behind the
// 519 receive handler.  [STUB] out of scope for this build — the body is a
// 682-byte (0x2A6) thiscall (the pause-debug instance in ECX; 7 stack args)
// that resolves the FMV "videos" settings slot path through the shared-slot
// machinery (CopySharedSlot / InitSharedSlot / SetSharedSlot /
// ResolveLipComponentSlotPath / MakeSettingsFilePath / SharedSlotConcat /
// ReleaseSharedSlot), checks the settings file exists (SharedFileExists),
// reads the FMV state into the instance (+0x14/+0x18), applies the sub-
// title/video gate (sub_7D4650 / sub_67DAF0 / the LHAlloc + DWORD-pool chain
// sub_42CE40 / sub_67F100 / sub_C0E130 / sub_452E70 / sub_18EAAF0) and writes
// the flags (+0x10/+0x54-+0x58, and the +0x98/+0xA0 double/byte).  The body
// depends on ~20 unimplemented callees (sub_6A8760 / GetEngineTime /
// sub_18FB460 / sub_18F02C0 / sub_18F1120 / sub_7D4650 / sub_67DAF0 /
// unknown_libname_2583 / sub_42CE40 / sub_67F100 / sub_C0E130 / sub_452E70 /
// sub_18EAAF0 / PauseDebugSetStopState — all out of scope, so the body is NOT
// reimplemented here.  Faithful port of the wrapper (PauseDebugRecv519) only;
// the callee is an explicit stub carried the same way as PauseDebugA1A530 /
// PauseDebugA1FE30.
static int FmvPacketStateLoad(void *obj, void *stateObj, unsigned char a3,
                              unsigned char a4, unsigned char a5,
                              unsigned char a6, DWORD a7, int a8)
{
    (void)obj; (void)stateObj; (void)a3; (void)a4; (void)a5; (void)a6;
    (void)a7; (void)a8;
    return 0;
}

// PauseDebugRecv519  (0x6805B0 - 0x6805DC)  -- the receive handler for the
// pause-debug FMV packet (519): unpack the packet and delegate to the FMV
// state loader.
//
// This is a thin wrapper in the original (44 bytes, no frame): it reads the
// incoming packet (arg_0 = pkt) at +8..+11 (four bytes, movzx) and +12 (a
// DWORD), adds 4 to the packet pointer (the state object handed to the
// loader) and sets ECX to the pause-debug instance (arg_1 = obj) before
// calling sub_67E030 with seven stack args (pkt+4, *(pkt+8), *(pkt+9),
// *(pkt+10), *(pkt+11), *(pkt+12), 1).  The return value is the loader's
// result.  [NOT byte-identical] the body has relative E8 calls and references
// the (unimplemented) state loader, so it is a C++ reimplementation.  The
// callee is an explicit [STUB] carried as FmvPacketStateLoad above; the
// observable behavior in this build is unchanged (the packet manager is 0, so
// the handler is never called and only its address is registered).
static int PauseDebugRecv519(void *pkt, void *obj)
{
    BYTE *bytes = (BYTE *)pkt;
    return FmvPacketStateLoad(obj,
                              (char *)pkt + 4,
                              bytes[8],
                              bytes[9],
                              bytes[10],
                              bytes[11],
                              *(DWORD *)(bytes + 12),
                              1);
}

// PauseDebugSer519LowerSearch (sub_651F70, 0x651F70 - 0x651FC3) -- the "lower-bound"
// half of the two-call sorted-entry binary search the 519/520 serialize handlers
// run over the packet-state entry list (the sorted 20-byte entry list at the
// packet state's +8 (start) .. +0xC (end)).  Faithful port of the original
// binary search: starting at the list start, the probe entry is advanced by
// 20*(count/2); if the probe's first word is >= the target, the search advances
// to the next entry and recomputes the count (v4 += -1 - v4 / 2); otherwise
// the count is halved (the target is in the first half).  [NOT byte-identical]
// placement-dependent (the inline division-by-20 magic + the relative E8 calls).
static int PauseDebugSer519LowerSearch(DWORD *obj, int *a2, unsigned short *a3)
{
    int v3 = *(obj + 2);                       // the list start (+8)
    int v4 = (*(obj + 3) - v3) / 20;           // the entry count
    *a2 = 0;
    a2[1] = v3;
    while (v4 > 0)
    {
        int v5 = *a2;
        int v6 = a2[1] + 20 * (v4 / 2);
        if (*a3 < *(unsigned short *)v6)
        {
            v4 /= 2;
        }
        else
        {
            a2[1] = v6 + 20;
            *a2 = v5;
            v4 += -1 - v4 / 2;
        }
    }
    return (int)a2;
}

// PauseDebugSer519UpperSearch (sub_651F00, 0x651F00 - 0x651F6E) -- the "upper-bound"
// half of the two-call sorted-entry binary search the serialize handlers run
// over the packet-state entry list.  Faithful port of the original: starting at
// the list start, the probe entry is advanced by 20*(count/2); if the probe's
// first word >= the target, the search backs out to the original entry and
// halves the count; otherwise the search advances to the next entry and
// recomputes the count (v4 += -1 - (v4 >> 1)).  [NOT byte-identical]
// placement-dependent (the inline division-by-20 magic + the relative E8 calls).
static int PauseDebugSer519UpperSearch(DWORD *obj, int *a2, unsigned short *a3)
{
    int v3 = *(obj + 2);                       // the list start (+8)
    int v4 = (*(obj + 3) - v3) / 20;           // the entry count
    *a2 = 0;
    a2[1] = v3;
    while (v4 > 0)
    {
        int v5 = 20 * (v4 >> 1);
        int oldEntry = a2[1];
        a2[1] += v5;
        unsigned short *v6 = (unsigned short *)a2[1];
        if (*v6 >= *a3)
        {
            a2[1] = oldEntry;
            v4 >>= 1;
        }
        else
        {
            a2[1] += 20;
            v4 += -1 - (v4 >> 1);
        }
    }
    return (int)a2;
}

// PauseDebugSer519  (0x67F940 - 0x67FA4B)  -- the serialize handler for the
// pause-debug FMV packet (519): build the CNetPlayFMVPacket from the
// packet-state object, then iterate the sorted 20-byte entry list for the
// 519 id.  [NOT byte-identical] placement-dependent (relative E8 calls to
// sub_651F70 / sub_651F00 / ReleaseSharedSlot / sub_9EDB40 / the packet vftable
// call, plus the high-VA CNetPlayFMVPacket vftable).
//
// this = a4 = the packet-state object (the sorted 20-byte entry list at +8..+0xC
// with the entry's first WORD holding the packet id; the +0xAC byte read at the
// loop tail gates the "keep looping" condition); a1 = unused (the placeholder
// leading arg); a2 = the outer object handed to the packet-constructor thunk
// (the CNetPlayFMVPacket vftable[0] call, out of scope in this build); a3 = the
// object whose +0xAC byte gates the loop.
static int PauseDebugSer519(int a1, int a2, int a3, DWORD *a4)
{
    (void)a1;
    BYTE packet[16];
    memset(packet, 0, sizeof(packet));

    // The packet (the v11 local): the first word is the CNetPlayFMVPacket vftable.
    // (The original sub_9EDBB0 thunk originally writes &CApplicationPacket::vftable;
    // the write is immediately overwritten with the PlayFMV vftable, so only the
    // PlayFMV vftable matters.)
    *(DWORD *)(packet + 0) = (DWORD)g_CNetPlayFMVPacketVtable;

    // +0x04: the shared slot (the original InitSharedSlot call; a documented
    // [STUB]-adjacent helper, implemented in this build).
    InitSharedSlot((F3SharedBuffer **)(packet + 4));

    // The packet fields: the +0x0A/+0x0B byte pair (1, 1) and the +0x0C dword
    // (1) are written before the vftable[0] call in the original, as is the
    // +0x08/+0x09 byte pair (0, 0) (v13 / v14).
    packet[10] = 1;                     // +0x0A (v15)
    packet[11] = 1;                     // +0x0B (v16)
    *(DWORD *)(packet + 12) = 1;        // +0x0C (v17)
    packet[8] = 0;                      // +0x08 (v13)
    packet[9] = 0;                      // +0x09 (v14)

    // The packet-filling callee (the CNetPlayFMVPacket vftable[0] call,
    // sub_680160) -- out of scope for this build.  It writes the packet's
    // +0x08..+0x11 byte fields and the +0x0C dword through the outer object's
    // vftable slots [16] / [10] (0x40 / 0x28), plus calls sub_67FFC0 (the
    // 519-packet builder, which depends on a deep set of unimplemented callees
    // -- SetSaveGamePath / LHAlloc / sub_67FD50 / the LHFree finalizer).  The
    // handler is never called in this build (the packet manager is 0) -- only
    // its address is registered -- so the observable behavior is unchanged.
    // [STUB] -- documented no-op, mirroring FmvPacketStateLoad / PauseDebugA1FE30.
    (void)a2;

    // The two binary searches for the 519 id (sub_651F70 lower-bound +
    // sub_651F00 upper-bound -- together bracketed over the sorted entry list).
    DWORD lowerSearch[2];
    DWORD upperSearch[2];
    unsigned short target = 519;
    PauseDebugSer519LowerSearch(a4, (int *)lowerSearch, &target);
    PauseDebugSer519UpperSearch(a4, (int *)upperSearch, &target);

    // Iterate the entries from the found entry to the end of the list.
    // (The original reads an uninitialized "end" local from the loop's
    // comparison -- no store for it exists anywhere in the body; in the
    // original the loop would iterate against garbage.  The honest
    // interpretation is the search object's +0xC field (the list end, the
    // same pointer the binary searches read), so the loop runs from the
    // found entry to the actual list end.)
    DWORD foundEntry = upperSearch[1];
    DWORD endEntry = *(DWORD *)((char *)a4 + 12);
    if (foundEntry != endEntry)
    {
        DWORD v6 = foundEntry + 8;
        do
        {
            DWORD *holder = (DWORD *)*(DWORD **)v6;
            if (*(DWORD *)v6 != 0)
            {
                if (holder != 0 && *holder != 0)
                {
                    int v8 = *holder;
                    if (v8 != 0)
                    {
                        (*(void (__cdecl **)(char **, DWORD))(v6 + 8))(
                            (char **)packet, *(DWORD *)(v8 + 8));
                    }
                }
                else
                {
                    PauseDebugSharedNodeRelease((DWORD *)(v6 - 4));
                }
            }
            if (a3 != 0 && (*(unsigned char *)((char *)a3 + 172) & 2) == 0)
                break;
            foundEntry += 20;
            v6 += 20;
        } while (foundEntry != endEntry);
    }

    // The shared-slot destructor (the return value is the function's result,
    // as the original leaves EAX with ReleaseSharedSlot's result).
    long result = ReleaseSharedSlot((F3SharedBuffer **)(packet + 4));

    // The packet vftable reset (sub_9EDB40: the first word back to the
    // CApplicationPacket vftable).
    *(DWORD *)(packet + 0) = (DWORD)g_CApplicationPacketVtable;

    (void)packet;
    return (int)result;
}

//  PauseDebugSetStopState  (sub_67CF30, 0x67CF30 - 0x67CF72) -- set the
//  FMV stop-state word (+0x30) under the instance's +0x34 critical section.
//  [NOT byte-identical] placement-dependent (the Enter/Leave critical-section
//  IAT slots).  The original is thiscall (obj in ECX; the state word on the
//  stack), so the obj is passed explicitly.
static void PauseDebugSetStopState(void *obj, int state)
{
    F3CsGuard guard;
    CsGuardAcquire(&guard, (CRITICAL_SECTION *)((char *)obj + 52), 0);
    *(DWORD *)((char *)obj + 48) = (DWORD)state;
    CsGuardRelease(&guard);
}

//  PauseDebugGetStopState  (sub_67CF80, 0x67CF80 - 0x67CFBE) -- read the
//  FMV stop-state word (+0x30) under the instance's +0x34 critical section.
//  [NOT byte-identical] placement-dependent (the Enter/Leave critical-section
//  IAT slots).  The original is thiscall (obj in ECX), so the obj is passed
//  explicitly.
static int PauseDebugGetStopState(void *obj)
{
    F3CsGuard guard;
    CsGuardAcquire(&guard, (CRITICAL_SECTION *)((char *)obj + 52), 0);
    int v2 = *(DWORD *)((char *)obj + 48);
    CsGuardRelease(&guard);
    return v2;
}

//  PauseDebugStopFmvReceive  (sub_67D310, 0x67D310 - 0x67D3A9) -- the
//  pause-debug instance's stop-FMV state machine.  [NOT byte-identical]
//  placement-dependent (the Enter/Leave critical-section IAT slots + the
//  tail-call structure).  The original is thiscall (obj in ECX); a C call
//  cannot stage ECX, so the obj is passed explicitly.
//
//  Faithful port of the original: acquire the +0x34 critical section, read
//  the +0x30 state word, release and re-acquire it, then:
//    state == 3  ->  state = 4
//    state == 1  ->  set state = 5
//    state == 2  ->  set state = 0
//  (the else branch re-reads the state word under the re-acquired section
//  before deciding; only the state == 2 path calls the getter, exactly as
//  the original does).
static void PauseDebugStopFmvReceive(void *obj)
{
    F3CsGuard guard;
    CsGuardAcquire(&guard, (CRITICAL_SECTION *)((char *)obj + 52), 0);
    DWORD v3 = *(DWORD *)((char *)obj + 48);
    CsGuardRelease(&guard);
    CsGuardAcquire(&guard, (CRITICAL_SECTION *)((char *)obj + 52), 0);
    if (v3 == 3)
    {
        *(DWORD *)((char *)obj + 48) = 4;
        CsGuardRelease(&guard);
        return;
    }
    DWORD v4 = *(DWORD *)((char *)obj + 48);
    CsGuardRelease(&guard);
    if (v4 == 1)
    {
        PauseDebugSetStopState(obj, 5);
    }
    else if (PauseDebugGetStopState(obj) == 2)
    {
        PauseDebugSetStopState(obj, 0);
    }
}

//  PauseDebugRecv520  (sub_67F4B0, 0x67F4B0 - 0x67F4B9) -- the receive handler
//  for the pause-debug FMV stop packet (520): tail-jump into the stop-FMV
//  state machine with the pause-debug instance in ECX.
//
//  This is a 9-byte (no-frame) wrapper in the original: `mov ecx,
//  [esp+arg_4]; jmp sub_67D310` -- the second argument (obj) becomes the
//  thiscall's ECX and the state machine runs with it.  [NOT byte-identical]
//  the body is a tail-jmp (the original's absolute target is not
//  reproducible); the callee is the implemented PauseDebugStopFmvReceive
//  above.  The original returns the void state machine's result (undefined
//  EAX -- the callee leaves no defined return value), so 0 is returned.
static int PauseDebugRecv520(void *pkt, void *obj)
{
    (void)pkt;
    PauseDebugStopFmvReceive(obj);
    return 0;
}

//  PauseDebugSer520  (sub_67FA50, 0x67FA50 - 0x67FB27) -- the serialize handler
//  for the pause-debug FMV stop packet (520): build the CNetStopFMVPacket
//  from the packet-state object, then iterate the sorted 20-byte entry list
//  for the 520 id.  [NOT byte-identical] placement-dependent (relative E8
//  calls to PauseDebugSer519LowerSearch / PauseDebugSer519UpperSearch /
//  PauseDebugSharedNodeRelease / sub_9EDB40 / the packet-ctor call, and the
//  high-VA CNetStopFMVPacket vftable).
//
//  this = a4 = the packet-state object (the sorted 20-byte entry list at
//  +8..+0xC with the entry's first WORD holding the packet id; the +0xAC byte
//  read at the loop tail gates the "keep looping" condition); a1 = unused
//  (the placeholder leading arg); a2 = the outer object handed to the packet-
//  ctor thunk (the CNetStopFMVPacket vftable[0] call, out of scope in this
//  build); a3 = the object whose +0xAC byte gates the loop.
static void PauseDebugSer520(int a1, int a2, int a3, DWORD *a4)
{
    (void)a1;
    BYTE packet[16];
    memset(packet, 0, sizeof(packet));

    // The packet (the v10 local): the first word is the CNetStopFMVPacket
    // vftable.  (The original sub_9EDBB0 thunk originally writes
    // &CApplicationPacket::vftable; the write is immediately overwritten
    // with the StopFMV vftable, so only the StopFMV vftable matters.)
    *(DWORD *)(packet + 0) = (DWORD)g_CNetStopFMVPacketVtable;

    // The packet-filling callee (the CNetStopFMVPacket vftable[0] call,
    // nullsub_19) -- out of scope for this build.  It writes the packet
    // through the outer object's vftable slots (the CNetStopFMVPacket ctor
    // thunk); the handler is never called in this build (the packet manager
    // is 0) -- only its address is registered -- so the observable behavior
    // is unchanged.  [STUB] -- documented no-op, mirroring the 519 handler's
    // packet-filling callee.
    (void)a2;

    // The two binary searches for the 520 id (PauseDebugSer519LowerSearch --
    // the sub_651F70 lower-bound search, and PauseDebugSer519UpperSearch --
    // the sub_651F00 upper-bound search; the two helpers are packet-id-
    // agnostic and are shared with the 519 serialize handler).
    DWORD lowerSearch[2];
    DWORD upperSearch[2];
    unsigned short target = 520;
    PauseDebugSer519LowerSearch(a4, (int *)lowerSearch, &target);
    PauseDebugSer519UpperSearch(a4, (int *)upperSearch, &target);

    // Iterate the entries from the found entry (the upper-bound search's final
    // entry -- the first entry whose first word >= 520) to the first entry
    // strictly above the 520 id (the lower-bound search's final entry).  This
    // is exactly the range the original's two search calls bracket, so the
    // walk covers the entries whose first word == 520 -- the two-search-
    // bounds walk, not the +0xC list-end walk the (divergent) 519 handler
    // uses.
    DWORD foundEntry = upperSearch[1];
    DWORD endEntry = lowerSearch[1];
    if (foundEntry != endEntry)
    {
        DWORD v7 = foundEntry + 8;
        do
        {
            DWORD *holder = (DWORD *)*(DWORD **)v7;
            if (*(DWORD *)v7 != 0)
            {
                if (holder != 0 && *holder != 0)
                {
                    int v8 = *holder;
                    if (v8 != 0)
                    {
                        (*(void (__cdecl **)(char **, DWORD))(v7 + 8))(
                            (char **)packet, *(DWORD *)(v8 + 8));
                    }
                }
                else
                {
                    PauseDebugSharedNodeRelease((DWORD *)(v7 - 4));
                }
            }
            if (a3 != 0 && (*(unsigned char *)((char *)a3 + 172) & 2) == 0)
                break;
            foundEntry += 20;
            v7 += 20;
        } while (foundEntry != endEntry);
    }

    // The packet vftable reset (sub_9EDB40: the first word back to the
    // CApplicationPacket vftable).
    *(DWORD *)(packet + 0) = (DWORD)g_CApplicationPacketVtable;
    (void)packet;
}

// PauseDebugInstanceCtor  (0x67E310 - 0x67E45C) -- construct the pause-debug
// instance in place.  [NOT byte-identical] placement-dependent (relative E8
// calls / the CRT InitializeCriticalSection / GetNetManager / the packet-
// manager helpers / the two PauseDebugRegisterPacket calls).
//
// thiscall in the original (obj in ECX); a C call cannot stage ECX, so the
// obj is passed explicitly.  Faithful port of the original control flow:
// the two -1 owner words, InitSharedSlot at +0x08 / +0x0C, the +0x10 flag,
// the +0x2C / +0x30 zeroes, the +0x34 critical section, InitSharedSlot at
// +0x4C, InitSharedSlotWide at +0x50, the +0x54..+0x5A byte flags and the
// +0x5C / +0x60 1.0f floats, the +0x68 owner chain (a 4-byte block holding
// &obj+0x68, and a 24-byte self-linked sentinel block at +0x7C), the
// +0x88 CNetPacketCallback base, the +0x98 / +0xA0 zeroes, and the two packet
// registrations (519 / 520) on the net manager's packet manager.
static void *PauseDebugInstanceCtor(void *obj)
{
    char *b = (char *)obj;
    *(DWORD *)(b + 0x00) = (DWORD)-1;                       // +0x00
    *(DWORD *)(b + 0x04) = (DWORD)-1;                       // +0x04
    InitSharedSlot((F3SharedBuffer **)(b + 0x08));          // +0x08
    InitSharedSlot((F3SharedBuffer **)(b + 0x0C));          // +0x0C
    *(DWORD *)(b + 0x10) = 1;                               // +0x10
    *(DWORD *)(b + 0x2C) = 0;                               // +0x2C
    *(DWORD *)(b + 0x30) = 0;                               // +0x30
    InitializeCriticalSection((CRITICAL_SECTION *)(b + 0x34));   // +0x34
    InitSharedSlot((F3SharedBuffer **)(b + 0x4C));          // +0x4C
    InitSharedSlotWide((F3SharedBuffer **)(b + 0x50));       // +0x50
    b[0x54] = 1;                                            // +0x54
    b[0x55] = 1;                                            // +0x55
    b[0x56] = 0;                                            // +0x56..+0x5A
    b[0x57] = 0;
    b[0x58] = 0;
    b[0x59] = 0;
    b[0x5A] = 0;
    *(float *)(b + 0x5C) = 1.0f;                            // +0x5C (dword_1AE4618)
    *(float *)(b + 0x60) = 1.0f;                            // +0x60

    // The +0x68 owner block: a 4-byte block (*block = &obj+0x68).
    DWORD *v2 = (DWORD *)(b + 0x68);
    DWORD *v3 = (DWORD *)LHAlloc(4);
    if (v3 != 0)
        *v3 = (DWORD)v2;
    else
        v3 = 0;
    *v2 = (DWORD)v3;                                        // +0x68

    // The +0x7C self-linked sentinel block (24 bytes, words 0 and 1 = self).
    DWORD *v4 = (DWORD *)LHAlloc(24);
    if (v4 != 0)
        *v4 = (DWORD)v4;
    if (v4 != (DWORD *)-4)
        v4[1] = (DWORD)v4;
    *(DWORD *)(b + 0x7C) = (DWORD)v4;                        // +0x7C
    *(DWORD *)(b + 0x80) = 0;                                // +0x80
    *(DWORD *)(b + 0x84) = 0;                                // +0x84

    // The +0x88 CNetPacketCallback DNameNode base: the CBaseObject vtable
    // store is overwritten immediately by the derived vtable, so only the
    // derived shape is stored (the netinit.cpp pattern).
    *(void **)(b + 0x88) = g_PauseDebugPacketCallbackVtable;
    *(DWORD *)(b + 0x8C) = 0;                                // +0x8C

    // +0x98 = 0.0 / +0xA0 = 0
    *(double *)(b + 0x98) = 0.0;
    b[0xA0] = 0;

    // The self back-pointer (stored before each registration, as the original).
    *(DWORD *)(b + 0x90) = (DWORD)obj;                       // +0x90

    void *nm = GetNetManager();
    void *pm = (nm != 0) ? PauseDebugPacketManager(nm) : 0;

    unsigned char lock[8];
    PauseDebugLockCtor(lock);                                // sub_77DB70
    PauseDebugRegisterPacket(pm, 519, lock, b + 0x88,
                             (void *)PauseDebugRecv519, (void *)PauseDebugSer519);

    void *nm2 = GetNetManager();
    void *pm2 = (nm2 != 0) ? PauseDebugPacketManager(nm2) : 0;
    PauseDebugLockCtor(lock);                                // sub_77DB70 (re-zeroed)
    PauseDebugRegisterPacket(pm2, 520, lock, b + 0x88,
                             (void *)PauseDebugRecv520, (void *)PauseDebugSer520);
    return obj;
}

// PauseDebugLinkedChainRelease  (sub_67F4D0, 0x67F4D0 - 0x67F50D) -- the
// +0x68 owner-chain teardown.  [NOT byte-identical] placement-dependent
// (relative E8 calls to ReleaseSharedSlotWide / LHFree).  The original is
// thiscall (obj in ECX, a pointer to the +0x68 block); a C call cannot stage
// ECX, so the obj is passed explicitly.
//
// Per the original (this == the +0x68 block): v4 = *(obj+0x14) (the +0x7C
// sentinel block); [*v4] = v4; [v4+4] = v4; [obj+0x18] = 0; and when the
// sentinel chain is non-self-linked (v4's first word != v4 -- never happens
// in this build, since the ctor always sets the self-pointer), release every
// node in the chain (ReleaseSharedSlotWide + LHFree) walking the chain.
static void PauseDebugLinkedChainRelease(void *obj)
{
    char *o = (char *)obj;
    DWORD *v4 = (DWORD *)*(DWORD *)(o + 0x14);
    DWORD *v3 = (DWORD *)*v4;
    *v4 = (DWORD)v4;
    *(DWORD *)(v4 + 1) = (DWORD)v4;
    *(DWORD *)(o + 0x18) = 0;
    if (v3 != (DWORD *)*(DWORD *)(o + 0x14))
    {
        for (;;)
        {
            DWORD *v5 = (DWORD *)*v3;
            ReleaseSharedSlotWide((F3SharedBuffer **)(v3 + 2));
            LHFree(v3);
            v3 = v5;
            if (v5 == (DWORD *)*(DWORD *)(o + 0x14))
                break;
        }
    }
}

// PauseDebugInstanceAtExit  (sub_1AC26C0, 0x1AC26C0 - 0x1AC26DA) -- the
// atexit teardown of the pause-debug instance.  [NOT byte-identical]
// placement-dependent (relative E8 calls to CNetPacketCallbackDtor /
// PauseDebugLinkedChainRelease / LHFree / ReleaseSharedSlotWide /
// ReleaseSharedSlot / the CRT DeleteCriticalSection).
//
// Tears the instance down in the original order: the +0x88 CNetPacketCallback
// (CNetPacketCallbackDtor), the +0x68 owner chain (PauseDebugLinkedChainRelease),
// the +0x7C sentinel block and the +0x68 owner block (LHFree each, the +0x7C
// slot zeroed after the first free), the +0x50 wide slot
// (ReleaseSharedSlotWide), the +0x4C slot (ReleaseSharedSlot), the +0x34
// critical section (DeleteCriticalSection), the +0x0C slot (ReleaseSharedSlot)
// and the +0x08 slot (ReleaseSharedSlot).
static void __cdecl PauseDebugInstanceAtExit(void)
{
    char *b = (char *)g_unk_1DBE300;
    CNetPacketCallbackDtor(b + 0x88);                       // +0x88
    PauseDebugLinkedChainRelease(b + 0x68);                 // +0x68 chain

    DWORD *v4 = (DWORD *)*(DWORD *)(b + 0x7C);
    LHFree(v4);                                             // the +0x7C sentinel block
    DWORD *v3 = (DWORD *)*(DWORD *)(b + 0x68);
    LHFree(v3);                                             // the +0x68 owner block
    *(DWORD *)(b + 0x7C) = 0;

    ReleaseSharedSlotWide((F3SharedBuffer **)(b + 0x50));   // +0x50
    ReleaseSharedSlot((F3SharedBuffer **)(b + 0x4C));       // +0x4C
    DeleteCriticalSection((CRITICAL_SECTION *)(b + 0x34));  // +0x34
    ReleaseSharedSlot((F3SharedBuffer **)(b + 0x0C));       // +0x0C
    ReleaseSharedSlot((F3SharedBuffer **)(b + 0x08));       // +0x08
}

// GetPauseDebugInstance  (0x67E460 - 0x67E48F) -- the one-time atexit-install
// + return of &unk_1DBE300.  [NOT byte-identical] placement-dependent
// (relative E8 calls to PauseDebugInstanceCtor / atexit / the high-VA flag
// dword_1DBE3A8).
//
// The one-time pause-debug instance install: while the flag is clear, set it
// (bit 0), construct the instance in place (PauseDebugInstanceCtor) and
// register the atexit teardown (PauseDebugInstanceAtExit, sub_1AC26C0); the
// instance is then returned (&g_unk_1DBE300).  The atexit hook fires once per
// process.
static void *GetPauseDebugInstance(void)
{
    if ((g_dword_1DBE3A8 & 1) == 0)
    {
        g_dword_1DBE3A8 |= 1;
        PauseDebugInstanceCtor(g_unk_1DBE300);
        atexit(PauseDebugInstanceAtExit);
    }
    return (void *)g_unk_1DBE300;
}


// sub_67D070 (0x67D070 - 0x67D078) -- thiscall: sets *(this + 0x59) = a2 and
// returns a2.
static int TurnOnPauseArmed(void *obj, int a2)
{
    *(unsigned char *)((char *)obj + 0x59) = (unsigned char)a2;
    return a2;
}

// sub_6A68A0 (0x6A68A0 - 0x6A68A4) -- thiscall: returns *(this + 0x10).
static int NetManagerSlot10(void *obj)
{
    return (int)*(DWORD *)((char *)obj + 0x10);
}

// sub_687D90 (0x687D90 - 0x687DD5) -- the PauseGamePacket build + send step.
// [STUB] the PauseGamePacket serializer (sub_9EDBB0 / sub_686DE0 / sub_9EDB40)
// is unimplemented; its result is discarded by both callers.
static int SendPauseGamePacket(void *obj, int a2, int a3) { (void)obj; (void)a2; (void)a3; return 0; }

// sub_685520 (0x685520 - 0x685534) -- thiscall: the paused-state send tail-call.
// [STUB] the packet-send chain (sub_6850A0 / sub_684F10) is unimplemented.
static int SendPausedState(void *obj) { (void)obj; return 0; }

// sub_6E4720 (0x6E4720 - 0x6E4739) -- the pause-debug instance ctor step:
// leaves +0x40 / +0x41 zeroed and installs the CGameModePauseDebug vftable.
// Its CGameModePause ctor call (sub_E5F9D0) is unimplemented [STUB].
static void *PauseDebugCtor(void *obj)
{
    ((char *)obj)[0x40] = 0;
    ((char *)obj)[0x41] = 0;
    *(DWORD *)obj = 0;   // [STUB] the CGameModePauseDebug vftable is not modelled
    return obj;
}

// [BYTE-IDENTICAL]  PauseDebugRelease  (0x61E040 - 0x61E051)  -- the thunk's
// release fn: when the SEH block's arg is non-null, dispatch the object's
// deleting destructor (vtable[0](obj, 1)).
//
// The pause-debug thunk-record writer (PauseDebugThunk) registers this as the
// SEH block's handler; the block is released by PauseDebugThunkRelease. The
// original is a cdecl 1-arg function with no absolute refs or relative calls,
// so it emits the original 17 bytes exactly.
static __declspec(naked) int PauseDebugRelease(int)
{
    __asm {
        mov    ecx, [esp+4]
        test   ecx, ecx
        jz     short f3_pd_done
        mov    eax, [ecx]
        mov    edx, [eax]
        push   1
        call   edx
    f3_pd_done:
        retn
    }
}

// sub_61FEC0 (0x61FEC0 - 0x61FF0B) -- the 12-byte thunk-record writer:
// *this = a2; when a2 != 0, allocate the {1, releaseFn, a2} block and store
// it at *(this+4); otherwise leave *(this+4) = 0.  Returns this.
static DWORD *PauseDebugThunk(DWORD *obj, int a2)
{
    *obj = (DWORD)a2;
    if (a2 == 0)
    {
        obj[1] = 0;
        return obj;
    }
    DWORD *v3 = (DWORD *)LHAlloc(12);
    if (v3 != 0)
    {
        DWORD v4 = *obj;
        v3[0] = 1;
        v3[1] = (DWORD)(int (*)(int))PauseDebugRelease;  // PauseDebugRelease: cdecl, 1 stack arg
        v3[2] = v4;
        obj[1] = (DWORD)v3;
    }
    else
    {
        obj[1] = 0;
    }
    return obj;
}

// sub_6E4220 (0x6E4220 - 0x6E4332) -- the entity instance list-size increment.
// [NOT byte-identical] placement-dependent (relative E8 calls down the
// std::logic_error / std::length_error throw chain and the dword_1DDCFC8
// exception hook).  Thiscall + retn 4: ECX = the entity instance (this), the
// stack arg is the amount n.  If 0x1FFFFFFF - *(obj + 0x18) < n (unsigned) it
// builds and throws std::length_error("list<T> too long") (invoking the
// non-NULL callback at dword_1DDCFC8 first); otherwise *(obj + 0x18) += n and
// returns n.  The caller discards the result.
static int EntityInstanceSizeAdd(DWORD *obj, int n)
{
    unsigned *size = (unsigned *)((char *)obj + 0x18);
    if (0x1FFFFFFFu - *size < (unsigned)n)
    {
        std::length_error err("list<T> too long");
        if (g_dword_1DDCFC8 != 0)
            g_dword_1DDCFC8(&err);
        throw err;
    }
    *size += (unsigned)n;
    return n;
}

// sub_6E3E20 (0x6E3E20 - 0x6E3E60) -- the entity instance sub-object
// validator.  [NOT byte-identical] placement-dependent (relative E8 calls to
// __invalid_parameter_noinfo) so C++ reimplementation.  Thiscall + plain retn;
// ECX = the entity instance (this).  Reads *(this) as v1 (the sub-instance
// pointer) and *(this + 0x14) as v2 (the guard word slot), validates the
// chain, and returns v3 + 8 where v3 = *(v2 + 4) (the 3rd slot of the
// sub-object at v2).  The twin checks (the guard word compared against the
// *v1's +0x14 slot, then against the re-derefed v1) are preserved verbatim.
static DWORD EntityInstanceSubPtr(DWORD *obj)
{
    DWORD v1 = *(DWORD *)obj;
    if (v1 == 0)
    {
        F3InvalidParam();
        return 0;   // original: __invalid_parameter_noinfo (no return)
    }
    DWORD v2 = *(DWORD *)((char *)obj + 0x14);
    DWORD v3 = *(DWORD *)(v2 + 4);
    DWORD v4 = *(DWORD *)v1;              // ESI != 0 here (checked above)
    if (v3 == *(DWORD *)(v4 + 0x14))
        F3InvalidParam();
    v1 = *(DWORD *)v1;                    // re-deref (same value as v4)
    if (v3 == *(DWORD *)(v1 + 0x14))
        F3InvalidParam();
    return (DWORD)(v3 + 8);
}

// sub_6E4370 (0x6E4370 - 0x6E43D6) -- the entity instance release-block writer.
// [NOT byte-identical] placement-dependent (relative E8 calls to LHAlloc /
// InterlockedIncrement and the helper EntityInstanceSizeAdd) so C++
// reimplementation.  __stdcall + retn 0Ch (the caller pushes a1/a2/a3);
// ECX = the entity instance (the object whose +0x18 counter is bumped
// below): a1 = *(this) is unused, a2 = *(this + 0x14) (the guard slot) is
// the second stack arg, a3 = the caller's thunk-local is the third.  Builds
// the 16-byte release block
// {a2, old_sub_object_ptr, *a3, a3[1]}, increments the refcount of
// a3[1] (the thunk block) when non-zero, bumps the entity's list size (this
// object +0x18, via EntityInstanceSizeAdd), stores the block at *(a2 + 4)
// (overwriting the old sub-object pointer), writes the block at *(old_
// sub_object_ptr), and returns the old sub-object pointer.
static DWORD *EntityInstanceReleaseBlock(DWORD *obj, DWORD *local)
{
    DWORD a2 = obj[5];
    DWORD v3 = *(DWORD *)(a2 + 4);
    DWORD *v4 = (DWORD *)LHAlloc(16);
    if (v4 != 0)
        v4[0] = a2;
    if (v4 != (DWORD *)-4)
        v4[1] = v3;
    if (v4 != (DWORD *)-8)
    {
        v4[2] = *local;
        v4[3] = local[1];
        if (local[1] != 0)
            InterlockedIncrement((volatile LONG *)local[1]);
    }
    EntityInstanceSizeAdd(obj, 1);
    *(DWORD *)(a2 + 4) = (DWORD)v4;
    *(DWORD *)v3 = (DWORD)v4;
    return (DWORD *)v3;
}

// sub_6E32C0 (0x6E32C0 - 0x6E32FC) -- the entity-instance teardown.
// [NOT byte-identical] placement-dependent (relative E8 calls down the
// entity-instance chain) so C++ reimplementation.  Thiscall + retn 4:
// ECX = the entity instance (this), a2 = the caller's thunk-local (the
// pause-debug release record).  When *(this + 0x18) != 0, validate the
// entity chain through EntityInstanceSubPtr and tail-call the vtable slot 8
// (offset 32) of the sub-instance view at v3 (the returned sub-object pointer
// + 8): ecx = *(v3), eax = *(ecx), call *(eax + 0x20).  Then, unconditionally,
// builds the release block through EntityInstanceReleaseBlock (the guard
// slot + the refcount bump + the list-size bump), and tail-calls the
// vtable slot 5 (offset 0x14) of the thunk-local's object (local[0]) with
// local[0] as the this argument -- the return value of that dispatch is the
// function's return.  [STUB] the callers (pause-debug teardown) are dead in
// this build (the worker callback is never invoked), so this is exercised
// only when the worker setters are wired up.
static int EntityInstanceTeardown(DWORD *obj, DWORD *local)
{
    DWORD *P = (DWORD *)obj[1];
    if (P[6] != 0)
    {
        DWORD v3 = (DWORD)EntityInstanceSubPtr(P);
        DWORD ecx = *(DWORD *)v3;
        DWORD eax = *(DWORD *)ecx;
        void (*vtable8)(DWORD) = (void (*)(DWORD))(*(DWORD *)(eax + 0x20));
        vtable8(ecx);
    }
    EntityInstanceReleaseBlock(P, local);   // result discarded by the original
    DWORD ecx2 = local[0];
    DWORD eax2 = *(DWORD *)ecx2;
    int (*vtable5)(DWORD) = (int (*)(DWORD))(*(DWORD *)(eax2 + 0x14));
    return (int)vtable5(ecx2);
}

// sub_61FF10 (0x61FF10 - 0x61FF4B) -- the thunk-record release: decrement the
// thunk block's refcount; at zero, call the release fn (vtable[0](context, 1))
// and LHFree the block; zero both words of the local.  Returns the LHFree result.
static DWORD PauseDebugThunkRelease(DWORD *obj)
{
    DWORD result;

    result = obj[1];
    if (result != 0)
    {
        result = (DWORD)InterlockedDecrement((volatile LONG *)result);
        if (result == 0)
        {
            DWORD *block = (DWORD *)obj[1];
            ((int (*)(int))block[1])(block[2]);   // PauseDebugRelease(context): vtable[0](context, 1)
            LHFree(block);   // [STUB] LHFree is void in this build; the original returns its result
        }
    }
    obj[0] = 0;
    obj[1] = 0;
    return result;
}

// j_GetNetManager_1 (0x673C30 - 0x673C38) -- thiscall thunk: returns GetNetManager().
static void *j_GetNetManager_1(void *obj) { (void)obj; return GetNetManager(); }

// ===========================================================================
//  Multi-player pause worker-callback cluster (the SECOND VM-state-driven
//  worker callback, registered on the worker at +0x260 via SetMultiplayerPauseOnVMState).
//
//  SubObjSlot4 (sub_658E40) / VmCoreSlot1C (sub_673E80) / VmCoreSlot40
//  (sub_673F80) are the 7-byte thiscall slot getters the multi-player pause
//  flow drives; SubObjSlotGetRelease (sub_6BCB80) / ListEntryGetRelease
//  (sub_BBE910) are the shared-node resolve/release helpers (both release
//  the slot's shared node via PauseDebugSharedNodeRelease when the stored
//  value is null); SubObjListMatch (sub_6B6EC0) walks the 12-byte entry list
//  and returns the first word of the entry whose resolved view matches;
//  NetHeldGate (sub_683770) is the net-manager held gate; NetManagerHeld
//  (sub_6A87A0) is its thunk; NetManagerTeardown (sub_6A66D0) is the
//  net-manager teardown/cleanup step; EntityInstanceRelease (sub_6E2F20) is
//  the entity-instance release thunk (the entity-instance chain, documented
//  no-op); NetOtherPlayerUIPause (sub_684710) is the "other player looking
//  at UI" pause-send handler; NetClientWaitForPause (sub_6AEE30) is the
//  multi-player pause wait handler (both documented no-op stubs).
// ===========================================================================

// sub_673E80 (0x673E80 - 0x673E87) -- the +0x1C slot of the VM core's +0x0C
// sub-object (thiscall): *(*(this + 0x0C) + 0x1C).  Plain C++ (the same
// 7-byte thiscall shape as VMEntityOfCore / NetManagerSlot10 elsewhere in
// this file).
static int VmCoreSlot1C(int core)
{
    return (int)*(DWORD *)(*(DWORD *)(core + 0x0C) + 0x1C);
}

// sub_658E40 (0x658E40 - 0x658E47) -- the +4 slot of the object at (this + 4)
// (thiscall): *(*(this + 4) + 4).
static int SubObjSlot4(int obj)
{
    return (int)*(DWORD *)(*(DWORD *)(obj + 4) + 4);
}

// sub_673F80 (0x673F80 - 0x673F87) -- the +0x40 slot of the VM core's +0x0C
// sub-object (thiscall): *(*(this + 0x0C) + 0x40).
static int VmCoreSlot40(int core)
{
    return (int)*(DWORD *)(*(DWORD *)(core + 0x0C) + 0x40);
}

// sub_683770 (0x683770 - 0x68378E) -- the net-manager held gate (thiscall):
// 1 when (byte at +0x3C set AND byte at +0x3E set) OR (dword at +0x94 set),
// 0 otherwise.  [NOT byte-identical] C++ reimplementation (the original's
// three-branch CMP/JZ/JNZ byte layout is not reproduced); the project's
// established convention for this shape.
static int NetHeldGate(int obj)
{
    BYTE *b = (BYTE *)obj;
    if (b[0x3C] != 0 && b[0x3E] != 0)
        return 1;
    if (*(DWORD *)(b + 0x94) != 0)
        return 1;
    return 0;
}

// sub_6A87A0 (0x6A87A0 - 0x6A87B3) -- the net-manager held gate thunk:
// GetNetManager() -> NetManagerSlot10 -> sub_683770 (the held gate).
// [NOT byte-identical] placement-dependent (relative E8 calls throughout).
static int NetManagerHeld(void)
{
    void *nm = GetNetManager();
    if (nm == 0)
        return 0;
    return NetHeldGate(NetManagerSlot10(nm));
}

// sub_6A66D0 (0x6A66D0 - 0x6A67B2) -- the net-manager teardown/cleanup step
// (thiscall): the one-time teardown install (the unk_1DBE470 cluster + the
// CNetLiveSystem one-time ctor + atexit thunk -- both documented no-op
// stubs in this build), the net-manager's ~19 sub-object releases (all
// documented no-op stubs), then the *(this + 0x4C) &= sub_6A87A0() AND-mask
// and the gate result.  [NOT byte-identical] placement-dependent
// (relative E8 calls throughout) -- C++ reimplementation.
static int NetManagerTeardown(void *obj)
{
    int v = NetManagerHeld();
    if (obj != 0)
    {
        unsigned char *p = (unsigned char *)((char *)obj + 0x4C);
        *p = (unsigned char)(*p & (unsigned char)v);
    }
    return v;
}

// sub_6E2F20 (0x6E2F20 - 0x6E2F28) -- the entity-instance release thunk
// (thiscall): the +4 slot of the entity (the sub-object) + the caller's
// flag arg.  [STUB] the entity-instance chain behind it (sub_6E2DE0's ~8
// unimplemented callees: EntityInstanceSubPtr (implemented), sub_6E41B0 /
// sub_6E4030 / the vtable dispatches) is out of scope -- the arguments are
// accepted and ignored, matching the observable behavior of the no-op stub
// it replaces (the pause release is never exercised in this build).
static void EntityInstanceRelease(void *obj, int a2)
{
    (void)obj; (void)a2;
}

// sub_6BCB80 (0x6BCB80 - 0x6BCBA0) -- the +0xEC slot resolve/release
// (thiscall): v1 = *(this + 0xEC); if v1 != 0, return *v1 when the
// pointed-to block is live, else release the shared node at this + 0xE8 and
// return 0.  [NOT byte-identical] placement-dependent (the relative E8 call
// to PauseDebugSharedNodeRelease).
static int SubObjSlotGetRelease(int obj)
{
    DWORD v1 = *(DWORD *)(obj + 0xEC);
    if (v1 != 0)
    {
        if (*(DWORD *)v1 != 0)
            return *(DWORD *)v1;
        PauseDebugSharedNodeRelease((DWORD *)(obj + 0xE8));
    }
    return 0;
}

// sub_BBE910 (0xBBE910 - 0xBBE92A) -- the +0x10 slot resolve/release
// (thiscall): the same shape with the +0x10 / +0x0C pair.  [NOT
// byte-identical] placement-dependent (the relative E8 call to
// PauseDebugSharedNodeRelease).
static int ListEntryGetRelease(int obj)
{
    DWORD v1 = *(DWORD *)(obj + 0x10);
    if (v1 != 0)
    {
        if (*(DWORD *)v1 != 0)
            return *(DWORD *)v1;
        PauseDebugSharedNodeRelease((DWORD *)(obj + 0x0C));
    }
    return 0;
}

// sub_6B6EC0 (0x6B6EC0 - 0x6B6F5A) -- the shared-node list match step
// (thiscall): walk the 12-byte entries in the {*(this + 0x0C) .. *(this + 0x10)}
// list; for each entry whose resolved shared-node view (via sub_BBE910) is
// non-null, carries the +0xAC flag bit 1, and matches the compare object's
// +0x18 shape (PauseDebugCompare), write the entry's first word to *out and
// return 1.  Otherwise return 0.  [NOT byte-identical] placement-dependent
// (relative E8 calls to sub_BBE910 / PauseDebugCompare).
static int SubObjListMatch(int obj, DWORD compareObj, DWORD *out)
{
    DWORD start = *(DWORD *)(obj + 0x0C);
    DWORD end = *(DWORD *)(obj + 0x10);
    int count = (int)((end - start) / 12);
    if (count == 0)
        return 0;
    int v4 = 0;
    int i = 0;
    for (;;)
    {
        DWORD v6 = ListEntryGetRelease(*(DWORD *)(start + i + 4));
        if (v6 != 0 && (*(unsigned char *)(v6 + 172) & 2) != 0 &&
            PauseDebugCompare((const DWORD *)(v6 + 24), (const DWORD *)(compareObj + 24)))
        {
            *out = *(DWORD *)(start + 12 * v4);
            return 1;
        }
        if (++v4 >= count)
            return 0;
        i += 12;
    }
}

// sub_684710 (0x684710 - 0x68499E) -- the "other player looking at UI"
// pause-send handler (thiscall + retn 4): set *(this + 0x98) = 1 and clear
// *(this + 0x16A); when the +0x11C counter is clear, run the 654-byte
// shared-string build + pause teardown, then increment the counter.
// [STUB] the teardown body (the PauseDebugThunk / sub_9EDDB0 / sub_C2B4E0 /
// sub_C2B090 shared-string build + the CNetLiveSystem install + ~20
// unimplemented helpers) is out of scope -- the observable byte writes and
// the counter increment are preserved; the body is a documented no-op.
// Returns 0 (the original is void; the result is discarded by this build's
// caller).
static int NetOtherPlayerUIPause(int obj, int a2)
{
    (void)a2;
    BYTE *o = (BYTE *)obj;
    if (o == 0)
        return 0;
    o[0x98] = 1;
    o[0x16A] = 0;
    DWORD count = *(DWORD *)(o + 0x11C);
    if (count != 0)
    {
        *(DWORD *)(o + 0x11C) = count + 1;
        return 0;
    }
    // [STUB] the big teardown body -- no-op
    ++*(DWORD *)(o + 0x11C);
    return 0;
}

// sub_6AEE30 (0x6AEE30 - 0x6AF266) -- the multi-player pause wait handler
// (thiscall + retn 8): the 1078-byte "other player looking at UI / paused"
// handler.  [STUB] ~30 unimplemented VM-internal callees (sub_7D4660 /
// sub_6AE490 / sub_BBD6E0 / sub_673E80 / sub_659200 / sub_6A8760 /
// sub_674450 / sub_6AE420 / sub_BBD360 / sub_6AC780 / sub_BBE8D0 /
// sub_BBD860 / sub_BB0FA0 / sub_BB2000 ...) -- the arguments are accepted
// and ignored.  Returns 0 (the original returns the handler's result, which
// is a documented no-op here).
static int NetClientWaitForPause(void *obj, DWORD out, int a3)
{
    (void)obj; (void)out; (void)a3;
    return 0;
}

// sub_619DE0 (0x619DE0 - 0x619FF1) -- the VM-state-driven multi-player pause
// worker callback (registered on the worker at +0x260 via SetMultiplayerPauseOnVMState).
// [NOT byte-identical] placement-dependent (relative E8 calls throughout)
// and refs the high-VA globals g_GameEngine (via GetGameEngine) +
// byte_1DBD9E1 (0x1DBD9E1, kept static here) + ~13 unimplemented engine
// callees (marked [STUB] below); it is only registered (never invoked) in
// this build, so it is dead code here -- exercised only when a multi-player
// pause event occurs with the worker pump live.
//
// The flow (the two pause callbacks PauseGameOnVMState (the +0x25C slot)
// and this one share the VM-state gate + the pause-packet / paused-state
// send path, but this one dispatches the entity sub-object step and also
// handles the multi-player pause states):
//   1. VMStateByte() != 0 gate (returns 0 otherwise).
//   2. KoreGetVMCore() -> VMEntityOfCore() -> EntitySubObject() -> the
//      entity sub-object step (vtable[1]); esi = the step result (v5).
//   3. KoreGetVMCore() -> j_GetNetManager_1() -> NetManagerTeardown()
//      (sub_6A66D0; the net-manager sub-object release pass).
//   4. result == 10: GetPauseDebugInstance() + TurnOnPauseArmed(p, 0);
//      re-read the net manager twice, SendPauseGamePacket(slot10#1, 0, 1)
//      + SendPausedState(slot10#2); return the paused-state result.
//   5. result == 2 && byte_1DBD9E1 != 0: release the previous pause-debug
//      instance (KoreGetVMCore() -> VMEntityOfCore() ->
//      EntityInstanceRelease -- the documented no-op thunk), clear the
//      flag, SendPauseGamePacket(slot10#1, 0, 1) + SendPausedState(slot10#2).
//   6. VM core slot6 dispatch (the [edx+0x18] thiscall call); if its result
//      is non-zero, return it.
//   7. The entity sub-object step again; if the result is 1/2/8, return it.
//   8. Net manager slot10 + 362 flag: if set, run NetOtherPlayerUIPause
//      (the multi-player "other player looking at UI" handler) and return.
//   9. Otherwise VmCoreSlot1C() -> SubObjSlot4() -> SubObjSlotGetRelease();
//      if the resolved object non-null AND its +0xAC byte carries flag bit 1,
//      VmCoreSlot40() -> SubObjListMatch() -> NetClientWaitForPause();
//      return the handler's result.
//  The fall-through (all paths) returns the raw result.
static int MultiplayerPauseOnVMState(void)
{
    int result = VMStateByte();
    if (result == 0)
        return result;

    int core = KoreGetVMCore();
    int entity = VMEntityOfCore(core);
    int sub = EntitySubObject((void *)entity);
    int stepResult = (sub != 0)
        ? ((int (__thiscall *)(DWORD))(*(DWORD *)sub + 4))(sub) : 0;   // vtable[1]

    core = KoreGetVMCore();
    void *nm = j_GetNetManager_1((void *)core);
    NetManagerTeardown(nm);

    if (stepResult == 10)
    {
        // The pause-armed(0) + pause-packet + paused-state path.
        void *p = GetPauseDebugInstance();
        TurnOnPauseArmed(p, 0);   // sets *(p + 0x59) = 0
        core = KoreGetVMCore();
        nm = j_GetNetManager_1((void *)core);
        int v1 = (nm != 0) ? NetManagerSlot10(nm) : 0;
        SendPauseGamePacket((void *)v1, 0, 1);
        core = KoreGetVMCore();
        nm = j_GetNetManager_1((void *)core);
        int v2 = (nm != 0) ? NetManagerSlot10(nm) : 0;
        return SendPausedState((void *)v2);
    }

    if (stepResult == 2 && g_byte_1DBD9E1 != 0)
    {
        // Release the previous pause-debug instance + the pause-packet path.
        core = KoreGetVMCore();
        entity = VMEntityOfCore(core);
        EntityInstanceRelease((void *)entity, stepResult);   // the documented no-op thunk
        g_byte_1DBD9E1 = 0;
        core = KoreGetVMCore();
        nm = j_GetNetManager_1((void *)core);
        int v1 = (nm != 0) ? NetManagerSlot10(nm) : 0;
        SendPauseGamePacket((void *)v1, 0, 1);
        core = KoreGetVMCore();
        nm = j_GetNetManager_1((void *)core);
        int v2 = (nm != 0) ? NetManagerSlot10(nm) : 0;
        SendPausedState((void *)v2);
    }

    // The VM core slot6 dispatch.
    core = KoreGetVMCore();
    result = (core != 0)
        ? ((int (__thiscall *)(DWORD))(*(DWORD *)core + 24))(core) : 0;   // vtable[6]
    if (result != 0)
        return result;

    // The entity sub-object step again.
    core = KoreGetVMCore();
    entity = VMEntityOfCore(core);
    sub = EntitySubObject((void *)entity);
    result = (sub != 0)
        ? ((int (__thiscall *)(DWORD))(*(DWORD *)sub + 4))(sub) : 0;   // vtable[1]

    if (result == 1 || result == 2 || result == 8)
        return result;

    core = KoreGetVMCore();
    nm = j_GetNetManager_1((void *)core);
    if (nm != 0 && *(unsigned char *)((char *)NetManagerSlot10(nm) + 362) != 0)
    {
        core = KoreGetVMCore();
        nm = j_GetNetManager_1((void *)core);
        int v1 = (nm != 0) ? NetManagerSlot10(nm) : 0;
        return NetOtherPlayerUIPause(v1, 0);
    }

    core = KoreGetVMCore();
    int obj1 = VmCoreSlot1C(core);
    int obj2 = SubObjSlot4(obj1);
    int result2 = SubObjSlotGetRelease(obj2);

    if (result2 != 0 && (*(unsigned char *)(result2 + 172) & 2) != 0)
    {
        DWORD out = 0;
        core = KoreGetVMCore();
        int obj3 = VmCoreSlot40(core);
        int r2 = SubObjListMatch(obj3, (DWORD)result2, &out);
        if (r2 != 0)
        {
            void *engine = GetGameEngine();
            void *gui = (engine != 0) ? *(void **)((char *)engine + 0xB8) : 0;
            return NetClientWaitForPause(gui, out, 1);
        }
    }

    return result2;
}


static int PauseGameOnVMState(void)
{
    int result = VMStateByte();

    if (result != 0)
    {
        int core = KoreGetVMCore();
        int v2 = VMEntityOfCore(core);
        int v3 = EntitySubObject((void *)v2);

        // The vtable[1] dispatch (the original calls it unconditionally).
        // v3 is the real return of EntitySubObject (*(v4 + 8));
        // keep the null guard so the dispatch stays safe when the entity
        // build/validate failed (the original would null-deref on a null v3).
        result = (v3 != 0) ? ((int (*)(int))((void **)v3)[1])(v3) : 0;

        if (result == 10)
        {
            void *p = GetPauseDebugInstance();
            TurnOnPauseArmed(p, 1);   // sets *(p + 0x59) = 1
        }
        else
        {
            if (result == 2)
                return result;
            if (g_byte_1DBD9E1 != 0)
                return result;

            // Release the pause-debug instance (the one GetPauseDebugInstance installed).
            int v9 = (int)(DWORD)LHAlloc(0x44);
            int v10 = (v9 != 0) ? (int)(DWORD)PauseDebugCtor((void *)v9) : 0;
            DWORD local[2];
            PauseDebugThunk(local, v10);
            int core2 = KoreGetVMCore();
            int v13a = VMEntityOfCore(core2);
            EntityInstanceTeardown((DWORD *)v13a, local);
            PauseDebugThunkRelease(local);
            g_byte_1DBD9E1 = 1;
        }

        // The pause sequence (shared by both paths).
        int vc = KoreGetVMCore();
        void *nm = j_GetNetManager_1((void *)vc);
        // [STUB] the original reads *(nm + 0x10) unconditionally (the
        // net manager is always installed in the original build); a null
        // net manager here is possible pre-start, so guard the +0x10 read.
        int v5 = (nm != 0) ? NetManagerSlot10(nm) : 0;
        SendPauseGamePacket((void *)v5, 1, 1);
        int vc2 = KoreGetVMCore();
        void *nm2 = j_GetNetManager_1((void *)vc2);
        int v8 = (nm2 != 0) ? NetManagerSlot10(nm2) : 0;
        return SendPausedState((void *)v8);
    }
    return result;
}
// sub_18FAC40 (0x18FAC40 - 0x18FACB9) -- the heap manager's audio-mixer
// "armed" level setter (thiscall, retn 4: the heap manager in ECX, the flag
// byte on the stack).  Under the manager critical section (stru_20FC8AC),
// when the byte at +0x79 differs from the flag: store it, recompute the
// mixer level (0.0f while the flag is set, the saved float at +0x7C otherwise)
// and stage the CSetGlobalMixer audio-comms command through the
// VfsAllocCacheAlloc / VfsAllocCacheFinalize pair (the CSetGlobalMixer vftable
// is the original VA 0x1B6F890, an opaque marker like the other rdata vtables;
// the staged object is a no-op in this build: g_unk_1DDC830 is zero BSS because
// InitVirtualFileSystem is never reached, exactly like the audio-comms
// wrappers above).  The identity ctor sub_145D7C0 and the null dtor nullsub_124
// are side-effect free and omitted.
// [NOT byte-identical] placement-dependent (relative E8 calls to the IAT slots)
// and refs the high-VA globals g_unk_1DDC830 (via the VfsAllocCacheAlloc /
// VfsAllocCacheFinalize pair) and g_dword_2075ED8 (via GetHeapManager).
static void HeapManagerSetArmed(void *hm, int a2)
{
    if (hm == 0)
        return;   // [STUB-guard] the original would null-deref pre-start
    EnterCriticalSection(MgrCs());
    if (*(BYTE *)((char *)hm + 0x79) != (BYTE)a2)
    {
        *(BYTE *)((char *)hm + 0x79) = (BYTE)a2;
        float level;
        if (a2 != 0)
            level = 0.0f;
        else
            level = *(float *)((char *)hm + 0x7C);
        DWORD levelBits;
        memcpy(&levelBits, &level, sizeof(levelBits));
        // sub_145D7F0 (stdcall forwarder) -> sub_14610B0: stage the
        // CSetGlobalMixer command (the +4 float slot, the +8 1000 slot, the
        // vftable at +0).
        DWORD *obj = (DWORD *)VfsAllocCacheAlloc(g_unk_1DDC830, 1);
        if (obj != 0) {
            obj[0] = 0x1B6F890;   // [STUB] the CSetGlobalMixer vftable (original VA)
            obj[1] = levelBits;
            obj[2] = 1000;
            VfsAllocCacheFinalize(g_unk_1DDC830);
        }
    }
    LeaveCriticalSection(MgrCs());
}

// sub_674660 (0x674660 - 0x674764) -- the VM-state pause step: resolve the
// VM core's sub-object release (SubObjSlot4 over *(*(core + 0x0C) + 0x1C),
// SubObjSlotGetRelease over the resolved object), test its +0xAC flag bit 1
// and +0x2C 0x20 mask, then -- on the pass -- read the +0xA8 dword (or
// binary-search the +0x50 list via sub_640AC0), resolve the VM core's +0x1C
// / +0x40 slots (sub_659200 over *(*(dword_1DBDD4C + 0x0C) + 0x1C)), run the
// sub_AADAB0 / sub_91A200 / sub_9134F0(28) / sub_9134F0(7) gate chain,
// dispatch sub_BFC490(v4), tail-call sub_9155A0(v6, a2) and return
// sub_914610(1).  [STUB] the ~8 unimplemented VM-internal callees
// (sub_640AC0 / sub_659200 / sub_AADAB0 / sub_91A200 -- the recursive
// sub-object walk -- sub_9134F0 / sub_BFC490 / sub_9155A0 / sub_914610) are
// not implemented in this build, so the whole step -- the 2 implemented
// slot-read/release primitives (SubObjSlot4 / SubObjSlotGetRelease) included
// -- is documented as a no-op here (it is also only reached through a state
// that never occurs in this build, the VM being uncreated).  The result is
// discarded by ArmedPauseOnVMState, the only caller.  The argument is accepted
// and ignored.
static int VMPauseCoreStep(int core)
{
    (void)core;
    return 0;
}

// sub_67D090 (0x67D090 - 0x67D097) -- thiscall: sets *(this + 0x5A) = a2 and
// returns a2.  The +0x5A-byte sibling of TurnOnPauseArmed (sub_67D070, +0x59),
// installed on the pause-debug instance by ArmedPauseOnVMState.
static int PauseDebugArmedSet(void *obj, int a2)
{
    *(BYTE *)((char *)obj + 0x5A) = (BYTE)a2;
    return a2;
}

// sub_6199C0 (0x6199C0 - 0x619A61) -- the third VM-state-driven worker callback
// (registered on the worker at +0x264 via SetArmedPauseOnVMState).  The audio-armed /
// window-placement sibling of the two pause callbacks above: when the worker
// callback runs with a1 == 0, first arm the heap manager's audio-mixer level
// (silenced, via HeapManagerSetArmed) and -- while the VM reports something
// active -- run the VM-state pause step (VMPauseCoreStep, result discarded);
// then read the main window's placement (GetWindowPlacement over the manager
// vtable[8] GetWindow).  When the window is not maximized (showCmd != 2) or
// the "arm" flag is clear, install the one-time pause-debug instance and set
// its +0x5A flag to the arm flag (PauseDebugArmedSet) and return it; otherwise
// return the GetWindowPlacement result.
// [NOT byte-identical] placement-dependent (relative E8 calls throughout) and
// refs the high-VA globals g_dword_2075ED8 (via GetHeapManager), g_unk_1DDC830
// (via the VfsAllocCacheAlloc / VfsAllocCacheFinalize pair in
// HeapManagerSetArmed), the pause-debug instance (GetPauseDebugInstance) and
// the manager vtable[8] dispatch -- C++ reimplementation.
static int ArmedPauseOnVMState(int a1)
{
    void *hm = GetHeapManager();
    HeapManagerSetArmed(hm, a1 == 0);   // sub_18FAC40

    if (a1 == 0 && VMStateByte() != 0)
    {
        int core = KoreGetVMCore();
        VMPauseCoreStep(core);   // sub_674660 -- [STUB]
    }

    WINDOWPLACEMENT wndpl;
    memset(&wndpl, 0, sizeof(wndpl));
    wndpl.length = 44;

    void *mgr   = GetManager();
    void **vt   = (void **)*(void ***)mgr;
    HWND hwnd   = (HWND)((int (*)(void *))vt[8])(mgr);
    BOOL result = GetWindowPlacement(hwnd, &wndpl);

    if (wndpl.showCmd != 2 || a1 == 0)
    {
        // sub_67E460 + sub_67D090: install the pause-debug instance and set
        // its +0x5A flag to the arm flag.
        PauseDebugArmedSet(GetPauseDebugInstance(), a1 == 0);
        return a1 == 0;
    }
    return result;
}
// sub_6904F0 (0x6904F0 - 0x690817) -- the "GUI_SCREEN_COOP_FAILED" screen step
// (thiscall + retn 4): the coop-failed screen dispatch body hung off the
// net-held worker callback -- the gate's +0x3C/+0x3E/+0x94 branch (the
// STRICT path: KoreGetVMCore -> sub_673BB0 -> unknown_libname_451 ->
// sub_720310 -> AssignSharedSlotWide -> ReleaseSharedSlotWide -> sub_1A00310
// -> sub_5C1740 -> sub_1A00810 -> ReleaseSharedSlotWide/ sub_1A00310 ->
// sub_675CA0 -> sub_B73360 -> SharedSlotToWide -> sub_1A00810 ->
// ReleaseSharedSlotWide/ ReleaseSharedSlot/ ReleaseSharedSlotWide), then the
// slot-136/140 guard + sub_68F700, then the coop-failed branch (the one-time
// CNetLiveSystem install + SetSharedSlot("GUI_SCREEN_COOP_FAILED") + the
// Slot24 / sub_C1A150 / SetSaveGamePath chain + the VMStateByte / sub_6743D0 /
// sub_71C410 / sub_7D6E60 / sub_7D4090 / sub_B80A00 / sub_6743A0 screen
// dispatch + the CGameVersionCtor / GetGameEngine / sub_761110 / sub_68F6B0 /
// sub_623280 tail + the final one-time CNetLiveSystem install).  [STUB] the
// ~35 unimplemented VM-internal callees (sub_687050, KoreGetVMCore,
// sub_673BB0, unknown_libname_451, sub_720310, AssignSharedSlotWide,
// ReleaseSharedSlotWide, sub_1A00310, sub_5C1740, sub_1A00810, sub_675CA0,
// sub_B73360, SharedSlotToWide, ReleaseSharedSlot, sub_68F700,
// CNetLiveSystemCtor, CNetLiveSystemAtExit, SetSharedSlot,
// CNetManagerGetSlot24, sub_C1A150, SetSaveGamePath, VMStateByte,
// sub_6743D0, sub_71C410, sub_7D6E60, sub_7D4090, sub_B80A00, sub_6743A0,
// CGameVersionCtor, GetGameEngine, GetGameEngineVersionTable, sub_761110,
// sub_68F6B0, sub_623280, ReleaseSharedSlot, _invalid_parameter_noinfo) are
// not implemented in this build -- the whole body is documented as a no-op
// here.  The arguments are accepted and ignored; the only caller
// (NetHeldCoopFailed, below) is itself a worker callback that is never
// invoked in this build.
static void NetCoopFailedScreen(int obj, int a2)
{
    (void)obj; (void)a2;
}

// sub_619A70 (0x619A70 - 0x619A8F) -- the net-held-gate worker callback
// (registered on the worker at +0x268 via SetNetHeldCoopFailed): when the net-manager
// held gate (NetManagerHeld -- the +0x10 slot's +0x3C/+0x3E/+0x94 gate) fires,
// resolve the net manager's +0x10 slot (NetManagerSlot10, the same object the
// gate checks) and kick the coop-failed screen step (NetCoopFailedScreen,
// sub_6904F0 -- [STUB], see its comment) with a2 = 0.  The original's
// stack-arg idiom (push 0 before the GetNetManager call; sub_6904F0 returns
// via retn 4, popping the pushed 0) is simply the a2 = 0 stack argument.
// [NOT byte-identical] placement-dependent (relative E8 calls throughout) --
// C++ reimplementation; the gate thunk above already carries the same shape.
static int NetHeldCoopFailed(void)
{
    int result = NetManagerHeld();
    if (result != 0)
    {
        void *nm = GetNetManager();
        NetCoopFailedScreen(NetManagerSlot10(nm), 0);
    }
    return result;
}

// Worker callback setters (thiscall, worker in ECX + one function pointer).
// The +0x25C setter is the byte-identical SetPauseGameOnVMState, the +0x260
// setter is the byte-identical SetMultiplayerPauseOnVMState, the +0x264
// setter is the byte-identical SetArmedPauseOnVMState and the +0x268 setter is
// the byte-identical SetNetHeldCoopFailed (worker.cpp).

//  FinalizeModelHeaders  (0x18013B0 - 0x18013C6)  -- forwards the 28-byte
//  "globals_model_headers" SSO string to the file-subsystem validation chain
//  (sub_1811F50 / sub_1819080 / sub_181B6E0) and returns the string.
//  [NOT byte-identical] placement-dependent (the E8 call to sub_1811F50).
//
//  Faithful call sequence (the original's bytes at 0x18013B0 - 0x18013C6):
//    eax = param2 (&string); esi = param2; push eax; push esi;
//    call sub_1811F50 (this=ecx, param1=param2=&string); eax = esi; return esi.
//  The callee chain (sub_1819080 / sub_181B6E0 / sub_181AF00 / sub_724E10 /
//  sub_6F9AE0 / __invalid_parameter_noinfo) is NOT implemented in this project.
//  Established from the callee's bytes (0x1811F50 - 0x1811F81 + 0x1819080):
//  in the normal case (vtable set + the string's len != 0) sub_1819080 always
//  returns 0, so sub_1811F50 takes its else branch and WRITES the string's
//  first dword to 0 before returning it.  That side effect is reproduced here;
//  the caller then stores the zeroed first dword into the engine's +0x7C slot.
//  The a1 (out-holder) parameter is ignored, exactly as the original does.
static void *FinalizeModelHeaders(void *obj, void *a, void *b)
{
    (void)obj;       // the file-subsystem object (this, passed through)
    (void)b;         // the out-holder (a1) -- ignored by the callee
    *(void **)a = 0; // sub_1811F50's else branch (the normal case)
    return a;
}
// 0x18E8CD0 - 0x18E8D31 -- SetEngineService
// [NOT byte-identical] placement-dependent (relative calls to the impl
// builder / pair teardown and the LHAlloc / InterlockedDecrement /
// InitializeCriticalSection IAT slots).
//
// Installs the engine-service object + SEH cleanup pair (dword_20FD1AC /
// dword_20FD1B0):
//   12-byte object { vtable (&off_1BE7424), impl, sehBlock } built by
//   BuildEngineServiceObject (sub_18E8C60): the 136-byte impl object via
//   EngineServiceInitImpl (sub_57AEC0 + sub_57E260), and the impl's own
//   12-byte SEH block { refcount = 1, handler = DisposeEngineServiceImpl
//   (sub_57CC30), arg = the impl }.
//   The pair block is 12 bytes { refcount = 1, handler = DisposeEngineService
//   (sub_57CC50), arg = the object }.
//
// The old pair is released first (ReleaseEngineServicePair -- sub_57FDF0:
// decrement the old block's refcount; on zero dispatch the old handler with
// the old arg, then LHFree the block; both globals are cleared). The new
// object is stored in g_engineServiceObject, and -- when non-null and the SEH
// block allocation succeeds -- the fresh block is stored in
// g_engineServiceSehBlock. The return value is the sub_57FDF0 result (or the
// fresh block pointer) -- both callers discard it, exactly as the original.
static void *g_engineServiceObject = 0;    // dword_20FD1AC
static void *g_engineServiceSehBlock = 0;  // dword_20FD1B0
static void *g_engineServiceVtable[2] = { (void *)F3StubRelease, 0 };

// The 136-byte engine-service impl object (sub_57AEC0 / sub_57E260).
// [NOT byte-identical] placement-dependent (relative calls to LHAlloc and
// InitializeCriticalSection); documented deviations: the 48-byte head's zero
// stores are unconditional when the allocation succeeds (the original's
// lea-vs-zero guards are only reachable when v1 == 0, where the original
// would crash on the [v1+2C] store -- it is treated as a fatal alloc failure
// here and the head is simply left 0), and no fields other than the
// documented ones are zeroed.
static void *EngineServiceInitImpl(void *impl)
{
    char *p = (char *)impl;
    p[0] = 0;
    *(DWORD *)(p + 0x30) = 0;
    *(DWORD *)(p + 0x34) = 0;
    *(DWORD *)(p + 0x38) = 0;
    *(DWORD *)(p + 0x44) = 0;
    *(DWORD *)(p + 0x48) = 0;
    *(DWORD *)(p + 0x4C) = 0;
    // the self-linked 48-byte list head (sub_57E260): [0]/[4]/[8] = head,
    // byte 0x2C = 1 (0x2D = 1 follows in the original's TailCall idiom)
    void *head = LHAlloc(48);
    if (head != 0)
    {
        *(DWORD *)((char *)head + 0x00) = (DWORD)head;
        *(DWORD *)((char *)head + 0x04) = (DWORD)head;
        *(DWORD *)((char *)head + 0x08) = (DWORD)head;
        ((BYTE *)head)[0x2C] = 1;
        ((BYTE *)head)[0x2D] = 1;
    }
    *(void **)(p + 0x18) = head;
    *(DWORD *)(p + 0x1C) = 0;
    // the +0x50 4-byte back-pointer block (the original allocates it even
    // on failure; the back-pointer content is only written on success)
    void *block = LHAlloc(4);
    if (block != 0)
        *(void **)block = (void *)(p + 0x50);
    *(void **)(p + 0x50) = block;
    InitializeCriticalSection((LPCRITICAL_SECTION)(p + 112));
    return impl;
}

// The impl-object destructor (sub_57CC30): the deep impl teardown (sub_57DAF0,
// the +0x44/+0x48 and +0x30/+0x34 sub-pair lists + the sub_57C3F0 / sub_580DA0
// release chain) is a documented no-op here -- the lists are empty in this
// build and the impl is never released (the service-stop step is a stub), so
// nothing observable runs beyond the critical-section release.
static int DisposeEngineServiceImpl(int arg)
{
    if (arg == 0)
        return 0;
    DeleteCriticalSection((LPCRITICAL_SECTION)((char *)arg + 112));
    LHFree((void *)arg);
    return 0;
}

// The deleting-destructor dispatcher for the 12-byte object (sub_57CC50):
// obj->vtable[0](obj, 1).
static int DisposeEngineService(int arg)
{
    if (arg == 0)
        return 0;
    void **vt = *(void ***)(void *)arg;
    return ((int (*)(void *, int))vt[0])((void *)arg, 1);
}

// The 12-byte object builder (sub_18E8C60) -- returns the (new) 12-byte object.
static void *BuildEngineServiceObject(void *obj)
{
    *(void **)obj = g_engineServiceVtable;
    void *impl = LHAlloc(136);
    if (impl != 0)
        impl = EngineServiceInitImpl(impl);
    *(void **)((char *)obj + 4) = impl;
    if (impl != 0)
    {
        void *block = LHAlloc(12);
        if (block != 0)
        {
            *(DWORD *)block = 1;                              // refcount
            *(void **)((char *)block + 4) = (void *)DisposeEngineServiceImpl;
            *(DWORD *)((char *)block + 8) = (DWORD)impl;
            *(void **)((char *)obj + 8) = block;
        }
        else
        {
            *(void **)((char *)obj + 8) = 0;
        }
    }
    else
    {
        *(void **)((char *)obj + 8) = 0;
    }
    return obj;
}

// The pair teardown (sub_57FDF0) -- decrement the old block's refcount; on
// zero dispatch its handler with its arg, then LHFree the block. Both globals
// are cleared. Returns the decrement result (0 when there is no block or the
// refcount reached zero -- the original returns the heap-free return value in
// the zero case, which no caller reads).
static int ReleaseEngineServicePair(void)
{
    void *block = g_engineServiceSehBlock;
    int result = 0;
    if (block != 0)
    {
        result = InterlockedDecrement((volatile LONG *)block);
        if (result == 0)
        {
            ((int (*)(int))*(void **)((char *)block + 4))((int)*(DWORD *)((char *)block + 8));
            LHFree(block);
        }
    }
    g_engineServiceObject = 0;
    g_engineServiceSehBlock = 0;
    return result;
}

// The pair installer (sub_18E8CD0).
static int SetEngineService(void)
{
    void *obj = LHAlloc(12);
    if (obj != 0)
        obj = BuildEngineServiceObject(obj);
    else
        obj = 0;
    int result = ReleaseEngineServicePair();
    g_engineServiceObject = obj;
    if (obj != 0)
    {
        void *block = LHAlloc(12);
        if (block != 0)
        {
            *(DWORD *)block = 1;                              // refcount
            *(void **)((char *)block + 4) = (void *)DisposeEngineService;
            *(DWORD *)((char *)block + 8) = (DWORD)obj;
            g_engineServiceSehBlock = block;
            return (int)block;
        }
        g_engineServiceSehBlock = 0;
        return 0;
    }
    return result;
}
//  GetEngineService  (0x18E8960 - 0x18E8966)  -- return the current engine-service object
//  [NOT byte-identical] placement-independent but references the high-VA
//  global dword_20FD1AC (0x020FD1AC) not present in this build.
//  A tiny getter (mov eax, dword_20FD1AC; retn) used pervasively by the
//  engine to reach the current engine-service object.
static void *GetEngineService(void)
{
    return g_engineServiceObject;
}
//  sub_57B160  (0x57B160 - 0x57B255)  -- the engine-service impl worker
//  [STUB] still unimplemented: a 245-byte __thiscall body (the impl's
//  +0x70 critical section, the +0x28 sub-object build (sub_57CEC0), the
//  +0x34 pointer check, the +0x28 teardown (sub_57CE70) and its
//  sub_57EDF0 / sub_57CF60 / sub_5807D0 callees) is not reimplemented here.
//  ForwardEngineServiceImpl (the tail-call thunk below) forwards the impl
//  here, exactly as the original's E9 jmp does.
static void sub_57B160(void *impl)
{
    (void)impl;
}
//  ForwardEngineServiceImpl  (0x18E8D50 - 0x18E8D58)  -- engine-service object
//  tail-call thunk: load the +4 impl and tail-jump to the impl worker
//  (sub_57B160). [NOT byte-identical] the original is an 8-byte tail-call
//  (mov ecx, [ecx+4]; jmp sub_57B160) whose E9 displacement is
//  placement-dependent, so it is implemented in C++ with a direct call
//  instead of the inline tail jump.
static void ForwardEngineServiceImpl(void *obj)
{
    // mov ecx, [ecx+4]
    void *impl = *(void **)((char *)obj + 4);
    sub_57B160(impl);      // tail call: forward the impl to the worker
}
//  SetEngineStartedFlag  (0x18E7180 - 0x18E7188)  -- 8-byte thunk: set the
//  engine-started flag (byte_2075F2B) to 1.  [NOT byte-identical] the original
//  stores to the high-VA global 0x2075F2B not present in this build, so it is
//  implemented in C++ over a static global.
static void SetEngineStartedFlag(void) { g_byte_2075F2B = 1; }
//  TextureStreamerCs  -- lazy-init the Texture Streamer critical section
//  (stru_20FD1B4).  The original relies on stru_20FD1B4 being initialized at
//  startup; here we InitializeCriticalSection on first use (the same
//  lazy-init pattern as MgrCs above).
static CRITICAL_SECTION *TextureStreamerCs(void)
{
    if (!g_cs_20FD1B4Init) { InitializeCriticalSection(&g_cs_20FD1B4); g_cs_20FD1B4Init = 1; }
    return &g_cs_20FD1B4;
}
//  TextureStreamerWork  (0x18E7FC0 - 0x18E8460)  -- the streamer work loop
//  body (0x1184 bytes) is NOT implemented in this build.  [STUB] reports no
//  pending work (0), so the thread's while loop exits immediately.
static int TextureStreamerWork(void) { return 0; }
//  TextureStreamerThread  (0x18E8460 - 0x18E84BF)  -- the "Texture Streamer"
//  thread entry (started by StartTextureStreamer with no argument):
//  announce the thread name, mark the streamer running under the critical
//  section, then spin while the streamer has pending work, sleeping 1 ms per
//  pass; on exit, clear the stop-arm flag and the running flag.
//  [NOT byte-identical] refs the high-VA globals stru_20FD1B4 / byte_2075F38 /
//  byte_2075F39 and the SetThreadName / Enter / LeaveCriticalSection / Sleep
//  IAT slots.
static void TextureStreamerThread(void *arg)
{
    (void)arg;
    SetThreadName("Texture Streamer");
    CRITICAL_SECTION *cs = TextureStreamerCs();
    EnterCriticalSection(cs);
    g_byte_2075F38 = 1;
    LeaveCriticalSection(cs);
    while (TextureStreamerWork() != 0)
        Sleep(1u);
    g_byte_2075F39 = 0;
    g_byte_2075F38 = 0;
}
//  StartTextureStreamer  (0x18E84C0 - 0x18E84F4)  -- the guarded thread
//  spawner: under the Texture Streamer critical section, read the running
//  flag; when it is 0, spawn the "Texture Streamer" thread
//  (TextureStreamerThread) with no argument.
//  [NOT byte-identical] refs the high-VA globals stru_20FD1B4 /
//  byte_2075F38 and the Enter / LeaveCriticalSection / _beginthread IAT
//  slots.
static void StartTextureStreamer(void)
{
    CRITICAL_SECTION *cs = TextureStreamerCs();
    EnterCriticalSection(cs);
    BYTE running = g_byte_2075F38;
    LeaveCriticalSection(cs);
    if (running == 0)
        _beginthread(TextureStreamerThread, 0u, NULL);
}
//  CHeroUniqueIDCtor  (0xC1E6A0 - 0xC1E6CB)  -- the CHeroUniqueID member ctor
//  [NOT byte-identical] placement-dependent (relative E8 calls to
//  InitSharedSlotWide).
//
//  void *CHeroUniqueIDCtor(void *obj)
//
//  Construct the CHeroUniqueID sub-object embedded at CLoadingScreen +0x18
//  (this = the member base): store the (placeholder) vtable, zero the four
//  dwords at +8 / +0xC / +0x10 / +0x14, zero the 8-byte quad word at +0x18
//  (the original xorps'd xmm0 over [esi+18h]), and run InitSharedSlotWide
//  on the wide-string slot at +0x20 (the wide-shared-slot null-ctor). The
//  original is thiscall (obj in ECX, plain retn); declared here as a plain
//  free function per the project convention.
static void *CHeroUniqueIDCtor(void *obj)
{
    DWORD *m = (DWORD *)obj;
    *(void **)obj = g_CHeroUniqueIDVtable;
    m[2] = 0;
    m[3] = 0;
    m[4] = 0;
    m[5] = 0;
    m[6] = 0;
    m[7] = 0;
    InitSharedSlotWide((F3SharedBuffer **)((char *)obj + 0x20));
    return obj;
}
//  CLoadingScreenCtor  (0x7330F0 - 0x733158)  -- the NUI::CLoadingScreen ctor
//  [NOT byte-identical] placement-dependent (relative E8 calls to
//  CBaseClassNonCopyableCtor / LHAlloc / InitializeCriticalSection /
//  InitSharedSlot / CHeroUniqueIDCtor / sub_18A88B0 / srand).
//
//  void *CLoadingScreenCtor(void *obj)
//
//  Construct the 88-byte NUI::CLoadingScreen object: run the
//  CBaseClassNonCopyableCtor (base vtable, overwritten below), store the
//  (placeholder) vtable, zero +4, allocate a 24-byte critical section via
//  LHAlloc(24) and InitializeCriticalSection it in place (stored at +8;
//  NULL on allocation failure), zero the +0xC flag byte, InitSharedSlot the
//  +0x10 shared slot, zero +0x14, run the CHeroUniqueID member ctor at
//  +0x18, zero +0x40..+0x50, and seed the RNG from the low word of the
//  QueryPerformanceCounter (the original pushes EAX -- the LowPart -- as
//  srand's Seed). Then return obj. The original is thiscall (obj in ECX,
//  plain retn); declared here as a plain free function per the project
//  convention.
static void *CLoadingScreenCtor(void *obj)
{
    F3BaseClassNonCopyableCtor(obj);
    *(void **)obj = g_CLoadingScreenVtable;
    *(void **)((char *)obj + 4) = 0;
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)LHAlloc(24u);
    if (cs != 0)
        InitializeCriticalSection(cs);
    *(void **)((char *)obj + 8) = cs;
    *(unsigned char *)((char *)obj + 12) = 0;
    InitSharedSlot((F3SharedBuffer **)((char *)obj + 16));
    *(void **)((char *)obj + 20) = 0;
    CHeroUniqueIDCtor((char *)obj + 24);
    *(void **)((char *)obj + 64) = 0;
    *(void **)((char *)obj + 68) = 0;
    *(void **)((char *)obj + 72) = 0;
    *(void **)((char *)obj + 76) = 0;
    *(void **)((char *)obj + 80) = 0;
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    srand((int)q.LowPart);
    return obj;
}
//  CGameRendererInnerCtor  (sub_731330, 0x731330 - 0x731484)  -- the 776-byte
//  CGameRenderer inner-state constructor.
//  [NOT byte-identical] placement-dependent (relative calls to
//  InitializeCriticalSection / EventObjCtor / LHAlloc / InitIntOptionMap /
//  InitFloatOptionMap / InitDetailOptionMap / InitVideoOptionsDefaults).
//
//  void *CGameRendererInnerCtor(void *obj)
//
//  Constructs the 776-byte inner state of a CGameRenderer (passed by the
//  CGameRendererCtor at CGameEngine +0x9C):
//    +0x00 / +0x18 / +0x30  the three 24-byte critical sections
//    +0x48 / +0x50 / +0x60 / +0x68  the 8-byte zeroed qword quad
//    +0x58  the DWORD zero
//    +0x70 .. +0x80  the five -1 (0xFFFFFFFF) DWORDs
//    +0x84  the DWORD zero
//    +0x88  the 0 flag byte (the flag the 0x72F8E0 getter reads)
//    +0x8C  the DWORD zero
//    +0x90  the 0 flag byte (the byte the 0x72F7E0 flush writes 1 to)
//    +0x98 / +0xA0 / +0xB0  the 8-byte zeroed qword triple
//    +0xA8  the 1 flag byte
//    +0xB8  the DWORD zero
//    +0xBC / +0xBD  the two 0 flag bytes
//    +0xC0 / +0x144 / +0x1C8  the three 0x84-byte Win32-event wrappers
//    +0x24C  the DWORD zero
//    +0x250  the -1.0f float
//    +0x254  the 0 flag byte
//    +0x258  the DWORD zero
//    +0x25C  a self-referencing 4-byte heap block (LHAlloc(4)): the block
//           contains the address of +0x25C itself, and +0x25C holds the block
//           pointer
//    +0x268 / +0x26C / +0x270  the three DWORD zeros
//    +0x274 / +0x294 / +0x2B4  the three 32-byte option maps (Int / Float /
//           Detail)
//    +0x2D4  the 52-byte video-options record (1280x720 defaults)
//  Returns obj.
static void *CGameRendererInnerCtor(void *obj)
{
    char *b = (char *)obj;
    memset(b, 0, 776u);
    InitializeCriticalSection((CRITICAL_SECTION *)b + 0x00);
    InitializeCriticalSection((CRITICAL_SECTION *)b + 0x18);
    InitializeCriticalSection((CRITICAL_SECTION *)b + 0x30);
    *(DWORD *)(b + 0x58) = 0;
    *(DWORD *)(b + 0x70) = 0xFFFFFFFFu;
    *(DWORD *)(b + 0x74) = 0xFFFFFFFFu;
    *(DWORD *)(b + 0x78) = 0xFFFFFFFFu;
    *(DWORD *)(b + 0x7C) = 0xFFFFFFFFu;
    *(DWORD *)(b + 0x80) = 0xFFFFFFFFu;
    *(DWORD *)(b + 0x84) = 0;
    *(BYTE *)(b + 0x88) = 0;
    *(DWORD *)(b + 0x8C) = 0;
    *(BYTE *)(b + 0x90) = 0;
    *(BYTE *)(b + 0xA8) = 1;
    *(DWORD *)(b + 0xB8) = 0;
    *(BYTE *)(b + 0xBC) = 0;
    *(BYTE *)(b + 0xBD) = 0;
    EventObjCtor(b + 0xC0, 0, 0);            // 0x18C46E0
    EventObjCtor(b + 0x144, 0, 0);           // 0x18C46E0
    EventObjCtor(b + 0x1C8, 0, 0);           // 0x18C46E0
    *(DWORD *)(b + 0x24C) = 0;
    *(float *)(b + 0x250) = -1.0f;
    *(BYTE *)(b + 0x254) = 0;
    *(DWORD *)(b + 0x258) = 0;
    void **wrapper = (void **)LHAlloc(4u);
    if (wrapper != 0)
        *wrapper = (void *)(b + 0x25C);
    *(void **)(b + 0x25C) = wrapper;
    *(DWORD *)(b + 0x268) = 0;
    *(DWORD *)(b + 0x26C) = 0;
    *(DWORD *)(b + 0x270) = 0;
    InitIntOptionMap((F3OptionMap *)(b + 0x274), 0, 0);      // sub_40E6D0
    InitFloatOptionMap((F3OptionMap *)(b + 0x294), 0, 0);   // sub_40E780
    InitDetailOptionMap((F3OptionMap *)(b + 0x2B4), 0, 0);  // sub_40E830
    InitVideoOptionsDefaults((F3VideoOptions *)(b + 0x2D4));   // sub_19E1380
    return obj;
}

//  CGameRendererCtor  (sub_72FB30, 0x72FB30 - 0x72FB85)  -- the CGameRenderer
//  ctor (the CGameEngine +0x9C object, RunFrontEnd's +0x9C slot).
//  [NOT byte-identical] placement-dependent (relative E8 calls to
//  CBaseClassNonCopyableCtor / LHAlloc / CGameRendererInnerCtor).
//
//  void *CGameRendererCtor(void *obj)
//
//  Constructs the CGameRenderer object RunFrontEnd allocates at CGameEngine
//  +0x9C (stored as the +0x9C slot after the old object's vtable-slot-0
//  release): run the base ctor, overwrite the vtable with the placeholder
//  one, clear the +4 impl slot, allocate the 776-byte inner state via
//  LHAlloc(776) and construct it (CGameRendererInnerCtor), then store the
//  new inner pointer at +4 and return obj. The original reads the +4 slot
//  after allocating and, when it was non-zero, would delete the old inner
//  state through the inner destructor (sub_7312A0) + LHFree -- [obj+4] is
//  always 0 at that point (the slot was just assigned 0 and is not reassigned
//  before the check), so that branch is dead for every input and the inner
//  destructor is not invoked in this build.
static void *CGameRendererCtor(void *obj)
{
    F3BaseClassNonCopyableCtor(obj);
    *(void **)obj = g_CGameRendererVtable;
    *(void **)((char *)obj + 4) = 0;
    void *inner = LHAlloc(776u);
    void *v3 = 0;
    if (inner != 0)
        v3 = CGameRendererInnerCtor(inner);
    // Dead branch: the original reads [obj+4] here and, when non-zero,
    // deletes the old inner state (sub_7312A0 + LHFree). [obj+4] is always 0
    // here, so this branch is never taken.
    if (*(void **)((char *)obj + 4) != 0)
    {
        /* sub_7312A0(old inner state) + LHFree -- never taken */
    }
    *(void **)((char *)obj + 4) = v3;
    return obj;
}
//  sub_731490  (0x731490 - 0x731634)  -- the CGameRenderer impl worker that the
//  tail-call thunk below forwards to.
//  [STUB] not reimplemented in this build -- the 420-byte body (the +0x24C
//  renderer-object slot, the +0x84 engine-service pointer via sub_61A220, the
//  52-byte saved-options record copy into this+0xB5, the option-map finalizer
//  (sub_487050), the thumbnail / asset-path setup via SetSaveGamePath for the
//  Shaders / TextureThumbnails / AlphaTextureThumbnails / SourceAssets slots,
//  the CopySharedSlot / LHAlloc(4) particle-callback alloc and the
//  sub_731200 finalizer) is not reimplemented here.
static int sub_731490(void *obj, int a2, void *a3, int a4, const void *a5)
{
    (void)obj; (void)a2; (void)a3; (void)a4; (void)a5;
    return 0;
}
//  CGameRendererApplySavedOptions  (sub_72FB10, 0x72FB10 - 0x72FB2B)  -- the
//  CGameRenderer tail-call thunk: push the renderer's +4 impl pointer as the
//  impl's `this`, pass the outer renderer / engine / video-options records
//  along as the impl's arguments, and tail-call the impl worker (sub_731490).
//  [NOT byte-identical] placement-dependent (the single relative E8 call into
//  sub_731490).
//
//  int CGameRendererApplySavedOptions(void *obj, void *argList, void *a, void *b)
static int CGameRendererApplySavedOptions(void *obj, void *argList, void *a, void *b)
{
    void *inner = *(void **)((char *)obj + 4);      // mov ecx, [ecx+4]
    return sub_731490(inner, (int)(DWORD)obj, argList, (int)(DWORD)a, b);
}
//  sub_7306A0  (0x7306A0 - 0x730706)  -- the worker-thread starter behind the
//  thunk: under the renderer inner state's critical section, when the +0x88
//  (136) "thread already started" flag is still 0, spawn the "3D Engine"
//  thread (CreateThread: StartAddress, 0x20000 stack, lpParameter = the
//  critical section), set the flag, and run the thread-registration
//  notification (sub_19E2100).
//  [STUB] not reimplemented in this build: the faithful body needs its two
//  callees -- StartAddress (0x7303B0, the 294-byte engine worker loop whose
//  own callees (sub_1A22440 / sub_1A22420 / sub_72FDE0 / sub_18F1990 /
//  sub_18F0170 / SetThreadName / WaitForEvent / GetManager / ...) are
//  unimplemented -- and sub_19E2100 (0x19E2100, the thread-registration
//  notification pair: dword_1E2E7CC store + sub_19D0DC0, both unimplemented).
//  Spawning the real thread in this build would run the full engine worker
//  loop against a dead consumer, so the flag-check + CreateThread step stays
//  a documented no-op here (the flag is also never set, so the renderer's
//  +0x88 flag byte -- which sub_72F8E0 reads -- stays 0 as well).
static void sub_7306A0(void *cs) { (void)cs; }

//  StartWorkerThread  (0x72FAE0 - 0x72FAE8)  -- the
//  CGameRenderer / context-proxy tail-call thunk: load the object's +4
//  inner-state (critical-section-bearing) pointer and tail-call the
//  worker-thread starter (sub_7306A0) with it.  [NOT byte-identical]
//  placement-dependent (the E9 tail call into sub_7306A0).
//
//  void StartWorkerThread(void *obj)
static void StartWorkerThread(void *obj)
{
    void *inner = *(void **)((char *)obj + 4);      // mov ecx, [ecx+4]
    sub_7306A0(inner);                                // jmp sub_7306A0 (tail call)
}
// MountVideoFile (0x61BFE0, the video-slot mount over the +0x30 sub-object)
// is now implemented above (the MountPermanentBanks block).
//  ===============================================================
//  InitParticleSystem  (sub_61A5E0, 0x61A5E0 - 0x61A7B6)
//  [NOT byte-identical] placement-dependent (relative E8 calls to LHAlloc /
//  LHFree / InterlockedIncrement / InterlockedDecrement and the high-VA
//  factory / global slots).
//
//  This is the engine's particle-system front-end initializer:
//    1. builds the three 4-byte light-primitive vftable-holder blocks
//       (CFable2SpotLightPrimitiveFactory / CFable2PointLightPrimitiveFactory /
//       CFable2RenderPrimitiveFactory) and feeds them to the factory
//       registration chain (sub_1926130 -- a documented stub, see below);
//    2. constructs the 52-byte CFable2MeshVolumeQuery object at [obj+0x88]
//       together with a fresh 12-byte refcount block {1, ReleaseVtableSlot0,
//       meshQuery} and stashes the (value, block) pair into the persistent
//       engine-wide slot (dword_20FC2D0 / dword_20FC2D4), releasing the old
//       slot pair;
//    3. constructs the CHeightFieldCollider (4 bytes) at [obj+0x90] together
//       with a fresh 12-byte refcount block {1, ReleaseVtableSlot0, collider},
//       swapping the pair in via the sub_61FD20 pair-swap and stashing the
//       (value, block) pair into the persistent slot (dword_20FC294 /
//       dword_20FC298), releasing the old slot pair;
//    4. attempts the particle-thread spawn (sub_1926160: CreateThread over
//       sub_1921430) -- NOT performed in this build (the worker loop is dead),
//       so the thread globals stay 0 and the return value is 0.
//  The factory registration's globals (dword_1E2ED48/-4C/-3C/-40/-44/-2C/-30/-34/
//  -38/-B0/-EC0) live in the dead registration chain and are not reproduced.
// ===============================================================

//  the persistent particle-system slot globals (the original's high-VA slots)
static DWORD g_dword_20FC2D0 = 0;   // 0x20FC2D0  the mesh volume query pair -- the value
static DWORD g_dword_20FC2D4 = 0;   // 0x20FC2D4  the mesh volume query pair -- the block
static DWORD g_dword_20FC294 = 0;   // 0x20FC294  the heightfield pair -- the value
static DWORD g_dword_20FC298 = 0;   // 0x20FC298  the heightfield pair -- the block
static DWORD g_dword_1E2ED58 = 0;   // 0x1E2ED58  the a2 (worker-parameter) global
static DWORD g_dword_1E2ED54 = 0;   // 0x1E2ED54  the a3 (worker-parameter) global
static DWORD g_dword_2075EC4 = 0;   // 0x2075EC4  the particle-thread handle

//  the vftable placeholders (the release dispatcher never fires in this build
//  -- the pair stays stashed in the global -- but the slot-0 entry is a real
//  int-returning stub so the vftable is callable).
static int F3Slot0Release(DWORD obj, int freeIt) { (void)obj; (void)freeIt; return 0; }
static void *g_CFable2SpotLightPrimitiveFactoryVtable[1]   = { 0 };
static void *g_CFable2PointLightPrimitiveFactoryVtable[1]  = { 0 };
static void *g_CFable2RenderPrimitiveFactoryVtable[1]      = { 0 };
static void *g_CFable2MeshVolumeQueryVtable[1]             = { (void *)F3Slot0Release };
static void *g_NAbstractParticleSystemTypesIMeshVolumeQueryVtable[1] = { 0 };
static void *g_CHeightFieldColliderVtable[1]               = { (void *)F3Slot0Release };
static void *g_NAbstractParticleSystemTypesICollisionQueryVtable[1] = { 0 };

//  the 12-byte refcount block layout: {DWORD refcount; void *releaseFn; DWORD arg}
//  (the same idiom the engine uses for the +0x70 SEH block / the service pair).
static void *MkRefBlock(void *releaseFn, DWORD arg)
{
    void *p = LHAlloc(12u);
    if (p == 0)
        return 0;
    *(DWORD *)p = 1;
    *(void **)((char *)p + 4) = releaseFn;
    *(DWORD *)((char *)p + 8) = arg;
    return p;
}

//  ReleaseVtableSlot0  (sub_61E2A0 / sub_61DE40) -- the release dispatcher for
//  the 12-byte block: a no-op when the arg is null, else call the object's
//  vftable slot 0 with (this = arg, 1).  The original uses the identical body
//  for both blocks, so a single helper serves both.
static int ReleaseVtableSlot0(DWORD arg)
{
    if (arg == 0)
        return 0;
    void *vt = *(void **)(char *)(DWORD)arg;         // the vftable pointer stored at [arg]
    return ((int (*)(DWORD, int))((void **)vt)[0])(arg, 1);
}

//  GlobalPairRelease  (sub_50DD80 / sub_50DD30) -- release the (value, block)
//  pair stored in the pair at (valueSlot, blockSlot): decrement the block's
//  refcount, on zero dispatch the block's release callback with its arg and
//  free the block, and clear both slots.
static long GlobalPairRelease(DWORD *valueSlot, DWORD *blockSlot)
{
    DWORD block = *blockSlot;
    long result = (long)block;
    if (result != 0)
    {
        result = InterlockedDecrement((volatile LONG *)(void *)result);
        if (result == 0)
        {
            ((void (*)(DWORD))*(void **)((char *)block + 4))(*(DWORD *)((char *)block + 8));
            LHFree((void *)block);
        }
    }
    *valueSlot = 0;
    *blockSlot = 0;
    return result;
}

//  GlobalPairStash  (sub_1925160 / sub_1925120) -- stash the (value, block)
//  pair into the persistent global slot: release the old pair (clearing the
//  global slots) when the block differs, store the new value + block and
//  increment the new block's refcount.  The discarded return value is 0.
static long GlobalPairStash(DWORD *valueGlobal, DWORD *blockGlobal, DWORD value, DWORD block)
{
    if (*blockGlobal != block)
    {
        GlobalPairRelease(valueGlobal, blockGlobal);
        *valueGlobal = value;
        *blockGlobal = block;
        if (block != 0)
            return InterlockedIncrement((volatile LONG *)(void *)block);
    }
    return 0;
}

//  MeshVolumeQueryPairSet  (sub_620210) -- the engine-slot pair setter for the
//  mesh volume query: release the old pair's refcount block (callback + free
//  at zero), store the new value, build the fresh 12-byte block {1,
//  ReleaseVtableSlot0, value} (or null when the value is null) and store it.
//  Returns the fresh block (or 0).
static void *MeshVolumeQueryPairSet(void *slot, void *meshQuery)
{
    void *oldBlock = *(void **)((char *)slot + 4);
    if (oldBlock != 0 && InterlockedDecrement((volatile LONG *)oldBlock) == 0)
    {
        ((void (*)(DWORD))*(void **)((char *)oldBlock + 4))(*(DWORD *)((char *)oldBlock + 8));
        LHFree(oldBlock);
    }
    *(void **)slot = meshQuery;
    *(void **)((char *)slot + 4) = 0;
    void *newBlock = 0;
    if (meshQuery != 0)
    {
        newBlock = MkRefBlock((void *)ReleaseVtableSlot0, (DWORD)meshQuery);
        if (newBlock != 0)
        {
            *(void **)((char *)slot + 4) = newBlock;
        }
        else
        {
            *(void **)((char *)slot + 4) = 0;
            return 0;
        }
    }
    return newBlock;
}

//  HeightfieldPairSwap  (sub_61FD20) -- the heightfield slot pair swap: when
//  the slot's old block differs from the new block, release the old block
//  (callback + free at zero), store the new pair, and increment the new
//  block's refcount.
static void HeightfieldPairSwap(void *slot, void *value, void *block)
{
    void *oldBlock = *(void **)((char *)slot + 4);
    if (oldBlock != block)
    {
        if (oldBlock != 0 && InterlockedDecrement((volatile LONG *)oldBlock) == 0)
        {
            ((void (*)(DWORD))*(void **)((char *)oldBlock + 4))(*(DWORD *)((char *)oldBlock + 8));
            LHFree(oldBlock);
        }
        *(void **)slot = value;
        *(void **)((char *)slot + 4) = block;
        if (block != 0)
            InterlockedIncrement((volatile LONG *)block);
    }
}

//  IMeshVolumeQueryStore  (sub_19FCF40) -- the IMeshVolumeQuery vftable store
//  (overwritten by the CFable2 vftable in the same function).
static void *IMeshVolumeQueryStore(void *obj)
{
    *(void **)obj = g_NAbstractParticleSystemTypesIMeshVolumeQueryVtable;
    return obj;
}

//  ICollisionQueryStore  (sub_19FCF60) -- the ICollisionQuery vftable store
//  (overwritten by the CHeightFieldCollider vftable in the same function).
static void *ICollisionQueryStore(void *obj)
{
    *(void **)obj = g_NAbstractParticleSystemTypesICollisionQueryVtable;
    return obj;
}

//  Zero3Words  (unknown_libname_2582, 0x18A5F10) -- zero *this, *(this+1),
//  *(this+2).
static void *Zero3Words(void *obj)
{
    *(DWORD *)obj = 0;
    *(DWORD *)((char *)obj + 4) = 0;
    *(DWORD *)((char *)obj + 8) = 0;
    return obj;
}

//  MeshVolumeQueryInner  (sub_710650) -- the 28-byte self-linked state block
//  build (the mesh volume query's inner structure): a 4-byte wrapper
//  {this+8}, then a 28-byte block {[0] = [4] = [8] = block, byte 0x18 = 1,
//  byte 0x19 = 1} with the zero stores guarded exactly as the original.
static void *MeshVolumeQueryInner(void *obj)
{
    void *wrapper = LHAlloc(4u);
    if (wrapper != 0)
        *(void **)wrapper = obj;
    else
        wrapper = 0;
    *(void **)obj = wrapper;

    void *v5 = (void *)LHAlloc(28u);
    if (v5 != 0)
        *(DWORD *)v5 = 0;
    if ((int)v5 != -4)
        *(DWORD *)((char *)v5 + 4) = 0;
    if ((int)v5 != -8)
        *(DWORD *)((char *)v5 + 8) = 0;
    *(BYTE *)((char *)v5 + 25) = 0;
    *(BYTE *)((char *)v5 + 24) = 1;
    *(void **)((char *)obj + 0x18) = v5;
    *(BYTE *)((char *)v5 + 25) = 1;
    *(DWORD *)((char *)v5 + 4) = (DWORD)v5;
    *(DWORD *)v5 = (DWORD)v5;
    *(DWORD *)((char *)v5 + 8) = (DWORD)v5;
    *(DWORD *)((char *)obj + 0x1C) = 0;
    return obj;
}

//  MeshVolumeQueryCtor  (sub_70F430) -- the CFable2MeshVolumeQuery ctor (52
//  bytes): the IMeshVolumeQuery vftable store (overwritten by the CFable2
//  vftable), +4 = 1, the inner block build (MeshVolumeQueryInner) at [obj+8],
//  then +0x28/+0x2C/+0x30 zeroed.
static void *MeshVolumeQueryCtor(void *obj)
{
    IMeshVolumeQueryStore(obj);                                  // sub_19FCF40
    *(void **)obj = g_CFable2MeshVolumeQueryVtable;              // the overwrite
    *(DWORD *)((char *)obj + 4) = 1;
    MeshVolumeQueryInner((char *)obj + 8);                       // sub_710650
    Zero3Words((char *)obj + 0x28);                              // unknown_libname_2582
    return obj;
}

//  sub_1926130 -- the particle-system factory registration chain
//  [STUB] not reimplemented in this build: the callee chain sub_19224D0 ->
//  sub_5CEA80 / sub_5CEBC0 / sub_5CED00 / sub_1912BB0 / sub_1924FC0 (the light
//  primitive factory registration + the particle-system globals) is not
//  reimplemented here.  The three vftable-holder blocks are still allocated
//  and populated exactly as the original does.
static int sub_1926130(void *renderFactory, void *pointFactory, void *spotFactory, int callback, float radius, int a2)
{
    (void)renderFactory; (void)pointFactory; (void)spotFactory;
    (void)callback; (void)radius; (void)a2;
    return 0;
}

//  sub_1926160 -- the particle-thread spawn  (0x1926160 - 0x192618C)
//  [NOT byte-identical].  The original stores the two worker-parameter globals
//  (dword_1E2ED58 = a2, dword_1E2ED54 = a3), creates the "Particle Thread"
//  (CreateThread: sub_1921430, 0x10000 stack, lpParameter = a1) and stores the
//  handle in dword_2075EC4; the return value is the handle (the caller
//  discards it).  NOT performed in this build: the worker loop
//  (sub_1921430 -- the particle-thread spin over dword_20FC36x / byte_1D9E1ED)
//  is a documented stub, so no thread is created and the globals stay 0; the
//  return value is 0.
static int sub_1926160(int lpParameter, int a2, int a3)
{
    (void)lpParameter; (void)a2; (void)a3;
    g_dword_1E2ED58 = 0;
    g_dword_1E2ED54 = 0;
    g_dword_2075EC4 = 0;
    return 0;
}

//  InitParticleSystem  (sub_61A5E0, 0x61A5E0 - 0x61A7B6)  -- the engine's
//  particle-system front-end initializer.
static int InitParticleSystem(void *obj)
{
    // 1. the three 4-byte light-primitive vftable-holder blocks
    DWORD *spotBlock = (DWORD *)LHAlloc(4u);
    if (spotBlock != 0)
        *spotBlock = (DWORD)g_CFable2SpotLightPrimitiveFactoryVtable;
    DWORD *pointBlock = (DWORD *)LHAlloc(4u);
    if (pointBlock != 0)
        *pointBlock = (DWORD)g_CFable2PointLightPrimitiveFactoryVtable;
    DWORD *renderBlock = (DWORD *)LHAlloc(4u);
    if (renderBlock != 0)
        *renderBlock = (DWORD)g_CFable2RenderPrimitiveFactoryVtable;

    // 2. the factory registration chain (documented stub -- the allocations
    //    are real, exactly as the original)
    sub_1926130(renderBlock, pointBlock, spotBlock, 0, 15.0f, 2);

    // 3. the mesh volume query + its persistent slot
    DWORD *meshRaw = (DWORD *)LHAlloc(52u);
    void *meshQuery = 0;
    if (meshRaw != 0)
        meshQuery = MeshVolumeQueryCtor(meshRaw);
    MeshVolumeQueryPairSet((char *)obj + 0x88, meshQuery);

    DWORD meshValue = *(DWORD *)((char *)obj + 0x88);
    DWORD meshBlock = *(DWORD *)((char *)obj + 0x8C);
    if (meshBlock != 0)
        InterlockedIncrement((volatile LONG *)(void *)meshBlock);
    GlobalPairStash(&g_dword_20FC2D0, &g_dword_20FC2D4, meshValue, meshBlock);
    if (meshBlock != 0 && InterlockedDecrement((volatile LONG *)(void *)meshBlock) == 0)
    {
        ((void (*)(DWORD))*(void **)((char *)meshBlock + 4))(*(DWORD *)((char *)meshBlock + 8));
        LHFree((void *)meshBlock);
    }

    // 4. the heightfield collider + its persistent slot
    DWORD *collider = (DWORD *)LHAlloc(4u);
    if (collider != 0)
    {
        ICollisionQueryStore(collider);                            // sub_19FCF60
        *collider = (DWORD)g_CHeightFieldColliderVtable;           // the overwrite
    }
    if (collider != 0)
    {
        void *heightBlock = MkRefBlock((void *)ReleaseVtableSlot0, (DWORD)collider);
        HeightfieldPairSwap((char *)obj + 0x90, collider, heightBlock);
    }

    DWORD heightValue = *(DWORD *)((char *)obj + 0x90);
    DWORD heightBlockPtr = *(DWORD *)((char *)obj + 0x94);
    if (heightBlockPtr != 0)
        InterlockedIncrement((volatile LONG *)(void *)heightBlockPtr);
    GlobalPairStash(&g_dword_20FC294, &g_dword_20FC298, heightValue, heightBlockPtr);
    if (heightBlockPtr != 0 && InterlockedDecrement((volatile LONG *)(void *)heightBlockPtr) == 0)
    {
        ((void (*)(DWORD))*(void **)((char *)heightBlockPtr + 4))(*(DWORD *)((char *)heightBlockPtr + 8));
        LHFree((void *)heightBlockPtr);
    }

    // 5. the particle-thread spawn (documented no-op)
    return sub_1926160(0, 0, 0);
}
//  CFullScreenMoviePlayerCtor  (sub_731B80, 0x731B80 - 0x731B99)  -- the
//  NUI::CFullScreenMoviePlayer ctor (8-byte object).
//  [NOT byte-identical] placement-dependent (relative E8 call to the
//  CBaseClassNonCopyableCtor and the high-VA vftable
//  (??_7CFullScreenMoviePlayer@NUI@@6B@, 0x01AF5178)).
//
//  void *CFullScreenMoviePlayerCtor(void *obj)
//
//  Construct the 8-byte NUI::CFullScreenMoviePlayer object (engine member
//  0xB4, built over LHAlloc(8) in RunFrontEnd): run the
//  CBaseClassNonCopyableCtor (base vtable, overwritten below), store the
//  (placeholder) vtable (slot 0 = F3StubRelease, see the placeholder-vtable
//  block above), zero +4, and return obj. The original is thiscall (obj in
//  ECX, plain retn); declared here as a plain free function per the project
//  convention.
static void *CFullScreenMoviePlayerCtor(void *obj)
{
    F3BaseClassNonCopyableCtor(obj);
    *(void **)obj = g_CFullScreenMoviePlayerVtable;
    *(void **)((char *)obj + 4) = 0;
    return obj;
}
//  0x1DC269C -- the live 96-byte game-impl pointer (dword_1DC269C).  Written by
//  GameImplCtor (sub_712120); cleared by GameImplTeardown (sub_711DD0).
static int *g_dword_1DC269C = 0;   // 0x1DC269C: the live 96-byte game-impl

//  JmpLHFree2  (0x712AE0 - 0x712AE5)  -- the 5-byte tail-jump thunk `jmp LHFree`
//  that the 20-byte float block's SEH handler dispatches to.  [NOT byte-identical]
//  plain C++ equivalent of the tail-jump (frees its arg).
static void JmpLHFree2(int arg) { if (arg != 0) LHFree((void *)arg); }

//  ReleaseListNode28 / ReleaseListNode32  (sub_715810 / sub_714C40) -- the
//  recursive in-node finalizers shared by ResetSubList28 / ResetSubList32.
//  Their fast path dispatches the flag-set check (the block's flag byte at
//  +0x19 / +0x1D is 1, so the original returns immediately).  Only flag-set
//  blocks are ever constructed here, so the recursive release path (flag
//  clear) is never hit and the fast path is implemented as the faithful no-op.
static void ReleaseListNode28(void *node) { (void)node; }
static void ReleaseListNode32(void *node) { (void)node; }

//  F3SubListCtor28 / F3SubListCtor32  (sub_7158A0 / sub_715920) -- the 4-byte
//  self-referencing "sub-list" ctor.  Builds:
//   - a 4-byte v4 block holding the object pointer (*v4 = obj), stored at +0
//   - a blockSize-byte v5 block (28 bytes for the impl+0x10 sub-list, 32 for
//     the impl+0x30 sub-list): word 0 / word 1 / word 2 all = v5 (self-refs);
//     flag byte at +blockSize-4 (+0x18 / +0x1C) = 1 and flag byte at
//     +blockSize-3 (+0x19 / +0x1D) = 1; stored at word 6 (+0x18)
//   - word 7 (+0x1C) = 0 (the flag-clear word)
//  The original's word-0 guard ("if (v5 != 0)") is the real guard; the "!= -4"
//  / "!= -8" decompiler guards are artifacts of the lea/test/jz pattern and are
//  always true for a successful LHAlloc.
static void *F3SubListCtor28(void *obj)
{
    int *this_ = (int *)obj;
    int *v4 = (int *)LHAlloc(4);
    if (v4 != 0)
        *v4 = (int)obj;
    this_[0] = (v4 != 0) ? (int)v4 : 0;
    char *v5 = (char *)LHAlloc(28);
    if (v5 != 0)
    {
        *(DWORD *)(v5 + 0) = 0;
        *(DWORD *)(v5 + 4) = 0;
        *(DWORD *)(v5 + 8) = 0;
        v5[25] = 0;
        v5[24] = 1;
        this_[6] = (int)v5;
        v5[25] = 1;
        *(DWORD *)(v5 + 4) = (DWORD)v5;
        *(DWORD *)(v5 + 0) = (DWORD)v5;
        *(DWORD *)(v5 + 8) = (DWORD)v5;
    }
    else
        this_[6] = 0;
    this_[7] = 0;
    return obj;
}
static void *F3SubListCtor32(void *obj)
{
    int *this_ = (int *)obj;
    int *v4 = (int *)LHAlloc(4);
    if (v4 != 0)
        *v4 = (int)obj;
    this_[0] = (v4 != 0) ? (int)v4 : 0;
    char *v5 = (char *)LHAlloc(32);
    if (v5 != 0)
    {
        *(DWORD *)(v5 + 0) = 0;
        *(DWORD *)(v5 + 4) = 0;
        *(DWORD *)(v5 + 8) = 0;
        v5[29] = 0;
        v5[28] = 1;
        this_[6] = (int)v5;
        v5[29] = 1;
        *(DWORD *)(v5 + 4) = (DWORD)v5;
        *(DWORD *)(v5 + 0) = (DWORD)v5;
        *(DWORD *)(v5 + 8) = (DWORD)v5;
    }
    else
        this_[6] = 0;
    this_[7] = 0;
    return obj;
}

//  ResetSubList28 / ResetSubList32  (sub_716670 / sub_716760) -- the sub-list
//  reset called from GameImplTeardown.  On a constructed block the flag byte
//  is always set, so the original's recursive release finalizer (ReleaseListNode*)
//  dispatches the no-op fast path; the remaining writes are redundant self-
//  references plus the flag-clear word.  The 8-byte out-buffer the original
//  builds is discarded by the caller, so this is faithfully: clear word 7.
static void ResetSubList28(int *this_)
{
    void *v5 = (void *)this_[6];
    if (v5 != 0)
        *(DWORD *)((char *)v5 + 4) = (DWORD)v5;
    this_[7] = 0;
}
static void ResetSubList32(int *this_)
{
    void *v5 = (void *)this_[6];
    if (v5 != 0)
        *(DWORD *)((char *)v5 + 4) = (DWORD)v5;
    this_[7] = 0;
}

//  GameImplTeardown  (sub_711DD0, 0x711DD0 - 0x711EAF)  -- release the impl's
//  +0x5C / +0x54 SEH blocks, zero the +0x58/+0x5C / +0x50/+0x54 slots, then
//  the two sub-list slots (+0x48/+0x4C and +0x28/+0x2C) and free the 4-byte
//  / 28-byte / 32-byte blocks.  [NOT byte-identical] placement-dependent (the
//  relative E8 calls into LHFree / InterlockedDecrement).
static void GameImplTeardown(void *obj)
{
    int *this_ = (int *)obj;

    g_dword_1DC269C = 0;

    if (this_[23] != 0 && InterlockedDecrement((volatile LONG *)this_[23]) == 0)
    {
        void **b = (void **)this_[23];
        ((void (*)(void *))b[1])(b[2]);
        LHFree(b);
    }
    this_[22] = 0;   // +0x58
    this_[23] = 0;   // +0x5C

    if (this_[21] != 0 && InterlockedDecrement((volatile LONG *)this_[21]) == 0)
    {
        void **b = (void **)this_[21];
        ((void (*)(void *))b[1])(b[2]);
        LHFree(b);
    }
    this_[20] = 0;   // +0x50
    this_[21] = 0;   // +0x54

    ResetSubList32(this_ + 12);   // +0x30's sub-list
    LHFree((void *)this_[18]);    // +0x48: the 32-byte block
    LHFree((void *)this_[12]);    // +0x30: the 4-byte block
    this_[18] = 0;   // +0x48
    this_[19] = 0;   // +0x4C

    ResetSubList28(this_ + 4);    // +0x10's sub-list
    LHFree((void *)this_[10]);    // +0x28: the 28-byte block
    LHFree((void *)this_[4]);     // +0x10: the 4-byte block
    this_[10] = 0;   // +0x28
    this_[11] = 0;   // +0x2C
}

//  ReleaseGameImpl  (sub_7172B0, 0x7172B0 - 0x7172CB)  -- the SEH handler for
//  the wrapper's 12-byte block: tear down the 96-byte impl and free it.  [NOT
//  byte-identical] placement-dependent (the relative E8 calls into
//  GameImplTeardown / LHFree).
static int ReleaseGameImpl(int arg)
{
    if (arg != 0)
    {
        GameImplTeardown((void *)arg);
        LHFree((void *)arg);
    }
    return 0;
}

//  RegisterFloatBlockSeh  (sub_7136D0, 0x7136D0 - 0x71374A)  -- the SEH
//  installer for the 20-byte float block at impl+0x50: release the old SEH
//  block at this+4, store the value, and build a 12-byte SEH block
//  {refcount=1, handler=JmpLHFree2, arg=value}.  [NOT byte-identical]
//  placement-dependent (the relative E8 calls into LHAlloc / LHFree /
//  InterlockedDecrement / JmpLHFree2).
static void *RegisterFloatBlockSeh(void *obj, void *value)
{
    int *this_ = (int *)obj;
    int oldBlock = this_[1];
    if (oldBlock != 0 && InterlockedDecrement((volatile LONG *)oldBlock) == 0)
    {
        void **b = (void **)oldBlock;
        ((void (*)(void *))b[1])(b[2]);
        LHFree((void *)oldBlock);
    }
    this_[1] = 0;
    this_[0] = (int)value;
    if (value != 0)
    {
        int *block = (int *)LHAlloc(12);
        if (block == 0)
        {
            this_[1] = 0;
            return 0;
        }
        block[0] = 1;                 // refcount
        block[1] = (int)JmpLHFree2;   // handler: free the float block on release
        block[2] = (int)value;
        this_[1] = (int)block;
        return block;
    }
    return value;   // a2 == 0: the original returns a2 (= 0)
}

//  RegisterWrapperSeh  (sub_7172D0, 0x7172D0 - 0x717343)  -- the 8-byte
//  {value, SEH block} wrapper installer: release the old SEH block at obj+4,
//  store the value, and build a 12-byte SEH block {refcount=1,
//  handler=ReleaseGameImpl, arg=value}.  [NOT byte-identical] placement-
//  dependent (the relative E8 calls into LHAlloc / LHFree / InterlockedDecrement
//  / ReleaseGameImpl).
static void *RegisterWrapperSeh(void *obj, void *value)
{
    int *this_ = (int *)obj;
    int oldBlock = this_[1];
    if (oldBlock != 0 && InterlockedDecrement((volatile LONG *)oldBlock) == 0)
    {
        void **b = (void **)oldBlock;
        ((void (*)(void *))b[1])(b[2]);
        LHFree((void *)oldBlock);
    }
    this_[1] = 0;
    this_[0] = (int)value;
    if (value != 0)
    {
        int *block = (int *)LHAlloc(12);
        if (block == 0)
        {
            this_[1] = 0;
            return 0;
        }
        block[0] = 1;                     // refcount
        block[1] = (int)ReleaseGameImpl;  // handler
        block[2] = (int)value;
        this_[1] = (int)block;
        return block;
    }
    return value;   // a2 == 0: the original returns a2 (= 0)
}

//  GameImplCtor  (sub_712120, 0x712120 - 0x7121DA)  -- the 96-byte game-impl
//  ctor: the 1-word / 4-byte flag pattern, the two 4-byte + 28/32-byte
//  self-referencing sub-lists (impl+0x10 and impl+0x30), the 20-byte float
//  block ({1.5707964f, 1.5707964f, 0.5f, 0.5f, 0.5f}, dword_1BD15FC = 3.1415927f
//  and dword_1AEA1E0 = 0.5f) and its SEH block.  [NOT byte-identical]
//  placement-dependent (relative E8 calls to F3SubListCtor28/32 /
//  RegisterFloatBlockSeh and the high-VA global dword_1DC269C).
static void *GameImplCtor(void *obj)
{
    int *this_ = (int *)obj;
    char *b = (char *)obj;

    this_[0] = 1;
    b[9] = 1; b[10] = 1; b[11] = 1; b[12] = 1;   // bytes +9..+12 = 1
    b[4] = 0; b[5] = 0; b[6] = 0; b[7] = 0; b[8] = 0;  // bytes +4..+8 = 0

    F3SubListCtor28((void *)(this_ + 4));   // +0x10
    F3SubListCtor32((void *)(this_ + 12));  // +0x30

    this_[20] = 0;                            // +0x50
    this_[21] = 0;                            // +0x54
    this_[22] = 0;                            // +0x58
    this_[23] = 0;                            // +0x5C
    g_dword_1DC269C = this_;

    float *block = (float *)LHAlloc(20);
    if (block != 0)
    {
        block[0] = 3.1415927f * 0.5f;     // dword_1BD15FC * dword_1AEA1E0
        block[1] = 3.1415927f * 0.5f;
        block[2] = 0.5f;                    // dword_1AEA1E0
        block[3] = 0.5f;
        block[4] = 0.5f;
    }
    RegisterFloatBlockSeh((void *)(this_ + 20), block);
    return obj;
}

//  GameImplWrapperCtor  (sub_712430, 0x712430 - 0x71246F)  -- the 8-byte
//  {value, SEH block} wrapper ctor: zero both words, allocate the 96-byte game
//  impl and register it via RegisterWrapperSeh (the SEH slot stored at +4).
//  [NOT byte-identical] placement-dependent (the relative E8 calls into
//  GameImplCtor / RegisterWrapperSeh / LHAlloc).
static void *GameImplWrapperCtor(void *obj)
{
    void **this_ = (void **)obj;
    this_[0] = 0;
    this_[1] = 0;
    int *impl = (int *)LHAlloc(96);
    if (impl != 0)
        GameImplCtor((void *)impl);
    RegisterWrapperSeh(obj, impl);
    return obj;
}
static void  nullsub_29(void) {}

// The 36-byte F3WString wide-path struct now lives in gamemain.h (it is
// shared with the CLuaManager serialise-script path in engine/luavm.cpp).

static wchar_t *F3WStringData(F3WString *s)
{
    return (s->cap < 8) ? s->u.buf : (wchar_t *)s->u.ptr;
}

static void F3WStringReset(F3WString *s)
{
    s->cap = 7;
    s->len = 0;
    s->u.buf[0] = 0;
    s->e1 = s->e2 = 0;
}

// the "if (cap >= 8) LHFree()" idiom -- drop the SSO string's heap buffer
static void F3WStringFree(F3WString *s)
{
    if (s->cap >= 8)
        LHFree((void *)s->u.ptr);
}

// free + reset (the balanced reuse idiom)
static void F3WStringClear(F3WString *s)
{
    F3WStringFree(s);
    F3WStringReset(s);
}

// sub_435C10 -- assign (src, len) into the string. The original's callers
// free the previous heap buffer and reset len / cap before calling.
static void F3WStringAssign(F3WString *s, const wchar_t *src, unsigned int len)
{
    if (len > 7)
    {
        s->u.ptr = (DWORD)LHAlloc(2 * (len + 1));
        s->cap = len + 1;
    }
    else
        s->cap = 7;
    wchar_t *dst = F3WStringData(s);
    for (unsigned int i = 0; i < len; i++)
        dst[i] = src[i];
    dst[len] = 0;
    s->len = len;
    s->e1 = s->e2 = 0;
}

// ANSI -> wide, then assign (F3WStringReset + F3WStringAssign; the building
// block of F3WStringFromAnsi (sub_4358A0) minus the separator pass).
static void F3WStringAssignAnsi(F3WString *s, const char *src)
{
    wchar_t dst[0x801];
    int n = (int)mbstowcs(dst, src, 0x801);
    if (n < 0)
        n = 0;
    F3WStringAssign(s, dst, (unsigned int)n);
}

// sub_436BA0 -- path normalization: the original scans case-insensitively
// (towlower) for '/' and replaces each with '\\', then appends a trailing
// '\\' when the path does not already end in one.
static void F3WStringNormalize(F3WString *s)
{
    wchar_t *p = F3WStringData(s);
    for (unsigned int i = 0; i < s->len; i++)
        if (p[i] == '/')
            p[i] = L'\\';
    if (s->len != 0 && p[s->len - 1] != L'\\')
    {
        p[s->len] = L'\\';
        s->len++;
    }
}

// sub_4357C0 -- the separator pass: the original scans case-insensitively
// (towlower) for '/' and replaces each in place with '\\'.  Unlike
// F3WStringNormalize (sub_436BA0), it does not append a trailing separator.
static void F3WStringNormalizeSeparators(F3WString *s)
{
    wchar_t *p = F3WStringData(s);
    for (unsigned int i = 0; i < s->len; i++)
        if (p[i] == '/')
            p[i] = L'\\';
}

//  F3WStringFromAnsi  (0x4358A0 - 0x43593D)  -- build the 36-byte front-end
//  wide path string from an ANSI string: reset the SSO layout (cap 7, len 0,
//  first wchar 0, the +28 / +32 words zeroed), mbstowcs_s the whole source
//  into a 0x801-wchar stack buffer, assign (src, wcslen) through sub_435C10
//  (F3WStringAssign), then the sub_4357C0 separator pass ('/' -> '\\').
//  The original is a thiscall + retn 4 hybrid (the string in ECX, the ANSI
//  buffer on the stack) that returns this; here it is a plain two-arg free
//  function per the project convention.
//  [NOT byte-identical] placement-dependent: E8 calls to the _mbstowcs_s /
//  __alloca_probe / __security_check_cookie CRT slots and to sub_435C10 /
//  sub_4357C0, plus the original 0x1010-byte stack frame and cookie.
//  Exposed (non-static) for the CLuaManager serialise-script path
//  (engine/luavm.cpp); declared in gamemain.h.
F3WString *F3WStringFromAnsi(F3WString *obj, const char *src)
{
    F3WStringAssignAnsi(obj, src);
    F3WStringNormalizeSeparators(obj);
    return obj;
}

//  F3FnvHash  (0x56DFA0 - 0x56DFC5)  -- the FNV 32-bit hash primitive:
//  h = seed, then per 16-bit unit (up to the NUL word) h = (h * 0x1000193) ^
//  (the unit's low byte).  cdecl (the wide string in a1, the seed in a2) --
//  the FNV-1 iteration order (multiply, then xor) with the standard 32-bit
//  offset basis / prime, over the low byte of each wide unit.
static DWORD F3FnvHash(const wchar_t *s, DWORD seed)
{
    while (*s != 0)
    {
        seed = seed * 16777619u ^ (DWORD)(unsigned char)*s;
        s++;
    }
    return seed;
}

//  GetWideStringHash  (0x56DFD0 - 0x56E023)  -- the cached FNV 32-bit hash of
//  the 36-byte F3WString's data, stored in the string's own +28 word (e1): when
//  e1 is 0 and len is non-zero, take the data (the SSO buffer at +4 when
//  cap < 8, else the heap pointer -- F3WStringData) and, when it is non-null
//  and non-empty (wcsnlen up to 0x800 wchars), cache + return
//  F3FnvHash(data, 0x811C9DC5); otherwise cache 0.  Always returns the
//  (now-cached) e1 word.  thiscall in the original (the string in ECX).
//  [NOT byte-identical] placement-dependent (the _wcsnlen IAT slot + the
//  relative E8 call to F3FnvHash).  Exposed (non-static) for the CLuaManager
//  serialise-script path (engine/luavm.cpp) and the CWin32FileSystem verbose
//  open log / failure report; declared in gamemain.h.
int GetWideStringHash(F3WString *s)
{
    if (s->e1 == 0 && s->len != 0)
    {
        const wchar_t *data = F3WStringData(s);
        if (data != 0 && wcsnlen(data, 0x800) != 0)
            s->e1 = F3FnvHash(data, 0x811C9DC5u);
        else
            s->e1 = 0;
    }
    return (int)s->e1;
}

// sub_718250 (0x718250 - 0x718686) -- register one asset file into the
// language / VFS resource table: a1 = the file's F3WString path, a2 = its
// package / category name ("speech", "speechdoc", "lipsync", "text", "fonts",
// "videos", "gui"). Its whole body (the "language\\" shared-slot lookup
// through sub_717E00, the sub_5B20E0 / sub_180D7B0 string joins, the per-entry
// sub_5B12A0 / sub_435B20 archive registration, and the trailing per-entry
// frees) depends on the unimplemented shared-slot / VFS / archive helpers, so
// it remains a no-op stub here. [remaining stub]
static long sub_718250(F3WString *a1, const char *a2)
{ (void)a1; (void)a2; return 0; }

//  RegisterStandardAssets  (0x718690 - 0x718BCC)  -- register the game's
// standard asset packages into the language / VFS resource table. Walks a
// fixed list of
// (path, package) pairs; for each it builds a temp F3WString (sub_4358A0 =
// F3WStringFromAnsi), registers it by package (sub_718250), then drops the
// string's heap buffer (the "if (cap >= 8) LHFree" idiom -- every path here is
// > 7 chars, so it is always heap-allocated and freed).
// [NOT byte-identical] the original is 26 hand-inlined
// (push-constant / call) blocks referencing high-VA .rdata string constants
// and E8 calls to F3WStringFromAnsi (sub_4358A0) / sub_718250 / LHFree.
int RegisterStandardAssets(void)
{
    static const struct F3Asset { const char *path; const char *package; }
        kAssets[] =
    {
        { "speech/speech.bnk",              "speech"    },
        { "speech/speech.bnk.dat",          "speech"    },
        { "speech/expressions.bnk",         "speech"    },
        { "speech/expressions.bnk.dat",     "speech"    },
        { "speechdoc/speech.adb",           "speechdoc" },
        { "speechdoc/speech_vo.adb",        "speechdoc" },
        { "lipsync/lipsync.bnk",            "lipsync"   },
        { "lipsync/lipsync.bnk.dat",        "lipsync"   },
        { "lipsync/expressions.bnk",        "lipsync"   },
        { "lipsync/expressions.bnk.dat",    "lipsync"   },
        { "speech/cutscenes.vfsconfig",     "speech"    },
        { "speech/cutscenes_vo.vfsconfig",  "speech"    },
        { "text/book.babel",                "text"      },
        { "patch_text/book.babel",          "text"      },
        { "fonts/gui_normal.bft",           "fonts"     },
        { "videos/childbirth.bik",          "videos"    },
        { "videos/fall_m.bik",              "videos"    },
        { "videos/fall_f.bik",              "videos"    },
        { "videos/herospire.bik",           "videos"    },
        { "videos/historyalbion.bik",       "videos"    },
        { "videos/marriage.bik",            "videos"    },
        { "videos/newbeginnings.bik",       "videos"    },
        { "gui/gui_textures.bnk",           "gui"       },
        { "gui/gui_textures.bnk.dat",       "gui"       },
        { "gui/gui_texture_headers.bnk",    "gui"       },
        { "gui/gui_texture_headers.bnk.dat","gui"       },
    };
    const unsigned n = (unsigned)(sizeof(kAssets) / sizeof(kAssets[0]));
    F3WString s;
    int rc = 0;
    for (unsigned i = 0; i < n; ++i)
    {
        F3WStringFromAnsi(&s, kAssets[i].path);         // sub_4358A0
        rc = (int)sub_718250(&s, kAssets[i].package);   // register by package
        F3WStringFree(&s);                              // if (cap >= 8) LHFree
    }
    return rc;
}

// ============================================================
//  InitFrontendData's framework callees.  The shared-slot / path / archive /
//  file-subsystem clusters are not implemented yet, so these mirror the
//  observable behavior (slot init / release, the Win32 probes) and stub the
//  deep archive work.
// ============================================================

// sub_18AC3A0: the exe's directory (drive + path via _splitpath) into a
// shared slot.
static F3SharedBuffer **SetExeDirSlot(F3SharedBuffer **slot)
{
    char file[260];
    char drive[12];
    char dir[264];
    char out[0x114];
    GetModuleFileNameA(0, file, 0x104);
    _splitpath_s(file, drive, 0x0A, dir, 0x104, 0, 0, 0, 0);
    _snprintf(out, sizeof out, "%s%s", drive, dir);
    return SetSharedSlot(slot, out, -1);
}

// sub_18B03A0: GetFileAttributesA test on the slot's (resolved) path:
// 1 = existing directory, 2 = not found, 6 = exists but not a directory.
static int ExeDirSlotIsDirectory(F3SharedBuffer *slot)
{
    if (slot == 0 || slot->data == 0)
        return 2;
    DWORD attr = GetFileAttributesA(slot->data);
    if (attr == 0xFFFFFFFF)
        return 2;
    if ((attr & 0x10) != 0)
        return 1;
    return 6;
}

// sub_18C4270: the global current-directory string slot (dword_212304C,
// lazy-init) with a reference. 0 until the framework is implemented.
static F3SharedBuffer **GetCurrentDirSlot(F3SharedBuffer **out)
{
    *out = 0;
    return out;
}

// sub_6199B0: the fatal-error handler -- a no-op returning 0.
static int FrontendFatalErrorNoop(void) { return 0; }

// sub_61A840: the log-error handler -- GetLastError + the "%s (%u) #%08x"
// log message. The log framework is a remaining stub.
static int FrontendLogError(void)
{
    (void)GetLastError();
    return 0;
}

// sub_18B15C0: install the front-end error handler for a level (0 = log,
// 1 = fatal) under the CWD object's critical section, into the level's
// global handler slot (sub_5A9FE0).
static int (*g_frontendErrorHandlers[2])(void) = { 0, 0 };
static void RegisterFrontendErrorHandler(void *cwdObj, int level, int (*handler)(void))
{
    (void)cwdObj;
    if (level >= 0 && level <= 1)
        g_frontendErrorHandlers[level] = handler;
}

// sub_180B520: reserve / grow the 128 KB data region (dword_1E2D868).
static int ReserveDataRegion(unsigned int size) { (void)size; return 0; }

// ============================================================
//  File-subsystem (LHSFS) globals -- the embedded global object at
//  0x1E2D7C8 and the word globals that sit around it.
// ============================================================
// 0x1E2D7C8 -- the embedded global file-subsystem object (0x8C bytes,
// image-initialized to 0; the init-flag word at 0x1E2D854 sits right after
// it).
static DWORD g_fileSubsys[0x23];
// 0x1E2D854 -- the file-subsystem init flag (bit 0 = constructed).
static DWORD g_dword_1E2D854 = 0;
// 0x1E2D864 -- the archive-manager handle slot: the file subsystem is
// constructed around it. 0 here while the archive framework is stubbed.
static void *g_dword_1E2D864 = 0;
// 0x1E2D870 / 0x1E2D890 -- the two global VFS archive-collection objects
// (each a 0x20-byte {4-byte wrapper, sentinel node at +0x18, count at +0x1C}
// record): the 44-byte red-black tree collection and its 24-byte vector
// sibling, both lazily initialized by GetVfsArchiveCollection (sub_180D9B0).
static DWORD g_unk_1E2D870[8];    // 0x1E2D870 (0x20 bytes)
static DWORD g_unk_1E2D890[8];    // 0x1E2D890 (0x20 bytes)
// 0x1E2D8B0 -- the archive handle held by the collection (AddRef'd; 0 while
// the archive framework is stubbed).
static void *g_dword_1E2D8B0 = 0;
// 0x1E2D8B4 -- the archive-collection init flag (bit 0 = constructed).
static DWORD g_dword_1E2D8B4 = 0;

// sub_180AC90 (0x180AC90 - 0x180ACAC): the archive manager slot (the
// dword_1E2D864 handle) with a reference: *out = the slot handle (AddRef'd
// through vtable slot 1 when non-null); returns the out slot.
static F3SharedBuffer **GetArchiveSlot(F3SharedBuffer **out)
{
    F3SharedBuffer *h = (F3SharedBuffer *)g_dword_1E2D864;
    *out = h;
    if (h != 0)
        F3CallVtable(h, 1);           // vtable slot 1 (AddRef)
    return out;
}

// sub_18AB910: resolve a wide path against the CWD / archive into a fresh
// 48-byte path handle. 0 until the archive framework is implemented.
static void *ResolveArchivePath(void *out, F3WString *path, int a3, int a4)
{
    (void)path; (void)a3; (void)a4;
    *(void **)out = 0;
    return *(void **)out;   // the resolved handle (0 until the framework lands)
}

// sub_180A240: resolve dirHandle's "dir.manifest" directory listing.
static void *ResolveDirManifest(void *out, void *dirHandle, void *extra)
{
    (void)dirHandle; (void)extra;
    *(void **)out = 0;
    return *(void **)out;   // the resolved handle (0 until the framework lands)
}

// sub_18A9970: the 84-byte streaming manager (LHAlloc + the VirtualFileSystemCtor
// (0x18A98F0) ctor). A full page is allocated so the +0x161 flag byte the
// front-end writes past the 84-byte object stays in the allocation.
static void *CreateStreamingManager(void **out)
{
    void *m = LHAlloc(0x300);
    if (m != 0)
        memset(m, 0, 0x300);
    *out = m;
    return m;
}

// the streaming manager's "add archive" method (vtable slot 10):
// (this, dataHandle, namePath, 2, 1, + the four caller register args)
static void StreamingMgrAddArchive(void *mgr, void *handle, F3WString *name,
                                   int flags1, int flags2,
                                   int rD, int rS, int rB, int rP)
{
    (void)mgr; (void)handle; (void)name;
    (void)flags1; (void)flags2;
    (void)rD; (void)rS; (void)rB; (void)rP;
}

// sub_1806060: look up / register the Bink bank named by nameBuf (the
// "levels.bnk" / "streaming.bnk" rdata wide string) in the archive.
static void *LookupBnk(void *out, void *cwdSlot, void *nameBuf, int flags)
{
    (void)cwdSlot; (void)nameBuf; (void)flags;
    *(void **)out = 0;
    return *(void **)out;   // the bank handle (0 until the framework lands)
}

// the data-file object's "register entry" method (vtable slot 10):
// (this, resolvedHandle, nameBuf, flags1, flags2)
static void DataFileRegisterEntry(void *file, void *handle, void *nameBuf,
                                  int flags1, int flags2)
{
    (void)file; (void)handle; (void)nameBuf;
    (void)flags1; (void)flags2;
}

// the archive manager's "add path" method (vtable slot 4):
// (this, cwdSlot, bookPath, namePtr, flags)
static void ArchiveMgrAddPath(void *archive, F3SharedBuffer **cwdSlot,
                              void *bookPath, void *namePtr, int flags)
{
    (void)archive; (void)cwdSlot; (void)bookPath;
    (void)namePtr; (void)flags;
}

// ============================================================
//  The file subsystem (LHSFS) -- the embedded 0x8C-byte global object at
//  0x1E2D7C8, its lazy ctor, the atexit destructor, and the provider
//  registration cluster.
// ============================================================

// sub_1812C10 / sub_18120A0 / sub_1812410 / sub_1812C30 / sub_1812350 /
// sub_1813030 -- the six registered file-provider ctors ("Group" / "Bank" /
// "Win32" / "Composite" / "Ref" / "Switch"). The LHSFS provider framework is
// unimplemented in this build, so each ctor degrades to a null-returning
// no-op.
static void *FileProviderGroupCtor     (void) { return 0; }   // sub_1812C10
static void *FileProviderBankCtor      (void) { return 0; }   // sub_18120A0
static void *FileProviderWin32Ctor     (void) { return 0; }   // sub_1812410
static void *FileProviderCompositeCtor (void) { return 0; }   // sub_1812C30
static void *FileProviderRefCtor       (void) { return 0; }   // sub_1812350
static void *FileProviderSwitchCtor    (void) { return 0; }   // sub_1813030

// sub_1817B50 (0x1817B50 - 0x1817D14) -- look up (creating when absent) the
// file-provider registration node for a name and return a pointer to its
// ctor function slot. The provider table is unimplemented in this build, so
// the lookup degrades to a fixed slot the ctor pointer is written into
// (invisible in this build otherwise).
static DWORD g_fileProviderCtorSlot = 0;
static DWORD *FileProviderCtorSlot(F3AString *name)
{
    (void)name;
    return &g_fileProviderCtorSlot;
}

// sub_181D270 (0x181D270) -- register the config node under its name; returns
// a pointer to the stored-node slot. Unimplemented: a fixed slot.
static DWORD g_vfsCfgNodeSlot = 0;
static DWORD *VfsCfgNodeNameSlot(F3AString *name)
{
    (void)name;
    return &g_vfsCfgNodeSlot;
}

// sub_18194C0 (0x18194C0 - 0x18194EC) -- the 64-byte VFS config-node object
// ctor (the sub_181BA00 / sub_181BA80 node-slot inits). Unimplemented in
// this build: the block stays zeroed.
static void *VfsCfgNodeResCtor(void *obj)
{
    memset(obj, 0, 0x40);
    return obj;
}

// sub_1819600 (0x1819600 - 0x18196A2) -- init a 64-byte VFS config node as a
// "group" node: the LHVFS::VFSConfigNodes::CConfigNode vtable (0x1BC840C),
// the 28-byte embedded name string at +4 (empty, SSO cap 15), the handle at
// +0x20, then the CGroupNode vtable (0x1BC8420) re-stored, a 4-byte heap
// wrapper at +0x24 (pointing at the member slot), three zeroed words at
// +0x30, the flag byte at +0x3C, and -- when the handle is non-null and the
// name non-empty -- the node registered under its name (sub_181D270). The
// node vtables are unimplemented in this build, so a placeholder no-release
// vtable is stored instead.
static void *g_VfsCfgNodeVtable[2] = { (void *)F3StubRelease, 0 };
static void *VfsCfgGroupNodeInit(void *obj, F3AString *name, void *handle, int flag)
{
    BYTE *b = (BYTE *)obj;
    DWORD *o = (DWORD *)obj;
    o[0] = (DWORD)g_VfsCfgNodeVtable;       // the CConfigNode vtable (0x1BC840C)
    b[8] = 0;                               // the embedded name string's SSO buffer[0]
    o[0x18 / 4] = 0;                        // name len
    o[0x1C / 4] = 15;                       // name cap (SSO)
    o[0x20 / 4] = (DWORD)handle;
    o[0] = (DWORD)g_VfsCfgNodeVtable;       // re-stored as the CGroupNode vtable
    void *w = LHAlloc(4);
    if (w != 0)
        *(void **)w = (void *)(b + 0x24);
    o[0x24 / 4] = (DWORD)w;
    o[0x30 / 4] = 0;
    o[0x34 / 4] = 0;
    o[0x38 / 4] = 0;
    b[60] = (BYTE)flag;
    if (handle != 0 && name->len != 0)
        *VfsCfgNodeNameSlot(name) = (DWORD)obj;   // sub_181D270
    return obj;
}

// sub_1803190 / sub_1815470 / sub_18154F0 (0x1803190 - 0x1803202, 0x1815470
// - 0x1815518, 0x18154F0 - 0x1815598) -- the shared member-slot initializer
// (usercall thiscall + retn 8; the two stack args are a spilled handle copy
// and go unused). Allocates a 4-byte heap wrapper holding the member-slot
// pointer (slot[0]) and a 48- or 72-byte sub-object (slot[6]) whose header
// words all point at itself, byte 44 = 1, byte 45 = 1 (the 72-byte variant
// uses bytes 68/69); slot[7] = 0.
static void FileSubsysSubObjInit(void *slot, unsigned int size)
{
    void *w = LHAlloc(4);
    if (w != 0)
        *(void **)w = slot;
    *(void **)slot = w;
    void *o = LHAlloc(size);
    if (o != 0)
        memset(o, 0, 12);
    BYTE *b = (BYTE *)o;
    b[size - 3] = 0;
    b[size - 4] = 1;
    *(void **)((char *)slot + 24) = o;
    if (o != 0)
    {
        b[size - 3] = 1;
        *(void **)o = o;
        *(void **)((char *)o + 4) = o;
        *(void **)((char *)o + 8) = o;
    }
    *(void **)((char *)slot + 28) = 0;
}

// sub_18134F0 (0x18134F0 - 0x1813754) -- the file-subsystem object ctor
// (thiscall + retn 4; the archive handle is the spilled stack arg): the
// 4-byte wrapper + 48-byte sub-object init (sub_1815470) over the +0x00
// member, the 64-byte config-node resource (sub_18194C0) at +0x20, the
// wrapper + 72-byte sub-object init (sub_18154F0) over the +0x24 member,
// +0x44 = 0, the archive handle at +0x48 (AddRef'd through vtable slot 1),
// the empty-name "group" node registration (sub_1819600, result into +0x44),
// then the six named file-provider registrations (sub_1817B50): Group /
// Bank / Win32 / Composite / Ref / Switch.
static void *FileSubsysCtorBody(void *obj, void *handle)
{
    DWORD *o = (DWORD *)obj;
    FileSubsysSubObjInit(obj, 48);                      // sub_1815470
    void *res = 0;
    void *resMem = LHAlloc(0x40);
    if (resMem != 0)
        res = VfsCfgNodeResCtor(resMem);                // sub_18194C0
    o[0x20 / 4] = (DWORD)res;
    FileSubsysSubObjInit((char *)obj + 0x24, 72);       // sub_18154F0
    o[0x44 / 4] = 0;
    o[0x48 / 4] = (DWORD)handle;
    if (handle != 0)
        F3CallVtable(handle, 1);                        // vtable slot 1 (AddRef)
    void *node = 0;
    void *nodeMem = LHAlloc(0x40);
    if (nodeMem != 0)
    {
        F3AString s;
        F3AStringReset(&s);
        F3AStringAssign(&s, "", 0);                     // byte_1BCE523, len 0
        node = VfsCfgGroupNodeInit(nodeMem, &s, res, 1);    // sub_1819600
    }
    o[0x44 / 4] = (DWORD)node;
    struct { const char *name; unsigned int len; void *ctor; } providers[] =
    {
        { "Group",     5, (void *)FileProviderGroupCtor },
        { "Bank",      4, (void *)FileProviderBankCtor },
        { "Win32",     5, (void *)FileProviderWin32Ctor },
        { "Composite", 9, (void *)FileProviderCompositeCtor },
        { "Ref",       3, (void *)FileProviderRefCtor },
        { "Switch",    6, (void *)FileProviderSwitchCtor },
    };
    for (int i = 0; i < 6; i++)
    {
        F3AString n;
        F3AStringReset(&n);
        F3AStringAssign(&n, providers[i].name, providers[i].len);
        *FileProviderCtorSlot(&n) = (DWORD)providers[i].ctor;    // sub_1817B50
        F3AStringFree(&n);
    }
    return obj;
}

// sub_1801A30 (0x1801A30 - 0x1801A95) -- the file-subsystem object ctor
// (thiscall + retn 4; the archive handle is the spilled stack arg): builds
// the object (FileSubsysCtorBody, sub_18134F0), the 4-byte heap wrapper at
// +0x4C (pointing at the member slot), three zeroed words at +0x58, the
// wrapper + 48-byte sub-object init (sub_1803190) over the +0x64 member,
// +0x84 = 0, and holds the archive handle at +0x88 (AddRef'd through vtable
// slot 1).
static void *FileSubsysCtor(void *obj, void *handle)
{
    DWORD *o = (DWORD *)obj;
    FileSubsysCtorBody(obj, handle);                    // sub_18134F0
    void *w = LHAlloc(4);
    if (w != 0)
        *(void **)w = (void *)((char *)obj + 0x4C);
    o[0x4C / 4] = (DWORD)w;
    o[0x58 / 4] = 0;
    o[0x5C / 4] = 0;
    o[0x60 / 4] = 0;
    FileSubsysSubObjInit((char *)obj + 0x64, 48);       // sub_1803190
    o[0x84 / 4] = 0;
    o[0x88 / 4] = (DWORD)handle;
    if (handle != 0)
        F3CallVtable(handle, 1);                        // vtable slot 1 (AddRef)
    return obj;
}

// sub_18030E0 (0x18030E0 - 0x18030F1) -- release every object pointer in
// the range [begin, end): each non-null entry through its vtable slot 2.
static void FileSubsysReleaseRange(void *begin, void *end)
{
    for (void **p = (void **)begin; p != (void **)end; p++)
        if (*p != 0)
            F3CallVtable(*p, 2);
}

// sub_1817FE0 (0x1817FE0) -- the 64-byte VFS config-node dtor. Unimplemented
// in this build: no-op.
static void VfsCfgNodeDtor(void *node) { (void)node; }
// sub_1803BE0 (0x1804620's +0x64-member reset, 0x1803BE0 - 0x1804058) --
// re-link the sub-object list to empty and release the dropped entries.
// Unimplemented in this build: no-op.
static void FileSubsysSubObjReset64(void *local, void *w, void *o, void *w2, void *o2)
{ (void)local; (void)w; (void)o; (void)w2; (void)o2; }
// sub_1816500 (0x1816500) -- the 72-byte sub-object list reset (+0x24
// member). Unimplemented in this build: no-op.
static void FileSubsysSubObjReset72(void *local, void *w, void *o, void *w2, void *o2)
{ (void)local; (void)w; (void)o; (void)w2; (void)o2; }
// sub_18171D0 (0x18171D0) -- the 48-byte sub-object list reset (+0x00
// member). Unimplemented in this build: no-op.
static void FileSubsysSubObjReset48(void *local, void *w, void *o, void *w2, void *o2)
{ (void)local; (void)w; (void)o; (void)w2; (void)o2; }

// sub_1813900 (0x1813900 - 0x18139AC) -- the file-subsystem dtor body:
// release the +0x44 group node through its vtable slot 0 (one arg, freeIt =
// 1), the +0x20 64-byte config-node resource (sub_1817FE0 + LHFree), and
// the +0x48 archive handle (vtable slot 2), then tear down the +0x24
// (sub_1816500) and +0x00 (sub_18171D0) sub-object pairs (list reset, LHFree
// the sub-object, zero the slot words, LHFree the wrapper).
static int FileSubsysDtorBody(void *obj)
{
    DWORD *o = (DWORD *)obj;
    void *group = (void *)o[0x44 / 4];
    if (group != 0)
        ((void (*)(void *, int))F3_VT(group)[0])(group, 1);
    void *res = (void *)o[0x20 / 4];
    if (res != 0)
    {
        VfsCfgNodeDtor(res);                            // sub_1817FE0
        LHFree(res);
    }
    void *handle = (void *)o[0x48 / 4];
    if (handle != 0)
        F3CallVtable(handle, 2);                        // vtable slot 2 (Release)
    char tmp[8];
    FileSubsysSubObjReset72(tmp,
        (void *)o[0x24 / 4], *(void **)o[0x3C / 4],
        (void *)o[0x24 / 4], (void *)o[0x3C / 4]);      // sub_1816500
    LHFree((void *)o[0x3C / 4]);                        // the 72-byte sub-object
    o[0x3C / 4] = 0;
    o[0x40 / 4] = 0;
    LHFree((void *)o[0x24 / 4]);                        // the 4-byte wrapper
    FileSubsysSubObjReset48(tmp,
        (void *)o[0x00 / 4], *(void **)o[0x00 / 4],
        (void *)o[0x00 / 4], (void *)o[0x18 / 4]);      // sub_18171D0
    LHFree((void *)o[0x18 / 4]);                        // the 48-byte sub-object
    o[0x18 / 4] = 0;
    o[0x1C / 4] = 0;
    LHFree((void *)o[0x00 / 4]);                        // the 4-byte wrapper
    return 0;                                           // the LHFree result
}

// sub_1804620 (0x1804620 - 0x18046C5) -- the file-subsystem object dtor:
// release the +0x88 and +0x84 held handles (vtable slot 2), tear down the
// +0x64 member (sub_1803BE0 list reset, LHFree the 48-byte sub-object at
// +0x7C, zero +0x7C / +0x80, LHFree the wrapper at +0x64), tear down the
// +0x4C member (the +0x58 object range released through sub_18030E0, LHFree
// +0x58, zero +0x58 .. +0x60, LHFree the wrapper at +0x4C), then tail-call
// the dtor body (sub_1813900).
static int FileSubsysDtor(void *obj)
{
    DWORD *o = (DWORD *)obj;
    void *h1 = (void *)o[0x88 / 4];
    if (h1 != 0)
        F3CallVtable(h1, 2);                            // vtable slot 2 (Release)
    void *h2 = (void *)o[0x84 / 4];
    if (h2 != 0)
        F3CallVtable(h2, 2);
    char tmp[8];
    FileSubsysSubObjReset64(tmp,
        (void *)o[0x64 / 4], *(void **)o[0x7C / 4],
        (void *)o[0x64 / 4], (void *)o[0x7C / 4]);      // sub_1803BE0
    LHFree((void *)o[0x7C / 4]);                        // the 48-byte sub-object
    o[0x7C / 4] = 0;
    o[0x80 / 4] = 0;
    LHFree((void *)o[0x64 / 4]);                        // the 4-byte wrapper
    void *m58 = (void *)o[0x58 / 4];
    if (m58 != 0)
    {
        FileSubsysReleaseRange(m58, (void *)o[0x5C / 4]);   // sub_18030E0
        LHFree(m58);
    }
    o[0x58 / 4] = 0;
    o[0x5C / 4] = 0;
    o[0x60 / 4] = 0;
    LHFree((void *)o[0x4C / 4]);                        // the 4-byte wrapper
    return FileSubsysDtorBody(obj);                     // sub_1813900 (tail call)
}

// sub_1AD3940 (0x1AD3940 - 0x1AD394A) -- the atexit handler: destruct the
// global file-subsystem object (sub_1804620).
static void FileSubsysAtExit(void)
{
    FileSubsysDtor(g_fileSubsys);
}

//  GetFileSubsystem  (0x1801C20 - 0x1801C70)  -- the global file-subsystem
//  singleton getter (thiscall; the handle in ECX is spilled to the stack and
//  never read): on the first call it sets the 0x1E2D854 init flag, takes the
//  archive manager out of its slot (GetArchiveSlot, sub_180AC90 -- AddRef'd),
//  constructs the 0x8C-byte global object around it (FileSubsysCtor,
//  sub_1801A30), drops the temporary reference (vtable slot 2), and
//  registers the atexit destructor (sub_1AD3940 -> FileSubsysDtor). Always
//  returns the address of the global object (&unk_1E2D7C8).
//  [NOT byte-identical] placement-dependent (relative calls to
//  GetArchiveSlot / FileSubsysCtor / the atexit IAT thunk / the vtable
//  release) and refs the high-VA globals g_dword_1E2D854 / g_dword_1E2D864
//  and the embedded g_fileSubsys object at their original addresses.
void *GetFileSubsystem(void *obj)
{
    (void)obj;                     // the original spills it but never reads it
    if ((g_dword_1E2D854 & 1) == 0)
    {
        g_dword_1E2D854 |= 1;
        void *handle = 0;
        GetArchiveSlot((F3SharedBuffer **)&handle);     // sub_180AC90
        FileSubsysCtor(g_fileSubsys, handle);           // sub_1801A30
        if (handle != 0)
            F3CallVtable(handle, 2);                    // vtable slot 2 (Release)
        atexit(FileSubsysAtExit);
    }
    return g_fileSubsys;
}

// sub_180F350 (0x180F350 - 0x180F3C4) -- initialize the 0x20-byte
// VFS archive-collection object's 44-byte red-black-tree sentinel: the
// 4-byte heap wrapper (pointing back at the object) at [0], the 44-byte
// sentinel node (all three child/self pointers set to itself, the 40/41
// flag bytes set) at [6], and [7] = 0.  The archive framework is stubbed, so
// the tree stays empty (a single sentinel).
static void VfsCollectionTreeInit(void *slot)
{
    void *w = LHAlloc(4);
    if (w != 0)
        *(void **)w = slot;
    *(void **)slot = w;                       // slot[0] = wrapper
    BYTE *n = (BYTE *)LHAlloc(44);            // the 44-byte sentinel node
    DWORD *o = (DWORD *)slot;
    o[6] = (DWORD)n;                          // slot[6] = node (+0x18)
    if (n != 0)
    {
        memset(n, 0, 44);
        n[40] = 1;
        n[41] = 1;
        ((DWORD *)n)[0] = (DWORD)n;           // node[0] = node (self)
        ((DWORD *)n)[1] = (DWORD)n;           // node[1] = node (self)
        ((DWORD *)n)[2] = (DWORD)n;           // node[2] = node (self)
    }
    o[7] = 0;                                 // slot[7] = 0 (+0x1C)
}

// sub_180F3D0 (0x180F3D0 - 0x180F444) -- the vector sibling of the above:
// the same {wrapper, sentinel, count} init but with a 24-byte sentinel node
// (the 20/21 flag bytes set).  The archive framework is stubbed, so the
// vector stays empty.
static void VfsCollectionVecInit(void *slot)
{
    void *w = LHAlloc(4);
    if (w != 0)
        *(void **)w = slot;
    *(void **)slot = w;                       // slot[0] = wrapper
    BYTE *n = (BYTE *)LHAlloc(24);            // the 24-byte sentinel node
    DWORD *o = (DWORD *)slot;
    o[6] = (DWORD)n;                          // slot[6] = node (+0x18)
    if (n != 0)
    {
        memset(n, 0, 24);
        n[20] = 1;
        n[21] = 1;
        ((DWORD *)n)[0] = (DWORD)n;           // node[0] = node (self)
        ((DWORD *)n)[1] = (DWORD)n;           // node[1] = node (self)
        ((DWORD *)n)[2] = (DWORD)n;           // node[2] = node (self)
    }
    o[7] = 0;                                 // slot[7] = 0 (+0x1C)
}

// sub_5B17B0 (0x5B17B0 - 0x5B17F5) -- clear a range of 36-byte (9-DWORD)
// records from first to last (exclusive): for each record, free the +0x04
// payload when the +0x18 type is >= 8 (through the heap-table free slot =
// LHFree), then set the type to 7, zero the +0x14 word, and zero the payload
// pointer's low word.  Shared by the VFS archive-collection tree nodes
// (VfsCollectionTreeNodeDtor) and the general record-table teardown
// (sub_5AF760 / sub_5B0330 / sub_5B0350 / sub_5B1460).
// [NOT byte-identical] placement-dependent (relative call) and refs the
// high-VA heap table (0x02075B24) via LHFree.
static void ReleaseRecordRange(void *first, void *last)
{
    for (DWORD *r = (DWORD *)first; r != (DWORD *)last; r += 9)
    {
        if (r[6] >= 8)
            LHFree((void *)r[1]);
        r[6] = 7;
        r[5] = 0;
        *(WORD *)(r + 1) = 0;
    }
}

// sub_180F060 (0x180F060 - 0x180F0A4) -- the recursive vector-node dtor of
// the VFS archive-collection vector (cdecl): walks the 24-byte element nodes
// backward through the +0x00 links, stopping at the sentinel (byte +0x15 = 1).
// For each live node: recurse into the +0x08 link, release the +0x10 payload
// handle through vtable slot 1 when non-null, and LHFree the node.
// [NOT byte-identical] placement-dependent (relative recursive call + LHFree +
// the payload's vtable release).
static void VfsCollectionVecNodeDtor(void *node)
{
    for (DWORD *i = (DWORD *)node; ((BYTE *)i)[21] == 0; )
    {
        VfsCollectionVecNodeDtor((void *)i[2]);
        if (i[4] != 0)
            F3CallVtable((void *)i[4], 1);     // vtable slot 1 (Release)
        DWORD *next = (DWORD *)i[0];
        LHFree(i);
        i = next;
    }
}

// sub_18109B0 (0x18109B0 - 0x1810A1C) -- the recursive tree-node dtor of the
// VFS archive-collection tree (cdecl): walks the 44-byte red-black element
// nodes backward through the +0x00 links, stopping at the sentinel (byte
// +0x29 = 1).  For each live node: recurse into the +0x08 link, clear the
// [+0x1C, +0x20) 36-byte record range (ReleaseRecordRange) and LHFree the
// record block when present, zero the +0x1C / +0x20 / +0x24 fields, and
// LHFree the +0x10 block and the node.
// [NOT byte-identical] placement-dependent (relative recursive call +
// ReleaseRecordRange + LHFree).
static void VfsCollectionTreeNodeDtor(void *node)
{
    for (DWORD *i = (DWORD *)node; ((BYTE *)i)[41] == 0; )
    {
        VfsCollectionTreeNodeDtor((void *)i[2]);
        if (i[7] != 0)
        {
            ReleaseRecordRange((void *)i[7], (void *)i[8]);
            LHFree((void *)i[7]);
        }
        i[7] = 0;
        i[8] = 0;
        i[9] = 0;
        LHFree((void *)i[4]);
        DWORD *next = (DWORD *)i[0];
        LHFree(i);
        i = next;
    }
}

// sub_180DF70 (0x180DF70 - 0x180DFD3) -- advance a VFS archive-collection
// vector iterator by one element (thiscall; the 2-DWORD {base, node} iterator
// in ECX): a sentinel base or node is __invalid_parameter_noinfo; when the
// node's +0x08 link is the sentinel, walk the +0x00 links from the sentinel
// back to its predecessor (the last live node); otherwise follow the +0x04
// links while the iterator's node equals the candidate's +0x08 link, stopping
// at the sentinel, and the +0x04 value is stored.  Pure pointer logic.
// [NOT byte-identical] placement-dependent (relative call to
// __invalid_parameter_noinfo).
static void VfsCollectionVecIterAdvance(DWORD *it)
{
    if (it[0] == 0)
    {
        F3InvalidParam();
        return;                    // original: jmp __invalid_parameter_noinfo (no return)
    }
    DWORD *n = (DWORD *)it[1];
    if (((BYTE *)n)[21] != 0)
    {
        F3InvalidParam();
        return;                    // original: jmp __invalid_parameter_noinfo (no return)
    }
    DWORD *m = (DWORD *)n[2];
    if (((BYTE *)m)[21] != 0)
    {
        DWORD *x = m;
        while (((BYTE *)(*x))[21] == 0)
            x = (DWORD *)(*x);
        it[1] = (DWORD)x;
    }
    else
    {
        DWORD *x = (DWORD *)n[1];
        while (((BYTE *)x)[21] == 0 && it[1] == x[2])
        {
            it[1] = (DWORD)x;
            x = (DWORD *)x[1];
        }
        it[1] = (DWORD)x;
    }
}

// sub_180E070 (0x180E070 - 0x180E0D3) -- the tree sibling of the above, over
// the 44-byte nodes (the sentinel flag is byte +0x29 instead of +0x15).
// [NOT byte-identical] placement-dependent (relative call to
// __invalid_parameter_noinfo).
static void VfsCollectionTreeIterAdvance(DWORD *it)
{
    if (it[0] == 0)
    {
        F3InvalidParam();
        return;                    // original: jmp __invalid_parameter_noinfo (no return)
    }
    DWORD *n = (DWORD *)it[1];
    if (((BYTE *)n)[41] != 0)
    {
        F3InvalidParam();
        return;                    // original: jmp __invalid_parameter_noinfo (no return)
    }
    DWORD *m = (DWORD *)n[2];
    if (((BYTE *)m)[41] != 0)
    {
        DWORD *x = m;
        while (((BYTE *)(*x))[41] == 0)
            x = (DWORD *)(*x);
        it[1] = (DWORD)x;
    }
    else
    {
        DWORD *x = (DWORD *)n[1];
        while (((BYTE *)x)[41] == 0 && it[1] == x[2])
        {
            it[1] = (DWORD)x;
            x = (DWORD *)x[1];
        }
        it[1] = (DWORD)x;
    }
}

// sub_180F450 (0x180F450 - 0x180F77F) -- erase one element from the VFS
// archive-collection vector (the 815-byte red-black rebalance +
// std::out_of_range throw).  Remaining no-op stub: only reachable from the
// slow path of VfsCollectionVecEraseRange, which requires a non-empty
// collection -- never the case in this build.
static DWORD *VfsCollectionVecEraseElement(DWORD *obj, DWORD *out, DWORD base, DWORD node)
{
    (void)base;
    out[0] = obj[0];
    out[1] = node;
    return out;
}

// sub_1810650 (0x1810650 - 0x18109AA) -- erase one element from the VFS
// archive-collection tree (the 858-byte red-black rebalance +
// std::out_of_range throw + the record-range release).  Remaining no-op
// stub: only reachable from the slow path of VfsCollectionTreeEraseRange,
// which requires a non-empty collection -- never the case in this build.
static DWORD *VfsCollectionTreeEraseElement(DWORD *obj, DWORD *out, DWORD base, DWORD node)
{
    (void)base;
    out[0] = obj[0];
    out[1] = node;
    return out;
}

// sub_180FA60 (0x180FA60 - 0x180FB3E) -- erase a range of elements from the
// VFS archive-collection vector (thiscall + `retn 14h` hybrid; declared as a
// free function here): the 0x20-byte collection record in ECX, the 2-DWORD
// out iterator pair, and the begin / end iterator pairs (each {base, node};
// the begin pair is advanced in place).  A base that is null or != the
// collection's 4-byte wrapper is __invalid_parameter_noinfo.  The fast path
// (begin node == the sentinel's +0x00 value and end node == the sentinel
// itself, i.e. the whole collection in its empty state) destroys the first
// node (VfsCollectionVecNodeDtor),
// resets the list to a single self-linked sentinel with the count zeroed,
// and returns {wrapper, sentinel}.  The slow path advances the begin
// iterator (VfsCollectionVecIterAdvance) and erases the current element
// (VfsCollectionVecEraseElement) until the end node is reached.
// [NOT byte-identical] placement-dependent (relative calls to the node dtor
// / iterator advance / element erase / __invalid_parameter_noinfo).
static DWORD *VfsCollectionVecEraseRange(DWORD *obj, DWORD *out,
    DWORD beginBase, DWORD beginNode, DWORD endBase, DWORD endNode)
{
    DWORD *node = (DWORD *)obj[6];
    if (beginBase == 0 || beginBase != obj[0])
        F3InvalidParam();
    if (beginNode == node[0])
    {
        if (endBase == 0 || endBase != obj[0])
            F3InvalidParam();
        if (endNode == (DWORD)node)
        {
            VfsCollectionVecNodeDtor((void *)node[1]);   // destroy the list
            node[1] = (DWORD)node;
            obj[7] = 0;                                  // count
            node[0] = (DWORD)node;
            node[2] = (DWORD)node;
            out[1] = node[0];
            out[0] = obj[0];                             // wrapper
            return out;
        }
    }
    DWORD it[2] = { beginBase, beginNode };
    DWORD cur = beginBase;
    DWORD last = beginNode;
    for (;;)
    {
        if (cur == 0 || cur != endBase)
            F3InvalidParam();
        last = it[1];
        if (it[1] == endNode)
            break;
        VfsCollectionVecIterAdvance(it);
        DWORD tmp[2];
        VfsCollectionVecEraseElement(obj, tmp, cur, last);
        cur = it[0];
    }
    out[0] = obj[0];                                     // wrapper
    out[1] = last;
    return out;
}

// sub_1810D20 (0x1810D20 - 0x1810DFB) -- the tree sibling of the above, over
// the 44-byte red-black nodes (VfsCollectionTreeNodeDtor /
// VfsCollectionTreeIterAdvance / VfsCollectionTreeEraseElement).
// [NOT byte-identical] placement-dependent (relative calls to the node dtor
// / iterator advance / element erase / __invalid_parameter_noinfo).
static DWORD *VfsCollectionTreeEraseRange(DWORD *obj, DWORD *out,
    DWORD beginBase, DWORD beginNode, DWORD endBase, DWORD endNode)
{
    DWORD *node = (DWORD *)obj[6];
    if (beginBase == 0 || beginBase != obj[0])
        F3InvalidParam();
    if (beginNode == node[0])
    {
        if (endBase == 0 || endBase != obj[0])
            F3InvalidParam();
        if (endNode == (DWORD)node)
        {
            VfsCollectionTreeNodeDtor((void *)node[1]);  // destroy the tree
            node[1] = (DWORD)node;
            obj[7] = 0;                                  // count
            node[0] = (DWORD)node;
            node[2] = (DWORD)node;
            out[1] = node[0];
            out[0] = obj[0];                             // wrapper
            return out;
        }
    }
    DWORD it[2] = { beginBase, beginNode };
    DWORD cur = beginBase;
    DWORD last = beginNode;
    for (;;)
    {
        if (cur == 0 || cur != endBase)
            F3InvalidParam();
        last = it[1];
        if (it[1] == endNode)
            break;
        VfsCollectionTreeIterAdvance(it);
        DWORD tmp[2];
        VfsCollectionTreeEraseElement(obj, tmp, cur, last);
        cur = it[0];
    }
    out[0] = obj[0];                                     // wrapper
    out[1] = last;
    return out;
}

// sub_1810F50 (0x1810F50 - 0x1810FD6) -- the global VFS archive-collection
// dtor (thiscall; the 0x1E2D870 collection record in ECX): release the held
// archive handle (+0x40 = 0x1E2D8B0) through vtable slot 1 when non-null,
// erase + free the 24-byte-node vector sub-object at +0x20 (0x1E2D890) --
// the whole-range erase, then LHFree the sentinel node and the 4-byte
// wrapper, the node / count fields zeroed -- and do the same for the
// 44-byte-node tree sub-object at +0x00.
// [NOT byte-identical] placement-dependent (relative calls to the two range
// erases / LHFree / the handle's vtable release).
static void VfsArchiveCollectionDtor(DWORD *obj)
{
    DWORD out[2];
    if (obj[16] != 0)                                    // +0x40 = 0x1E2D8B0
        F3CallVtable((void *)obj[16], 1);                // vtable slot 1 (Release)
    DWORD *vnode = (DWORD *)obj[14];                     // +0x38 = 0x1E2D8A8
    VfsCollectionVecEraseRange(obj + 8, out, obj[8], vnode[0], obj[8], (DWORD)vnode);
    LHFree((void *)vnode);
    obj[14] = 0;
    obj[15] = 0;
    LHFree((void *)obj[8]);
    DWORD *tnode = (DWORD *)obj[6];                      // +0x18 = 0x1E2D888
    VfsCollectionTreeEraseRange(obj, out, obj[0], tnode[0], obj[0], (DWORD)tnode);
    LHFree((void *)tnode);
    obj[6] = 0;
    obj[7] = 0;
    LHFree((void *)obj[0]);
}

// sub_1AD39E0 (0x1AD39E0 - 0x1AD39EA) -- the atexit destructor for the
// global VFS archive collection: destruct the 0x1E2D870 collection record
// (VfsArchiveCollectionDtor).  Registered by GetVfsArchiveCollection; in this
// build the handle is null and both collections hold only their sentinels, so
// it frees the two 4-byte wrappers + two sentinel nodes and zeroes the fields.
// [NOT byte-identical] placement-dependent (the original is
// `mov ecx, offset dword_1E2D870; jmp sub_1810F50`).
static void VfsArchiveCollectionAtExit(void)
{
    VfsArchiveCollectionDtor(g_unk_1E2D870);
}

// sub_180D9B0 (0x180D9B0 - 0x180DA37) -- the lazy one-time init of the global
// VFS archive collection (guarded by the g_dword_1E2D8B4 flag): the archive
// handle (GetArchiveSlot, AddRef'd) is taken, the two collection sub-objects
// are initialized (the 44-byte tree sentinel at g_unk_1E2D870 and the 24-byte
// vector sentinel at g_unk_1E2D890), the handle is stored at g_dword_1E2D8B0
// (its reference bumped and the temporary dropped), and the atexit destructor
// (VfsArchiveCollectionAtExit, sub_1AD39E0) is registered.  Always returns
// &g_unk_1E2D870.
//  [NOT byte-identical] placement-dependent (relative calls to GetArchiveSlot /
//  the two collection sub-object inits / the vtable AddRef + Release /
//  _atexit) and refs the high-VA globals at their original addresses.
static void *GetVfsArchiveCollection(void)
{
    if ((g_dword_1E2D8B4 & 1) == 0)
    {
        g_dword_1E2D8B4 |= 1;
        F3SharedBuffer *slot = 0;
        F3SharedBuffer *handle = *GetArchiveSlot(&slot);    // sub_180AC90
        VfsCollectionTreeInit(g_unk_1E2D870);               // sub_180F350
        VfsCollectionVecInit(g_unk_1E2D890);                // sub_180F3D0
        g_dword_1E2D8B0 = handle;
        if (handle != 0)
            F3CallVtable(handle, 1);                        // vtable slot 1 (AddRef)
        if (slot != 0)
            F3CallVtable(slot, 2);                          // vtable slot 2 (Release)
        atexit(VfsArchiveCollectionAtExit);
    }
    return g_unk_1E2D870;
}

// sub_180D220 (0x180D220 - 0x180D3E1) -- the file subsystem's per-entry VFS
// reload (thiscall + `retn 0Ch`; the archive collection in ECX, the out entry
// slot, the entry, and the .vfsconfig path F3WString on the stack).  Hashes the
// path (GetWideStringHash / sub_56DFD0), looks the entry up in the archive collection (sub_180F0E0
// tree search), and dispatches the matched provider's reload (the entry
// vtable[4] with the path / provider index).  Unimplemented in this build: the
// provider-name lookup + the tree search are remaining no-op stubs because the
// archive collection is empty, so no provider is ever matched and the reload
// finds nothing -- the out slot is cleared and the out pointer is returned.
static void *VfsReloadFileEntry(void *collection, void **out, void *entry, void *path)
{
    (void)collection; (void)entry; (void)path;
    *out = 0;                    // no provider matched (empty archive collection)
    return out;
}

// sub_17DBD50 (0x17DBD50 - 0x17DBD9C) -- re-open the stream's 8-byte file
// block (close + free the old, allocate + init a fresh one) and (re)read the
// entry into it through the CXMLDocumentParser (sub_17E69D0).  Returns the
// parse result (0 / 1).  The LHXML parse engine is a remaining stub, so the
// parse is reported to have succeeded (the block is freshly allocated) but the
// tree is never populated -- no section is ever found, matching OpenXmlFile.
static char ReopenXmlFileBlock(F3XmlStream *stream, void *entry)
{
    (void)entry;
    DWORD *old = (DWORD *)stream->fileBuf;
    if (old != 0)
    {
        CloseXmlFileBlock(old);           // sub_17E68F0
        XmlHeapFree(old);                 // sub_17E65B0
    }
    DWORD *block = (DWORD *)XmlHeapAlloc(8);
    stream->fileBuf = (block != 0) ? InitXmlFileBlock(block) : 0;
    return (stream->fileBuf != 0) ? 1 : 0;
}

// sub_1812980 (0x1812980 - 0x1812C0A) -- the VFS config-group builder: builds
// a 64-byte group node from the "ID" / "Mountable" attributes + the child
// provider elements (VfsCfgGroupNodeInit + the per-child FileProviderCtorSlot
// dispatch + sub_18192A0 registration).  Never reached in this build (the
// VFSConfig root is never found), so it degrades to a no-op returning 0.
static void *BuildVfsConfigGroup(void *pair, int flag1, int flag2)
{
    (void)pair; (void)flag1; (void)flag2;
    return 0;
}

// sub_18192A0 (0x18192A0 - 0x181931B) -- register (or erase) a VFS config node
// in the global node collection (a vector-erase by node handle).  Never reached
// in this build (the VFSConfig root is never found); no-op.
static void RegisterVfsConfig(void *node)
{
    (void)node;
}

// sub_1813760 (0x1813760 - 0x1813850) -- the file subsystem's VFS-config root
// reader (thiscall + `retn 8`; the file subsystem in ECX, the entry + the
// stream on the stack): when the current root element is "VFSConfig" (stream
// vtable[2]), it reads the "AutoMount" attribute (stream vtable[10], defaulting
// to true when absent), builds the config group from {entry, stream}
// (BuildVfsConfigGroup), registers it (RegisterVfsConfig), and advances the
// stream (vtable[3]); returns 0 on success or E_FAIL (0x80004005) when the
// group build (or the AutoMount handle bind) fails.  The "VFSConfig" root is
// never found in this build (the LHXML parse is a remaining stub), so the body
// is unreachable and it returns 0.
static int VfsLoadConfigRoot(void *fs, void *entry, F3XmlStream *stream)
{
    void **vt = (void **)*(void **)stream;
    if (((char(__thiscall *)(void *, const char *))vt[2])(stream, "VFSConfig") != 0)
    {
        F3AString autoMount;
        F3AStringReset(&autoMount);
        F3AStringAssign(&autoMount, "AutoMount", 9);      // sub_402F00
        BYTE autoMountVal = 1;
        if (((char(__thiscall *)(void *, void *, BYTE *))vt[10])
                    (stream, &autoMount, &autoMountVal) == 0)
            autoMountVal = 1;
        DWORD pair[2] = { (DWORD)entry, (DWORD)stream };
        void *config = BuildVfsConfigGroup(pair, 1, 1);   // sub_1812980 (stub -> 0)
        int bindFailed = 0;
        if (config != 0 && autoMountVal != 0)
        {
            void **configVt = (void **)*(void **)config;  // the group-node vtable
            bindFailed = ((int(__thiscall *)(void *, DWORD))configVt[1])
                               (config, ((DWORD *)fs)[18]) < 0;
        }
        if (config == 0 || bindFailed)
            return 0x80004005;                            // E_FAIL
        RegisterVfsConfig(config);                        // sub_18192A0 (stub)
        ((void(__thiscall *)(void *))vt[3])(stream);      // vtable[3] (advance)
    }
    return 0;
}

// sub_1813850 (0x1813850 - 0x18138FA) -- the file subsystem's per-entry
// VFS-config file load: thiscall + `retn 8` (the file subsystem in ECX, the
// file entry + the .vfsconfig path F3WString on the stack; declared as a plain
// cdecl free function here, all callers are project code).  Lazily inits the
// global archive collection (GetVfsArchiveCollection -> &g_unk_1E2D870),
// re-loads the named file entry from its provider (VfsReloadFileEntry: the
// provider-name lookup + the entry vtable[4] reload dispatch), then builds a
// 24-byte CXMLReadStream, re-opens the file block (ReopenXmlFileBlock), and
// when the root element is "VFSConfig" (VfsLoadConfigRoot) builds + registers
// the config object (sub_1812980 / sub_18192A0), returning 0 or E_FAIL
// (0x80004005).  [NOT byte-identical] placement-dependent (relative calls to
// GetVfsArchiveCollection / VfsReloadFileEntry / ReopenXmlFileBlock /
// VfsLoadConfigRoot / the entry vtable[2] release dispatch).
static int FileSubsysLoadVfsConfigFile(void *fs, void *entry, void *path)
{
    (void)fs;
    if (entry == 0)
        return 0x80004005;                            // E_FAIL
    void *collection = GetVfsArchiveCollection();     // sub_180D9B0
    VfsReloadFileEntry(collection, &entry, entry, path);   // sub_180D220
    if (entry == 0)
        return 0;
    F3XmlStream stream;
    CXMLReadStreamCtor(&stream);
    if (ReopenXmlFileBlock(&stream, entry) == 0)      // sub_17DBD50
    {
        CXMLReadStreamDtor(&stream);
        if (entry != 0)
            F3CallVtable(entry, 2);                   // vtable slot 2 (Release)
        return 0x80004005;                            // E_FAIL
    }
    int result = VfsLoadConfigRoot(fs, entry, &stream);   // sub_1813760
    CXMLReadStreamDtor(&stream);
    if (entry != 0)
        F3CallVtable(entry, 2);                       // vtable slot 2 (Release)
    return result;
}

//  FileSubsysLoadVfsConfig  (sub_1801580, 0x1801580 - 0x180161F)  -- the
//  file subsystem's VFS-config load: thiscall + `retn 4` (the 0x8C-byte
//  file-subsystem object in ECX, the .vfsconfig path F3WString on the stack;
//  declared as a plain cdecl free function here, all callers are project
//  code).  Iterates the file-entry collection [begin = +0x58, end = +0x5C),
//  and for each entry locks it (vtable slot 1), (re)loads its VFS config
//  from the path (sub_1813850 -> FileSubsysLoadVfsConfigFile), and unlocks
//  it (vtable slot 2).  The original's _invalid_parameter_noinfo bounds
//  asserts are dropped (the collection is zero-initialized and never
//  inconsistent in this build).  Dead in this build: no entries are ever
//  registered in g_fileSubsys, so the loop body never runs.  Six call sites
//  in the original: RunFrontEnd ("startup.vfsconfig") and the speech / lipsync /
//  cutscene config loaders (MountPermanentBanks 0x61C320 / sub_719DA0 /
//  sub_9F4B50 / sub_11C4C30, the latter three still stubs).  [NOT byte-identical] placement-dependent
//  (relative calls to __invalid_parameter_noinfo / sub_1813850 / the entry
//  vtable dispatches).
static void FileSubsysLoadVfsConfig(void *fs, void *path)
{
    DWORD *o = (DWORD *)fs;
    void **begin = (void **)o[0x58 / 4];
    void **end = (void **)o[0x5C / 4];
    for (void **i = begin; i != end; i++)
    {
        void *entry = *i;
        if (entry != 0)
            F3CallVtable(entry, 1);                     // vtable slot 1 (lock)
        FileSubsysLoadVfsConfigFile(fs, entry, path);   // sub_1813850
        if (entry != 0)
            F3CallVtable(entry, 2);                     // vtable slot 2 (unlock)
    }
}

// sub_18A9970 (0x18A9970 - 0x18A99A0) -- allocate (LHAlloc 0x54) and
// construct the 84-byte streaming manager (VirtualFileSystemCtor, 0x18A98F0),
// store it in *out (AddRef'd through vtable slot 1) and return out.  The
// 0x54 size here is the raw ctor's allocation (distinct from the full-page
// CreateStreamingManager the front-end uses so the +0x161 flag stays in the
// allocation).
static void *CreateStreamingManager84(void **out)
{
    void *m = LHAlloc(0x54);
    if (m != 0)
        m = VirtualFileSystemCtor(m);                       // 0x18A98F0
    *out = m;
    if (m != 0)
        ((void (*)(void *))F3_VT(m)[1])(m);                 // vtable slot 1 (AddRef)
    return out;
}

// sub_5AF290 (0x5AF290 - 0x5AF2D7) -- re-initialise the 28-byte path
// descriptor to the "Source" wide string (cap = 7, len = 0, buf[0] = 0, then
// the wide-string assign + finalize, sub_435C10 / sub_436BA0).  Only used on
// the a3 != 0 branch of FileSubsysRegisterDir (dead in this build); the wide
// "Source" assign / finalize are remaining no-op stubs.
static void F3DescInitSource(F3AString *desc)
{
    desc->d0 = 0;
    desc->u.buf[0] = 0;
    desc->len = 0;
    desc->cap = 7;
    // sub_435C10(desc, L"Source", L"Source", wcslen) + sub_436BA0(desc).
}

// sub_18013D0 (0x18013D0 - 0x18014EA) -- build the combined dir node for the
// file subsystem's current directory (thiscall + retn 8; *out is the result
// slot, a2 / a3 the two handles).  a2 == 0 -> *out = 0; a3 == 0 -> AddRef a2
// and *out = a2; otherwise build a fresh 84-byte streaming manager (sub_18A9970)
// and register both handles into it through vtable slot 10 (re-initialising the
// 28-byte descriptor between the two), then AddRef / store / Release.  Only
// reached when the archive framework lands (a2 is 0 in this build).
static void *FileSubsysBuildDirNode(void **out, void *a2, void *a3)
{
    *out = 0;
    if (a2 == 0)
        return out;
    if (a3 == 0)
    {
        ((void (*)(void *))F3_VT(a2)[1])(a2);               // vtable slot 1 (AddRef)
        if (*out != 0)
            ((void (*)(void *))F3_VT(*out)[2])(*out);       // release old (dead)
        *out = a2;
        return out;
    }
    void *mgr = a2;
    CreateStreamingManager84(&mgr);                         // sub_18A9970
    F3AString desc;
    desc.d0 = 0; desc.u.buf[0] = 0; desc.len = 0; desc.cap = 7;
    ((void (__thiscall *)(void *, void *, F3AString *, int, int))
        F3_VT(mgr)[10])(mgr, a2, &desc, 2, 1);
    if (desc.cap >= 8)
        LHFree((void *)desc.u.ptr);
    desc.d0 = 0; desc.u.buf[0] = 0; desc.len = 0; desc.cap = 7;
    ((void (__thiscall *)(void *, void *, F3AString *, int, int))
        F3_VT(mgr)[10])(mgr, a3, &desc, 2, 1);
    if (desc.cap >= 8)
        LHFree((void *)desc.u.ptr);
    ((void (*)(void *))F3_VT(mgr)[1])(mgr);                 // vtable slot 1 (AddRef)
    if (*out != 0)
        ((void (*)(void *))F3_VT(*out)[2])(*out);           // release old (dead)
    *out = mgr;
    ((void (*)(void *))F3_VT(mgr)[2])(mgr);                 // vtable slot 2 (Release)
    return out;
}

// sub_1803F80 (0x1803F80 - 0x180402F) -- grow the file-subsystem dir-handle
// vector and append the new handle (the std::vector grow path, the sub_18039B0
// insert underneath).  The vector at obj+0x4C is empty in this build (its begin
// / end / capacity-end words all image-initialize to 0), so the append always
// takes this grow path; it is only ever reached when the archive framework
// lands, so it is left as a safe no-op stub.
static void FileSubsysVecGrow(void *vec, void *out8, void *wrapper,
                              void *end, void **value)
{
    (void)vec; (void)out8; (void)wrapper; (void)end; (void)value;
}

// sub_18041D0 (0x18041D0 - 0x1804244) -- append a dir-handle reference to the
// file-subsystem's dir-handle vector at obj+0x4C (thiscall + retn 4).  begin
// (+0x0C) / end (+0x10) / capacity-end (+0x14): when the current element count
// (end - begin) >> 2 is within capacity, copy *value into the next slot and
// AddRef it through vtable slot 1, then advance end; otherwise take the grow
// path (sub_1803F80, a no-op stub in this build).  Shared by four file
// subsystem registration methods.
static void FileSubsysAppendDirNode(void *vec, void **value)
{
    DWORD *v = (DWORD *)vec;
    DWORD begin = v[0x0C / 4];
    DWORD capacity = (begin != 0) ? ((v[0x14 / 4] - begin) >> 2) : 0;
    DWORD end = v[0x10 / 4];
    if ((int)((end - begin) >> 2) >= (int)capacity)
        FileSubsysVecGrow(vec, 0, (void *)v[0], (void *)end, value);
    else
    {
        if (end != 0)
        {
            void *h = *value;
            *(void **)end = h;
            if (h != 0)
                ((void (*)(void *))F3_VT(h)[1])(h);        // vtable slot 1 (AddRef)
        }
        end += 4;
        v[0x10 / 4] = end;
    }
}

// sub_1801890 (0x1801890 - 0x1801990) -- register archiveHandle as the file
// subsystem's current directory (thiscall + retn 8; obj in ECX, the two handles
// on the stack).  a2 (the "data" archive handle) is 0 in this build, so the
// body is skipped (the original's `test ebx,ebx; jz` straight to the epilogue).
//  Faithful body: resolve a2's path through the +0x88 provider (vtable slot 10)
// freeing the descriptor's heap buffer when capacity >= 8, unregister the old
// dir (slot 11 on the provider), and -- when a3 is set -- re-init the descriptor
// to the "Source" wide string (sub_5AF290), register a3 (slot 10, flag 3),
// AddRef a3 and Release the old dir handle; store a3 at +0x84, build the
// combined dir node (sub_18013D0), append it to the +0x4C dir-handle vector
// (sub_18041D0), and Release the combined node.
//  [NOT byte-identical] placement-dependent (relative calls to LHFree /
//  sub_5AF290 / sub_18013D0 / sub_18041D0) and refs the high-VA "Source" wide
//  string (word_1BDFD50, 0x01BDFD50).
static void FileSubsysRegisterDir(void *obj, void *a2, void *a3)
{
    if (a2 == 0)
        return;
    void *provider = *(void **)((char *)obj + 0x88);
    F3AString desc;
    desc.d0 = 0; desc.u.buf[0] = 0; desc.len = 0; desc.cap = 7;
    ((void (__thiscall *)(void *, void *, F3AString *, int, int))
        F3_VT(provider)[10])(provider, a2, &desc, 2, 1);
    if (desc.cap >= 8)
        LHFree((void *)desc.u.ptr);
    void *oldDir = *(void **)((char *)obj + 0x84);
    if (oldDir != 0)
        ((void (__thiscall *)(void *, void *))F3_VT(provider)[11])(provider, oldDir);
    if (a3 != 0)
    {
        F3DescInitSource(&desc);                            // sub_5AF290
        ((void (__thiscall *)(void *, void *, F3AString *, int, int))
            F3_VT(provider)[10])(provider, a3, &desc, 3, 1);
        if (desc.cap >= 8)
            LHFree((void *)desc.u.ptr);
        ((void (*)(void *))F3_VT(a3)[1])(a3);               // vtable slot 1 (AddRef)
    }
    if (oldDir != 0)
        ((void (*)(void *))F3_VT(oldDir)[2])(oldDir);       // vtable slot 2 (Release)
    *(void **)((char *)obj + 0x84) = a3;
    void *node = a2;
    FileSubsysBuildDirNode(&node, a2, a3);                  // sub_18013D0
    FileSubsysAppendDirNode((char *)obj + 0x4C, &node);     // sub_18041D0
    if (node != 0)
        ((void (*)(void *))F3_VT(node)[2])(node);           // vtable slot 2 (Release)
}

// LegacyFileCtor (0x17FE870 - 0x17FE8A5): the LHSFS::CLegacyFile ctor over a
// 16-byte block (vtable + shared slot + two zeroed words). 'slot' (a2) is
// the caller's path-string shared slot (the address of the F3SharedBuffer
// * field), handed to the CLegacyFileOpen open-setup.
void *LegacyFileCtor(void *obj, F3SharedBuffer **slot, int a3, int a4)
{
    DWORD *b = (DWORD *)obj;
    b[0] = (DWORD)g_CLegacyFileVtable;
    InitSharedSlot((F3SharedBuffer **)&b[1]);
    b[2] = 0;
    b[3] = 0;
    CLegacyFileOpen(obj, slot, a3, a4);
    return obj;
}

// sub_17FDC30 (0x17FDC30 - 0x17FDC39): does the legacy file have data (the
// +8 file slot set, i.e. did the open succeed)?
int LegacyFileHasData(const void *obj)
{
    return *(const DWORD *)((const char *)obj + 8) != 0;
}

// sub_61D800: copy the legacy file's content into the shared slot.
static void CopyFileToSharedSlot(void *file, F3SharedBuffer **out)
{
    (void)file; (void)out;
}

// sub_17FDE60 / sub_17FDEC0: publish obj into the global slot
// (dword_1E2D7A4 / dword_1E2D7AC; refcounted, one-time atexit registration).
static int PublishDataObject1(void *obj) { (void)obj; return 0; }
static int PublishDataObject2(void *obj) { (void)obj; return 0; }

// sub_17FD600: resolve the slot's path ("data\\") into the global data path
// (dword_1E2D7C4) via DetachToLowerSharedSlot (sub_1A01BE0, implemented
// above in the CLegacyMemoryFile section).
static long ResolveDataPathSlot(F3SharedBuffer **slot) { (void)slot; return 0; }

// sub_17FD6A0: resolve the "data\\" shared slot into the object's wide path
// globals (dword_1E2D7BC).
static void LegacyFileSetDataPath(void *obj) { (void)obj; }

// sub_17FD970 (0x17FD970 - 0x17FD9A7): the LHSFS::CLegacyFile dtor (release
// +12 / +8, release the shared slot, reset the vtable to LHFile::CAFile).
// Returns the slot release's new g_Addend count.
long LegacyFileDtor(void *obj)
{
    DWORD *b = (DWORD *)obj;
    if (b[3] != 0 && F3_VT((void *)b[3]) != 0)
        ((void (*)(void *))F3_VT((void *)b[3])[2])((void *)b[3]);
    if (b[2] != 0 && F3_VT((void *)b[2]) != 0)
        ((void (*)(void *))F3_VT((void *)b[2])[2])((void *)b[2]);
    long r = ReleaseSharedSlot((F3SharedBuffer **)&b[1]);
    b[0] = (DWORD)g_CAFileVtable;
    return r;
}

// sub_61AFA0: does data/streaming.bnk exist in the "data" archive? (Builds
// the "streaming.bnk" + "data" path strings, resolves the path, and queries
// the handle's vtable slot 4.)
static int DataStreamingBnkExists(void)
{
    F3WString bnk, dir;
    F3WStringFromAnsi(&bnk, "streaming.bnk");           // sub_4358A0
    F3WStringReset(&dir);
    F3WStringAssign(&dir, L"data", 4);
    F3WStringNormalize(&dir);
    void *handle = 0;
    ResolveArchivePath(&handle, &dir, 0, 0);
    F3WStringClear(&dir);
    int exists = 0;
    void *result = 0;
    if (handle != 0 && F3_VT(handle) != 0)
        ((void (*)(void *, void *, int *, int))F3_VT(handle)[4])(handle, &result, &exists, 1);
    if (handle != 0 && F3_VT(handle) != 0)
        ((void (*)(void *))F3_VT(handle)[2])(handle);
    int r = (result != 0);
    if (result != 0)
        ((void (*)(void *))F3_VT(result)[2])(result);
    F3WStringClear(&bnk);
    return r;
}

// ============================================================
//  InitFrontendData  (0x61B090 - 0x61B66E)  -- the front-end data / streaming
//  init (sole caller: RunFrontEnd, 0x61D1BA)
//  [NOT byte-identical] placement-dependent (relative calls into the shared-
//  slot / path / archive / file-subsystem framework and the Win32 imports),
//  a usercall register-parameter hybrid, and it refs the high-VA globals
//  g_byte_1DBD02A (0x01DBD02A) / g_unk_1DBDA78 (0x01DBDA78) and the string
//  literals at their original rdata addresses.
//
//  long InitFrontendData(void *obj, int regB, int regP, int regD, int regS)
//
//  A usercall hybrid in the original: obj in ECX, and the caller's EBX, EBP,
//  EDI, ESI registers thread straight through into the streaming manager's
//  AddArchive dispatch (the four caller registers reach the method
//  unmodified). The sole caller (RunFrontEnd, 0x61D1BA) just lets its live
//  register values flow through: EDI = the sign-in state, ESI = obj, EBX =
//  the caller's Sleep IAT slot, EBP = its frame pointer.
//
//  It brings up the front-end's file + streaming subsystem after XNA sign-in:
//  - sets the process CWD to the exe's directory (when it is one).
//  - g_byte_1DBD02A (the run-from-disk flag) = 1 when data/streaming.bnk
//    exists or the command line has "FakeRunFromDVD".
//  - installs the fatal (no-op) / log error handlers and reserves the
//    128 KB data region.
//  - resolves the "data" directory (re-resolving it through dir.manifest
//    when streaming.bnk is present but not fakeDVD, and skipping the
//    archive registration when streaming.bnk is absent and not fakeDVD),
//    creates the 84-byte streaming manager, and registers the archive with
//    the four caller registers.
//  - registers the "language" data set, the two Bink banks (levels.bnk /
//    streaming.bnk) and the "language\\en-uk\\text\\book.babel" text book.
//  - hands the archive handle to the global file subsystem, builds the
//    "build_version.txt" legacy file (its content copied into the
//    g_unk_1DBDA78 slot when the data file has content), and publishes the
//    "data\\" path globals.
//
//  The deep archive / streaming / file-subsystem callees are remaining
//  stubs; the return value is the final ReleaseSharedSlot count (ignored by
//  RunFrontEnd).
// ============================================================
static long InitFrontendData(void *obj, int regB, int regP, int regD, int regS)
{
    (void)obj;

    // ---- the original's 0x74-byte stack frame (selected slots) ----
    F3SharedBuffer *exeDir   = 0;          // the exe-directory string slot
    F3SharedBuffer *cwdSlot  = 0;          // the current-directory slot
    F3SharedBuffer *archive  = 0;          // the archive manager slot
    F3WString       dataPath = { 0 };      // the 36-byte "data" path slot
    F3WString       langSet  = { 0 };      // the 36-byte "language" name slot
    F3WString       bookPath = { 0 };      // the 36-byte bnk / book path slot
    F3WString       langPath = { 0 };      // the 36-byte "data\\language" path slot
    F3SharedBuffer *dataHandle = 0;        // the resolved data dir / archive handle
    F3SharedBuffer *tmpHandle  = 0;        // the re-resolve out slot
    void           *streamMgr  = 0;        // the 84-byte streaming manager
    DWORD           fileBlock[4] = { 0 };  // the 16-byte data-file object (the build_version file)
    F3SharedBuffer *fileSlot  = 0;        // the temporary path-string shared slot (v40)

    // 1. SetCurrentDirectoryA to the exe's directory (when it is one).
    SetExeDirSlot(&exeDir);
    if (ExeDirSlotIsDirectory(exeDir) != 0)
    {
        const char *dir = (exeDir != 0 && exeDir->data != 0) ? exeDir->data : "";
        SetCurrentDirectoryA(dir);
    }

    // 2. The run-from-disk flag: data/streaming.bnk present, or
    //    "FakeRunFromDVD" on the command line.
    int bnkExists = DataStreamingBnkExists();
    int fakeDVD   = (strstr(GetCommandLineA(), "FakeRunFromDVD") != 0);
    if (bnkExists != 0 || fakeDVD)
        g_byte_1DBD02A = 1;

    // 3. Error handlers + the data region + the global slots.
    GetCurrentDirSlot(&cwdSlot);
    RegisterFrontendErrorHandler(cwdSlot, 1, FrontendFatalErrorNoop);
    RegisterFrontendErrorHandler(cwdSlot, 0, FrontendLogError);
    ReserveDataRegion(0x20000);
    GetArchiveSlot(&archive);

    // 4. Resolve the "data" directory.
    F3WStringReset(&dataPath);
    F3WStringAssign(&dataPath, L"data", 4);
    F3WStringNormalize(&dataPath);
    dataHandle = (F3SharedBuffer *)ResolveArchivePath(&dataHandle, &dataPath, 0, 0);
    F3WStringClear(&dataPath);

    if (bnkExists == 0)
    {
        if (!fakeDVD)
            goto dataSubsys;      // LABEL_63: skip the archive registration
        goto registerData;        // LABEL_15
    }
    if (fakeDVD)
        goto registerData;        // LABEL_15

    // streaming.bnk present, not fakeDVD: resolve through dir.manifest.
    {
        F3WStringClear(&langSet);
        F3WStringFromAnsi(&langSet, "dir.manifest");    // sub_4358A0
        tmpHandle = (F3SharedBuffer *)ResolveDirManifest(&tmpHandle, dataHandle, &langSet);
        if (tmpHandle != 0 && F3_VT(tmpHandle) != 0)
            ((void (*)(void *))F3_VT(tmpHandle)[1])(tmpHandle);
        if (dataHandle != 0 && F3_VT(dataHandle) != 0)
            ((void (*)(void *))F3_VT(dataHandle)[2])(dataHandle);
        dataHandle = tmpHandle;
        tmpHandle  = 0;
        F3WStringClear(&langSet);
    }
    goto afterArchive;

registerData:
    // LABEL_15: re-resolve the "data" archive and move the handle in.
    {
        F3WStringReset(&dataPath);
        F3WStringAssign(&dataPath, L"data", 4);
        F3WStringNormalize(&dataPath);
        tmpHandle = (F3SharedBuffer *)ResolveArchivePath(&tmpHandle, &dataPath, 0, 0);
        if (tmpHandle != 0 && F3_VT(tmpHandle) != 0)
            ((void (*)(void *))F3_VT(tmpHandle)[1])(tmpHandle);
        if (dataHandle != 0 && F3_VT(dataHandle) != 0)
            ((void (*)(void *))F3_VT(dataHandle)[2])(dataHandle);
        dataHandle = tmpHandle;
        tmpHandle  = 0;
        F3WStringClear(&dataPath);
    }

afterArchive:
    // 5. The streaming manager + the archive registration (the four caller
    //    registers thread through as the method's trailing args).
    CreateStreamingManager(&streamMgr);
    StreamingMgrAddArchive(streamMgr, dataHandle, &dataPath, 2, 1,
                           regD, regS, regB, regP);
    F3WStringClear(&bookPath);

    // 6. The "language" name + the "data\\language" set registration.
    F3WStringAssignAnsi(&langSet, "language");
    F3WStringNormalize(&langSet);
    F3WStringReset(&langPath);
    F3WStringAssign(&langPath, L"data\\language", 13);
    F3WStringNormalize(&langPath);
    tmpHandle = (F3SharedBuffer *)ResolveArchivePath(&tmpHandle, &langPath, 0, 0);
    DataFileRegisterEntry(fileBlock, (tmpHandle != 0) ? (void *)tmpHandle->data : 0,
                          &langSet.u, 2, 1);
    if (tmpHandle != 0 && F3_VT(tmpHandle) != 0)
        ((void (*)(void *))F3_VT(tmpHandle)[2])(tmpHandle);
    F3WStringClear(&langSet);

    // 7. The two Bink banks (levels.bnk, then streaming.bnk).
    F3WStringAssign(&bookPath, L"levels.bnk", 10);
    tmpHandle = (F3SharedBuffer *)LookupBnk(&tmpHandle, cwdSlot, &bookPath, 0);
    DataFileRegisterEntry(fileBlock, (tmpHandle != 0) ? (void *)tmpHandle->data : 0,
                          &bookPath, 2, 1);
    if (tmpHandle != 0 && F3_VT(tmpHandle) != 0)
        ((void (*)(void *))F3_VT(tmpHandle)[2])(tmpHandle);
    F3WStringClear(&bookPath);

    F3WStringAssign(&bookPath, L"streaming.bnk", 13);
    tmpHandle = (F3SharedBuffer *)LookupBnk(&tmpHandle, cwdSlot, &bookPath, 0);
    DataFileRegisterEntry(fileBlock, (tmpHandle != 0) ? (void *)tmpHandle->data : 0,
                          &bookPath, 2, 0);
    if (tmpHandle != 0 && F3_VT(tmpHandle) != 0)
        ((void (*)(void *))F3_VT(tmpHandle)[2])(tmpHandle);
    F3WStringClear(&bookPath);

    // 8. The babel text book + the manager flag byte (the original writes
    //    past the 84-byte allocation at manager+0x161, and clears it again
    //    when the book string's ptr field reads 0).
    F3WStringFromAnsi(&bookPath, "language\\en-uk\\text\\book.babel");  // sub_4358A0
    if (streamMgr != 0)
        ((char *)streamMgr)[0x161] = 1;
    ArchiveMgrAddPath(archive, &cwdSlot, &bookPath, &langSet.u, 1);
    int bookPtrWasSet = (bookPath.u.ptr != 0);
    F3WStringClear(&bookPath);
    if (streamMgr != 0 && !bookPtrWasSet)
        ((char *)streamMgr)[0x161] = 0;

    // The manager / handle releases (the original's order).
    if (streamMgr != 0 && F3_VT(streamMgr) != 0)
        ((void (*)(void *))F3_VT(streamMgr)[1])(streamMgr);
    if (dataHandle != 0 && F3_VT(dataHandle) != 0)
        ((void (*)(void *))F3_VT(dataHandle)[2])(dataHandle);
    if (fileBlock[0] != 0 && F3_VT((void *)fileBlock[0]) != 0)
        ((void (*)(void *))F3_VT((void *)fileBlock[0])[2])((void *)fileBlock[0]);
    if (streamMgr != 0 && F3_VT(streamMgr) != 0)
        ((void (*)(void *))F3_VT(streamMgr)[2])(streamMgr);

dataSubsys:
    // 9. The file subsystem + the build_version.txt legacy file + the
    //    "data\\" path globals.
    {
        void *fs = GetFileSubsystem(dataHandle);          // sub_1801C20
        FileSubsysRegisterDir(fs, dataHandle, 0);         // sub_1801890
    }

    // The build_version.txt file object: the caller's temporary
    // shared slot (fileSlot) holds the path string, the 16-byte
    // CLegacyFile (fileBlock) is constructed separately, and the slot is
    // released afterwards.
    SetSharedSlot(&fileSlot, "build_version.txt", -1);
    LegacyFileCtor(fileBlock, &fileSlot, 1, 2);           // LegacyFileCtor
    ReleaseSharedSlot(&fileSlot);
    if (LegacyFileHasData(fileBlock) != 0)                // sub_17FDC30(v45)
        CopyFileToSharedSlot(fileBlock, &g_unk_1DBDA78);  // sub_61D800(&unk_1DBDA78)
    PublishDataObject1(archive);                          // sub_17FDE60(v44)

    SetSharedSlot(&fileSlot, "data\\", -1);
    ResolveDataPathSlot(&fileSlot);                       // sub_17FD600
    ReleaseSharedSlot(&fileSlot);
    PublishDataObject2(archive);                          // sub_17FDEC0(v44)

    SetSharedSlot(&fileSlot, "data\\", -1);
    LegacyFileSetDataPath(fileSlot);                      // sub_17FD6A0
    ReleaseSharedSlot(&fileSlot);
    LegacyFileDtor(fileBlock);                            // sub_17FD970(v45)

    // 10. Final releases (the original's order).
    if (dataHandle != 0 && F3_VT(dataHandle) != 0)
        ((void (*)(void *))F3_VT(dataHandle)[2])(dataHandle);
    if (archive != 0 && F3_VT(archive) != 0)
        ((void (*)(void *))F3_VT(archive)[2])(archive);
    if (cwdSlot != 0 && F3_VT(cwdSlot) != 0)
        ((void (*)(void *))F3_VT(cwdSlot)[2])(cwdSlot);
    return ReleaseSharedSlot(&exeDir);
}

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
    char            v108[100];              // LoadVideoOptions object (the 96-byte option-map triple)
    DWORD           v70[5];                 // the video-options file-path shared slot (MakeVideoOptionsPath)
    DWORD           str103[13];             // the 52-byte saved video-options record (v103..v106)
    DWORD           v107 = 0;               // the separate command-line word
    DWORD           v84[2];                 // GetDefaultResolution slot ({width, height})
    F3SharedBuffer *v88 = 0;                // MakeAbnormalShutdownPath / SharedFileExists slot
    DWORD           v93[7];                 // the CDiskFileWin32 / CAFile object (28 bytes)
    F3SharedBuffer *v96;                    // InitSharedSlot / ReleaseSharedSlot slot
    DWORD           notify[2];              // the 8-byte XNotify buffer
    F3SharedBuffer *v76 = 0;               // SharedSlotToWide wide-string slot
    DWORD           v85, v86;               // GetCurrentResolution slots ({width, height})
    DWORD           v91, v92, v89, v90;     // sign-in UI strings
    DWORD           v102[2];
    DWORD           v97[2];
    int             v98, v99, v100, v101;
    int             v72;                    // var_16D (game ran)
    int             v71;                    // var_16E (sign-in wait flag)
    int             v164;                   // sub_1801C20 result
    void           *v168 = 0;              // FinalizeModelHeaders out-holder
    F3WString       vfsCfg;                 // the 36-byte "startup.vfsconfig" string (sub_4358A0)
    int             vfs[4];                 // the 16-byte vfs config word block
    F3AString       modelHdr;               // the 28-byte "globals_model_headers" string (F3AStringAssign / sub_402F00)
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
    LoadVideoOptions(v108);   // 0x775EE0: build the video-options maps
    MakeVideoOptionsPath((F3SharedBuffer **)v70);   // 0x72F7B0: settings dir + "VideoOptions.xml"
    LoadVideoOptionsValues(str103, (F3SharedBuffer **)v70);

    // 5. Parse the command-line / locale words.
    DWORD *parsed = GetDefaultResolution(v84);    // 0x61A000: default resolution from the desktop aspect ratio
    g_dword_1C88DE4 = (DWORD)parsed[0];
    g_dword_1C88DE8 = (DWORD)parsed[1];
    if (((v107 >> 8) & 0xFF) == 0)   // BYTE1(v107)
    {
        str103[1] = (DWORD)parsed[0];
        str103[2] = (DWORD)parsed[1];
    }

    // 6. Build the file object + shared slot; show an error box on failure.
    MakeAbnormalShutdownPath(&v88);   // 0x67C930: settings dir + "abnormal_shutdown.tmp"
    v93[0] = (DWORD)g_CDiskFileWin32Vtable;
    memset(&v93[1], 0, 24);
    InitSharedSlot(&v96);
    int v88Exists = SharedFileExists(&v88);
    if (v88Exists != 0)
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
        CDiskFileWin32Open((CDiskFileWin32 *)v93, &v88, 4, 2);
        CDiskFileWin32Close((CDiskFileWin32 *)v93);
    }

    v72 = 0;

    // 7. Dispatch the manager "run the game" vtable slot.
    {
        void *mgr  = GetManager();
        void **vt  = (void **)*(void ***)mgr;
        int   v13  = (int)(DWORD)SharedSlotToWide(&g_slot_1DBD048, &v76);
        int   r    =
            ((int (*)(void *, int, int, int, int, int, int, int, int))vt[1])(
                mgr, (int)v107, (int)str103[1], (int)str103[2], (int)str103[0], arg,
                (int)g_dword_1C86BF0, (int)g_dword_1C86C08, v13);
        ReleaseSharedSlotWide(&v76);
        if (r != 0)
        {
            DWORD *p = GetCurrentResolution(&v85);
            if ((unsigned char)str103[3] != (unsigned char)IsFullScreen()
                || (int)str103[1] != p[0]
                || (int)str103[2] != p[1])
            {
                str103[3] = (DWORD)IsFullScreen();
                DWORD *p2 = GetCurrentResolution(&v86);
                str103[1] = (DWORD)p2[0];
                str103[2] = (DWORD)p2[1];
                base[0x160] = 1;
                MakeVideoOptionsPath((F3SharedBuffer **)v70);   // 0x72F7B0
                SaveVideoOptionsFile((F3VideoOptions *)str103, (F3SharedBuffer **)v70);   // 0x775DA0
            }
            ResetGraphicsSurfaces();
            v72 = 1;
        }
    }

    // 8. Start the pump thread, set the cursor, and handle XNA sign-in.
    g_byte_1DBD9E0 = 1;
    _beginthread(RunGameLoop, 0, 0);
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
            // Copy the four localized sign-in UI strings into their wide slots
            // (the wide shared-slot copy ctor, 0x19FFA20; thiscall + retn 4 in
            // the original -- destination slot in ECX, the locale entry on the
            // stack). Skipped in the standalone test (the user is signed in).
            CopySharedSlotWide((F3SharedBuffer **)&v91, (F3SharedBuffer **)GetLocaleEntry(g_dword_1DBDA80));
            CopySharedSlotWide((F3SharedBuffer **)&v92, (F3SharedBuffer **)GetLocaleEntry(g_dword_1DBDA84));
            CopySharedSlotWide((F3SharedBuffer **)&v89, (F3SharedBuffer **)GetLocaleEntry(g_dword_1DBDA88));
            CopySharedSlotWide((F3SharedBuffer **)&v90, (F3SharedBuffer **)GetLocaleEntry(g_dword_1DBDA7C));
            v102[0] = (DWORD)GetSharedSlotWide((F3SharedBuffer **)&v91);
            v102[1] = (DWORD)GetSharedSlotWide((F3SharedBuffer **)&v92);
            v98 = v99 = v100 = v101 = 0;
            v97[0] = v97[1] = 0;

            int code = 5;
            do
            {
                v70[0] = (DWORD)&v98;
                int a = (DWORD)GetSharedSlotWide((F3SharedBuffer **)&v89);
                int b = (DWORD)GetSharedSlotWide((F3SharedBuffer **)&v90);
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
            ReleaseSharedSlotWide((F3SharedBuffer **)&v91);
            ReleaseSharedSlotWide((F3SharedBuffer **)&v89);
            ReleaseSharedSlotWide((F3SharedBuffer **)&v90);
            ReleaseSharedSlotWide((F3SharedBuffer **)&v92);

            if (signState != 0)
                goto signedIn;   // LABEL_46: the user signed in mid-loop
        }

        // Cancelled: tear down the file object, post WM_CLOSE, return 0.
        (void)cancelled;
        g_byte_1DBD9E0 = 0;
        WaitForEvent(&g_unk_1DBD9E8, -1, 0);
        {
            void *mgr = GetManager();
            void **vt = (void **)*(void ***)mgr;
            HWND hwnd = (HWND)((int (*)(void *))vt[8])(mgr);
            PostMessageA(hwnd, 0x10, 0, 0);   // WM_CLOSE
        }
        ReleaseSharedSlotWide((F3SharedBuffer **)&v91);
        ReleaseSharedSlotWide((F3SharedBuffer **)&v89);
        ReleaseSharedSlotWide((F3SharedBuffer **)&v90);
        ReleaseSharedSlotWide((F3SharedBuffer **)&v92);
        v93[0] = (DWORD)g_CDiskFileWin32Vtable;
        CDiskFileWin32Close((CDiskFileWin32 *)v93);
        goto epilogue;
    }

signedIn:
// 10. The signed-in continuation: front-end data + device construction.
    {
        // 0x61d1b3: CNetUserManagerRefreshSignin(CNetManagerGetUserManager(
        // GetNetManager())) — re-read local user 0's sign-in state after the
        // local sign-in and refresh the CNetUserManager's (+0x28) record 0.
        // CNetManagerGetUserManager is a byte-identical naked thiscall that
        // reads the CNetManager's +0x28 slot from ECX; a C expression cannot
        // stage that register, so read the slot directly (identical result).
        { 
          void *mgr = GetNetManager();
          void *um = (mgr != 0) ? (void *)*(DWORD *)((char *)mgr + 0x28) : 0;
          CNetUserManagerRefreshSignin(um); 
        }
        // 0x61d1ba: the front-end data / streaming init.  A usercall hybrid
        // in the original: ECX = obj, and the caller's live EBX / EBP / EDI /
        // ESI thread into the streaming manager's AddArchive dispatch (EDI
        // = the sign-in state, ESI = obj; EBX / EBP hold the caller's Sleep
        // IAT slot / frame pointer -- 0 here).
        InitFrontendData(obj, 0, 0, XUserGetSigninState(0), (int)(DWORD)obj);
        CreateHeapManager(g_byte_1DBD030 != 0);
        nullsub_7();
        RegisterNetStateFactoryRegs();

        const char *cmd = GetCommandLineA();
        g_byte_1DBD034 = (strstr(cmd, "WaitBeforeEnteringFrontEnd") != 0);
        base[0x24] = 0;
        CGameEngineClearNodeList(obj);

        // Copy the node-list pointers (heapNode -> +0x1C, listFirst -> +0x20).
        int heapNode  = (int)dw[0];
        int listFirst = (int)dw[0x14 / 4];
        dw[0x1C / 4] = (DWORD)heapNode;
        dw[0x20 / 4] = (DWORD)listFirst;

        void *p56 = LHAlloc(0x38);
        int   v35 = (p56 != 0) ? (int)CSortedMapCtor(p56) : 0;
        void *v36 = (void *)dw[0x98 / 4];
        if (v36 != 0)
        {
            CSortedMapDtor(v36);
            LHFree(v36);
        }
        dw[0x98 / 4] = (DWORD)v35;

        InitLipCultures();
        v164 = (int)GetFileSubsystem(0);             // sub_1801C20 (obj unused)
        F3WStringFromAnsi(&vfsCfg, "startup.vfsconfig");   // sub_4358A0
        memset(vfs, 0, sizeof(vfs));
        FileSubsysLoadVfsConfig((void *)v164, &vfsCfg);   // sub_1801580
        if (vfs[3] >= 8)
            LHFree((void *)vfs[0]);

        vfs[0] = vfs[1] = vfs[2] = 0;
        vfs[3] = 7;
        void *p48 = LHAlloc(0x30);
        int   v39 = (p48 != 0) ? (int)CPackageManagerCtor(p48) : 0;

        void *v40 = (void *)dw[0xAC / 4];
        if (v40 != 0)
            ((void (*)(void *, int))F3_VT(v40)[0])(v40, 1);
        dw[0xAC / 4] = (DWORD)v39;

        void *p288 = LHAlloc(0x120);
        int   v37 = (p288 != 0) ? (int)CVersionTableCtor(p288, (void *)dw[0xAC / 4]) : 0;

        void *v41 = (void *)dw[0xB0 / 4];
        if (v41 != 0)
        {
            CVersionTableDtor(v41);
            LHFree(v41);
        }
        dw[0xB0 / 4] = (DWORD)v37;

        CPackageManagerLoadCollections((void *)dw[0xAC / 4]);
        // 0x61D3B6: CGameVersionCopyFrom(base + 0xEC, CVersionTableGetVersion1(
        // engine+0xB0)) -- copies CV1 into the engine's +0xEC slot. CVersion
        // TableGetVersion1 is a byte-identical naked thiscall that reads the
        // table's +0x04 slot from ECX; a C expression cannot stage that
        // register, so read the slot directly (identical result).
        CGameVersionCopyFrom(base + 0xEC, (char *)dw[0xB0 / 4] + 4);
        ReinitEngineLua(obj, 0);

        void *p736 = LHAlloc(0x2E0);
        int   v44 = (p736 != 0) ? (int)CNetSaveGameCtor(p736) : 0;
        void *v45 = (void *)dw[0xA8 / 4];
        if (v45 != 0)
        {
            CNetSaveGameDtor(v45);   // 0x627690
            LHFree(v45);
        }
        dw[0xA8 / 4] = (DWORD)v44;

        g_off_1D8C850 = (void *)JmpLHAllocAligned;   // 0x619990 (the real aligned-alloc thunk)
        g_off_1D8C854 = (void *)JmpLHFreeAligned;    // 0x6199A0 (the real aligned-free thunk)
        LoadAudioMasteringProfiles();

        g_byte_1DBD9E0 = 0;
        WaitForEvent(&g_unk_1DBD9E8, -1, 0);
    }

    if (v72 == 0)
        goto epilogue;

    // 11. The device-created / worker-callback / thread-start block.
    {
        void *mgr = GetManager();
        void **vt = (void **)*(void ***)mgr;
        ((void (*)(void *, int))vt[10])(mgr, (unsigned char)g_byte_1DBD01F);

        HWND hwnd = (HWND)((int (*)(void *))vt[8])(GetManager());
        // Original: `push hwnd` once at 0x61D46C, then GetHeapManager
        // (sub_18FA3E0) — which takes no argument; the push is the pending
        // stdcall argument the decompiler mis-attributes to it — and
        // GetHeapManagerSlot1C (sub_18FA3D0, thiscall); SetSoundHWND
        // (sub_145D7B0) finally consumes the pushed hwnd, storing it into
        // dword_1DDC934 (the DirectSound SetCooperativeLevel window handle).
        // The stored value / return value are unused at this call site.
        void *hm = GetHeapManager();
        GetHeapManagerSlot1C(hm);
        SetSoundHWND((int)hwnd);
        // 0x61D487-0x61D48E: GetNetManager() -> CNetManagerGetVoiceManager
        // (the +0x30 slot) -> CNetVoiceManagerInit (sub_741E50, the CNetVoice-
        // Manager XNotify/XHV voice bring-up; implemented in netinit.cpp).
        CNetVoiceManagerInit(CNetManagerGetVoiceManager(GetNetManager()));

        ((F3ThiscallSetFn)SetPauseGameOnVMState)(GetWorker(), (F3CallbackFn)PauseGameOnVMState);   // 0x61D493
        ((F3ThiscallSetFn)SetMultiplayerPauseOnVMState)(GetWorker(), (F3CallbackFn)MultiplayerPauseOnVMState);   // 0x61D4B0
        ((F3ThiscallSetFn)SetArmedPauseOnVMState)(GetWorker(), (F3CallbackFn)ArmedPauseOnVMState);   // 0x61D4C1
        ((F3ThiscallSetFn)SetNetHeldCoopFailed)(GetWorker(), (F3CallbackFn)NetHeldCoopFailed);   // 0x61D4D0

        g_byte_2075F07 = 0;
        F3AStringReset(&modelHdr);                  // the default SSO ctor (cap = 15, len = 0, buf[0] = 0)
        F3AStringAssign(&modelHdr, "globals_model_headers", 0x15);   // sub_402F00
        void *v52 = FinalizeModelHeaders((void *)v164, &modelHdr, &v168);
        void *v52obj = *(void **)v52;
        if (v52obj != 0)
            ((void (*)(void *))F3_VT(v52obj)[1])(v52obj);
        void *v53 = (void *)dw[0x7C / 4];
        if (v53 != 0)
            ((void (*)(void *))F3_VT(v53)[2])(v53);
        dw[0x7C / 4] = (DWORD)v52obj;
        if (v168 != 0)
            ((void (*)(void *))F3_VT(v168)[2])(v168);
        if (modelHdr.cap >= 0x10)
            LHFree((void *)modelHdr.u.ptr);

        SetEngineService();
        void *e = GetEngineService();
        ForwardEngineServiceImpl(e);
        SetEngineStartedFlag();   // 0x61D56D
        StartTextureStreamer();

        void *p88 = LHAlloc(0x58);
        int   v55 = (p88 != 0) ? (int)CLoadingScreenCtor(p88) : 0;
        void *v56 = (void *)dw[0x28 / 4];
        if (v56 != 0)
            ((void (*)(void *, int))F3_VT(v56)[0])(v56, 1);
        dw[0x28 / 4] = (DWORD)v55;

        void *p8 = LHAlloc(8);
        int   v58 = (p8 != 0) ? (int)CGameRendererCtor(p8) : 0;
        void *v59 = (void *)dw[0x9C / 4];
        if (v59 != 0)
            ((void (*)(void *, int))F3_VT(v59)[0])(v59, 1);
        dw[0x9C / 4] = (DWORD)v58;
        CGameRendererApplySavedOptions((void *)v58, obj, v108, str103);

        ResetEngineTest((int)dw[0xBC / 4]);
        RegisterEngineTest((int)dw[0xBC / 4]);
        ClearCurrentThreadId();
        StartWorkerThread((void *)dw[0x9C / 4]);

        F3SharedBuffer **fileId = SetSharedSlot(&v160slot, "data\\art\\videos\\microsoft_logo.bik", -1);
        MountVideoFile(obj, fileId);
        ReleaseSharedSlot(&v160slot);
        InitParticleSystem(obj);

        base[0x84] = 0;
        dw[0x80 / 4] = (DWORD)_beginthread(MountPermanentBanks, 0x40000u, obj);

        void *p8b = LHAlloc(8);
        int   v62 = (p8b != 0) ? (int)CFullScreenMoviePlayerCtor(p8b) : 0;
        void *v63 = (void *)dw[0x2C / 4];
        if (v63 != 0)
            ((void (*)(void *, int))F3_VT(v63)[0])(v63, 1);
        dw[0x2C / 4] = (DWORD)v62;

        void *p8c = LHAlloc(8);
        int   v64 = (p8c != 0) ? (int)GameImplWrapperCtor(p8c) : 0;
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
        LoadApplyMasteringProfile();
    }

    // Success: the frontend ran and the device / threads were started.
    result = 1;

epilogue:
    // Shared cleanup epilogue (identical on the success and cancel / failure
    // paths; only the return value differs).
    v93[0] = (DWORD)g_CDiskFileWin32Vtable;
    CDiskFileWin32Close((CDiskFileWin32 *)v93);
    ReleaseSharedSlot(&v96);
    v93[0] = (DWORD)g_CAFileVtable;
    ReleaseSharedSlot(&v88);
    DestroyOptionMaps(v108);   // 0x40E1F0: destroy the video-options maps
    return result;
}
