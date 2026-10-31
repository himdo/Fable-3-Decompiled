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

// ---- globals referenced by WinMain ----
// Cross-module flags set when -loadsavegame is given; the save engine reads
// them later. (Move to a shared common/ header once another module needs them.)
static BYTE g_byte_1DBD01B = 0;  // 0x1DBD01B: set 1 when -loadsavegame is given
static BYTE g_byte_1C86BD9 = 0;  // 0x1C86BD9: set 0 when -loadsavegame is given

// ============================================================
//  WinMain  (0x005CF170 - 0x005CF2E1)  -- Fable 3 entry point
//
//  int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
//  1. Single-instance named mutex "Local\FableIII". If a second instance
//     starts (ERROR_ALREADY_EXISTS), focus the existing "Fable 3" window and
//     return -1.
//  2. FrameworkInit, parse -loadsavegame <path>, then run the engine
//     (sub_615740), FrameworkShutdown, release the mutex, return the
//     engine's exit code.
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd)
{
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
            sub_1A019B0(dest);                    // tell the engine which save to load
            g_byte_1DBD01B = 1;
            g_byte_1C86BD9 = 0;
        }
    }
    LocalFree(argv);

    // --- run the game ---
    int result = sub_615740((int)hInstance, lpCmdLine, nShowCmd);
    FrameworkShutdown();   // framework shutdown (refcounted)
    if (hMutex != NULL)
        ReleaseMutex(hMutex);
    return result;
}
