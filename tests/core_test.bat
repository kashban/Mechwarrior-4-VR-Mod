@echo off
rem Builds and runs the tests of mod\src\core.h. An argument names another folder holding core.h (tests\mutate.py:
rem a copy with one planted error); the program is then built into that folder.
setlocal
if not defined VSCMD_VER call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1 || exit /b 2
set "SRC=%~dp0..\mod\src"
set "OUT=%~dp0build"
if not "%~1"=="" set "SRC=%~1" & set "OUT=%~1"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /Od /EHsc /std:c++17 /W3 /I"%SRC%" /Fo"%OUT%\\" /Fe:"%OUT%\core_test.exe" "%~dp0core_test.cpp" >"%OUT%\core_test_build.log" 2>&1
if errorlevel 1 (type "%OUT%\core_test_build.log" & exit /b 3)
"%OUT%\core_test.exe"
exit /b %errorlevel%
