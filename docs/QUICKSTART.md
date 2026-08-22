# Quickstart Guide (ESP32)

🇬🇧 English | 🇫🇷 [Français](QUICKSTART_FR.md) | 🇪🇸 [Español](QUICKSTART_ES.md)

## 1. Prerequisites
- [VSCode](https://code.visualstudio.com/) + [PlatformIO IDE Extension](https://platformio.org/).
- A formatted FAT32 microSD card (1GB to 32GB).
- USB cable connected to your ESP32 board.

---

## 2. Prepare the SD Card
1. Format your microSD card to **FAT32**.
2. Copy the contents of the `sdcard_assets/` folder to the root of the microSD card.
3. Insert the card into your board's SD slot.

---

## 3. Build & Flash Firmware

### For Standard ESP32 (`esp32dev`):
```bash
pio run -e esp32dev -t upload
```

### For Waveshare ESP32-S3 (`esp32s3_waveshare`):
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
