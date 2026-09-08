$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
. "$PSScriptRoot\common.ps1"
$preset = Get-GiflerVisualStudioPreset -Configuration Debug
$settingsRoot = Join-Path (Get-Location) ('artifacts/ui-smoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $settingsRoot | Out-Null
Invoke-GiflerNativeCommand "build/$($preset.BinaryDir)/Debug/gifler_app_smoke.exe" $settingsRoot
Write-Host "UI renders and isolated settings: $settingsRoot"
