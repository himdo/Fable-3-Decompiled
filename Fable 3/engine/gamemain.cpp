// Fable 3 — frontend run module implementation (RunFrontEnd, 0x61CB70, plus its
// module-private globals, the xlive.dll / steam_api import stubs, and the
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

static void  sub_19FFA20(void *str) { (void)str; }
static int   sub_19FF560(void *str) { (void)str; return 0; }
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
        sub_18C4560(&g_unk_1DBD9E8, -1, 0);
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
    CDiskFileWin32Close((CDiskFileWin32 *)v93);
    ReleaseSharedSlot(&v96);
    v93[0] = (DWORD)g_CAFileVtable;
    ReleaseSharedSlot(&v88);
    DestroyOptionMaps(v108);   // 0x40E1F0: destroy the video-options maps
    return result;
}
