// Fable 3 — game thread module (the game thread entry point).
#ifndef F3_GAMETHREAD_H
#define F3_GAMETHREAD_H

// 0x615380 - the game thread entry point (called by _beginthread in RunGame
// with a 0x40000-byte stack). Sets up the thread, creates the worker objects
// (each with an SEH cleanup block), loads the shared-memory blob, runs the
// game, and sets the process exit code.
void GameThread(void *a1);

#endif  // F3_GAMETHREAD_H
