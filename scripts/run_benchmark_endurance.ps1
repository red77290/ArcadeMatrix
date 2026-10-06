# ==============================================================================
# ArcadeMatrix - Run 24h Endurance Benchmark Harness (PowerShell)
# ==============================================================================
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ScriptArgs
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$VenvDir = Join-Path $ProjectRoot ".venv"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"

# 1. Setup virtual environment if absent
if (-not (Test-Path $VenvPython)) {
    Write-Host "[ArcadeMatrix] Creating virtual environment in $VenvDir..."
    python -m venv $VenvDir
}

# 2. Upgrade pip and install requirements
& $VenvPython -m pip install --quiet --upgrade pip
& $VenvPython -m pip install --quiet -r (Join-Path $ScriptDir "requirements.txt")

# 3. Execute benchmark
$BenchmarkScript = Join-Path $ScriptDir "benchmark_endurance.py"
& $VenvPython $BenchmarkScript $ScriptArgs
