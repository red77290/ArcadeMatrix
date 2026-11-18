# ==============================================================================
# ArcadeMatrix - Run STRESS-06 Torture Test Suite (PowerShell)
# ==============================================================================
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ScriptArgs = @()
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = Split-Path -Parent $ScriptDir
$VenvDir = Join-Path $ProjectRoot ".venv"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"

# 1. Setup virtual environment if absent
if (-not (Test-Path -LiteralPath $VenvPython)) {
    Write-Host "[ArcadeMatrix] Creating virtual environment in $VenvDir..."
    python -m venv $VenvDir
    if ($LASTEXITCODE -ne 0 -or (-not (Test-Path -LiteralPath $VenvPython))) {
        Write-Host "[ERROR] Failed to create Python virtual environment. Ensure Python 3 is installed and on PATH." -ForegroundColor Red
        exit 1
    }
}

# 2. Upgrade pip and install requirements
& $VenvPython -m pip install --quiet --upgrade pip
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $VenvPython -m pip install --quiet -r (Join-Path $ScriptDir "requirements.txt")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# 3. Execute torture test
$TortureScript = Join-Path $ScriptDir "torture_test.py"
& $VenvPython $TortureScript @ScriptArgs
exit $LASTEXITCODE
