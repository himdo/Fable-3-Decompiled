// Fable 3 — engine Lua / KoreVM module declarations (the "Fable 3" Lua
// environment: the 1032-byte KoreVM host module, the CLuaManager /
// CEntityScriptHandleManager wrappers, and ReinitEngineLua). Implemented in
// engine/luavm.cpp (split out of gamemain.cpp, the frontend run module).

#ifndef F3_LUAVM_H
#define F3_LUAVM_H

// void DestroyLuaModule(int luaModule)   (sub_6281B0, 0x6281B0 - 0x6281B5)
// -- release the engine's "Fable 3" Lua module (the 1032-byte KoreVM host)
// by forwarding to DestroyLuaModuleBody (sub_5EDFE0).
void DestroyLuaModule(int luaModule);

// int ReinitEngineLua(void *obj, int flag)   (0x61A470 - 0x61A5D1)
// -- (re)build the engine's Lua environment (the +0xBC Lua module, the +0xC0
// CLuaManager, the +0xC4 CEntityScriptHandleManager).  obj = the CGameEngine.
int ReinitEngineLua(void *obj, int flag);

#endif // F3_LUAVM_H
