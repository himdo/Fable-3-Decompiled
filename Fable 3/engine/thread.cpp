// Fable 3 — thread utilities (SetThreadName, 0x18C5840).

#include "engine.h"
#include <windows.h>

// ============================================================
//  SetThreadName  (0x18C5840 - 0x18C58C3)  -- announce the current thread's
//  name via a first-chance SEH exception
//  [NOT byte-identical] refs the compiler-generated SEH scope table
//  (stru_1C7F160) + EH4 scope handler (0x152ECE0), the original /GS
//  security cookie, and the RaiseException IAT slot (0x01AD51B0) at
//  their original high VAs.
//
//  cdecl, one pointer argument: the thread-name string. Every thread
//  entry point in the game calls this as its first statement: GameThread
//  ("GameThread"), StartAddress ("3D Engine"), Texture Streamer,
//  Lightmap Streamer, Particle Thread, Cloth Simulation, VFS - Request
//  Manager / VFS - Request Finalizer, Permanent Bank Mounter, Startup
//  Audio Loading, ConnectionThread, Update Live Presence Thread, and
//  Engine Fatal Shutdown.
//
//  Behavior:
//    1. Fill a 4-dword argument block: { 0x1000, name, -1, 0 }.
//    2. RaiseException(0x406D1388, 0, 4, args) — the "unwind" exception
//       code 0x006D1388 with the SEH_1STCHANCE bit (0x40000000) set, so
//       the raise is delivered first-chance to any debugger / profiler /
//       vectored handler. Arguments[1] carries the name string; the
//       sibling helper sub_1657E40 (raised by the thread-creation wrapper
//       at 0x1657ED0) uses the same code with
//       { 0x1000, threadParam, startAddress, 0 }.
//    3. The local __except (EXCEPTION_EXECUTE_HANDLER) swallows the
//       exception if no other handler on the stack handles it, and the
//       function returns 0xFFFFFFFF (all callers treat it as void). If
//       an outer handler consumes it with EXCEPTION_CONTINUE_EXECUTION,
//       execution resumes after RaiseException and it returns 0.
// ============================================================
int SetThreadName(const char *name)
{
    DWORD args[4];

    __try
    {
        args[0] = 0x1000;
        args[1] = (DWORD)name;
        args[2] = 0xFFFFFFFF;
        args[3] = 0;
        RaiseException(0x406D1388, 0, 4, args);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0xFFFFFFFF;
    }
    return 0;
}
