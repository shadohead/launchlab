param(
    [ValidateSet('plan','ports','backup','flash')][string]$Action = 'plan',
    [string]$Port,
    [string]$Backup,
    [switch]$Execute
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$deviceArgs = @((Join-Path $PSScriptRoot 'local_firmware.py'), $Action)
if ($Port) { $deviceArgs += @('--port', $Port) }
if ($Backup) { $deviceArgs += @('--backup', $Backup) }
if ($Execute) { $deviceArgs += '--execute' }
& (Join-Path $repo 'firmware/.arduino/flash-venv/Scripts/python.exe') @deviceArgs
exit $LASTEXITCODE
