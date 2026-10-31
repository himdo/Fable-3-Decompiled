// Fable 3 — save module (save-game loading).
//
// SetSaveGamePath is __thiscall with this = &dword_1DBD06C (a global "smart
// pointer") and Source = the narrow save path. It:
//   1. If Source is a non-empty string, allocates a 16-byte shared-buffer
//      control block (LHAlloc(16)), assigns Source into it (SharedBufferAssign),
//      and sets its refcount to 1.
//   2. Releases the previously stored block: InterlockedDecrement its
//      refcount and, on reaching 0, free it (SharedBufferFree).
//   3. Stores the new block into the global slot.
// The global slot (dword_1DBD06C) is the single "this" the engine uses, so it
// is kept as a module static here; a getter will be added when the engine
// needs to read which save to load.

#include "save.h"
#include "common/common.h"

#include <windows.h>
#include <string.h>

// 0x1DBD06C - the global "save game to load" slot: a pointer to a 16-byte
// F3SharedBuffer holding the save path (null when none is set).
static F3SharedBuffer *g_dword_1DBD06C = 0;

// ============================================================
//  SetSaveGamePath  (0x01A019B0 - 0x01A01A32)  -- assign (src) into a slot
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc, SharedBufferAssign, SharedBufferFree).
//
//  void SetSaveGamePath(F3SharedBuffer **slot, const char *Source)
//
//  __thiscall in the original with this = the slot. Generic: WinMain uses it
//  for the global save-game slot, GameThread for the CDebugTraceFable's string
//  member. Stores Source into *slot (refcounted), releasing the old value.
// ============================================================
void SetSaveGamePath(F3SharedBuffer **slot, const char *Source)
{
    F3SharedBuffer *newBlock = 0;

    if (Source != 0 && *Source != 0)
    {
        newBlock = (F3SharedBuffer *)LHAlloc(16);   // 16-byte control block
        if (newBlock != 0)
        {
            newBlock->data     = 0;
            newBlock->length   = 0;
            newBlock->capacity = 0;

            unsigned int len = (unsigned int)strlen(Source);
            SharedBufferAssign(newBlock, Source, len);   // fill data / length / capacity
            newBlock->refcount = 1;
        }
        else
        {
            newBlock = 0;   // allocation failed: leave unset
        }
    }

    // Release the previously stored block.
    if (*slot != 0)
    {
        if (InterlockedDecrement(&(*slot)->refcount) <= 0 && *slot != 0)
            SharedBufferFree(*slot);   // refcount hit 0: free data + block
        *slot = 0;
    }

    // Store the new block.
    if (newBlock != 0)
        *slot = newBlock;
}

// ============================================================
//  SaveGameSlot  -- the global save slot (0x1DBD06C)
// ============================================================
F3SharedBuffer **SaveGameSlot(void)
{
    return &g_dword_1DBD06C;
}

// ============================================================
//  SaveGamePath  -- getter for the global save slot (0x1DBD06C)
// ============================================================
const char *SaveGamePath(void)
{
    return (g_dword_1DBD06C != 0) ? g_dword_1DBD06C->data : 0;
}
