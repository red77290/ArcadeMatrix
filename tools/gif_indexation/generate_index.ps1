# generate_index.ps1
# Scans subdirectories of your SD card's gifs\ folder for .gif and .raw files
# and generates the playlists.json manifest the ESP32 Web UI needs to let you
# pick which folders to play (WebServerAPI.cpp reads it from /gifs/playlists.json).
# Usage: .\generate_index.ps1 -Path <sd_card_root_or_gifs_folder>
param (
    [Parameter(Mandatory=$true)]
    [string]$Path
)

if (-Not (Test-Path $Path)) {
    Write-Host "Directory $Path does not exist. Pass your SD card root or its gifs\ folder." -ForegroundColor Red
    exit 1
}

# Accept either the SD root (auto-descend into .\gifs) or the gifs folder itself.
if ((Split-Path -Path $Path -Leaf) -ne "gifs") {
    $gifsSubPath = Join-Path -Path $Path -ChildPath "gifs"
    if (Test-Path $gifsSubPath) {
        $Path = $gifsSubPath
    }
}

$playlists = @{}
$totalFiles = 0

Get-ChildItem -Path $Path -Directory | ForEach-Object {
    $folderName = $_.Name
    $files = Get-ChildItem -Path $_.FullName -File | Where-Object { $_.Extension -match "\.(gif|raw)$" } | Select-Object -ExpandProperty Name
    
    if ($files.Count -gt 0) {
        # Create the object for playlists.json
        $playlists[$folderName] = @{
            "path" = "/gifs/$folderName"
            "files" = $files
        }
        $totalFiles += $files.Count
        Write-Host "[OK] Found $($files.Count) animations in '$folderName'"
    }
}

$jsonPath = Join-Path -Path $Path -ChildPath "playlists.json"
# Use ConvertTo-Json and ensure utf8 without BOM
$playlists | ConvertTo-Json -Depth 3 | Out-File -FilePath $jsonPath -Encoding utf8
Write-Host "`nDone! Successfully created $jsonPath" -ForegroundColor Green
Write-Host "Total: $($playlists.Count) folders and $totalFiles animations indexed."
