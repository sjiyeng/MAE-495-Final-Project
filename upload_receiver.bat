@echo off
title ASME e-HPVC — Build & Upload Receiver (Pitwall Dashboard Node)
powershell -ExecutionPolicy Bypass -File "%~dp0fast_build.ps1" -Target rx -Upload %*
pause
