$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
. "$PSScriptRoot\common.ps1"
$preset = Get-GiflerVisualStudioPreset -Configuration Debug
$output = Join-Path (Get-Location) 'artifacts/audio-packets.csv'
Invoke-GiflerNativeCommand "build/$($preset.BinaryDir)/Debug/gifler_audio_packet_probe.exe" $output
$rows = Import-Csv -LiteralPath $output
$qpcGaps = $rows | Where-Object { [math]::Abs([long]$_.qpcGapFrames) -gt 2 }
$deviceGaps = $rows | Where-Object { [long]$_.deviceGapFrames -ne 0 }
Write-Host "QPC jumps >2 samples: $($qpcGaps.Count); device-position gaps: $($deviceGaps.Count)"
$qpcGaps | Select-Object -First 10 | Format-Table
Write-Host "Packet metadata: $output"
