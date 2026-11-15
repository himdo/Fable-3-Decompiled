#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "engine/engine.h"

static LONG WINAPI _CrashHandler(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    BYTE *addr = (BYTE *)ep->ExceptionRecord->ExceptionAddress;
    HMODULE h = 0;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       (const char *)addr, &h);
    printf("!! EXCEPTION 0x%lx at %p (base %p, rva %p)\n",
           (unsigned long)code, (void *)addr, (void *)h,
           (void *)((BYTE *)addr - (BYTE *)h));
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   // unbuffered (see output on crash)
    AddVectoredExceptionHandler(1, _CrashHandler);

    // Compute the shared-memory mapping name the same way GameThread does.
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    const WCHAR *p = wcsrchr(path, 0x5C);   // backslash
    if (!p) p = wcsrchr(path, 0x2F);         // slash
    if (p) p++;
    WCHAR name[1024];
    swprintf(name, 1024, L"-=[SMS_%s_SMS]=-", p);
    printf("TEST: path=%ls name=%ls\n", path, name);

    // Create the mapping so GameThread's load succeeds.
    HANDLE hMap = CreateFileMappingW(NULL, NULL, PAGE_READWRITE, 0, 0x6C4, name);
    printf("created mapping %ls = %p (err=%lu)\n", name, (void*)hMap, (unsigned long)GetLastError());

    printf("before GameThread: GetWorker() = %p\n", GetWorker());
    GameThread((void*)42);
    printf("after GameThread: GetWorker() = %p\n", GetWorker());
    printf("GAME_THREAD_OK\n");
    if (hMap) CloseHandle(hMap);
    return 0;
}
