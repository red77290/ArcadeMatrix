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
# Requires only what ships built-in with Windows 10/11: PowerShell 5+ and .NET ZipFile.
# No Python, no third-party installs needed.

param(
    [string]$Dest = ".\pixelcade",
    [string[]]$Systems = @(),
    [string]$ZipPath = ""
)

$ErrorActionPreference = "Stop"

$PixelcadeZipUrl = "https://github.com/red77290/pixelcade/archive/refs/heads/master.zip"
$DefaultRootPrefix = "pixelcade-master"
$SkipDirs = @(".git", ".github", "scripts", "docs", "_catalog")
$ValidExtensions = @(".png", ".gif", ".jpg", ".jpeg")

# --- Prerequisite check (explicit, so a non-technical user knows exactly what's needed) ---
$psVersion = $PSVersionTable.PSVersion.Major
if ($psVersion -lt 5) {
    Write-Host "ERROR: PowerShell 5.0 or newer is required (found $($PSVersionTable.PSVersion))." -ForegroundColor Red
    Write-Host "Update via Windows Update, or install PowerShell 7+ from https://aka.ms/powershell" -ForegroundColor Red
    exit 1
}

try {
    Add-Type -AssemblyName System.IO.Compression -ErrorAction Stop
    Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop
} catch {
    Write-Host "ERROR: Required .NET System.IO.Compression assembly could not be loaded: $_" -ForegroundColor Red
    exit 1
}

# Ensure TLS 1.2 is enabled on Windows 10 PowerShell 5.1 (.NET Framework 4.x)
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch {
    # Ignore if platform manages TLS versions automatically (.NET Core / PS 7+)
}

# Resolve destination literally via .NET so paths containing '[' or ']' never trigger PowerShell wildcard errors
if ([string]::IsNullOrWhiteSpace($Dest)) {
    $Dest = ".\pixelcade"
}
$DestClean = $Dest.Trim().Trim('"').Trim("'")
if ([System.IO.Path]::IsPathRooted($DestClean)) {
    $DestFullPath = [System.IO.Path]::GetFullPath($DestClean)
} else {
    $DestFullPath = [System.IO.Path]::GetFullPath((Join-Path $PWD.ProviderPath $DestClean))
}
[void][System.IO.Directory]::CreateDirectory($DestFullPath)

$WorkDir = $null
$ZipFile = $null
$zipArchive = $null

