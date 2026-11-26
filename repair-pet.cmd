@echo off
setlocal
if exist "%~dp0wx_demo.exe" (
  set "WX_DEMO_APP_DIR=%~dp0."
) else (
  set "WX_DEMO_APP_DIR=%~dp0x64\Debug"
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup-pet.ps1" -ExecutableDirectory "%WX_DEMO_APP_DIR%" %*
if errorlevel 1 (
  echo Repair failed. See %%LOCALAPPDATA%%\wx_demo\pet-setup.log
  pause
  exit /b 1
)
echo Repair completed. Reopen or reload the companion.
pause
