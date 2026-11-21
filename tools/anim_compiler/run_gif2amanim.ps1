# ==============================================================================
# ArcadeMatrix - Run gif2amanim Animation Compiler (PowerShell)
# Works both from a full repository checkout and from standalone ArcadeMatrix-tools.zip
# ==============================================================================
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ScriptArgs = @()
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptDir)

$LocalReq = Join-Path $ScriptDir "requirements.txt"
$RepoReq = Join-Path $ProjectRoot "scripts\requirements.txt"
$ReqFile = if (Test-Path -LiteralPath $LocalReq) {
    $LocalReq
} elseif (Test-Path -LiteralPath $RepoReq) {
    $RepoReq
} else {
    Write-Host "[ERROR] Missing requirements.txt in $ScriptDir" -ForegroundColor Red
    exit 1
}

$RepoVenv = Join-Path $ProjectRoot ".venv"
$VenvDir = if (Test-Path -LiteralPath $RepoVenv) { $RepoVenv } else { Join-Path $ScriptDir "venv" }

function Get-VenvPythonPath([string]$BaseDir) {
    $winPy = Join-Path $BaseDir "Scripts\python.exe"
    if (Test-Path -LiteralPath $winPy) { return $winPy }
    $unixPy = Join-Path $BaseDir "bin/python"
    if (Test-Path -LiteralPath $unixPy) { return $unixPy }
    return $null
}

$VenvPython = Get-VenvPythonPath $VenvDir

# 1. Setup virtual environment if absent
if ($null -eq $VenvPython) {
    Write-Host "[ArcadeMatrix] Creating virtual environment in $VenvDir..."
    $pyCmd = if (Get-Command python -ErrorAction SilentlyContinue) { "python" } elseif (Get-Command python3 -ErrorAction SilentlyContinue) { "python3" } else { $null }
    if ($null -eq $pyCmd) {
        Write-Host "[ERROR] Python 3 is not installed or not on PATH." -ForegroundColor Red
        exit 1
    }
    & $pyCmd -m venv --system-site-packages $VenvDir
    $VenvPython = Get-VenvPythonPath $VenvDir
    if ($LASTEXITCODE -ne 0 -or ($null -eq $VenvPython)) {
        Write-Host "[ERROR] Failed to create Python virtual environment in $VenvDir." -ForegroundColor Red
        exit 1
    }
}

# 2. Install requirements if Pillow is missing
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $VenvPython -c "import PIL" *> $null
$hasPil = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $prevEap

if (-not $hasPil) {
    & $VenvPython -m pip install --quiet --upgrade pip
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $VenvPython -m pip install --quiet -r $ReqFile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# 3. Execute compiler
$CompilerScript = Join-Path $ScriptDir "gif2amanim.py"
& $VenvPython $CompilerScript @ScriptArgs
exit $LASTEXITCODE
