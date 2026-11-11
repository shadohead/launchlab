$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
& (Join-Path $repo 'firmware/.arduino/flash-venv/Scripts/python.exe') (Join-Path $PSScriptRoot 'local_firmware.py') build
exit $LASTEXITCODE
