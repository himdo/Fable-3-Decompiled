@echo off
setlocal EnableExtensions
set "V90=D:\Program Files (x86)\Microsoft Visual Studio 9.0"
set "SDK=C:\Program Files\Microsoft SDKs\Windows\v7.1"
set "INCLUDE=%V90%\VC\INCLUDE;%SDK%\Include"
set "LIB=%V90%\VC\LIB;%SDK%\Lib"
set "PATH=%V90%\VC\bin;%PATH%"
if not exist test_out mkdir test_out
pushd test_out
cl /nologo /c /I .. /O2 /Ob2 /DNDEBUG /DWIN32 /D_WINDOWS /W3 /EHsc /GS /MT /Z7 ..\test_engine.cpp ..\engine\engine.cpp ..\engine\worker.cpp ..\engine\manager.cpp ..\engine\object_registry.cpp ..\engine\xlo_device.cpp ..\engine\gameengine.cpp ..\engine\gamethread.cpp ..\engine\gamemain.cpp ..\engine\netinit.cpp ..\engine\thread.cpp ..\common\common.cpp ..\save\save.cpp ..\framework\framework.cpp
if errorlevel 1 (echo COMPILE FAILED & popd & exit /b 1)
rem Build the xlive.dll import library from the module-definition file.
lib /nologo /machine:x86 /def:..\engine\xlive.def /out:xlive.lib
if errorlevel 1 (echo LIB FAILED & popd & exit /b 1)
rem The ClearCsurface path imports D3DXFloat32To16Array: build the d3dx9_42
rem import lib too (the DLL itself ships in System32/SysWOW64).
lib /nologo /machine:x86 /def:..\engine\d3dx9_42.def /out:d3dx9_42.lib
if errorlevel 1 (echo LIB FAILED & popd & exit /b 1)
rem CreateD3D9DisplayObject imports Direct3DCreate9: build the d3d9 import lib
rem too (the DLL ships in System32/SysWOW64).
lib /nologo /machine:x86 /def:..\engine\d3d9.def /out:d3d9.lib
if errorlevel 1 (echo LIB FAILED & popd & exit /b 1)
link /nologo /subsystem:console /machine:x86 /out:test_engine.exe test_engine.obj engine.obj worker.obj manager.obj object_registry.obj xlo_device.obj gameengine.obj gamethread.obj gamemain.obj netinit.obj thread.obj common.obj save.obj framework.obj xlive.lib d3dx9_42.lib d3d9.lib user32.lib kernel32.lib gdi32.lib ws2_32.lib
if errorlevel 1 (echo LINK FAILED & popd & exit /b 1)
rem The exe now really imports from the bundled XLiveAPI shim: copy
rem xlive.dll (game dir = three levels up) next to it so the loader binds.
if exist ..\..\..\xlive.dll copy /y ..\..\..\xlive.dll . >nul
echo === BUILD OK ===
test_engine.exe
echo exit=%ERRORLEVEL%
popd
