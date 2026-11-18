@echo off
setlocal EnableExtensions
set "V90=D:\Program Files (x86)\Microsoft Visual Studio 9.0"
set "V90BIN=%V90%\VC\bin"
set "SDK=C:\Program Files\Microsoft SDKs\Windows\v7.1"
set "INCLUDE=%V90%\VC\INCLUDE;%SDK%\Include"
set "LIB=%V90%\VC\LIB;%SDK%\Lib"
set "PATH=%V90BIN%;%PATH%"
pushd "%~dp0"
if not exist "Release\Win32" mkdir "Release\Win32"
pushd "Release\Win32"
cl /nologo /c /I ..\.. /O2 /Ob2 /Oi /DNDEBUG /DWIN32 /D_WINDOWS /W3 /EHsc /GS /MT /Zc:forScope ..\..\engine\luavm.cpp
echo EXITCODE=%ERRORLEVEL%
popd
popd
