// Fable 3 — game thread module implementation (GameThread, 0x615380, plus
// its module-private globals, SEH handlers, and stub callees).

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // original used _snwprintf
#endif

#include "gamethread.h"
#include "engine.h"               // SetThreadName
#include "framework/framework.h"  // FrameworkInit
#include "common/common.h"        // LHAlloc / LHFree
#include "save/save.h"            // SetSaveGamePath
#include "object_registry.h"      // F3ObjReg / SetDebugTrace / F3ReleaseSehRef
#include "manager.h"              // SetManager / DisposeManager / sub_18F7160
#include "gameengine.h"           // CGameEngineCtor

#include <windows.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>

// 0x1DBD008: 1 when the shared-memory blob was loaded, 0 otherwise.
static BYTE  g_byte_1DBD008 = 0;
// 0x1DBD010 / 0x1DBD014: objects whose strings form the error message box
// (body / title) shown when the shared-memory blob is missing.
static DWORD g_dword_1DBD010 = 0;
static DWORD g_dword_1DBD014 = 0;

// Placeholder vtables for the two objects created on the game thread.
static void *g_CFableWindowsDebugHandlerVtable[4] = { 0 };
static void *g_CDebugTraceFableVtable[4]          = { 0 };

// The game buffer (0x6C4 bytes) that the shared-memory blob is loaded into.
// Its original address is 0x1DBD318 (a high-VA data global).
static char g_gameBuffer[0x6C4];

// ---- SEH handlers for the three objects (called when their refcount hits 0) ----
// ============================================================
//  LHFreeHandler  (0x00401D20 - 0x00401D2B)  -- SEH cleanup handler that
//  frees its object on the game heap
//  [NOT byte-identical] placement-independent, but refs the heap table
//  (0x02075B24) at the original high VA not present in this build.
//
//  int LHFreeHandler(int obj)
//
//  Byte-identical to LHFree (0x1A032C0): the 11-byte thunk
//  `mov ecx, dword_2075B24; mov edx, [ecx+10h]; jmp edx` that tail-calls
//  the heap table's free slot. Stored as the `handler` of a 12-byte SEH
//  block ({ refcount, handler, arg }); when the block's refcount hits 0
//  the engine calls handler(arg), freeing `arg` via the game heap.
//  Referenced by GameThread (0x615380, CFableWindowsDebugHandler pair)
//  and by sub_42DE70 (generic object + SEH-block registration).
// ============================================================
static int LHFreeHandler(int arg) { LHFree((void *)arg); return 0; }

// ============================================================
//  JmpLHFree  (0x615870 - 0x615875)  -- 5-byte `jmp LHFree` thunk, stored as
//  the CDebugTraceFable SEH block's handler
//  [NOT byte-identical] placement-dependent: the near `jmp LHFree` is rebased
//  to this build's LHFree (the original displacement 0x013EDA4B targets
//  LHFree at its original VA 0x1A032C0).
//
//  A raw tail jump with no arg handling of its own: the SEH release code
//  pushes the object pointer and calls the handler, so LHFree receives that
//  pointer as its own (cdecl) parameter and frees the CDebugTraceFable via
//  the game heap.
// ============================================================
static int __declspec(naked) JmpLHFree(int)
{
    __asm { jmp LHFree }
}

// ---- other engine callees (stubs for now) -----------------------------------
// ============================================================
//  GetGameBuffer  (0x00619AA0 - 0x00619AA6)  -- getter for the game buffer
//  [NOT byte-identical] placement-independent (no calls / relative jumps),
//  but refs the game buffer at its original high VA (0x01DBD318), not present
//  in this build.
//
//  void *GetGameBuffer(void)
//
//  A 6-byte getter: `mov eax, offset <game buffer>; retn`. GameThread zeroes
//  the buffer and fills it from the shared-memory blob (-=[SMS_<exename>_SMS]=-);
//  the engine read paths (sub_6488A0, sub_9F2770, sub_9F31F0, sub_9F38A0,
//  sub_9F4B50) read fields out of it (e.g. +68, +108, +680, +1728).
// ============================================================
void *GetGameBuffer(void)
{
    return g_gameBuffer;   // originally 0x1DBD318
}

