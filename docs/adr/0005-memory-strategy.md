# ADR-0005: Embedded Memory Management & PSRAM Strategy

## Status
Accepted

## Context
ESP32 standard boards offer ~320KB of internal SRAM shared between Wi-Fi stack, AsyncTCP network buffers, FreeRTOS tasks, and HUB75 DMA descriptors. The Waveshare ESP32-S3 board adds 16MB of Octal PSRAM.

Careless allocation in internal SRAM (e.g. allocating static full-screen PNG decoding structs or unbounded JSON strings) leads to heap fragmentation and network socket allocation failures (`AsyncTCP failed to start task`).

## Decision
1. **Lazy & Dynamic Allocation**:
   - Icon bitmaps use a 3-level cache (`IconService`): L1 in-memory RGB565 bitmaps, L2 SD card cache, and L3 lightweight JPEG proxy (`images.weserv.nl` + `JPEGDEC` requiring only ~2.5 KB RAM, eliminating heavy `PNGdec` ~38 KB allocations).
   - `GifEngine` prioritizes PSRAM (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`) when available and falls back to word-aligned internal SRAM (`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`) on non-PSRAM boards.
   - Large GIF files are buffered in PSRAM if free PSRAM > file size + 500KB headroom; otherwise, streamed chunk-by-chunk from SD.
2. **Network Payloads & TLS Admission**:
   - JSON parsing uses bounded `StaticJsonDocument` or `SpiRamJsonDocument` with explicit capacity limits and zero-allocation streaming parsers.
   - All TLS handshakes are serialized globally via `NetworkBudget::ScopedTlsHandshakeLock` and admitted only when internal DRAM headroom (`canStartTlsSession()`) is verified.
3. **Dynamic Pipeline & PSRAM Gating**:
   - Network TLS engines (`CryptoEngine`, `StockEngine`, `DashboardEngine`) run on both `WAVESHARE_S3` and classic `ESP32_STD` (`needsPsram = false`) by dynamically downshifting presentation color depth to 4-bit via `PipelineSelectionPolicy` and executing `prefetchData()` (`fetchCombined()`) inside the DMA-released transition window.
   - Audio playback engines requiring persistent streaming buffers (`music_player`) declare `needsPsram = true` and `needsAudioOutput = true`.
   - `FighterEngine` limits sprite scaling to 32px on non-PSRAM devices to keep frame buffers under 32KB.

## Consequences
- Guaranteed stability and zero OOM crashes on non-PSRAM ESP32 devices while preserving full Crypto, Stock, and Dashboard functionality.
- High-resolution (64px) asset rendering and audio streaming enabled automatically on S3+PSRAM hardware.
