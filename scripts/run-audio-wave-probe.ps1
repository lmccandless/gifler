param([Parameter(Mandatory=$true)][string]$TonePath)
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
. "$PSScriptRoot\common.ps1"
$preset = Get-GiflerVisualStudioPreset -Configuration Debug
$exe = Join-Path (Get-Location) "build/$($preset.BinaryDir)/Debug/gifler_tests.exe"
$output = Join-Path (Get-Location) 'artifacts/captured-audio-test.wav'
$process = Start-Process -FilePath $exe -ArgumentList @('--audio-wave-probe', "`"$output`"") -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 500
    $player = [System.Media.SoundPlayer]::new((Resolve-Path -LiteralPath $TonePath).Path)
    try { $player.PlaySync() } finally { $player.Dispose() }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Audio capture probe failed: $($process.ExitCode)" }
} finally { $process.Dispose() }
Write-Host "Captured audio: $output"
