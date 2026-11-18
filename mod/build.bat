@echo off
rem Builds the 32-bit d3d11.dll of MW4VR (MW4Mercs.exe is a 32-bit process).
rem MW4VR_OUT: another folder than build (under mod\). MW4VR_PDB=1: with debug information beside the DLLs - what
rem tools\security\binskim.bat needs; the DLLs for the game are built without.
setlocal
rem (in a Visual Studio x86 prompt, or the pipeline's, the compiler is there already)
if not defined VSCMD_VER call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
set OUT=build
if defined MW4VR_OUT set OUT=%MW4VR_OUT%
if not exist %OUT% mkdir %OUT%
rem Hardening: the compiler's extra checks (/sdl: among others the C library's unchecked functions - wcscpy,
rem swscanf, fopen - are errors) and Control Flow Guard.
rem Control Flow Guard checks nothing inside the game (its .exe is built without, so Windows leaves the checks
rem empty); it is on for the day the DLL is loaded by a program that has it.
set HARDEN=/sdl /guard:cf
set DBG1=
set DBG2=
set DBGL=
if defined MW4VR_PDB set DBG1=/Zi /Fd%OUT%\d3d11.pdb
if defined MW4VR_PDB set DBG2=/Zi /Fd%OUT%\dinput.pdb
if defined MW4VR_PDB set DBGL=/DEBUG /OPT:REF /OPT:ICF
rem The units are compiled for optimisation at link time (/GL, /LTCG): a call between two of them costs what a
rem call inside one file costs.
cl /nologo /O2 /GL /MP /MT /EHsc /std:c++17 /W3 %HARDEN% %DBG1% /LD /Iinclude /Fo%OUT%\ /Fe:%OUT%\d3d11.dll src\*.cpp src\vtidx.c ^
   /link /DLL /NOLOGO /LTCG /guard:cf %DBGL% kernel32.lib user32.lib advapi32.lib gdi32.lib
if errorlevel 1 exit /b 1
rem dinput.dll: in front of the system's old DirectInput (see dinput\dinput.cpp)
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 %HARDEN% %DBG2% /LD /Fo%OUT%\ /Fe:%OUT%\dinput.dll dinput\dinput.cpp ^
   /link /DLL /NOLOGO /guard:cf %DBGL% /DEF:dinput\dinput.def kernel32.lib user32.lib
exit /b %errorlevel%
