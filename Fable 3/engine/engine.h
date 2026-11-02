// Fable 3 — engine module (game engine init + main run loop).
//
// Declares the engine entry that WinMain calls to start and run the game.
// The implementation is split into focused modules, one per engine object
// or concern:
//   worker.h          CWorkerWin32 (664-byte main worker) + GetWorker
//   manager.h         CManagerWin32 (48-byte manager) + GetManager
//   object_registry.h SEH cleanup blocks + global object pairs (SetDebugTrace)
//   xlo_device.h      XLO device callbacks (OnCreateDevice / OnResetDevice)
//   gameengine.h      CGameEngine (356-byte engine object) ctor
//   gamethread.h      GameThread (0x615380), the game thread entry point
//   thread.cpp        SetThreadName (0x18C5840), the thread-name announce
// engine.cpp holds RunGame (0x615740), the engine init + main run loop.
#ifndef F3_ENGINE_H
#define F3_ENGINE_H

#include "worker.h"
#include "manager.h"
#include "gamethread.h"

// 0x1DBD00C — the process exit code, returned by the main run loop. Set by
// GameThread once the game run (RunFrontEnd, 0x61CB70) finishes.
extern DWORD g_dword_1DBD00C;

// 0x615740 - engine init + main run loop. Allocates the 664-byte worker
// object and constructs it (CWorkerWin32Ctor), spawns the game thread
// (_beginthread(sub_615380, 0x40000, ...)), pumps messages in a
// WaitForSingleObject loop, and returns the engine exit code.
// (The engine-internal callees are stubs for now.)
int RunGame(int hInstance, char *lpCmdLine, int nShowCmd);

// 0x18C5840 - announce the current thread's name by raising the first-chance
// SEH exception 0x406D1388 with args { 0x1000, name, -1, 0 }; a local
// __except swallows it if no outer handler handles it. Every thread entry
// point calls this first. Returns -1 (0xFFFFFFFF) if handled locally, else 0.
int SetThreadName(const char *name);

#endif  // F3_ENGINE_H
