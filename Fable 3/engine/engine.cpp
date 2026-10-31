// Fable 3 — engine module (game engine init + main run loop).
//
// sub_615740 is the main game function. Its full decompile:
//   - allocates the 664-byte engine object (sub_1A035C0(664))
//   - calls sub_18F7040(hInstance, nShowCmd) to start the real engine
//   - sets up a 12-byte SEH handler (handler = sub_615880)
//   - registers thread callbacks (sub_6152F0, sub_615340)
//   - spawns the game thread (_beginthread(sub_615380, 0x40000, ...))
//   - pumps messages (sub_18F6A40 / sub_18F8600) while the thread runs
//   - returns the global dword_1DBD00C as the process exit code
//
// [STUB] — the callees above will be added to this module in due course.

#include "engine.h"

#include <windows.h>

// ============================================================
//  sub_615740  (0x00615740 - 0x006157F9)  -- engine init + main run loop
//  [STUB]
// ============================================================
int sub_615740(int hInstance, char *lpCmdLine, int nShowCmd)
{
    (void)hInstance;
    (void)lpCmdLine;
    (void)nShowCmd;
    return 0;
}
