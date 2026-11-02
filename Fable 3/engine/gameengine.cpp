// Fable 3 — CGameEngine module implementation (the 356-byte game engine
// object, its +0xC8 sub-object, and the 92-byte CGameVersion sub-object).

#include "gameengine.h"
#include "common/common.h"

// CGameVersion vftable (0x1AF4F00): the original's single slot points to
// sub_72EDB0, which is not implemented yet (never invoked by the code paths
// built so far), so the slot is a placeholder.
static void *g_CGameVersionVtable[1] = { 0 };

// ============================================================
//  CGameVersionReset  (0x72DC80 - 0x72DD06)  -- reset the version lists
//  [NOT byte-identical] placement-dependent (relative calls to LHFree).
//
//  void *CGameVersionReset(void *obj)
//
//  Zeroes the +4 flag byte, then resets each of the three node lists to
//  empty: re-links the sentinel (head.next = head.prev = head), zeroes the
//  count, and LHFrees every list node (the sentinel itself is kept). The
//  return value (the third list's sentinel) is discarded by the sole caller.
// ============================================================
static void *CGameVersionReset(void *obj)
{
    char *base = (char *)obj;
    int   heads[3] = { 0x1C, 0x38, 0x54 };

    base[4] = 0;
    for (int i = 0; i < 3; i++)
    {
        void *head = *(void **)(base + heads[i]);
        void *node = *(void **)head;
        *(void **)head               = head;
        *(void **)((char *)head + 4) = head;
        *(void **)(base + heads[i] + 4) = 0;
        while (node != head)
        {
            void *next = *(void **)node;
            LHFree(node);
            node = next;
        }
    }
    return *(void **)(base + 0x54);
}

// ============================================================
//  CGameVersionCtor  (0x72DD10 - 0x72DDDA)  -- construct a CGameVersion
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc).
//
//  void *CGameVersionCtor(void *obj)
//
//  Stores the vtable, then for each list slot (+0x08, +0x24, +0x40):
//  allocates a 4-byte heap node that back-references the slot, allocates a
//  12-byte self-linked sentinel block, stores it as the list head and zeroes
//  the count. Finally runs CGameVersionReset. Returns this.
// ============================================================
static void *CGameVersionCtor(void *obj)
{
    char *base = (char *)obj;
    int   slots[3] = { 0x08, 0x24, 0x40 };

    *(void **)base = g_CGameVersionVtable;
    for (int i = 0; i < 3; i++)
    {
        void *node = LHAlloc(4);
        if (node)
            *(void **)node = base + slots[i];
        *(void **)(base + slots[i]) = node;

        void *head = LHAlloc(12);
        if (head)
            *(void **)head = head;
        if ((char *)head + 4 != (void *)0)
            *(void **)((char *)head + 4) = head;
        *(void **)(base + slots[i] + 0x14) = head;
        *(void **)(base + slots[i] + 0x18) = 0;
    }

    CGameVersionReset(obj);
    return obj;
}

// ============================================================
//  CGameEngineSubCtor  (0x623360 - 0x6233D4)  -- construct the +0xC8 sub-object
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc).
//
//  void *CGameEngineSubCtor(void *obj, void *a2, void *a3)
//
//  The two stack arguments are unused by the callee (the original cleans
//  them with `retn 8`; the caller passes the address of a 1-byte stack
//  local for both). Allocates a 4-byte heap node back-referencing the
//  sub-object (stored at +0), plus a 44-byte block whose first three words
//  self-link into an empty list and whose bytes +0x28/+0x29 are set to 1
//  (flag bytes); the block pointer is stored at +0x18 with the +0x1C count
//  zeroed. Returns this (the caller discards it).
// ============================================================
static void *CGameEngineSubCtor(void *obj, void *a2, void *a3)
{
    char *sub;
    char *block;

    (void)a2;
    (void)a3;
    sub = (char *)obj;

    void *node = LHAlloc(4);
    if (node)
        *(void **)node = obj;
    *(void **)sub = node;

    block = (char *)LHAlloc(44);
    if (block != (void *)0)
        *(void **)block = 0;
    if (block + 4 != (void *)0)
        *(void **)(block + 4) = 0;
    if (block + 8 != (void *)0)
        *(void **)(block + 8) = 0;
    block[41] = 0;
    block[40] = 1;
    *(void **)(sub + 0x18) = block;
    block[41] = 1;
    *(void **)(block + 4) = block;
    *(void **)block       = block;
    *(void **)(block + 8) = block;
    *(void **)(sub + 0x1C) = 0;
    return obj;
}

