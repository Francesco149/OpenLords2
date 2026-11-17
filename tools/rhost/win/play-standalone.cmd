@echo off
rem OpenLords2 standalone (the port, no retail code) with free-play capture - tools/rhost/win/play.ps1 -Standalone
title OpenLords2 (standalone)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play.ps1" -Standalone %*
pause
