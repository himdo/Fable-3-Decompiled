// Fable 3 (Fable3.exe) — decompilation (in progress).
//
// This file mirrors the application code in .text of the original binary.
// Functions are named after their IDA symbols and annotated with their
// original address range so the decompiled bodies can be slotted in one by
// one, the same way the Fable 3 Launcher was reconstructed:
//
//     // sub_XXXXXX  (0x<start> - 0x<end>)  -- description
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
//     0x005CF170:        WinMain  (369 bytes)  -- decompiled below
//
// This is a FUNCTIONAL first draft: the single-instance logic, the
// -loadsavegame command-line parsing, and the existing-window focus logic are
// implemented. The framework init/shutdown and the engine init/run are stubs
// to be filled in as they are decompiled.

#define _CRT_SECURE_NO_WARNINGS   // original used wcstombs (pre-secure-CRT API)

#include <windows.h>
#include <wchar.h>

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

// ---- forward declarations of the functions WinMain calls (stubs below) ----
int  sub_1A02B50(void);                                    // framework init (refcounted)
void sub_1A019B0(char *Source);                            // set save game to load
int  sub_615740(int hInstance, char *lpCmdLine, int nShowCmd); // engine init + run
int  sub_1A02E70(void);                                    // framework shutdown (refcounted)

// ---- globals referenced by WinMain ----
static BYTE g_byte_1DBD01B = 0;  // 0x1DBD01B: set 1 when -loadsavegame is given
static BYTE g_byte_1C86BD9 = 0;  // 0x1C86BD9: set 0 when -loadsavegame is given

// ============================================================
//  WinMain  (0x005CF170 - 0x005CF2E1)  -- Fable 3 entry point
//
//  int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
//  1. Single-instance named mutex "Local\FableIII". If a second instance
//     starts (ERROR_ALREADY_EXISTS), focus the existing "Fable 3" window and
//     return -1.
//  2. Framework init (sub_1A02B50), parse -loadsavegame <path>, then run the
//     engine (sub_615740), framework shutdown (sub_1A02E70), release the
//     mutex, return the engine's exit code.
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

    sub_1A02B50();   // framework init (refcounted)

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
    sub_1A02E70();   // framework shutdown (refcounted)
    if (hMutex != NULL)
        ReleaseMutex(hMutex);
    return result;
}

// ============================================================
//  sub_1A02B50  (0x01A02B50 - 0x01A02B76)  -- framework init (refcounted)
//
//  if (dword_2075C44 != 0) ++dword_2075C44;
//  else { sub_1A02870(); ++dword_2075C44; byte_2075B33 = 1; return 1; }
//  [STUB]
// ============================================================
int sub_1A02B50(void)
{
    return 0;
}

// ============================================================
//  sub_1A019B0  (0x01A019B0 - 0x01A01A12)  -- set the save game to load
//
//  __thiscall with this = &dword_1DBD06C (a global smart-pointer) and
//  Source = the narrow save path. Allocates a string object
//  (sub_1A035C0(16)) and assigns Source into it, releasing the old value.
//  [STUB]
// ============================================================
void sub_1A019B0(char *Source)
{
    (void)Source;
}

// ============================================================
//  sub_615740  (0x00615740 - 0x006157F9)  -- engine init + main run loop
//
//  int __cdecl sub_615740(int hInstance, char *lpCmdLine, int nShowCmd)
//  Allocates the 664-byte engine object (sub_1A035C0(664)) and starts it via
//  sub_18F7040; wraps it in a shared_ptr (deleter sub_615880); spawns the
//  game thread (_beginthread(sub_615380, 0x40000, ...)); pumps messages in a
//  WaitForSingleObject loop; returns the engine exit code (dword_1DBD00C).
//  [STUB]
// ============================================================
int sub_615740(int hInstance, char *lpCmdLine, int nShowCmd)
{
    (void)hInstance;
    (void)lpCmdLine;
    (void)nShowCmd;
    return 0;
}

// ============================================================
//  sub_1A02E70  (0x01A02E70 - 0x01A02E9B)  -- framework shutdown (refcounted)
//
//  if (byte_2075B33) { if (--dword_2075C44 == 0) { fclose(Stream); sub_4017B0(); byte_2075B33 = 0; } }
//  else __debugbreak();
//  [STUB]
// ============================================================
int sub_1A02E70(void)
{
    return 0;
}
