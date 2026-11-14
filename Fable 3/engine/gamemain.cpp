// Fable 3 — frontend run module implementation (RunFrontEnd, 0x61CB70, plus its
// module-private globals, the steam_api dynamic import (the xlive.dll
// functions are real link-time imports via engine/xlive.h), and the
// unimplemented engine callees it drives).

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // original used strstr / the CRT directly
#endif

#include <stdio.h>
#include "gamemain.h"
#include "common/common.h"        // LHAlloc / LHFree / InitSharedSlot / ReleaseSharedSlot
#include "gameengine.h"           // GetGameEngine (sub_619A90)
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
#include <wctype.h>               // iswspace (the XLipCultures line trim)
#include <mbstring.h>             // _mbscmp
#include <math.h>                 // fabsf
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
static BYTE  g_byte_2075F4E = 1;   // 0x2075F4E: path-normalize enable flag
// Function-pointer slots published by the frontend (0x1D8C850 / 0x1D8C854).
static void *g_off_1D8C850 = 0;    // 0x1D8C850
static void *g_off_1D8C854 = 0;    // 0x1D8C854
// Global "this" objects the frontend dispatches through (0x1DBD048 / 0x1DBD9E8).
// The callees (stubs) never dereference them, so a zeroed block stands in.
static int   g_unk_1DBD048[8] = { 0 };   // 0x1DBD048
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
static void AppendSharedSlot(F3SharedBuffer **slot, const char *src);
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

// The 24-byte option-map tree node (the red-black tree backing each map).
typedef struct F3OptionNode
{
    F3OptionNode *  link0;    // +0x00
    F3OptionNode *  link1;    // +0x04
    F3OptionNode *  link2;    // +0x08
    F3SharedBuffer *key;      // +0x0C: the 16-byte key block (a ref)
    DWORD           keyLen;   // +0x10
    BYTE            color;    // +0x14 (byte 20)
    BYTE            leaf;     // +0x15 (byte 21)
    BYTE            pad22;    // +0x16
    BYTE            pad23;    // +0x17
} F3OptionNode;               // 24 bytes

// The 32-byte option-map object; three sit side by side in the 96-byte
// video-options triple (Int at +0x00, Float at +0x20, Detail at +0x40).
typedef struct F3OptionMap
{
    DWORD *       wrapper;    // +0x00: the 4-byte heap block { &obj }
    DWORD         pad[5];     // +0x04..+0x17: untouched by the ctor / copy
    F3OptionNode *root;       // +0x18: the sentinel node
    DWORD         count;      // +0x1C: the element count
} F3OptionMap;                // 32 bytes

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
static void InitDetailOptionMap(F3OptionMap *obj, void *a2, void *a3) { InitOptionMapBase(obj, a2, a3); }   // sub_40E830

