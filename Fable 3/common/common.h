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

// LHFree - the game heap free (counterpart to LHAlloc). Routes through the
// allocator table's free slot.
void LHFree(void *p);

// SharedBufferAssign (0x403FE0) - assign (src, len) into the counted buffer at this_:
// frees any old payload, allocates a fresh aligned buffer, copies src,
// null-terminates and zeroes the trailing padding, updates length + capacity.
void SharedBufferAssign(F3SharedBuffer *this_, const char *src, unsigned int len);

// SharedBufferFree (0x403820) - release a counted buffer: free the payload, clear
// length, set the capacity flag, then free the control block itself.
void SharedBufferFree(F3SharedBuffer *this_);

#endif  // F3_COMMON_H
