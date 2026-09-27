# Wiring Guide & Hardware Integration Pitfalls

🇬🇧 English | 🇫🇷 [Français](WIRING_FR.md) | 🇪🇸 [Español](WIRING_ES.md)

Wiring a HUB75 LED matrix and storage peripherals to an ESP32 requires absolute precision. Because HUB75 uses continuous DMA transfers and the ESP32 multiplexes GPIOs through a flexible Pin Matrix, improper pin assignments or library interactions can cause severe bus collisions, SD mount failures, or watchdog bootloops.

ArcadeMatrix officially supports two distinct hardware profiles:
1. **Standard ESP32 (`esp32dev`)** — Standard ESP32-WROOM / ESP32-D0WD (DMDos Board V3 / RetroPixelLED hardware).
2. **Waveshare ESP32-S3 RGB Matrix (`esp32s3_waveshare`)** — ESP32-S3 with 16MB Octal PSRAM + 32MB Flash.

---

## 1. Standard ESP32 (`esp32dev` / DMDos Board V3)

This profile is designed for classic ESP32 boards with 320 KB internal SRAM, using standard HUB75 DMA and a MicroSD card connected via the VSPI bus.

### 1.1 HUB75 LED Matrix Pinout

| HUB75 Signal | ESP32 GPIO | Description | Hardware Notes |
| :--- | :--- | :--- | :--- |
| **R1** | 25 | Red Top | Data line |
| **G1** | 26 | Green Top | Data line |
| **B1** | 27 | Blue Top | Data line |
| **R2** | 14 | Red Bottom | Data line |
| **G2** | 12 | Green Bottom | ⚠️ *Strapping pin (MTDI). Must not be pulled HIGH during boot on 3.3V flash boards.* |
| **B2** | 13 | Blue Bottom | Data line |
| **A** | 33 | Row Address A | Address line |
| **B** | 32 | Row Address B | Address line |
| **C** | 22 | Row Address C | Address line |
| **D** | 17 | Row Address D | Address line |
| **E** | **GND / -1** | Row Address E | **32px panels (1/16 scan): Connect to GND.** For 64px panels: GPIO 21. |
| **LAT (STB)**| 4 | Latch | Clock latch |
| **OE** | 15 | Output Enable | Active low PWM blanking. ⚠️ *Strapping pin (MTDO).* |
| **CLK** | 16 | Matrix Clock | Parallel DMA clock |

> [!CAUTION]
> **CRITICAL PITFALL: NEVER ASSIGN HUB75 PIN E TO GPIO 18!**
> GPIO 18 is strictly dedicated to the MicroSD SPI Clock (`VSPI_SCK`).
> On standard 64x32 panels (1/16 scan), pin `E` is **not used** and must be tied to **GND** on the panel side (software passes `-1`).
> Assigning pin E to GPIO 18 will hijack the SPI clock line and completely disable the MicroSD card!

### 1.2 MicroSD Card Pinout (VSPI Bus)

The MicroSD reader uses the dedicated VSPI hardware bus:

| SD Pin | ESP32 GPIO | VSPI Signal | Wiring Notes |
| :--- | :--- | :--- | :--- |
| **CS** | 5 | Chip Select | Managed via software GPIO (active LOW). Must be pulled HIGH on boot. |
| **CLK / SCK**| 18 | SPI Clock | Up to 25 MHz SPI clock. **Do NOT share with HUB75.** |
| **MISO / DO**| 19 | Data Out (Card -> ESP32) | Requires pull-up (`INPUT_PULLUP`). |
| **MOSI / DI**| 23 | Data In (ESP32 -> Card) | SPI master data output. |
| **VCC** | 3.3V / 5V | Power Supply | Connect to clean 3.3V (or 5V if module has onboard 3.3V LDO regulator). |
| **GND** | GND | Common Ground | Must be tied to ESP32 ground reference. |

### 1.3 Control Interfaces & Peripherals

| Component | ESP32 GPIO | Description |
| :--- | :--- | :--- |
| **Button (PIN)** | 21 | Multifunction push button (Short click: navigate / Long press: select). Tied to GND. |
| **IR Receiver** | 34 | NEC protocol infrared demodulator (Input-only GPIO). |
| **I2C SDA** | 21 | Optional sensor bus (shared with button when I2C is active). |
| **I2C SCL** | 22 | Optional sensor bus (conflicts with Matrix C line on classic wiring). |

---

## 2. Technical Traps & Architectural Rules for ESP32 MicroSD

If you modify HAL initialization or storage code, respect these 6 anti-regression invariants:

### 🔴 Trap 1: Hardware CS Hijacking via `SPI.begin()`
In the Arduino-ESP32 framework (`esp32-hal-spi.c`), passing `SD_CS_PIN` (5) as the 4th argument of `SPI.begin(sck, miso, mosi, ss)` automatically calls `pinMatrixOutAttach(ss, SPI_SS_IDX, ...)`. This connects GPIO 5 to the hardware SPI controller's CS0 line.
**However**, the `greiman/SdFat` library controls CS in software via `digitalWrite(m_csPin, level)`. Once mapped to `SPI_SS_IDX`, standard GPIO writes **cannot pull the pin LOW**. The SD card is never selected, resulting in `code=0x1 (CMD0 timeout), data=0xFF`.
- **Fix:** Always initialize SPI with `SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, -1)` and explicitly detach the CS pin: `pinMatrixOutDetach(SD_CS_PIN, false, false)`.

