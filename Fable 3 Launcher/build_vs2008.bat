@echo off
rem ============================================================
rem  Fable 3 Launcher - build with the genuine VS2008 SP1 x86
rem  toolchain (cl 15.00.30729.01 / link 9.00.30729.01) + Win SDK 7.1
rem  to match the original FableLauncher.exe.
rem
rem  Resources (icons, version info, manifest) are embedded to match
rem  the original's .rsrc byte-for-byte. The version info is patched
rem  post-link because rc.exe cannot emit the original dwFileFlagsMask
rem  and file-date fields.
rem ============================================================
setlocal EnableExtensions

rem ---- toolchain locations ----
set "V90=D:\Program Files (x86)\Microsoft Visual Studio 9.0"
set "V90BIN=%V90%\VC\bin"
set "SDK=C:\Program Files\Microsoft SDKs\Windows\v7.1"
set "RC=%SDK%\Bin\RC.Exe"
set "ORIG=D:\projects\hacking\Windows\Fable 3\FableLauncher.exe"

rem ---- configuration ----
set "CFG=Release"
set "ARCH=Win32"

rem ---- VS2008 environment (x86 host/compiler + SDK 7.1) ----
set "INCLUDE=%V90%\VC\INCLUDE;%SDK%\Include"
set "LIB=%V90%\VC\LIB;%SDK%\Lib"
set "PATH=%V90BIN%;%PATH%"

rem ---- compiler flags: Release x86, static CRT, LTCG, C++ ----
set "CLFLAGS=/nologo /c /O2 /Ob2 /Oi /DNDEBUG /DWIN32 /D_WINDOWS /W3 /EHsc /GS /MT /GL /Z7 /Fds"
rem ---- linker flags: LTCG, GUI, x86, base 0x400000 ----
set "LNKFLAGS=/nologo /LTCG /subsystem:windows /machine:x86 /base:0x400000 /DYNAMICBASE /NXCOMPAT"

set "OUTREL=%CFG%\%ARCH%"
set "RES=..\\..\\res"
pushd "%~dp0" || exit /b 1
if not exist "%OUTREL%" mkdir "%OUTREL%"

rem ---- extract copyrighted icon assets from the original (not in the repo) ----
echo.
echo === extract icons from original ===
python "%~dp0extract_icons.py" "%ORIG%" "%~dp0res"
if errorlevel 1 (echo EXTRACT ICONS FAILED & popd & exit /b 1)

rem ---- compile resources (working dir = res\ so the .rc finds its files) ----
echo.
echo === rc: icons + version (lang 0x0809) ===
pushd "res" || exit /b 1
"%RC%" /l 0x0809 /nologo /fo icons.res icons.rc
if errorlevel 1 (echo RC ICONS FAILED & popd & exit /b 1)
popd

echo.
echo === rc: manifest (lang 0x0409) ===
pushd "res" || exit /b 1
"%RC%" /l 0x0409 /nologo /fo man.res man.rc
if errorlevel 1 (echo RC MANIFEST FAILED & popd & exit /b 1)
popd

pushd "%OUTREL%" || exit /b 1

echo.
echo === cl (x86): main.cpp ===
cl %CLFLAGS% ..\..\main.cpp
if errorlevel 1 (echo COMPILE FAILED & popd & popd & exit /b 1)

echo.
echo === link (x86) ===
link %LNKFLAGS% /out:FableLauncher.exe /pdb:FableLauncher.pdb main.obj %RES%\icons.res %RES%\man.res
if errorlevel 1 (echo LINK FAILED & popd & popd & exit /b 1)

echo.
echo === patch app-region bytes (0x401000-0x404120) to match original exactly ===
rem copy /Y FableLauncher.exe FableLauncher_before.exe > NUL  (disabled)
rem python ..\..\patch_f3blob.py FableLauncher.exe FableLauncher_before.exe  (patching disabled)
rem if errorlevel 1 (echo BLOB PATCH FAILED & popd & popd & exit /b 1)  (disabled)

echo.
echo === patch .rsrc section to match original (byte-level) ===
python ..\..\patch_rsrc.py FableLauncher.exe "%ORIG%"
if errorlevel 1 (echo PATCH FAILED & popd & popd & exit /b 1)

popd
popd
echo.
echo === BUILD OK: %~dp0%OUTREL%\FableLauncher.exe ===
exit /b 0
