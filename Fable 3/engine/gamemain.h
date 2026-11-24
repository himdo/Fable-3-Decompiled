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

// sub_56DFD0 (0x56DFD0 - 0x56E023) -- GetWideStringHash: the cached FNV
// 32-bit hash of the wide string's data (seed 0x811C9DC5; per 16-bit unit
// h = (h * 0x1000193) ^ low byte, through sub_56DFA0), stored in the string's
// own +28 word (e1). thiscall in the original (the string in ECX); declared
// as a free function per the project convention. [NOT byte-identical]
// placement-dependent (the _wcsnlen IAT slot + the F3FnvHash relative call).
// Exposed for the CLuaManager serialise-script path (engine/luavm.cpp) and
// the CWin32FileSystem verbose open log / failure report (engine/gamemain.cpp).
int GetWideStringHash(F3WString *s);

// sub_1A02200 (0x1A02200 - 0x1A02228) -- set dst to a literal, then append
// src: SetSharedSlot(dst, lit, -1) followed by SharedSlotAppendSlot(dst, src);
// returns dst. cdecl. The two-arg sibling of the already-exposed
// SharedSlotConcat (0x1A02260, slot + slot). [NOT byte-identical]
// placement-dependent (relative calls to SetSharedSlot / SharedSlotAppendSlot).
// Exposed for the CLuaManager serialise-script path (engine/luavm.cpp).
F3SharedBuffer **SharedSlotConcatLiteral(F3SharedBuffer **dst, const char *lit, F3SharedBuffer **src);

// LegacyFileCtor (0x17FE870 - 0x17FE8A5) -- the LHSFS::CLegacyFile ctor over a
// 16-byte block (vtable + shared slot + two zeroed words) followed by the
// CLegacyFileOpen open-setup over the path slot (CLegacyFileOpen: private
// lowercased path block, prefix check + substring rebuild, one-time atexit
// registration, lazy 48-byte file-system wrapper, vt[4] open, file+8 store).
//  thiscall in the original (the block in ECX, the slot address + mode +
// flags on the stack); declared as a plain free function per the project
// convention.
void *LegacyFileCtor(void *obj, F3SharedBuffer **slot, int a3, int a4);

// sub_17FDC30 (0x17FDC30 - 0x17FDC39) -- is the legacy file's +8 file slot
// set (did the open succeed)?
int LegacyFileHasData(const void *obj);

// LegacyFileDtor (0x17FD970 - 0x17FD9A7) -- the LHSFS::CLegacyFile dtor
// (release +0xC / +8, release the shared slot, reset the vtable to
// LHFile::CAFile).  Returns the slot release's new g_Addend count.
long LegacyFileDtor(void *obj);

// ============================================================
//  Accessibility settings (defined in gamemain.cpp next to the
//  CXMLReadStream / shared-slot helpers it is built from).
// ============================================================

// 0x1DBE2F0 - the settings-directory shared slot (F3SharedBuffer*): .bss
// (starts null), the base slot of every settings-file path builder (defined
// in gamemain.cpp). Exposed for SaveSettingsBeforeExit (worker.cpp), which
// rebuilds the AccessibilitySettings.xml path against it.
extern F3SharedBuffer *g_dword_1DBE2F0;

// sub_1A01880 (0x1A01880 - 0x1A01934) -- append (or create) a string on a
// shared slot (defined in gamemain.cpp). Exposed for SaveSettingsBeforeExit
// (worker.cpp).
void AppendSharedSlot(F3SharedBuffer **slot, const char *src);

// SharedFileExists (0x18B0F30 - 0x18B0F98) -- "is the (resolved) path of
// this shared slot an existing, non-directory file?" (defined in
// gamemain.cpp). Exposed for SaveSettingsBeforeExit (worker.cpp).
int SharedFileExists(F3SharedBuffer **obj);

// DeleteSharedFile (0x18B0420 - 0x18B0494) -- "delete the (resolved) path of
// this shared slot": resolve through the path resolver (sub_18AFB90),
// DeleteFileA the resolved text (the CWD -- a directory, so the delete always
// fails), release the local block and g_Addend on both branches, and return
// DeleteFileA's 1/0 result (defined in gamemain.cpp).
int DeleteSharedFile(F3SharedBuffer **slot);

// InitAccessibilitySettings (0x18F72B0 - 0x18F7455) -- the
// accessibility-settings start-up load (snapshot the three system parameters
// it may override, apply the saved overrides from AccessibilitySettings.xml
// when present, run SaveSettingsBeforeExit). Called from
// CWorkerWin32_RunGame (worker.cpp). Returns 0 when the file was processed,
// else the live-slot counter value of the existence-check release.
LONG InitAccessibilitySettings(void);

// SaveSettingsBeforeExit (0x18F71E0 - 0x18F72AC) -- restore the three system
// parameters snapshotted at start-up, then delete AccessibilitySettings.xml
// from the settings directory when present (DeleteSharedFile) -- defined in
// worker.cpp, the CWorkerWin32 module; also called by InitAccessibilitySettings
// here.
LONG SaveSettingsBeforeExit(void);

// 0x1D9E18C / 0x1D9E194 / 0x1D9E19C: the system-parameter snapshot globals
// (defined in gamemain.cpp). g_dword_1D9E18C / g_dword_1D9E194 are the
// SPI_GETHIDCONFIRM / SPI_GETKEYBOARDSPEED snapshot values; g_byte_1D9E19C is
// the 4-byte SPI_GETKEYBOARDDelay value block. SaveSettingsBeforeExit
// (worker.cpp) restores from them.
extern DWORD g_dword_1D9E18C;
extern DWORD g_dword_1D9E194;
extern BYTE  g_byte_1D9E19C[4];

// ============================================================
//  F3OptionMap -- the 24-byte option-map tree node + the 32-byte
//  option-map object (the red-black tree backing each option map: the
//  video-options triple in gamemain.cpp is three of them side by side,
//  and CNetSaveGameCtor (gamethread.cpp) builds the CNetSaveGame +0x08
//  detail-option map from it).
// ============================================================
typedef struct F3OptionNode
{
    F3OptionNode *    link0;    // +0x00
    F3OptionNode *    link1;    // +0x04
    F3OptionNode *    link2;    // +0x08
    F3SharedBuffer *  key;      // +0x0C: the 16-byte key block (a ref)
    DWORD             keyLen;   // +0x10
    BYTE              color;    // +0x14 (byte 20)
    BYTE              leaf;     // +0x15 (byte 21)
    BYTE              pad22;    // +0x16
    BYTE              pad23;    // +0x17
} F3OptionNode;               // 24 bytes

typedef struct F3OptionMap
{
    DWORD *       wrapper;    // +0x00: the 4-byte heap block { &obj }
    DWORD         pad[5];     // +0x04..+0x17: untouched by the ctor / copy
    F3OptionNode *root;       // +0x18: the sentinel node
    DWORD         count;      // +0x1C: the element count
} F3OptionMap;                // 32 bytes

// InitDetailOptionMap (sub_40E830) -- the 32-byte option-map ctor (the
// 4-byte head { &obj } + the 24-byte self-linked sentinel; the (a2, a3)
// args are dead in the original). Defined in gamemain.cpp (formerly
// static; exposed for CNetSaveGameCtor in gamethread.cpp).
void InitDetailOptionMap(F3OptionMap *obj, void *a2, void *a3);

#endif  // F3_GAMEMAIN_H
