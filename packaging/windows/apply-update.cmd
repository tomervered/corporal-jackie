@echo off
rem Applies this update to the Corporal Jackie folder it was unzipped into.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_update\apply-update.ps1"
rem The update is done (or refused); this script deletes itself.
(goto) 2>nul & del "%~f0"
