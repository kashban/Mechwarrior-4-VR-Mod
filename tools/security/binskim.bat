@echo off
rem BinSkim (Microsoft's checker of a binary's hardening) on the mod's DLLs. It needs debug information, so the
rem DLLs are built once more by mod\build.bat with a PDB, into mod\build\analyze\bin - the DLLs for the game
rem (mod\build) are not touched. The log goes to mod\build\analyze\binskim.log.
rem BINSKIM: the path of BinSkim.exe (NuGet package Microsoft.CodeAnalysis.BinSkim, tools\win-x64).
rem Exit code: 1 when BinSkim names an error (the release's gate), 0 otherwise; warnings and notes are listed.
setlocal
if not defined BINSKIM echo set BINSKIM to the path of BinSkim.exe & exit /b 2
set MW4VR_OUT=build\analyze\bin
set MW4VR_PDB=1
call "%~dp0..\..\mod\build.bat" >nul
if errorlevel 1 echo the build with debug information failed & exit /b 2
cd /d "%~dp0..\..\mod"
"%BINSKIM%" analyze %MW4VR_OUT%\d3d11.dll %MW4VR_OUT%\dinput.dll --level Error Warning Note --output build\analyze\binskim.sarif --log ForceOverwrite > build\analyze\binskim.log 2>&1
"%SystemRoot%\System32\findstr.exe" /r /c:": error BA" /c:": warning BA" /c:": note BA" build\analyze\binskim.log
"%SystemRoot%\System32\findstr.exe" /r /c:": error BA" build\analyze\binskim.log >nul && exit /b 1
exit /b 0