// sub_1A012C0 (0x1A012C0 - 0x1A0131C): take a ref of the source slot's block
static F3SharedBuffer **RefSharedSlot(F3SharedBuffer **dst, F3SharedBuffer **src)
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
//  builders.
// ============================================================
static F3SharedBuffer *g_dword_1DBE2F0 = 0;   // 0x1DBE2F0: settings-directory path slot

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
//  (InitSharedSlot / RefSharedSlot own it).
static void AppendSharedSlot(F3SharedBuffer **slot, const char *src)
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
//  [NOT byte-identical] placement-dependent (relative calls to RefSharedSlot /
//  AppendSharedSlot).
//
//  Ref the base slot into dst (RefSharedSlot, 0x1A012C0), then append the
//  literal filename (AppendSharedSlot, 0x1A01880), so dst = base + name as a
//  shared string. Returns dst. dst / base are the addresses of the slot words
//  (typed F3SharedBuffer **); the original passes them as plain int*.
static F3SharedBuffer **MakeSettingsFilePath(F3SharedBuffer **dst, F3SharedBuffer **base,
                                             const char *name)
{
    RefSharedSlot(dst, base);
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
    RefSharedSlot(&node->key, &src->key);   // sub_1A012C0(node+12, src+12)
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
static const char *GetSlotString(F3SharedBuffer **slot)
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
    void (*nextSibling)(void *stream);                      // +0x0C
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
    // [remaining stub] the tree key lookup / insert (sub_4122E0 / sub_403F10 /
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
static DWORD g_qword_20FC928[2] = { 0, 0 };

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
// Defined separately (static) in worker.cpp / xlo_device.cpp.
static void *g_dword_1E2EEF4  = 0;
// 0x2075F05: surface-creation flag (nonzero -> 0x200 create flag + format 0).
static BYTE  g_byte_2075F05   = 0;
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
static DWORD g_dword_1D9AF60[0x200 * 28] = {
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
static DWORD QueryCsurfaceFormat(int *fmt, void *surface)
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
// (0x18D04D0, the dirty registration) is implemented below, and sub_18D02E0
// is the tail-called sync step whose one unconditional side effect (clearing
// the dirty-registration counter) is modelled.
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
static int sub_18D02E0(void) { g_dword_2073938 = 0; return 0; }  // sync/commit the entities to the device

// ============================================================
//  ResetGraphicsEntities  (0x18D1960 - 0x18D19AE)  -- reset and rebuild the
//  frontend graphics-entity pool and rebind it to the device.
//  [NOT byte-identical] placement-dependent (relative calls to the
//  graphics-entity subsystem callees BuildGraphicsEntityPool /
//  RegisterGraphicsEntities / sub_18D02E0
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
//    8. return sub_18D02E0();  -- sync/commit the entities to the device.
//
//  Called by ResetGraphicsSurfaces (0x18E6D40) with the current device object
//  (dword_1E2EEF4) as the argument.  The return value is forwarded from the
//  tail-called sync step (sub_18D02E0) and ignored by the caller.  Replaces
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
    return sub_18D02E0();
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
static int   g_dword_1E2ED7C = 0;           // 0x1E2ED7C  frame-nesting counter
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
static void SetFrontendEvent(void *obj);

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
static void  sub_61C320(void *p) { (void)p; }                     // _beginthread entry (no-op)
// sub_6A66C0 is the canonical GetNetManager (netinit.cpp / netinit.h);
// sub_6A6960 is the canonical CNetManagerGetUserManager (netinit.cpp).
static int   sub_6A69A0(int a) { (void)a; return 0; }
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
// A safe no-op IMMNotificationClient: the real client the game registers with
// the enumerator (vtable 0x01B6F408) is not implemented in this build, so its
// first field is a live no-op vtable.  RegisterEndpointNotificationCallback
// AddRefs it (and any audio-event callback resolves to safe code), so the
// first field of the object (the vtable) must be a mapped address, not
// 0x01B6F408.  Each method is a plain __stdcall entry with the exact parameter
// arity so it cleans the right number of stack bytes.
struct F3AudioPropKey { int d1; short d2, d3; char d4[8]; int d5; };   // 20-byte PROPERTYKEY (by value)
static long __stdcall F3AudioNotifQI(void *self, const void *riid, void **ppv) { (void)self; (void)riid; if (ppv) *ppv = 0; return (long)0x80004002; }
static long __stdcall F3AudioNotifAddRef(void *self)   { return 1; }
static long __stdcall F3AudioNotifRelease(void *self)  { return 1; }
static long __stdcall F3AudioNotifStateChanged(void *self, void *id, unsigned st) { return 0; }
static long __stdcall F3AudioNotifAdded(void *self, void *id)   { return 0; }
static long __stdcall F3AudioNotifRemoved(void *self, void *id) { return 0; }
static long __stdcall F3AudioNotifDefaultFlow(void *self, unsigned a, unsigned b, void *c) { return 0; }
static long __stdcall F3AudioNotifDefaultRole(void *self, unsigned a, unsigned b, void *c) { return 0; }
static long __stdcall F3AudioNotifPropertyChanged(void *self, void *id, F3AudioPropKey key) { return 0; }
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

static BYTE  g_byte_1DDC828 = 0;              // 0x1DDC828: InitVirtualFileSystem done flag
static DWORD g_dword_1DDC820 = 0;             // 0x1DDC820: the CProcessDelegateThiscall object (16 bytes)
static DWORD g_dword_1DDC824 = 0;             // 0x1DDC824: query-table mode (2 = drain)
static BYTE  g_unk_1DDC830[28];               // 0x1DDC830: the main (80/4096) allocation-cache object
static DWORD g_off_1D6A80C[2] = { kCProcessDelegateStaticVftable, 0 };  // 0x1D6A80C: {vtable, VfsGetQueryTable}
static DWORD g_off_1D6E9C8[2] = { kCProcessDelegateStaticVftable, 0 };  // 0x1D6E9C8: {vtable, AudioProcessDelegateTick}

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

// VfsProviderAlloc  (0x1477DA0)  -- [STUB] allocate the provider query buffer;
//  returns a fresh block (non-null) so the query ctor takes its success path.
static DWORD VfsProviderAlloc(int totalBytes, int count, int a3, int a4, int a5)
{
    (void)count; (void)a3; (void)a4; (void)a5;
    return (DWORD)LHAlloc(totalBytes > 0 ? (unsigned int)totalBytes : 1u);
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
    void *p = LHAlloc(32) ? (void *)VfsProviderAlloc(a3 * a2, a2, 1, (int)((BYTE *)v5)[60], 0) : 0;
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
//  worker's critical section (sub_1A22480/sub_1A22460 over stru_1DDCA90 +
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

// ProcessDelegateDtor  (0x146DFA0 - 0x146DFA7)  -- the CProcessDelegateThiscall
//  deleter (sub_14833E0, a remaining deep stub); returns 0.
static int ProcessDelegateDtor(void *obj) { (void)obj; return 0; }

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

// AudioEngineCtor  (0x1479440)  -- [STUB] the 340-byte audio engine build (six
//  node lists + the query ring); returns its (ECX) argument.
static void *AudioEngineCtor(void *obj, int a2, int a3, int a4)
{
    (void)a2; (void)a3; (void)a4;
    return obj;
}

// AudioProcessDelegateTick  (0x147A740 - 0x147A74B)  -- the registered audio
//  process tick (the deep sub_146D930/sub_1478F20/sub_1478820/... no-op chain).
static int AudioProcessDelegateTick(void) { return 0; }

// AudioFrameworkInit  (0x150D180)  -- one-time audio framework bring-up [STUB];
//  the two VfsWorkerCallbackRegister registrations into slots 1/2 are no-ops in
//  this build.
static int AudioFrameworkInit(void) { return 1; }

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

// VfsQueryTableBuild  (0x1473E70)  -- [STUB] build the query table (drain the
//  pending ring into the cache under the +0x34 CS); void.
static void VfsQueryTableBuild(void *obj, int timeout) { (void)obj; (void)timeout; }

// VfsQueryTableDrain  (0x1474320)  -- [STUB] drain one pending query (the ring
//  index + the InterlockedExchangeAdd + SetFrontendEvent); returns 1.
static int VfsQueryTableDrain(void *obj, int elem) { (void)obj; (void)elem; return 1; }

// VfsGetQueryTable  (0x1460210 - 0x1474011)  -- the VFS query-table getter
//  registered as the worker-slot-0 callback.  In drain mode (g_dword_1DDC824 ==
//  2) it walks the cache's +0x10C counter, draining pending queries
//  (VfsQueryTableDrain) until caught up; otherwise it (re)builds the table via
//  VfsQueryTableBuild over the descriptor's +0x04 count.  Returns the result
//  pointer (the cache's +0x10C in drain mode; void/0 otherwise).
static DWORD *VfsGetQueryTable(void)
{
    if (g_dword_1DDC824 == 2) {
        void *cachePtr = *(void **)((char *)g_unk_1DDC830 + 4);   // 0x1DDC834
        DWORD *cache = (DWORD *)cachePtr;                          // *(0x1DDC834)
        DWORD *result = (DWORD *)((char *)cache + 0x10C);
        while (cache != 0 &&
               *(DWORD *)((char *)cache + 0x10C) > *(DWORD *)((char *)g_unk_1DDC830 + 0x14)) {
            VfsQueryTableDrain(cachePtr, *(DWORD *)((char *)g_unk_1DDC830 + 0x14));
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
//  remaining stubs: register the per-provider VFS module groups (audio /
//  video / input / table-lookup providers) after a successful init.  Not
//  implemented in this build (reached only on the success path).
static int RegisterVfsGroup1(void) { return 0; }
static int RegisterVfsGroup2(DWORD a) { (void)a; return 0; }
static int RegisterVfsGroup3(void) { return 0; }
static int RegisterVfsGroup4(void) { return 0; }


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
            RegisterVfsGroup1();
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
static void F3InvalidParam(void) {}

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
//     "cultures_win32.txt" name slot (sub_1A02260 = the 2-source path concat
//     over a fresh slot), loaded through XLipLoadCultures (sub_17CC7C0);
//     on failure retries with "\\cultures.txt" (each failure touches
//     GetLastError, as the original does through its EDX IAT slot).
//  3. Registers the base path as the default lip component (CreateLipFSRoot,
//     sub_17BEE80) and
//     the seven {name, .., resolved path} component slots (dword_1DC2748,
//     five DWORDs per slot) through RegisterLipComponent, runs the
//     component bring-up
//     (sub_17BF770), then resolves each slot's relative path into its +0x0C
//     slot (ResolveLipComponentSlotPath, sub_717D60).
//  4. Rebuilds the culture-string array: frees the old block
//     (dword_1DC27EC), count = sub_17C0610(0) (dword_1DC27F0),
//     LHAlloc(96 * count, the original's `mul` overflow folds to -1), then
//     per 96-byte entry fills the 85-byte name (sub_17C0680), the culture id
//     (sub_17CC320) at +88, and records the "text" culture index in
//     dword_1DC27F4. (With the count-0 stub sub_17C0610 the per-entry loop
//     is dead in this build.)
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
static void *GetFileSubsystem(void *obj);

// ============================================================
//  F3AString -- the 28-byte front-end ANSI string (the sub_402F00 family,
//  the MSVC basic_string<char, 16-byte-SSO> layout) that ReleaseLipCultures
//  builds on the stack to name each "lip%u" entry:
//    +0    d0 (untouched by assign / dtor)
//    +4    SSO buffer (16 chars) / heap pointer
//    +20   char length
//    +24   capacity ( < 16 => SSO )
// ============================================================
typedef struct F3AString
{
    DWORD   d0;
    union { char buf[16]; DWORD ptr; } u;
    DWORD   len;
    DWORD   cap;
} F3AString;

// default SSO ctor (the original zeroes the capacity, length and buffer[0],
// leaving d0 as-is): cap = 15, len = 0, buffer[0] = 0.
static void F3AStringReset(F3AString *s)
{
    s->cap = 15;
    s->len = 0;
    s->u.buf[0] = 0;
}

// sub_402F00 (0x402F00 - 0x402F70) -- basic_string<char>::assign(src, len):
// thiscall, returns the string.  "lip%u" is always short (< 16 chars) so the
// SSO path is taken (no heap buffer); the general heap-growth path is
// mirrored for completeness.
static F3AString *F3AStringAssign(F3AString *s, const char *src, unsigned int len)
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
static void F3AStringFree(F3AString *s)
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
// sub_1A02260 (0x1A02260 - 0x1A02292) -- the 2-source shared-slot path
// concat: zero dst, ref (CopySharedSlot) src1, append src2 (sub_1A01940);
// returns the dst slot address.
static int * sub_1A02260(int *a1, int *a2, const char ***a3)
{ (void)a2; (void)a3; *a1 = 0; return a1; }
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
// sub_17BFC20 (0x17BFC20 - 0x17BFF58) -- bounded strcpy into an 85-byte
// destination (the "text" name copy); 0 on success.
static int   sub_17BFC20(const char *src, char *dst, int size)
{ (void)src; (void)dst; (void)size; return 0; }
// sub_17C0610 (0x17C0610 - 0x17C0674) -- number of registered lip cultures
// (sub_17B3DC0 over the culture table at 0x1E2C680 for a1 = 0).
static int   sub_17C0610(int a1) { (void)a1; return 0; }
// sub_17C0680 (0x17C0680 - 0x17C08D0) -- copy culture a2's 85-byte name into
// Destination (FindLipComponentByName lookup + the name at +56).
static int   sub_17C0680(int a1, int a2, char *dst, int size)
{ (void)a1; (void)a2; (void)dst; (void)size; return -1; }
// sub_17CC320 (0x17CC320 - 0x17CC4E2) -- XLipCultures name -> culture id
// (lower-cased lookup through sub_17CD070; case 1 = the id at +28).
static int   sub_17CC320(char *s, int a2) { (void)s; (void)a2; return 0; }
//  RegisterStandardAssets  (0x718690 - 0x718BCC)  -- register the game's
// standard asset packages ("speech" / "speechdoc" / "lipsync" / "text" /
// "fonts" / "videos" / "gui") into the language / VFS resource table.
// Defined below, after the F3WString helpers it builds its temp strings with.
static int   RegisterStandardAssets(void);
// sub_719540 (0x719540 - 0x719765) -- post-load culture bookkeeping over the
// loaded culture set (the name slot + out flag byte).
static char  sub_719540(void *a1, char *a2) { (void)a1; (void)a2; return 0; }

static long InitLipCultures(void)
{
    F3SharedBuffer *cultureName = 0;   // "cultures_win32.txt" name slot (v23)
    F3SharedBuffer *path        = 0;   // settings dir + "\" (v19)
    F3SharedBuffer *fullPath    = 0;   // path + culture name (v20)
    F3SharedBuffer *textSlot    = 0;   // the "text" culture name slot (v21)
    char            textFlag    = 0;   // sub_719540's out flag (v22)

    (void)GetGameEngine();             // sub_619A90, result discarded
    g_dword_1DC2808 = (DWORD)(void (*)(void))LHAlloc;   // j_LHAlloc_1
    g_dword_1DC280C = (DWORD)(void (*)(void))LHFree;    // j_LHFree_3
    ReleaseLipCultures();
    SetLipMemAllocators((int)g_dword_1DC2808, (int)g_dword_1DC280C);
    InitLipRuntime();

    SetSharedSlot(&cultureName, "cultures_win32.txt", -1);
    MakeSettingsFilePath(&path, (F3SharedBuffer **)&g_dword_1DC2744, "\\");
    sub_1A02260((int *)&fullPath, (int *)&path, (const char ***)&cultureName);
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
    g_dword_1DC27F0 = (DWORD)sub_17C0610(0);
    {
        unsigned long long n = 96ULL * g_dword_1DC27F0;   // `mul`, overflow -> -1
        unsigned size = (n > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (unsigned)n;
        g_dword_1DC27EC = (DWORD)LHAlloc(size);
    }

    char name[85];
    memset(name, 0, sizeof(name));   // original: memset(0x54) + the 0x54 byte
    sub_17BFC20("text", name, 0x55);
    SetSharedSlot(&textSlot, name, -1);
    sub_719540(&textSlot, &textFlag);

    unsigned count = 0;
    if (g_dword_1DC27F0 != 0)
    {
        int off = 0;
        do
        {
            char *entry = (char *)(DWORD)g_dword_1DC27EC + off;
            sub_17C0680(0, (int)count, entry, 0x55);
            *(DWORD *)(entry + 88) = (DWORD)sub_17CC320(entry, 1);
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

static void  sub_4358A0(void *obj, const char *name) { (void)obj; (void)name; }
static void  sub_1801580(void *obj, void *str) { (void)obj; (void)str; }
// Placeholder vtables for the frontend's stub-constructed engine members.
// The original constructors store real vtables (NUI::CLoadingScreen, 0x7330F0;
// CGameRenderer, 0x72FB30; NUI::CFullScreenMoviePlayer, 0x731B80; 0x767360:
// CPackageManager); those tables are unimplemented in this build, but slot 0
// (the `vt[0](obj, 1)` release dispatch used by RunFrontEnd's old-value frees
// and by CGameEngineDestroy) must be a callable no-op so the teardown does not
// dispatch through unconstructed memory.
static void F3StubRelease(void *obj, int freeIt) { (void)obj; (void)freeIt; }
static void *g_CLoadingScreenVtable[2]         = { (void *)F3StubRelease, 0 };
static void *g_CGameRendererVtable[2]          = { (void *)F3StubRelease, 0 };
static void *g_CFullScreenMoviePlayerVtable[2] = { (void *)F3StubRelease, 0 };
static void *g_CPackageManagerVtable[2]        = { (void *)F3StubRelease, 0 };

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
    ((BYTE *)head)[flagOffset] = 1;
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
static void SetFrontendEvent(void *obj)
{
    SetEvent(*(HANDLE *)((char *)obj + 0x80));
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
// sub_7330F0 -- NUI::CLoadingScreen ctor (0x58 bytes): stub stores the
// placeholder vtable and zeroes +4 (the CS slot).
static void *sub_7330F0(void *obj)
{ *(void **)obj = g_CLoadingScreenVtable; *(void **)((char *)obj + 4) = 0; return obj; }
// sub_72FB30 -- CGameRenderer ctor (8 bytes): stub stores the placeholder
// vtable and zeroes +4 (the impl slot).
static void *sub_72FB30(void *obj)
{ *(void **)obj = g_CGameRendererVtable; *(void **)((char *)obj + 4) = 0; return obj; }
static void  sub_72FB10(void *obj, void *argList, void *a, void *b)
{ (void)obj; (void)argList; (void)a; (void)b; }
static void  nullsub_28(int a) { (void)a; }
static void  sub_70DDB0(int a) { (void)a; }
static void  sub_18F0180(void) {}
static void  sub_72FAE0(void *obj) { (void)obj; }
static void  sub_61BFE0(void *obj, int a) { (void)obj; (void)a; }
static void  sub_61A5E0(void *obj) { (void)obj; }
// sub_731B80 -- NUI::CFullScreenMoviePlayer ctor (8 bytes): stub stores the
// placeholder vtable and zeroes +4.
static void *sub_731B80(void *obj)
{ *(void **)obj = g_CFullScreenMoviePlayerVtable; *(void **)((char *)obj + 4) = 0; return obj; }
// sub_712430 -- the 8-byte {value, SEH block} wrapper ctor: zeroes both words
// (the original then allocates the 96-byte impl and registers it via
// sub_7172D0, which the stub skips; the SEH slot stays 0).
static void *sub_712430(void *obj)
{ *(void **)obj = 0; *(void **)((char *)obj + 4) = 0; return obj; }
static void  nullsub_29(void) {}
static void  sub_18FB930(void) {}

// F3_VT(OBJ): read OBJ's vtable (the first word) as a void ** so its slots
// can be indexed. (OBJ is a class instance pointer.)
#define F3_VT(OBJ) ((void **)(*(void **)(OBJ)))

// ============================================================
//  F3WString -- the 36-byte front-end wide path string (the MSVC
//  basic_string<wchar_t, 2> layout + two extra zeroed dwords) that
//  InitFrontendData's callees (sub_435C10 / sub_4358A0 / sub_436BA0 /
//  sub_5AF180) operate on:
//    +0    d0 (low word zeroed by the reset idiom)
//    +4    SSO buffer (8 wchars) / heap pointer
//    +20   wchar length
//    +24   capacity ( < 8 => SSO )
//    +28 / +32  extra zeroed words
// ============================================================
typedef struct F3WString
{
    DWORD   d0;
    union { wchar_t buf[8]; DWORD ptr; } u;
    DWORD   len;
    DWORD   cap;
    DWORD   e1, e2;
} F3WString;

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

// sub_4358A0 -- ANSI -> wide, then assign (the +28 / +32 words zeroed).
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
// F3WStringAssignAnsi), registers it by package (sub_718250), then drops the
// string's heap buffer (the "if (cap >= 8) LHFree" idiom -- every path here is
// > 7 chars, so it is always heap-allocated and freed).
// [NOT byte-identical] the original is 26 hand-inlined
// (push-constant / call) blocks referencing high-VA .rdata string constants
// and E8 calls to sub_4358A0 / sub_718250 / LHFree.
static int RegisterStandardAssets(void)
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
        F3WStringAssignAnsi(&s, kAssets[i].path);       // sub_4358A0
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
static void *GetFileSubsystem(void *obj)
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

// sub_17FE870: the LHSFS::CLegacyFile ctor over a 16-byte block (vtable +
// shared slot + two zeroed words). 'slot' (a2) is the caller's temporary
// path-string shared slot, handed to the sub_17FE0A0 setup (a remaining
// stub here).
static void LegacyFileCtor(void *obj, F3SharedBuffer *slot, int a3, int a4)
{
    DWORD *b = (DWORD *)obj;
    b[0] = (DWORD)g_CLegacyFileVtable;
    InitSharedSlot((F3SharedBuffer **)&b[1]);
    b[2] = 0;
    b[3] = 0;
    (void)slot; (void)a3; (void)a4;
}

// sub_17FDC30: does the legacy file have data (the +8 slot set)?
static int LegacyFileHasData(const void *obj)
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
// (dword_1E2D7C4) via sub_1A01BE0.
static long ResolveDataPathSlot(F3SharedBuffer **slot) { (void)slot; return 0; }

// sub_17FD6A0: resolve the "data\\" shared slot into the object's wide path
// globals (dword_1E2D7BC).
static void LegacyFileSetDataPath(void *obj) { (void)obj; }

// sub_17FD970: the LHSFS::CLegacyFile dtor (release +12 / +8, release the
// shared slot, reset the vtable to LHFile::CAFile).
static long LegacyFileDtor(void *obj)
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
    F3WStringReset(&bnk);
    F3WStringAssignAnsi(&bnk, "streaming.bnk");
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
    DWORD           fileObj[4]   = { 0 };  // the 16-byte data-file object
    F3SharedBuffer *dataHandle = 0;        // the resolved data dir / archive handle
    F3SharedBuffer *tmpHandle  = 0;        // the re-resolve out slot
    void           *streamMgr  = 0;        // the 84-byte streaming manager
    DWORD           fileBlock[4] = { 0 };  // the 16-byte build_version file
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
        F3WStringAssignAnsi(&langSet, "dir.manifest");
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
    DataFileRegisterEntry(fileObj, (tmpHandle != 0) ? (void *)tmpHandle->data : 0,
                          &langSet.u, 2, 1);
    if (tmpHandle != 0 && F3_VT(tmpHandle) != 0)
        ((void (*)(void *))F3_VT(tmpHandle)[2])(tmpHandle);
    F3WStringClear(&langSet);

    // 7. The two Bink banks (levels.bnk, then streaming.bnk).
    F3WStringAssign(&bookPath, L"levels.bnk", 10);
    tmpHandle = (F3SharedBuffer *)LookupBnk(&tmpHandle, cwdSlot, &bookPath, 0);
    DataFileRegisterEntry(fileObj, (tmpHandle != 0) ? (void *)tmpHandle->data : 0,
                          &bookPath, 2, 1);
    if (tmpHandle != 0 && F3_VT(tmpHandle) != 0)
        ((void (*)(void *))F3_VT(tmpHandle)[2])(tmpHandle);
    F3WStringClear(&bookPath);

    F3WStringAssign(&bookPath, L"streaming.bnk", 13);
    tmpHandle = (F3SharedBuffer *)LookupBnk(&tmpHandle, cwdSlot, &bookPath, 0);
    DataFileRegisterEntry(fileObj, (tmpHandle != 0) ? (void *)tmpHandle->data : 0,
                          &bookPath, 2, 0);
    if (tmpHandle != 0 && F3_VT(tmpHandle) != 0)
        ((void (*)(void *))F3_VT(tmpHandle)[2])(tmpHandle);
    F3WStringClear(&bookPath);

    // 8. The babel text book + the manager flag byte (the original writes
    //    past the 84-byte allocation at manager+0x161, and clears it again
    //    when the book string's ptr field reads 0).
    F3WStringAssignAnsi(&bookPath, "language\\en-uk\\text\\book.babel");
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
    LegacyFileCtor(fileBlock, fileSlot, 1, 2);            // sub_17FE870
    ReleaseSharedSlot(&fileSlot);
    if (LegacyFileHasData(fileObj) != 0)                  // sub_17FDC30(v45)
        CopyFileToSharedSlot(fileObj, &g_unk_1DBDA78);    // sub_61D800(&unk_1DBDA78)
    PublishDataObject1(archive);                          // sub_17FDE60(v44)

    SetSharedSlot(&fileSlot, "data\\", -1);
    ResolveDataPathSlot(&fileSlot);                       // sub_17FD600
    ReleaseSharedSlot(&fileSlot);
    PublishDataObject2(archive);                          // sub_17FDEC0(v44)

    SetSharedSlot(&fileSlot, "data\\", -1);
    LegacyFileSetDataPath(fileSlot);                      // sub_17FD6A0
    ReleaseSharedSlot(&fileSlot);
    LegacyFileDtor(fileObj);                              // sub_17FD970(v45)

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
        int   v13  = (int)(DWORD)SharedSlotToWide((F3SharedBuffer **)&g_unk_1DBD048, &v76);
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
        sub_4358A0(&v81[0], "startup.vfsconfig");
        memset(vfs, 0, sizeof(vfs));
        sub_1801580((void *)v164, &v81[0]);
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
        int c = (int)(DWORD)GetNetManager();
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
    CDiskFileWin32Close((CDiskFileWin32 *)v93);
    ReleaseSharedSlot(&v96);
    v93[0] = (DWORD)g_CAFileVtable;
    ReleaseSharedSlot(&v88);
    DestroyOptionMaps(v108);   // 0x40E1F0: destroy the video-options maps
    return result;
}
