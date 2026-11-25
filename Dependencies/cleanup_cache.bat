@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0cleanup_cache.ps1"
pause
