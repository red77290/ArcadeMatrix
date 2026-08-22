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

| Engine / Feature | ESP32 Standard (No PSRAM) | Waveshare ESP32-S3 (16MB PSRAM) | Requirement (`EngineRequirements`) |
|---|---|---|---|
| **Clock Engine** (30 Themes) | ✅ Active | ✅ Active | None |
| **Date Engine** | ✅ Active | ✅ Active | None |
| **Weather Engine** | ✅ Active | ✅ Active | `needsNetwork=true` |
| **GIF Player** | ✅ Active (SD Streaming) | ✅ Active (PSRAM Buffering) | `needsSd=true` |
| **Fighter Overlay** | ✅ Active (32px mode) | ✅ Active (64px mode) | `needsSd=true` |
| **Temp & Humidity** | ⚠️ Active if SHTC3 present | ✅ Active (Onboard SHTC3) | `needsTempSensor=true` |
| **Visualizer & Decibel** | ⚠️ Active if I2S mic present | ✅ Active (Onboard ES7210) | `needsAudio=true` |
| **Crypto Ticker** | 🚫 Skipped (*Requires PSRAM*) | ✅ Active (Quote Cache) | `needsPsram=true`, `needsNetwork=true` |
| **Stock Ticker** | 🚫 Skipped (*Requires PSRAM*) | ✅ Active (Quote Cache) | `needsPsram=true`, `needsNetwork=true` |
| **Custom Message** | ✅ Active | ✅ Active | None |
| **Marquee Alert** | ✅ Active | ✅ Active | None |

---

## 3. Power Supply Considerations
- **RGB LED Matrix**: HUB75 / HUB75E panels (P2, P2.5, P3, P4, P5).
- **Dedicated 5V Power Supply**: A 64x32 matrix can draw up to 4A at full white brightness. **Always power the matrix directly from the power supply, NOT through the ESP32 5V pin.**
