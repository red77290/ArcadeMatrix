# ArcadeMatrix

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

Welcome to the open-source ESP32 firmware for driving HUB75 LED Matrices! This project allows you to display Arcade Clocks, Animated GIFs, Weather, and even simulated **MUGEN fighting game sprites** directly on a real LED matrix.

## 💾 Installation

**[⬇️ Download the latest pre-built firmware](https://github.com/red77290/ArcadeMatrix/releases/latest)**
(built and tested automatically by CI on every tagged release - pick `ArcadeMatrix-esp32dev.zip`
or `ArcadeMatrix-esp32s3.zip` depending on your board, then flash `firmware-*.bin`,
`bootloader-*.bin`, `partitions-*.bin`, and `boot_app0.bin` with `esptool.py` - see
[Getting Started](docs/GETTING_STARTED.md#flashing-a-pre-built-release) for exact offsets and
command. The browser-based Web Installer above will be the easier option once the repo is public.)


## Features
- **Massive Clock Selection:** Animated clocks including classic Arcade, Binary, Cyberpunk, Flip, Word, **Pac-Man**, **Tetris**, **SlotMachine**, and **Versus (Mugen)**!
- **Wi-Fi Web UI:** Access `http://arcadematrix.local` to upload GIFs and change settings live!
- **MUGEN Fighter Engine:** Simulates 2D fighting games natively on the matrix using extracted sprites with perfect virtual ground alignment.
- **GIF Engine:** Smooth playback of GIFs stored on the SD Card.
- **MQTT Support:** Integrates seamlessly with Batocera and Recalbox to display game marques.

## SD Card Structure
Format your SD card to **FAT32**. Your SD card should look like this:
```
SD:/
  ├─ conf.ini
  ├─ gifs/
  │   ├─ playlists.json
  │   └─ mario.gif
  └─ fighters_32/
      ├─ backgrounds/
      │   └─ stage1.raw
      └─ ryu/
          ├─ idle.fgt
          └─ attack.fgt
  └─ fighters_64/
      └─ (same structure for 64px tall panels)
```
*Note: The `www/` folder is no longer required on the SD card as the Web UI is now baked directly into the ESP32 firmware!*

## Configuration (`conf.ini`)
The `conf.ini` file located at the root of your SD card is exhaustive. It contains parameters for the Matrix size, color depth, clock themes, idle rotation order, and MUGEN sprite backgrounds.
Open the `conf.ini` provided in the `release/sdcard/` folder to see all possible values.

## MUGEN Sprite Extraction (The `mugen_extractor.py` Script)
To display fighters in the `SPRITES` module, the ESP32 expects `.fgt` raw files. Since the ESP32 is not powerful enough to decode complex MUGEN character formats natively, we provide a custom Python script to convert them and generate an `index.txt` manifest containing perfect bounding boxes and virtual ground values.

### How to use the extractor:
1. Make sure you have Python 3 installed with the `Pillow` library (`pip install Pillow`), or just run `tools/mugen_extractor/start_extractor.sh`/`.bat` which sets this up for you automatically.
2. Go to the `tools/mugen_extractor/` folder in the repository.
3. Run the script, pointing `--src` at your MUGEN `chars/` folder:
   ```bash
   python mugen_extractor.py --src /Path/To/Your/Mugen/chars --dest ./fighters_32
   ```
4. The script generates `.fgt` files along with an `index.txt`/`index.json` manifest in the `--dest` folder. Run it twice (with `--dest ./fighters_32` and `--dest ./fighters_64`) if you want assets for both matrix sizes.
5. Copy the resulting `fighters_32/` or `fighters_64/` folder to your SD card.

For full details, please read the documentation inside `tools/mugen_extractor/README.md`.

### Sprite Backgrounds
Fighters need an arena! You can define the background they fight on by placing a raw image file (e.g., `stage1.raw`) in `SD:/fighters_32/backgrounds/`.
Then, link this background in your `conf.ini` under the `[DATE]` section (backgrounds are used to spice up the date module!):
```ini
BACKGROUND_SPRITE=stage1.raw
```

## GIF Playlist Indexing (Web UI folder selection)
The Web UI lets you tick/untick which `gifs/` subfolders play during the idle rotation, but it needs a `playlists.json` manifest to know what's on the SD card. GIF playback itself works fine without it (the engine always reads files directly from the SD card) - this step is only needed if you want to use that checkbox selector.

1. Organize your GIFs into subfolders under `gifs/` on your SD card, e.g. `gifs/mario/`, `gifs/sonic/` (each subfolder becomes one selectable playlist; loose `.gif` files directly under `gifs/` always play and don't need this step).
2. Run one of the native scripts in `tools/gif_indexation/` - no Python required:
   ```bash
   ./generate_index.sh /Volumes/SDCARD      # macOS/Linux - pass the SD root or its gifs/ folder
   ```
   ```powershell
   .\generate_index.ps1 -Path E:\           # Windows
   ```
3. This creates `gifs/playlists.json` on the SD card. Re-run it whenever you add, remove, or rename a folder inside `gifs/`.

For full details, see `tools/gif_indexation/README.md`.

## Compilation
To compile the firmware yourself, you must use **PlatformIO**.
- For 128x32: A standard ESP32 WROOM is sufficient.
- For 256x64: An **ESP32-S3 with PSRAM** is highly recommended to avoid Out-Of-Memory crashes with double buffering.

Run the following command to build:
```bash
pio run -e esp32dev
```

## 📚 Further Documentation
- [Getting Started (PlatformIO setup, build, flash, logs)](docs/GETTING_STARTED.md)
- [Web Installer (flash from your browser, no CLI needed)](webinstaller/README.md) - *goes live once this repo is public (GitHub Pages requires a public repo on the free plan); until then, use the pre-built firmware above.*
- [Hardware Guide](docs/HARDWARE.md)
- [Wiring Guide](docs/WIRING.md)
- [Configuration Guide](docs/CONFIGURATION.md)
- [Developer Guide](docs/DEVELOPER.md)
- [Architecture](docs/ARCHITECTURE.md)