static int   *sub_6195C0(int a1) { (void)a1; return 0; }                // get a per-LCID localized-string entry
static int   sub_61CB70(void *argList, int a2) { (void)argList; (void)a2; return 1; }  // run the game (success)
static void  sub_61BE00(void *obj) { (void)obj; }                       // cleanup (part 1)
static void  sub_61AE70(void *obj) { (void)obj; }                       // cleanup (part 2)
static void  sub_61C670(void *obj) { (void)obj; }                       // cleanup (part 3)
static long  sub_401700(F3ObjReg *reg) { (void)reg; return 0; }         // free the debug handler

// ============================================================
//  HeapStressTest  (0x18986E0 - 0x189888C)  -- engine heap sanity check
//  [NOT byte-identical] placement-dependent (relative call to LHAlloc,
//  indirect heap frees, /GS stack cookie, 0x3E8-iteration loops).
//
//  void *HeapStressTest(void)
//
//  The engine's heap self-test: allocate 1000 (0x3E8) 16-byte blocks and free
//  them all again, stressing the allocator. The original builds an intrusive
//  doubly-linked list over the blocks (list head embedded at offset 4 of each
//  block, first block held in the global dword_20B5FC8) and tears it down by
//  removing the head and tail block each iteration, freeing the head before the
//  tail -- which yields the exact allocation/free sequence reproduced here.
//  The return value is discarded by the sole caller (GameThread).
// ============================================================
static void *HeapStressTest(void)
{
    static void *g_head = 0;        // dword_20B5FC8: first allocated block
    void        *blocks[0x3E8];     // the 1000 blocks (the list is internal)

    for (int i = 0; i < 0x3E8; i++)
        blocks[i] = LHAlloc(16);
    g_head = blocks[0];

    // Remove head + tail each iteration (head freed before tail): the free
    // order is N1, N1000, N2, N999, ..., N500, N501.
    for (int k = 1; k <= 0x1F4; k++)   // 0x1F4 = 500 iterations
    {
        LHFree(blocks[k - 1]);         // head block (Nk)
        LHFree(blocks[0x3E8 - k]);     // tail block (N(1001-k))
    }
    return g_head;
}

