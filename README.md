# ArcadeMatrix

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

📺 **Video Demo / Présentation :** https://youtu.be/2sA5wLVozRQ?si=T1gn6MYDwpq2-54c

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
> | **ESP32-S3 Waveshare** | Waveshare ESP32-S3 Matrix Board (32MB Flash + 16MB PSRAM (N32R16)) | Select **ESP32-S3 (Waveshare)** |

---

## 💾 Releases & SD Card Kit

**[⬇️ Download Latest Pre-built Release & SD Card Kit](https://github.com/red77290/ArcadeMatrix/releases/latest)**
- **Firmware Bundles**: Pick `ArcadeMatrix-esp32dev.zip` or `ArcadeMatrix-esp32s3_waveshare.zip` depending on your board (contains `firmware-*.bin`, `bootloader-*.bin`, `partitions-*.bin`, and `boot_app0.bin` for manual `esptool.py` flashing - see [Getting Started](docs/GETTING_STARTED.md#flashing-a-pre-built-release)).
- **SD Card Starter Kit (`ArcadeMatrix-sdcard.zip`)**: Ready-to-copy root folder structure containing `config.json`, GIF/MUGEN asset folders, and playlist indexing scripts.


## Features
- **Massive Animated Clock Selection (`clock`):** Interactive clocks including classic Arcade, Binary, Cyberpunk, Flip, Word, **Pac-Man**, **Tetris**, **SlotMachine**, **Pong**, **MatrixRain (Katakana)**, and **Versus (Mugen)**!
- **📻 Autonomous WebRadio & Music Engine (`music`):** Background streaming audio with real-time linear MP3 frame decoding (`minimp3`), high-fidelity Everest ES8311 I2S DAC output, Bluetooth A2DP Sink, full-color PNG album artwork, scrolling artist/title, and dynamic 64-point Cooley-Tukey FFT audio visualizer!
- **🧭 6-Axis Gyroscope Auto-Rotation (`QMI8658` / `GyroHAL`):** Automatic screen orientation ($0^\circ, 90^\circ, 180^\circ, 270^\circ$) detecting physical gravity vector, 500ms anti-vibration hysteresis, custom mounting offset, and 1-click zero calibration from the Web UI!
- **🎵 Spotify Now Playing (`spotify`):** Real-time track display with full-color album artwork, scrolling artist/title, progress bar, and animated audio equalizer.
- **📡 Google Cast & Nest (`google_cast`):** Automatic mDNS discovery of Google Home / Nest Audio devices with live streaming media artwork, progress, and volume display.
- **🖥️ System Monitor (`sysinfo`):** Real-time monitoring of CPU usage (%), RAM (%), SoC hardware temperature (°C/°F), and Uptime with vibrant gauge bars and visual themes.
- **🥊 M.U.G.E.N Combat Engine (`fighter`):** Authentic retro sprite battles (Street Fighter, KOF, DBZ, Marvel...) directly extracted in RGB565 format without stutter, playable standalone or as background overlay on clocks.
- **📈 Real-Time Crypto & Stock Market Tickers (`crypto`, `stock`):** Live price quotes, 24h % badges, and historical sparkline charts from CoinGecko, Binance, and Yahoo Finance with smart TTL caching.
- **🌦️ Dynamic Weather Forecasts (`weather`):** Live weather conditions, temperature, 3-day forecasts, and retro animated icons via OpenWeatherMap.
- **🌡️ Indoor Temperature & Humidity (SHTC3):** Responsive display (°C/°F toggle), custom thermometer & water drop pixel art, and REST endpoint for Home Assistant integration!
- **🔊 Decibel & Sound Level Meter (Arcade / Gaming Room):** Real-time SPL noise monitoring with 6 reactive Pixel Art smileys (<45dB 😊 to >88dB 🚨) and an Audio Visualizer. ([🎥 Watch the Demo](https://youtu.be/Ljx5W2vFIU8?si=efGPixHGv7h8kcQU))
- **🎵 Rhythmic Music Visualizer:** 4 priority display modes (Spectrum Equalizer with peak hold, Oscilloscope Waveform, Radial Circles, and Neon Fire).
- **Wi-Fi Web UI:** Access `http://arcadematrix.local` to upload GIFs, calibrate screen orientation, and change settings live!
- **GIF Engine (`gifs`):** Smooth playback of GIFs and auto-discovered playlists stored on the SD card.
- **MQTT Support (`marquee`):** Integrates seamlessly with Batocera, Recalbox, and RetroPie to display official scraped game marquees via your Pixelcade fork.
- **OTA Updates:** Flash firmware updates wirelessly directly through the Web UI or Web Installer.
- **ESP32-S3 Waveshare Support:** Full support for high-end ESP32-S3 boards and 256x64 True Matrix panels via DMA.

## SD Card Structure
Format your SD card to **FAT32** or **exFAT**. Your SD card should look like this:
```
SD:/
  ├─ config.json
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

## Configuration (`config.json`)
The `config.json` file located at the root of your SD card is exhaustive. It contains parameters for the Matrix size, color depth, clock themes, idle rotation order, and MUGEN sprite backgrounds.
Open the `config.json` provided in the `release/sdCard/` folder to see all possible values.

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
Then, link this background in your `config.json` under the `[DATE]` section (backgrounds are used to spice up the date module!):
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

## ⚡ Hardware Compatibility & Features

| Feature | ESP32-S3 (Waveshare Board) | ESP32 Classic (DevKit) |
| :--- | :---: | :---: |
| Matrix Support | Up to 256x64 (True Matrix) | Up to 128x32 |
| Double Buffering | ✅ Yes (Smooth) | ✅ Yes (Smooth) |
| Animations (GIFs) | ✅ Yes | ✅ Yes |
| MUGEN Engine | ✅ Yes | ✅ Yes |
| Web UI & Wi-Fi | ✅ Yes | ✅ Yes |
| **Autonomous WebRadio & MP3 Decoding** | ✅ Yes (Built-in ES8311 DAC & Speaker PA) | ❌ No (Requires I2S DAC & PSRAM) |
| **Bluetooth A2DP Audio Sink** | ✅ Yes (ESP-IDF Native A2DP Sink) | ⚠️ Limited (Requires external DAC) |
| **6-Axis Gyro Auto-Rotation (`QMI8658`)** | ✅ Yes (Built-in IMU & 1-Click Calibrate) | ❌ No (Requires external I2C sensor) |
| **Real-Time Crypto** | ✅ Yes | ❌ No (Not enough RAM for SSL) |
| **Stock Market** | ✅ Yes | ❌ No (Not enough RAM for SSL) |
| **Decibel Meter** | ✅ Yes (Built-in ES7210 Mic Array) | ❌ No (Requires external I2S Mic & custom code) |
| **Indoor Temp & Humidity (SHTC3)** | ✅ Yes (Built-in Sensor) | ❌ No (Requires external I2C SHTC3 & custom code) |

> [!NOTE]
> **Dynamic Hardware Probing & Graceful Degradation:** All hardware sensors (Gyroscope `QMI8658`, Microphone `ES7210`, DAC `ES8311`, Temperature `SHTC3`) are dynamically probed on the I2C/I2S bus at startup. If a sensor or peripheral is absent on your board, the feature is **automatically disabled safely without crashing**, falling back to manual settings in the Web UI.

- **ESP32-S3 Waveshare RGB Matrix Board (`esp32s3_waveshare`)**: **100% Compatible with all features.** Highly recommended. Required for large **256x64 True Matrix panels**, autonomous WebRadio audio streaming, Gyroscope auto-rotation, RAM-heavy modules (Crypto, Stock), and leverages built-in hardware sensors (Decibel, Temp, Speaker DAC) out of the box.
- **Classic ESP32 (WROOM-32 / `esp32dev`)**: Dual-core Tensilica Xtensa LX6 @ 240MHz. Supports core animations, Web UI, and MUGEN for **128x32 / 64x32 matrix panels**. Does not support RAM-heavy features like HTTPS/SSL (Crypto/Stock) or standalone audio streaming. Built-in sensors (Mic/Temp/Gyro/DAC) are also absent from standard DevKits.

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
