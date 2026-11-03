@echo off
powershell -ExecutionPolicy Bypass -File "%~dp0fast_build.ps1" -Target rx -Upload
pause
