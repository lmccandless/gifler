$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
. "$PSScriptRoot\common.ps1"
$preset = Get-GiflerVisualStudioPreset -Configuration Debug
Invoke-GiflerNativeCommand "build/$($preset.BinaryDir)/Debug/gifler_tests.exe" --audio-probe
