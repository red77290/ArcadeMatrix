# Wiring Guide

🇬🇧 English | 🇫🇷 [Français](WIRING_FR.md) | 🇪🇸 [Español](WIRING_ES.md)

Wiring a HUB75 LED matrix to an ESP32 requires precise connections. The matrix data pins map to the ESP32's GPIO pins.

## Standard ESP32 (WROOM) Pinout
This is the default wiring map for the DMA Engine.

| HUB75 Pin | ESP32 GPIO | Description |
|-----------|------------|-------------|
| R1        | 25         | Red Top |
| G1        | 26         | Green Top |
| B1        | 27         | Blue Top |
| R2        | 14         | Red Bottom |
| G2        | 12         | Green Bottom |
| B2        | 13         | Blue Bottom |
| A         | 33         | Address A |
| B         | 32         | Address B |
| C         | 22         | Address C |
| D         | 17         | Address D |
| E         | 18         | Address E (For 64px tall panels) |
| LAT (STB) | 4          | Latch |
| OE        | 15         | Output Enable |
| CLK       | 16         | Clock |

*Note: Pin E is only required if your matrix is 64 pixels tall (e.g., 1/32 scan rate). On 32px-tall
panels (1/16 scan), the panel's `E` pin is typically left unconnected or tied to GND on the panel
side itself, so GPIO18 stays exclusively dedicated to the SD card's SPI clock below - no conflict
in that (very common) configuration.*

## SD Card Wiring (both boards)

The SD card uses a separate SPI bus (`SPI.begin()` in `src/main.cpp`), independent from the
HUB75 matrix's dedicated I2S/DMA pins above.

| SD Pin | ESP32 GPIO | Description |
|--------|------------|-------------|
| CS     | 5          | Chip Select |
| SCK    | 18         | SPI Clock |
| MISO   | 19         | Data from card |
| MOSI   | 23         | Data to card |

ℹ️ GPIO18 is shared on paper between this SD SCK line and the HUB75 `E` address pin defined
above, but this is **not a conflict** in the common case: on 32px-tall panels (1/16 scan), `E`
isn't used/wired to the ESP32 at all (often tied to GND on the panel itself), so GPIO18 is free
for the SD card - this is the tested/working configuration. Only if you wire a genuine 64px-tall
panel with `E` actually connected to GPIO18 would the two share the same physical pin; in that
specific case, remap either the SD SCK line (`VSPI_SCK` in `src/main.cpp`) or the HUB75 `E` pin
(`_pins` in `src/core/MatrixEngine.cpp`) to a free GPIO before wiring both. If your SD card fails
to mount (`sdWait Failed` / `sdSelectCard Failed` in the serial log), the more likely causes are:
the card isn't FAT32-formatted, isn't fully seated, or your power supply can't drive the matrix +
SD + Wi-Fi simultaneously (a bare ESP32 board's onboard regulator is often insufficient for a
fully wired panel - use a dedicated 5V/3A+ supply feeding both the panel and the ESP32).

## ESP32-S3 Waveshare Wiring (100% Tested & Physical Hardware Validated)

The **Waveshare ESP32-S3 Matrix Board** (32MB Flash + 16MB PSRAM (N32R16)) integrates its own onboard HUB75 wiring and 1-bit SD_MMC bus. The dedicated profile `HARDWARE_PROFILE_WAVESHARE_S3` in `include/HardwareProfile.h` (`pio run -e esp32s3_waveshare`) automatically remaps all pins to **completely bypass the GPIO 33-37 range reserved for Octal PSRAM**.

**This configuration is 100% tested and physically verified on real hardware with smooth operation.**

### Official Waveshare ESP32-S3 Pinout (HUB75 & SD_MMC)

| HUB75 Signals | ESP32-S3 GPIO | SD Card Signals (SD_MMC) | ESP32-S3 GPIO |
| :--- | :--- | :--- | :--- |
| **R1** | 4 | **D0** | 17 |
| **G1** | 5 | **CMD** | 44 |
| **B1** | 6 | **CLK** | 1 |
| **R2** | 7 | | |
| **G2** | 15 | | |
| **B2** | 16 | | |
| **A** | 18 | | |
| **B** | 8 | | |
| **C** | 3 | | |
| **D** | 42 | | |
| **E** | 9 | | |
| **LAT** | 40 | | |
| **OE** | 2 | | |
| **CLK** | 41 | | |

*All HUB75 and SD pins are located outside the critical GPIO 33-37 range reserved by octal PSRAM, guaranteeing perfect operation on 256x64 and 128x32 panels without any conflicts.*

