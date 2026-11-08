@echo off
title ASME e-HPVC — Build & Upload Transmitter (Trike Node)
powershell -ExecutionPolicy Bypass -File "%~dp0fast_build.ps1" -Target tx -Upload %*
pause
