$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)
. "$PSScriptRoot\common.ps1"

$preset = Get-GiflerVisualStudioPreset -Configuration Debug
Invoke-GiflerNativeCommand cmake --preset $preset.Configure
Invoke-GiflerNativeCommand cmake --build --preset $preset.Build
