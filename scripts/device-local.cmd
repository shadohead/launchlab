@echo off
if "%~1"=="" (
  "%~dp0..\firmware\.arduino\flash-venv\Scripts\python.exe" "%~dp0local_firmware.py" plan
) else (
  "%~dp0..\firmware\.arduino\flash-venv\Scripts\python.exe" "%~dp0local_firmware.py" %*
)
exit /b %errorlevel%
