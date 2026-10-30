@echo off
rem ============================================================
rem  Fable 3 (Fable3.exe) - build with the genuine VS2008 SP1 x86
rem  toolchain (cl 15.00.30729.01 / link 9.00.30729.01) + Win SDK 7.1
rem  to match the original Fable3.exe.
rem
rem  Templated from the Fable 3 Launcher build (same toolchain + flags).
rem  The original is a GUI app: PE32, image base 0x400000, subsystem
rem  windows, DYNAMICBASE | NXCOMPAT (DllCharacteristics 0x8140).
rem
rem  Resources (icons, version info, manifest) are embedded to match the
rem  original's .rsrc byte-for-byte. The resource + patch steps run only
rem  once their inputs are in place; until then the build compiles + links
rem  main.cpp only.
rem ============================================================
setlocal EnableExtensions

rem ---- toolchain locations ----
set "V90=D:\Program Files (x86)\Microsoft Visual Studio 9.0"
set "V90BIN=%V90%\VC\bin"
set "SDK=C:\Program Files\Microsoft SDKs\Windows\v7.1"
set "RC=%SDK%\Bin\RC.Exe"
set "ORIG=D:\projects\hacking\Windows\Fable 3\Fable3.exe"

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
set "RRES="
pushd "%~dp0" || exit /b 1
if not exist "%OUTREL%" mkdir "%OUTREL%"

rem ---- resources: run only when the .rc assets are present ----
if not exist "res\icons.rc" goto :skip_res
if not exist "res\man.rc" (set "HAVE_MAN=0") else (set "HAVE_MAN=1")

if exist "%~dp0extract_icons.py" (
    echo.
    echo === extract icons from original ===
    python "%~dp0extract_icons.py" "%ORIG%" "%~dp0res"
    if errorlevel 1 (echo EXTRACT ICONS FAILED & popd & exit /b 1)
)

echo.
echo === rc: icons, lang 0x0809 ===
pushd "res" || exit /b 1
"%RC%" /l 0x0809 /nologo /fo icons.res icons.rc
if errorlevel 1 (echo RC ICONS FAILED & popd & popd & exit /b 1)
popd
set "RRES=%RES%\icons.res"

if defined HAVE_MAN (
    echo.
    echo === rc: manifest, lang 0x0409 ===
    pushd "res" || exit /b 1
    "%RC%" /l 0x0409 /nologo /fo man.res man.rc
    if errorlevel 1 (echo RC MANIFEST FAILED & popd & popd & exit /b 1)
    popd
    set "RRES=%RRES% %RES%\man.res"
)
goto :compile
:skip_res

:compile
pushd "%OUTREL%" || exit /b 1

echo.
echo === cl, x86: main.cpp ===
cl %CLFLAGS% ..\..\main.cpp
if errorlevel 1 (echo COMPILE FAILED & popd & popd & exit /b 1)

echo.
echo === link, x86 ===
link %LNKFLAGS% /out:Fable3.exe /pdb:Fable3.pdb main.obj %RRES%
if errorlevel 1 (echo LINK FAILED & popd & popd & exit /b 1)

if exist ..\..\patch_rsrc.py (
    echo.
    echo === patch .rsrc section to match original ===
    python ..\..\patch_rsrc.py Fable3.exe "%ORIG%"
    if errorlevel 1 (echo PATCH FAILED & popd & popd & exit /b 1)
)

popd
popd
echo.
echo === BUILD OK: %~dp0%OUTREL%\Fable3.exe ===
exit /b 0
