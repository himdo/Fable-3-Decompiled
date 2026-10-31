// Fable 3 — framework module (refcounted init / shutdown).
//
// Declares the framework init/shutdown pair that WinMain drives. The
// implementation lives in framework.cpp. Each function keeps its IDA address
// range so it maps back to the stripped Fable3.exe.
#ifndef F3_FRAMEWORK_H
#define F3_FRAMEWORK_H

// 0x1A02B50 - refcounted framework init (first call computes the exe dir).
int  FrameworkInit(void);

// 0x1A02E70 - refcounted framework shutdown (last call closes the Stream and
// releases the framework's global shared pointer).
int  FrameworkShutdown(void);

#endif  // F3_FRAMEWORK_H
