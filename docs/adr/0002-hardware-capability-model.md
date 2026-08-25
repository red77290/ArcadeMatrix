# ADR-0002: Hardware Capability Model & Requirement Gating

## Status
Accepted

## Context
ArcadeMatrix targets diverse ESP32 hardware variants:
- Classic ESP32 (`ESP32_STD` / `esp32dev`): No PSRAM (~320KB RAM), standard SPI SD card, basic matrix resolutions (32px).
- Waveshare ESP32-S3 (`WAVESHARE_S3` / `esp32s3_waveshare`): 16MB Octal PSRAM, SD_MMC (1-bit), ES7210 I2S audio codec & microphone, SHTC3 temperature sensor, higher matrix resolutions (64px).

Previously, hardware detection was fragmented: `#ifdef BOARD_HAS_PSRAM`, `psramFound()`, and direct `ESP.getPsramSize()` calls were scattered across engine implementations (`CryptoEngine`, `StockEngine`, `GifEngine`, `MarqueeEngine`, `FighterEngine`).

## Decision
1. Establish a strict two-tier truth model:
   - **Compile-Time (`HardwareProfile.h`)**: Board identity (`HwProfile`), pin mappings, bus modes.
   - **Runtime (`HardwareHAL`)**: Single source of truth for peripheral presence (`HardwareCapabilities`: `hasPsram`, `psramBytes`, `hasMicrophone`, `hasTempSensor`, `hasGyroscope`).
2. Prohibit scattered `psramFound()` / `#ifdef` checks in engine source code.
3. Separate engine **Capabilities** (what an engine can do: `realtime`, `supports_128x32`, `allowsOverlay`, `selfPaced`) from **Requirements** (what an engine needs to function: `needsPsram`, `needsAudio`, `needsTempSensor`, `needsGyroscope`, `needsNetwork`, `needsSd`).
4. Centralize requirement gating in `EngineRegistrar::meetsRequirements()`. If hardware requirements are not met, the engine is skipped at registration and a descriptive reason is provided to `/api/engines` and the WebUI.
5. Adaptive engines (e.g. `FighterEngine` selecting 32px vs 64px, `GifEngine` selecting buffering vs streaming) query `hardwareHAL.capabilities()` dynamically.

## Consequences
- Single location for hardware detection.
- UI automatically greys out unsupported engines with actionable reasons ("Requires PSRAM", "Requires microphone").
- Safe execution on memory-constrained devices without risks of Out-Of-Memory (OOM) panics.
