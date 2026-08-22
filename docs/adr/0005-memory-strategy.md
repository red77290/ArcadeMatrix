# ADR-0005: Embedded Memory Management & PSRAM Strategy

## Status
Accepted

## Context
ESP32 standard boards offer ~320KB of internal SRAM shared between Wi-Fi stack, AsyncTCP network buffers, FreeRTOS tasks, and HUB75 DMA descriptors. The Waveshare ESP32-S3 board adds 16MB of Octal PSRAM.

Careless allocation in internal SRAM (e.g. allocating static full-screen PNG decoding structs or unbounded JSON strings) leads to heap fragmentation and network socket allocation failures (`AsyncTCP failed to start task`).

## Decision
1. **Lazy & Dynamic Allocation**:
   - `PNGdec` structures (~38KB) are allocated dynamically only on actual PNG asset playback, freeing internal SRAM when unused.
   - `GifEngine` internal double-buffering canvas is placed in word-aligned internal DMA RAM (`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`) to prevent double-buffering tearing.
   - Large GIF files are buffered in PSRAM if free PSRAM > file size + 500KB headroom; otherwise, streamed chunk-by-chunk from SD.
2. **Network Payloads**:
   - JSON parsing uses bounded `StaticJsonDocument` or `DynamicJsonDocument` with explicit capacity limits.
   - Large network payloads (Crypto / Stock ticker quotes) use chunked/streamed JSON parsing rather than `http.getString()`.
3. **PSRAM Requirement Gating**:
   - Memory-heavy engines (e.g. full-featured `CryptoEngine` and `StockEngine` with historical caching) declare `needsPsram = true` and are skipped on classic ESP32.
   - `FighterEngine` limits sprite scaling to 32px on non-PSRAM devices to keep frame buffers under 32KB.

## Consequences
- Guaranteed stability and zero OOM crashes on non-PSRAM ESP32 devices.
- High-resolution (64px) asset rendering and deep quote caching enabled automatically on S3+PSRAM hardware.
