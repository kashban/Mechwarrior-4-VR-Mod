@echo off
rem Starts MW4 Mercenaries again until it gets past the flaky start-up (see mw4vr_start.ps1).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0mw4vr_start.ps1" %*
