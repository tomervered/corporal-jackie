@echo off
rem Applies a downloaded update package to this Corporal Jackie folder.
rem Drag CorporalJackie-windows-x64-update.zip onto this file, or double-click
rem it to use the newest one in your Downloads folder. The checks and the
rem work are done by app\updater\apply-update.ps1, which came with this copy.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0app\updater\apply-update.ps1" -Root "%~dp0." %1
