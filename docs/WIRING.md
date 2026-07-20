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

## ESP32-S3 Wiring

⚠️ **Known conflict, not yet re-validated on real hardware**: the firmware currently uses the **same**
pin map on ESP32-S3 as on the classic ESP32 (see table above), defined once in `src/MatrixEngine.cpp`.
This is a problem for 256x64 panels specifically, because that's exactly the case where octal PSRAM
(N8R8/N16R8) is required — and octal PSRAM on ESP32-S3 internally reserves **GPIO 33-37**, which
directly conflicts with pin `A` (GPIO 33) and is adjacent to pin `B` (GPIO 32) above. The firmware
prints a runtime warning about this (see `MatrixEngine::begin`), but the pin map itself has not been
updated/verified against physical ESP32-S3 wiring yet. If you are wiring a 256x64 panel on ESP32-S3,
double-check your specific board's available GPIOs before trusting the default map, and consider
remapping `A`/`B` (and re-testing) to GPIOs outside the 33-37 range.

### ESP32-S3 GPIO Availability Reference

This table summarizes which GPIOs are safe to use for HUB75 signals on an ESP32-S3 module, depending
on the PSRAM mode. Always double-check against your specific board's datasheet, since some devkits
also wire additional GPIOs to onboard peripherals (USB, buttons, RGB LED, etc.).

| GPIO range | Status | Notes |
|------------|--------|-------|
| 0, 3, 45, 46 | **Reserved (strapping pins)** | Used at boot for mode selection; avoid driving these directly. |
| 19, 20 | Reserved (USB) | Native USB D-/D+ on most S3 devkits. |
| 26-32 | **Reserved (Quad Flash/PSRAM)** | Always reserved on ESP32-S3, regardless of PSRAM mode. |
| 33-37 | **Reserved only in Octal ("opi") PSRAM mode** | Free to use if your module has no PSRAM or uses Quad ("qio") PSRAM instead. **Conflicts with the current firmware's pin A (33) and is adjacent to pin B (32) when using octal PSRAM** — see the warning above. |
| 1-2, 4-18, 21, 38-48 | Generally free | Recommended pool to remap `A`/`B` (and any other conflicting signal) away from 33-37 when using octal PSRAM. |

**Recommended action for 256x64 (octal PSRAM) builds on ESP32-S3:** remap pins `A` and `B` in
`MatrixEngine.cpp`'s `_pins` struct to two GPIOs from the "generally free" pool above (e.g. 38/39),
rewire accordingly, and remove/validate the runtime warning once confirmed working on real hardware.
This has not been done yet in this codebase — treat the default S3 256x64 wiring as **unverified**.