try {
    if (-not [string]::IsNullOrWhiteSpace($ZipPath)) {
        $ZipClean = $ZipPath.Trim().Trim('"').Trim("'")
        $ZipFile = if ([System.IO.Path]::IsPathRooted($ZipClean)) {
            [System.IO.Path]::GetFullPath($ZipClean)
        } else {
            [System.IO.Path]::GetFullPath((Join-Path $PWD.ProviderPath $ZipClean))
        }
        if (-not [System.IO.File]::Exists($ZipFile)) {
            Write-Host "ERROR: Specified ZipPath does not exist: $ZipFile" -ForegroundColor Red
            exit 1
        }
        Write-Host "Using local Pixelcade archive: $ZipFile"
    } else {
        $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) "am_px_$([guid]::NewGuid().ToString('N').Substring(0, 8))"
        [void][System.IO.Directory]::CreateDirectory($WorkDir)
        $ZipFile = Join-Path $WorkDir "pixelcade.zip"

        Write-Host "Downloading Pixelcade repository snapshot from $PixelcadeZipUrl ..."
        $prevProgress = $ProgressPreference
        try {
            # Suppress PowerShell 5.1 per-chunk progress rendering (speeds up multi-hundred-MB downloads 20x+)
            # and pass -UseBasicParsing so clean Windows installs without legacy IE DOM never fail.
            $ProgressPreference = "SilentlyContinue"
            Invoke-WebRequest -Uri $PixelcadeZipUrl -OutFile $ZipFile -UserAgent "ArcadeMatrix-pixelcade-sync" -UseBasicParsing
        } catch {
            Write-Host "ERROR: failed to download Pixelcade repository: $_" -ForegroundColor Red
            Write-Host "Check your internet connection and that github.com is reachable (proxy/firewall?)." -ForegroundColor Red
            exit 1
        } finally {
            $ProgressPreference = $prevProgress
        }

        $sizeMb = [math]::Round(([System.IO.FileInfo]::new($ZipFile)).Length / 1MB, 1)
        Write-Host "Downloaded $sizeMb MB."
    }

    Write-Host "Indexing archive entries ..."
    $zipArchive = [System.IO.Compression.ZipFile]::OpenRead($ZipFile)

    # Detect archive top-level folder prefix (e.g. "pixelcade-master/")
    $rootPrefix = $null
    foreach ($entry in $zipArchive.Entries) {
        $norm = $entry.FullName.Replace('\', '/')
        $slash = $norm.IndexOf('/')
        if ($slash -gt 0) {
            $candidate = $norm.Substring(0, $slash)
            if ($candidate -eq $DefaultRootPrefix) {
                $rootPrefix = $candidate
                break
            }
            if ($null -eq $rootPrefix) {
                $rootPrefix = $candidate
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($rootPrefix)) {
        Write-Host "ERROR: unexpected archive layout, '$DefaultRootPrefix' folder not found in archive." -ForegroundColor Red
        exit 1
    }

    $skipSet = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($s in $SkipDirs) { [void]$skipSet.Add($s) }

    $extSet = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($e in $ValidExtensions) { [void]$extSet.Add($e) }

    # Case-insensitive lookup: lowerSystemName -> canonical folder name in archive
    $availableSystems = New-Object 'System.Collections.Generic.Dictionary[string,string]' ([System.StringComparer]::OrdinalIgnoreCase)
    # canonicalSystemName -> List[ZipArchiveEntry] of direct child artwork files
    $artworkBySystem = New-Object 'System.Collections.Generic.Dictionary[string,System.Collections.Generic.List[System.IO.Compression.ZipArchiveEntry]]' ([System.StringComparer]::OrdinalIgnoreCase)
    # fileName -> ZipArchiveEntry inside _catalog/
    $catalogEntries = New-Object 'System.Collections.Generic.Dictionary[string,System.IO.Compression.ZipArchiveEntry]' ([System.StringComparer]::OrdinalIgnoreCase)

    $prefixWithSlash = "$rootPrefix/"
    foreach ($entry in $zipArchive.Entries) {
        $norm = $entry.FullName.Replace('\', '/')
        if (-not $norm.StartsWith($prefixWithSlash, [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        $rel = $norm.Substring($prefixWithSlash.Length)
        if ([string]::IsNullOrEmpty($rel)) { continue }

        $parts = $rel.Split('/')
        $topDir = $parts[0]
        if ([string]::IsNullOrEmpty($topDir)) { continue }
        if (($topDir -eq ".") -or ($topDir -eq "..") -or $topDir.Contains("..") -or ($topDir.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -ge 0)) {
            continue
        }

        if ($topDir.Equals("_catalog", [System.StringComparison]::OrdinalIgnoreCase)) {
            if ($parts.Length -eq 2 -and (-not [string]::IsNullOrEmpty($parts[1]))) {
                $catLeaf = $parts[1]
                if (($catLeaf -ne ".") -and ($catLeaf -ne "..") -and (-not $catLeaf.Contains("..")) -and ($catLeaf.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -lt 0)) {
                    $catalogEntries[$catLeaf] = $entry
                }
            }
            continue
        }

        if ($skipSet.Contains($topDir)) {
            continue
        }

        # Register directory if it appears either as an explicit folder entry ("snes/") or has files under it ("snes/foo.png")
        if ($parts.Length -ge 2) {
            if (-not $availableSystems.ContainsKey($topDir)) {
                $availableSystems[$topDir] = $topDir
                $artworkBySystem[$topDir] = New-Object 'System.Collections.Generic.List[System.IO.Compression.ZipArchiveEntry]'
            }
        }

        # Match maxdepth 1 artwork files directly under <system>/<file> (same as pixelcade_sync.sh)
        if ($parts.Length -eq 2 -and (-not [string]::IsNullOrEmpty($parts[1]))) {
            $fileName = $parts[1]
            if (($fileName -eq ".") -or ($fileName -eq "..") -or $fileName.Contains("..") -or ($fileName.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -ge 0)) {
                continue
            }
            $ext = [System.IO.Path]::GetExtension($fileName)
            if (-not [string]::IsNullOrEmpty($ext) -and $extSet.Contains($ext)) {
                $canonical = $availableSystems[$topDir]
                $artworkBySystem[$canonical].Add($entry)
            }
        }
    }

    if ($availableSystems.Count -eq 0) {
        Write-Host "ERROR: no artwork system folders found under '$rootPrefix/' in archive." -ForegroundColor Red
        exit 1
    }

    $selectedSystems = New-Object 'System.Collections.Generic.List[string]'
    $seenSelected = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)

    if ($Systems.Count -gt 0) {
        $requestedRaw = New-Object 'System.Collections.Generic.List[string]'
        foreach ($item in $Systems) {
            if ($null -eq $item) { continue }
            foreach ($part in ($item -split ',')) {
                $trimmed = $part.Trim()
                if ($trimmed.Length -gt 0 -and $seenSelected.Add($trimmed)) {
                    $requestedRaw.Add($trimmed)
                }
            }
        }
        if ($availableSystems.ContainsKey("console") -and $seenSelected.Add("console")) {
            $requestedRaw.Add("console")
        }
        Write-Host "Filtering to systems: $($requestedRaw -join ', ')"
        foreach ($req in $requestedRaw) {
            if ($availableSystems.ContainsKey($req)) {
                $selectedSystems.Add($availableSystems[$req])
            } else {
                Write-Host "WARNING: no such system folder in Pixelcade repo: $req (skipped)" -ForegroundColor Yellow
            }
        }
    } else {
        $allNames = @($availableSystems.Values) | Sort-Object
        foreach ($sysName in $allNames) {
            $selectedSystems.Add($sysName)
        }
    }

    Write-Host "Extracting artwork directly to $DestFullPath ..."
    $destRootPrefix = $DestFullPath.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $copied = 0
    foreach ($system in $selectedSystems) {
        $destSysDir = [System.IO.Path]::GetFullPath((Join-Path $DestFullPath $system))
        if (-not $destSysDir.StartsWith($destRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "ERROR: refusing unsafe archive path outside destination: $destSysDir" -ForegroundColor Red
            exit 1
        }
        [void][System.IO.Directory]::CreateDirectory($destSysDir)

        $entries = $artworkBySystem[$system]
        foreach ($entry in $entries) {
            $norm = $entry.FullName.Replace('\', '/')
            $leafName = $norm.Substring($norm.LastIndexOf('/') + 1)
            $targetFile = [System.IO.Path]::GetFullPath((Join-Path $destSysDir $leafName))
            if (-not $targetFile.StartsWith($destRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                Write-Host "ERROR: refusing unsafe archive entry outside destination: $targetFile" -ForegroundColor Red
                exit 1
            }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $targetFile, $true)
            $copied++
        }
    }

    # Extract _catalog/_folders.txt and _catalog/<system>.txt for offline ESP32 catalog resolution
    $catalogCopied = 0
    if ($catalogEntries.Count -gt 0) {
        $destCatalog = [System.IO.Path]::GetFullPath((Join-Path $DestFullPath "_catalog"))
        if (-not $destCatalog.StartsWith($destRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            Write-Host "ERROR: refusing unsafe catalog path outside destination: $destCatalog" -ForegroundColor Red
            exit 1
        }
        [void][System.IO.Directory]::CreateDirectory($destCatalog)

        if ($catalogEntries.ContainsKey("_folders.txt")) {
            $foldersTarget = [System.IO.Path]::GetFullPath((Join-Path $destCatalog "_folders.txt"))
            if ($foldersTarget.StartsWith($destRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($catalogEntries["_folders.txt"], $foldersTarget, $true)
            }
        }
        foreach ($system in $selectedSystems) {
            $catName = "$system.txt"
            if ($catalogEntries.ContainsKey($catName)) {
                $catEntry = $catalogEntries[$catName]
                $normCat = $catEntry.FullName.Replace('\', '/')
                $actualCatLeaf = $normCat.Substring($normCat.LastIndexOf('/') + 1)
                $catTarget = [System.IO.Path]::GetFullPath((Join-Path $destCatalog $actualCatLeaf))
                if (-not $catTarget.StartsWith($destRootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                    Write-Host "ERROR: refusing unsafe catalog entry outside destination: $catTarget" -ForegroundColor Red
                    exit 1
                }
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($catEntry, $catTarget, $true)
                $catalogCopied++
            }
        }
    }

    Write-Host ""
    Write-Host "Done. Copied $copied artwork files (+ $catalogCopied catalog manifests) into $DestFullPath" -ForegroundColor Green
    Write-Host "Next step: copy the contents of '$DestFullPath' onto your SD card's \pixelcade folder,"
    Write-Host "so paths look like \pixelcade\mame\pacman.png on the card."
} finally {
    if ($null -ne $zipArchive) {
        $zipArchive.Dispose()
    }
    if (($null -ne $WorkDir) -and [System.IO.Directory]::Exists($WorkDir)) {
        try {
            [System.IO.Directory]::Delete($WorkDir, $true)
        } catch {
            Remove-Item -LiteralPath $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
