// Fable 3 (Fable3.exe) — application entry point.
//
// WinMain drives the framework and engine modules. Each subsystem lives in
// its own folder (framework/, engine/, save/) with a header + source, so the
// reconstruction can grow one module at a time. See README.md for the layout
// and the plan for future modules.
//
// Original image layout (x86 PE32, image base 0x00400000, GUI subsystem):
//   0x00401000 - 0x01AD4000  .text   application + CRT / game code (~23.5 MB)
//   0x01AD4000 - 0x01AD5000  .orpc   COM import descriptors
//   0x01AD5000 - 0x01AD576C  .idata  import directory
//   0x01AD576C - 0x01C83000  .rdata  read-only data (vtables, literals)
//   0x01C83000 - 0x02130000  .data   writable data / globals
//   0x02130000 - 0x02133000  .lhmem / .lhtrc / .tls   Lionhead data sections
//   0x02133000 - 0x02245000  .rsrc   resources (~1.1 MB)
//
// Entry chain (from IDA, symbols stripped):
//     entry 0x1523B39:   call ___security_init_cookie; jmp __tmainCRTStartup
//     0x15239BB:         __tmainCRTStartup  ->  call _WinMain@16
//     0x005CF170:        WinMain  (369 bytes)

#define _CRT_SECURE_NO_WARNINGS   // original used wcstombs / _s APIs

#include <windows.h>
#include <wchar.h>
#include <stdlib.h>

#include "framework/framework.h"
#include "engine/engine.h"
#include "save/save.h"

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")

// ---- globals referenced by WinMain ----
// Cross-module flags set when -loadsavegame is given; the save engine reads
// them later. (Move to a shared common/ header once another module needs them.)
static BYTE g_byte_1DBD01B = 0;  // 0x1DBD01B: set 1 when -loadsavegame is given
static BYTE g_byte_1C86BD9 = 0;  // 0x1C86BD9: set 0 when -loadsavegame is given

#ifdef FABLE3_WAIT_FOR_DEBUGGER
// ============================================================
//  WaitForDebuggerOrTimeout  -- debug-build-only convenience (NOT part of the
//  original). Compiled only when FABLE3_WAIT_FOR_DEBUGGER is defined: the debug
//  build (build_vs2008_debug.bat) sets it, the release build does not, so this
//  pause is present in debug builds and entirely absent from release builds.
//
//  The reconstructed Fable3.exe is a small GUI exe that, when launched
//  through the FableLauncher -> F3Secu -> Fable3 chain, starts running (and
//  can exit) before a debugger has time to attach by name. To make attach-by-
//  name reliable, pause at the very start of the process and spin until a
//  debugger is attached (PEB->BeingDebugged set), so the process stays alive
//  and halted-in-user-code long enough to attach to.
//
//  Controlled by the FABLE3_WAIT_DEBUGGER_MS environment variable
//  (inherited through CreateProcess by the launch chain):
//      unset          -> 60000 (default: wait up to 60 s for a debugger)
//      FABLE3_WAIT_DEBUGGER_MS=0    -> no wait (run immediately)
//      FABLE3_WAIT_DEBUGGER_MS=-1   -> wait indefinitely until a debugger attaches
//      FABLE3_WAIT_DEBUGGER_MS=<n>  -> wait up to <n> ms
// ============================================================
static void WaitForDebuggerOrTimeout(void)
{
    long timeoutMs = 60000;   // default: wait up to 60 seconds
    const char *env = getenv("FABLE3_WAIT_DEBUGGER_MS");
    if (env != NULL && *env != '\0')
        timeoutMs = strtol(env, NULL, 10);

    const int indefinite = (timeoutMs < 0);
    if (timeoutMs == 0)       // explicitly disabled
        return;

    OutputDebugStringA("[Fable3] Waiting for debugger to attach...\n");
    const DWORD start = GetTickCount();
    for (;;)
    {
        if (IsDebuggerPresent())
        {
            OutputDebugStringA("[Fable3] Debugger attached; continuing.\n");
            break;
        }
        if (!indefinite && (long)(GetTickCount() - start) >= timeoutMs)
            break;          // timed out without a debugger -> proceed normally
        Sleep(50);
    }
}
#endif // FABLE3_WAIT_FOR_DEBUGGER

// ============================================================
//  WinMain  (0x005CF170 - 0x005CF2E1)  -- Fable 3 entry point
//
//  int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
//  1. Single-instance named mutex "Local\FableIII". If a second instance
//     starts (ERROR_ALREADY_EXISTS), focus the existing "Fable 3" window and
//     return -1.
//  2. FrameworkInit, parse -loadsavegame <path>, then run the engine
//     (RunGame), FrameworkShutdown, release the mutex, return the
//     engine's exit code.
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{
#ifdef FABLE3_WAIT_FOR_DEBUGGER
    // --- debug hook: pause until a debugger attaches (see above) ---
    WaitForDebuggerOrTimeout();
#endif

    // --- single-instance guard ---
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Local\\FableIII");
    if (GetLastError() == ERROR_ALREADY_EXISTS)   // 183: an instance is already running
    {
        HWND hwnd = FindWindowA("Fable 3", NULL);
        if (hwnd != NULL)
        {
            ShowWindow(hwnd, SW_SHOW);      // 5
            ShowWindow(hwnd, SW_RESTORE);   // 9
            SetForegroundWindow(hwnd);
        }
        ReleaseMutex(hMutex);
        return -1;
    }

    FrameworkInit();   // framework init (refcounted)

    // --- parse the command line for -loadsavegame <path> ---
    int nArgs = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    for (int i = 0; i < nArgs; ++i)
    {
        if (wcscmp(argv[i], L"-loadsavegame") == 0)
        {
            char dest[256];
            wcstombs(dest, argv[i + 1], 0xFF);   // wide -> narrow (up to 255 chars)
            SetSaveGamePath(SaveGameSlot(), dest);   // tell the engine which save to load
            g_byte_1DBD01B = 1;
            g_byte_1C86BD9 = 0;
        }
    }
    LocalFree(argv);

    // --- run the game ---
    int result = RunGame((int)hInstance, lpCmdLine, nShowCmd);
    FrameworkShutdown();   // framework shutdown (refcounted)
    if (hMutex != NULL)
        ReleaseMutex(hMutex);
    return result;
}
