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

// ---- framework-internal globals (0x2075xxx / Stream in the original) ----
static DWORD g_dword_2075C44 = 0;  // 0x2075C44: framework refcount
static BYTE  g_byte_2075B33  = 0;  // 0x2075B33: framework initialized flag
static FILE *g_Stream        = 0;  // "Stream": global file stream (opened by the engine)

// ============================================================
//  sub_1A02870  (0x01A02870 - 0x01A02937)  -- compute the exe directory
//
//  Takes GetCommandLineA() (which begins with the exe path), isolates the
//  first token (truncating at the first space, or the first quote when the
//  command line is quoted — in which case the leading quote is stripped),
//  then truncates at the last backslash so only the directory remains, and
//  stores it in the global byte_2075B38 (0x107 = 263 bytes).
//  [STUB]
// ============================================================
static int sub_1A02870(void)
{
    return 0;
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
//  sub_1A02870();        // first time: compute the exe directory
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

    sub_1A02870();   // compute the executable's directory (first init only)
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
