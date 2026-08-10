# ArcadeMatrix Web Installer

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

This folder is the source for a static [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
flashing page, published to GitHub Pages by `.github/workflows/build.yml`'s `deploy-pages` job
(runs on every push to `main`, after both firmware builds succeed).

## How it works

- `index.html` embeds two `<esp-web-install-button>` widgets (one per supported board), each
  pointing at a small `manifest-*.json` describing which binary goes at which flash offset.
- The actual firmware binaries (`firmware-esp32dev.bin`, `bootloader-esp32dev.bin`,
  `partitions-esp32dev.bin`, and the `esp32s3` equivalents) are **not** committed here - they are
  fresh build artifacts, copied into the published site by CI from `.pio/build/<env>/`. This keeps
  the git history free of binary churn on every commit while always serving the latest `main`
  build to visitors.
- `bin/boot_app0.bin` **is** committed - it's a tiny (8KB), fixed file provided by the
  Arduino-ESP32 framework (selects which OTA app partition to boot) and is identical for every
  build, so there's no reason to regenerate/re-host it per build.

## Flash offsets (Arduino-ESP32 default partitioning)

| File | ESP32 (classic) | ESP32-S3 |
|---|---|---|
| bootloader | `0x1000` | `0x0` (S3 ROM bootloader header differs) |
| partitions | `0x8000` | `0x8000` |
| boot_app0  | `0xE000` | `0xE000` |
| firmware   | `0x10000` | `0x10000` |

These match exactly what `pio run -t upload -v` invokes under the hood (verified locally with
`esptool.py ... write_flash`), so a browser-based flash via this page and a `pio run -t upload`
flash produce an identical result.

## Testing locally

Open `index.html` via a local static file server (not `file://`, WebSerial needs a real origin)
after copying real binaries into this folder to match the manifest paths, e.g.:

```bash
pio run -e esp32dev -e esp32s3_waveshare
cp .pio/build/esp32dev/bootloader.bin webinstaller/bootloader-esp32dev.bin
cp .pio/build/esp32dev/partitions.bin webinstaller/partitions-esp32dev.bin
cp .pio/build/esp32dev/firmware.bin   webinstaller/firmware-esp32dev.bin
cp .pio/build/esp32s3_waveshare/bootloader.bin  webinstaller/bootloader-esp32s3_waveshare.bin
cp .pio/build/esp32s3_waveshare/partitions.bin  webinstaller/partitions-esp32s3_waveshare.bin
cp .pio/build/esp32s3_waveshare/firmware.bin    webinstaller/firmware-esp32s3_waveshare.bin
python3 scripts/validate_webinstaller.py
cd webinstaller && python3 -m http.server 8080
```

Then visit `http://localhost:8080` in Chrome/Edge with a board connected over USB.
