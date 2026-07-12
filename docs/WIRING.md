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
| A         | 23         | Address A |
| B         | 19         | Address B |
| C         | 5          | Address C |
| D         | 17         | Address D |
| E         | 18         | Address E (For 64px tall panels) |
| LAT (STB) | 4          | Latch |
| OE        | 15         | Output Enable |
| CLK       | 16         | Clock |

*Note: Pin E is only required if your matrix is 64 pixels tall (e.g., 1/32 scan rate).*

## ESP32-S3 Wiring
If you are compiling for the ESP32-S3 (recommended for 256x64), the pinouts are dynamically mapped in the hardware definitions. Check `platformio.ini` or the default library mapping for your specific S3 board variant.
