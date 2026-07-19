@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "AUDIO=%~1"
set "OUTPUT=%~2"

if "%AUDIO%"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\run_simple.ps1" -ChooseAudio
    exit /b %ERRORLEVEL%
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\run_simple.ps1" -Audio "%AUDIO%" -Output "%OUTPUT%"
exit /b %ERRORLEVEL%
