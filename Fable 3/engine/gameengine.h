// Fable 3 — CGameEngine module (the 356-byte game engine object).
//
// The engine object lives in a 356-byte stack buffer inside GameThread
// (ArgList in IDA). It is constructed by CGameEngineCtor, run by RunFrontEnd
// (0x61CB70), and torn down by CGameEngineDestroy (0x61C670). The current
// instance is published into the global dword_1DBD9DC (read back by the
// getter sub_619A90).
#ifndef F3_GAMEENGINE_H
#define F3_GAMEENGINE_H

#include <windows.h>

// ============================================================
//  CGameVersion  -- the version sub-object (92 bytes)
//
//  Embedded at +0xEC of CGameEngine. Holds three self-linked node
//  lists: each list slot at +0x08 / +0x24 / +0x40 gets a 4-byte heap
//  node that back-references the slot, a 12-byte self-linked sentinel
//  block stored as the list head (+0x14), and a zero count (+0x18).
//  The lists therefore sit at +0x1C/+0x20, +0x38/+0x3C, +0x54/+0x58.
// ============================================================
typedef struct CGameVersion
{
    void *   vtable;       // +0x00  single-slot vftable (0x1AF4F00)
    char     flag;         // +0x04  zeroed by CGameVersionReset
    char     _pad1[3];     // +0x05 .. +0x08
    void *   node1;        // +0x08  4-byte heap node -> this+8
    char     _pad2[16];    // +0x0C .. +0x1C
    void *   list1Head;    // +0x1C  12-byte self-linked sentinel block
    void *   list1Count;   // +0x20  node count
    void *   node2;        // +0x24  4-byte heap node -> this+0x24
    char     _pad3[16];    // +0x28 .. +0x38
    void *   list2Head;    // +0x38
    void *   list2Count;   // +0x3C
    void *   node3;        // +0x40  4-byte heap node -> this+0x40
    char     _pad4[16];    // +0x44 .. +0x54
    void *   list3Head;    // +0x54
    void *   list3Count;   // +0x58
} CGameVersion;

// Compile-time check: the sub-object must be exactly 92 bytes.
typedef char CGameVersionSizeCheck[(sizeof(CGameVersion) == 92) ? 1 : -1];

// ============================================================
//  CGameEngine  -- the 356-byte game engine object
//
//  Layout:
//    +0x00  heapNode    4-byte heap node back-referencing the object
//    +0x14  node list   first / count / last / sentinel
//    +0x30  sub-object  node + members, critical section at +0x48
//    +0x48  cs1         CRITICAL_SECTION (24 bytes)
//    +0x60  cs2         CRITICAL_SECTION (24 bytes)
//    +0x78  ..          object-pointer members zeroed by the ctor
//    +0xC8  sub-object  CGameEngineSubCtor (node at +0xC8, 44-byte block at +0xE0)
//    +0xEC  version     CGameVersion sub-object (92 bytes)
//    +0x148 cs3         CRITICAL_SECTION (24 bytes)
//    +0x160 trailing byte
// ============================================================
typedef struct CGameEngine
{
    void *           heapNode;      // +0x00
    char             _pad1[16];     // +0x04 .. +0x14
    void *           listFirst;     // +0x14
    void *           listCount;     // +0x18
    void *           listLast;      // +0x1C
    void *           listSentinel;  // +0x20
    char             _pad2[12];     // +0x24 .. +0x30 (bytes +0x24/+0x25 zeroed by the ctor)
    void *           sub0x30Node;   // +0x30  4-byte heap node -> this+0x30
    char             _pad3[8];      // +0x34 .. +0x3C
    void *           sub0x30A;      // +0x3C
    void *           sub0x30B;      // +0x40
    void *           sub0x30C;      // +0x44
    CRITICAL_SECTION cs1;           // +0x48 .. +0x5F
    CRITICAL_SECTION cs2;           // +0x60 .. +0x77
    void *           member0x78;    // +0x78
    void *           member0x7C;    // +0x7C
    void *           member0x80;    // +0x80
    char             member0x84;    // +0x84
    char             _pad4[3];      // +0x85 .. +0x88
    void *           member0x88;    // +0x88
    void *           member0x8C;    // +0x8C
    void *           member0x90;    // +0x90
    void *           member0x94;    // +0x94
    void *           member0x98;    // +0x98
    void *           member0x9C;    // +0x9C
    void *           member0xA0;    // +0xA0
    void *           member0xA4;    // +0xA4
    void *           member0xA8;    // +0xA8
    void *           member0xAC;    // +0xAC
    void *           member0xB0;    // +0xB0
    void *           member0xB4;    // +0xB4
    void *           member0xB8;    // +0xB8
    void *           member0xBC;    // +0xBC
    void *           member0xC0;    // +0xC0
    void *           member0xC4;    // +0xC4
    void *           sub0xC8Node;   // +0xC8
    char             _pad6[12];     // +0xCC .. +0xD8
    char             _pad7[8];      // +0xD8 .. +0xE0
    void *           sub0xE0Block;  // +0xE0  the 44-byte block
    void *           sub0xE4Count;  // +0xE4
    void *           member0xE8;    // +0xE8
    CGameVersion     version;       // +0xEC .. +0x147
    CRITICAL_SECTION cs3;           // +0x148 .. +0x15F
    char             member0x160;   // +0x160
    char             _pad8[3];      // +0x161 .. +0x164
} CGameEngine;

// Compile-time check: the object must be exactly 356 bytes (the ArgList buffer).
typedef char CGameEngineSizeCheck[(sizeof(CGameEngine) == 356) ? 1 : -1];

// CGameEngineCtor (0x61C530) -- construct the 356-byte game engine in place.
void *CGameEngineCtor(void *obj);

// 0x1DBD9DC -- publish (or clear with 0) the current engine instance; the
// setter half of the global written by CGameEngineCtor and cleared by
// CGameEngineDestroy.
void SetGameEngineInstance(void *obj);

// sub_619A90 (0x619A90 - 0x619A96) -- the current game engine getter.
// Returns the instance published into dword_1DBD9DC (0 or the engine).
void *GetGameEngine(void);

#endif  // F3_GAMEENGINE_H
