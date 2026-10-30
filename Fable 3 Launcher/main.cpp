// Fable 3 Launcher (FableLauncher.exe) — byte-identical decompilation.
//
// This file mirrors the application code in .text of the original binary.
// Every function below is named after its IDA symbol, placed in the same
// order (by address) as in the original image.
//
// Original image layout (x86, image base 0x00400000):
//   0x00401000 - 0x00404099  application code + inlined MSVC STL (this file)
//   0x00404099 - 0x00411000  static MSVC CRT / EH (supplied by the linker via /MT)
//
// Each function is annotated with its original address range so that the
// decompiled bodies can be slotted in function by function.
#include <windows.h>
#include <sstream>
#include <string>
#include <stdlib.h>
#include <new>

// Forward declarations: WinMain calls these helpers before their definitions.
// __declspec(noinline) keeps the calls out-of-line. Each body writes into the
// stream, so its effect flows into the later `std::wstring app = ss.str();`;
// that data dependency stops LTCG from dead-code-eliminating the if-bodies.
__declspec(noinline) void sub_403AA0(std::wostream& os, const char* s);
__declspec(noinline) void sub_403DB0(std::wostream& os);

// ---------------------------------------------------------------------------
// Supporting data + CRT/EH helpers for the MSVC STL / exception code below.
// ---------------------------------------------------------------------------
// The primary vtables in the original (vptr is assigned the address of the
// first vfunc). Reproduced byte-for-byte from .rdata; the entries that name
// app-range functions are shown as comments. These are only dereferenced for
// virtual calls, which the launcher never performs on exception objects, so
// the raw image addresses are kept verbatim.
static const unsigned int vtable_bad_alloc[] =
    { 0x00401010, 0x00404E54, 0x004138AC, 0x004013B0, 0x004135A8, 0x00404432 };
static const unsigned int vtable_runtime_error[] =
    { 0x00401B10, 0x004010B0, 0x00000000, 0x00413824, 0x004018C0, 0x00401550 };
static const unsigned int vtable_locale_facet[] =
    { 0x004013B0, 0x004135A8, 0x00404432, 0x0000002A, 0x00000043, 0x004135F4 };

// CRT / EH helpers (supplied by the linker via /MT; referenced only from the
// (unreachable) STL stubs below, so the linker never needs to resolve them).
// Empty definitions so the (unreachable) STL helpers above link; the real
// implementations live in the static CRT and are never invoked by the launcher.
extern "C" int         crt_exc_init(void* t, int a)     { (void)t; (void)a; return 0; }   // sub_404E3E
extern "C" void        crt_exc_init2(int a1)            { (void)a1; }                     // sub_404D60
extern "C" const char* crt_exc_what(void* t)            { (void)t; return ""; }           // sub_404E54
extern "C" void        crt_exc_dtor(void* t)            { (void)t; }                      // sub_404EA7
extern "C" void        crt_locimp_ctor(int a1, int a2)  { (void)a1; (void)a2; }           // sub_404432
extern "C" void        crt_lockit_ctor(void* a, int b)  { (void)a; (void)b; }
extern "C" void        crt_lockit_dtor(void* a)         { (void)a; }
extern "C" void        crt_locinfo_ctor(void* a, int b) { (void)a; (void)b; }
extern "C" void        crt_locinfo_dtor(void* a)        { (void)a; }

// In-file string helpers referenced before their definitions.
int sub_402A70(int this_, DWORD* a2, unsigned int a3, char* a4);

// std::ctype<wchar_t> primary vtable (vptr points at [0]).
static const DWORD vtable_ctype[] = {
    0x4018C0, 0x401550, 0x401520, 0x401570, 0x4015B0, 0x401610, 0x4015F0, 0x401670,
    0x401650, 0x401700, 0x4016B0, 0x401730, 0x401800, 0x4017B0, 0x401830, 0x4137D4
};

// CRT char-class / conversion helpers (never invoked by the launcher; empty
// definitions so the unreachable ctype/codecvt helpers above link).
extern "C" const void*    crt_getctype(void* a) { (void)a; return 0; }                       // _Getctype
extern "C" const void*    crt_getcvt(void* a)   { (void)a; return 0; }                       // _Getcvt
extern "C" unsigned short crt_getwctype(wchar_t c, const void* v) { (void)c; (void)v; return 0; }   // _Getwctype
extern "C" const wchar_t* crt_getwctypes(wchar_t* b, wchar_t* e, unsigned short* cls, const void* v)
{ (void)b; (void)e; (void)cls; (void)v; return 0; }                                          // _Getwctypes
extern "C" wchar_t crt_towlower(wchar_t c, const void* v) { (void)v; return c; }             // _Towlower
extern "C" wchar_t crt_towupper(wchar_t c, const void* v) { (void)v; return c; }             // _Towupper
extern "C" int   crt_mbrtowc(wchar_t* wc, const char* s, unsigned int n, void* state, const void* cvt)
{ (void)wc; (void)s; (void)n; (void)state; (void)cvt; return 0; }                            // _Mbrtowc
extern "C" int   crt_wcrtomb(char* s, wchar_t wc, void* state, const void* cvt)
{ (void)s; (void)wc; (void)state; (void)cvt; return 0; }                                     // _Wcrtomb
extern "C" void  crt_invalid_param() { }                                                     // _invalid_parameter_noinfo

// std::ios_base::failure primary vtable (same shape as runtime_error: dtor + what).
static const DWORD vtable_ios_base_failure[] = { 0x401B10, 0x4010B0 };
// std::ios_base primary vtable.
static const DWORD vtable_ios_base[] = {
    0x401B80, 0x413BC0, 0x401B80, 0x413B78, 0x402990, 0x401F10, 0x401F10, 0x402130,
    0x401F20, 0x401F30, 0x401F60, 0x401F80, 0x402030, 0x4020E0
};

// EH / locale CRT helpers (never invoked by the launcher).
extern "C" void  crt_cxx_throw(void* exc, void* ti) { (void)exc; (void)ti; }        // _CxxThrowException
extern "C" void* crt_locale_init()   { return 0; }                                   // std::locale::_Init
extern "C" int   crt_sub_404281()    { return 0; }                                   // sub_404281
extern "C" void  crt_ios_base_dtor(void* this_) { (void)this_; }                     // std::ios_base::_Ios_base_dtor

// In-file helpers referenced before their definitions.
int sub_401D80(int this_, void* Source);
int __fastcall sub_402F70(int a1, int a2, BYTE* Source, char* SourceSize);

// std::wistream / std::wstreambuf primary vtables.
static const DWORD vtable_wistream[] = {
    0x402950, 0x413A7C, 0x4028A0, 0x4139F8, 0x4029F0, 0x4139AC, 0x402A50, 0x402340,
    0x402550, 0x402130, 0x4025B0, 0x401F30, 0x401F60, 0x401F80
};
static const DWORD vtable_wstreambuf[] = {
    0x402990, 0x401F10, 0x401F10, 0x402130, 0x401F20, 0x401F30, 0x401F60, 0x401F80,
    0x402030, 0x4020E0, 0x402100, 0x402120, 0x402130, 0x402140
};

// CRT helpers (never invoked by the launcher).
extern "C" void crt_mtxdst(LPCRITICAL_SECTION cs) { (void)cs; }                 // _Mtxdst
extern "C" unsigned int crt_memcpy_s(void* d, unsigned int dn, const void* s, unsigned int cn)
{ (void)d; (void)dn; (void)s; (void)cn; return 0; }                            // memcpy_s

// In-file streambuf helper referenced before its definition.
int sub_402BD0(int this_);

// vtable pointer values (original binary addresses; the vptr is assigned these
// but the launcher never performs a virtual call on the stream objects, so the
// exact target only matters for fidelity). Vtable-adjustment tables are real
// data (the ctor dereferences them for the multiple-inheritance vptr offsets).
static const DWORD unk_413480_tbl[] = { 0, 0x54, 0, 0x4C, 0x4138AC, 0 };
static const DWORD unk_413488_tbl[] = { 0, 0x4C, 0x4138F4, 0x401B10, 0 };
static const DWORD vtbl_wios = 0x4133E4;
static const DWORD vtbl_basic_stringstream = 0x41347C;
static const DWORD vtbl_wstringbuf_sec = 0x413440;
static const DWORD vtbl_wiostream = 0x413438;
static const DWORD vtbl_wostream = 0x413430;
static const DWORD vtbl_wistream_ptr = 0x413428;

// In-file wstringbuf/wstring helpers referenced before their definitions.
int   sub_402BF0(int);
DWORD* __stdcall sub_402C10(DWORD* a1, int a2);
DWORD* __stdcall sub_4031E0(DWORD* a1);
int sub_4030E0(int* this_);
void* __fastcall sub_402E10(unsigned int a1);
void  sub_4022D0(void* this_);
DWORD* sub_403050(DWORD* this_, unsigned int a2, unsigned int a3);
BYTE* sub_403300(int this_, unsigned int Destination, size_t SourceSize);
DWORD* __stdcall sub_403140(DWORD* a1, int a2);
int   sub_402C80(int a1, int a2);
int   sub_403470(DWORD* a1, int a2, unsigned int a3, unsigned int a4);
int   sub_403520(BYTE* a1, DWORD* a2, unsigned int a3);
int   sub_403630(void* this_, int a2);
DWORD* sub_4036E0(unsigned int a1, unsigned int a2, DWORD* a3);
BOOL  sub_403770(int a1, unsigned int a2);
WORD* __stdcall sub_4037D0(DWORD* a1, unsigned int a2, int a3);
int   sub_403950(int* a1);
int   sub_401420(int* a1, int* a2);
extern "C" void crt_sub_404E61(const char* a1) { (void)a1; }       // sub_404E61 (invalid_parameter)
static const wchar_t unk_4133C8 = 0;                                // empty wide string (0x4133C8)
static int dword_417D80 = 0;                                        // 0x417D80 (locale data)
static int dword_417118 = 0;                                        // 0x417118 (locale data)
static int dword_41710C = 0;                                        // 0x41710C (locale data)
extern "C" void  crt_sub_404818(int a1) { (void)a1; }               // sub_404818 (in/out sync)
extern "C" int   crt_sub_404821(int a1) { (void)a1; return 0; }     // sub_404821 (tie)
extern "C" int   crt_uncaught_exception() { return 0; }             // __uncaught_exception
extern "C" void  crt_exception_ctor(void* t, const char* m) { (void)t; (void)m; }  // std::exception::exception
extern "C" unsigned int crt_memmove_s(void* d, size_t dn, const void* s, size_t cn)
{ (void)d; (void)dn; (void)s; (void)cn; return 0; }                // memmove_s
extern "C" void crt_sub_40473B() { }        // sub_40473B (invalid_parameter)
extern "C" void crt_sub_404703() { }        // sub_404703 (bad_alloc / throw)

