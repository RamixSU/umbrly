param(
    [string]$Executable = "$PSScriptRoot\..\umbrly.exe",
    [string[]]$Cases = @("arithmetic", "control_flow", "functions", "function_for", "function_calls")
)

$ErrorActionPreference = "Continue"
$artifactDir = Join-Path $PSScriptRoot ".artifacts"
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null

$failed = 0

foreach ($name in $Cases) {
    $source = Join-Path $PSScriptRoot "cases\$name.umb"
    $native = Join-Path $artifactDir "$name.exe"

    $interpreted = @(& $Executable $source 2>&1 | ForEach-Object { $_.ToString().TrimEnd() })
    $interpretedExit = $LASTEXITCODE

    $buildOutput = @(& $Executable -b $source $native 2>&1)
    $buildExit = $LASTEXITCODE
    if ($buildExit -ne 0) {
        Write-Host "FAIL $name (native build failed)" -ForegroundColor Red
        $buildOutput | ForEach-Object { Write-Host "  $_" }
        $failed++
        continue
    }

    $compiled = @(& $native 2>&1 | ForEach-Object { $_.ToString().TrimEnd() })
    $compiledExit = $LASTEXITCODE
    $sameOutput = [string]::Equals(
        (($interpreted -join "`n").Trim()),
        (($compiled -join "`n").Trim()),
        [System.StringComparison]::Ordinal)
    $ok = $interpretedExit -eq $compiledExit -and $sameOutput

    if ($ok) { Write-Host "PASS parity $name" -ForegroundColor Green }
    else {
        Write-Host "FAIL parity $name" -ForegroundColor Red
        Write-Host "  interpreter exit=$interpretedExit output=$($interpreted -join ' | ')"
        Write-Host "  native      exit=$compiledExit output=$($compiled -join ' | ')"
        $failed++
    }
}

if ($failed) { Write-Host "$failed parity test(s) failed"; exit 1 }
Write-Host "All $($Cases.Count) parity tests passed"
