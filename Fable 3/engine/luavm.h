// Fable 3 — engine Lua / KoreVM module declarations (the "Fable 3" Lua
// environment: the 1032-byte KoreVM host module, the CLuaManager /
// CEntityScriptHandleManager wrappers, and ReinitEngineLua). Implemented in
// engine/luavm.cpp (split out of gamemain.cpp, the frontend run module).

#ifndef F3_LUAVM_H
#define F3_LUAVM_H

// int KoreGetVMCore(void)   (sub_673C50, 0x673C50 - 0x673C56)
// -- the 6-byte getter of the Kore VM core object (g_dword_1DBDD4C;
// 0 = no live VM).  5963 callers in the original.
int KoreGetVMCore(void);

// void DestroyLuaModule(int luaModule)   (sub_6281B0, 0x6281B0 - 0x6281B5)
// -- release the engine's "Fable 3" Lua module (the 1032-byte KoreVM host)
// by forwarding to DestroyLuaModuleBody (sub_5EDFE0).
void DestroyLuaModule(int luaModule);

// int ReinitEngineLua(void *obj, int flag)   (0x61A470 - 0x61A5D1)
// -- (re)build the engine's Lua environment (the +0xBC Lua module, the +0xC0
// CLuaManager, the +0xC4 CEntityScriptHandleManager).  obj = the CGameEngine.
int ReinitEngineLua(void *obj, int flag);

// void ResetEngineTest(int luaModule)   (nullsub_28, 0x70DBB0)
// -- the one-byte `retn` no-op paired with RegisterEngineTest (the argument
// is unused; ReinitEngineLua pushes the Lua module, RunFrontEnd does not).
void ResetEngineTest(int luaModule);

// void RegisterEngineTest(int luaModule)   (sub_70DDB0, 0x70DDB0 - 0x70DEBA)
// -- register the "Debug.EngineTest" Lua module: ensure the "Debug" table
// (sub_633E70) + the "EngineTest" subtable (sub_67C2B0) and register the
// five methods (AddMistInstance, UpdateMaterialParameterFloat,
// UpdateMaterialParameterVec4, GetSelectedPrimitiveResourceID,
// SetSelectedPrimitive).  Called from ReinitEngineLua (flag set) and
// RunFrontEnd, always right after ResetEngineTest.
void RegisterEngineTest(int luaModule);

#endif // F3_LUAVM_H
