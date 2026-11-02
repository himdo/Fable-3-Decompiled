// Fable 3 — frontend run module implementation (RunFrontEnd, 0x61CB70, plus its
// module-private globals, the xlive.dll / steam_api import stubs, and the
// unimplemented engine callees it drives).

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // original used strstr / the CRT directly
#endif

#include "gamemain.h"
#include "common/common.h"        // LHAlloc / LHFree / InitSharedSlot / ReleaseSharedSlot
#include "manager.h"              // GetManager
#include "worker.h"               // GetWorker

#include <windows.h>
#include <process.h>              // _beginthread
#include <string.h>

// ============================================================
//  Globals referenced by RunFrontEnd (kept at their original IDs)
// ============================================================
// Steam / frontend state flags.
static BYTE  g_byte_1DBDC49 = 0;   // 0x1DBDC49: "ThisIsSteam" data present
static BYTE  g_byte_1DBDC4A = 0;   // 0x1DBDC4A: SteamAPI initialized
static BYTE  g_byte_1DBD9E0 = 0;   // 0x1DBD9E0: frontend "entered" flag
// Locale IDs for the sign-in error / guide message boxes.
static DWORD g_dword_1DBDA90 = 0;  // 0x1DBDA90
static DWORD g_dword_1DBDA8C = 0;  // 0x1DBDA8C
static DWORD g_dword_1DBDA80 = 0;  // 0x1DBDA80
static DWORD g_dword_1DBDA84 = 0;  // 0x1DBDA84
static DWORD g_dword_1DBDA88 = 0;  // 0x1DBDA88
static DWORD g_dword_1DBDA7C = 0;  // 0x1DBDA7C
// Parsed command-line / locale words handed to the manager run call.
static DWORD g_dword_1C88DE4 = 0;  // 0x1C88DE4
static DWORD g_dword_1C88DE8 = 0;  // 0x1C88DE8
static DWORD g_dword_1C86BF0 = 0;  // 0x1C86BF0
static DWORD g_dword_1C86C08 = 0;  // 0x1C86C08
// Misc flags read / written by the frontend.
static BYTE  g_byte_1DBD030 = 0;   // 0x1DBD030
static BYTE  g_byte_1DBD034 = 0;   // 0x1DBD034: WaitBeforeEnteringFrontEnd on the command line
static BYTE  g_byte_1DBD03E = 0;   // 0x1DBD03E
static BYTE  g_byte_1DBD01F = 0;   // 0x1DBD01F
static BYTE  g_byte_2075F07 = 0;   // 0x2075F07
// Function-pointer slots published by the frontend (0x1D8C850 / 0x1D8C854).
static void *g_off_1D8C850 = 0;    // 0x1D8C850
static void *g_off_1D8C854 = 0;    // 0x1D8C854
// Global "this" objects the frontend dispatches through (0x1DBD048 / 0x1DBD9E8).
// The callees (stubs) never dereference them, so a zeroed block stands in.
static int   g_unk_1DBD048[8] = { 0 };   // 0x1DBD048
static int   g_unk_1DBD9E8[8] = { 0 };   // 0x1DBD9E8
// Placeholder vtables for the two file objects created on the frontend.
static void *g_CDiskFileWin32Vtable[4] = { 0 };
static void *g_CAFileVtable[4]         = { 0 };

// ============================================================
//  xlive.dll / steam_api import stubs
//
//  In the original these are imported from xlive.dll (the bundled XLiveAPI
//  shim) and steam_api. This self-contained build does not link either, so
//  they are stubbed here. XUserGetSigninState reports "signed in" so the
//  frontend takes the already-signed-in path and skips the blocking XNA
//  sign-in / guide / message-box UI loops.
// ============================================================
static int   XNotifyCreateListener(int a, int b) { (void)a; (void)b; return 1; }
static int   XNotifyGetNext(int listener, int a, void *hi, void *lo)
{ (void)listener; (void)a; (void)hi; (void)lo; return 0; }
static int   XUserGetSigninState(int user) { (void)user; return 3; }   // XUSER_STATE_SIGNED_IN
static int   XShowSigninUI(int a, int b) { (void)a; (void)b; return 0; }
static int   XShowGuideUI(int a) { (void)a; return 0; }
static int   XShowMessageBoxUI(int a, int b, int c, int d, void *e,
                               int f, int g, void *h, void *i)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i; return 6; }
