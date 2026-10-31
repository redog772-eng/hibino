@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_arena.ps1" %*
if errorlevel 1 (
  echo.
  echo MATCH START FAILED. Send the ERROR line back.
)
pause
