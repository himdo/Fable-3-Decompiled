// Fable 3 — CManagerWin32 module (the 48-byte manager object + registry).
//
// The current manager (NSystem::CManagerWin32) is published as a global
// object pair (dword_20FC8C4/20FC8C8) and disposed through the
// byte-identical DisposeManager SEH handler, which calls the object's
// deleting destructor (vtable[0]).
#ifndef F3_MANAGER_H
#define F3_MANAGER_H

#include "object_registry.h"

// 0x18F8750 - return the current manager (dword_20FC8C4). Used by the engine
// to reach the current CManagerWin32.
void *GetManager(void);

// 0x18F87E0 - install the current manager + SEH cleanup block.
long SetManager(F3ObjReg *reg);

// 0x00615850 - the manager's SEH dispose handler [BYTE-IDENTICAL]. Invoked
// by the SEH block when its refcount reaches zero; calls vtable[0](obj, 1).
int DisposeManager(int arg);

// sub_18F7160 (0x18F7160) - NSystem::CManagerWin32 constructor (48-byte
// object): vtable, critical section at +8, zeroed members at +32..+44.
void *sub_18F7160(void *obj);

#endif  // F3_MANAGER_H
