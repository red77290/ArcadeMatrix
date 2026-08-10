# Hardware Requirements

🇬🇧 English | 🇫🇷 [Français](HARDWARE_FR.md) | 🇪🇸 [Español](HARDWARE_ES.md)

## ESP32 Models
The ArcadeMatrix firmware supports standard ESP32 boards, but requirements change based on your matrix size:

### 128x32 or Smaller (Standard Usage)
- **Board:** Standard ESP32 WROOM (e.g. NodeMCU ESP32S, ESP32 Dev Module).
- **RAM:** Standard SRAM is perfectly fine.
- **Wiring:** Uses standard VSPI/HSPI pins. See [WIRING.md](WIRING.md) for pinouts.

### 256x64 or Larger (Advanced Usage)
- **Board:** **ESP32-S3 with PSRAM** (e.g. ESP32-S3 WROOM-1 N8R8).
- **RAM:** PSRAM is **MANDATORY** for double-buffering at 24-bit color depth on massive panels.
- **Why?** A 256x64 display requires ~98KB per frame. Double buffering requires ~200KB of contiguous DMA RAM, which the standard ESP32 cannot reliably provide while maintaining Wi-Fi and Web server operations. The ESP32-S3 seamlessly offloads this to PSRAM or has enough contiguous blocks to prevent OOM (Out Of Memory) crashes.

### ESP32-S3 Waveshare (100% Tested & Physical Hardware Validated)
The **Waveshare ESP32-S3 Matrix Board** (8MB Flash + PSRAM) is **100% supported and physically verified on real hardware**. 
The dedicated profile `HARDWARE_PROFILE_WAVESHARE_S3` (`pio run -e esp32s3_waveshare`) remaps HUB75 pins to free GPIOs (A=18, B=8, C=3, D=42, E=9) and uses the high-speed 1-bit SD_MMC interface (CMD=44, CLK=1, D0=17), eliminating any conflict with octal PSRAM. Everything runs flawlessly without any GPIO conflicts. See [WIRING.md](WIRING.md) for full pinout tables.

## Multiple Panels: Chaining vs. True 2D Grids/Walls (Runtime vs. Compile-Time)
The RPi build (`ArcadeMatrix_RPi`) uses the `rpi-rgb-led-matrix` library, which exposes `--led-chain`,
`--led-parallel` and `--led-rows` as **fully runtime-configurable** flags — a Raspberry Pi has 2-3
independent HUB75 GPIO headers, so building a 2D wall of panels (e.g. 2 rows x 2 columns) is just a
config change, no rebuild required.

ESP32 boards only expose a **single** HUB75 output. This firmware already supports `CHAIN=N` in
`conf.ini` (`ConfigLoader::matrix.chainLength`) for daisy-chaining panels **in a single row** at
runtime (e.g. `CHAIN=4` for a 512x32 ribbon) — this works today and needs no firmware changes.

**True 2D grids/walls (multiple rows of chained panels, e.g. a 2x2 wall) are NOT currently wired into
this firmware.** The underlying `ESP32-HUB75-MatrixPanel-I2S-DMA` library does ship a
`VirtualMatrixPanel_T` helper that remaps virtual (x,y) coordinates onto a serpentine/zig-zag chain of
panels to build such a wall, but it is a **C++ template class** — its chain shape and scan-type are
**compile-time** parameters, not something that can be read from `conf.ini` at boot like every other
setting in this project. Wiring it in properly would require either:
1. A dedicated PlatformIO build flag/environment per wall layout (recompile+reflash to change layout), or
2. Refactoring every engine (~46 call sites) from the concrete `MatrixPanel_I2S_DMA*` type to a common
   `Adafruit_GFX*`-based interface, so a `VirtualMatrixPanel_T` instance could be swapped in.

Both are non-trivial and a genuine architecture gap vs. the RPi's fully runtime `--led-parallel`
support — tracked as a known limitation rather than silently ignored. Single-row chaining via `CHAIN=`
remains the supported way to build a larger display today.

### Flash Usage
The firmware never uses SPIFFS/LittleFS — all runtime assets (GIFs, fighter sprites, playlists,
`conf.ini`) live on the external SD card. Because of this, `esp32dev`'s PlatformIO environment uses
`board_build.partitions = min_spiffs.csv` instead of the Arduino-ESP32 default partition table: this
keeps the same dual-bank OTA layout (two app slots, so `/api/update` keeps working) but grows each
app slot from 1.25MB to ~1.875MB by reclaiming the otherwise-wasted ~900KB SPIFFS partition. As of
this writing, `esp32dev` firmware uses ~66% of its app slot (vs. 98%+ before this change) — comfortable
headroom for the PNG/weather-icon features planned next.

## Matrix Hardware
- **Type:** HUB75 / HUB75E RGB LED Matrix Panels (P2, P2.5, P3, P4, P5).
- **Driver Chips:** Compatible with standard shift registers (FM6126A, ICN2038S, etc.).
- **Power Supply:** A dedicated 5V power supply is required. A 64x32 matrix can draw up to 4 Amps at full white brightness. **Do not power the matrix directly from the ESP32!**
