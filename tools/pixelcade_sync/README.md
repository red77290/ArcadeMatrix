# pixelcade_sync

One-shot PC-side tool that downloads the [Pixelcade](https://github.com/alinke/pixelcade) marquee
artwork repository and lays it out ready to copy onto your ArcadeMatrix SD card, so the ESP32
never needs live internet access to display box-art/marquees during gameplay.

## Why not fetch it live from the ESP32?

`ArcadeMatrix_RPi` (the Raspberry Pi sibling project) downloads images from Pixelcade on demand
(`core/dmd_cache.py`), caching them to disk after the first fetch. The ESP32 can't reasonably do
the same:
- No spare flash/RAM budget for an HTTPS+TLS client fetching images mid-game, alongside the DMA
  matrix driver.
- No filesystem cache big enough to grow unbounded, and no cache-eviction logic worth the
  complexity for a microcontroller.

Instead, `RetroFrontendListener` on the ESP32 expects artwork to already be present at
`/pixelcade/<system>/<game>.png` on the SD card - this script populates that folder once (offline,
on a real PC), you copy it to the SD card, and the firmware just does a fast `SD.exists()` +
`gif->playGif()` at runtime. No network round-trip at all when a game launches.

## Usage

```bash
# Sync every system Pixelcade has artwork for (large - several hundred MB)
python3 pixelcade_sync.py

# Only sync the systems you actually use (recommended - much faster/smaller)
python3 pixelcade_sync.py --systems mame,snes,nes,gba

# Custom output location
python3 pixelcade_sync.py --systems mame --dest ./sdcard/pixelcade
```

No third-party dependencies - stdlib only (`urllib` + `zipfile`), consistent with
`tools/bdf_to_amfont/`.

## Applying to your SD card

Copy the contents of the output folder to the root of your ArcadeMatrix SD card, so you end up
with paths like:

```
/pixelcade/mame/pacman.png
/pixelcade/snes/super_mario_world.png
```

The system folder names (`mame`, `snes`, `nes`, ...) match Pixelcade's own repository layout, and
`RetroFrontendListener::mapSystemToPixelcadeFolder()` (firmware side) maps Recalbox/Batocera
`SystemId` values (e.g. `fbneo`, `megadrive`) to these same folder names - kept in sync with
`ArcadeMatrix_RPi/core/dmd_cache.py`'s `SYSTEM_MAP`. If you add a system there, mirror the change
in both places.

## Re-running / keeping artwork up to date

Pixelcade's repository grows over time as more games get artwork. Just re-run this script
periodically (it re-downloads the full snapshot each time - there's no incremental/diff mode) and
re-copy the output to your SD card to pick up new games.
