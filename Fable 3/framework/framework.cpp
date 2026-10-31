// Fable 3 — framework module (refcounted init / shutdown).
//
// Contains FrameworkInit / FrameworkShutdown and the two private stubs they
// call, plus the framework-internal globals. Everything here is internal to
// the module: the only externally visible symbols are FrameworkInit and
// FrameworkShutdown (declared in framework.h).

#include "framework.h"
#include "common/common.h"

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

// The 12-byte refcounted control block the global shared object points to:
//   +0  refcount    reference count (Interlocked)
//   +4  destructor  invoked as destructor(arg) when the count reaches zero
//   +8  arg         the argument handed to the destructor
typedef struct F3SharedObject
{
    volatile long refcount;
    void (*destructor)(int);
    int arg;
} F3SharedObject;

// 0x20B6004 + 0x20B6008 — the global shared object pair. `g_lpAddend` points to
// a refcounted control block (see F3SharedObject); when its refcount reaches
// zero the destructor is invoked and the block is freed (ReleaseGlobalObject).
static DWORD          g_dword_20B6004 = 0;  // 0x20B6004
static F3SharedObject *g_lpAddend     = 0;  // 0x20B6008: the global shared object

// ============================================================
//  ComputeExeDirectory  (0x01A02870 - 0x01A02937)  -- compute the exe directory
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
//
//  if (g_lpAddend != 0) {
//      if (InterlockedDecrement(&g_lpAddend->refcount) == 0) {
//          g_lpAddend->destructor(g_lpAddend->arg);   // invoke the destructor
//          LHFree(g_lpAddend);                          // free the control block
//      }
//  }
//  g_lpAddend = 0;  g_dword_20B6004 = 0;
//  Returns the final refcount (0 when there was nothing to release).
// ============================================================
static int ReleaseGlobalObject(void)
{
    long result = 0;
    if (g_lpAddend != 0)
    {
        result = InterlockedDecrement(&g_lpAddend->refcount);
        if (result == 0)
        {
            g_lpAddend->destructor(g_lpAddend->arg);
            LHFree(g_lpAddend);
        }
    }
    g_lpAddend = 0;
    g_dword_20B6004 = 0;
    return (int)result;
}

// ============================================================
//  FrameworkInit  (0x01A02B50 - 0x01A02B76)  -- framework init (refcounted)
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
