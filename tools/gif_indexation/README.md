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

It scans subfolders inside your SD card's **`gifs/`** (Horizontal / YOKO) and **`gifs_tate/`** (Vertical / TATE) folders:
- Each subfolder becomes one selectable playlist in the Web UI.
- Generates `index.txt` inside each subfolder for $O(1)$ fast random access by the firmware.
- Generates `playlists.json` at the root of `gifs/` and `gifs_tate/` with animation counts.

## Usage

Run the script pointing at either your SD card's **root** or a specific folder:

```bash
# macOS/Linux
./generate_index.sh /Volumes/SDCARD          # SD root - indexes both gifs/ (YOKO) and gifs_tate/ (TATE)
./generate_index.sh /Volumes/SDCARD/gifs     # or the gifs/ folder specifically
./generate_index.sh /Volumes/SDCARD/gifs_tate# or the gifs_tate/ folder specifically
```

```powershell
# Windows
.\generate_index.ps1 -Path E:\               # SD root - indexes both gifs\ (YOKO) and gifs_tate\ (TATE)
.\generate_index.ps1 -Path E:\gifs           # or the gifs\ folder specifically
.\generate_index.ps1 -Path E:\gifs_tate      # or the gifs_tate\ folder specifically
```

The script writes **`<sd_card>/gifs/playlists.json`** and **`<sd_card>/gifs_tate/playlists.json`**, and creates **`index.txt`** inside each subfolder.

**Re-run the script every time you add, remove, or rename folders or GIFs** so the Web UI and firmware index stay in sync with what's on the SD card.

---
*This tool is open source and designed for the ArcadeMatrix ecosystem.*
