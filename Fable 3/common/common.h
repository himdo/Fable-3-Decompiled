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

// SharedBufferAssignWide (0x404CC0) - the wide-text counterpart of
// SharedBufferAssign: resize the block to hold len wide code units (no source
// copy): free the old payload, allocate (2*len+5)&~3 bytes, space-fill it,
// write the wide null terminator at code unit len, zero the trailing padding,
// and update capacity (aligned size XOR-merge, top bit preserved) and length
// (wide-char count). userpurge in the original (block in ESI, count in EAX).
void SharedBufferAssignWide(F3SharedBuffer *this_, unsigned int len);

// SharedBufferFree (0x403820) - release a counted buffer: free the payload, clear
// length, set the capacity flag, then free the control block itself.
void SharedBufferFree(F3SharedBuffer *this_);

// SharedBufferCreate (0x1A01360) - allocate a fresh 16-byte counted buffer and
// fill it from (src, size): zero data/length/capacity, SharedBufferAssign the
// payload (size bytes, or the null-terminated length when size == -1), set the
// refcount to 1, and return the block (0 on OOM).
F3SharedBuffer *SharedBufferCreate(const char *src, int size);

// 0x2075C48 - global live-object count for the (ANSI) shared slots below (the
// IDA name is "Addend"): InitSharedSlot bumps it, ReleaseSharedSlot drops it;
// the worker ctor / dtor body do as well.
extern DWORD g_Addend;

// 0x2075C4C - the live-object count for the WIDE (WCHAR) shared-slot family
// (a second, separate counter from g_Addend). Bumped by the wide slot ctor
// (sub_19FF580) and the wide setters/cloner, dropped by
// ReleaseSharedSlotWide (0x19FFA70).
extern DWORD g_WideSlotCount;

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

// ReleaseSharedSlotWide (0x19FFA70) - the destructor of the WIDE (WCHAR)
// shared-slot family (the counterpart to the wide ctor, sub_19FF580, and the
// wide analogue of ReleaseSharedSlot, 0x1A01320). Identical in behaviour to
// ReleaseSharedSlot except it drops the wide live-slot counter (g_WideSlotCount,
// 0x2075C4C) rather than g_Addend, and on zero refcount frees the block through
// the family's inline clone of SharedBufferFree (sub_404370, byte-identical to
// 0x403820) rather than SharedBufferFree itself. Decrements the block's
// refcount, frees on zero, nulls the slot, and returns the new counter value.
long ReleaseSharedSlotWide(F3SharedBuffer **slot);

// RefSharedSlot (0x1A012C0) - the ANSI shared-slot COPY constructor (the
// counterpart to InitSharedSlot's null-ctor and SetSharedSlot's assign).
// thiscall in the original (dst slot in ECX, src slot on the stack, retn 4);
// declared as a plain free function to match the project convention. Nulls the
// destination, registers it in the live-slot count (g_Addend), and when src is
// a different non-empty slot shares its block: copies the block pointer in and
// increments the block's refcount (at +12). Returns the destination slot.
F3SharedBuffer **RefSharedSlot(F3SharedBuffer **this_, F3SharedBuffer **src);

// SetSharedSlot (0x1A01CC0) - the general shared-slot setter (the generic core of
// SetSaveGamePath). thiscall in the original (slot in ECX, src + size on the
// stack, callee cleans via retn 8); declared as a plain free function here.
// Nulls the slot, bumps the live-slot count (g_Addend), builds a fresh counted
// buffer from (src, size) via SharedBufferCreate when src is non-empty, releases
// any old block the slot still holds, stores the new block, and returns the slot.
F3SharedBuffer **SetSharedSlot(F3SharedBuffer **obj, const char *src, int size);

// CopySharedSlotWide (0x19FFA20) - the copy constructor of the WIDE (WCHAR)
// shared-slot family (counterpart to the wide null-ctor, sub_19FF580, and the
// wide analogue of the ANSI copy, sub_1A012C0). thiscall in the original (slot
// in ECX, src on the stack, retn 4); declared as a plain free function here.
// Nulls the destination slot, registers it in the wide live-slot count
// (g_WideSlotCount, 0x2075C4C), and when src is a different non-empty slot,
// shares its block: copies the block pointer in and increments the block's
// refcount (at +12). Returns the destination slot. The sibling copy-assignment
// (sub_19FF9E0) is the same body minus the up-front null + counter bump.
F3SharedBuffer **CopySharedSlotWide(F3SharedBuffer **this_, F3SharedBuffer **src);

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

// Per-message localized-string ID keys for the "wrong executable" message box
// (title = g_dword_1DBD010, body = g_dword_1DBD014). Defined in common.cpp and
// populated by the LocaleInit static global constructor at startup (before
// main/WinMain), mirroring the original's static ctors sub_1A5B5F0/sub_1A5B620.
// See locale_static_ctor_callpath.md.
extern DWORD g_dword_1DBD010;   // 0x1DBD010: GUI_WRONG_EXECUTABLE_NAME
extern DWORD g_dword_1DBD014;   // 0x1DBD014: GUI_WRONG_EXECUTABLE_DESC

// GetSharedSlotWide (0x19FF560) - the wide-string getter for the WIDE (WCHAR)
// shared-slot family (the accessor counterpart to the wide null-ctor
// sub_19FF580, the wide copy ctor CopySharedSlotWide 0x19FFA20 and the wide
// dtor ReleaseSharedSlotWide 0x19FFA70). thiscall in the original (slot in
// ECX, no stack args, plain `retn`); declared as a plain free function here.
// `slot` is a pointer to a wide slot (an `F3SharedBuffer *` block whose data
// member is the wide string); returns block->data (the wide string) when the
// slot holds a block, otherwise the default empty wide string (the original
// "Path" global at 0x01BDFD28). Byte-identical body to GetLocalizedString
// (0x19FF570). Replaces the `sub_19FF560` no-op stub in RunFrontEnd (used to
// pull the four localized sign-in UI strings out of their wide slots for
// XShowMessageBoxUI).
const WCHAR *GetSharedSlotWide(F3SharedBuffer **slot);

#endif  // F3_COMMON_H
