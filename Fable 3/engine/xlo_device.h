// Fable 3 — XLO (Xbox Live On) device callbacks.
//
// The two no-arg callbacks RunGame registers on the current worker
// (CWorkerWin32): OnCreateDevice (field +0x258, via SetOnCreateDevice) and
// OnResetDevice (field +0x26C, via SetOnResetDevice). The XLO layer invokes
// them when a device is created / reset; each forwards to the corresponding
// XDK XLiveOn* import, passing the current device object (dword_1E2EEF4) and
// a copy of the game's 56-byte device-caps block (dword_1E2ED80, reached in
// the original through the sub_18F01A0 getter).
#ifndef F3_XLO_DEVICE_H
#define F3_XLO_DEVICE_H

// 0x006152F0 - the worker's on-create-device callback (XLO layer).
int OnCreateDevice(void);

// 0x00615340 - the worker's on-reset-device callback (XLO layer).
int OnResetDevice(void);

#endif  // F3_XLO_DEVICE_H
