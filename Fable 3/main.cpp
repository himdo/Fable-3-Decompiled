// Fable 3 (Fable3.exe) — byte-identical decompilation (in progress).
//
// This file mirrors the application code in .text of the original binary.
// Functions are named after their IDA symbols and placed in the same order
// (by address) as in the original image, each annotated with its original
// address range so the decompiled bodies can be slotted in one by one, the
// same way the Fable 3 Launcher was reconstructed:
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
// The genuine entry point (WinMain) is still under analysis; a stub is
// provided so the project compiles and links. Replace the stub body and add
// the game's functions below as they are decompiled.

#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return 0;
}