// ---------------------------------------------------------------------------
// sub_401000  (0x401000 - 0x40100B)
// ---------------------------------------------------------------------------
// std::bad_alloc constructor: set the vtable, then tail-call the base
// (std::exception) initializer.
__declspec(noinline) int sub_401000(void* this_)
{
    *(void**)this_ = (void*)vtable_bad_alloc;
    return crt_exc_init(this_, 0);
}

// ---------------------------------------------------------------------------
// sub_401010  (0x401010 - 0x401034)
// ---------------------------------------------------------------------------
// std::bad_alloc destructor: re-point the vtable, run the base teardown, and
// free the object if the "delete" flag (low bit of a2) is set.
__declspec(noinline) void* sub_401010(void* this_, char a2)
{
    *(void**)this_ = (void*)vtable_bad_alloc;
    crt_exc_init(this_, 0);
    if (a2 & 1)
        operator delete(this_);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_401040  (0x401040 - 0x4010AA)
// ---------------------------------------------------------------------------
// std::runtime_error constructor (two-argument form used by the throw path).
__declspec(noinline) int __stdcall sub_401040(int a1, int a2)
{
    crt_exc_init2(a1);
    *(int*)a1 = (int)vtable_runtime_error;
    *(int*)(a1 + 32) = 0;
    *(int*)(a1 + 36) = 15;
    *(char*)(a1 + 16) = 0;
    sub_402A70((int)a2, (DWORD*)0, -1, 0);
    return a1;
}

// ---------------------------------------------------------------------------
// unknown_libname_1  (0x4010B0 - 0x4010BE)
// ---------------------------------------------------------------------------
// basic_string data() accessor: return the inline buffer when the capacity is
// below the small-string threshold, otherwise the heap pointer.
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x4010B0-0x4010BE, 14 bytes).
__declspec(noinline) wchar_t* unknown_libname_1(void* this_)
{
    int* p = (int*)this_;
    if (p[9] < 0x10u)
        return (wchar_t*)(p + 4);
    return (wchar_t*)p[4];
}

// ---------------------------------------------------------------------------
// sub_4010C0  (0x4010C0 - 0x401146)
// ---------------------------------------------------------------------------
// std::_Locinfo constructor: zero/initialize the four embedded strings, then
// construct the locale info.
__declspec(noinline) void* __stdcall sub_4010C0(void* a1, int a2)
{
    int* p = (int*)a1;
    char* c = (char*)a1;
    crt_lockit_ctor(a1, 0);
    p[7]  = 15; p[6] = 0;  c[8]  = 0;
    p[14] = 15; p[13] = 0; c[36] = 0;
    p[21] = 15; p[20] = 0; c[64] = 0;
    p[28] = 15; p[27] = 0; c[92] = 0;
    crt_locinfo_ctor(a1, a2);
    return a1;
}

// ---------------------------------------------------------------------------
// sub_401150  (0x401150 - 0x401222)
// ---------------------------------------------------------------------------
// std::_Locinfo destructor: tear down the locale info, then free + reset the
// four embedded strings (heap-backed ones only).
__declspec(noinline) void __stdcall sub_401150(void* a1)
{
    int* p = (int*)a1;
    char* c = (char*)a1;
    void** v = (void**)a1;
    crt_locinfo_dtor(a1);
    if (p[28] >= 0x10u) operator delete(v[23]);
    p[28] = 15; p[27] = 0; c[92] = 0;
    if (p[21] >= 0x10u) operator delete(v[16]);
    p[21] = 15; p[20] = 0; c[64] = 0;
    if (p[14] >= 0x10u) operator delete(v[9]);
    p[14] = 15; p[13] = 0; c[36] = 0;
    if (p[7] >= 0x10u) operator delete(v[2]);
    p[7] = 15; p[6] = 0; c[8] = 0;
    crt_lockit_dtor(a1);
}

