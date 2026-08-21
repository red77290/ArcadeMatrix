🇬🇧 English | 🇫🇷 [Français](ARCHITECTURE_FR.md) | 🇪🇸 [Español](ARCHITECTURE_ES.md)

# Architecture Overview (ESP32 - C++)

This document provides a detailed overview of the ArcadeMatrix architecture on ESP32 developed in **C++**. It explains the hardware constraints, the dual-core threading strategy, the rendering pipeline, and the "Lazy-Once" lifecycle of the engines.

---

## 1. Core Philosophy: Hardware Constraints

Unlike the Raspberry Pi version, the ESP32 version is developed in **C++** and built around severe hardware constraints:
- **RAM limits and PSRAM:** The core architecture is optimized to run on the lowest common denominator (a standard ESP32 with ~320 KB of free RAM). However, ArcadeMatrix fully supports advanced boards like the **ESP32-S3** with PSRAM (up to 16 MB). *Warning:* some highly memory-intensive engines (like `CryptoEngine` and `StockEngine` which store large history charts and parse huge JSON API payloads) **strictly require PSRAM** to function. Engines that fit within the 320 KB use it, while those requiring PSRAM will fail if enabled on a standard ESP32. Heap fragmentation remains our biggest enemy, hence the importance of the controlled lifecycle.
- **CPU Constraints (240 MHz):** To maintain 60 FPS on the matrix, rendering must be extremely fast.
- **Direct DMA Access:** Drawing primitives are written directly into the I2S DMA hardware buffer without an intermediate operating system.

---

## 2. The "Lazy-Once" Lifecycle & Memory Strategy

To avoid kernel panics caused by heap fragmentation over time, the C++ architecture relies on a strict **Lazy-Once** lifecycle model via a Factory pattern.

```mermaid
graph TD
                 Registry[Engine Registry]
                       │
                 Descriptor[EngineDescriptor]
                       │
                Factory[Lambda Factory]
                       │
                 Instance[IEngine (std::unique_ptr)]
                       │
              ┌────────┴────────┐
              │                 │
       Context[ApplicationContext] Config[DictionaryEngineConfig]
              │                 │
              └────────┬────────┘
                       │
                 Manager[RotationManager]
                       │
          ┌────────────┼────────────┐
          │            │            │
       activate      update       render
          │            │            │
          └────────────┼────────────┘
                       │
                  deactivate
```

### Phase Explanation (C++):

1. **`initialize()` (Allocation):**
   * Called *exactly once* the first time the engine needs to be displayed.
   * This is the **ONLY** place where `new`, `std::vector`, or `String` allocations should occur. You must pre-allocate all necessary memory here.
2. **`activate()` (Temporary Preparation):**
   * Called every time the engine becomes active. Resets timers or temporary state without allocating memory.
3. **`update()` & `render()` (Hot Loop - 60 FPS):**
   * **Constraint:** **STRICTLY NO UNNECESSARY DYNAMIC ALLOCATION.** Do not use `String` concatenation, do not call `malloc`. Mutate pre-allocated arrays.
4. **`deactivate()` (Standby):**
   * Frees temporary network connections or stops listening.

### Why the Factory Pattern?
If we instantiated all C++ engines globally at boot, the 320 KB heap would be instantly exhausted. The Registry stores lightweight `EngineDescriptor` objects containing a lambda function (`factory`). The `RotationManager` only calls this factory when an engine is first scheduled to appear, keeping inactive engines out of RAM.

---

## 3. Dual-Core Architecture

The ESP32 is a dual-core microcontroller. ArcadeMatrix exploits this to separate concerns:

1. **Core 1 (App Core): Rendering Loop**
   - The `loop()` function runs here.
   - Responsible for calling `update()` and `render()` at exactly 60 FPS.
   - Pushes pixels via the I2S DMA. **Must never block.**

2. **Core 0 (Pro Core): Network & API**
   - Handles the Web Server, REST API, Wi-Fi connections, and asynchronous tasks.
   - Modifying the configuration via the API (Core 0) updates the `DictionaryEngineConfig` safely while Core 1 continues rendering.

---

## 4. Hardware HAL (I2C / I2S Isolation)

Because the ESP32 shares pins and buses, the `HardwareHAL` abstracts the hardware layer:
- It manages the I2C bus (for RTC, sensors) and ensures thread safety.
- It prevents collisions between the SD card (SPI/SDMMC) and the LED Matrix DMA (I2S).
