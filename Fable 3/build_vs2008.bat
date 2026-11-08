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
rem /I ..\..  adds the project root so modules include each other root-relative
set "CLFLAGS=/nologo /c /I ..\.. /O2 /Ob2 /Oi /DNDEBUG /DWIN32 /D_WINDOWS /W3 /EHsc /GS /MT /GL /Z7 /Fds"
rem ---- objdiff base objects: same flags WITHOUT /GL (native machine code, not LTCG IR) ----
set "CLFLAGS_NOGL=/nologo /c /I ..\.. /O2 /Ob2 /Oi /DNDEBUG /DWIN32 /D_WINDOWS /W3 /EHsc /GS /MT /Z7 /Fds"
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

:: Application sources: the entry point plus one file per module folder.
:: Add a new module by listing its .cpp in SRCS (paths are relative to the
:: project root; each .obj is produced in the Release/Win32 output dir and
:: then linked below by name).
set "SRCS=..\..\main.cpp ..\..\framework\framework.cpp ..\..\common\common.cpp ..\..\engine\engine.cpp ..\..\engine\worker.cpp ..\..\engine\manager.cpp ..\..\engine\object_registry.cpp ..\..\engine\xlo_device.cpp ..\..\engine\gameengine.cpp ..\..\engine\gamethread.cpp ..\..\engine\gamemain.cpp ..\..\engine\netinit.cpp ..\..\engine\thread.cpp ..\..\save\save.cpp"

for %%F in (%SRCS%) do (
    echo.
    echo === cl, x86: %%F ===
    cl %CLFLAGS% "%%F"
    if errorlevel 1 (echo COMPILE FAILED & popd & popd & exit /b 1)
)

echo.
echo === lib: import libraries (from engine/*.def) ===
rem One .def per non-SDK DLL the original imports; generate an import
rem library for each so the linker can bind calls to those DLLs. (Standard
rem Windows DLLs below use the Win SDK import libraries directly.)
set "IMPORT_DEFS=xlive d3d9 dinput8 dsound xinput1_3 x3daudio1_7 d3dx9_42 binkw32 steam_api"
for %%d in (%IMPORT_DEFS%) do (
    lib /nologo /machine:x86 /def:..\..\engine\%%d.def /out:%%d.lib
    if errorlevel 1 (echo LIB FAILED: %%d & popd & exit /b 1)
)

echo.
echo === link, x86 ===
rem .lib order: bundled third-party / DirectX import libs, then the standard
rem Windows DLLs the original imports (kernel32 is pulled by the default CRT).
set "IMPORT_LIBS=xlive.lib d3d9.lib dinput8.lib dsound.lib xinput1_3.lib x3daudio1_7.lib d3dx9_42.lib binkw32.lib steam_api.lib user32.lib gdi32.lib shell32.lib ole32.lib rpcrt4.lib oleaut32.lib ws2_32.lib"
link %LNKFLAGS% /out:Fable3.exe /pdb:Fable3.pdb main.obj framework.obj common.obj engine.obj worker.obj manager.obj object_registry.obj xlo_device.obj gameengine.obj gamethread.obj gamemain.obj netinit.obj thread.obj save.obj %IMPORT_LIBS% %RRES%
if errorlevel 1 (echo LINK FAILED & popd & popd & exit /b 1)

rem The exe now really imports from the bundled third-party DLLs; copy the
rem ones the original loads (xlive, binkw32, steam_api) from the project-root
rem "dlls" folder next to the exe so the loader binds.
set "DLLDIR=..\..\..\dlls"
for %%d in (xlive.dll binkw32.dll steam_api.dll) do (
    if exist "%DLLDIR%\%%d" copy /y "%DLLDIR%\%%d" . >nul
)

if exist ..\..\patch_rsrc.py (
    echo.
    echo === patch .rsrc section to match original ===
    python ..\..\patch_rsrc.py Fable3.exe "%ORIG%"
    if errorlevel 1 (echo PATCH FAILED & popd & popd & exit /b 1)
)

rem ---- objdiff base objects: a second, no-/GL compile of every source ----
rem The /GL objects above hold MSObj (LTCG) IR, which objdiff cannot read. A
rem parallel compile without /GL emits native machine-code objects (one per
rem source) named <name>_objdiff.obj. The /GL objects (main.obj, ...) are
rem left intact for the linker; these *_objdiff.obj are base objects only.
echo.
echo === cl, x86, no /GL (objdiff base objects) ===
rem NOTE: %%~nG is expanded per-iteration (unlike a set var inside the loop body,
rem which is expanded once at parse time), so /Fo gets the right name each pass.
for %%F in (%SRCS%) do (
    for %%G in ("%%F") do (
        cl %CLFLAGS_NOGL% /Fo"%%~nG_objdiff.obj" "%%F"
        if errorlevel 1 (echo OBJDIFF COMPILE FAILED: %%F & popd & popd & exit /b 1)
    )
)

popd
popd

rem ---- objdiff config: build the synthesized target + objdiff.json ----
if exist "%~dp0gen_objdiff.py" (
    echo.
    echo === gen objdiff.json ===
    python "%~dp0gen_objdiff.py"
    if errorlevel 1 (echo GEN OBJDIFF FAILED & exit /b 1)
)

echo.
echo === BUILD OK: %~dp0%OUTREL%\Fable3.exe ===
exit /b 0
