# ArcadeMatrix GIF Playlist Indexer

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

This tool generates the `playlists.json` manifest the ESP32 Web UI needs to let you
tick/untick which `gifs/` subfolders ("playlists") should be included in the idle rotation.
It is **only needed for that Web UI checkbox list** - GIF playback itself works without it,
since the `GifEngine` always reads the actual `.gif`/`.png`/`.raw` files directly from the SD
card at runtime.

Two native scripts are provided - **no Python required**:
- `generate_index.sh` (macOS/Linux, pure Bash)
- `generate_index.ps1` (Windows, pure PowerShell)

## What it does

It scans one level of subfolders inside your SD card's `gifs/` folder. Each subfolder becomes
one selectable "playlist" in the Web UI. For example:

```text
gifs/
  ├── mario.gif          <- always played, not a playlist (loose files at the gifs/ root)
  ├── mario/
  │   ├── walk.gif
  │   └── jump.gif
  └── sonic/
      └── run.gif
```

Here, `mario/` and `sonic/` become two playlists you can enable/disable from the Web UI.

## Usage

Run the script pointing at either your SD card's **root** or its **`gifs/` folder directly**
- both work, the script auto-detects which one you gave it:

```bash
# macOS/Linux
./generate_index.sh /Volumes/SDCARD          # SD root - auto-descends into gifs/
./generate_index.sh /Volumes/SDCARD/gifs     # or the gifs/ folder itself
```

```powershell
# Windows
.\generate_index.ps1 -Path E:\               # SD root - auto-descends into gifs\
.\generate_index.ps1 -Path E:\gifs           # or the gifs\ folder itself
```

The script always writes the result to **`<sd_card>/gifs/playlists.json`**, which is the exact
path the firmware expects (`WebServerAPI.cpp` serves `/api/playlists` by reading
`/gifs/playlists.json` from the SD card). If that file is missing or stale, the Web UI's
playlist selector will simply show nothing to pick from (GIF playback is unaffected either way).

**Re-run the script every time you add, remove, or rename a folder inside `gifs/`** so the Web
UI stays in sync with what's actually on the SD card.

---
*This tool is open source and designed for the ArcadeMatrix ecosystem.*
