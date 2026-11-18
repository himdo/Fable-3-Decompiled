@echo off
setlocal EnableExtensions
set "V90=D:\Program Files (x86)\Microsoft Visual Studio 9.0"
set "SDK=C:\Program Files\Microsoft SDKs\Windows\v7.1"
set "INCLUDE=%V90%\VC\INCLUDE;%SDK%\Include"
set "LIB=%V90%\VC\LIB;%SDK%\Lib"
set "PATH=%V90%\VC\bin;%PATH%"
pushd "%~dp0"
echo === running build_vs2008 ===
cmd /c build_vs2008.bat > _fullbuild.log 2>&1
echo FULLBUILD_EXIT=%ERRORLEVEL%
