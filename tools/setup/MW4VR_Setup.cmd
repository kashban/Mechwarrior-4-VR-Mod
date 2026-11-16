@echo off
rem MW4VR setup - a window that installs the mod into the game's folder (see mw4vr_readme.txt).
start "" powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0mw4vr_setup.ps1"
