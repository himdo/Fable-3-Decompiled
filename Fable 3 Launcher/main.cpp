// Fable 3 Launcher (FableLauncher.exe) — byte-identical decompilation.
//
// This file mirrors the application code in .text of the original binary.
// Every function below is a placeholder stub named after its IDA symbol,
// placed in the same order (by address) as in the original image.
//
// Original image layout (x86, image base 0x00400000):
//   0x00401000 - 0x00404099  application code  (this file)
//   0x00404099 - 0x00411000  static MSVC CRT / EH (supplied by the linker via /MT)
//
// Each stub is annotated with its original address range so that the
// decompiled bodies can be slotted in function by function.
#include <windows.h>
#include <sstream>
#include <string>

// Forward declaration: WinMain calls this helper before its definition.
void sub_403DB0(std::wostream& os);

// ---------------------------------------------------------------------------
// 0x00401000 - 0x00404099 : application functions (IDA name  ->  stub)
// ---------------------------------------------------------------------------

// sub_401000  (0x00401000 - 0x0040100B)
void sub_401000(void)
{
}

// sub_401010  (0x00401010 - 0x00401034)
void sub_401010(void)
{
}

// sub_401040  (0x00401040 - 0x004010AA)
void sub_401040(void)
{
}

// unknown_libname_1  (0x004010B0 - 0x004010BE)
void unknown_libname_1(void)
{
}

// sub_4010C0  (0x004010C0 - 0x00401146)
void sub_4010C0(void)
{
}

// sub_401150  (0x00401150 - 0x00401222)
void sub_401150(void)
{
}

// sub_401230  (0x00401230 - 0x004012A1)
void sub_401230(void)
{
}

// sub_4012B0  (0x004012B0 - 0x004012D7)
void sub_4012B0(void)
{
}

// sub_4012E0  (0x004012E0 - 0x0040131A)
void sub_4012E0(void)
{
}

// sub_401320  (0x00401320 - 0x00401327)
void sub_401320(void)
{
}

// sub_401380  (0x00401380 - 0x004013A8)
void sub_401380(void)
{
}

// sub_4013B0  (0x004013B0 - 0x004013CF)
void sub_4013B0(void)
{
}

// sub_4013D0  (0x004013D0 - 0x00401415)
void sub_4013D0(void)
{
}

// sub_401420  (0x00401420 - 0x0040151D)
void sub_401420(void)
{
}

// sub_401520  (0x00401520 - 0x00401541)
void sub_401520(void)
{
}

// sub_401550  (0x00401550 - 0x0040156E)
void sub_401550(void)
{
}

// sub_401570  (0x00401570 - 0x004015A8)
void sub_401570(void)
{
}

// sub_4015B0  (0x004015B0 - 0x004015E8)
void sub_4015B0(void)
{
}

// sub_4015F0  (0x004015F0 - 0x00401604)
void sub_4015F0(void)
{
}

// sub_401610  (0x00401610 - 0x00401641)
void sub_401610(void)
{
}

// sub_401650  (0x00401650 - 0x00401664)
void sub_401650(void)
{
}

// sub_401670  (0x00401670 - 0x004016A1)
void sub_401670(void)
{
}

// sub_4016B0  (0x004016B0 - 0x004016F3)
void sub_4016B0(void)
{
}

// sub_401700  (0x00401700 - 0x00401722)
void sub_401700(void)
{
}

// sub_401730  (0x00401730 - 0x004017A2)
void sub_401730(void)
{
}

// sub_4017B0  (0x004017B0 - 0x004017FF)
void sub_4017B0(void)
{
}

// sub_401800  (0x00401800 - 0x00401829)
void sub_401800(void)
{
}

// sub_401830  (0x00401830 - 0x004018B5)
void sub_401830(void)
{
}

// sub_4018C0  (0x004018C0 - 0x004018F7)
void sub_4018C0(void)
{
}

