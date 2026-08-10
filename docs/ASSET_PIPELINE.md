# ArcadeMatrix Asset Preprocessing Pipeline

This document describes the offline asset optimization pipeline for ArcadeMatrix ESP32.

---

## 💡 Why Preprocess Assets Offline?

The ESP32 is a high-performance microcontroller, but its RAM and CPU clock are limited compared to a PC or Raspberry Pi.

Rather than making the ESP32 decode heavy image formats or complex animation scripts in real time, ArcadeMatrix relies on a strict philosophy: **Pre-process offline on PC, stream ultra-fast on ESP32**.

---

## 🕹️ 1. MUGEN Sprites (`.fgt`)

The Python script `tools/mugen_extractor/mugen_extractor.py` converts MUGEN character files (`.sff`, `.air`) into optimized `.fgt` binary files.

- **Extraction**: Master palette decoding and key animation selection (`walk`, `attack`, `hit`, `win`, `special1-3`, `super1-3`, `fall`).
- **Virtual Ground**: Calculates a uniform ground axis (`ground_y`) to prevent sprite jitter during attack moves.
- **Scaling (`--scale`)**: Custom scaling factor (e.g. `--scale 0.5` cuts sprite dimensions by half and saves 75% RAM).
- **Indexing**: Generates `index.txt` (read by `FighterEngine.cpp` on ESP32) and `index.json` (read by Raspberry Pi).

```bash
# Example conversion for 64px panels at scale 0.5:
python3 tools/mugen_extractor/mugen_extractor.py --src /path/to/mugen/chars --dest /Volumes/SDCARD/fighters_64 --scale 0.5
```

---

## 🔤 2. Custom Bitmap Fonts (`.amf`)

The script `tools/bdf_to_amfont/bdf_to_amfont.py` converts standard `.bdf` bitmap fonts into the compact `.amf` binary format (ArcadeMatrix Font).

- **Performance Gain**: Instant binary decoding without text BDF parsers in RAM.
- **Usage**: Drop your `.bdf` files into `/fonts/` on the SD card and run the script. Generated `.amf` fonts appear live in the WebUI dropdowns for Clock and Date.

```bash
python3 tools/bdf_to_amfont/bdf_to_amfont.py /Volumes/SDCARD
```

---

## 🎬 3. GIF Playlists & Animations

- **Auto-Discovery**: Firmware dynamically scans `/gifs/` and its subfolders (e.g., `/gifs/mario`, `/gifs/sonic`).
- **Indexing**: `generate_index.sh` or `generate_index.bat` scripts create an `index.txt` file in each GIF directory to speed up randomized playback and ignore macOS junk (`._*`).

---

## 🖼️ 4. Raw RGB565 Marquees (`.raw`)

- **Format**: Raw RGB565 little-endian pixels (exactly `width * height * 2` bytes).
- **Usage**: Displaying static battle stage backgrounds (`SD:/fighters_32/backgrounds/stage1.raw`) or POSTing live marquees over REST via `POST /api/marquee`.