### 🔴 Trap 2: Missing `USER_SPI_BEGIN` in `SdFat`
In `SdFat` ([`SdSpiArduinoDriver.h`](file:///Users/red1l/Documents/work/git/perso/ArcadeMatrix/.pio/libdeps/esp32dev/SdFat/src/SdCard/SdSpiCard/SpiDriver/SdSpiArduinoDriver.h)), if `spiConfig.options` does not include `USER_SPI_BEGIN`, the driver calls `m_spi->begin()` without arguments, resetting the SPI peripheral with default framework pins.
- **Fix:** Pass `SdSpiConfig(SD_CS_PIN, SHARED_SPI | USER_SPI_BEGIN, SD_SCK_MHZ(f), &SPI)`.

### 🟠 Trap 3: Floating MISO Line
Many low-cost passive MicroSD breakouts lack onboard pull-up resistors on the DAT0/MISO line. During card initialization and tri-state periods, a floating MISO reads continuous `0xFF`.
- **Fix:** Explicitly enable the ESP32 internal pull-up: `pinMode(VSPI_MISO, INPUT_PULLUP)`.

### 🟠 Trap 4: SD-to-SPI Mode Transition (74 Clock Cycles)
At power-on, all SD cards start in native SD Bus mode. To switch the internal state machine to SPI mode, the host must clock at least 74 cycles (20 dummy bytes of `0xFF` at 1 MHz) with `CS = HIGH` before asserting CS LOW and sending `CMD0`.
- **Fix:** Send 20 dummy bytes via `SPI.transfer(0xFF)` with CS held HIGH before calling `sd.begin()`.

### 🔴 Trap 5: Watchdog Expiration During Multi-Frequency Fallback (TG1WDT ~9.2s)
The ESP32 bootloader hardware watchdog (TG1WDT) has a hardware timeout of approximately 9.2 seconds.
When `sd.begin()` attempts a 4-step frequency ladder (25 -> 16 -> 10 -> 4 MHz), each failed attempt waits up to 2000 ms (`SD_INIT_TIMEOUT`). If four attempts fail synchronously, 8.0 seconds elapse. With boot overhead, the total time reaches ~9.2s, triggering a hardware `TG1WDT_SYS_RESET` and a reboot loop.
- **Fix:** Always feed the watchdog (`esp_task_wdt_reset()`) between frequency attempts, and keep the frequency ladder lean.

### 🔴 Trap 6: Monolithic NVS Partition Format Reset
Calling `nvs_flash_erase()` synchronously on a corrupt NVS partition disables CPU cache and halts FreeRTOS interrupts. On large flash partitions, this exceeds the Interrupt Watchdog Timer (IWDT) threshold, triggering `TG1WDT_SYS_RESET`.
- **Fix:** Format the NVS partition sector-by-sector in 4 KB chunks with `esp_task_wdt_reset()` and `delay(5)` per sector.

---

## 3. Waveshare ESP32-S3 Matrix Board (`esp32s3_waveshare`)

The Waveshare ESP32-S3 board (N32R16: 32MB Flash + 16MB Octal PSRAM) features factory-integrated routing and uses the native high-speed 1-bit `SD_MMC` bus.

### 3.1 Official Waveshare S3 Pinout

| HUB75 Signal | ESP32-S3 GPIO | SD_MMC Signal | ESP32-S3 GPIO | Onboard Peripherals | ESP32-S3 GPIO |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **R1** | 4 | **D0 (DAT0)** | 17 | **I2C SDA** | 47 (SHTC3 @ 0x70, ES7210 @ 0x40) |
| **G1** | 5 | **CMD** | 44 | **I2C SCL** | 48 |
| **B1** | 6 | **CLK** | 1 | **I2S MCLK** | 12 |
| **R2** | 7 | | | **I2S SCLK** | 43 |
| **G2** | 15 | | | **I2S LRCK** | 38 |
| **B2** | 16 | | | **I2S ASDOUT** | 39 |
| **A** | 18 | | | **DAC Out (ES8311)**| 21 |
| **B** | 8 | | | | |
| **C** | 3 | | | | |
| **D** | 42 | | | | |
| **E** | 9 | | | | |
| **LAT** | 40 | | | | |
| **OE** | 2 | | | | |
| **CLK** | 41 | | | | |

### 3.2 Technical Traps & Architectural Rules for Waveshare S3

### 🔴 Trap 1: The Octal OPI PSRAM Exclusion Zone (GPIO 33 to 37)
The 16 MB high-speed Octal PSRAM requires 8 data lines and a differential clock. On the ESP32-S3 N32R16, **GPIO 33, 34, 35, 36, and 37 are permanently consumed by the internal PSRAM interface**.
- **Rule:** NEVER assign or toggle GPIO 33-37 in software. Any read, write, or peripheral attachment to these pins causes an immediate unrecoverable kernel panic.

### 🔴 Trap 2: Native SD_MMC vs SPI
The Waveshare S3 board MicroSD slot is hardwired directly to the ESP32-S3 SDMMC host controller (GPIO 1, 44, 17). It does **NOT** support SPI communication.
- **Rule:** Always compile with `USE_SD_MMC = 1` and initialize via `SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN)`. Never link `SdFat` on the S3 target.

### 🟠 Trap 3: VFS Fat Context in Fast DRAM (`max_files=3`)
When initializing `SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 3)`, the parameter `max_files=3` is critical. It guarantees that the FatFs filesystem context (~1.7 KB) allocates into internal SRAM rather than spilling into external PSRAM. This prevents cache line eviction stalls against the HUB75 GDMA engine reading framebuffers from PSRAM.

---

## 4. Power Supply Guidelines (All Boards)

- **Dedicated 5V Power Supply:** An active 64x32 RGB LED matrix can draw up to 4.0A at peak white brightness.
- **Common Ground:** Always connect the ground line of the external 5V power supply directly to the ESP32 ground pin (`GND`).
- **Never Power Panels via USB:** USB ports typically supply 500mA–1000mA, which will brown out the ESP32 and cause corrupted SD writes or watchdog crashes.
