param(
    [string]$Executable = "$PSScriptRoot\..\umbrly.exe"
)

$ErrorActionPreference = "Continue"
$cases = @(
    @{ Name="arithmetic"; Exit=0; Output=@("10","4","21","2.33333","1","TRUE") },
    @{ Name="control_flow"; Exit=0; Output=@("25","0") },
    @{ Name="while_vm"; Exit=0; Output=@("5000050000") },
    @{ Name="functions"; Exit=0; Output=@("3628800") },
    @{ Name="function_locals"; Exit=0; Output=@("500500") },
    @{ Name="function_for"; Exit=0; Output=@("31") },
    @{ Name="function_calls"; Exit=0; Output=@("10100") },
    @{ Name="function_float_fallback"; Exit=0; Output=@("1.5") },
    @{ Name="value_vm"; Exit=0; Output=@("Hello, Umbrly! x2!","7:8:9") },
    @{ Name="value_vm_loops"; Exit=0; Output=@("10","[2, 4, 6]") },
    @{ Name="value_vm_objects"; Exit=0; Output=@("17") },
    @{ Name="value_vm_print"; Exit=0; Output=@("VM says hello, world","5") },
    @{ Name="value_vm_short_circuit"; Exit=0; Output=@("FALSE","TRUE","TRUE") },
    @{ Name="arrays"; Exit=0; Output=@("[1, 2, 3, 4]","4","3") },
    @{ Name="errors"; Exit=0; Output=@("caught") },
    @{ Name="cycle_rejected"; Exit=1; Contains="PUSH()" }
)

$failed = 0
foreach ($case in $cases) {
    $file = Join-Path $PSScriptRoot "cases\$($case.Name).umb"
    $lines = @(& $Executable $file 2>&1 | ForEach-Object { $_.ToString() })
    $exit = $LASTEXITCODE
    $ok = $exit -eq $case.Exit
    if ($case.ContainsKey("Contains")) {
        $ok = $ok -and (($lines -join "`n") -like "*$($case['Contains'])*")
    } else {
        $actual = (($lines | ForEach-Object { $_.TrimEnd() }) -join "`n").Trim()
        $expected = (($case.Output | ForEach-Object { $_.TrimEnd() }) -join "`n").Trim()
        $ok = $ok -and [string]::Equals($actual, $expected, [System.StringComparison]::Ordinal)
    }
    if ($ok) { Write-Host "PASS $($case.Name)" -ForegroundColor Green }
    else {
        Write-Host "FAIL $($case.Name) (exit=$exit)" -ForegroundColor Red
        $lines | ForEach-Object { Write-Host "  $_" }
        $failed++
    }
}

if ($failed) { Write-Host "$failed test(s) failed"; exit 1 }
Write-Host "All $($cases.Count) tests passed"
