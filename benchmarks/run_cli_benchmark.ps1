param(
    [string]$Umbrly = "$PSScriptRoot\..\umbrly.exe",
    [string]$Python = "",
    [int]$Runs = 5,
    [int]$Warmups = 1
)

$ErrorActionPreference = "Stop"
if (!$Python) {
    $found = Get-Command python -ErrorAction SilentlyContinue
    if ($found) { $Python = $found.Source }
    else {
        $Python = "C:\Users\Ramix\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
    }
}
if (!(Test-Path $Umbrly)) { throw "Umbrly executable not found: $Umbrly" }
if (!(Test-Path $Python)) { throw "Python executable not found: $Python" }
if ($Runs -lt 3) { throw "Use at least 3 measured runs" }

function Invoke-Measured([string]$exe, [string]$script) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    $start.Arguments = '"' + $script.Replace('"', '\"') + '"'
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.CreateNoWindow = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd().Trim()
    $stderr = $process.StandardError.ReadToEnd().Trim()
    $process.WaitForExit()
    $watch.Stop()
    if ($process.ExitCode -ne 0) { throw "$exe failed for $script`n$stderr" }
    return @{ Milliseconds = $watch.Elapsed.TotalMilliseconds; Output = $stdout }
}

function Get-Median([double[]]$values) {
    $sorted = @($values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2) { return $sorted[$middle] }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
}

$cases = @("int_loop", "fibonacci", "strings", "arrays")
$rows = @()
Write-Host "Umbrly interpreter: $Umbrly"
Write-Host "Python interpreter: $Python"
Write-Host "Runs: $Runs measured + $Warmups warmup(s); full CLI wall time"

foreach ($name in $cases) {
    $umb = Join-Path $PSScriptRoot "cli\$name.umb"
    $py = Join-Path $PSScriptRoot "cli\$name.py"

    for ($i = 0; $i -lt $Warmups; $i++) {
        [void](Invoke-Measured $Umbrly $umb)
        [void](Invoke-Measured $Python $py)
    }

    [double[]]$umbTimes = @()
    [double[]]$pyTimes = @()
    $expected = $null
    for ($i = 0; $i -lt $Runs; $i++) {
        $u = Invoke-Measured $Umbrly $umb
        $p = Invoke-Measured $Python $py
        if ($u.Output -ne $p.Output) {
            throw "Output mismatch in $name`nUmbrly: $($u.Output)`nPython: $($p.Output)"
        }
        $expected = $u.Output
        $umbTimes += $u.Milliseconds
        $pyTimes += $p.Milliseconds
    }

    $umbMedian = Get-Median $umbTimes
    $pyMedian = Get-Median $pyTimes
    $speedup = $pyMedian / $umbMedian
    $rows += [pscustomobject]@{
        Benchmark = $name
        UmbrlyMs = [Math]::Round($umbMedian, 3)
        PythonMs = [Math]::Round($pyMedian, 3)
        PythonOverUmbrly = [Math]::Round($speedup, 2)
        Checksum = $expected
    }
}

$rows | Format-Table -AutoSize
$geomean = [Math]::Exp((($rows | ForEach-Object { [Math]::Log($_.PythonOverUmbrly) } | Measure-Object -Average).Average))
Write-Host ("Geometric mean Python/Umbrly: {0:N2}x" -f $geomean)
Write-Host "Values above 1.00 mean Umbrly was faster; below 1.00 mean Python was faster."
