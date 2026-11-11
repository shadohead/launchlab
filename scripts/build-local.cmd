@echo off
"%~dp0..\firmware\.arduino\flash-venv\Scripts\python.exe" -u "%~dp0local_firmware.py" build
if errorlevel 1 (
  echo.
  echo Build failed. No flashable development bundle was approved.
  exit /b 1
)
echo.
echo Build complete. No device was connected or flashed.
