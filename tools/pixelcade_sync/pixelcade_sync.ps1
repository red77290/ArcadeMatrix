# pixelcade_sync.ps1 - one-shot PC-side tool (Windows) to pre-cache Pixelcade marquee artwork
# into a folder you then copy onto the ArcadeMatrix SD card, instead of the ESP32 fetching images
# live from GitHub on every game launch (it has no spare flash/RAM/CPU budget for that - see
# docs/DEVELOPER.md's "Pixelcade-style marquee/box-art integration" section).
#
# Usage:
#   .\pixelcade_sync.ps1                              # sync every system Pixelcade has art for
#   .\pixelcade_sync.ps1 -Systems mame,snes,nes       # only sync the systems you actually use
#   .\pixelcade_sync.ps1 -Dest D:\sdcard\pixelcade -Systems mame
#
# Then copy the resulting folder onto the root of your SD card, so you end up with paths like
# \pixelcade\mame\pacman.png on the card.
#
# Requires only what's built into Windows 10/11: PowerShell 5+, Invoke-WebRequest, Expand-Archive.
# No Python, no third-party installs needed.

param(
    [string]$Dest = ".\pixelcade",
    [string[]]$Systems = @()
)

$ErrorActionPreference = "Stop"

$PixelcadeZipUrl = "https://github.com/alinke/pixelcade/archive/refs/heads/master.zip"
$ZipRootPrefix = "pixelcade-master"
$SkipDirs = @(".git", ".github", "scripts", "docs")
$ValidExtensions = @(".png", ".gif", ".jpg", ".jpeg")

# --- Prerequisite check (explicit, so a non-technical user knows exactly what's needed) ---
$psVersion = $PSVersionTable.PSVersion.Major
if ($psVersion -lt 5) {
    Write-Host "ERROR: PowerShell 5.0 or newer is required (found $($PSVersionTable.PSVersion))." -ForegroundColor Red
    Write-Host "Update via Windows Update, or install PowerShell 7+ from https://aka.ms/powershell" -ForegroundColor Red
    exit 1
}
if (-not (Get-Command Expand-Archive -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: 'Expand-Archive' cmdlet not found. It ships with PowerShell 5+ on Windows 10/11." -ForegroundColor Red
    Write-Host "If you're on an older Windows version, install the 'Microsoft.PowerShell.Archive' module:" -ForegroundColor Red
    Write-Host "  Install-Module Microsoft.PowerShell.Archive -Scope CurrentUser" -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $Dest | Out-Null
$WorkDir = Join-Path $env:TEMP "pixelcade_sync_$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

try {
    $ZipFile = Join-Path $WorkDir "pixelcade.zip"
    Write-Host "Downloading Pixelcade repository snapshot from $PixelcadeZipUrl ..."
    try {
        Invoke-WebRequest -Uri $PixelcadeZipUrl -OutFile $ZipFile -UserAgent "ArcadeMatrix-pixelcade-sync"
    } catch {
        Write-Host "ERROR: failed to download Pixelcade repository: $_" -ForegroundColor Red
        Write-Host "Check your internet connection and that github.com is reachable (proxy/firewall?)." -ForegroundColor Red
        exit 1
    }
    $sizeMb = [math]::Round((Get-Item $ZipFile).Length / 1MB, 1)
    Write-Host "Downloaded $sizeMb MB."

    Write-Host "Extracting ..."
    $ExtractDir = Join-Path $WorkDir "extracted"
    Expand-Archive -Path $ZipFile -DestinationPath $ExtractDir -Force

    $SrcRoot = Join-Path $ExtractDir $ZipRootPrefix
    if (-not (Test-Path $SrcRoot)) {
        Write-Host "ERROR: unexpected archive layout, '$ZipRootPrefix' folder not found after extraction." -ForegroundColor Red
        exit 1
    }

    if ($Systems.Count -gt 0) {
        Write-Host "Filtering to systems: $($Systems -join ', ')"
        $systemDirs = $Systems
    } else {
        $systemDirs = Get-ChildItem -Path $SrcRoot -Directory | Where-Object { $SkipDirs -notcontains $_.Name } | ForEach-Object { $_.Name }
    }

    $copied = 0
    foreach ($system in $systemDirs) {
        $system = $system.Trim()
        if (-not $system) { continue }
        $srcDir = Join-Path $SrcRoot $system
        if (-not (Test-Path $srcDir)) {
            Write-Host "WARNING: no such system folder in Pixelcade repo: $system (skipped)" -ForegroundColor Yellow
            continue
        }
        $destDir = Join-Path $Dest $system
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null

        $files = Get-ChildItem -Path $srcDir -File | Where-Object { $ValidExtensions -contains $_.Extension.ToLower() }
        foreach ($f in $files) {
            Copy-Item -Path $f.FullName -Destination $destDir -Force
            $copied++
        }
    }

    Write-Host ""
    Write-Host "Done. Copied $copied artwork files into $((Resolve-Path $Dest).Path)" -ForegroundColor Green
    Write-Host "Next step: copy the contents of '$Dest' onto your SD card's \pixelcade folder,"
    Write-Host "so paths look like \pixelcade\mame\pacman.png on the card."
} finally {
    Remove-Item -Recurse -Force -Path $WorkDir -ErrorAction SilentlyContinue
}