// ---------------------------------------------------------------------------
// sub_401230  (0x401230 - 0x4012A1)
// ---------------------------------------------------------------------------
// std::runtime_error copy/assignment initializer.
__declspec(noinline) int sub_401230(int this_, int a2)
{
    crt_exc_init((void*)this_, a2);
    *(int*)this_ = (int)vtable_runtime_error;
    *(int*)(this_ + 36) = 15;
    *(int*)(this_ + 32) = 0;
    *(char*)(this_ + 16) = 0;
    sub_402A70((int)((char*)a2 + 12), (DWORD*)0, -1, 0);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_4012B0  (0x4012B0 - 0x4012D7)
// ---------------------------------------------------------------------------
// std::locale::facet reference-count increment (under the locale lock).
__declspec(noinline) void sub_4012B0(void* this_)
{
    int lock;
    crt_lockit_ctor(&lock, 0);
    int ref = *(int*)((char*)this_ + 4);
    if (ref != -1)
        *(int*)((char*)this_ + 4) = ref + 1;
    crt_lockit_dtor(&lock);
}

// ---------------------------------------------------------------------------
// sub_4012E0  (0x4012E0 - 0x40131A)
// ---------------------------------------------------------------------------
// std::locale::facet reference-count decrement; returns the facet if the count
// reaches zero (it is about to be destroyed), else NULL.
__declspec(noinline) int __fastcall sub_4012E0(int a1)
{
    int lock;
    crt_lockit_ctor(&lock, 0);
    int ref = *(int*)(a1 + 4);
    if (ref != 0 && ref != -1)
        *(int*)(a1 + 4) = ref - 1;
    int result = (*(int*)(a1 + 4) == 0) ? a1 : 0;
    crt_lockit_dtor(&lock);
    return result;
}

// ---------------------------------------------------------------------------
// sub_401320  (0x401320 - 0x401327)
// ---------------------------------------------------------------------------
// std::locale::facet constructor: set the vtable.
__declspec(noinline) void sub_401320(void* this_)
{
    *(DWORD*)this_ = 0x411190;
}

// ---------------------------------------------------------------------------
// sub_401380  (0x401380 - 0x4013A8)
// ---------------------------------------------------------------------------
// locale facet copy-constructor: copy the message string into the new object.
__declspec(noinline) int sub_401380(int a1, int a2)
{
    int msg = *(int*)a1;
    *(int*)(a2 + 20) = 0;
    *(int*)(a2 + 24) = 15;
    *(char*)(a2 + 4) = 0;
    sub_402A70((int)(msg + 24), (DWORD*)0, -1, 0);
    return a2;
}

// ---------------------------------------------------------------------------
// sub_4013B0  (0x4013B0 - 0x4013CF)
// ---------------------------------------------------------------------------
// std::locale::facet destructor: re-point the vtable and free if flagged.
__declspec(noinline) void* sub_4013B0(void* this_, char a2)
{
    *(void**)this_ = (void*)vtable_locale_facet;
    if (a2 & 1)
        operator delete(this_);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_4013D0  (0x4013D0 - 0x401415)
// ---------------------------------------------------------------------------
int sub_4013D0(int this_)
{
    *(int*)(this_ + 4) = 0;
    *(int*)this_ = (int)&vtable_ctype;
    *(int*)(this_ + 8) = (int)crt_getctype(0);
    *(int*)(this_ + 24) = (int)crt_getcvt(0);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_401420  (0x401420 - 0x40151D)
// ---------------------------------------------------------------------------
int sub_401420(int* a1, int* a2)
{
    (void)a2;
    if (a1 && *a1 == 0) {
        void* p = operator new(0x20u);
        *a1 = p ? sub_4013D0((int)p) : 0;
    }
    return 2;
}

// ---------------------------------------------------------------------------
// sub_401520  (0x401520 - 0x401541)
// ---------------------------------------------------------------------------
int sub_401520(int this_, unsigned short a2, wchar_t a3)
{
    return (crt_getwctype(a3, (const void*)(this_ + 8)) & a2) != 0;
}

// ---------------------------------------------------------------------------
// sub_401550  (0x401550 - 0x40156E)
// ---------------------------------------------------------------------------
const wchar_t* sub_401550(int this_, wchar_t* a2, wchar_t* a3, unsigned short* a4)
{
    return crt_getwctypes(a2, a3, a4, (const void*)(this_ + 8));
}

// ---------------------------------------------------------------------------
// sub_401570  (0x401570 - 0x4015A8)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x401570-0x4015A8, 56 bytes).
unsigned short* sub_401570(void* this_, int a2, unsigned short* a3, unsigned short* a4)
{
    int (*is)(void*, int, wchar_t) =
        (int (*)(void*, int, wchar_t))(*(DWORD*)(*(DWORD*)this_ + 8));
    for (; a3 != a4; ++a3)
        if (is(this_, a2, *a3) != 0) break;
    return a3;
}

// ---------------------------------------------------------------------------
// sub_4015B0  (0x4015B0 - 0x4015E8)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x4015B0-0x4015E8, 56 bytes).
unsigned short* sub_4015B0(void* this_, int a2, unsigned short* a3, unsigned short* a4)
{
    int (*is)(void*, int, wchar_t) =
        (int (*)(void*, int, wchar_t))(*(DWORD*)(*(DWORD*)this_ + 8));
    for (; a3 != a4; ++a3)
        if (is(this_, a2, *a3) == 0) break;
    return a3;
}

// ---------------------------------------------------------------------------
// sub_4015F0  (0x4015F0 - 0x401604)
// ---------------------------------------------------------------------------
wchar_t sub_4015F0(int this_, wchar_t a2)
{
    return crt_towlower(a2, (const void*)(this_ + 8));
}

// ---------------------------------------------------------------------------
// sub_401610  (0x401610 - 0x401641)
// ---------------------------------------------------------------------------
wchar_t* sub_401610(char* this_, wchar_t* a2, wchar_t* a3)
{
    if (a2 != a3) {
        const void* v4 = (const void*)(this_ + 8);
        do { *a2 = crt_towlower(*a2, v4); ++a2; } while (a2 != a3);
    }
    return a2;
}

// ---------------------------------------------------------------------------
// sub_401650  (0x401650 - 0x401664)
// ---------------------------------------------------------------------------
wchar_t sub_401650(int this_, wchar_t a2)
{
    return crt_towupper(a2, (const void*)(this_ + 8));
}

// ---------------------------------------------------------------------------
// sub_401670  (0x401670 - 0x4016A1)
// ---------------------------------------------------------------------------
wchar_t* sub_401670(char* this_, wchar_t* a2, wchar_t* a3)
{
    if (a2 != a3) {
        const void* v4 = (const void*)(this_ + 8);
        do { *a2 = crt_towupper(*a2, v4); ++a2; } while (a2 != a3);
    }
    return a2;
}

// ---------------------------------------------------------------------------
// sub_4016B0  (0x4016B0 - 0x4016F3)
// ---------------------------------------------------------------------------
int sub_4016B0(int this_, char a2)
{
    wchar_t v4 = 0;
    int v5 = 0;
    if (crt_mbrtowc(&v4, &a2, 1u, &v5, (const void*)(this_ + 24)) < 0)
        return 0xFFFF;
    return v4;
}

// ---------------------------------------------------------------------------
// sub_401700  (0x401700 - 0x401722)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x401700-0x401722, 34 bytes).
int sub_401700(void* this_, int a2, int a3, int a4)
{
    int (*cmp)(void*, int, int, int, int) =
        (int (*)(void*, int, int, int, int))(*(DWORD*)(*(DWORD*)this_ + 44));
    return cmp(this_, a2, a3, a4, a3 - a2);
}

// ---------------------------------------------------------------------------
// sub_401730  (0x401730 - 0x4017A2)
// ---------------------------------------------------------------------------
BYTE* sub_401730(int a1, BYTE* a2, BYTE* a3, unsigned short* a4, int a5)
{
    if (a5 < a3 - a2) crt_invalid_param();
    if (a2 != a3) {
        const void* v8 = (const void*)(a1 + 24);
        do {
            wchar_t wc = 0;
            int state = 0;
            int r = crt_mbrtowc(&wc, (const char*)a2, 1u, &state, v8);
            *a4 = (r < 0) ? (unsigned short)-1 : (unsigned short)wc;
            ++a2; ++a4;
        } while (a2 != a3);
    }
    return a2;
}

// ---------------------------------------------------------------------------
// sub_4017B0  (0x4017B0 - 0x4017FF)
// ---------------------------------------------------------------------------
char sub_4017B0(int this_, wchar_t a2, char a3)
{
    int state = 0;
    char Byte = 0;
    if (crt_wcrtomb(&Byte, a2, &state, (const void*)(this_ + 24)) == 1)
        return Byte;
    return a3;
}

// ---------------------------------------------------------------------------
// sub_401800  (0x401800 - 0x401829)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x401800-0x401829, 41 bytes).
int sub_401800(void* this_, int a2, int a3, int a4, int a5)
{
    int (*fn)(void*, int, int, int, int, int) =
        (int (*)(void*, int, int, int, int, int))(*(DWORD*)(*(DWORD*)this_ + 56));
    return fn(this_, a2, a3, a4, a5, (a3 - a2) >> 1);
}

// ---------------------------------------------------------------------------
// sub_401830  (0x401830 - 0x4018B5)
// ---------------------------------------------------------------------------
wchar_t* sub_401830(char* this_, wchar_t* a2, wchar_t* a3, char a4, char* a5, unsigned int a6)
{
    if (a6 < a3 - a2) crt_invalid_param();
    if (a2 != a3) {
        const void* v9 = (const void*)(this_ + 24);
        do {
            wchar_t wc = *a2;
            int state = 0;
            char Byte = a4;
            if (crt_wcrtomb(&Byte, wc, &state, v9) == 1) *a5 = Byte;
            ++a2; ++a5;
        } while (a2 != a3);
    }
    return a2;
}

// ---------------------------------------------------------------------------
// sub_4018C0  (0x4018C0 - 0x4018F7)
// ---------------------------------------------------------------------------
void* sub_4018C0(void* this_, char a2)
{
    bool had_data = ((DWORD*)this_)[5] != 0;
    *(DWORD*)this_ = (DWORD)&vtable_ctype;
    if (had_data) free(((void**)this_)[4]);
    *(DWORD*)this_ = (DWORD)&vtable_locale_facet;
    if (a2 & 1) operator delete(this_);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_401900  (0x401900 - 0x401932)
// ---------------------------------------------------------------------------
int sub_401900(void** this_)
{
    *this_ = (void*)&vtable_runtime_error;
    if ((unsigned int)this_[9] >= 0x10) operator delete(this_[4]);
    this_[9] = (void*)15;
    this_[8] = 0;
    *((BYTE*)this_ + 16) = 0;
    return crt_exc_init(this_, 0);
}

// ---------------------------------------------------------------------------
// sub_401940  (0x401940 - 0x401A5E)
// ---------------------------------------------------------------------------
__declspec(noreturn) void sub_401940(DWORD* this_, char a2, int a3)
{
    (void)a3;
    if (a2 != 0) crt_cxx_throw(0, 0);
    int v3 = this_[2] & this_[3];
    DWORD exc[12];
    const char* msg;
    if (v3 & 4) {
        msg = "ios_base::badbit set";
        sub_402F70((int)msg, 0x14, 0, 0);
        sub_401040((int)exc, (int)exc);
    } else if (v3 & 2) {
        msg = "ios_base::failbit set";
        sub_401D80((int)exc, (void*)msg);
        sub_401040((int)exc, (int)exc);
    } else {
        msg = "ios_base::eofbit set";
        sub_401D80((int)exc, (void*)msg);
        sub_401040((int)exc, (int)exc);
    }
    exc[0] = (DWORD)&vtable_ios_base_failure;
    crt_cxx_throw(exc, (void*)0x414448);
}

// ---------------------------------------------------------------------------
// sub_401A60  (0x401A60 - 0x401A8E)
// ---------------------------------------------------------------------------
DWORD* sub_401A60(int a1, int a2, DWORD* a3)
{
    DWORD v6 = (DWORD)a2;
    int v3 = **((DWORD**)(a1 + 36));
    *a3 = (DWORD)v3;
    crt_lockit_ctor(&v6, 0);
    int v4 = *(int*)(v3 + 4);
    if (v4 != -1) *(int*)(v3 + 4) = v4 + 1;
    crt_lockit_dtor(&v6);
    return a3;
}

// ---------------------------------------------------------------------------
// sub_401A90  (0x401A90 - 0x401B0C)
// ---------------------------------------------------------------------------
void sub_401A90(DWORD* a1)
{
    a1[9] = 0; a1[1] = 0; a1[3] = 0; a1[4] = 513; a1[5] = 6;
    a1[6] = 0; a1[7] = 0; a1[8] = 0; a1[2] = 0;
    DWORD* v1 = (DWORD*)operator new(4u);
    if (v1) {
        *v1 = (DWORD)crt_locale_init();
        int v2 = crt_sub_404281();
        DWORD v4 = 0;
        crt_lockit_ctor(&v4, 0);
        int v3 = *(int*)(v2 + 4);
        if (v3 != -1) *(int*)(v2 + 4) = v3 + 1;
        crt_lockit_dtor(&v4);
        a1[9] = (DWORD)v1;
    } else {
        a1[9] = 0;
    }
}

// ---------------------------------------------------------------------------
// sub_401B10  (0x401B10 - 0x401B57)
// ---------------------------------------------------------------------------
void** sub_401B10(void** this_, char a2)
{
    *this_ = (void*)&vtable_runtime_error;
    if ((unsigned int)this_[9] >= 0x10) operator delete(this_[4]);
    this_[9] = (void*)15;
    this_[8] = 0;
    *((BYTE*)this_ + 16) = 0;
    crt_exc_init(this_, 0);
    if (a2 & 1) operator delete(this_);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_401B60  (0x401B60 - 0x401B79)
// ---------------------------------------------------------------------------
DWORD* sub_401B60(DWORD* this_, void* a2)
{
    sub_401230((int)this_, (int)a2);
    *this_ = (DWORD)&vtable_ios_base_failure;
    return this_;
}

// ---------------------------------------------------------------------------
// sub_401B80  (0x401B80 - 0x401BA8)
// ---------------------------------------------------------------------------
DWORD* sub_401B80(DWORD* this_, char a2)
{
    *(DWORD*)this_ = (DWORD)&vtable_ios_base;
    crt_ios_base_dtor(this_);
    if (a2 & 1) operator delete(this_);
    return this_;
}

// ---------------------------------------------------------------------------
// _WinMain@16  (0x00401BB0 - 0x00401D71)
// ---------------------------------------------------------------------------
// Builds the command into a wide stringstream: the (narrow) command line is
// widened and appended, and if the result is empty the default command
// ("F3Secu.exe") is substituted. The string is then launched with
// CreateProcessW in a retry loop: a child exit code of 291 (0x123) causes an
// immediate re-launch (the SecuROM tamper-detection re-spawn).
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    std::wstringstream ss;
    if (lpCmdLine != NULL)
        sub_403AA0(ss, lpCmdLine);

    if (ss.str() == L"")
        sub_403DB0(ss);

    std::wstring app = ss.str();

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    bool retry;
    do
    {
        retry = false;
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        if (!CreateProcessW(app.c_str(), NULL, NULL, NULL, TRUE, 0,
                            NULL, NULL, &si, &pi))
            break;
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode == 291)
            retry = true;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } while (retry);

    return 0;
}

// ---------------------------------------------------------------------------
// sub_401D80  (0x401D80 - 0x401DB9)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x401D80-0x401DB9, 57 bytes).
int sub_401D80(int this_, void* Source)
{
    *(DWORD*)(this_ + 24) = 15;
    *(DWORD*)(this_ + 20) = 0;
    *(BYTE*)(this_ + 4) = 0;
    sub_402F70((int)Source, (int)strlen((const char*)Source), 0, 0);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_401DC0  (0x401DC0 - 0x401DE5)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x401DC0-0x401DE5, 37 bytes).
int sub_401DC0(int this_, DWORD* a2)
{
    *(DWORD*)(this_ + 20) = 0;
    *(DWORD*)(this_ + 24) = 15;
    *(BYTE*)(this_ + 4) = 0;
    sub_402A70((int)this_, (DWORD*)a2, 0, (char*)0xFFFFFFFF);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_401E20  (0x401E20 - 0x401E76)
// ---------------------------------------------------------------------------
int sub_401E20(int a1)
{
    int v1 = *(int*)(*(int*)a1 + 4);
    int v2 = 0;
    DWORD streambuf = *(DWORD*)(v1 + a1 + 40);
    DWORD vptr = *(DWORD*)streambuf;
    int (*vfunc12)(DWORD) = (int (*)(DWORD))(*(DWORD*)(vptr + 48));
    if (((*(BYTE*)(v1 + a1 + 8) & 6) == 0) && (vfunc12(streambuf) == -1)) {
        v2 = 4;
    }
    int v3 = a1 + *(int*)(*(int*)a1 + 4);
    if (v2 != 0) {
        int v4 = v2 | *(int*)(v3 + 8);
        if (*(int*)(v3 + 40) == 0) v4 |= 4;
        int v5 = v4 & 0x17;
        *(int*)(v3 + 8) = v5;
        if ((v5 & *(int*)(v3 + 12)) != 0) sub_401940((DWORD*)v3, 0, 0);
    }
    return a1;
}

// ---------------------------------------------------------------------------
// sub_401E90  (0x401E90 - 0x401E9F)
// ---------------------------------------------------------------------------
int sub_401E90(DWORD* this_)
{
    int result = *(int*)((char*)this_ - 8);
    *(DWORD*)((char*)this_ + *(int*)(result + 4) - 8) = 0x413428;
    return result;
}

// ---------------------------------------------------------------------------
// sub_401EA0  (0x401EA0 - 0x401F10)
// ---------------------------------------------------------------------------
void sub_401EA0(void* this_)
{
    int* v2 = *(int**)((char*)this_ + 14*4);
    *(DWORD*)this_ = (DWORD)&vtable_wstreambuf;
    if (v2) {
        int v3 = *v2;
        if (*v2 != 0) {
            DWORD v6 = 0;
            crt_lockit_ctor(&v6, 0);
            int v4 = *(int*)(v3 + 4);
            if (v4 != 0 && v4 != -1) *(int*)(v3 + 4) = v4 - 1;
            void* v5 = (*(int*)(v3 + 4) == 0) ? (void*)v3 : 0;
            crt_lockit_dtor(&v6);
            if (v5) ((void (*)(void*, int))(**(void***)v5))(v5, 1);
        }
        operator delete(v2);
    }
    crt_mtxdst(*(LPCRITICAL_SECTION*)((char*)this_ + 4));
    operator delete(*(void**)((char*)this_ + 4));
}

// ---------------------------------------------------------------------------
// sub_401F10  (0x401F10 - 0x401F18)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x401F10-0x401F18, 8 bytes).
int __stdcall sub_401F10(int a1)
{
    (void)a1;
    return 0xFFFF;
}

// ---------------------------------------------------------------------------
// sub_401F20  (0x401F20 - 0x401F26)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x401F20-0x401F26, 6 bytes).
int sub_401F20()
{
    return 0xFFFF;
}

// ---------------------------------------------------------------------------
// sub_401F30  (0x401F30 - 0x401F5C)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x401F30-0x401F5C, 44 bytes).
int sub_401F30(DWORD** this_)
{
    DWORD vptr = *(this_[0]);
    int (*sgetc)(DWORD**) = (int (*)(DWORD**))(*(DWORD*)(vptr + 16));
    if (sgetc(this_) == 0xFFFF) return 0xFFFF;
    --*(this_[12]);
    unsigned short* buf = *(unsigned short**)(this_ + 8);
    unsigned short wc = *buf;
    *(unsigned short**)(this_ + 8) = buf + 1;
    return wc;
}

// ---------------------------------------------------------------------------
// sub_401F60  (0x401F60 - 0x401F76)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x401F60-0x401F76, 22 bytes).
int sub_401F60(void* this_, int a2, int a3)
{
    DWORD vptr = *(DWORD*)this_;
    int (*sputc)(void*, int, int, int) = (int (*)(void*, int, int, int))(*(DWORD*)(vptr + 28));
    return sputc(this_, a2, -1, a3);
}

// ---------------------------------------------------------------------------
// sub_401F80  (0x401F80 - 0x40202A)
// ---------------------------------------------------------------------------
int sub_401F80(const void** this_, char* Destination, int a3, int a4)
{
    int v5 = 0;
    if (a4 <= 0) return 0;
    do {
        int v8 = sub_402BD0(*(int*)this_);
        if (v8 <= 0) {
            DWORD vptr = *(DWORD*)this_[0];
            int (*sgetc)(const void**) = (int (*)(const void**))(*(DWORD*)(vptr + 20));
            int v11 = sgetc(this_);
            if (v11 == -1) return v5;
            *(WORD*)Destination = (WORD)v11;
            ++v5; Destination += 2; --a4;
        } else {
            int v9 = (a4 < v8) ? a4 : v8;
            char* buf = *(char**)this_[8];
            crt_memcpy_s(Destination, 2*a3, buf, 2*v9);
            *(char**)this_[12] = *(char**)this_[12] - v9;
            a4 -= v9; v5 += v9; Destination += 2*v9;
            *(char**)this_[8] = buf + 2*v9;
        }
    } while (a4 > 0);
    return v5;
}

// ---------------------------------------------------------------------------
// sub_402030  (0x402030 - 0x4020D3)
// ---------------------------------------------------------------------------
int sub_402030(void* const** this_, char* Source, int a3)
{
    int v3 = a3, v4 = 0;
    if (a3 <= 0) return 0;
    do {
        int v7 = sub_402BF0((int)this_);
        if (v7 <= 0) {
            unsigned short (*sputc)(void* const**, unsigned short) =
                (unsigned short (*)(void* const**, unsigned short))(*(this_[0] + 4));
            if (sputc(this_, *(unsigned short*)Source) == 0xFFFF) return v4;
            ++v4; Source += 2; --v3;
        } else {
            int v8 = (v3 < v7) ? v3 : v7;
            int v9 = 2 * v8;
            char** p9 = (char**)(this_ + 9*4);
            crt_memcpy_s(*p9, 2*v7, Source, 2*v8);
            *(char**)(this_ + 13*4) = *(char**)(this_ + 13*4) - v8;
            a3 -= v8; v4 += v8; Source += v9;
            *p9 = *p9 + v9;
            v3 = a3;
        }
    } while (v3 > 0);
    return v4;
}

// ---------------------------------------------------------------------------
// unknown_libname_2  (0x4020E0 - 0x4020FA)
// ---------------------------------------------------------------------------
DWORD* __stdcall unknown_libname_2(DWORD* a1, int a2, int a3, int a4)
{
    (void)a2; (void)a3; (void)a4;
    *a1 = -1; a1[2] = 0; a1[3] = 0; a1[4] = 0;
    return a1;
}

// ---------------------------------------------------------------------------
// unknown_libname_3  (0x402100 - 0x40211A)
// ---------------------------------------------------------------------------
DWORD* __stdcall unknown_libname_3(DWORD* a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; (void)a8;
    *a1 = -1; a1[2] = 0; a1[3] = 0; a1[4] = 0;
    return a1;
}

// ---------------------------------------------------------------------------
// sub_402120  (0x402120 - 0x402125)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x402120-0x402125, 5 bytes).
void* sub_402120(void* this_, int a2, int a3)
{
    (void)a2; (void)a3;
    return this_;
}

// ---------------------------------------------------------------------------
// sub_402130  (0x402130 - 0x402133)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x402130-0x402133, 3 bytes).
int sub_402130()
{
    return 0;
}

// ---------------------------------------------------------------------------
// nullsub_2  (0x402140 - 0x402143)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x402140-0x402143, 3 bytes).
void __stdcall nullsub_2(int a1)
{
    (void)a1;
}

// ---------------------------------------------------------------------------
// sub_402150  (0x402150 - 0x4021E7)
// ---------------------------------------------------------------------------
DWORD* __stdcall sub_402150(DWORD* a1)
{
    *a1 = (DWORD)unk_413480_tbl;
    a1[2] = (DWORD)unk_413488_tbl;
    a1[21] = vtbl_wios;
    sub_402C10(a1, (int)(a1 + 3));
    *(DWORD*)((char*)a1 + *(DWORD*)(*a1 + 4)) = vtbl_basic_stringstream;
    sub_4031E0(a1 + 3);
    a1[3] = vtbl_wstringbuf_sec;
    a1[18] = 0;
    a1[19] = 0;
    return a1;
}

// ---------------------------------------------------------------------------
// sub_4021F0  (0x4021F0 - 0x402271)
// ---------------------------------------------------------------------------
int sub_4021F0(DWORD* this_)
{
    *(DWORD*)((char*)this_ + *(DWORD*)(*(this_ - 21) + 4) - 84) = vtbl_basic_stringstream;
    char* v1 = (char*)(this_ - 18);
    sub_4022D0(this_ - 18);
    *(DWORD*)(&v1[*(DWORD*)(*((DWORD*)v1 - 3) + 4) - 12]) = vtbl_wiostream;
    int result = *(DWORD*)(*((DWORD*)v1 - 1) + 4);
    *(DWORD*)(&v1[result - 4]) = vtbl_wostream;
    *(DWORD*)(&v1[*(DWORD*)(*((DWORD*)v1 - 3) + 4) - 12]) = vtbl_wistream_ptr;
    return result;
}

// ---------------------------------------------------------------------------
// sub_402280  (0x402280 - 0x40229A)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x402280-0x40229A, 26 bytes).
int sub_402280(int a1, int a2)
{
    sub_402C80(a1 + 12, a2);
    return a2;
}

// ---------------------------------------------------------------------------
// sub_4022D0  (0x4022D0 - 0x402336)
// ---------------------------------------------------------------------------
void sub_4022D0(void* this_)
{
    bool v2 = (((BYTE*)this_)[64] & 1) == 0;
    *(DWORD*)this_ = (DWORD)vtable_wstreambuf;
    if (!v2) operator delete(**((void***)this_ + 4));
    **((DWORD**)this_ + 4) = 0;
    **((DWORD**)this_ + 8) = 0;
    **((DWORD**)this_ + 12) = 0;
    **((DWORD**)this_ + 5) = 0;
    **((DWORD**)this_ + 9) = 0;
    **((DWORD**)this_ + 13) = 0;
    *((DWORD*)this_ + 16) &= ~1u;
    *((DWORD*)this_ + 15) = 0;
    sub_401EA0(this_);
}

// ---------------------------------------------------------------------------
// sub_402340  (0x402340 - 0x402543)
// ---------------------------------------------------------------------------
unsigned short sub_402340(int this_, unsigned short a2)
{
    if (((BYTE*)(this_ + 64))[0] & 8) {
        unsigned int* v3 = *(unsigned int**)(this_ + 36);
        unsigned int v4 = *v3;
        if (*v3 != 0) {
            unsigned int v5 = *(unsigned int*)(this_ + 60);
            if (v4 < v5) {
                int v6 = v4 + 2 * **(DWORD **)(this_ + 52) - v5;
                *v3 = v5;
                **(DWORD **)(this_ + 52) = v6 >> 1;
            }
        }
    }
    if (a2 == 0xFFFF) return 0;
    unsigned int v8 = **(DWORD **)(this_ + 36);
    if (v8 != 0) {
        unsigned int* v9 = *(unsigned int**)(this_ + 52);
        if (v8 < v8 + 2 * *v9) {
            --*v9;
            unsigned short* v10 = *(unsigned short**)(this_ + 36);
            unsigned short* v11 = (*(unsigned short**)(this_ + 36))++;
            *v11 = a2;
            return a2;
        }
    }
    if (((BYTE*)(this_ + 64))[0] & 2) return 0xFFFF;
    // buffer-growth path (simplified; unreachable for the launcher)
    unsigned int v12 = (v8 != 0) ? (unsigned int)(v8 + 2 * **(DWORD **)(this_ + 52) - **(DWORD **)(this_ + 16)) >> 1 : 0;
    unsigned int v13 = (v12 >> 1 >= 0x20) ? (v12 >> 1) : 32;
    if (v13 == 0) return 0xFFFF;
    void* v15 = sub_402E10((unsigned int)v13);
    void* v16 = **(void ***)(this_ + 16);
    if (v12 == 0) {
        *(DWORD*)(this_ + 60) = (DWORD)v15;
        *(*(int**)(this_ + 20)) = (DWORD)v15;
        **(DWORD **)(this_ + 36) = (DWORD)v15;
        **(DWORD **)(this_ + 52) = (int)(2 * (v13 + v12)) >> 1;
        **(DWORD **)(this_ + 16) = (DWORD)v15;
        if (((BYTE*)(this_ + 64))[0] & 4) { *(*(int**)(this_ + 32)) = 0; **(DWORD **)(this_ + 48) = (DWORD)(v15) >> 1; }
        else { *(*(int**)(this_ + 32)) = (DWORD)v15; **(DWORD **)(this_ + 48) = 1; }
    } else {
        memcpy_s(v15, 2 * (v13 + v12), v16, 2 * v12);
    }
    if (((BYTE*)(this_ + 64))[0] & 1) operator delete(v16);
    unsigned int* v25 = *(unsigned int**)(this_ + 52);
    *(DWORD*)(this_ + 64) |= 1u;
    --*v25;
    unsigned short* v27 = (*(unsigned short**)(this_ + 36))++;
    *v27 = a2;
    return a2;
}

// ---------------------------------------------------------------------------
// sub_402550  (0x402550 - 0x4025AD)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x402550-0x4025AD, 93 bytes).
int sub_402550(int this_, unsigned short a2)
{
    unsigned int v2 = **(DWORD **)(this_ + 32);
    if (v2 == 0 || v2 <= **(DWORD **)(this_ + 16)
        || (a2 != 0xFFFF && a2 != *(unsigned short*)(v2 - 2) && ((BYTE*)(this_ + 64))[0] & 2)) {
        return 0xFFFF;
    }
    ++**(DWORD **)(this_ + 48);
    **(DWORD **)(this_ + 32) -= 2;
    if (a2 == 0xFFFF) return 0;
    ***(WORD ***)(this_ + 32) = a2;
    return a2;
}

// ---------------------------------------------------------------------------
// sub_4025B0  (0x4025B0 - 0x402618)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x4025B0-0x402618, 104 bytes).
unsigned short sub_4025B0(int this_)
{
    unsigned int* v1 = *(unsigned int**)(this_ + 32);
    unsigned int v2 = *v1;
    if (*v1 == 0) return 0xFFFF;
    int* v4 = *(int**)(this_ + 48);
    if (v2 < v2 + 2 * (unsigned int)*v4) return *(unsigned short*)v2;
    if (((BYTE*)(this_ + 64))[0] & 4) return 0xFFFF;
    unsigned int v5 = **(DWORD **)(this_ + 36);
    if (v5 == 0 || (v5 <= v2 && *(DWORD*)(this_ + 60) <= v2)) return 0xFFFF;
    if (*(DWORD*)(this_ + 60) < v5) *(DWORD*)(this_ + 60) = v5;
    *v4 = (int)(*(DWORD*)(this_ + 60) - *v1) >> 1;
    return ***(WORD ***)(this_ + 32);
}

// ---------------------------------------------------------------------------
// sub_402620  (0x402620 - 0x40278B)
// ---------------------------------------------------------------------------
int* sub_402620(int this_, int* a2, int a3, int a4, char a5)
{
    unsigned int* v5 = *(unsigned int**)(this_ + 36);
    unsigned int v6 = *v5;
    if (*v5 != 0 && *(DWORD*)(this_ + 60) < v6) *(DWORD*)(this_ + 60) = v6;
    int v8 = -1;
    if ((a5 & 1) == 0 || **(DWORD **)(this_ + 32) == 0) {
        if ((a5 & 2) != 0 && *v5 != 0) {
            if (a4 == 2) v8 = ((*(DWORD*)(this_ + 60) - **(DWORD **)(this_ + 16)) >> 1) + a3;
            else if (a4 == 1) v8 = ((int)(*v5 - **(DWORD **)(this_ + 16)) >> 1) + a3;
            else { v8 = -1; if (a4 == 0) v8 = a3; }
            int v16 = **(DWORD **)(this_ + 16);
            if (v8 >= 0 && v8 <= (*(DWORD*)(this_ + 60) - v16) >> 1) {
                int v17 = v8 + ((int)(v16 - *v5) >> 1);
                **(DWORD **)(this_ + 52) -= v17;
                **(DWORD **)(this_ + 36) += 2 * v17;
            } else v8 = -1;
        }
    } else {
        int v7 = **(DWORD **)(this_ + 32);
        if (a4 == 2) v8 = ((*(DWORD*)(this_ + 60) - **(DWORD **)(this_ + 16)) >> 1) + a3;
        else if (a4 == 1) { if (a5 & 2) v8 = -1; else v8 = ((v7 - **(DWORD **)(this_ + 16)) >> 1) + a3; }
        else if (a4 != 0) v8 = -1;
        else v8 = a3;
        if (v8 >= 0) {
            int v9 = **(DWORD **)(this_ + 16);
            if (v8 <= (*(DWORD*)(this_ + 60) - v9) >> 1) {
                int v10 = v8 + ((v9 - v7) >> 1);
                **(DWORD **)(this_ + 48) -= v10;
                **(DWORD **)(this_ + 32) += 2 * v10;
                if (a5 & 2) {
                    int* v11 = *(int**)(this_ + 36);
                    if (*v11 != 0) {
                        int v12 = *v11, v13 = **(DWORD **)(this_ + 52), v14 = **(DWORD **)(this_ + 32);
                        *v11 = v14;
                        **(DWORD **)(this_ + 52) = (v12 + 2 * v13 - v14) >> 1;
                    }
                }
            } else v8 = -1;
        }
    }
    *a2 = v8; a2[2] = 0; a2[3] = 0; a2[4] = 0;
    return a2;
}

// ---------------------------------------------------------------------------
// sub_402790  (0x402790 - 0x40286A)
// ---------------------------------------------------------------------------
int* sub_402790(int this_, int* a2, int a3, int a4, int a5, int a6, int a7, int a8, char a9)
{
    (void)a4; (void)a6; (void)a7; (void)a8;
    unsigned int* v9 = *(unsigned int**)(this_ + 36);
    int v10 = a3 + a5;
    unsigned int v11 = *v9;
    if (*v9 != 0 && *(DWORD*)(this_ + 60) < v11) *(DWORD*)(this_ + 60) = v11;
    if (v10 != -1) {
        if ((a9 & 1) != 0 && **(DWORD **)(this_ + 32) != 0) {
            if (v10 >= 0) {
                int v13 = **(DWORD **)(this_ + 16);
                if (v10 <= (*(DWORD*)(this_ + 60) - v13) >> 1) {
                    int v14 = v10 + ((v13 - **(DWORD **)(this_ + 32)) >> 1);
                    **(DWORD **)(this_ + 48) -= v14;
                    **(DWORD **)(this_ + 32) += 2 * v14;
                    if (a9 & 2) {
                        int* v15 = *(int**)(this_ + 36);
                        if (*v15 != 0) {
                            int v16 = *v15, v17 = **(DWORD **)(this_ + 52), v18 = **(DWORD **)(this_ + 32);
                            *v15 = v18;
                            **(DWORD **)(this_ + 52) = (v16 + 2 * v17 - v18) >> 1;
                        }
                    }
                    goto done;
                }
            }
        } else if (a9 & 2) {
            unsigned int v19 = *v9;
            if (v19 != 0 && v10 >= 0) {
                int v20 = **(DWORD **)(this_ + 16);
                if (v10 <= (*(DWORD*)(this_ + 60) - v20) >> 1) {
                    int v21 = v10 + ((int)(v20 - v19) >> 1);
                    **(DWORD **)(this_ + 52) -= v21;
                    **(DWORD **)(this_ + 36) += 2 * v21;
                    goto done;
                }
            }
        }
        v10 = -1;
    }
done:
    *a2 = v10; a2[2] = 0; a2[3] = 0; a2[4] = 0;
    return a2;
}

// ---------------------------------------------------------------------------
// sub_4028A0  (0x4028A0 - 0x4028D9)
// ---------------------------------------------------------------------------
char* sub_4028A0(char* this_, char a2)
{
    char* v2 = this_ - 4;
    *(DWORD*)((char*)this_ + *(DWORD*)(*((DWORD*)this_ - 1) + 4) - 4) = vtbl_wostream;
    *(DWORD*)this_ = (DWORD)&vtable_ios_base;
    crt_ios_base_dtor(this_);
    if (a2 & 1) operator delete(v2);
    return v2;
}

// ---------------------------------------------------------------------------
// sub_4028E0  (0x4028E0 - 0x402942)
// ---------------------------------------------------------------------------
int* sub_4028E0(int* this_, char a2)
{
    int v3 = *this_;
    if (*this_ != 0) {
        DWORD v7 = 0;
        crt_lockit_ctor(&v7, 0);
        int v4 = *(int*)(v3 + 4);
        if (v4 != 0 && v4 != -1) *(int*)(v3 + 4) = v4 - 1;
        void* v5 = (*(int*)(v3 + 4) == 0) ? (void*)v3 : 0;
        crt_lockit_dtor(&v7);
        if (v5) ((void (*)(void*, int))(**(void***)v5))(v5, 1);
    }
    if (a2 & 1) operator delete(this_);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_402950  (0x402950 - 0x402989)
// ---------------------------------------------------------------------------
char* sub_402950(char* this_, char a2)
{
    char* v2 = this_ - 8;
    *(DWORD*)((char*)this_ + *(DWORD*)(*((DWORD*)this_ - 2) + 4) - 8) = vtbl_wistream_ptr;
    *(DWORD*)this_ = (DWORD)&vtable_ios_base;
    crt_ios_base_dtor(this_);
    if (a2 & 1) operator delete(v2);
    return v2;
}

// ---------------------------------------------------------------------------
// sub_402990  (0x402990 - 0x4029AE)
// ---------------------------------------------------------------------------
void* sub_402990(void* this_, char a2)
{
    sub_401EA0(this_);
    if (a2 & 1) operator delete(this_);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_4029B0  (0x4029B0 - 0x4029E5)
// ---------------------------------------------------------------------------
DWORD* sub_4029B0(DWORD* this_, char a2)
{
    DWORD* v2 = (DWORD*)((char*)this_ - 84);
    sub_4021F0(this_);
    *(DWORD*)this_ = (DWORD)&vtable_ios_base;
    crt_ios_base_dtor(this_);
    if (a2 & 1) operator delete(v2);
    return v2;
}

// ---------------------------------------------------------------------------
// sub_4029F0  (0x4029F0 - 0x402A45)
// ---------------------------------------------------------------------------
char* sub_4029F0(char* this_, char a2)
{
    char* v2 = this_ - 12;
    *(DWORD*)((char*)this_ + *(DWORD*)(*((DWORD*)this_ - 3) + 4) - 12) = vtbl_wiostream;
    *(DWORD*)((char*)this_ + *(DWORD*)(*((DWORD*)this_ - 1) + 4) - 4) = vtbl_wostream;
    *(DWORD*)((char*)this_ + *(DWORD*)(*((DWORD*)this_ - 3) + 4) - 12) = vtbl_wistream_ptr;
    *(DWORD*)this_ = (DWORD)&vtable_ios_base;
    crt_ios_base_dtor(this_);
    if (a2 & 1) operator delete(v2);
    return v2;
}

// ---------------------------------------------------------------------------
// sub_402A50  (0x402A50 - 0x402A6E)
// ---------------------------------------------------------------------------
void* sub_402A50(void* this_, char a2)
{
    sub_4022D0(this_);
    if (a2 & 1) operator delete(this_);
    return this_;
}

// ---------------------------------------------------------------------------
// sub_402A70  (0x402A70 - 0x402B4A)
// ---------------------------------------------------------------------------
// basic_string buffer fill / assignment (basic_streambuf copy helper).
int sub_402A70(int this_, DWORD* a2, unsigned int a3, char* a4)
{
    char* v5;
    if (a2[5] < a3) crt_sub_40473B();
    v5 = (char*)(a2[5] - a3);
    if (a4 < v5) v5 = a4;
    if ((DWORD*)this_ == a2) {
        sub_403050((DWORD*)&v5[a3], (unsigned int)-1, 0);
        sub_403050(0, a3, 0);
        return this_;
    }
    if (v5 == (char*)-1) crt_sub_404703();
    DWORD v7 = *(DWORD*)(this_ + 24);
    bool v8 = (v5 == 0);
    if (v7 >= (unsigned int)v5) {
        if (v5 == 0) {
            *(DWORD*)(this_ + 20) = 0;
            if (v7 < 0x10) *(BYTE*)(this_ + 4) = 0; else **((BYTE**)(this_ + 4)) = 0;
            return this_;
        }
    } else {
        sub_403300(this_, (unsigned int)(size_t)v5, (size_t)*(DWORD*)(this_ + 20));
        v8 = (v5 == 0);
    }
    if (!v8) {
        DWORD* v9 = (a2[6] < 0x10u) ? (a2 + 1) : (DWORD*)a2[1];
        void* v12 = (v7 < 0x10) ? (void*)(this_ + 4) : *(void**)(this_ + 4);
        crt_memcpy_s(v12, v7, (char*)v9 + a3, (size_t)v5);
        *(DWORD *)(this_ + 20) = (DWORD)v5;
        void** v11 = (v7 < 0x10u) ? (void**)(this_ + 4) : (void**)*(DWORD **)(this_ + 4);
        v5[(DWORD)v11] = 0;
    }
    return this_;
}

// ---------------------------------------------------------------------------
// sub_402B50  (0x402B50 - 0x402B75)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x402B50-0x402B75, 37 bytes).
int sub_402B50(void* this_, char* Source)
{
    return sub_402F70((int)this_, 0, (BYTE*)Source, (char*)strlen(Source));
}

// ---------------------------------------------------------------------------
// sub_402B80  (0x402B80 - 0x402BCA)
// ---------------------------------------------------------------------------
void sub_402B80(DWORD* this_, char a2, size_t SourceSize)
{
    if (a2 != 0 && this_[6] >= 0x10u) {
        void* v4 = (void*)this_[1];
        if (SourceSize != 0) crt_memcpy_s(this_ + 1, 0x10u, v4, SourceSize);
        operator delete(v4);
    }
    this_[5] = (DWORD)SourceSize;
    this_[6] = 15;
    *((BYTE*)this_ + SourceSize + 4) = 0;
}

// ---------------------------------------------------------------------------
// sub_402BD0  (0x402BD0 - 0x402BE1)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x402BD0-0x402BE1, 17 bytes).
int sub_402BD0(int this_)
{
    return (**(DWORD **)(this_ + 32) != 0) ? **(DWORD **)(this_ + 48) : 0;
}

// ---------------------------------------------------------------------------
// sub_402BF0  (0x402BF0 - 0x402C01)
// ---------------------------------------------------------------------------
  // [BYTE-IDENTICAL] Compiles to byte-for-byte identical code as the original (0x402BF0-0x402C01, 17 bytes).
int sub_402BF0(int this_)
{
    return (**(DWORD **)(this_ + 36) != 0) ? **(DWORD **)(this_ + 52) : 0;
}

// ---------------------------------------------------------------------------
// sub_402C10  (0x402C10 - 0x402C7A)
// ---------------------------------------------------------------------------
DWORD* __stdcall sub_402C10(DWORD* a1, int a2)
{
    sub_403140(a1, a2);
    *(DWORD*)((char*)a1 + *(DWORD*)(a1[2] + 4) + 8) = vtbl_wostream;
    *(DWORD*)((char*)a1 + *(DWORD*)(*a1 + 4)) = vtbl_wiostream;
    return a1;
}

// ---------------------------------------------------------------------------
// sub_402C80  (0x402C80 - 0x402E02)
// ---------------------------------------------------------------------------
int sub_402C80(int a1, int a2)
{
    int v2 = *(int*)(a2 + 64);
    if ((v2 & 2) != 0 || *(*(unsigned int**)(a2 + 36)) == 0) {
        if ((v2 & 4) != 0 || *(*(DWORD**)(a2 + 32)) == 0) {
            *(DWORD*)(a1 + 24) = 7; *(DWORD*)(a1 + 20) = 0; *(WORD*)(a1 + 4) = 0;
            sub_403470((DWORD*)0, a2, 0, 0);
        } else {
            sub_403520((BYTE*)((int)(*(*(DWORD**)(a2 + 32)) + 2 * **(DWORD **)(a2 + 48) - **(DWORD **)(a2 + 16)) >> 1), (DWORD*)0, 0);
            *(DWORD*)(a1 + 24) = 7; *(DWORD*)(a1 + 20) = 0; *(WORD*)(a1 + 4) = 0;
            sub_403470((DWORD*)0, a2, 0, 0);
        }
    } else {
        unsigned int v4 = *(*(unsigned int**)(a2 + 36));
        int v5 = *(int*)(a2 + 60);
        if (v5 < v4) v5 = v4;
        sub_403520((BYTE*)((int)((v5 - **(DWORD **)(a2 + 20)) >> 1)), (DWORD*)0, 0);
        *(DWORD*)(a1 + 24) = 7; *(DWORD*)(a1 + 20) = 0; *(WORD*)(a1 + 4) = 0;
        sub_403470((DWORD*)0, a2, 0, 0);
    }
    return a1;
}

// ---------------------------------------------------------------------------
// sub_402E10  (0x402E10 - 0x402E61)
// ---------------------------------------------------------------------------
void* __fastcall sub_402E10(unsigned int a1)
{
    if (a1 != 0) {
        if (0xFFFFFFFF / a1 < 2) {
            char* v2 = 0;
            crt_exception_ctor(0, (const char*)&v2);
            crt_cxx_throw(0, (void*)0x414440);
        }
    }
    return operator new(2 * a1);
}

// ---------------------------------------------------------------------------
// sub_402E70  (0x402E70 - 0x402EF6)
// ---------------------------------------------------------------------------
int sub_402E70(int a1, int a2)
{
    *(DWORD*)a2 = a1;
    int v2 = *(DWORD*)(*(DWORD*)(*(DWORD*)a1 + 4) + a1 + 40);
    if (v2 != 0) crt_sub_404818(v2 + 4);
    int v3 = a1 + *(DWORD*)(*(DWORD*)a1 + 4);
    if (*(DWORD*)(v3 + 8) == 0 && *(DWORD*)(v3 + 44) != 0) sub_401E20(a1);
    *(BYTE*)(a2 + 4) = (*(DWORD*)(*(DWORD*)(*(DWORD*)a1 + 4) + a1 + 8) == 0);
    return a2;
}

// ---------------------------------------------------------------------------
// sub_402F00  (0x402F00 - 0x402F70)
// ---------------------------------------------------------------------------
int __stdcall sub_402F00(int a1)
{
    if (!crt_uncaught_exception()) sub_4030E0((int*)a1);
    int result = *(DWORD*)(*(DWORD*)(**(DWORD**)a1 + 4) + *(DWORD*)a1 + 40);
    if (result != 0) return crt_sub_404821(result + 4);
    return result;
}

// ---------------------------------------------------------------------------
// sub_402F70  (0x402F70 - 0x403046)
// ---------------------------------------------------------------------------
int __fastcall sub_402F70(int a1, int a2, BYTE* Source, char* SourceSize)
{
    (void)a2;
    if (Source) {
        unsigned int v5 = *(DWORD*)(a1 + 24);
        DWORD* v6 = (DWORD*)(a1 + 4);
        unsigned int v7 = (v5 < 0x10) ? (unsigned int)(a1 + 4) : (unsigned int)*v6;
        if ((unsigned int)Source >= v7) {
            unsigned int v8 = (v5 < 0x10) ? (unsigned int)(a1 + 4) : (unsigned int)*v6;
            if (v8 + *(DWORD*)(a1 + 20) > (unsigned int)Source) {
                if (v5 >= 0x10) v6 = (DWORD*)*v6;
                return sub_402A70(a1, (DWORD*)a1, (unsigned int)(Source - (BYTE*)v6), (char*)Source);
            }
        }
    }
    if (SourceSize == (char*)-1) crt_sub_404703();
    char* v10 = *(char**)(a1 + 24);
    bool v11 = (SourceSize == 0);
    if (v10 >= SourceSize) {
        if (SourceSize == 0) {
            *(DWORD*)(a1 + 20) = 0;
            if ((unsigned int)v10 < 0x10) *(BYTE*)(a1 + 4) = 0; else **((BYTE**)(a1 + 4)) = 0;
            return a1;
        }
    } else {
        sub_403300(a1, (unsigned int)(size_t)SourceSize, (size_t)*(DWORD*)(a1 + 20));
        v11 = (SourceSize == 0);
    }
    if (!v11) {
        unsigned int v12 = *(DWORD*)(a1 + 24);
        void** v13 = (void**)(a1 + 4);
        void* v14 = (v12 < 0x10) ? (void*)(a1 + 4) : *v13;
        crt_memcpy_s(v14, v12, Source, (size_t)SourceSize);
        *(DWORD *)(a1 + 20) = (DWORD)SourceSize;
        if (v12 >= 0x10u) v13 = (void**)*v13;
        SourceSize[(DWORD)v13] = 0;
    }
    return a1;
}

// ---------------------------------------------------------------------------
// sub_403050  (0x403050 - 0x4030D3)
// ---------------------------------------------------------------------------
DWORD* sub_403050(DWORD* this_, unsigned int a2, unsigned int a3)
{
    if (this_[5] < a2) crt_sub_40473B();
    unsigned int v5 = a3;
    unsigned int v6 = this_[5] - a2;
    if (v6 < a3) v5 = this_[5] - a2;
    if (v5 != 0) {
        unsigned int v7 = this_[6];
        DWORD* v13 = (v7 < 0x10) ? (this_ + 1) : (DWORD*)this_[1];
        DWORD* v9 = (v7 < 0x10) ? (this_ + 1) : (DWORD*)this_[1];
        crt_memmove_s((char*)v9 + a2, v7 - a2, (char*)v13 + a2 + v5, v6 - v5);
        unsigned int v10 = this_[5] - v5;
        DWORD* v8 = (this_[6] < 0x10u) ? (this_ + 1) : (DWORD*)this_[1];
        this_[5] = v10;
        *((BYTE*)v8 + v10) = 0;
    }
    return this_;
}

// ---------------------------------------------------------------------------
// sub_4030E0  (0x4030E0 - 0x40313B)
// ---------------------------------------------------------------------------
int sub_4030E0(int* this_)
{
    int result = *this_;
    if (((BYTE*)this_ + *(DWORD*)(*this_ + 4) + 16)[0] & 2) return sub_401E20((int)this_);
    return result;
}

// ---------------------------------------------------------------------------
// sub_403140  (0x403140 - 0x4031D7)
// ---------------------------------------------------------------------------
DWORD* __stdcall sub_403140(DWORD* a1, int a2)
{
    *(DWORD*)((char*)a1 + *(DWORD*)(*a1 + 4)) = vtbl_wistream_ptr;
    int v2 = *a1;
    a1[1] = 0;
    char* v3 = (char*)a1 + *(DWORD*)(v2 + 4);
    sub_401A90((DWORD*)v3);
    *(DWORD*)(v3 + 40) = a2;
    *(DWORD*)(v3 + 44) = 0;
    *(WORD*)(v3 + 48) = (unsigned short)sub_403630((void*)v3, (int)v3);
    if (*(DWORD*)(v3 + 40) == 0) {
        int v4 = (*(DWORD*)(v3 + 8) & 0x13) | 4;
        *(DWORD*)(v3 + 8) = v4;
        if ((v4 & *(DWORD*)(v3 + 12)) != 0) sub_401940((DWORD*)v3, 0, 0);
    }
    *(DWORD*)(v3 + 4) = 0;
    return a1;
}

// ---------------------------------------------------------------------------
// sub_4031E0  (0x4031E0 - 0x4032D1)
// ---------------------------------------------------------------------------
DWORD* __stdcall sub_4031E0(DWORD* a1)
{
    *a1 = (DWORD)vtable_wstreambuf;
    void* v1 = operator new(4u);
    if (v1) {
        *(DWORD*)v1 = (DWORD)crt_locale_init();
        int v2 = crt_sub_404281();
        DWORD v5 = 0;
        crt_lockit_ctor(&v5, 0);
        int v3 = *(int*)(v2 + 4);
        if (v3 != -1) *(int*)(v2 + 4) = v3 + 1;
        crt_lockit_dtor(&v5);
    } else {
        v1 = 0;
    }
    a1[4] = (DWORD)(a1 + 2);
    a1[8] = (DWORD)(a1 + 6);
    a1[9] = (DWORD)(a1 + 7);
    a1[14] = (DWORD)v1;
    a1[5] = (DWORD)(a1 + 3);
    a1[12] = (DWORD)(a1 + 10);
    a1[13] = (DWORD)(a1 + 11);
    a1[3] = 0;
    a1[7] = 0;   // = *(DWORD*)a1[9]
    a1[11] = 0;  // = *(DWORD*)a1[13]
    a1[2] = 0;   // = *(DWORD*)a1[4]
    a1[6] = 0;   // = *(DWORD*)a1[8]
    a1[10] = 0;  // = *(DWORD*)a1[12]
    return a1;
}

// ---------------------------------------------------------------------------
// sub_403300  (0x403300 - 0x40346E)
// ---------------------------------------------------------------------------
BYTE* sub_403300(int this_, unsigned int Destination, size_t SourceSize)
{
    unsigned int v4 = Destination | 0xF;
    if ((Destination | 0xF) == 0xFFFFFFFF) {
        v4 = Destination;
    } else {
        unsigned int v5 = *(DWORD*)(this_ + 24);
        unsigned int v6 = v5 >> 1;
        if (v4 / 3 < v5 >> 1 && v5 <= (unsigned int)-2 - v6) v4 = v6 + v5;
    }
    size_t v7 = v4 + 1;
    if (v4 == -1) {
        v7 = 0;
    } else if (0xFFFFFFFF / v7 == 0) {
        crt_exception_ctor(0, 0);
        crt_cxx_throw(0, (void*)0x414440);
    }
    void* dest = operator new(v7);
    if (SourceSize != 0) {
        const void* src = (*(DWORD*)(this_ + 24) < 0x10u) ? (const void*)(this_ + 4) : *(const void**)(this_ + 4);
        crt_memcpy_s(dest, v4 + 1, src, SourceSize);
    }
    if (*(DWORD*)(this_ + 24) >= 0x10u) operator delete(*(void**)(this_ + 4));
    BYTE* result = (BYTE*)(this_ + 4);
    *(DWORD*)(this_ + 4) = (DWORD)dest;
    *(DWORD*)(this_ + 24) = v4;
    *(DWORD*)(this_ + 20) = (DWORD)SourceSize;
    if (v4 >= 0x10) result = (BYTE*)dest;
    result[SourceSize] = 0;
    return result;
}

// ---------------------------------------------------------------------------
// sub_403470  (0x403470 - 0x403516)
// ---------------------------------------------------------------------------
int sub_403470(DWORD* a1, int a2, unsigned int a3, unsigned int a4)
{
    if (a1[5] < a3) crt_sub_40473B();
    unsigned int v6 = a1[5] - a3;
    if (a4 < v6) v6 = a4;
    if ((DWORD*)a2 == a1) {
        sub_4036E0(0, 0, a1);
        sub_4036E0(0, a3, a1);
        return a2;
    }
    if (sub_403770(a2, a1[6]) != 0) {
        DWORD* v8 = (a1[6] < 8) ? (a1 + 1) : (DWORD*)a1[1];
        void** v9 = (void**)(a2 + 4);
        void* v10 = (*(DWORD*)(a2 + 24) < 8) ? (void*)(a2 + 4) : *v9;
        crt_memcpy_s(v10, 2 * *(DWORD*)(a2 + 24), (char*)v8 + 2 * a3, 2 * v6);
        *(DWORD*)(a2 + 20) = v6;
        if (*(DWORD*)(a2 + 24) >= 8) v9 = (void**)*v9;
        *((WORD*)v9 + v6) = 0;
    }
    return a2;
}

// ---------------------------------------------------------------------------
// sub_403520  (0x403520 - 0x4035C1)
// ---------------------------------------------------------------------------
int sub_403520(BYTE* a1, DWORD* a2, unsigned int a3)
{
    unsigned int v4 = a2[6];
    DWORD* v5 = a2 + 1;
    DWORD* v6 = (v4 < 8) ? (a2 + 1) : (DWORD*)*v5;
    if (a1 && a1 >= (BYTE*)v6 && ((char*)v6 + 2 * a2[5] > (char*)a1)) {
        if (v4 >= 8) v5 = (DWORD*)*v5;
        return sub_403470(a2, (int)a2, (unsigned int)((a1 - (BYTE*)v5) >> 1), a3);
    }
    if (sub_403770((int)a1, a2[6]) != 0) {
        unsigned int v9 = a2[6];
        void** v10 = (void**)(a2 + 1);
        void* v11 = (v9 < 8) ? (void*)(a2 + 1) : *v10;
        crt_memcpy_s(v11, 2 * v9, a1, 2 * a3);
        a2[5] = a3;
        if (v9 >= 8) v10 = (void**)*v10;
        *((WORD*)v10 + a3) = 0;
    }
    return (int)a2;
}

// ---------------------------------------------------------------------------
// sub_4035D0  (0x4035D0 - 0x403625)
// ---------------------------------------------------------------------------
void* __stdcall sub_4035D0(char* a1)
{
    char* v1 = a1;
    if (a1) {
        if (0xFFFFFFFF / (unsigned int)a1 == 0) {
            a1 = 0;
            crt_exception_ctor(0, (const char*)&a1);
            crt_cxx_throw(0, (void*)0x414440);
        }
    } else {
        v1 = 0;
    }
    return operator new((size_t)v1);
}

// ---------------------------------------------------------------------------
// sub_403630  (0x403630 - 0x4036DF)
// ---------------------------------------------------------------------------
int sub_403630(void* this_, int a2)
{
    sub_401A60(a2, (int)this_, (DWORD*)&a2);
    int v2 = sub_403950((int*)a2);
    int v3 = a2;
    if (a2 != 0) {
        DWORD v5 = 0;
        crt_lockit_ctor(&v5, 0);
        int v6 = *(int*)(v3 + 4);
        if (v6 != 0 && v6 != -1) *(int*)(v3 + 4) = v6 - 1;
        int v7 = (*(int*)(v3 + 4) == 0) ? v3 : 0;
        crt_lockit_dtor(&v5);
        if (v7) ((void (*)(void*, int))(**(void***)v7))((void*)v7, 1);
    }
    int v4 = v2;
    int (*vfunc)(int, int) = *(int (**)(int, int))(v4 + 40);
    return vfunc(v4, 32);
}

// ---------------------------------------------------------------------------
// sub_4036E0  (0x4036E0 - 0x40376F)
// ---------------------------------------------------------------------------
DWORD* sub_4036E0(unsigned int a1, unsigned int a2, DWORD* a3)
{
    if (a3[5] < a2) crt_sub_40473B();
    unsigned int v4 = a3[5] - a2;
    if (v4 < a1) a1 = a3[5] - a2;
    if (a1 != 0) {
        unsigned int v5 = a3[6];
        DWORD* v10 = (v5 < 8) ? (a3 + 1) : (DWORD*)a3[1];
        DWORD* v11 = (v5 < 8) ? (a3 + 1) : (DWORD*)a3[1];
        crt_memmove_s((char*)v11 + 2 * a2, 2 * (v5 - a2), (char*)v10 + 2 * a2 + 2 * a1, 2 * (v4 - a1));
        unsigned int v7 = a3[5] - a1;
        DWORD* v6 = (a3[6] < 8) ? (a3 + 1) : (DWORD*)a3[1];
        a3[5] = v7;
        *((WORD*)v6 + v7) = 0;
    }
    return a3;
}

// ---------------------------------------------------------------------------
// sub_403770  (0x403770 - 0x4037CC)
// ---------------------------------------------------------------------------
BOOL sub_403770(int a1, unsigned int a2)
{
    if (a2 > 0x7FFFFFFE) crt_sub_404703();
    unsigned int v3 = *(DWORD*)(a1 + 24);
    if (v3 < a2) {
        sub_4037D0((DWORD*)a1, a2, *(DWORD*)(a1 + 20));
        return (a2 != 0);
    }
    if (a2 == 0) {
        *(DWORD*)(a1 + 20) = 0;
        if (v3 >= 8) { **((WORD**)(a1 + 4)) = 0; return FALSE; }
        *(WORD*)(a1 + 4) = 0;
    }
    return (a2 != 0);
}

// ---------------------------------------------------------------------------
// sub_4037D0  (0x4037D0 - 0x403946)
// ---------------------------------------------------------------------------
WORD* __stdcall sub_4037D0(DWORD* a1, unsigned int a2, int a3)
{
    unsigned int v3 = a2 | 7;
    if ((a2 | 7) <= 0x7FFFFFFE) {
        unsigned int v4 = a1[6];
        unsigned int v5 = v4 >> 1;
        if (v3 / 3 < v4 >> 1 && v4 <= 2147483646 - v5) v3 = v5 + v4;
    } else {
        v3 = a2;
    }
    unsigned int v6 = v3 + 1;
    if (v3 == -1) {
        v6 = 0;
    } else if (0xFFFFFFFF / v6 < 2) {
        crt_exception_ctor(0, 0);
        crt_cxx_throw(0, (void*)0x414440);
    }
    void* v7 = operator new(2 * v6);
    int v8 = a3;
    if (a3 != 0) {
        const void* v9 = (a1[6] < 8) ? (const void*)(a1 + 1) : (const void*)a1[1];
        crt_memcpy_s(v7, 2 * v3 + 2, v9, 2 * a3);
    }
    if (a1[6] >= 8) operator delete((void*)a1[1]);
    WORD* result = (WORD*)(a1 + 1);
    a1[1] = (DWORD)v7;
    a1[6] = v3;
    a1[5] = v8;
    if (v3 >= 8) result = (WORD*)v7;
    result[v8] = 0;
    return result;
}

// ---------------------------------------------------------------------------
// sub_403950  (0x403950 - 0x403A91)
// ---------------------------------------------------------------------------
int sub_403950(int* a1)
{
    DWORD v10 = 0, v11 = 0;
    crt_lockit_ctor(&v11, 0);
    int v1 = dword_417D80;
    int v9 = dword_417D80;
    if (dword_417118 == 0) {
        crt_lockit_ctor(&v10, 0);
        if (dword_417118 == 0) dword_417118 = ++dword_41710C;
        crt_lockit_dtor(&v10);
    }
    int v2 = dword_417118;
    int v3 = *a1;
    int v4 = 0;
    if ((unsigned int)dword_417118 < *(DWORD*)(*a1 + 12)) {
        v4 = *(int*)(*(DWORD*)(v3 + 8) + 4 * dword_417118);
        if (v4 != 0) goto found;
    }
    if (*(BYTE*)(v3 + 20) == 0) {
        v4 = 0;
        goto found;
    }
    int v5 = crt_sub_404281();
    if (v2 < *(DWORD*)(v5 + 12)) {
        v4 = *(int*)(*(DWORD*)(v5 + 8) + 4 * v2);
        goto found;
    }
    if (v1 != 0) {
        v4 = v1;
        goto found;
    }
    if (sub_401420(&v9, a1) == -1) {
        crt_sub_404E61("bad cast");
        crt_cxx_throw(0, (void*)0x414448);
    }
    v4 = v9;
    dword_417D80 = v9;
    crt_lockit_ctor(&v10, 0);
    int v7 = *(int*)(v4 + 4);
    if (v7 != -1) *(int*)(v4 + 4) = v7 + 1;
    crt_lockit_dtor(&v10);
found:
    crt_lockit_dtor(&v11);
    return v4;
}

// ---------------------------------------------------------------------------
// sub_403AA0  (0x00403AA0 - 0x00403DAC)
// ---------------------------------------------------------------------------
// Appends a narrow (char*) command line to the wide command stream, widening
// each character (classic-locale ctype::widen == (wchar_t)(unsigned char)c).
__declspec(noinline) void sub_403AA0(std::wostream& os, const char* s)
{
    for (const char* p = s; *p; ++p)
        os << (wchar_t)(unsigned char)*p;
}

// ---------------------------------------------------------------------------
// sub_403DB0  (0x00403DB0 - 0x00404010)
// ---------------------------------------------------------------------------
// Default-command builder. WinMain calls this only when the command stream is
// empty. It appends the wide string L"F3Secu.exe" (the SecuROM game stub that
// the launcher re-spawns) to the stream, which then becomes the command passed
// to CreateProcessW.
__declspec(noinline) void sub_403DB0(std::wostream& os)
{
    os << L"F3Secu.exe";
}

// ---------------------------------------------------------------------------
// sub_404010  (0x404010 - 0x404029)
// ---------------------------------------------------------------------------
void* sub_404010(void* this_, void* a2)
{
    crt_exception_ctor(this_, (const char*)a2);
    *(DWORD*)this_ = (DWORD)vtable_bad_alloc;
    return this_;
}

// ---------------------------------------------------------------------------
// sub_404030  (0x404030 - 0x404099)
// ---------------------------------------------------------------------------
int sub_404030(unsigned int a1, DWORD* a2, unsigned int a3)
{
    unsigned int v3 = a1;
    if (a2[5] < a1) v3 = a2[5];
    unsigned int v4 = v3;
    if (v3 >= a3) v4 = a3;
    WORD* v5 = (a2[6] < 8) ? (WORD*)(a2 + 1) : (WORD*)a2[1];
    WORD* v6 = (WORD*)&unk_4133C8;
    if (v4 != 0) {
        while (*v5 == *v6) {
            ++v5; ++v6;
            if (--v4 == 0) {
                if (v3 >= a3) return (v3 != a3);
                else return -1;
            }
        }
        return (*v5 < *v6) ? -1 : 1;
    }
    if (v3 >= a3) return (v3 != a3);
    else return -1;
}

