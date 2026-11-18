@echo off
rem MW4VR setup - a window that installs the mod into the game's folder (see mw4vr_readme.txt).
rem (Windows' own PowerShell by its full path: by its name alone, a powershell.exe or .cmd lying in this folder
rem would be started instead)
start "" "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0mw4vr_setup.ps1"
