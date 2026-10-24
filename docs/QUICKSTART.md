# Quickstart Guide (ESP32)

🇬🇧 English | 🇫🇷 [Français](QUICKSTART_FR.md) | 🇪🇸 [Español](QUICKSTART_ES.md)

## 1. Prerequisites
- A formatted **FAT32** or **exFAT** microSD card.
- USB cable connected to your ESP32 / ESP32-S3 board.
- Either Chrome / Edge / Opera for the **[One-Click Web Installer](https://red77290.github.io/ArcadeMatrix/)**, or [VSCode](https://code.visualstudio.com/) + [PlatformIO IDE Extension](https://platformio.org/) to build from source.

---

## 2. Prepare the SD Card
1. Format your microSD card to **FAT32** or **exFAT**.
2. Copy the contents of the `release/sdCard/` folder to the root of the microSD card.
3. Insert the card into your board's SD slot.

---

## 3. Flash or Build Firmware

### Option A: One-Click Browser Flash (Fastest — No Software Needed)
Open **[https://red77290.github.io/ArcadeMatrix/](https://red77290.github.io/ArcadeMatrix/)** in Chrome, Edge, or Opera, select your board (**ESP32 Standard** or **ESP32-S3 Waveshare**), and click **Install**.

### Option B: Build & Flash from Source (PlatformIO)

#### For Standard ESP32 (`esp32dev`):
```bash
pio run -e esp32dev -t upload
```

#### For Waveshare ESP32-S3 (`esp32s3_waveshare`):
```bash
pio run -e esp32s3_waveshare -t upload
```

---

## 4. Connect to WebUI
1. On first boot, if Wi-Fi is not configured, ArcadeMatrix starts an Access Point:
   - **SSID**: `ArcadeMatrix-Setup`
   - **Password**: `matrix123`
2. Open your browser at `http://192.168.4.1` (or `http://ArcadeMatrix.local` once connected to your local Wi-Fi).
3. Configure your engines, themes, rotation, and enjoy!
