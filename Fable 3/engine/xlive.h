// Fable 3 — xlive.dll (XLiveAPI) import declarations.
//
// In the original Fable3.exe each of these is a link-time import from
// xlive.dll (import-directory name "xlive.dll"), resolved BY ORDINAL. The
// build reproduces the same import table: the build scripts generate an
// import library from engine/xlive.def (`lib /def` -> xlive.lib) and link it,
// so every call site binds to an xlive.dll IAT slot imported by ordinal,
// exactly as the original does.
//
// Calling conventions were taken from the bundled xlive.dll epilogues
// (verified in IDA): 13 of the 14 are stdcall (WINAPI) and XOnlineStartup is
// the odd one — a bare `retn` with zero arguments (the original calls
// `call XOnlineStartup` with no push / no caller cleanup at 0x181F50b).
// The stdcall @N on each declaration matches the retn N in xlive.dll, so the
// caller does not clean the stack and the callee (the real xlive.dll) does.
#ifndef F3_XLIVE_H
#define F3_XLIVE_H

#include <windows.h>

// The 56-byte XLO device-caps block (defined in engine/xlo_device.cpp).
struct F3XloDeviceCaps;

#ifdef __cplusplus
extern "C" {
#endif

// ---- xlive.dll imports driven by RunFrontEnd (0x61CB70) ----
// XNotifyCreateListener  ordinal 5270  stdcall @8
int   __stdcall XNotifyCreateListener(int dwFilter, int bAsync);
// XNotifyGetNext         ordinal 651   stdcall @16
int   __stdcall XNotifyGetNext(int hListener, int dwFlags,
                     void *pExtendedData, void *pNotificationData);
// XUserGetSigninState    ordinal 5262  stdcall @4
int   __stdcall XUserGetSigninState(int dwUser);
// XShowSigninUI          ordinal 5260  stdcall @8
int   __stdcall XShowSigninUI(int dwUser, int dwMessage);
// XShowGuideUI           ordinal 5215  stdcall @4
int   __stdcall XShowGuideUI(int dwMessage);
// XShowMessageBoxUI      ordinal 5266  stdcall @36
int   __stdcall XShowMessageBoxUI(int dwUser, int pszTitle, int pszText,
                        int dwTextLines, void *rgwchButtons,
                        int dwDefaultButton, int dwButtonCount,
                        void *pdwButtonResult, void *pOverlapped);
// XCancelOverlapped      ordinal 5254  stdcall @4
int   __stdcall XCancelOverlapped(void *pOverlapped);

// ---- xlive.dll imports driven by InitNetworking (0x6A7210) et al. ----
// XLiveInitializeEx      ordinal 5297  stdcall @8
DWORD __stdcall XLiveInitializeEx(const void *pConfiguration, DWORD dwVersion);
// XNetStartup            ordinal 51    stdcall @4
DWORD __stdcall XNetStartup(const void *pConfig);
// XOnlineStartup         ordinal 5310  bare `retn`, ZERO args (cdecl)
DWORD XOnlineStartup(void);
// XPresenceInitialize    ordinal 5313  stdcall @4
DWORD __stdcall XPresenceInitialize(int dwTimeoutMs);
// XLivePreTranslateMessage ordinal 5030 stdcall @4
// Installed as the worker's stdcall F3PreTranslateFn pre-translate hook slot
// (PumpWorkerMessages, 0x18F8530); returning FALSE means "not handled".
BOOL  __stdcall XLivePreTranslateMessage(LPMSG pMsg);

// ---- XLO device callbacks (engine/xlo_device.cpp) ----
// XLiveOnCreateDevice    ordinal 5005  stdcall @8
DWORD __stdcall XLiveOnCreateDevice(DWORD dwDevice, struct F3XloDeviceCaps *pCaps);
// XLiveOnResetDevice     ordinal 5007  stdcall @4
DWORD __stdcall XLiveOnResetDevice(struct F3XloDeviceCaps *pCaps);

#ifdef __cplusplus
}
#endif

#endif  // F3_XLIVE_H