// sub_401900  (0x00401900 - 0x00401932)
void sub_401900(void)
{
}

// sub_401940  (0x00401940 - 0x00401A5E)
void sub_401940(void)
{
}

// sub_401A60  (0x00401A60 - 0x00401A8E)
void sub_401A60(void)
{
}

// sub_401A90  (0x00401A90 - 0x00401B0C)
void sub_401A90(void)
{
}

// sub_401B10  (0x00401B10 - 0x00401B57)
void sub_401B10(void)
{
}

// sub_401B60  (0x00401B60 - 0x00401B79)
void sub_401B60(void)
{
}

// sub_401B80  (0x00401B80 - 0x00401BA8)
void sub_401B80(void)
{
}

// _WinMain@16  (0x00401BB0 - 0x00401D71)
//
// Builds the command line into a wide stringstream, then (if it is empty)
// lets sub_403DB0 massage it. The resulting string is launched with
// CreateProcessW in a retry loop: a child exit code of 291 (0x123) causes
// an immediate re-launch (the SecuROM tamper-detection re-spawn).
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;

    std::wstringstream ss;
    if (lpCmdLine != NULL)
        ss << lpCmdLine;

    if (ss.str() == L"")
        sub_403DB0(ss);

    std::wstring app = ss.str();

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    BOOL retry;
    do
    {
        retry = FALSE;
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        if (!CreateProcessW(app.c_str(), NULL, NULL, NULL, TRUE, 0,
                            NULL, NULL, &si, &pi))
            break;
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode == 291)
            retry = TRUE;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } while (retry);

    return 0;
}

// sub_401D80  (0x00401D80 - 0x00401DB9)
void sub_401D80(void)
{
}

// sub_401DC0  (0x00401DC0 - 0x00401DE5)
void sub_401DC0(void)
{
}

// sub_401E20  (0x00401E20 - 0x00401E76)
void sub_401E20(void)
{
}

// sub_401E90  (0x00401E90 - 0x00401E9F)
void sub_401E90(void)
{
}

// sub_401EA0  (0x00401EA0 - 0x00401F10)
void sub_401EA0(void)
{
}

// sub_401F10  (0x00401F10 - 0x00401F18)
void sub_401F10(void)
{
}

// sub_401F20  (0x00401F20 - 0x00401F26)
void sub_401F20(void)
{
}

// sub_401F30  (0x00401F30 - 0x00401F5C)
void sub_401F30(void)
{
}

// sub_401F60  (0x00401F60 - 0x00401F76)
void sub_401F60(void)
{
}

// sub_401F80  (0x00401F80 - 0x0040202A)
void sub_401F80(void)
{
}

// sub_402030  (0x00402030 - 0x004020D3)
void sub_402030(void)
{
}

// unknown_libname_2  (0x004020E0 - 0x004020FA)
void unknown_libname_2(void)
{
}

// unknown_libname_3  (0x00402100 - 0x0040211A)
void unknown_libname_3(void)
{
}

// sub_402120  (0x00402120 - 0x00402125)
void sub_402120(void)
{
}

// sub_402130  (0x00402130 - 0x00402133)
void sub_402130(void)
{
}

// nullsub_2  (0x00402140 - 0x00402143)
void nullsub_2(void)
{
}

// sub_402150  (0x00402150 - 0x004021E7)
void sub_402150(void)
{
}

// sub_4021F0  (0x004021F0 - 0x00402271)
void sub_4021F0(void)
{
}

// sub_402280  (0x00402280 - 0x0040229A)
void sub_402280(void)
{
}

// sub_4022D0  (0x004022D0 - 0x00402336)
void sub_4022D0(void)
{
}

// sub_402340  (0x00402340 - 0x00402543)
void sub_402340(void)
{
}

// sub_402550  (0x00402550 - 0x004025AD)
void sub_402550(void)
{
}

