#include <windows.h>
#include <stdio.h>
#include <wchar.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    WCHAR name[1024];
    swprintf(name, 1024, L"-=[SMS_test_engine.exe_SMS]=-");
    printf("name = %ls\n", name);

    HANDLE hCreate = CreateFileMappingW(NULL, NULL, PAGE_READWRITE, 0, 0x6C4, name);
    printf("CreateFileMappingW = %p (err=%lu)\n", (void*)hCreate, (unsigned long)GetLastError());

    HANDLE hOpen = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    printf("OpenFileMappingW   = %p (err=%lu)\n", (void*)hOpen, (unsigned long)GetLastError());

    // Also try with a simpler name (no brackets) to see if brackets are the issue.
    HANDLE h2 = CreateFileMappingW(NULL, NULL, PAGE_READWRITE, 0, 0x6C4, L"SMSTEST");
    HANDLE h3 = OpenFileMappingW(FILE_MAP_READ, FALSE, L"SMSTEST");
    printf("simple Create = %p, Open = %p (err=%lu)\n", (void*)h2, (void*)h3, (unsigned long)GetLastError());

    if (hCreate) CloseHandle(hCreate);
    if (hOpen) CloseHandle(hOpen);
    if (h2) CloseHandle(h2);
    if (h3) CloseHandle(h3);
    return 0;
}
