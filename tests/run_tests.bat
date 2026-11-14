@echo off
rem All automatic tests; with "mutation" also the mutation round (several minutes).
cd /d "%~dp0.."
python -W error -m unittest discover -s tests || exit /b 1
if "%1"=="mutation" python tests\mutate.py || exit /b 1