// sub_4025B0  (0x004025B0 - 0x00402618)
void sub_4025B0(void)
{
}

// sub_402620  (0x00402620 - 0x0040278B)
void sub_402620(void)
{
}

// sub_402790  (0x00402790 - 0x0040286A)
void sub_402790(void)
{
}

// sub_4028A0  (0x004028A0 - 0x004028D9)
void sub_4028A0(void)
{
}

// sub_4028E0  (0x004028E0 - 0x00402942)
void sub_4028E0(void)
{
}

// sub_402950  (0x00402950 - 0x00402989)
void sub_402950(void)
{
}

// sub_402990  (0x00402990 - 0x004029AE)
void sub_402990(void)
{
}

// sub_4029B0  (0x004029B0 - 0x004029E5)
void sub_4029B0(void)
{
}

// sub_4029F0  (0x004029F0 - 0x00402A45)
void sub_4029F0(void)
{
}

// sub_402A50  (0x00402A50 - 0x00402A6E)
void sub_402A50(void)
{
}

// sub_402A70  (0x00402A70 - 0x00402B4A)
void sub_402A70(void)
{
}

// sub_402B50  (0x00402B50 - 0x00402B75)
void sub_402B50(void)
{
}

// sub_402B80  (0x00402B80 - 0x00402BCA)
void sub_402B80(void)
{
}

// sub_402BD0  (0x00402BD0 - 0x00402BE1)
void sub_402BD0(void)
{
}

// sub_402BF0  (0x00402BF0 - 0x00402C01)
void sub_402BF0(void)
{
}

// sub_402C10  (0x00402C10 - 0x00402C7A)
void sub_402C10(void)
{
}

// sub_402C80  (0x00402C80 - 0x00402E02)
void sub_402C80(void)
{
}

// sub_402E10  (0x00402E10 - 0x00402E61)
void sub_402E10(void)
{
}

// sub_402E70  (0x00402E70 - 0x00402EF6)
void sub_402E70(void)
{
}

// sub_402F00  (0x00402F00 - 0x00402F70)
void sub_402F00(void)
{
}

// sub_402F70  (0x00402F70 - 0x00403046)
void sub_402F70(void)
{
}

// sub_403050  (0x00403050 - 0x004030D3)
void sub_403050(void)
{
}

// sub_4030E0  (0x004030E0 - 0x0040313B)
void sub_4030E0(void)
{
}

// sub_403140  (0x00403140 - 0x004031D7)
void sub_403140(void)
{
}

// sub_4031E0  (0x004031E0 - 0x004032D1)
void sub_4031E0(void)
{
}

// sub_403300  (0x00403300 - 0x0040346E)
void sub_403300(void)
{
}

// sub_403470  (0x00403470 - 0x00403516)
void sub_403470(void)
{
}

// sub_403520  (0x00403520 - 0x004035C1)
void sub_403520(void)
{
}

// sub_4035D0  (0x004035D0 - 0x00403625)
void sub_4035D0(void)
{
}

// sub_403630  (0x00403630 - 0x004036DF)
void sub_403630(void)
{
}

// sub_4036E0  (0x004036E0 - 0x0040376F)
void sub_4036E0(void)
{
}

// sub_403770  (0x00403770 - 0x004037CC)
void sub_403770(void)
{
}

// sub_4037D0  (0x004037D0 - 0x00403946)
void sub_4037D0(void)
{
}

// sub_403950  (0x00403950 - 0x00403A91)
void sub_403950(void)
{
}

// sub_403AA0  (0x00403AA0 - 0x00403DAC)
void sub_403AA0(void)
{
}

// sub_403DB0  (0x00403DB0 - 0x00404010)
void sub_403DB0(std::wostream& os)
{
    (void)os;
}

// sub_404010  (0x00404010 - 0x00404029)
void sub_404010(void)
{
}

// sub_404030  (0x00404030 - 0x00404099)
void sub_404030(void)
{
}
