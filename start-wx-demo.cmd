@echo off
setlocal
if exist "%~dp0wx_demo.exe" (
  set "WX_DEMO_APP_DIR=%~dp0."
) else (
  set "WX_DEMO_APP_DIR=%~dp0x64\Debug"
)
if not exist "%WX_DEMO_APP_DIR%\wx_demo.exe" (
  echo No executable found. Build wx_demo, or download the complete binary release.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup-pet.ps1" -ExecutableDirectory "%WX_DEMO_APP_DIR%"
if errorlevel 1 (
  echo Pet setup failed. The editor can still open. See %%LOCALAPPDATA%%\wx_demo\pet-setup.log
  pause
)
start "" "%WX_DEMO_APP_DIR%\wx_demo.exe"
