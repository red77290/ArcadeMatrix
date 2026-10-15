# Hardware Requirements & Capability Matrix

🇬🇧 English | 🇫🇷 [Français](HARDWARE_FR.md) | 🇪🇸 [Español](HARDWARE_ES.md)

## 1. Supported Board Profiles

ArcadeMatrix supports three firmware targets across two hardware capability tiers:

### 1.1 ESP32 Standard (`esp32dev`)
- **Microcontroller**: Standard ESP32 dual-core (e.g. NodeMCU, ESP32 Dev Module).
- **RAM**: ~320 KB Internal SRAM (No PSRAM).
- **Storage**: SPI SD card (VSPI: `CS=5, SCK=18, MISO=19, MOSI=23`).
- **Matrix Resolution**: Up to 128x32 or 64x64.
- **Audio / Sensor**: Optional external INMP441 / SHTC3 modules.

### 1.2 Generic ESP32-S3 N16R8 / N8R8 (`esp32s3_n16r8`)
- **Microcontroller**: ESP32-S3 dual-core with **Quad SPI Flash** (`8 MB` or `16 MB`, `QIO`) + **Octal SPI PSRAM** (`8 MB`, `OPI`) — e.g. `ESP32-S3-DevKitC-1-N16R8` or `N8R8`.
- **Runtime Memory Detection**: Automatically detects physical Flash (`8 MB` / `16 MB` / `32 MB`) and PSRAM size at boot.
- **Storage**: Fast 1-bit `SD_MMC` (`CLK=1`, `CMD/MOSI=44` [printed as **`RX`** on DevKitC-1 boards], `D0/MISO=17`; pull `DAT3/CS` to `3.3V` with a `10kΩ` resistor).
- **Matrix Resolution**: Up to 256x64 (SPIRAM DMA double-buffering supported).

### 1.3 Waveshare ESP32-S3 Matrix Board (`esp32s3_waveshare`)
- **Microcontroller**: ESP32-S3 dual-core (`N32R16V`) with **32 MB Octal Flash (`OPI`)** + **16 MB Octal PSRAM (`OPI`)**.
- **Storage**: Fast 1-bit `SD_MMC` (`D0=17, CMD=44, CLK=1`).
- **Onboard Peripherals**: ES7210 I2S audio codec + digital microphone, ES8311 DAC, SHTC3 temperature/humidity sensor.
- **Matrix Resolution**: Up to 256x64 (SPIRAM DMA double-buffering supported).

### 1.4 ESP32-S3 Pinout (`esp32s3_n16r8` & `esp32s3_waveshare`)

| Subsystem | Signal (SD_MMC / SPI Adapter Label) | ESP32-S3 GPIO | Notes |
|---|---|---|---|
| **HUB75 Matrix** | `R1, G1, B1` | `GPIO 4, 5, 6` | Upper half RGB data |
| **HUB75 Matrix** | `R2, G2, B2` | `GPIO 7, 15, 16` | Lower half RGB data |
| **HUB75 Matrix** | `A, B, C, D, E` | `GPIO 18, 8, 3, 42, 9` | Row address lines (`E` used on 64-row panels) |
| **HUB75 Matrix** | `LAT, OE, CLK` | `GPIO 40, 2, 41` | Latch, Output Enable, Pixel Clock |
| **MicroSD (1-bit SD_MMC)** | `CLK` (`CLK` / `SCK`) | `GPIO 1` | Hardware SDMMC clock |
| **MicroSD (1-bit SD_MMC)** | `CMD` (**`MOSI`** on SPI adapters) | `GPIO 44` (**`RX`** on DevKitC-1) | Command / Response (`10kΩ` pull-up to `3.3V`) |
| **MicroSD (1-bit SD_MMC)** | `DAT0 / D0` (**`MISO`** on SPI adapters) | `GPIO 17` | Data line 0 (`10kΩ` pull-up to `3.3V`) |
| **MicroSD (1-bit SD_MMC)** | `DAT3 / CS` (**`CS`** on SPI adapters) | `3.3V` | Pull high to `3.3V` via `10kΩ` resistor (or leave unconnected if 3.3V adapter has onboard `10kΩ` pull-ups) |
| **I2C Bus** | `SDA, SCL` | `GPIO 47, 48` | Optional `SHTC3` (`0x70`), `QMI8658` (`0x6B`), `MPU6050` (`0x68`) |
| **I2S Audio** | `MCLK, BCLK, WS, DIN, DOUT` | `GPIO 12, 43 (`TX`), 38, 39, 21` | Onboard ES7210/ES8311 or external I2S Mic/DAC |

