// Fable 3 — frontend run module (RunFrontEnd, 0x61CB70).
//
// RunFrontEnd is the big game start-up / frontend driver. GameThread
// constructs the 356-byte CGameEngine in place (CGameEngineCtor) and then
// calls RunFrontEnd(engine, arg). It:
//   - sets up the XNA notification listener, probes Steam ("Data/ThisIsSteam"),
//   - builds the CDiskFileWin32 / CAFile file objects and the locale message
//     string,
//   - dispatches the manager's "run the game" vtable slot with the parsed
//     command-line / locale arguments,
//   - drives the XNA sign-in / guide / message-box UI loop (skipped when the
//     user is already signed in),
//   - on success creates the render device + engine sub-objects, installs the
//     worker callbacks, loads the intro video, and starts the 3D thread.
//
// It is thiscall + `retn 4` in the original (obj in ECX, one int on the stack,
// result byte in AL). Declared here as a plain free function taking obj as its
// first parameter to match the project convention for non-byte-identical
// thiscall methods.
#ifndef F3_GAMEMAIN_H
#define F3_GAMEMAIN_H

// char RunFrontEnd(void *obj, int arg)   (obj = the CGameEngine, 356 bytes)
char RunFrontEnd(void *obj, int arg);

#endif  // F3_GAMEMAIN_H