static int   XCancelOverlapped(void *p) { (void)p; return 0; }
static int   SteamAPI_Init(void) { return 0; }

// ============================================================
//  Unimplemented engine callees (stubs)
//
//  Each is a minimal stand-in matching the original call's argument count so
//  the frontend's control flow, stack usage, and call sequence are preserved.
//  They are "remaining stubs": real per-function behavior is out of scope
//  until each is decompiled on its own.
// ============================================================
static void  sub_18A89A0(void) {}
static void  sub_6A7210(void) {}
static int   sub_1A01CC0(void *obj, const char *path, int size)
{ (void)obj; (void)path; (void)size; return 0; }
static int   sub_18B0F30(void *obj) { (void)obj; return 0; }   // false -> no error box
static void  sub_775EE0(void *obj) { (void)obj; }
static void  sub_72F7B0(void *obj) { (void)obj; }
static void  sub_775D10(void *str, char c) { (void)str; (void)c; }
static int   sub_775DA0(void *str, char c) { (void)str; (void)c; return 0; }

static int   g_sub61A000Buf[2] = { 0, 0 };
static int  *sub_61A000(void *obj) { (void)obj; return g_sub61A000Buf; }

static void  sub_67C930(void *obj) { (void)obj; }
static void  sub_18B1360(void *obj) { (void)obj; }
static void  sub_18C5150(void *obj, void *a, int b, int c)
{ (void)obj; (void)a; (void)b; (void)c; }
static void  sub_18C4840(void *obj) { (void)obj; }

static int   sub_1A02560(void *obj, void *str) { (void)obj; (void)str; return 0; }
static void  sub_19FFA70(void *str) { (void)str; }
static void  sub_19FFA20(void *str) { (void)str; }
static int   sub_19FF560(void *str) { (void)str; return 0; }
static int   sub_18F02B0(void) { return 0; }

static int   g_sub18F1100Buf[2] = { 0, 0 };
static int  *sub_18F1100(void *obj) { (void)obj; return g_sub18F1100Buf; }

static void  sub_18E6D40(void) {}
static void  sub_619D70(void *p) { (void)p; }                     // _beginthread entry (no-op)
static void  sub_61C320(void *p) { (void)p; }                     // _beginthread entry (no-op)
static int   sub_6A66C0(void) { return 0; }
static int   sub_6A6960(int a) { (void)a; return 0; }
static int   sub_6A69A0(int a) { (void)a; return 0; }
static void  sub_772190(int a) { (void)a; }
static void  sub_61B090(void *obj) { (void)obj; }
static void  sub_18FBD00(int a) { (void)a; }
static void  nullsub_7(void) {}
static void  sub_770960(void) {}
static void  sub_620F30(void *obj) { (void)obj; }
static void *sub_18EADB0(void *obj) { return obj; }
static void  sub_18EB1A0(void *obj) { (void)obj; }
static void  sub_719770(void) {}
static int   sub_1801C20(void) { return 0; }
static void  sub_4358A0(void *obj, const char *name) { (void)obj; (void)name; }
static void  sub_1801580(void *obj, void *str) { (void)obj; (void)str; }
static void *sub_767360(void *obj) { return obj; }
static void *sub_762490(void *obj, void *a) { (void)a; return obj; }
static void  sub_6234A0(void *obj) { (void)obj; }
static void  sub_7672E0(void *obj) { (void)obj; }
static int   sub_760FA0(void *obj) { (void)obj; return 0; }
static void  sub_72EDF0(void *obj, int a) { (void)obj; (void)a; }
static void  sub_61A470(void *obj, int a) { (void)obj; (void)a; }
static void *sub_754DE0(void *obj) { return obj; }
static void  sub_627690(void *obj) { (void)obj; }
static void  sub_619990(void) {}
static void  sub_6199A0(void) {}
static void  sub_18FA3F0(void) {}
static void  sub_18C4560(void *obj, int a, int b) { (void)obj; (void)a; (void)b; }
static int   sub_18FA3E0(void *hwnd) { (void)hwnd; return 0; }
static int   sub_18FA3D0(int a) { (void)a; return 0; }
static void  sub_145D7B0(int a) { (void)a; }
static void  sub_741E50(int a) { (void)a; }

