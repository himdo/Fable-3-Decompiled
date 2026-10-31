// Fable 3 — engine module (game engine init + main run loop).
//
// Declares the engine entry that WinMain calls to start and run the game.
// This is the largest module; it will grow into the full engine as it is
// decompiled (see README.md).
#ifndef F3_ENGINE_H
#define F3_ENGINE_H

// 0x615740 - engine init + main run loop. Allocates the 664-byte worker
// object and constructs it (CWorkerWin32Ctor), spawns the game thread
// (_beginthread(sub_615380, 0x40000, ...)), pumps messages in a
// WaitForSingleObject loop, and returns the engine exit code.
// (The engine-internal callees are stubs for now.)
int RunGame(int hInstance, char *lpCmdLine, int nShowCmd);

#endif  // F3_ENGINE_H
