// Fable 3 — game thread module (the game thread entry point).
#ifndef F3_GAMETHREAD_H
#define F3_GAMETHREAD_H

#include "gameengine.h"   // CGameEngine

// 0x615380 - the game thread entry point (called by _beginthread in RunGame
// with a 0x40000-byte stack). Sets up the thread, creates the worker objects
// (each with an SEH cleanup block), loads the shared-memory blob, runs the
// game, and sets the process exit code.
void GameThread(void *a1);

// 0x619AA0 - getter for the 0x6C4-byte game buffer (original address
// 0x1DBD318) that holds the shared-memory blob loaded by GameThread.
void *GetGameBuffer(void);

// 0x61BE00 - the post-game component shutdown pump: builds the
// CStartupComponent, pushes it onto the engine's node list, and ticks it
// until the list resolves (done flag at +0x24).
void CGameEngineShutdown(CGameEngine *obj);

#endif  // F3_GAMETHREAD_H
