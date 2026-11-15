@echo off
rem Builds the 32-bit d3d11.dll of MW4VR (MW4Mercs.exe is a 32-bit process).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /LD /Iinclude /Fobuild\ /Fe:build\d3d11.dll src\main.cpp src\vtidx.c ^
   /link /DLL /NOLOGO kernel32.lib user32.lib advapi32.lib gdi32.lib
if errorlevel 1 exit /b 1
rem dinput.dll: in front of the system's old DirectInput (see dinput\dinput.cpp)
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /LD /Fobuild\ /Fe:build\dinput.dll dinput\dinput.cpp ^
   /link /DLL /NOLOGO /DEF:dinput\dinput.def kernel32.lib user32.lib
exit /b %errorlevel%
