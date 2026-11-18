@echo off
rem Static analysis of the mod with clang-tidy (the one that comes with the Visual Studio Build Tools, component
rem "C++ Clang tools"). The log goes to mod\build\analyze\tidy.log.
setlocal
if not defined VSCMD_VER call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
set TIDY=%VCINSTALLDIR%Tools\Llvm\bin\clang-tidy.exe
if not exist "%TIDY%" echo clang-tidy not found: %TIDY% & exit /b 1
cd /d "%~dp0..\..\mod"
if not exist build\analyze mkdir build\analyze
set OUT=build\analyze
rem The checks, and why some are off: .clang-tidy in the repository's root. Exit code 1: something was found.
set ARGS=--target=i686-pc-windows-msvc -fms-compatibility -fms-extensions -D_CRT_SECURE_NO_WARNINGS -Iinclude -w
"%TIDY%" --quiet --config-file=..\.clang-tidy --header-filter=mod.src.* src\main.cpp -- -std=c++17 %ARGS% > %OUT%\tidy.log 2>&1
"%TIDY%" --quiet --config-file=..\.clang-tidy src\vtidx.c -- %ARGS% >> %OUT%\tidy.log 2>&1
"%TIDY%" --quiet --config-file=..\.clang-tidy dinput\dinput.cpp -- -std=c++17 %ARGS% >> %OUT%\tidy.log 2>&1
"%SystemRoot%\System32\find.exe" /c "warning:" %OUT%\tidy.log
"%SystemRoot%\System32\findstr.exe" /c:"warning:" /c:"error:" %OUT%\tidy.log >nul && exit /b 1
exit /b 0
