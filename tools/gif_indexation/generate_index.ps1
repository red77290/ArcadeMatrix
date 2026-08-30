# generate_index.ps1
# Scans subdirectories of your SD card's gifs\ (YOKO) and gifs_tate\ (TATE) folders
# for .gif and .raw files and generates the playlists.json manifest the ESP32 Web UI needs.
# Usage: .\generate_index.ps1 -Path <sd_card_root_or_gifs_folder>
param (
    [Parameter(Mandatory=$true)]
    [string]$Path
)

if (-Not (Test-Path $Path)) {
    Write-Host "Directory $Path does not exist. Pass your SD card root or its gifs\ folder." -ForegroundColor Red
    exit 1
}

function Index-GifDirectory([string]$dirPath) {
    if (-Not (Test-Path $dirPath)) { return }
    $folderBase = Split-Path -Path $dirPath -Leaf
    Write-Host "`n--- Indexing $folderBase ($dirPath) ---" -ForegroundColor Cyan

    $playlists = @{}
    $totalFiles = 0

    Get-ChildItem -Path $dirPath -Directory | ForEach-Object {
        $folderName = $_.Name
        $files = Get-ChildItem -Path $_.FullName -File | Where-Object { $_.Extension -match "\.(gif|raw)$" } | Select-Object -ExpandProperty Name
        
        if ($files.Count -gt 0) {
            $playlists[$folderName] = @{
                "path" = "/$folderBase/$folderName"
                "files" = $files
            }
            $totalFiles += $files.Count

            # Also create index.txt inside the folder for O(1) random access
            $indexTxt = Join-Path -Path $_.FullName -ChildPath "index.txt"
            $files | Out-File -FilePath $indexTxt -Encoding ascii

            Write-Host "  [OK] Found $($files.Count) animations in '$folderName'"
        }
    }

    $jsonPath = Join-Path -Path $dirPath -ChildPath "playlists.json"
    $playlists | ConvertTo-Json -Depth 3 | Out-File -FilePath $jsonPath -Encoding utf8
    Write-Host "Done! Successfully created $jsonPath ($($playlists.Count) folders, $totalFiles animations)" -ForegroundColor Green
}

$gifsSubPath = Join-Path -Path $Path -ChildPath "gifs"
$gifsTateSubPath = Join-Path -Path $Path -ChildPath "gifs_tate"

if ((Test-Path $gifsSubPath) -or (Test-Path $gifsTateSubPath)) {
    Index-GifDirectory $gifsSubPath
    Index-GifDirectory $gifsTateSubPath
} else {
    Index-GifDirectory $Path
}
