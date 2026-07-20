$ErrorActionPreference = "Stop"

function Get-GiflerVisualStudioPreset {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("Debug", "Release")]
        [string] $Configuration
    )

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $instances = @()
    if (Test-Path $vswhere) {
        $instances = & $vswhere -all -products * -format json | ConvertFrom-Json
    }

    $hasVs2022 = $instances | Where-Object { $_.installationVersion -like "17.*" } | Select-Object -First 1
    $hasVs2026 = $instances | Where-Object { $_.installationVersion -like "18.*" } | Select-Object -First 1

    $suffix = $Configuration.ToLowerInvariant()
    if ($hasVs2022) {
        return [PSCustomObject]@{
            Configure = "vs2022-x64-$suffix"
            Build = $suffix
            Test = $suffix
            BinaryDir = "vs2022-x64-$suffix"
        }
    }

    if ($hasVs2026) {
        return [PSCustomObject]@{
            Configure = "vs2026-x64-$suffix"
            Build = "$suffix-vs2026"
            Test = "$suffix-vs2026"
            BinaryDir = "vs2026-x64-$suffix"
        }
    }

    throw "No Visual Studio 2022 or 2026 instance was found. Install Visual Studio Build Tools with the Desktop C++ workload."
}

function Invoke-GiflerNativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string] $FilePath,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]] $Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}
