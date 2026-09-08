param([string] $Ffmpeg = 'ffmpeg.exe', [string] $Ffprobe = 'ffprobe.exe')
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
. "$PSScriptRoot\common.ps1"
$preset = Get-GiflerVisualStudioPreset -Configuration Debug
$encoder = (Get-Command $Ffmpeg -ErrorAction Stop).Source
$probe = (Get-Command $Ffprobe -ErrorAction Stop).Source
$output = Join-Path (Get-Location) 'artifacts/export-smoke'
Invoke-GiflerNativeCommand "build/$($preset.BinaryDir)/Debug/gifler_export_smoke.exe" $encoder $output
foreach ($extension in @('mp4', 'webm', 'gif')) {
    $path = Join-Path $output "ten-seconds.$extension"
    $json = & $probe -v error -count_frames -show_streams -show_format -of json $path
    if ($LASTEXITCODE) { throw "ffprobe failed for $path" }
    $info = $json | ConvertFrom-Json
    $video = $info.streams | Where-Object codec_type -eq 'video' | Select-Object -First 1
    if ([math]::Abs([double]$info.format.duration - 10) -gt 0.15) { throw "$extension duration is not 10 seconds" }
    if ($extension -ne 'gif') {
        if ($video.r_frame_rate -ne '30/1') { throw "$extension frame rate is not 30 FPS" }
        if ([int]$video.nb_read_frames -ne 300) { throw "$extension does not contain 300 video frames" }
        $audio = $info.streams | Where-Object codec_type -eq 'audio'
        if (!$audio) { throw "$extension has no audio" }
    }
    Write-Host "$extension verified: $($info.format.duration)s, $($video.nb_read_frames) frames"
}
# Inspect animated WebP's RIFF frame durations without depending on decoder support.
$bytes = [IO.File]::ReadAllBytes((Join-Path $output 'ten-seconds.webp'))
$offset = 12; $durationMs = 0; $frames = 0
while ($offset + 8 -le $bytes.Length) {
    $tag = [Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
    $size = [BitConverter]::ToUInt32($bytes, $offset + 4)
    if ($tag -eq 'ANMF') {
        $durationMs += [int]$bytes[$offset + 20] + ([int]$bytes[$offset + 21] -shl 8) + ([int]$bytes[$offset + 22] -shl 16)
        $frames++
    }
    $offset += 8 + $size + ($size % 2)
}
if ([math]::Abs($durationMs - 10000) -gt 100 -or $frames -lt 290) { throw "WebP timing mismatch: $durationMs ms / $frames frames" }
Write-Host "WebP verified: $durationMs ms, $frames frames"
$duration = & $probe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 (Join-Path $output 'ninety-seconds.gif')
if ($LASTEXITCODE -or [math]::Abs([double]$duration - 90) -gt 0.1) { throw 'Long coalesced GIF lost its 90-second duration' }
Write-Host "Long coalesced GIF verified: $duration seconds"
foreach ($name in @('social-48000', 'social-44100', 'social-wide-short')) {
    $path = Join-Path $output "$name.mp4"
    $json = & $probe -v error -count_frames -show_streams -show_format -of json $path
    if ($LASTEXITCODE) { throw "ffprobe failed for $path" }
    $info = $json | ConvertFrom-Json
    $video = $info.streams | Where-Object codec_type -eq 'video' | Select-Object -First 1
    $audio = $info.streams | Where-Object codec_type -eq 'audio' | Select-Object -First 1
    $rate = if ($name -eq 'social-44100') { 44100 } else { 48000 }
    $expectedDuration = if ($name -eq 'social-wide-short') { 0.5 } else { 1.0 }
    if ($video.codec_name -ne 'h264' -or $video.profile -ne 'High' -or $video.pix_fmt -ne 'yuv420p' -or
        $video.sample_aspect_ratio -ne '1:1' -or $video.field_order -ne 'progressive' -or $video.r_frame_rate -ne '30/1') {
        throw "Social video format mismatch: $name"
    }
    if ($audio.codec_name -ne 'aac' -or $audio.profile -ne 'LC' -or $audio.channels -ne 2 -or [int]$audio.sample_rate -ne $rate) {
        throw "Social audio format mismatch: $name"
    }
    if ([math]::Abs([double]$info.format.duration - $expectedDuration) -gt 0.03 -or
        [int]$video.nb_read_frames -ne [int]($expectedDuration * 30)) { throw "Social duration/frame mismatch: $name" }
    if ($name -eq 'social-wide-short' -and ($video.width -ne 1000 -or $video.height -ne 420)) { throw 'Wide clip was not padded correctly' }
    Write-Host "$name verified: H.264 High, 30 FPS, square pixels, AAC-LC stereo $rate Hz, $($info.format.duration)s"
}
foreach ($fps in @(60, 77, 120, 240)) {
    $path = Join-Path $output "fps-$fps.mp4"
    $json = & $probe -v error -count_frames -show_streams -show_format -of json $path
    if ($LASTEXITCODE) { throw "ffprobe failed for $path" }
    $info = $json | ConvertFrom-Json
    $video = $info.streams | Where-Object codec_type -eq 'video' | Select-Object -First 1
    if ($video.r_frame_rate -ne "$fps/1" -or [int]$video.nb_read_frames -ne $fps -or
        [math]::Abs([double]$info.format.duration - 1) -gt 0.02) {
        throw "$fps FPS export has incorrect frame count, rate, or duration"
    }
    Write-Host "High/custom FPS verified: $fps FPS, $fps frames, 1 second"
}