// 0x1DBD9DC: the current game engine object (set by CGameEngineCtor, cleared by the dtor).
static void *g_GameEngine = 0;

// ============================================================
//  CGameEngineCtor  (0x61C530 - 0x61C66C)  -- construct the game engine
//  [NOT byte-identical] placement-dependent (relative calls to LHAlloc,
//  the CS init thunk, CGameEngineSubCtor, CGameVersionCtor) and it writes
//  the high-VA global dword_1DBD9DC.
//
//  void *CGameEngineCtor(void *obj)
//
//  Initializes the 356-byte CGameEngine in place: the back-referencing
//  heap node (+0x00), the self-linked node list (+0x14, sentinel at +0x20,
//  last = the node), the +0x30 sub-object, the critical sections at +0x48
//  and +0x60, the zeroed object-pointer members (+0x78..+0xC4), the +0xC8
//  sub-object (CGameEngineSubCtor), the CGameVersion at +0xEC, the third
//  critical section at +0x148, and the trailing byte. Publishes the object
//  into dword_1DBD9DC. Returns this.
// ============================================================
void *CGameEngineCtor(void *obj)
{
    CGameEngine *g    = (CGameEngine *)obj;
    char        *base = (char *)obj;
    char         tmp;    // the original passes its address (twice) to CGameEngineSubCtor

    g->heapNode = LHAlloc(4);
    if (g->heapNode)
        *(void **)g->heapNode = obj;

    void *head = LHAlloc(12);
    if (head)
        *(void **)head = head;
    if ((char *)head + 4 != (void *)0)
        *(void **)((char *)head + 4) = head;
    g->listFirst    = head;
    g->listCount    = 0;
    g->listLast     = 0;
    g->listSentinel = head;
    g->listLast     = g->heapNode;

    base[0x24] = 0;
    base[0x25] = 0;

    g->sub0x30Node = LHAlloc(4);
    if (g->sub0x30Node)
        *(void **)g->sub0x30Node = base + 0x30;
    g->sub0x30A = 0;
    g->sub0x30B = 0;
    g->sub0x30C = 0;

    InitializeCriticalSection(&g->cs1);
    InitializeCriticalSection(&g->cs2);

    g->member0x78 = 0;
    g->member0x7C = 0;
    g->member0x80 = 0;
    g->member0x84 = 0;
    g->member0x88 = 0;
    g->member0x8C = 0;
    g->member0x90 = 0;
    g->member0x94 = 0;
    g->member0x98 = 0;
    g->member0x9C = 0;
    g->member0xA0 = 0;
    g->member0xA4 = 0;
    g->member0xA8 = 0;
    g->member0xAC = 0;
    g->member0xB0 = 0;
    g->member0xB4 = 0;
    g->member0xB8 = 0;

    g->member0xBC = 0;
    g->member0xC0 = 0;
    g->member0xC4 = 0;

    CGameEngineSubCtor(base + 0xC8, &tmp, &tmp);

    g->member0xE8 = 0;
    CGameVersionCtor(&g->version);

    InitializeCriticalSection(&g->cs3);
    g->member0x160 = 0;

    g_GameEngine = obj;   // dword_1DBD9DC
    return obj;
}
