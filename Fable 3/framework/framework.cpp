// Fable 3 — framework module (refcounted init / shutdown).
//
// Contains FrameworkInit / FrameworkShutdown and the two private stubs they
// call, plus the framework-internal globals. Everything here is internal to
// the module: the only externally visible symbols are FrameworkInit and
// FrameworkShutdown (declared in framework.h).

#include "framework.h"

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
//  sub_4017B0  (0x004017B0 - 0x004017F7)  -- release the global shared pointer
//
//  if (lpAddend != 0) {
//      if (InterlockedDecrement(lpAddend) == 0) {
//          lpAddend[1](lpAddend[2]);              // call the object's destructor
//          (*(dword_2075B24 + 16))(lpAddend);      // free the control block
//      }
//  }
//  lpAddend = 0;  dword_20B6004 = 0;
//  With the current stubs lpAddend is never set, so this is a no-op.
//  [STUB]
// ============================================================
static int sub_4017B0(void)
{
    return 0;
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
//      sub_4017B0();          // release the framework's global shared pointer
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
    result = sub_4017B0();
    g_byte_2075B33 = 0;
    return result;
}
