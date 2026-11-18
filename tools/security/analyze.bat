@echo off
rem Static analysis of the mod with the compiler's own analyser (cl /analyze). Nothing is built for the game:
rem the output goes to mod\build\analyze and the log to mod\build\analyze\analyze.log.
setlocal
if not defined VSCMD_VER call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0..\..\mod"
if not exist build\analyze mkdir build\analyze
set OUT=build\analyze
rem /analyze:only: no code is generated; /analyze:external-: the SDK's and OpenXR's headers are not ours to fix
set FLAGS=/nologo /MT /EHsc /std:c++17 /W4 /sdl /analyze /analyze:only /analyze:stacksize 65536 ^
  /external:anglebrackets /external:W0 /analyze:external- /Iinclude
cl %FLAGS% /Fo%OUT%\ src\*.cpp src\vtidx.c > %OUT%\analyze.log 2>&1
cl %FLAGS% /Fo%OUT%\ dinput\dinput.cpp >> %OUT%\analyze.log 2>&1
"%SystemRoot%\System32\find.exe" /c "warning C" %OUT%\analyze.log
exit /b 0
