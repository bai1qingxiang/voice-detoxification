@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "AUDIO=%~1"
set "OUTPUT=%~2"
set "VOICE_OPTION=%~3"

if "%AUDIO%"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\run_simple.ps1" -ChooseAudio
    exit /b %ERRORLEVEL%
)

if /I "%VOICE_OPTION%"=="--restore-voice" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\run_simple.ps1" -Audio "%AUDIO%" -Output "%OUTPUT%" -RestoreVoice
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%scripts\run_simple.ps1" -Audio "%AUDIO%" -Output "%OUTPUT%"
)
exit /b %ERRORLEVEL%