// Worker callback functions the frontend installs (their addresses are stored
// in the worker at +0x25C/+0x260/+0x264/+0x268 by the sub_18F6Cxx setters).
static void  sub_61A330(void) {}
static void  sub_619DE0(void) {}
static void  sub_6199C0(void) {}
static void  sub_619A70(void) {}

// Worker callback setters (thiscall, worker in ECX + one function pointer).
// The originals store the pointer at worker +0x25C/+0x260/+0x264/+0x268; the
// worker object in this build is smaller, so they are stubbed as no-ops.
static void  sub_18F6C90(void *worker, void *fn) { (void)worker; (void)fn; }
static void  sub_18F6C80(void *worker, void *fn) { (void)worker; (void)fn; }
static void  sub_18F6C70(void *worker, void *fn) { (void)worker; (void)fn; }
static void  sub_18F6C60(void *worker, void *fn) { (void)worker; (void)fn; }

static void  sub_402F00(void *obj, const char *name, int size)
{ (void)obj; (void)name; (void)size; }
static int   g_sub18013B0Word = 0;
static void *sub_18013B0(void *obj, void *a, void *b)
{ (void)obj; (void)a; (void)b; return &g_sub18013B0Word; }
static void  sub_18E8CD0(void) {}
static int   sub_18E8960(int a, int b) { (void)a; (void)b; return 0; }
static void  sub_18E8D50(void *obj, int a) { (void)obj; (void)a; }
static void  sub_18E7180(void) {}
static void  sub_18E84C0(void) {}
static void *sub_7330F0(void *obj) { return obj; }
static void *sub_72FB30(void *obj) { return obj; }
static void  sub_72FB10(void *obj, void *argList, void *a, void *b)
{ (void)obj; (void)argList; (void)a; (void)b; }
static void  nullsub_28(int a) { (void)a; }
static void  sub_70DDB0(int a) { (void)a; }
static void  sub_18F0180(void) {}
static void  sub_72FAE0(void *obj) { (void)obj; }
static void  sub_61BFE0(void *obj, int a) { (void)obj; (void)a; }
static void  sub_61A5E0(void *obj) { (void)obj; }
static void *sub_731B80(void *obj) { return obj; }
static void *sub_712430(void *obj) { return obj; }
static void  nullsub_29(void) {}
static void  sub_18FB930(void) {}
static void  sub_40E1F0(void *obj) { (void)obj; }

// F3_VT(OBJ): read OBJ's vtable (the first word) as a void ** so its slots
// can be indexed. (OBJ is a class instance pointer.)
#define F3_VT(OBJ) ((void **)(*(void **)(OBJ)))

