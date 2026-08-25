# ADR-0003: Physical Wiring & Bus Pin-Maps Frozen

## Status
Accepted

## Context
The pin mappings, bus modes (HUB75 DMA, SD vs SD_MMC 1-bit, I2C, I2S ES7210 codec clocking), and initialization sequences for both `ESP32_STD` and `WAVESHARE_S3` profiles are tested and validated in hardware production.

Any inadvertent pin re-assignment, timing modification, or bus refactoring introduces severe risk of hardware malfunction, display corruption, or SD read panics.

## Decision
1. **Freeze all pin definitions and bus initialization in `include/HardwareProfile.h`**:
   - HUB75 pin definitions for both profiles.
   - SD SPI and SD_MMC 1-bit configurations.
   - I2C pins (`SDA=47, SCL=48` on S3, `SDA=21, SCL=22` on standard).
   - Audio I2S pin assignments and ES7210 codec mappings.
2. Mark all profile sections in `HardwareProfile.h` with:
   `// WIRING VALIDÉ & TESTÉ — NE PAS MODIFIER`
3. All future hardware expansions (e.g. Gyroscope) must utilize the existing shared I2C bus (`0x68` / `0x69`) without creating new GPIO dependencies.

## Consequences
- Guarantees 100% hardware compatibility across future refactorings.
- Hardware abstraction layers focus strictly on detection and capability querying without altering bus configurations.
