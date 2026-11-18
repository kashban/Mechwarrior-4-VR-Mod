@echo off
rem Starts MW4 Mercenaries without the administrator prompt (see mw4vr_start.ps1).
rem (Windows' own PowerShell by its full path: by its name alone, a powershell.exe or .cmd lying in the game's
rem folder would be started instead)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0mw4vr_start.ps1" %*
