@echo off
title ASME e-HPVC — Build & Upload Wheel Speed Sensor Test (DIO Pin 1)
powershell -ExecutionPolicy Bypass -File "%~dp0fast_build.ps1" -Upload %*
pause
