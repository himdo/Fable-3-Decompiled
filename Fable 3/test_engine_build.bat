@echo off
setlocal EnableExtensions
set "V90=D:\Program Files (x86)\Microsoft Visual Studio 9.0"
set "SDK=C:\Program Files\Microsoft SDKs\Windows\v7.1"
set "INCLUDE=%V90%\VC\INCLUDE;%SDK%\Include"
set "LIB=%V90%\VC\LIB;%SDK%\Lib"
set "PATH=%V90%\VC\bin;%PATH%"
if not exist test_out mkdir test_out
pushd test_out
cl /nologo /c /I .. /O2 /Ob2 /DNDEBUG /DWIN32 /D_WINDOWS /W3 /EHsc /GS /MT /Z7 ..\test_engine.cpp ..\engine\engine.cpp ..\engine\worker.cpp ..\engine\manager.cpp ..\engine\object_registry.cpp ..\engine\xlo_device.cpp ..\engine\gameengine.cpp ..\engine\gamethread.cpp ..\engine\gamemain.cpp ..\engine\thread.cpp ..\common\common.cpp ..\save\save.cpp ..\framework\framework.cpp
if errorlevel 1 (echo COMPILE FAILED & popd & exit /b 1)
link /nologo /subsystem:console /machine:x86 /out:test_engine.exe test_engine.obj engine.obj worker.obj manager.obj object_registry.obj xlo_device.obj gameengine.obj gamethread.obj gamemain.obj thread.obj common.obj save.obj framework.obj user32.lib kernel32.lib
if errorlevel 1 (echo LINK FAILED & popd & exit /b 1)
echo === BUILD OK ===
test_engine.exe
echo exit=%ERRORLEVEL%
popd