// ============================================================
//  RunFrontEnd  (0x0061CB70 - 0x0061D7D6)  -- the game start-up / frontend
//  driver
//  [NOT byte-identical] placement-dependent (hundreds of relative calls to
//  engine callees, xlive.dll / steam_api imports, and Win32 functions) and
//  it references high-VA data globals / IAT slots.
//
//  char RunFrontEnd(void *obj, int arg)   (obj = the CGameEngine, 356 bytes)
//
//  thiscall + `retn 4` in the original: obj in ECX, one int on the stack
//  (the game-thread argument / command-line int), result byte in AL. Returns
//  1 on success (the frontend ran and the device / threads were started), 0
//  when the user cancelled sign-in / the frontend was aborted.
//
//  The control flow mirrors the disassembly: the Steam / XNA set-up, the
//  manager "run the game" dispatch, the XNA sign-in / guide / message-box UI
//  loop, the device + engine sub-object construction, and the shared cleanup
//  epilogue (success returns 1, cancel / failure returns 0).
// ============================================================
char RunFrontEnd(void *obj, int arg)
{
    char   *base = (char *)obj;
    DWORD  *dw   = (DWORD *)obj;

    // ---- locals (the original's stack frame) ----
    F3SharedBuffer *steamSlot = 0;          // the "Data/ThisIsSteam" string slot
    char            v108[100];              // sub_775EE0 object
    DWORD           v70[5];                 // sub_72F7B0 object
    DWORD           str103[4];              // the 16-byte string (v103..v106)
    DWORD           v107 = 0;               // the separate command-line word
    int             v84;                    // sub_61A000 slot
    F3SharedBuffer *v88 = 0;                // sub_67C930 / sub_18B0F30 slot
    DWORD           v93[6];                 // the CDiskFileWin32 / CAFile object
    F3SharedBuffer *v96;                    // InitSharedSlot / ReleaseSharedSlot slot
    DWORD           notify[2];              // the 8-byte XNotify buffer
    int             v76;                    // sub_1A02560 string
    int             v85, v86;               // sub_18F1100 strings
    DWORD           v91, v92, v89, v90;     // sign-in UI strings
    DWORD           v102[2];
    DWORD           v97[2];
    int             v98, v99, v100, v101;
    int             v72;                    // var_16D (game ran)
    int             v71;                    // var_16E (sign-in wait flag)
    int             v164;                   // sub_1801C20 result
    void           *v168 = 0;              // sub_18013B0 out-pointer
    char            v81[4];                 // "startup.vfsconfig" string
    int             vfs[4];                 // the 16-byte vfs config word block
    DWORD           modelHdr;               // "globals_model_headers" string
    int             v140, v154;             // sub_402F00 model-headers word block
    F3SharedBuffer *v160slot = 0;          // microsoft_logo.bik string slot
    char            result = 0;             // default: cancel / failure -> 0

    // 1. Framework / engine bootstrap.
    sub_18A89A0();
    sub_6A7210();

    // 2. Ensure the XNA notification listener exists (obj+0xE8).
    if (dw[0xEA / 4] == 0)
        dw[0xEA / 4] = XNotifyCreateListener(3, 0);

    // 3. Probe the Steam data slot ("Data/ThisIsSteam").
    sub_1A01CC0(&steamSlot, "Data/ThisIsSteam", -1);
    int steamOk = sub_18B0F30(&steamSlot);
    ReleaseSharedSlot(&steamSlot);
    if (steamOk != 0)
    {
        g_byte_1DBDC49 = 1;
        if (SteamAPI_Init() != 0)
            g_byte_1DBDC4A = 1;
    }

    // 4. Build the small frontend strings.
    sub_775EE0(v108);
    sub_72F7B0(v70);
    sub_775D10(str103, (char)v70[0]);

    // 5. Parse the command-line / locale words.
    int *parsed = sub_61A000(&v84);
    g_dword_1C88DE4 = (DWORD)parsed[0];
    g_dword_1C88DE8 = (DWORD)parsed[1];
    if (((v107 >> 8) & 0xFF) == 0)   // BYTE1(v107)
    {
        str103[1] = (DWORD)parsed[0];
        str103[2] = (DWORD)parsed[1];
    }

    // 6. Build the file object + shared slot; show an error box on failure.
    sub_67C930(&v88);
    v93[0] = (DWORD)g_CDiskFileWin32Vtable;
    memset(&v93[1], 0, 20);
    InitSharedSlot(&v96);
    if (sub_18B0F30(&v88) != 0)
    {
        void *mgr   = GetManager();
        void **vt   = (void **)*(void ***)mgr;
        const WCHAR *title = GetLocalizedString((const WCHAR ***)GetLocaleEntry(g_dword_1DBDA90));
        const WCHAR *body  = GetLocalizedString((const WCHAR ***)GetLocaleEntry(g_dword_1DBDA8C));
        HWND hwnd = (HWND)((int (*)(void *))vt[8])(mgr);
        MessageBoxW(hwnd, body, title, 0x30);   // MB_OK | MB_ICONINFORMATION
    }
    else
    {
        sub_18B1360(&v88);
        sub_18C5150(v93, &v88, 4, 2);
        sub_18C4840(v93);
    }

    v72 = 0;

    // 7. Dispatch the manager "run the game" vtable slot.
    {
        void *mgr  = GetManager();
        void **vt  = (void **)*(void ***)mgr;
        int   v13  = sub_1A02560(&g_unk_1DBD048, &v76);
        int   r    =
            ((int (*)(void *, int, int, int, int, int, int, int, int))vt[1])(
                mgr, (int)v107, (int)str103[1], (int)str103[2], (int)str103[0], arg,
                (int)g_dword_1C86BF0, (int)g_dword_1C86C08, v13);
        sub_19FFA70(&v76);
        if (r != 0)
        {
            int *p = sub_18F1100(&v85);
            if ((unsigned char)str103[3] != (unsigned char)sub_18F02B0()
                || (int)str103[1] != p[0]
                || (int)str103[2] != p[1])
            {
                str103[3] = (DWORD)sub_18F02B0();
                int *p2 = sub_18F1100(&v86);
                str103[1] = (DWORD)p2[0];
                str103[2] = (DWORD)p2[1];
                base[0x160] = 1;
                sub_72F7B0(v70);
                sub_775DA0(str103, (char)v70[0]);
            }
            sub_18E6D40();
            v72 = 1;
        }
    }

    // 8. Start the pump thread, set the cursor, and handle XNA sign-in.
    g_byte_1DBD9E0 = 1;
    _beginthread(sub_619D70, 0, 0);
    SetCursor(LoadCursorA(0, (LPCSTR)0x7F00));
    v71 = 1;
    if (XUserGetSigninState(0) == 0)
    {
        void *mgr = GetManager();
        void **vt = (void **)*(void ***)mgr;
        ((void (*)(void *, int))vt[12])(mgr, 1);
        XShowSigninUI(1, 0);
        v71 = 0;
    }

    notify[0] = 0;
    notify[1] = 0;
    while (v71 == 0)
    {
        if (XNotifyGetNext(dw[0xEA / 4], 0, &notify[1], &notify[0]) != 0
            && notify[1] == 9 && notify[0] == 0)   // 0x0000000900000000
            v71 = 1;
        Sleep(1);
    }

    // 9. The XNA sign-in / guide / message-box UI loop (skipped when signed in).
    if (XUserGetSigninState(0) == 0)
    {
        int cancelled = 0;
        while (1)
        {
            void *mgr = GetManager();
            void **vt = (void **)*(void ***)mgr;
            ((void (*)(void *, int))vt[12])(mgr, 1);
            sub_19FFA20(GetLocaleEntry(g_dword_1DBDA80));
            sub_19FFA20(GetLocaleEntry(g_dword_1DBDA84));
            sub_19FFA20(GetLocaleEntry(g_dword_1DBDA88));
            sub_19FFA20(GetLocaleEntry(g_dword_1DBDA7C));
            v102[0] = (DWORD)sub_19FF560(&v91);
            v102[1] = (DWORD)sub_19FF560(&v92);
            v98 = v99 = v100 = v101 = 0;
            v97[0] = v97[1] = 0;

            int code = 5;
            do
            {
                v70[0] = (DWORD)&v98;
                int a = sub_19FF560(&v89);
                int b = sub_19FF560(&v90);
                code = XShowMessageBoxUI(0, b, a, 2, v102, 0, 3, v97, &v98);
                if (code == 5)
                {
                    XCancelOverlapped(&v98);
                    v98 = v99 = v100 = v101 = 0;
                }
                Sleep(1);
            } while (code == 5);

            while ((DWORD)v98 == 997)
                Sleep(1);

            if (v97[0] == 1)
            {
                cancelled = 1;
                break;
            }

            v71 = 0;
            do
            {
                if (XNotifyGetNext(dw[0xEA / 4], 0, &notify[1], &notify[0]) != 0
                    && notify[1] == 9 && notify[0] == 0)
                    v71 = 1;
                Sleep(1);
            } while (v71 == 0);

            while (XShowGuideUI(0) != 0)
                Sleep(1);

            v71 = 0;
            do
            {
                if (XNotifyGetNext(dw[0xEA / 4], 0, &notify[1], &notify[0]) != 0
                    && notify[1] == 9 && notify[0] == 0)
                    v71 = 1;
                if (XUserGetSigninState(0) != 0)
                    break;
                Sleep(1);
            } while (v71 == 0);

            int signState = XUserGetSigninState(0);
            Sleep(1);
            sub_19FFA70(&v91);
            sub_19FFA70(&v89);
            sub_19FFA70(&v90);
            sub_19FFA70(&v92);

            if (signState != 0)
                goto signedIn;   // LABEL_46: the user signed in mid-loop
        }

        // Cancelled: tear down the file object, post WM_CLOSE, return 0.
        (void)cancelled;
        g_byte_1DBD9E0 = 0;
        sub_18C4560(&g_unk_1DBD9E8, -1, 0);
        {
            void *mgr = GetManager();
            void **vt = (void **)*(void ***)mgr;
            HWND hwnd = (HWND)((int (*)(void *))vt[8])(mgr);
            PostMessageA(hwnd, 0x10, 0, 0);   // WM_CLOSE
        }
        sub_19FFA70(&v91);
        sub_19FFA70(&v89);
        sub_19FFA70(&v90);
        sub_19FFA70(&v92);
        v93[0] = (DWORD)g_CDiskFileWin32Vtable;
        sub_18C4840(v93);
        goto epilogue;
    }

signedIn:
    // 10. The signed-in continuation: front-end data + device construction.
    {
        int a = sub_6A66C0();
        int b = sub_6A6960(a);
        sub_772190(b);
        sub_61B090(obj);
        sub_18FBD00(g_byte_1DBD030 != 0);
        nullsub_7();
        sub_770960();

        const char *cmd = GetCommandLineA();
        g_byte_1DBD034 = (strstr(cmd, "WaitBeforeEnteringFrontEnd") != 0);
        base[0x24] = 0;
        sub_620F30(obj);

        // Copy the node-list pointers (heapNode -> +0x1C, listFirst -> +0x20).
        int heapNode  = (int)dw[0];
        int listFirst = (int)dw[0x14 / 4];
        dw[0x1C / 4] = (DWORD)heapNode;
        dw[0x20 / 4] = (DWORD)listFirst;

        void *p56 = LHAlloc(0x38);
        int   v35 = (p56 != 0) ? (int)sub_18EADB0(p56) : 0;
        void *v36 = (void *)dw[0x98 / 4];
        if (v36 != 0)
        {
            sub_18EB1A0(v36);
            LHFree(v36);
        }
        dw[0x98 / 4] = (DWORD)v35;

        sub_719770();
        v164 = sub_1801C20();
        sub_4358A0(&v81[0], "startup.vfsconfig");
        memset(vfs, 0, sizeof(vfs));
        sub_1801580((void *)v164, &v81[0]);
        if (vfs[3] >= 8)
            LHFree((void *)vfs[0]);

        vfs[0] = vfs[1] = vfs[2] = 0;
        vfs[3] = 7;
        void *p48 = LHAlloc(0x30);
        int   v39 = (p48 != 0) ? (int)sub_767360(p48) : 0;

        void *v40 = (void *)dw[0xAC / 4];
        if (v40 != 0)
            ((void (*)(void *, int))F3_VT(v40)[0])(v40, 1);
        dw[0xAC / 4] = (DWORD)v39;

        void *p288 = LHAlloc(0x120);
        int   v37 = (p288 != 0) ? (int)sub_762490(p288, (void *)dw[0xAC / 4]) : 0;

        void *v41 = (void *)dw[0xB0 / 4];
        if (v41 != 0)
        {
            sub_6234A0(v41);
            LHFree(v41);
        }
        dw[0xB0 / 4] = (DWORD)v37;

        sub_7672E0((void *)dw[0xAC / 4]);
        int v42 = sub_760FA0((void *)dw[0xB0 / 4]);
        sub_72EDF0(base + 0xEC, v42);
        sub_61A470(obj, 0);

        void *p736 = LHAlloc(0x2E0);
        int   v44 = (p736 != 0) ? (int)sub_754DE0(p736) : 0;
        void *v45 = (void *)dw[0xA8 / 4];
        if (v45 != 0)
        {
            sub_627690(v45);
            LHFree(v45);
        }
        dw[0xA8 / 4] = (DWORD)v44;

        g_off_1D8C850 = (void *)sub_619990;
        g_off_1D8C854 = (void *)sub_6199A0;
        sub_18FA3F0();

        g_byte_1DBD9E0 = 0;
        sub_18C4560(&g_unk_1DBD9E8, -1, 0);
    }

    if (v72 == 0)
        goto epilogue;

    // 11. The device-created / worker-callback / thread-start block.
    {
        void *mgr = GetManager();
        void **vt = (void **)*(void ***)mgr;
        ((void (*)(void *, int))vt[10])(mgr, (unsigned char)g_byte_1DBD01F);

        HWND hwnd = (HWND)((int (*)(void *))vt[8])(GetManager());
        int a = sub_18FA3E0(hwnd);
        int b = sub_18FA3D0(a);
        sub_145D7B0(b);
        int c = sub_6A66C0();
        int d = sub_6A69A0(c);
        sub_741E50(d);

        sub_18F6C90(GetWorker(), (void *)sub_61A330);
        sub_18F6C80(GetWorker(), (void *)sub_619DE0);
        sub_18F6C70(GetWorker(), (void *)sub_6199C0);
        sub_18F6C60(GetWorker(), (void *)sub_619A70);

        g_byte_2075F07 = 0;
        v140 = 0xF;
        v154 = 0;
        sub_402F00(&modelHdr, "globals_model_headers", 0x15);
        void *v52 = sub_18013B0((void *)v164, &modelHdr, &v168);
        void *v52obj = *(void **)v52;
        if (v52obj != 0)
            ((void (*)(void *))F3_VT(v52obj)[1])(v52obj);
        void *v53 = (void *)dw[0x7C / 4];
        if (v53 != 0)
            ((void (*)(void *))F3_VT(v53)[2])(v53);
        dw[0x7C / 4] = (DWORD)v52obj;
        if (v168 != 0)
            ((void (*)(void *))F3_VT(v168)[2])(v168);
        if (v140 >= 0x10)
            LHFree((void *)v154);

        sub_18E8CD0();
        int e = sub_18E8960(0, 0);
        sub_18E8D50((void *)e, 0);
        sub_18E7180();
        sub_18E84C0();

        void *p88 = LHAlloc(0x58);
        int   v55 = (p88 != 0) ? (int)sub_7330F0(p88) : 0;
        void *v56 = (void *)dw[0x28 / 4];
        if (v56 != 0)
            ((void (*)(void *, int))F3_VT(v56)[0])(v56, 1);
        dw[0x28 / 4] = (DWORD)v55;

        void *p8 = LHAlloc(8);
        int   v58 = (p8 != 0) ? (int)sub_72FB30(p8) : 0;
        void *v59 = (void *)dw[0x9C / 4];
        if (v59 != 0)
            ((void (*)(void *, int))F3_VT(v59)[0])(v59, 1);
        dw[0x9C / 4] = (DWORD)v58;
        sub_72FB10((void *)v58, obj, v108, str103);

        nullsub_28((int)dw[0xBC / 4]);
        sub_70DDB0((int)dw[0xBC / 4]);
        sub_18F0180();
        sub_72FAE0((void *)dw[0x9C / 4]);

        int fileId = sub_1A01CC0(&v160slot, "data\\art\\videos\\microsoft_logo.bik", -1);
        sub_61BFE0(obj, fileId);
        ReleaseSharedSlot(&v160slot);
        sub_61A5E0(obj);

        base[0x84] = 0;
        dw[0x80 / 4] = (DWORD)_beginthread(sub_61C320, 0x40000u, obj);

        void *p8b = LHAlloc(8);
        int   v62 = (p8b != 0) ? (int)sub_731B80(p8b) : 0;
        void *v63 = (void *)dw[0x2C / 4];
        if (v63 != 0)
            ((void (*)(void *, int))F3_VT(v63)[0])(v63, 1);
        dw[0x2C / 4] = (DWORD)v62;

        void *p8c = LHAlloc(8);
        int   v64 = (p8c != 0) ? (int)sub_712430(p8c) : 0;
        volatile LONG **v65 = (volatile LONG **)(void *)dw[0xB4 / 4];
        if (v65 != 0)
        {
            if (v65[1] != 0 && InterlockedDecrement(v65[1]) == 0)
            {
                ((void (*)(int))F3_VT(v65[1])[1])((int)(*(void **)((char *)v65[1] + 8)));
                LHFree((void *)v65[1]);
            }
            *v65   = 0;
            v65[1] = 0;
            LHFree((void *)v65);
        }
        dw[0xB4 / 4] = (DWORD)v64;

        if (g_byte_1DBD03E != 0)
            nullsub_29();
        sub_18FB930();
    }

    // Success: the frontend ran and the device / threads were started.
    result = 1;

epilogue:
    // Shared cleanup epilogue (identical on the success and cancel / failure
    // paths; only the return value differs).
    v93[0] = (DWORD)g_CDiskFileWin32Vtable;
    sub_18C4840(v93);
    ReleaseSharedSlot(&v96);
    v93[0] = (DWORD)g_CAFileVtable;
    ReleaseSharedSlot(&v88);
    sub_40E1F0(v108);
    return result;
}
