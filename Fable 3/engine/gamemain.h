// Fable 3 — frontend run module (RunFrontEnd, 0x61CB70).
//
// RunFrontEnd is the big game start-up / frontend driver. GameThread
// constructs the 356-byte CGameEngine in place (CGameEngineCtor) and then
// calls RunFrontEnd(engine, arg). It:
//   - sets up the XNA notification listener, probes Steam ("Data/ThisIsSteam"),
//   - builds the CDiskFileWin32 / CAFile file objects and the locale message
//     string,
//   - dispatches the manager's "run the game" vtable slot with the parsed
//     command-line / locale arguments,
//   - drives the XNA sign-in / guide / message-box UI loop (skipped when the
//     user is already signed in),
//   - on success creates the render device + engine sub-objects, installs the
//     worker callbacks, loads the intro video, and starts the 3D thread.
//
// It is thiscall + `retn 4` in the original (obj in ECX, one int on the stack,
// result byte in AL). Declared here as a plain free function taking obj as its
// first parameter to match the project convention for non-byte-identical
// thiscall methods.
#ifndef F3_GAMEMAIN_H
#define F3_GAMEMAIN_H

#include "common/common.h"   // F3SharedBuffer

// char RunFrontEnd(void *obj, int arg)   (obj = the CGameEngine, 356 bytes)
char RunFrontEnd(void *obj, int arg);

// F3SharedBuffer **MakeAbnormalShutdownPath(F3SharedBuffer **obj)   (0x67C930)
// settings-directory slot + "abnormal_shutdown.tmp" (shared by the worker module)
F3SharedBuffer **MakeAbnormalShutdownPath(F3SharedBuffer **obj);

// 0x18EB1A0 - the 0x38-byte CS-guarded sorted map object's destructor (the
// counterpart of the static CSortedMapCtor, 0x18EADB0, in gamemain.cpp):
// DeleteCriticalSection at +0x20, the tree + sentinel destroy
// (SortedMapTreeDestroy, 0x578A40), and the owner-tag LHFree.
void CSortedMapDtor(void *obj);

// 0x18FA3E0 - the global manager object getter (dword_2075ED8; takes no
// argument in the original). Returns the 144-byte manager object built by
// CreateHeapManager (0x18FBD00).
void *GetHeapManager(void);

// 0x18FA3D0 - thiscall getter for the manager's +0x1C member (the 1-byte
// sound-system object). Declared as a plain free function here.
void *GetHeapManagerSlot1C(void *hm);

// 0x760FA0 - the version table's first CGameVersion sub-object (CV1, +0x04)
// getter. The original is thiscall (obj in ECX); byte-identical naked this
// call, so obj must be staged in ECX at the call site (the declaration's
// obj parameter is unused). The sub_760FC0 / sub_761CC0 /
// CGameVersionCopyFrom version-compare / copy family takes its result.
void *CVersionTableGetVersion1(void *obj);

// 0x1A01270 - copy-assign one shared slot onto another with refcounting
// (this / src are both the addresses of F3SharedBuffer * fields).
F3SharedBuffer **CopySharedSlot(F3SharedBuffer **thisSlot, F3SharedBuffer **srcSlot);

// 0x1A02260 - concat two source slots into a fresh dst (zero dst, g_Addend++,
// copy src1, append src2; returns the dst slot address).  cdecl.
F3SharedBuffer **SharedSlotConcat(F3SharedBuffer **dst, F3SharedBuffer **src1, F3SharedBuffer **src2);

// 0x19FFAB0 - build a fresh ANSI slot from the low byte of each wide code
// unit of the source wide block and store it in the out slot.
F3SharedBuffer **AnsiFromWideSlot(F3SharedBuffer **wide, F3SharedBuffer **out);

// ============================================================
//  F3WString -- the 36-byte front-end wide path string (the MSVC
//  basic_string<wchar_t, 2> layout + two extra zeroed dwords) that the
//  front-end path-string methods operate on (sub_435C10 = F3WStringAssign
//  assign, sub_4358A0 = F3WStringFromAnsi, sub_4357C0 =
//  F3WStringNormalizeSeparators, sub_436BA0 = F3WStringNormalize):
//    +0    d0 (low word zeroed by the reset idiom)
//    +4    SSO buffer (8 wchars) / heap pointer
//    +20   wchar length
//    +24   capacity ( < 8 => SSO )
//    +28 / +32  extra zeroed words (the wide-hash cache uses +28)
// ============================================================
typedef struct F3WString
{
    DWORD   d0;
    union { wchar_t buf[8]; DWORD ptr; } u;
    DWORD   len;
    DWORD   cap;
    DWORD   e1, e2;
} F3WString;

// sub_4358A0 (0x4358A0 - 0x43593D) -- build the 36-byte front-end wide path
// string from an ANSI string: reset the SSO layout, mbstowcs the whole source
// into a 0x801-wchar stack buffer, assign (src, wcslen) through sub_435C10,
// then the sub_4357C0 separator pass ('/' -> '\\'). The original is a
// thiscall + retn 4 hybrid (the string in ECX, the ANSI buffer on the stack)
// that returns this; here a plain two-arg free function per the project
// convention. [NOT byte-identical] placement-dependent (the CRT _mbstowcs_s /
// __alloca_probe / __security_check_cookie slots and the sub_435C10 /
// sub_4357C0 relative calls). Exposed for the CLuaManager serialise-script
// path (engine/luavm.cpp, CLuaManagerImplRunSerialiseScript).
F3WString *F3WStringFromAnsi(F3WString *obj, const char *src);

// sub_1A02200 (0x1A02200 - 0x1A02228) -- set dst to a literal, then append
// src: SetSharedSlot(dst, lit, -1) followed by SharedSlotAppendSlot(dst, src);
// returns dst. cdecl. The two-arg sibling of the already-exposed
// SharedSlotConcat (0x1A02260, slot + slot). [NOT byte-identical]
// placement-dependent (relative calls to SetSharedSlot / SharedSlotAppendSlot).
// Exposed for the CLuaManager serialise-script path (engine/luavm.cpp).
F3SharedBuffer **SharedSlotConcatLiteral(F3SharedBuffer **dst, const char *lit, F3SharedBuffer **src);

// sub_17FE870 (0x17FE870 - 0x17FE8A5) -- the LHSFS::CLegacyFile ctor over a
// 16-byte block (vtable + shared slot + two zeroed words) followed by the
// sub_17FE0A0 open-setup over the path slot (a documented stub, so the +8
// file slot stays 0 and LegacyFileHasData is false in this build).  thiscall
// in the original (the block in ECX, the slot address + mode + flags on the
// stack); declared as a plain free function per the project convention.
void LegacyFileCtor(void *obj, F3SharedBuffer **slot, int a3, int a4);

// sub_17FDC30 (0x17FDC30 - 0x17FDC39) -- is the legacy file's +8 file slot
// set (did the open succeed)?
int LegacyFileHasData(const void *obj);

// sub_17FD970 (0x17FD970 - 0x17FD9A7) -- the LHSFS::CLegacyFile dtor
// (release +12 / +8, release the shared slot, reset the vtable to
// LHFile::CAFile).  Returns the slot release's new g_Addend count.
long LegacyFileDtor(void *obj);

#endif  // F3_GAMEMAIN_H
