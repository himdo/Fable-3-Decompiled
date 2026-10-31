// Fable 3 — save module (save-game loading).
//
// Handles the -loadsavegame <path> command-line option: storing the path the
// engine should load. Will grow into the full save-game subsystem.
#ifndef F3_SAVE_H
#define F3_SAVE_H

// 0x1A019B0 - set the save game to load. Assigns the (narrow) save path to
// the global smart pointer at 0x1DBD06C. [STUB]
void sub_1A019B0(char *Source);

#endif  // F3_SAVE_H
