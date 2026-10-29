// Fable 3 Launcher - GUI entry point (WinMain, matches original launcher's
// Windows GUI subsystem / WinMainCRTStartup entry point).
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return 0;
}
