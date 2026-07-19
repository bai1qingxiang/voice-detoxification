@echo off
setlocal

set "PROJECT_DIR=%~dp0"
cd /d "%PROJECT_DIR%"

if not exist "build\build.ninja" (
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGGML_CCACHE=OFF
    if errorlevel 1 goto :error
)

cmake --build build --target voice_detox_app voice_detox_desktop --parallel 1
if errorlevel 1 goto :error

for %%D in (libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libgomp-1.dll) do (
    for /f "delims=" %%P in ('g++ -print-file-name=%%D') do copy /y "%%P" "build\bin\%%D" >nul
)

start "" "%PROJECT_DIR%build\bin\voice_detox_desktop.exe"
exit /b 0

:error
echo.
echo Desktop application build failed.
pause
exit /b 1
