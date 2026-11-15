// Fable 3 -- XLip (lip-sync) culture / component system (engine/XLip.cpp).
//
// Public declarations for the XLip module and for the small set of
// engine-local shared-slot helpers the module shares with gamemain.cpp
// (the helpers keep their definitions in gamemain.cpp and are exposed
// here with external linkage).
#ifndef F3_XLIP_H
#define F3_XLIP_H

#include "common/common.h"   // F3SharedBuffer

// ============================================================
//  F3AString -- the 28-byte front-end ANSI string (the sub_402F00 family,
//  the MSVC basic_string<char, 16-byte-SSO> layout).  Defined in XLip.cpp
//  (the Fable 3 decompile keeps the definitions in the XLip module, from
//  which they moved out of gamemain.cpp) and shared by the VFS config and
//  file-provider code in gamemain.cpp:
//    +0    d0 (untouched by assign / dtor)
//    +4    SSO buffer (16 chars) / heap pointer
//    +20   char length
//    +24   capacity ( < 16 => SSO )
// ============================================================
typedef struct F3AString
{
    DWORD   d0;
    union { char buf[16]; DWORD ptr; } u;
    DWORD   len;
    DWORD   cap;
} F3AString;

// default SSO ctor (the original zeroes the capacity, length and buffer[0],
// leaving d0 as-is): cap = 15, len = 0, buffer[0] = 0.
void F3AStringReset(F3AString *s);

// sub_402F00 (0x402F00 - 0x402F70) -- basic_string<char>::assign(src, len):
// thiscall, returns the string.
F3AString *F3AStringAssign(F3AString *s, const char *src, unsigned int len);

// the "if (cap >= 16) LHFree(heap ptr)" dtor idiom.
void F3AStringFree(F3AString *s);

// ============================================================
//  Engine-local shared-slot helpers (definitions stay in gamemain.cpp,
//  exposed to the XLip module with external linkage).
// ============================================================

// sub_1A012C0 (0x1A012C0 - 0x1A0131C): take a ref of the source slot's block
// (the engine-local copy in gamemain.cpp; the common.cpp RefSharedSlot keeps
// its own name).
F3SharedBuffer **EngRefSharedSlot(F3SharedBuffer **dst, F3SharedBuffer **src);

// Ref the base slot into dst (EngRefSharedSlot), then append the literal
// filename (AppendSharedSlot), so dst = base + name as a shared string.
F3SharedBuffer **MakeSettingsFilePath(F3SharedBuffer **dst, F3SharedBuffer **base,
                                      const char *name);

// sub_1A00C20 (0x1A00C20 - 0x1A00C2F): the C string of a shared slot.
const char *GetSlotString(F3SharedBuffer **slot);

// no-op stand-in for the CRT's __invalid_parameter_noinfo() (kept executable
// without the msvcrt import).
void F3InvalidParam(void);

// sub_1801C20 (0x1801C20 - 0x1801C70): the global file-subsystem object
// getter (obj unused).
void *GetFileSubsystem(void *obj);

// 0x718690 - 0x718BCC: register the game's standard asset packages into the
// language / VFS resource table (called from InitLipCultures).
int RegisterStandardAssets(void);

// ============================================================
//  The XLip module's public entry point.
// ============================================================

// sub_719770 (0x719770 - 0x7199FB): the lip-sync culture / component system
// init, called once by RunFrontEnd right after the engine member 0x98 sorted
// map is built.  (XLip.cpp)
long InitLipCultures(void);

#endif  // F3_XLIP_H
