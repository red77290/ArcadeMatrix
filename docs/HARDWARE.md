# Hardware Requirements

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

## Matrix Hardware
- **Type:** HUB75 / HUB75E RGB LED Matrix Panels (P2, P2.5, P3, P4, P5).
- **Driver Chips:** Compatible with standard shift registers (FM6126A, ICN2038S, etc.).
- **Power Supply:** A dedicated 5V power supply is required. A 64x32 matrix can draw up to 4 Amps at full white brightness. **Do not power the matrix directly from the ESP32!**
