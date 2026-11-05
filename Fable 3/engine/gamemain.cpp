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
#include "manager.h"              // GetManager
#include "netinit.h"              // InitNetworking
#include "save/save.h"            // SetSaveGamePath (the shared-slot string setter)
#include "worker.h"               // GetWorker
#include "xlive.h"                // the xlive.dll (XLiveAPI) import declarations

#include <windows.h>
#include <process.h>              // _beginthread / _getdrive / _chdrive
#include <wchar.h>                // _wgetcwd
#include <string.h>
#include <mbstring.h>             // _mbscmp
#include <math.h>                 // fabsf
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
static BYTE  g_byte_2075F4E = 1;   // 0x2075F4E: path-normalize enable flag
// Function-pointer slots published by the frontend (0x1D8C850 / 0x1D8C854).
static void *g_off_1D8C850 = 0;    // 0x1D8C850
static void *g_off_1D8C854 = 0;    // 0x1D8C854
// Global "this" objects the frontend dispatches through (0x1DBD048 / 0x1DBD9E8).
// The callees (stubs) never dereference them, so a zeroed block stands in.
static int   g_unk_1DBD048[8] = { 0 };   // 0x1DBD048
// 0x1DBD9E8: the frontend object.  The original constructor (sub_18C46E0,
// thiscall + retn 8, invoked at registration by sub_1A5C6A0) stores a
// CreateEventA handle at +0x80; WaitForEvent / SetFrontendEvent / the
// CloseHandle sibling all read that slot.  It is not created in this build,
// so the 0x84-byte block stays zeroed: SetEvent(0) is a no-op failure and
// WaitForSingleObjectEx(0, ...) returns WAIT_FAILED immediately.
static int   g_unk_1DBD9E8[33] = { 0 };  // 0x1DBD9E8 (0x84 bytes)
// Placeholder vtables for the two file objects created on the frontend.
static void *g_CDiskFileWin32Vtable[4] = { 0 };
static void *g_CAFileVtable[4]         = { 0 };

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
static F3SharedBuffer **CopySharedSlot(F3SharedBuffer **thisSlot, F3SharedBuffer **srcSlot)
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

static int   sub_18F02B0(void) { return 0; }

// qword_20FC928 / dword_20FC92C: the current display resolution
// {width, height}, set by the device creation (sub_18F1B30) from the
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
// sub_6A66C0 is the canonical GetNetManager (netinit.cpp / netinit.h).
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

// sub_767360 -- CPackageManager ctor (0x30 bytes): stub stores the
// placeholder vtable and zeroes +4/+8.
static void *sub_767360(void *obj)
{ *(void **)obj = g_CPackageManagerVtable; *(void **)((char *)obj + 4) = 0;
  *(void **)((char *)obj + 8) = 0; return obj; }
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
//  (the sub_18C46E0 constructor's CreateEventA handle; 0 in this build, so
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
// call, which the decompiler mis-attributes to it). Remaining stubs: the heap
// manager (dword_2075ED8) is not implemented in this build, so they return 0.
void *GetHeapManager(void) { return 0; }
void *GetHeapManagerSlot1C(void *hm) { (void)hm; return 0; }
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
            if ((unsigned char)str103[3] != (unsigned char)sub_18F02B0()
                || (int)str103[1] != p[0]
                || (int)str103[2] != p[1])
            {
                str103[3] = (DWORD)sub_18F02B0();
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
        int a = (int)(DWORD)GetNetManager();
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
