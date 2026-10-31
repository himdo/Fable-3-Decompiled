// Fable 3 — save module (save-game loading).
//
// Handles the -loadsavegame <path> command-line option: storing the path the
// engine should load. Will grow into the full save-game subsystem.
#ifndef F3_SAVE_H
#define F3_SAVE_H

// 0x1A019B0 - set the save game to load. Stores the (narrow) save path into
// the global shared buffer at 0x1DBD06C (refcounted), releasing the previous
// value. (In the original this is __thiscall with this = &dword_1DBD06C; the
// global slot is the only instance, so it is held here.)
void SetSaveGamePath(char *Source);

// Return the currently-set save-game path (NULL when none is set). The engine
// reads this to know which save to load.
const char *SaveGamePath(void);

#endif  // F3_SAVE_H
