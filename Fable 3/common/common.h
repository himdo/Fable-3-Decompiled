// Fable 3 — common module (engine-wide memory / buffer helpers).
//
// The game routes ALL heap traffic through a custom allocator vtable
// (dword_2075B24) and represents counted byte payloads with a small
// reference-counted control block. These helpers are used by the whole
// engine (the allocator alone has ~14k call sites), so they live here
// rather than in any one subsystem.
#ifndef F3_COMMON_H
#define F3_COMMON_H

#include <windows.h>

// The 16-byte "shared counted buffer" control block: a counted byte payload
// (the payload itself is a separate heap allocation) plus a reference count.
//   +0  data      payload bytes (separately allocated, null-terminated)
//   +4  length    payload byte length
//   +8  capacity  aligned payload allocation size / flag word (top bit used)
//   +12 refcount  reference count (Interlocked)
typedef struct F3SharedBuffer
{
    char *        data;
    unsigned int  length;
    unsigned int  capacity;
    volatile long refcount;
} F3SharedBuffer;

// LHAlloc (0x1A035C0) - the game heap allocator. Sets the process heap to
// compatibility mode once, then routes through the allocator table. On
// failure (and with the verbose flag set) reports "LHBaseMemory::Alloc".
void *LHAlloc(unsigned int size);

// LHFree (0x1A032C0) - the game heap free (counterpart to LHAlloc).
// 11-byte thunk: ecx = dword_2075B24 (allocator table pointer), then
// jmp [ecx+10h] with p as the first stack argument. Unconditional (the
// underlying free(NULL) is a no-op).
void LHFree(void *p);

// SharedBufferAssign (0x403FE0) - assign (src, len) into the counted buffer at this_:
// frees any old payload, allocates a fresh aligned buffer, copies src,
// null-terminates and zeroes the trailing padding, updates length + capacity.
void SharedBufferAssign(F3SharedBuffer *this_, const char *src, unsigned int len);

// SharedBufferFree (0x403820) - release a counted buffer: free the payload, clear
// length, set the capacity flag, then free the control block itself.
void SharedBufferFree(F3SharedBuffer *this_);

// SharedBufferCreate (0x1A01360) - allocate a fresh 16-byte counted buffer and
// fill it from (src, size): zero data/length/capacity, SharedBufferAssign the
// payload (size bytes, or the null-terminated length when size == -1), set the
// refcount to 1, and return the block (0 on OOM).
F3SharedBuffer *SharedBufferCreate(const char *src, int size);

// 0x2075C48 - global live-object count for the shared slots below (the IDA
// name is "Addend"): InitSharedSlot bumps it, ReleaseSharedSlot drops it; the
// worker ctor / dtor body do as well.
extern DWORD g_Addend;

// InitSharedSlot (0x1A00C30) - construct a (null) shared slot and register it
// in the live-slot count. __thiscall in the original: the slot pointer is in
// ECX and is returned in EAX (no stack args). Declared here as a plain free
// function (the `obj` parameter is the slot pointer, the thiscall `this`),
// matching the project convention for non-byte-identical thiscall methods.
// The matching destructor is ReleaseSharedSlot (0x1A01320).
F3SharedBuffer ** InitSharedSlot(F3SharedBuffer **obj);

// ReleaseSharedSlot (0x1A01320) - the shared-slot destructor (counterpart to
// InitSharedSlot). Decrements the control block's refcount, frees the block
// via SharedBufferFree on zero, nulls the slot, and drops the global
// live-slot count (g_Addend). Returns the new g_Addend value.
long ReleaseSharedSlot(F3SharedBuffer **slot);

// SetSharedSlot (0x1A01CC0) - the general shared-slot setter (the generic core of
// SetSaveGamePath). thiscall in the original (slot in ECX, src + size on the
// stack, callee cleans via retn 8); declared as a plain free function here.
// Nulls the slot, bumps the live-slot count (g_Addend), builds a fresh counted
// buffer from (src, size) via SharedBufferCreate when src is non-empty, releases
// any old block the slot still holds, stores the new block, and returns the slot.
F3SharedBuffer **SetSharedSlot(F3SharedBuffer **obj, const char *src, int size);

// GetLocalizedString (0x19FF570) - thiscall accessor for a per-LCID
// localized-string entry. obj points to a word holding the current-locale
// string pointer (a `const WCHAR **`); returns *that pointer (the wide string)
// when it is non-null, otherwise the empty default string (the original "Path"
// global at 0x01BDFD28). The entries come from the locale tables selected by
// GetLocaleEntry (0x6195C0). Pick the current locale's localized-string
// table entry for a given key. `key` is searched against the 9-entry key
// table (dword_1DBD2F4); on a match at index i, the current user locale
// (GetUserDefaultLCID) selects one of 13 per-LCID string tables and the
// function returns a pointer into that table at entry i (an `int *` / `DWORD *`).
// If the key is not present it returns the default entry table (dword_20B6078).
// The returned entry is a `const WCHAR **` consumed by GetLocalizedString
// (0x19FF570).
int *GetLocaleEntry(int key);

const WCHAR *GetLocalizedString(const WCHAR ***obj);

#endif  // F3_COMMON_H
