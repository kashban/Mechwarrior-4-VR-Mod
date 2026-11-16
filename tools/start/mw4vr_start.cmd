@echo off
rem Starts MW4 Mercenaries without the administrator prompt (see mw4vr_start.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0mw4vr_start.ps1" %*
