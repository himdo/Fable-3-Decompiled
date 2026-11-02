// Fable 3 — XLO (Xbox Live On) device callback implementation. See
// xlo_device.h for the module overview.

#include "xlo_device.h"
#include <windows.h>
#include <string.h>

// 0x1E2EEF4: the current XLO device object (a vtable-backed pointer; the
// on-create-device callback passes its value straight to XLiveOnCreateDevice).
static DWORD g_dword_1E2EEF4 = 0;

// 0x1E2ED80: the 56-byte (14 DWORD) XLO device-caps block. The callbacks copy
// this into a local buffer before handing it to the XDK.
static DWORD g_dword_1E2ED80[14];

typedef struct F3XloDeviceCaps { DWORD field[14]; } F3XloDeviceCaps;
typedef char F3XloDeviceCapsSizeCheck[(sizeof(F3XloDeviceCaps) == 56) ? 1 : -1];

// XLiveOnCreateDevice -- XDK (Xbox 360) import (IAT thunk 0x17D1A54 ->
// __imp_XLiveOnCreateDevice). Not present in a PC build, so stub it. Returns
// the created-device result (DWORD). First arg is the device object value
// (dword_1E2EEF4); second is the 56-byte caps block.
static DWORD XLiveOnCreateDevice(DWORD device, F3XloDeviceCaps *caps)
{
    (void)device; (void)caps;
    return 0;
}

// XLiveOnResetDevice -- XDK (Xbox 360) import (IAT thunk 0x17D1A5A ->
// __imp_XLiveOnResetDevice). Not present in a PC build, so stub it.
// Takes the 56-byte device-caps block.
static DWORD XLiveOnResetDevice(F3XloDeviceCaps *caps)
{
    (void)caps;
    return 0;
}

// ============================================================
//  OnCreateDevice  (0x006152F0 - 0x00615336)  -- the worker's
//  on-create-device callback
//
//  [NOT byte-identical] placement-dependent (relative calls to the
//  sub_18F01A0 caps getter, the XLiveOnCreateDevice IAT thunk, and
//  __security_check_cookie) and reads the original security-cookie + data
//  globals at their high VAs.
//
//  int OnCreateDevice(void)
//
//  Invoked by the XLO layer when a device is created. It:
//    1. Reads the current device object (dword_1E2EEF4).
//    2. Copies the 56-byte device-caps block (dword_1E2ED80, via the
//       sub_18F01A0 getter) onto the stack.
//    3. Returns XLiveOnCreateDevice(device, caps).
// ============================================================
int OnCreateDevice(void)
{
    F3XloDeviceCaps caps;
    memcpy(&caps, g_dword_1E2ED80, sizeof(caps));   // == sub_18F01A0()
    return (int)XLiveOnCreateDevice(g_dword_1E2EEF4, &caps);
}

// ============================================================
//  OnResetDevice  (0x00615340 - 0x0061537D)  -- the worker's
//  on-reset-device callback
//
//  [NOT byte-identical] placement-dependent (relative calls to the
//  sub_18F01A0 caps getter, the XLiveOnResetDevice IAT thunk, and
//  __security_check_cookie) and reads the original security-cookie at
//  its high VA.
//
//  int OnResetDevice(void)
//
//  Invoked by the XLO layer when a device is reset. It copies the
//  56-byte device-caps block (dword_1E2ED80, via the sub_18F01A0
//  getter) onto the stack and returns XLiveOnResetDevice(caps).
// ============================================================
int OnResetDevice(void)
{
    F3XloDeviceCaps caps;
    memcpy(&caps, g_dword_1E2ED80, sizeof(caps));   // == sub_18F01A0()
    return (int)XLiveOnResetDevice(&caps);
}
