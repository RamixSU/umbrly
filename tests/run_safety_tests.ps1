param([string]$Executable = "$PSScriptRoot\..\umbrly.exe")
$ErrorActionPreference = "Continue"
$source = Join-Path $PSScriptRoot "cases\infinite_loop.umb"

$stepOutput = @(& $Executable -limit 10000 5000 $source 2>&1 | ForEach-Object { $_.ToString() })
$stepExit = $LASTEXITCODE
$stepOk = $stepExit -eq 1 -and (($stepOutput -join "`n") -like "*10000*")

$timeOutput = @(& $Executable -limit 0 20 $source 2>&1 | ForEach-Object { $_.ToString() })
$timeExit = $LASTEXITCODE
$timeOk = $timeExit -eq 1 -and (($timeOutput -join "`n") -like "*20*")

if ($stepOk) { Write-Host "PASS instruction limit" -ForegroundColor Green } else { Write-Host "FAIL instruction limit" -ForegroundColor Red }
if ($timeOk) { Write-Host "PASS timeout limit" -ForegroundColor Green } else { Write-Host "FAIL timeout limit" -ForegroundColor Red }
if (!$stepOk -or !$timeOk) { exit 1 }
Write-Host "All safety limit tests passed"
