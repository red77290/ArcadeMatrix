# Hardware Requirements & Capability Matrix

🇬🇧 English | 🇫🇷 [Français](HARDWARE_FR.md) | 🇪🇸 [Español](HARDWARE_ES.md)

## 1. Supported Board Profiles

ArcadeMatrix supports two physical board profiles:

### 1.1 ESP32 Standard (`esp32dev`)
- **Microcontroller**: Standard ESP32 dual-core (e.g. NodeMCU, ESP32 Dev Module).
- **RAM**: ~320 KB Internal SRAM (No PSRAM).
- **Storage**: SPI SD card (VSPI).
- **Matrix Resolution**: Up to 128x32 or 64x64.
- **Audio / Sensor**: Optional external INMP441 / SHTC3 modules.

### 1.2 Waveshare ESP32-S3 Matrix Board (`esp32s3_waveshare`)
- **Microcontroller**: ESP32-S3 dual-core with 32 MB Flash + 16 MB Octal PSRAM.
- **Storage**: Fast 1-bit SD_MMC (D0=17, CMD=44, CLK=1).
- **Onboard Peripherals**: ES7210 I2S audio codec + digital microphone, SHTC3 temperature/humidity sensor.
- **Matrix Resolution**: Up to 256x64 (SPIRAM DMA double-buffering supported).

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
