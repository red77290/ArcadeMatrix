# Hardware Benchmarks & Performance Metrics (ESP32)

## 1. Frame Rate & Loop Timing

| Engine | Framerate Target | Average Loop Interval | CPU Core Allocation | Notes |
|---|---|---|---|---|
| **Clock (Animated/Themes)** | ~60 FPS | ~16 ms | Core 1 (App) | Smooth sprite animations & gradients |
| **Clock (Static / Word / Binary)** | ~20 FPS | ~50 ms | Core 1 (App) | Adaptive throttling to conserve power |
| **GIF Player** | Variable (GIF FPS) | 16–33 ms | Core 1 (App) | DMA Direct playback |
| **Fighter Overlay** | ~60 FPS | ~16 ms | Core 1 (App) | Additive render pass on top of clock |
| **Audio Visualizer** | ~60 FPS | ~16 ms | Core 1 (App) | Fast FFT + I2S Audio |
| **WebServer / REST API** | Asynchronous | < 5 ms latency | Core 0 (Pro) | AsyncTCP non-blocking handling |

---

## 2. Memory Consumption Footprint

| Environment | Internal Free SRAM at Boot | PSRAM Available | Minimum Free Heap under Load |
|---|---|---|---|
| **`esp32dev` (Standard)** | ~185 KB | 0 MB | > 110 KB (Safe) |
| **`esp32s3_waveshare`** | ~240 KB | 16 MB Octal | > 190 KB SRAM / 15.2 MB PSRAM |
