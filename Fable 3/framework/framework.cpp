// Fable 3 — framework module (refcounted init / shutdown).
//
// Contains FrameworkInit / FrameworkShutdown and the two private stubs they
// call, plus the framework-internal globals. Everything here is internal to
// the module: the only externally visible symbols are FrameworkInit and
// FrameworkShutdown (declared in framework.h).

#include "framework.h"
#include "common/common.h"
#include "engine/object_registry.h"   // GetDebugHandlerPair / F3ObjReg / F3SehBlock

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- framework-internal globals (0x2075xxx / Stream in the original) ----
static DWORD g_dword_2075C44 = 0;  // 0x2075C44: framework refcount
static BYTE  g_byte_2075B33  = 0;  // 0x2075B33: framework initialized flag
static FILE *g_Stream        = 0;  // "Stream": global file stream (opened by the engine)

// 0x2075B38 — the executable's directory path (computed from the command line
// at first init). A 0x107-byte buffer; the game's base path for locating data
// files and save games.
char g_ExeDirectory[0x107];

// The global shared object pair (0x20B6004 + 0x20B6008) is the
// NDebug::NDebugManager::CFableWindowsDebugHandler object + its SEH control
// block, owned by engine/object_registry.cpp (GetDebugHandlerPair). It is
// installed by SetDebugHandler (object_registry.cpp) and released here during
// FrameworkShutdown. The 12-byte control block is an F3SehBlock { refcount,
// handler, arg }: when its refcount reaches zero the handler is invoked and the
// block is freed via the game heap (ReleaseGlobalObject below).

// ============================================================
//  ComputeExeDirectory  (0x01A02870 - 0x01A02937)  -- compute the exe directory
//  [NOT byte-identical] placement-dependent (relative calls into the CRT: strncpy_s,
//
//  Takes GetCommandLineA() (which begins with the exe path), isolates the
//  first token (truncating at the first space, or the first quote when the
//  command line is quoted — in which case the leading quote is stripped),
//  then truncates at the last backslash so only the directory remains, and
//  stores it in the global g_ExeDirectory (byte_2075B38, 0x107 = 263 bytes).
// ============================================================
static void ComputeExeDirectory(void)
{
    char  buf[0x107];          // local command-line buffer (263 bytes)
    char  delim = ' ';         // default delimiter: space (0x20)

    const char *cmdline = GetCommandLineA();

    // Copy up to 0x105 chars of the command line into the local buffer.
    strncpy_s(buf, sizeof(buf), cmdline, 0x105);
    buf[0x106] = 0;           // extra null-termination (matches the original)

    if (buf[0] == '"')         // command line starts with a quote
    {
        delim = '"';           // 0x22: the delimiter becomes the closing quote
        size_t len = strlen(buf);
        if (len > 0)
            memmove(buf, buf + 1, len);   // strip the leading quote
    }

    // Truncate at the first delimiter -> the exe path (drops the arguments).
    char *p = buf;
    if (*p != 0)
        while (*p != 0 && *p != delim)
            ++p;
    *p = 0;

    // Truncate at the last backslash -> the directory only.
    char *slash = strrchr(buf, '\\');
    if (slash != 0)
        slash[1] = 0;

    // Store into the global base directory.
    strcpy_s(g_ExeDirectory, sizeof(g_ExeDirectory), buf);
}

// The executable's directory path (computed from the command line at first
// init). Empty until FrameworkInit has run.
const char *ExeDirectory(void)
{
    return g_ExeDirectory;
}

// ============================================================
//  ReleaseGlobalObject  (0x004017B0 - 0x004017F7)  -- release the global shared object
//  [NOT byte-identical] placement-independent (no relative calls) but references the heap
//  table (0x02075B24), lpAddend (0x020B6008) and the InterlockedDecrement IAT slot
//  (0x01AD52FC) at original high VAs not present in this build.
//
//  if (lpAddend != 0) {
//      if (InterlockedDecrement(&lpAddend->refcount) == 0) {
//          lpAddend->destructor(lpAddend->arg);       // invoke the destructor
//          LHFree(lpAddend);                            // free the control block
//      }
//  }
//  lpAddend = 0;  dword_20B6004 = 0;
//  Returns the final refcount (0 when there was nothing to release).
//
//  Operates on the CFableWindowsDebugHandler global pair (dword_20B6004 /
//  lpAddend), shared with object_registry.cpp via GetDebugHandlerPair().
// ============================================================
static int ReleaseGlobalObject(void)
{
    F3ObjReg   *pair = GetDebugHandlerPair();   // 0x20B6004 / lpAddend (0x20B6008)
    F3SehBlock *sb   = (F3SehBlock *)pair->sehBlock;
    long result = 0;
    if (sb != 0)
    {
        result = InterlockedDecrement(&sb->refcount);
        if (result == 0)
        {
            sb->handler(sb->arg);
            LHFree(sb);
        }
    }
    pair->object   = 0;
    pair->sehBlock = 0;
    return (int)result;
}

// ============================================================
//  FrameworkInit  (0x01A02B50 - 0x01A02B76)  -- framework init (refcounted)
//  [NOT byte-identical] placement-dependent (relative call to ComputeExeDirectory).
//
//  if (g_dword_2075C44 != 0) { ++g_dword_2075C44; return <eax, unused>; }
//  ComputeExeDirectory();  // first time: compute the exe directory
//  ++g_dword_2075C44;
//  g_byte_2075B33 = 1;   // mark the framework as initialized
//  return 1;
// ============================================================
int FrameworkInit(void)
{
    if (g_dword_2075C44 != 0)
    {
        ++g_dword_2075C44;
        return 0;   // original leaves eax undefined here; value unused by callers
    }

    ComputeExeDirectory();   // compute the executable's directory (first init only)
    ++g_dword_2075C44;
    g_byte_2075B33 = 1;
    return 1;
}

// ============================================================
//  FrameworkShutdown  (0x01A02E70 - 0x01A02EA3)  -- framework shutdown (refcounted)
//  [NOT byte-identical] placement-dependent (relative calls to fclose, ReleaseGlobalObject).
//
//  if (g_byte_2075B33 == 0) { __debugbreak(); return; }   // not initialized
//  --g_dword_2075C44;
//  if (g_dword_2075C44 == 0) {
//      if (g_Stream) fclose(g_Stream);
//      ReleaseGlobalObject();   // release the framework's global shared object
//      g_byte_2075B33 = 0;
//  }
// ============================================================
int FrameworkShutdown(void)
{
    int result = 0;
    if (g_byte_2075B33 == 0)
    {
        __debugbreak();   // original: int 3 — reached only if shut down without init
        return 0;
    }

    --g_dword_2075C44;
    if (g_dword_2075C44 != 0)
        return 0;   // still in use; original returns undefined eax (unused)

    if (g_Stream != 0)
        fclose(g_Stream);
    result = ReleaseGlobalObject();
    g_byte_2075B33 = 0;
    return result;
}
