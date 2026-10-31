// Fable 3 — save module (save-game loading).
//
// Handles the -loadsavegame <path> command-line option: storing the path the
// engine should load. Will grow into the full save-game subsystem.
#ifndef F3_SAVE_H
#define F3_SAVE_H

#include "common/common.h"

// 0x1A019B0 - assign (src) into a reference-counted smart-pointer slot.
//
// In the original this is __thiscall with this = the slot (a pointer to an
// F3SharedBuffer). It is generic: WinMain uses it for the global save-game
// slot (0x1DBD06C), and GameThread uses it for the CDebugTraceFable's string
// member (to store "FTRACE"). It stores src into *slot (refcounted),
// releasing the previously-stored value.
void SetSaveGamePath(F3SharedBuffer **slot, const char *Source);

// The global save-game-to-load slot (0x1DBD06C): a pointer to an F3SharedBuffer
// holding the save path (null when none is set). WinMain writes it via
// SetSaveGamePath(SaveGameSlot(), path); the engine reads it via SaveGamePath.
F3SharedBuffer **SaveGameSlot(void);

// Return the currently-set save-game path (NULL when none is set). The engine
// reads this to know which save to load.
const char *SaveGamePath(void);

#endif  // F3_SAVE_H
