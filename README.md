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


- **🎛️ Master Desk Deck & Multi-Widget Dashboard (`dashboard`):** Complete horizontal desk clock & dashboard with handcrafted pixel-art watch dials, smooth sweeping second hand, multiple global timezones, outdoor weather (OpenWeatherMap + free Open-Meteo fallback), calibrated SHTC3 indoor climate, live Binance cryptos & Yahoo Finance stocks/ETFs banner, and a 100% responsive auto-scaling dynamic layout!
- **Massive Animated Clock Selection (`clock`):** Interactive clocks including classic Arcade, Binary, Cyberpunk, Flip, Word, **Pac-Man**, **Tetris**, **SlotMachine**, **Pong**, **MatrixRain (Katakana)**, and **Versus (Mugen)**!
- **📻 Autonomous WebRadio & Music Engine (`music`):** Background streaming audio with real-time linear MP3 frame decoding (`minimp3`), high-fidelity Everest ES8311 I2S DAC output (WebRadio streaming over Wi-Fi — *note: Bluetooth is BLE 5.0 only for control/setup, no Bluetooth Classic A2DP music streaming*), full-color PNG album artwork, scrolling artist/title, and dynamic 64-point Cooley-Tukey FFT audio visualizer!
- **🧭 6-Axis Gyroscope Auto-Rotation (`QMI8658` / `GyroHAL`):** Automatic screen orientation ($0^\circ, 90^\circ, 180^\circ, 270^\circ$) detecting physical gravity vector, 500ms anti-vibration hysteresis, custom mounting offset, and 1-click zero calibration from the Web UI!
- **🎵 Spotify Now Playing (`spotify`):** Real-time track display with full-color album artwork, scrolling artist/title, progress bar, and animated audio equalizer.
- **📡 Google Cast & Nest (`google_cast`):** Automatic mDNS discovery of Google Home / Nest Audio devices with live streaming media artwork, progress, and volume display.
- **🖥️ System Monitor (`sysinfo`):** Real-time monitoring of CPU usage (%), RAM (%), SoC hardware temperature (°C/°F), and Uptime with vibrant gauge bars and visual themes.
- **🥊 M.U.G.E.N Combat Engine (`fighter`):** Authentic retro sprite battles (Street Fighter, KOF, DBZ, Marvel...) directly extracted in RGB565 format without stutter, playable standalone or as background overlay on clocks.
- **📈 Real-Time Crypto & Stock Market Tickers (`crypto`, `stock`):** Live price quotes, 24h % badges, and historical sparkline charts from CoinGecko, Binance, and Yahoo Finance with smart TTL caching.
- **📰 Live Breaking News & GNews Ticker (`gnews`):** Real-time top headlines and curated breaking news with topic categories (Tech, World, Business, Science, Sports...), live pulsing broadcast beacon, customizable sub-pixel 60 FPS scrolling ticker, and multi-language/localization filtering!
- **🌦️ Dynamic Weather Forecasts (`weather`):** Live weather conditions, temperature, 3-day forecasts, and retro animated icons via OpenWeatherMap.
- **🌡️ Indoor Temperature & Humidity (SHTC3):** Responsive display (°C/°F toggle), custom thermometer & water drop pixel art, and REST endpoint for Home Assistant integration!
- **📊 Home Assistant & MQTT Data Engine (`mqttdata`):** Stream live Home Assistant dashboards, sensor values, multi-entity tables, 24-hour historical graphs, and local weather forecasts over MQTT! Features zero templates needed thanks to ready-to-import [Home Assistant Blueprints](https://github.com/red77290/ArcadeMatrix/tree/main/tools/home_assistant/blueprints) and the comprehensive [Home Assistant Guide](docs/HOME_ASSISTANT.md) — implemented by [@TooncesToo](https://github.com/TooncesToo)!
- **🔊 Decibel & Sound Level Meter (Arcade / Gaming Room):** Real-time SPL noise monitoring with 6 reactive Pixel Art smileys (<45dB 😊 to >88dB 🚨) and an Audio Visualizer. ([🎥 Watch the Demo](https://youtu.be/Ljx5W2vFIU8?si=efGPixHGv7h8kcQU))
- **🎵 Rhythmic Music Visualizer:** 4 priority display modes (Spectrum Equalizer with peak hold, Oscilloscope Waveform, Radial Circles, and Neon Fire).
- **Wi-Fi Web UI:** Access `http://arcadematrix.local` to manage playlists, calibrate screen orientation, and change settings live!
- **🗂️ Network GIF Library & Web File Manager (`gifs`):** Built-in file manager card in the Web UI allowing you to browse playlist folders, upload animated GIFs over Wi-Fi without removing the SD card, create/delete folders, rename items, and trigger automatic background re-indexing. Features native dual-orientation support (`?orientation=yoko|tate`) for both Horizontal (Yoko) and Vertical (Tate) display layouts — implemented by [@TooncesToo](https://github.com/TooncesToo)!
- **GIF Engine (`gifs`):** Smooth playback of GIFs and auto-discovered playlists stored on the SD card.
- **MQTT Support (`marquee`):** Integrates seamlessly with Batocera, Recalbox, and RetroPie to display official scraped game marquees via your Pixelcade fork.
- **OTA Updates:** Flash firmware updates wirelessly directly through the Web UI or Web Installer.
- **ESP32-S3 Waveshare Support:** Full support for high-end ESP32-S3 boards and 256x64 True Matrix panels via DMA.

## 🎮 Legendary Retro Game Clocks (Showcase & Hardware Simulations)

ArcadeMatrix includes a signature collection of handcrafted, hardware-synchronized retro arcade and console clocks rendered with 100% bit-perfect original sprites in full 60 FPS, with zero dynamic allocations on the Core 1 hot-path:

### 1. Street Fighter (Capcom) — Theme 38
*Suzaku Castle rooftop duel under crescent moon night sky with authentic Ryu and Ken idle breathing, Hadouken blast on minute change with hit recoil and spark effects, and golden arcade HUD.*
![Street Fighter Clock](docs/assets/clocks/poster_puzzle_bobble.png)

### 2. Metal Slug: Super Vehicle-001 (SNK Neo Geo) — Theme 41
*Authentic SNK Neo Geo pixel art with 4 rotating Arabian desert stages (Market Bazaar, Mosque Domes, Fortress Bunker, Golden Dome Watchtower), Marco Rossi combat animations, Rebel Di-Cokka tank, patrol helicopter, and heavy machine gun firefights.*
![Metal Slug Clock](docs/assets/clocks/poster_metalslug.png)

### 3. Castlevania (Konami NES) — Theme 31
*Gothic clock tower Belfry with Simon Belmont climbing the grand stone staircase toward Dracula's chamber, authentic flickering pedestal torch, flapping vampire bat across the blood moon, and gothic HUD with lifebar pips & heart counters.*
![Castlevania Clock](docs/assets/clocks/poster_castlevania.png)

### 4. Super Mario Bros (NES) — Theme 30
*Mushroom Kingdom Overworld with 100% authentic 16x16 SMB1 brick & question mark blocks, jumping Mario hitting blocks to reveal coins, and animated Goombas.*
![Super Mario Bros Clock](docs/assets/clocks/poster_super_mario.png)

### 5. Mega Man (Capcom NES) — Theme 35
*Wily Castle fortress with classic Capcom blue Wily blocks, yellow ladder, and the Blue Bomber in action.*
![Mega Man Clock](docs/assets/clocks/poster_megaman.png)

### 6. Sonic The Hedgehog (Sega Genesis) — Theme 39
*Green Hill Zone checkered platforms, spinning gold rings, red spring, and Sonic idle/spin-dash animations.*
![Sonic The Hedgehog Clock](docs/assets/clocks/poster_sonic.png)

## 🚀 Universal Engine Compatibility: Auto Depth & Auto Buffer

ArcadeMatrix features an advanced, memory-aware execution pipeline that allows all 18 engines (including heavy engines like `AnimatedGIF`, `Stock`, `Crypto`, and `MUGEN`) to run on **classic ESP32 DevKit boards with 0 KB PSRAM** as well as flagship **ESP32-S3 boards with 16 MB PSRAM**:

- **🎨 Auto Color Depth (`dynamic_color_depth`)**:
  - **Rich 8-Bit Visuals by Default**: Clocks, fighting game animations, visualizers, messages, and marquees render at maximum 8-bit color depth (up to 256 brightness levels per RGB channel).
  - **Dynamic Sandbox Memory Reclamation**: When network engines (`stock`, `crypto`, `weather`, `gnews`) need to perform HTTPS requests, the HUB75 DMA engine temporarily downshifts to 4 bits under hardware blanking. This instantly frees **16 to 24 KB of contiguous DMA DRAM**, ensuring plenty of room for TLS handshakes and chunked JSON parsing without memory fragmentation.
  - **0 ms Instant Transitions**: When stock quotes, crypto prices, and sparkline charts are fresh in cache (`needsTlsFetch() == false`), the downshift is skipped completely. Stock and Crypto activate immediately in **full 8 bits with 0 ms transition time (zero display blanking)**.

- **⚡ Auto Buffering Pipeline (`render_pipeline: auto`)**:
  - **PSRAM Acceleration**: On ESP32-S3 boards, allocates a 16-bit canvas in PSRAM with double DMA buffers (`canvas_double`) for buttery-smooth 60 FPS tear-free rendering.
  - **SRAM1 Canvas Isolation**: On classic ESP32 chips, allocates the 8 KB off-screen canvas in **SRAM1** (non-DMA, CPU-only DRAM) during early boot before Wi-Fi and WebServer start. This preserves **8,192 bytes of contiguous DMA-capable memory in SRAM2**, preventing memory starvation.
  - **Tear-Free Single DMA**: Uses `Hub75BulkEncoder` sequential row bursts to commit frames atomically, eliminating scanline tearing even on single-buffered displays.

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
      ├─ (same structure for 64px tall panels)
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

> [!TIP]
> **Web UI File Manager & Uploader (No SD card removal needed!):**
> You can now manage playlists, create/delete folders, rename items, and upload animated GIFs directly through the browser using the Web UI **GIF File Manager** card with native Horizontal / Vertical orientation support, with automatic background re-indexing — implemented by [@TooncesToo](https://github.com/TooncesToo)!
> 
> Alternatively, for batch offline preparation on your computer:
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

## ⚡ Hardware Compatibility & Engine Matrix

| Engine / Feature | Category | ESP32-S3 (Waveshare Board) | ESP32 Classic (DevKit / `esp32dev`) | Hardware / Network Requirement |
| :--- | :--- | :---: | :---: | :--- |
| **Clock Engine (`clock`)** | `info` | 🟢 60 FPS | 🟢 60 FPS | Dynamic fonts, arcade/retro watch faces |
| **Animated GIFs (`gifs`)** | `media` | 🟢 Fullspeed 60 FPS | 🟢 Fullspeed 60 FPS | Micro-SD Card (Yoko / Tate orientations) |
| **M.U.G.E.N Combat (`fighter`)** | `arcade` | 🟢 60 FPS | 🟢 60 FPS | Micro-SD Card (RGB565 sprite streaming) |
| **Desk Deck Dashboard (`dashboard`)** | `info` | 🟢 10 FPS | 🟢 10 FPS | Wi-Fi (Multi-widget clock, weather & market) |
| **Real-Time Crypto (`crypto`)** | `finance` | 🟢 10 FPS (8 bits) | 🟢 10 FPS (8 bits cached) | Wi-Fi, HTTPS/TLS (Binance, CoinGecko) |
| **Stock Market & Sparklines (`stock`)** | `finance` | 🟢 10 FPS (8 bits) | 🟢 10 FPS (8 bits cached) | Wi-Fi, HTTPS/TLS (Yahoo Finance) |
| **Live Breaking News (`gnews`)** | `news` | 🟢 30 FPS | 🟢 30 FPS | Wi-Fi, HTTPS/TLS (GNews API) |
| **Live Weather Forecasts (`weather`)** | `info` | 🟢 10 FPS | 🟢 10 FPS | Wi-Fi (OpenWeatherMap, Open-Meteo) |
| **Date & Calendar (`date`)** | `info` | 🟢 30 FPS | 🟢 30 FPS | Local system time & optional sprite backdrop |
| **Scrolling Text Ticker (`message`)** | `text` | 🟢 60 FPS | 🟢 60 FPS | Sub-pixel 60 FPS text scroller |
| **System Telemetry (`sysinfo`)** | `system` | 🟢 10 FPS | 🟢 10 FPS | Real-time CPU, RAM, Temp & Uptime gauges |
| **Retro Gameroom Marquee (`marquee`)** | `arcade` | 🟢 60 FPS | 🟢 60 FPS | MQTT / Batocera / Recalbox / RetroPie |
| **Spotify Now Playing (`spotify`)** | `media` | 🟢 30 FPS | 🟢 30 FPS | Wi-Fi, HTTPS/TLS, Spotify Web API |
| **Google Cast Display (`google_cast`)** | `media` | 🟢 30 FPS | 🟢 30 FPS | Wi-Fi, mDNS local discovery |
| **Autonomous WebRadio (`music`)** | `media` | 🟢 30 FPS (I2S DAC) | ❌ Incompatible | Built-in ES8311 DAC & PSRAM required |
| **Audio Spectrum Visualizer (`audiovisualizer`)** | `audio` | 🟢 60 FPS (I2S Mic) | ❌ Incompatible | Built-in ES7210 microphone array required |
| **SPL Decibel Sound Meter (`decibel`)** | `audio` | 🟢 30 FPS (I2S Mic) | ❌ Incompatible | Built-in ES7210 microphone array required |
| **Indoor Climate Sensor (`temp`)** | `sensor` | 🟢 30 FPS (I2C SHTC3) | ❌ Incompatible | Built-in SHTC3 temperature/humidity sensor required |
| **Home Assistant & MQTT Data (`mqttdata`)** | `info` | 🟢 30 FPS | 🟢 30 FPS | MQTT Broker / Home Assistant Blueprints |

👉 *For the comprehensive technical breakdown (FPS, RAM, and DMA budgets), see the [Engine Compatibility Matrix](docs/ENGINE_COMPATIBILITY_MATRIX.md).*

> [!NOTE]
> ### 💡 ESP32 Classic (`esp32dev`) Full TLS Compatibility & Smart Transition Prefetch
> The classic ESP32 (WROOM-32 without PSRAM) is now **fully compatible** with TLS/HTTPS engines (`crypto`, `stock`, `gnews`, `weather`) thanks to an advanced **DMA-released memory sandbox**:
> 1. **Why the ~1s pause on the first rotation?** On classic ESP32, concurrent HUB75 DMA scanning and mbedTLS cryptographic handshakes cannot run simultaneously due to contiguous internal SRAM constraints. During the transition into a TLS engine, the firmware temporarily releases the DMA framebuffer, opening an **89 KB clean memory window** to prefetch all quotes, charts, and icons over HTTP/1.1 keep-alive in ~700 ms before reconfiguring the panel.
> 2. **Instant Subsequent Rotations:** This brief transition fetch occurs **only once** on the initial rotation! For all subsequent rotations during the cache lifetime (configurable via UI, e.g. 10 to 15 minutes), the engine serves cached quotes and sparkline charts directly from RAM at **full 8-bit color depth with zero transition delay and zero network overhead**.
> 3. **Automatic Refresh:** When the cache TTL expires, the engine cleanly performs a single background refresh on the next rotation and restarts the fresh cache cycle.

> [!NOTE]
> **Dynamic Hardware Probing & Graceful Degradation:** All hardware sensors (Gyroscope `QMI8658`, Microphone `ES7210`, DAC `ES8311`, Temperature `SHTC3`) are dynamically probed on the I2C/I2S bus at startup. If a sensor or peripheral is absent on your board, the feature is **automatically disabled safely without crashing**, falling back to manual settings in the Web UI.

- **ESP32-S3 Waveshare RGB Matrix Board (`esp32s3_waveshare`)**: **100% Compatible with all features.** Highly recommended. Required for large **256x64 True Matrix panels**, autonomous WebRadio audio streaming, Gyroscope auto-rotation, and leverages built-in hardware sensors (Decibel, Temp, Speaker DAC) out of the box.
- **Classic ESP32 (WROOM-32 / `esp32dev`)**: Dual-core Tensilica Xtensa LX6 @ 240MHz. Fully supports animations, Web UI, MUGEN, and now all HTTPS/TLS cloud engines (`crypto`, `stock`, `gnews`, `weather`) on **128x32 / 64x32 matrix panels**. Built-in physical audio/temp sensors are absent from standard DevKits unless wired externally.

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
- [Home Assistant Integration Guide](docs/HOME_ASSISTANT.md)
- [Home Assistant Blueprints](tools/home_assistant/blueprints/)
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
- **[@TooncesToo](https://github.com/TooncesToo)** for developing the Home Assistant & MQTT Data engine with ready-to-use blueprints, the network GIF library API, multi-file uploader, and Web UI file manager with dual-orientation support on both ESP32 and Raspberry Pi.

Special thanks to the **RPiTeam** for the awesome pack of 600 GIFs!

## 📜 License
This project is licensed under the **[PolyForm Noncommercial License 1.0.0](LICENSE)**.

**In short:** you're free to use, modify, and share this project for any noncommercial purpose (personal use, hobby builds, research, education, non-profit/public institutions) - see the full [LICENSE](LICENSE) file for the exact terms. **Any commercial use (selling assembled units, kits, or derived products/services) requires a separate license - contact [Red1L](https://github.com/red77290) to discuss commercial terms.**