// ============================================================
//  GameThread  (0x00615380 - 0x00615740)  -- the game thread entry point
//  [NOT byte-identical] placement-dependent (many relative calls to engine
//  callees and Win32/CRT functions).
//
//  void GameThread(void *a1)
//
//  Called by _beginthread (RunGame) with a 0x40000-byte stack. It:
//    1. Names the thread "GameThread", sets heap info (class 1), FrameworkInit.
//    2. Creates a debug handler, a 48-byte object, and a CDebugTraceFable, each
//       with an SEH cleanup block. The debug handler's SEH block is kept
//       (released at the end by sub_401700); the other two release their local
//       reference immediately (leaving the global as the owner).
//    3. Clears the game buffer and loads a shared-memory blob named after the
//       exe (-=[SMS_<exename>_SMS]=-). If the blob is missing, shows a message
//       box and ExitProcess(1).
//    4. Runs the game (CGameEngineCtor, sub_61CB70) and sets the exit code
//       (dword_1DBD00C), then cleans up (sub_61C670, sub_401700).
// ============================================================
void GameThread(void *a1)
{
    int arg = (int)a1;

    // 1. Thread setup + framework init.
    HeapSetInformation(NULL, (HEAP_INFORMATION_CLASS)1, NULL, 0);
    SetThreadName("GameThread");   // announce the thread name (first-chance SEH raise)
    FrameworkInit();

    // 2. Debug handler (4-byte) + SEH block (LHFreeHandler). The SEH block is
    //    kept (released at the end by sub_401700), not released here.
    F3ObjReg reg1;
    reg1.object = LHAlloc(4);
    if (reg1.object)
        *(void **)reg1.object = g_CFableWindowsDebugHandlerVtable;
    reg1.sehBlock = 0;
    if (reg1.object && (reg1.sehBlock = LHAlloc(12)) != 0)
    {
        F3SehBlock *sb = (F3SehBlock *)reg1.sehBlock;
        sb->refcount = 1;
        sb->handler  = LHFreeHandler;
        sb->arg      = (int)reg1.object;
    }
    SetDebugHandler(&reg1);   // register the CFableWindowsDebugHandler (0x1A02DD0)

    // 3. 48-byte object (sub_18F7160) + SEH block (DisposeManager); release local.
    F3ObjReg reg2;
    reg2.object = LHAlloc(48);
    reg2.object = reg2.object ? sub_18F7160(reg2.object) : 0;
    reg2.sehBlock = 0;
    if (reg2.object && (reg2.sehBlock = LHAlloc(12)) != 0)
    {
        F3SehBlock *sb = (F3SehBlock *)reg2.sehBlock;
        sb->refcount = 1;
        sb->handler  = DisposeManager;
        sb->arg      = (int)reg2.object;
    }
    SetManager(&reg2);
    F3ReleaseSehRef(reg2.sehBlock);

    // 4. CDebugTraceFable (8-byte) + SEH block (JmpLHFree); release local.
    F3ObjReg reg3;
    reg3.object = LHAlloc(8);
    if (reg3.object)
    {
        *(void **)reg3.object = g_CDebugTraceFableVtable;
        InitSharedSlot((F3SharedBuffer **)((char *)reg3.object + 4));   // construct the +4 string slot
        SetSaveGamePath((F3SharedBuffer **)(char *)reg3.object + 1, "FTRACE");
    }
    reg3.sehBlock = 0;
    if (reg3.object && (reg3.sehBlock = LHAlloc(12)) != 0)
    {
        F3SehBlock *sb = (F3SehBlock *)reg3.sehBlock;
        sb->refcount = 1;
        sb->handler  = JmpLHFree;
        sb->arg      = (int)reg3.object;
    }
    SetDebugTrace(&reg3);
    F3ReleaseSehRef(reg3.sehBlock);

    // 5. Heap sanity check + clear the game buffer.
    HeapStressTest();
    if (GetGameBuffer() != 0)
        memset(GetGameBuffer(), 0, 0x6C4);

    // 6. Load the shared-memory blob (named after the exe).
    WCHAR Filename[0x800];
    WCHAR Buffer[0x800];
    memset(Filename, 0, sizeof(Filename));
    memset(Buffer, 0, sizeof(Buffer));
    g_byte_1DBD008 = 0;
    if (GetGameBuffer() != 0)
    {
        HMODULE hMod = GetModuleHandleA(NULL);
        if (GetModuleFileNameW(hMod, Filename, 0x800) != 0)
        {
            const WCHAR *p = wcsrchr(Filename, L'\\');
            if (p == 0)
                p = wcsrchr(Filename, L'/');
            if (p != 0)
            {
                _snwprintf(Buffer, 0x800, L"-=[SMS_%s_SMS]=-", p + 1);
                HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, Buffer);
                if (hMap != 0)
                {
                    BYTE *view = (BYTE *)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
                    if (view != 0)
                    {
                        if (GetGameBuffer() != 0)
                            memcpy(GetGameBuffer(), view, 0x6C4);
                        if (UnmapViewOfFile(view))
                            g_byte_1DBD008 = 1;
                    }
                    CloseHandle(hMap);
                }
            }
        }
    }

    // 7. If the blob was not loaded, show a message box and exit.
    if (g_byte_1DBD008 == 0)
    {
        int v19 = (int)GetManager();
        const WCHAR *title = GetLocalizedString((const WCHAR ***)sub_6195C0(g_dword_1DBD010));
        const WCHAR *body  = GetLocalizedString((const WCHAR ***)sub_6195C0(g_dword_1DBD014));
        HWND hwnd = 0;
        if (v19)
        {
            void **vtable = (void **)*(void ***)v19;
            int (__thiscall *getWindow)(int) =
                (int (__thiscall *)(int))vtable[8];   // vtable slot at offset 32
            hwnd = (HWND)getWindow(v19);
        }
        MessageBoxW(hwnd, body, title, MB_ICONERROR);   // 0x10
        ExitProcess(1);
    }

    // 8. Run the game.
    char ArgList[356];
    CGameEngineCtor(ArgList);
    if (sub_61CB70(ArgList, arg) != 0)
    {
        sub_61BE00(ArgList);
        sub_61AE70(ArgList);
        g_dword_1DBD00C = 0;   // process exit code (returned by RunGame)
    }
    else
    {
        g_dword_1DBD00C = 1;
    }
    sub_61C670(ArgList);

    // 9. Free the debug handler.
    sub_401700(&reg1);
}
