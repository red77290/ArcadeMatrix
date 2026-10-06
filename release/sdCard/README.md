# ArcadeMatrix - Sample SD Card

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

This folder is a ready-to-use starting point for your SD card: copy its contents to the root of
a **FAT32**-formatted SD card, edit the modular domain files in `config/` (or legacy `config.json`), and you're ready to boot.

```
sdCard/
  ├─ config/                <- modular domain settings (v4.0+)
  │    ├─ hardware.json     <- panel dimensions, color depth, driver chip, pin settings
  │    ├─ system.json       <- timezone, night mode, brightness, language
  │    ├─ network.json      <- Wi-Fi credentials and MQTT settings
  │    ├─ playlist.json     <- engine rotation sequence and intervals
  │    └─ instances/        <- individual engine configurations (*.json)
  ├─ config.json            <- legacy monolithic fallback (auto-migrated to config/ if present)
  ├─ gifs/                  <- sample GIF playlist manifest (see docs/... or gif_indexation/ below)
  ├─ gifs_tate/             <- vertical (Tate) library (e.g. 32x128 sample for rotated panels)
  ├─ fighters_32/           <- sample MUGEN sprite export for 32px-tall matrices
  ├─ fighters_64/           <- sample MUGEN sprite export for 64px-tall matrices
  ├─ fonts/                 <- custom bitmap fonts
  └─ gif_indexation/        <- PC-side tool, NOT required on the SD card itself (see below)
```

## About `gif_indexation/`
This subfolder is a convenience copy of `tools/gif_indexation/` from the main repository - the
scripts that regenerate `gifs/playlists.json` after you add/remove GIF folders. **They run on
your computer (macOS/Linux/Windows), not on the ESP32**, so you don't strictly need to copy this
subfolder onto the SD card - it's bundled here purely so you have everything in one download
without needing to clone the full source repository. See `gif_indexation/README.md` for usage.

---
*For the full setup guide, see the main repository's `docs/GETTING_STARTED.md`.*
