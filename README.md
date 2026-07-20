# ArcadeMatrix

Welcome to the open-source ESP32 firmware for driving HUB75 LED Matrices! This project allows you to display Arcade Clocks, Animated GIFs, Weather, and even simulated **MUGEN fighting game sprites** directly on a real LED matrix.

📚 **Documentation Links:**
- [Getting Started (PlatformIO setup, build, flash, logs)](docs/GETTING_STARTED.md)
- [Hardware Guide](docs/HARDWARE.md)
- [Wiring Guide](docs/WIRING.md)
- [Configuration Guide](docs/CONFIGURATION.md)
- [Developer Guide](docs/DEVELOPER.md)
- [Architecture](docs/ARCHITECTURE.md)

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
  ├─ playlists.json
  ├─ gifs/
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
1. Make sure you have Python 3 installed with the `Pillow` library (`pip install Pillow`).
2. Go to the `tools/mugen_extractor/` folder in the repository.
3. Edit the `mugen_extractor.py` file to set your `src_dir` pointing to your MUGEN `chars/` folder.
4. Run the script:
   ```bash
   python mugen_extractor.py
   ```
5. The script will automatically generate `.fgt` files along with an `index.txt` manifest for all characters, perfectly scaled for 32px and 64px matrices.
6. Copy the resulting `fighters_32/` or `fighters_64/` folder to your SD card.

For full details, please read the documentation inside `tools/mugen_extractor/README.md`.

### Sprite Backgrounds
Fighters need an arena! You can define the background they fight on by placing a raw image file (e.g., `stage1.raw`) in `SD:/fighters_32/backgrounds/`.
Then, link this background in your `conf.ini` under the `[DATE]` section (backgrounds are used to spice up the date module!):
```ini
BACKGROUND_SPRITE=stage1.raw
```

## Compilation
To compile the firmware yourself, you must use **PlatformIO**.
- For 128x32: A standard ESP32 WROOM is sufficient.
- For 256x64: An **ESP32-S3 with PSRAM** is highly recommended to avoid Out-Of-Memory crashes with double buffering.

Run the following command to build:
```bash
pio run -e esp32dev
```
