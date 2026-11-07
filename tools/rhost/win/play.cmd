@echo off
rem Lords II free-play capture (OpenLords2 rhost) - tools/rhost/win/play.ps1
title Lords II capture
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play.ps1" %*
pause
