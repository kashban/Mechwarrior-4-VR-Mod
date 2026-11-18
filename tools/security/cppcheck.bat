@echo off
rem cppcheck on the mod's C++ (and the core's tests). The log goes to mod\build\analyze\cppcheck.log.
rem CPPCHECK: the path of cppcheck.exe.
rem Exit code: 1 when it names a finding of the severity "error", 0 otherwise.
setlocal
if not defined CPPCHECK echo set CPPCHECK to the path of cppcheck.exe & exit /b 1
cd /d "%~dp0..\..\mod"
if not exist build\analyze mkdir build\analyze
set T="--template={file}:{line}: [{severity}/{id}] {message}"
set COMMON=--platform=win32W --quiet --inline-suppr --suppress=missingIncludeSystem -D_WIN32 -D_MSC_VER=1940 -D_M_IX86
"%CPPCHECK%" %COMMON% %T% --enable=warning,portability,performance --inconclusive --std=c++17 --language=c++ -Iinclude ^
   src\main.cpp dinput\dinput.cpp ..\tests\core_test.cpp 2> build\analyze\cppcheck.log
"%CPPCHECK%" %COMMON% %T% --enable=warning,portability src\vtidx.c 2>> build\analyze\cppcheck.log
"%SystemRoot%\System32\find.exe" /c "[" build\analyze\cppcheck.log
"%SystemRoot%\System32\findstr.exe" /c:"[error/" build\analyze\cppcheck.log && exit /b 1
exit /b 0
