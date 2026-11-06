// Fable 3 — frontend run module (RunFrontEnd, 0x61CB70).
//
// RunFrontEnd is the big game start-up / frontend driver. GameThread
// constructs the 356-byte CGameEngine in place (CGameEngineCtor) and then
// calls RunFrontEnd(engine, arg). It:
//   - sets up the XNA notification listener, probes Steam ("Data/ThisIsSteam"),
//   - builds the CDiskFileWin32 / CAFile file objects and the locale message
//     string,
//   - dispatches the manager's "run the game" vtable slot with the parsed
//     command-line / locale arguments,
//   - drives the XNA sign-in / guide / message-box UI loop (skipped when the
//     user is already signed in),
//   - on success creates the render device + engine sub-objects, installs the
//     worker callbacks, loads the intro video, and starts the 3D thread.
//
// It is thiscall + `retn 4` in the original (obj in ECX, one int on the stack,
// result byte in AL). Declared here as a plain free function taking obj as its
// first parameter to match the project convention for non-byte-identical
// thiscall methods.
#ifndef F3_GAMEMAIN_H
#define F3_GAMEMAIN_H

#include "common/common.h"   // F3SharedBuffer

// char RunFrontEnd(void *obj, int arg)   (obj = the CGameEngine, 356 bytes)
char RunFrontEnd(void *obj, int arg);

// F3SharedBuffer **MakeAbnormalShutdownPath(F3SharedBuffer **obj)   (0x67C930)
// settings-directory slot + "abnormal_shutdown.tmp" (shared by the worker module)
F3SharedBuffer **MakeAbnormalShutdownPath(F3SharedBuffer **obj);

// 0x18EB1A0 - the 0x38-byte CS-guarded sorted map object's destructor (the
// counterpart of the static CSortedMapCtor, 0x18EADB0, in gamemain.cpp):
// DeleteCriticalSection at +0x20, the tree + sentinel destroy
// (SortedMapTreeDestroy, 0x578A40), and the owner-tag LHFree.
void CSortedMapDtor(void *obj);

// 0x18FA3E0 - the global heap-manager object getter (dword_2075ED8; takes no
// argument in the original). Remaining stub (returns 0 until the heap
// manager is implemented).
void *GetHeapManager(void);

// 0x18FA3D0 - thiscall getter for the heap manager's +0x1C member. Remaining
// stub (returns 0 until the heap manager is implemented).
void *GetHeapManagerSlot1C(void *hm);

// 0x1A01270 - copy-assign one shared slot onto another with refcounting
// (this / src are both the addresses of F3SharedBuffer * fields).
F3SharedBuffer **CopySharedSlot(F3SharedBuffer **thisSlot, F3SharedBuffer **srcSlot);

// 0x19FFAB0 - build a fresh ANSI slot from the low byte of each wide code
// unit of the source wide block and store it in the out slot.
F3SharedBuffer **AnsiFromWideSlot(F3SharedBuffer *wide, F3SharedBuffer **out);

#endif  // F3_GAMEMAIN_H