---

## 2. Feature Availability & Requirements Matrix

| Engine / Feature | Canonical ID | ESP32 Standard (No PSRAM) | Waveshare ESP32-S3 (16MB PSRAM) | Requirement (`EngineRequirements`) |
|---|---|---|---|---|
| **Clock Engine** (36 Themes) | `clock` | ✅ Active | ✅ Active | None |
| **Date Engine** | `date` | ✅ Active | ✅ Active | None |
| **Weather Engine** | `weather` | ✅ Active | ✅ Active | `needsNetwork=true` |
| **GIF Player** | `gifs` | ✅ Active (SD Streaming) | ✅ Active (PSRAM Buffering) | `needsSd=true` |
| **Crypto Ticker** | `crypto` | ✅ Active (4-bit dynamic depth + transition prefetch) | ✅ Active (8-bit + Quote Cache) | `needsNetwork=true`, `needsTls=true` |
| **Stock Ticker** | `stock` | ✅ Active (4-bit dynamic depth + transition prefetch) | ✅ Active (8-bit + Quote Cache) | `needsNetwork=true`, `needsTls=true` |
| **Audio Visualizer** | `audiovisualizer` | ⚠️ Active if I2S mic present | ✅ Active (Onboard ES7210) | `needsAudio=true` |
| **Decibel Meter** | `decibelMeter` | ⚠️ Active if I2S mic present | ✅ Active (Onboard ES7210) | `needsAudio=true` |
| **Temp & Humidity** | `temp` | ⚠️ Active if SHTC3 present | ✅ Active (Onboard SHTC3) | `needsTempSensor=true` |
| **Custom Message** | `message` | ✅ Active | ✅ Active | None |
| **Google Cast / Nest** | `google_cast` | ✅ Active | ✅ Active | `needsNetwork=true` |
| **Spotify Official Player** | `spotify` | ✅ Active (4-bit dynamic depth) | ✅ Active | `needsNetwork=true`, `needsTls=true` |
| **System Monitor** | `system_info` | ✅ Active | ✅ Active | None |
| **Universal Music Player** | `music_player` | 🚫 Skipped (*Requires PSRAM & Audio Output*) | ✅ Active | `needsPsram=true`, `needsAudioOutput=true` |
| **Master Dashboard** | `dashboard` | ✅ Active (4-bit dynamic depth + transition prefetch) | ✅ Active | None (Network/Sensor optional) |
| **GNews Live Ticker** | `gnews` | ✅ Active (4-bit dynamic depth + SD cache) | ✅ Active | `needsNetwork=true`, `needsTls=true` |
| **Arcade Marquee** | `marquee` | ✅ Active | ✅ Active | None |
| **MQTT Data** | `mqttdata` | ✅ Active (Up to 4 pages) | ✅ Active (Up to 6 pages) | `needsNetwork=true` |
| **Fighter Overlay** *(Transverse)* | `fighter` *(overlay)* | ✅ Active (32px mode) | ✅ Active (64px mode) | `needsSd=true` |

---

## 3. Power Supply Considerations
- **RGB LED Matrix**: HUB75 / HUB75E panels (P2, P2.5, P3, P4, P5).
- **Dedicated 5V Power Supply**: A 64x32 matrix can draw up to 4A at full white brightness. **Always power the matrix directly from the power supply, NOT through the ESP32 5V pin.**
