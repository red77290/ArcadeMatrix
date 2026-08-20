# Getting Started (ESP32 firmware, first-time PlatformIO setup)

🇬🇧 English | 🇫🇷 [Français](GETTING_STARTED_FR.md) | 🇪🇸 [Español](GETTING_STARTED_ES.md)

This guide is for developers who have never used [PlatformIO](https://platformio.org/) before and
want to build, flash, and debug the ArcadeMatrix firmware locally. For hardware wiring, see
`docs/HARDWARE.md`/`docs/WIRING.md`; for `conf.ini` options, see `docs/CONFIGURATION.md`; for the
codebase architecture, see `docs/ARCHITECTURE.md`; for contribution workflows (adding clocks,
REST endpoints, custom fonts), see `docs/DEVELOPER.md`.

## 1. Install PlatformIO

You only need **one** of these - pick whichever fits your workflow:

- **VS Code extension (recommended for beginners)**: install
  [Visual Studio Code](https://code.visualstudio.com/), then install the
  [PlatformIO IDE extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
  from the Extensions marketplace. It bundles its own Python/toolchain - no separate install needed.
- **CLI only** (works in any terminal, editor-agnostic):
  ```bash
  pip install -U platformio
  # or, on macOS with Homebrew:
  brew install platformio
  ```
  Verify it works: `pio --version`.

All commands below use the `pio` CLI - if you're using the VS Code extension, the same actions
are available from the PlatformIO sidebar (build/upload/monitor icons) and produce identical
results; the CLI is shown here because it's copy-pasteable and works the same on any OS/editor.

## 2. Open the workspace

```bash
git clone <this-repo-url>
cd ArcadeMatrix
```

No `pio project init` needed - `platformio.ini` already exists at the repo root and defines both
supported boards as separate build environments: `esp32dev` (classic ESP32, 4MB flash) and
`esp32s3` (ESP32-S3, 32MB Flash + 16MB PSRAM (N32R16)). See `docs/HARDWARE.md` for which one matches
your board and its specific GPIO/resolution limits.

## 3. Build the firmware

```bash
# Build both environments (fastest way to sanity-check your changes):
pio run -e esp32dev -e esp32s3

# Or just the one you actually own:
pio run -e esp32dev
```

The first build downloads the Espressif toolchain and all libraries listed in `platformio.ini`'s
`lib_deps` (HUB75 DMA driver, AnimatedGIF, PNGdec, ESPAsyncWebServer, ArduinoJson, etc.) - this can
take a few minutes the first time, and is cached afterward in `~/.platformio/`. A successful build
prints a `RAM:`/`Flash:` usage summary and ends with `[SUCCESS]`.

## 4. Flash it to your board

Connect the ESP32/ESP32-S3 via USB, then:

```bash
pio run -e esp32dev -t upload      # replace esp32dev with esp32s3 if that's your board
```

PlatformIO auto-detects the serial port in most cases. If it picks the wrong one (e.g. you have
multiple USB-serial devices connected), specify it explicitly:

```bash
pio device list                     # find the right port name
pio run -e esp32dev -t upload --upload-port /dev/ttyUSB0   # Linux/macOS example
pio run -e esp32dev -t upload --upload-port COM5           # Windows example
```

### Flashing a pre-built release

If you don't want to build from source, download `ArcadeMatrix-esp32dev.zip` or
`ArcadeMatrix-esp32s3_waveshare.zip` from the [latest release](https://github.com/red77290/ArcadeMatrix/releases/latest)
instead — `ArcadeMatrix-esp32s3_waveshare.zip` contains `firmware-esp32s3_waveshare.bin`, `bootloader-esp32s3_waveshare.bin`, `partitions-esp32s3_waveshare.bin` and `boot_app0.bin` for the Waveshare ESP32-S3 board. Flash all four with `esptool.py` at the offsets used
by the Arduino-ESP32 default partitioning (same offsets the browser-based Web Installer uses -
see `webinstaller/README.md`):

```bash
pip install esptool
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash \
  0x1000  bootloader-esp32dev.bin \
  0x8000  partitions-esp32dev.bin \
  0xE000  boot_app0.bin \
  0x10000 firmware-esp32dev.bin
```

For `esp32s3`, use `--chip esp32s3` and offset `0x0` for the bootloader instead of `0x1000` (the
S3's ROM bootloader header differs) - see the flash offset table in `webinstaller/README.md` for
the full breakdown.

## 5. Read the serial logs

The firmware logs boot progress, Wi-Fi status, SD card mount results, heap usage, and runtime
errors/warnings over serial at `115200` baud (see `monitor_speed` in `platformio.ini`):

```bash
pio device monitor -e esp32dev -b 115200
```

Press `Ctrl+C` to exit. To reboot the board and see the boot logs, press `Ctrl+T` followed by `Ctrl+R` in the monitor. Combine build+flash+monitor in one command for a fast dev loop:

```bash
pio run -e esp32dev -t upload && pio device monitor -e esp32dev -b 115200
```

## 6. Prepare the SD card

The firmware needs an external SD card (wired per `docs/WIRING.md`, chip-select on GPIO 5 by
default - see `SD_CS_PIN` in `src/main.cpp`) for:
- `/conf.ini` — your Wi-Fi/matrix/theme settings (auto-generated with defaults on first boot if
  missing; edit it directly on the card, or via the web UI's `/api/settings` once Wi-Fi is up).
- `/gifs/`, playlists of `.gif`/`.raw`/`.png` assets (see `docs/ARCHITECTURE.md` §4 for the format
  differences between the three).
- `/fighters_32/` or `/fighters_64/` — MUGEN-derived `.fgt` sprite sheets (see
  `tools/mugen_extractor/README.md` to generate your own from MUGEN character files).
- Optionally `/fonts/*.amf` — custom SD-loadable bitmap fonts (see `docs/DEVELOPER.md`'s
  "Loading a custom bitmap font from SD" section and `tools/bdf_to_amfont/`).

A FAT32-formatted card is required (standard for cards up to 32GB; larger cards may need to be
reformatted from exFAT to FAT32).

## 7. Running the test suite

```bash
pio test -e esp32dev
```

**Important caveat**: `test/test_config/test_config.cpp` is an **on-target** Unity test - it
compiles against the real ESP32 Arduino core (`WiFi.h`, `FS.h`, etc.) and must be **uploaded to a
physical board** to execute (PlatformIO flashes it, then reads pass/fail results back over serial).
There is currently no hardware-independent ("native"/host) test target for this firmware - see
`docs/ARCHITECTURE.md` and `docs/DEVELOPER.md` for why (the codebase leans on ESP32-specific APIs
like `SD.h`/`WiFi.h` throughout, which don't have drop-in desktop equivalents without a larger
mocking effort). This is also why CI (`.github/workflows/build.yml`) only **compiles** the test
target (`pio test -e <env> --without-uploading --without-testing`) rather than executing it -
GitHub Actions runners don't have a physical ESP32 attached, but a compile-only pass still catches
build regressions (stale includes, broken signatures, etc.) on every push/PR. If you have a board
connected locally, plain `pio test -e esp32dev` (no flags) is the right command to actually flash
and run it.

## Troubleshooting

- **`pio: command not found`** after `pip install`: your Python scripts directory isn't on `PATH`.
  Either use the VS Code extension instead, or add the printed `pip show -f platformio` bin path to
  your shell profile.
- **Upload fails / times out**: hold the board's `BOOT`/`IO0` button while the upload starts (some
  ESP32 dev boards need this to enter the bootloader), or lower `upload_speed` in `platformio.ini`.
- **Build fails with a missing library error**: delete `.pio/` and rebuild - a corrupted library
  cache is the most common cause (`rm -rf .pio && pio run -e esp32dev`).
- **`sdWait Failed` / `sdSelectCard Failed` / `Check status failed` a few times right at boot, then the firmware continues normally (Wi-Fi connects, NTP time displays correctly)**: this is benign - these are just internal attempts by the ESP32 SD driver during its initialization handshake (common with some SD cards/brands at default polling speeds), not a real mount failure. If the SD actually failed, `setup()` would print `CRITICAL ERROR: SD Card Mount Failed!` and hang indefinitely (rebooting via watchdog every ~30s) - it would never reach the Wi-Fi connection step. Only investigate your wiring/power if you see that exact critical error, or if SD reads/writes continue to fail long after boot (not just at the very beginning). The `does not exist, no permits for creation` errors right after are normal/harmless on first boot (e.g., `playlists_selected.json`, `fighters_32/index.txt` simply don't exist until you save a playlist / run `mugen_extractor`). Consult the "SD Card Wiring" table in `docs/WIRING.md` only if you are wiring a brand new board from scratch.
- **`AsyncTCP.cpp: begin(): failed to start task`** right after Wi-Fi connection: FreeRTOS cannot allocate a task for the asynchronous TCP stack, almost always due to low free internal heap (the large HUB75 DMA buffers on a non-PSRAM ESP32 can consume most of it). Check `ESP.getFreeHeap()` (printed after matrix init) - if only a few KB remain, reduce `mxconfig.min_refresh_rate`/panel resolution, or switch to an ESP32-S3 with PSRAM for large panels. The web server might partially start despite this warning, but expect it to be unstable until the free heap is resolved.
