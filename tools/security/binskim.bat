@echo off
rem BinSkim (Microsoft's checker of a binary's hardening) on the mod's DLLs. It needs debug information, so the
rem DLLs are built once more with the release's switches plus a PDB, into mod\build\analyze\bin - the DLLs for the
rem game (mod\build) are not touched. The log goes to mod\build\analyze\binskim.log.
rem BINSKIM: the path of BinSkim.exe (NuGet package Microsoft.CodeAnalysis.BinSkim, tools\win-x64).
setlocal
if not defined BINSKIM echo set BINSKIM to the path of BinSkim.exe & exit /b 1
if not defined VSCMD_VER call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0..\..\mod"
set OUT=build\analyze\bin
if not exist %OUT% mkdir %OUT%
rem (the first line of switches is build.bat's)
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /LD /Iinclude /Zi /Fo%OUT%\ /Fd%OUT%\d3d11.pdb /Fe:%OUT%\d3d11.dll src\main.cpp src\vtidx.c ^
   /link /DLL /NOLOGO /DEBUG /OPT:REF /OPT:ICF kernel32.lib user32.lib advapi32.lib gdi32.lib >nul
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /LD /Zi /Fo%OUT%\ /Fd%OUT%\dinput.pdb /Fe:%OUT%\dinput.dll dinput\dinput.cpp ^
   /link /DLL /NOLOGO /DEBUG /OPT:REF /OPT:ICF /DEF:dinput\dinput.def kernel32.lib user32.lib >nul
if errorlevel 1 exit /b 1
"%BINSKIM%" analyze %OUT%\d3d11.dll %OUT%\dinput.dll --level Error Warning Note --output build\analyze\binskim.sarif --log ForceOverwrite > build\analyze\binskim.log 2>&1
"%SystemRoot%\System32\findstr.exe" /r /c:": error BA" /c:": warning BA" /c:": note BA" build\analyze\binskim.log
exit /b 0
