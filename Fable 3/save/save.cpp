// Fable 3 — save module (save-game loading).
//
// sub_1A019B0 is __thiscall with this = &dword_1DBD06C (a global smart
// pointer) and Source = the narrow save path. It allocates a string object
// (sub_1A035C0(16)) and assigns Source into it, releasing the old value.
//
// [STUB] — the string / smart-pointer handling will be added in due course.

#include "save.h"

// ============================================================
//  sub_1A019B0  (0x01A019B0 - 0x01A01A12)  -- set the save game to load
//  [STUB]
// ============================================================
void sub_1A019B0(char *Source)
{
    (void)Source;
}
