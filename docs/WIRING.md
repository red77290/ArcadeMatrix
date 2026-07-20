# Wiring Guide

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

*Note: Pin E is only required if your matrix is 64 pixels tall (e.g., 1/32 scan rate).*

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
