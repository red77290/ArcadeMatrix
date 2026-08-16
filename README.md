# ArcadeMatrix

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

Welcome to the open-source ESP32 firmware for HUB75 LED matrix displays! This project allows you to display Arcade clocks, animated GIFs, live weather, and even **MUGEN fighting game sprites** simulated directly on a real LED matrix.

---

> [!IMPORTANT]
> ### ⚡ One-Click Web Installer (Browser Flash)
> Flash your ESP32 board directly from your web browser (Chrome / Edge / Opera) in one click with zero software to install!
> 
> 👉 **[🚀 Launch ArcadeMatrix Web Installer](https://red77290.github.io/ArcadeMatrix/)**
> 
> | Firmware Version | Compatible Hardware Board | Web Installer Button |
> | :--- | :--- | :--- |
> | **ESP32-DevKit (Classic)** | ESP32-DevKitC, NodeMCU-32S, WROOM-32 (4MB Flash) | Select **ESP32 (Standard)** |
> | **ESP32-S3 Waveshare** | Waveshare ESP32-S3 Matrix Board (8MB Flash + PSRAM) | Select **ESP32-S3 (Waveshare)** |

---

## 💾 Releases & SD Card Kit

**[⬇️ Download Latest Pre-built Release & SD Card Kit](https://github.com/red77290/ArcadeMatrix/releases/latest)**
- **Firmware Bundles**: Pick `ArcadeMatrix-esp32dev.zip` or `ArcadeMatrix-esp32s3_waveshare.zip` depending on your board (contains `firmware-*.bin`, `bootloader-*.bin`, `partitions-*.bin`, and `boot_app0.bin` for manual `esptool.py` flashing - see [Getting Started](docs/GETTING_STARTED.md#flashing-a-pre-built-release)).
- **SD Card Starter Kit (`ArcadeMatrix-sdcard.zip`)**: Ready-to-copy root folder structure containing `conf.ini`, GIF/MUGEN asset folders, and playlist indexing scripts.


## Features
- **Massive Clock Selection:** Animated clocks including classic Arcade, Binary, Cyberpunk, Flip, Word, **Pac-Man**, **Tetris**, **SlotMachine**, **MatrixRain**, and **Versus (Mugen)**!
- **🌡️ Indoor Temperature & Humidity (SHTC3):** Responsive display (°C/°F toggle), custom thermometer & water drop pixel art, and  REST endpoint for Home Assistant integration!
- **🔊 Decibel & Sound Level Meter (Arcade Room / Gaming Room Use-Case :) :** Real-time SPL noise monitoring with 6 reactive Pixel Art smileys (<45dB 😊 to >88dB 🚨). **Ideal for monitoring ambient noise levels in a loud arcade room, gaming room, or retro gaming party!**
- **🎵 Rhythmic Music Visualizer:** 4 priority display modes (Spectrum Equalizer with peak hold, Oscilloscope Waveform, Radial Circles, and Neon Fire).
- **Real-Time Crypto & Stock Market Tickers:** Live price quotes & 24h % badges from CoinGecko, Binance, and Yahoo Finance with configurable TTL cache.
- **Wi-Fi Web UI:** Access `http://arcadematrix.local` to upload GIFs and change settings live!
- **MUGEN Fighting Engine:** Natively simulates 2D fighting games on the matrix using extracted sprites with perfect virtual-ground alignment.
- **GIF Engine:** Smooth playback of GIFs stored on the SD card.
- **Weather (OpenWeatherMap):** 3-day forecast, with a 15-minute fetch cache to save your API calls.
- **MQTT Support:** Integrates seamlessly with Batocera and Recalbox to display game marquees.
- **OTA Updates:** Flash firmware updates wirelessly directly through the Web UI.
 - **ESP32-S3 Waveshare Support:** Full support for high-end ESP32-S3 boards and 256x64 True Matrix panels via DMA.

## SD Card Structure
Format your SD card to **FAT32** or **exFAT**. Your SD card should look like this:
```
SD:/
  ├─ conf.ini
  ├─ gifs/
  │  │   └─ mario.gif
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
Open the `conf.ini` provided in the `release/sdCard/` folder to see all possible values.

## MUGEN Sprite Extraction (The `mugen_extractor.py` Script)
To display fighters in the `SPRITES` module, the ESP32 expects `.fgt` raw files. Since the ESP32 is not powerful enough to decode complex MUGEN character formats natively, we provide a custom Python script to convert them and generate an `index.txt` manifest containing perfect bounding boxes and virtual ground values.

### How to use the extractor:
1. Make sure you have Python 3 installed with the `Pillow` library (`pip install Pillow`), or just run `tools/mugen_extractor/start_extractor.sh`/`.bat` which sets this up for you automatically.
2. Go to the `tools/mugen_extractor/` folder in the repository.
3. Run the script, pointing `--src` at your MUGEN `chars/` folder:
   ```bash
   python mugen_extractor.py --src /Path/To/Your/Mugen/chars --dest ./fighters_32
   # Or with custom scaling (e.g., --scale 0.5 to scale down by 50% saving 75% RAM):
   python mugen_extractor.py --src /Path/To/Your/Mugen/chars --dest ./fighters_64 --scale 0.5
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

## Custom Fonts (BDF → AMF conversion)
The Clock, Date, and scrolling Message can use custom bitmap fonts loaded from the SD card instead of the ~6 fonts compiled into the firmware, using the same `.bdf` fonts `ArcadeMatrix_RPi` already ships. The ESP32 has no on-device BDF parser though, so they must be converted to the compact `.amf` format first.

1. Copy your `.bdf` font(s) into the `fonts/` folder on your SD card.
2. Run the batch converter:
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py /Volumes/SDCARD   # pass the SD root or its fonts/ folder
   ```
   (No external dependencies required. Standard Python only.)
3. This converts each `.bdf` to an equivalently named `.amf` in-place. The resulting fonts immediately appear in the Web UI Settings page (Clock/Date "Font" dropdowns) - no restart required.

For full details, check `tools/bdf_to_amfont/README.md`.

## ⚡ Hardware Compatibility
- **Classic ESP32 (WROOM-32 / `esp32dev`)**: Dual-core Tensilica Xtensa LX6 @ 240MHz. Fully supports all features (Real-Time Crypto, Stock Market, MUGEN, Weather, Web UI) for **128x32 / 64x32 matrix panels** (internal 320KB SRAM is sufficient for double-buffered DMA).
- **ESP32-S3 with PSRAM (`esp32s3_waveshare`)**: Required for large **256x64 True Matrix panels** which require PSRAM for 256KB DMA double-buffering.

## Compilation
To compile the firmware yourself, you must use **PlatformIO**.
- For 128x32: A standard ESP32 WROOM is sufficient (`pio run -e esp32dev`).
- For 256x64: An **ESP32-S3 with PSRAM** is required (`pio run -e esp32s3_waveshare`).

Run the following command to build:
```bash
pio run -e esp32s3_waveshare
```

## 📚 Further Documentation
- [Getting Started (PlatformIO setup, build, flash, logs)](docs/GETTING_STARTED.md)
- [Web Installer (flash from your browser, no CLI needed)](webinstaller/README.md) - *goes live once this repo is public (GitHub Pages requires a public repo on the free plan); until then, use the pre-built firmware above.*
- [Hardware Guide](docs/HARDWARE.md)
- [Wiring Guide](docs/WIRING.md)
- [Configuration Guide](docs/CONFIGURATION.md)
- [Developer Guide](docs/DEVELOPER.md)
- [Architecture](docs/ARCHITECTURE.md)

## 🙏 Acknowledgments

A huge thanks to the open-source community and the creators of the incredible libraries that power this project:
- **[ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA)** by mrfaptastic
- **[AnimatedGIF](https://github.com/bitbank2/AnimatedGIF)** & **[PNGdec](https://github.com/bitbank2/PNGdec)** by bitbank2
- **[ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer)** by mathieucarbou
- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson)** by bblanchon
- **[PubSubClient](https://github.com/knolleary/pubsubclient)** by knolleary
- **[PicoMQTT](https://github.com/mlesniew/PicoMQTT)** by mlesniew
- **[Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library)** by Adafruit
- **[SdFat](https://github.com/greiman/SdFat)** by greiman

Special thanks to the **RPiTeam** for the awesome pack of 600 GIFs!

## 📜 License
This project is licensed under the **[PolyForm Noncommercial License 1.0.0](LICENSE)**.

**In short:** you're free to use, modify, and share this project for any noncommercial purpose (personal use, hobby builds, research, education, non-profit/public institutions) - see the full [LICENSE](LICENSE) file for the exact terms. **Any commercial use (selling assembled units, kits, or derived products/services) requires a separate license - contact [Red1L](https://github.com/red77290) to discuss commercial terms.**
