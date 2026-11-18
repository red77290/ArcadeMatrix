# generate_index.ps1
# Scans subdirectories of your SD card's gifs\ (YOKO) and gifs_tate\ (TATE) folders
# for .gif, .raw, and .png files and generates the playlists.json manifest the ESP32 Web UI needs.
# Usage: .\generate_index.ps1 -Path <sd_card_root_or_gifs_folder>
param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = "Stop"

$CleanInput = $Path.Trim().Trim('"').Trim("'")
if ([string]::IsNullOrWhiteSpace($CleanInput) -or (-not (Test-Path -LiteralPath $CleanInput))) {
    Write-Host "Directory '$Path' does not exist. Pass your SD card root or its gifs\ folder." -ForegroundColor Red
    exit 1
}

$ResolvedRoot = (Get-Item -LiteralPath $CleanInput).FullName
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Index-GifDirectory([string]$dirPath) {
    if (-not (Test-Path -LiteralPath $dirPath)) { return }
    $folderBase = Split-Path -Path $dirPath -Leaf
    Write-Host ""
    Write-Host "--- Indexing $folderBase ($dirPath) ---" -ForegroundColor Cyan

    $playlists = [ordered]@{}
    $totalFiles = 0

    $subDirs = @(
        Get-ChildItem -LiteralPath $dirPath -Directory |
            Where-Object { (-not $_.Name.StartsWith(".")) -and ($_.Name -ne "System Volume Information") } |
            Sort-Object Name
    )
    foreach ($dir in $subDirs) {
        $folderName = $dir.Name
        $files = @(
            Get-ChildItem -LiteralPath $dir.FullName -File |
                Where-Object { (-not $_.Name.StartsWith(".")) -and ($_.Extension -match '^\.(gif|raw|png)$') } |
                Sort-Object Name |
                Select-Object -ExpandProperty Name
        )

        if ($files.Count -gt 0) {
            $playlists[$folderName] = [ordered]@{
                "path"  = "/$folderBase/$folderName"
                "count" = $files.Count
            }
            $totalFiles += $files.Count

            # Write index.txt as UTF-8 without BOM + LF newlines for O(1) random access on ESP32
            $indexTxt = Join-Path -Path $dir.FullName -ChildPath "index.txt"
            $indexBody = ($files -join "`n") + "`n"
            [System.IO.File]::WriteAllText($indexTxt, $indexBody, $Utf8NoBom)

            Write-Host "  [OK] Found $($files.Count) animations in '$folderName'"
        }
    }

    $jsonPath = Join-Path -Path $dirPath -ChildPath "playlists.json"
    $jsonBody = ($playlists | ConvertTo-Json -Depth 3).Replace("`r`n", "`n") + "`n"
    [System.IO.File]::WriteAllText($jsonPath, $jsonBody, $Utf8NoBom)
    Write-Host "Done! Successfully created $jsonPath ($($playlists.Count) folders, $totalFiles animations)" -ForegroundColor Green
}

$gifsSubPath = Join-Path -Path $ResolvedRoot -ChildPath "gifs"
$gifsTateSubPath = Join-Path -Path $ResolvedRoot -ChildPath "gifs_tate"

if ((Test-Path -LiteralPath $gifsSubPath) -or (Test-Path -LiteralPath $gifsTateSubPath)) {
    Index-GifDirectory $gifsSubPath
    Index-GifDirectory $gifsTateSubPath
} else {
    Index-GifDirectory $ResolvedRoot
}
