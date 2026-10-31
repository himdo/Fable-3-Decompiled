// Fable 3 — common module (engine-wide memory / buffer helpers).

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// HeapSetInformation compatibility class is 0 in the original (see disasm of
// LHAlloc: `push 0 ; HeapInformationClass`); define it locally so the
// build does not depend on a newer SDK header exposing the name.
#ifndef HeapCompatibilityInformation
#define HeapCompatibilityInformation 0
#endif

// ---- custom heap allocator table (dword_2075B24) --------------------------
// Function-pointer table the engine uses for all heap traffic. Only the two
// slots the current code touches are backed by real (malloc/free) routines;
// the rest are left null. Layout (x86, 4-byte slots):
//   +0  (unused here)
//   +4  (unused here)
//   +8  allocate(size)      <- LHAlloc calls this
//   +12 (unused here)
//   +16 free(ptr)           <- SharedBufferAssign / SharedBufferFree call this
static void *f3_heap_alloc(unsigned int size) { return malloc(size); }
static void  f3_heap_free(void *p) { free(p); }
typedef struct F3HeapTable
{
    void *                 pad0;
    void *                 pad4;
    void *(*alloc)(unsigned int);
    void *                 pad12;
    void  (*free)(void *);
} F3HeapTable;
static F3HeapTable g_heapTable = { 0, 0, f3_heap_alloc, 0, f3_heap_free };

// ---- globals referenced by the allocator ----------------------------------
static BYTE  g_byte_2075B30  = 0;  // 0x2075B30: heap info initialized
static DWORD g_dword_1DADD10 = 0;  // 0x1DADD10: verbose / OOM-report flag

// ============================================================
//  OutOfMemory  (0x01A034F0 - 0x01A0356C)  -- report an out-of-memory condition
//
//  void OutOfMemory(const wchar_t *context, unsigned int size)
//
//  Locks a private critical section, calls the optional pre-callback
//  (dword_2075B20), formats and writes to the debugger:
//      "Out Of Memory allocating <size> in <context>\n"
//  then calls the optional post-callback (dword_2075B2C) with (0, 0) and breaks.
//
//  Only runs when an allocation fails AND the verbose flag (0x1DADD10) is set.
// ============================================================
static CRITICAL_SECTION g_OomCs;
struct OomCsInit
{
    OomCsInit()  { InitializeCriticalSection(&g_OomCs); }
    ~OomCsInit() { DeleteCriticalSection(&g_OomCs); }
};
static OomCsInit g_OomCsInit;

static void (*g_dword_2075B20)(void)          = 0;   // 0x2075B20: optional pre-callback
static void (*g_dword_2075B2C)(void *, void *) = 0;   // 0x2075B2C: optional post-callback

static void OutOfMemory(const wchar_t *context, unsigned int size)
{
    WCHAR buf[512];

    EnterCriticalSection(&g_OomCs);
    if (g_dword_2075B20 != 0)
        g_dword_2075B20();
    swprintf_s(buf, 0x1FF, L"Out Of Memory allocating %u in %s\n", size, context);
    OutputDebugStringW(buf);
    if (g_dword_2075B2C != 0)
        g_dword_2075B2C(0, 0);
    DebugBreak();
    LeaveCriticalSection(&g_OomCs);
}

// ============================================================
//  LHAlloc  (0x1A035C0 - 0x1A03627)  -- game heap allocator
// ============================================================
void *LHAlloc(unsigned int size)
{
    if (g_byte_2075B30 == 0)
    {
        DWORD compat = 2;
        HANDLE hHeap = GetProcessHeap();
        HeapSetInformation(hHeap, (HEAP_INFORMATION_CLASS)HeapCompatibilityInformation, &compat, 4);
        g_byte_2075B30 = 1;
    }

    void *p = g_heapTable.alloc(size);
    if (p == 0 && (g_dword_1DADD10 & 1))
        OutOfMemory(L"LHBaseMemory::Alloc", size);
    return p;
}

// ============================================================
//  LHFree  -- the game heap free (counterpart to LHAlloc)
// ============================================================
void LHFree(void *p)
{
    if (p != 0)
        g_heapTable.free(p);
}

// ============================================================
//  SharedBufferAssign  (0x403FE0 - 0x404053)  -- assign (src, len) into the buffer
// ============================================================
void SharedBufferAssign(F3SharedBuffer *this_, const char *src, unsigned int len)
{
    if (this_->data != 0)
    {
        g_heapTable.free(this_->data);
        this_->data = 0;
    }

    unsigned int aligned = (len + 4) & 0xFFFFFFFCu;
    char *buf = (char *)LHAlloc(aligned);

    this_->capacity = ((aligned ^ this_->capacity) & 0x7FFFFFFFu) ^ this_->capacity;
    this_->data     = buf;
    this_->length   = len;

    if (buf != 0)
    {
        memcpy(buf, src, len);
        buf[len] = 0;
        if (aligned > len + 1)
            memset(buf + len + 1, 0, aligned - (len + 1));
    }
}

// ============================================================
//  SharedBufferFree  (0x403820 - 0x40385A)  -- release the counted buffer
// ============================================================
void SharedBufferFree(F3SharedBuffer *this_)
{
    if (this_->data != 0)
    {
        g_heapTable.free(this_->data);
        this_->data = 0;
    }
    this_->capacity &= 0x80000000u;
    this_->length   = 0;
    g_heapTable.free(this_);
}
