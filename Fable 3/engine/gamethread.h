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

// CNetSaveGameCtor (0x754DE0 - 0x7550B2) -- the 0x2E0-byte (736-byte)
// CNetSaveGame frontend co-op net object's constructor (the counterpart
// of CNetSaveGameDtor): the two shared slots, the 32-byte detail-option
// map, the two 149-byte vec-pair containers, the { 0, 0, -inf, -1 } quad,
// the five 32-byte map containers, the 108-byte screen-params struct,
// the two CExternalAppDataMap objects, the 13-byte flag block, the
// 32-byte CNetPacketCallback, then the six co-op packet registrations
// (518/557/538/539/540/587) on the net manager's packet manager.
// Returns the object. (The 512-byte sub_7550B0 sibling is a separate
// object family.) See gamethread.cpp.
void *CNetSaveGameCtor(void *obj);

// CNetSaveGameDtor (0x627690 - 0x62791B) -- the 0x2E0-byte (736-byte)
// CNetSaveGame frontend co-op net object's (CGameEngine +0xA8; the owner
// of the co-op save-game / loading-screen packet group: CNetFakeLoadScreen
// Packet 518, CNetACSLoadScreenPacket 557, CNetSaveGamePacket 538,
// CNetReceivedSaveGamePacket 539, CNetSetLoadingScreenEnabledPacket 540,
// CNetSetSavingAsAllowedPacket 587) destructor: the two +0x2DC/+0x2D8
// load-screen objects (vtable slot 2), the +0x2C0 CNetPacketCallback
// (CNetPacketCallbackDtor), the seven 32-byte map containers (+0x290 /
// +0x26C / +0x1CC / +0x1AC / +0x18C / +0x16C / +0x008: tree clear +
// sentinel/head free + count zero), the four conditional 12-byte heap
// objects (+0x144 / +0x130 / +0xAC / +0x98), and the two shared slots
// (+0x04, +0x00 -- the tail return). Returns the last ReleaseSharedSlot
// result (discarded by both callers). See gamethread.cpp.
long CNetSaveGameDtor(void *obj);

#endif  // F3_GAMETHREAD_H
