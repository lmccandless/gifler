$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
. "$PSScriptRoot\common.ps1"

$preset = Get-GiflerVisualStudioPreset -Configuration Release
Invoke-GiflerNativeCommand cmake --preset $preset.Configure
Invoke-GiflerNativeCommand cmake --build --preset $preset.Build

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

$exe = Join-Path $root "build/$($preset.BinaryDir)/Release/Gifler.exe"
$pdb = Join-Path $root "build/$($preset.BinaryDir)/Release/Gifler.pdb"
$portableExe = Join-Path $dist "Gifler.exe"

if (!(Test-Path $exe)) {
    throw "Missing release executable: $exe"
}

$stage = Join-Path $dist "stage"
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe $stage
Copy-Item $exe $portableExe -Force
if (Test-Path $pdb) {
    Copy-Item $pdb $stage
}
Copy-Item "$root/docs/encoder-setup.md" $stage

$size = (Get-Item $exe).Length
Write-Host "Packaged: $portableExe"
Write-Host ("Gifler.exe size: {0:N0} bytes ({1:N2} MB)" -f $size, ($size / 1MB))
