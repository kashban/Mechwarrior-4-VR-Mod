@echo off
rem All automatic tests; with "mutation" also the mutation round (a few minutes).
rem set MW4_DIR=<the game's folder> to have mw4hook_abi.h held against mw4hook.dll and MW4Mercs.exe as well.
cd /d "%~dp0.."
python -W error -m unittest discover -s tests || exit /b 1
call tests\core_test.bat || exit /b 1
if "%1"=="mutation" python tests\mutate.py || exit /b 1
