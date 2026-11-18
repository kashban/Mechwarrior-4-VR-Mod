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
rem what is looked for: bugs, the CERT rules, the analyser's paths. Left out: rules about style and names only.
set CHECKS=-*,bugprone-*,cert-*,clang-analyzer-*,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions,-bugprone-reserved-identifier,-cert-dcl37-c,-cert-dcl51-cpp,-bugprone-implicit-widening-of-multiplication-result,-cert-err33-c,-bugprone-unchecked-string-to-number-conversion,-cert-err34-c,-cert-dcl50-cpp,-cert-err58-cpp,-bugprone-multi-level-implicit-pointer-conversion,-bugprone-casting-through-void,-bugprone-switch-missing-default-case,-bugprone-branch-clone,-bugprone-macro-parentheses
set ARGS=--target=i686-pc-windows-msvc -fms-compatibility -fms-extensions -D_CRT_SECURE_NO_WARNINGS -Iinclude -w
"%TIDY%" --quiet --checks=%CHECKS% --header-filter=mod.src.* src\main.cpp -- -std=c++17 %ARGS% > %OUT%\tidy.log 2>&1
"%TIDY%" --quiet --checks=%CHECKS% src\vtidx.c -- %ARGS% >> %OUT%\tidy.log 2>&1
"%TIDY%" --quiet --checks=%CHECKS% dinput\dinput.cpp -- -std=c++17 %ARGS% >> %OUT%\tidy.log 2>&1
"%SystemRoot%\System32\find.exe" /c "warning:" %OUT%\tidy.log
exit /b 0
