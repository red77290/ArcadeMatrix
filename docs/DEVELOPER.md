# Developer Guide (ESP32)

🇬🇧 English | 🇫🇷 [Français](DEVELOPER_FR.md) | 🇪🇸 [Español](DEVELOPER_ES.md)

Welcome to the ArcadeMatrix ESP32 development guide. This document explains how to extend the C++ project, specifically how to add a new Clock and how to expose it to the Web API.

---

## 1. Adding a New Specialized Clock

Thanks to ArcadeMatrix's hardware-coupled modular embedded architecture, adding a new clock means creating a dedicated display class under `src/engines/clocks/` that handles its own logic and direct rendering.

### Clock Theme Compatibility Matrix (IDs 0 to 29)

| ID | Theme / Clock | Class / File | 128x32 | 256x64 | Custom Font (.amf) | Custom Colors | Hardware Status |
|---|---|---|:---:|:---:|:---:|:---:|:---:|
| 0 | Nintendo | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 1 | Capcom | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 2 | Taito | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 3 | Sega | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 4 | Cave | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 5 | Konami | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 6 | SNK | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 7 | Technos | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 8 | IGS | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 9 | Hudson | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 10 | Banpresto | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 11 | Namco | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 12 | Ryu | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 13 | Mario | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 14 | Marco | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 15 | Megaman | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 16 | Bub | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 17 | Space Invaders | `ArcadeClock` | ✓ | ✓ | — | — | Hardware Validated |
| 18 | Cyberpunk | `CyberpunkClock` | ✓ | ✓ | — | — | Hardware Validated |
| 19 | FlipClock | `FlipClock` | ✓ | ✓ | — | — | Hardware Validated |
| **20** | **Custom Gradient** | `ClockEngine` | **✓** | **✓** | **✓** | **✓** | **Hardware Validated** |
| 21 | PongClock | `PongClock` | ✓ | ✓ | — | — | Hardware Validated |
| 22 | TetrisClock | `TetrisClock` | ✓ | ✓ | — | — | Hardware Validated |
| 23 | TetrisGameboy | `TetrisGameboyClock` | ✓ | ✓ | — | — | Hardware Validated |
| 24 | PacmanClock | `PacmanClock` | ✓ | ✓ | — | — | Hardware Validated |
| 25 | WordClock | `WordClock` | ✓ | ✓ | — | — | Hardware Validated |
| 26 | BinaryClock | `BinaryClock` | ✓ | ✓ | — | — | Hardware Validated |
| 27 | VersusClock | `VersusClock` | ✓ | ✓ | — | — | Hardware Validated |
| 28 | SlotMachineClock | `SlotMachineClock` | ✓ | ✓ | — | — | Hardware Validated |
| 29 | MatrixRainClock | `MatrixRainClock` | ✓ | ✓ | — | — | Hardware Validated |

---

### Step-by-Step

1. **Create the Header (`src/engines/clocks/MyClock.h`)**:
   ```cpp
   #pragma once
   #include <Arduino.h>
   #include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"
   #include "core/ConfigLoader.h"

   class MyClock {
   public:
       MyClock(MatrixPanel_I2S_DMA* display, ConfigLoader* config);
       void loop(); // Called every frame
   private:
       MatrixPanel_I2S_DMA* _display;
       ConfigLoader* _config;
   };
   ```

2. **Create the Implementation (`src/engines/clocks/MyClock.cpp`)**:
   ```cpp
   #include "MyClock.h"

   MyClock::MyClock(MatrixPanel_I2S_DMA* display, ConfigLoader* config) : _display(display), _config(config) {}

   void MyClock::loop() {
       if (!_display) return;
       _display->fillScreen(0);
       _display->drawPixel(10, 10, _display->color565(255, 0, 0));
   }
   ```

3. **Register the Clock in `ClockEngine.cpp`**:
   - Include your header at the top of `src/engines/ClockEngine.cpp`.
   - Instantiate your clock based on `clock_theme` selected by the user.
   - Dispatch the loop call inside `ClockEngine::loop()`.

---

## 2. Modifying the Web API & Configuration

If your new clock requires new user settings (e.g., `snake_speed`), you must modify the configuration pipeline from the frontend all the way to the struct.

1. **Update the Struct (`src/core/ConfigLoader.h`)**:
   Add your new variable to the main `Config` struct.
   ```cpp
   struct Config {
       // ... existing fields
       int snake_speed = 5;
   };
   ```

2. **Update the JSON Parser & Generator (`src/api/WebServerAPI.cpp`)**:
   The API communicates via JSON (`ArduinoJson`). 
   - Find the method that serializes the config to send to the browser and add: 
     `doc["snake_speed"] = config.snake_speed;`
   - Find the method that parses incoming JSON from the browser and add:
     `if (doc.containsKey("snake_speed")) config.snake_speed = doc["snake_speed"].as<int>();`

3. **Update the File System Loader (`src/core/ConfigLoader.cpp`)**:
   Ensure your new variable is read from and saved to the SD card's `conf.ini` file to survive reboots.

4. **Update the Web UI (`src/api/WebUI.h`, generated from the Vue frontend - see `scripts/build_webui.py`)**:
   - Add the HTML inputs for your setting.
   - Update the frontend JS to send your new variable in the JSON payload when the user clicks "Save".

### Notable REST endpoints (non-exhaustive)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/api/status` | GET | Uptime, free/min-free heap, PSRAM stats. |
| `/api/settings` | GET/POST | Full config read/write (persists to `conf.ini`). |
| `/api/wifi` | POST | `{ssid, password}` - saves credentials and attempts an immediate reconnect, reporting success/failure synchronously (does not require a reboot). |
| `/api/marquee` | POST | Raw RGB565 image body (little-endian, row-major, exactly `width*height*2` bytes matching the configured panel resolution - see `tools/mugen_extractor` for the same wire format convention). Displays it immediately for ~8s, interrupting the idle rotation, then resumes. There is no on-device image decoder, so any bridge/frontend integration must pre-convert artwork (PNG/JPEG/box-art) to this raw format before POSTing. |
| `/api/update` | POST | OTA firmware upload (`Update.h`), writes to the inactive OTA partition slot. |

### Pixelcade-style marquee/box-art integration (arcade cabinet frontends)

Unlike `ArcadeMatrix_RPi` (which downloads Pixelcade marquee artwork live from GitHub on demand,
see its `core/dmd_cache.py`), the ESP32 has no spare flash/RAM/CPU budget for an HTTPS client
fetching images mid-game, and no unbounded disk cache to grow over time. The recommended
architecture instead pre-caches all artwork **on the SD card ahead of time**, so displaying it at
runtime is just a fast local file lookup - no network involved at all when a game launches:

1. **One-time setup**: run `tools/pixelcade_sync/pixelcade_sync.sh` (macOS/Linux) or
   `pixelcade_sync.ps1` (Windows) on your PC (not the ESP32) to
   download Pixelcade's artwork repository and lay it out as `/pixelcade/<system>/<game>.png`.
   Copy the result to your SD card. See that tool's README for filtering to just the systems you
   use (the full repository is several hundred MB).
2. **One-time setup**: run `tools/recalbox_daemon/install.sh` (macOS/Linux) or `install.ps1`
   (Windows) from your PC to install a small event daemon on your Recalbox/Batocera device over
   SSH - prompts for both IPs, no manual SSH session needed. This is the *same* daemon protocol
   `ArcadeMatrix_RPi` uses (`core/ssh_installer.py`), so one install serves both projects.
3. At runtime, the daemon publishes `{"status": "playing"|"browsing"|"stopped", "game": "<rom
   basename>", "system": "<SystemId>"}` over MQTT on `recalbox/system/playing` (or
   `batocera/system/playing`) whenever the selected/playing game changes.
4. `RetroFrontendListener::handleGameEvent()` (firmware) parses this, maps the `SystemId` to a
   Pixelcade folder name (`mapSystemToPixelcadeFolder()` - kept in sync with the RPi's
   `dmd_cache.py` `SYSTEM_MAP`), and checks `/pixelcade/<folder>/<game>.png` on the SD card:
   - If found: displays it immediately via `gif->playGif()` (GifEngine's PNG decoder, added
     alongside GIF support - see `esp32-gif-png` in the changelog).
   - If not found (not yet synced, or Pixelcade has no art for that game): falls back to scrolling
     the game name as text via `MessageEngine`, matching the RPi's behavior when its cache misses.
   - On `"status": "stopped"`: calls `gif->stop()`, resuming the idle GIF/clock rotation.

This keeps 100% of image fetching/decoding/resizing off the runtime path entirely (done once,
offline, on a PC with real bandwidth and no memory constraints), matching the same "pre-convert
offline" philosophy as `.raw` GifEngine assets and `tools/mugen_extractor`.

**Legacy / alternative paths still supported:**
- `/api/marquee` (POST, raw RGB565 body) is still available for bridge scripts that want to push
  an arbitrary, non-Pixelcade image directly (e.g. a custom-generated marquee) rather than relying
  on the SD-cached Pixelcade lookup above.
- The native `/Recalbox/EmulationStation/Event` topic (`rungame`/`stop`) is still subscribed to as
  a basic fallback for setups that don't want to install the custom daemon, though it carries no
  game/system detail (Recalbox doesn't include it on that topic), so only a generic placeholder can
  be shown.
- The plain-text `STOP_GAME`/`START_GAME:<path>` bridge protocol from older setups still works too.

---

### Loading a custom bitmap font from SD (BitmapFontLoader)

By default all fonts are compiled into the firmware (`src/engines/fonts/`). To add a custom font
without a firmware rebuild:

1. Obtain or create a BDF bitmap font. Any of the RPi project's bundled fonts work as a starting
   point (`ArcadeMatrix_RPi/fonts/*.bdf`), or grab one from an X11/X BDF font archive.
2. Convert it to ArcadeMatrix's `.amf` format:
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py myfont.bdf myfont.amf
   ```
   By default this covers printable ASCII (`0x20`-`0x7E`); pass `--first`/`--last` to cover a
   different codepoint range if the BDF font has one (e.g. extended Latin-1).
3. Copy `myfont.amf` to the SD card, e.g. `/fonts/myfont.amf`.
4. Set `custom_font_path=/fonts/myfont.amf` under `[fonts]` in `conf.ini` (or via `/api/settings`).
5. Reboot. `BitmapFontLoader::loadFromSD()` parses the file into a heap-allocated `GFXfont`-
   compatible structure at boot and hands it to `MessageEngine` for the `/api/message` banner (the
   default 5x7 font is used as a silent fallback if the file is missing/corrupt).

**Format notes:** `.amf` mirrors Adafruit's own compiled-font layout exactly (byte-aligned
per-glyph bitmap offsets, MSB-first packed bits) — see the docstring in
`tools/bdf_to_amfont/bdf_to_amfont.py` for the exact binary layout, and `BitmapFontLoader.cpp`'s
parser for the corresponding ESP32-side reader. Fonts are limited to 65535 bytes of packed glyph
bitmap data (`GFXglyph.bitmapOffset` is a `uint16_t`) — the same ceiling Adafruit's own
`fontconvert` tool imposes on compiled-in fonts, not a new limitation.

---

## 3. Important Rules for ESP32 Development

- **Avoid `String` objects**: Use `char` arrays (`char[]`) whenever possible to avoid heap fragmentation, which is fatal on ESP32.
- **DMA Bounds**: Never draw outside the boundaries of `matrix_width` and `matrix_height`. Adafruit GFX handles most clipping, but direct memory writes will cause kernel panics.
- **Memory Leaks**: If you dynamically allocate classes (`new MyClock()`), ensure you `delete` them when the theme switches to prevent memory exhaustion.

---

## 4. Known Limitations & Security

### Security
- **Unauthenticated API**: The HTTP REST API and WebUI server execute without authentication on the local network. ArcadeMatrix is designed to run on a trusted private LAN. Do not expose port 80 directly to the public Internet without an authenticating reverse proxy.
- **CORS**: `Access-Control-Allow-Origin: *` headers are enabled by default for seamless control from local web applications.

### Known Limitations
- **No Automatic OTA Rollback**: The OTA firmware upload process writes to the secondary flash partition and flips the bootloader flag. If a buggy firmware boots but behaves incorrectly, there is no automatic rollback without physical re-flashing or WebSerial.
- **Synchronous Network Fetching in Providers**: While `AsyncWebServer` provides non-blocking HTTP handling, provider quote refreshes (`CryptoEngine`, `StockEngine`, `WeatherEngine`) execute network requests synchronously with local in-memory caching. In case of network failure, the last cached values are preserved without blocking the main display loop.
- **SD Card Required for GIF and MUGEN**: Playing animated GIFs or MUGEN fighters strictly requires a FAT32 or exFAT formatted microSD card.

---

## 5. Tutorial: Adding a New Rotation Module (e.g., Pager/News)

If you want to add a new display module (e.g., "Pager" for retro news) to the idle rotation loop, follow these steps from the UI down to the backend.

### Step 1: The Web UI
The rotation toggles are defined in `api/www/index.html`. Add your new module as a checkbox:
```html
<label class="toggle-checkbox">
  <input type="checkbox" value="pager">
  <span class="toggle-label">Pager</span>
</label>
```
The bundled JS (`app.js`) automatically parses all checked checkboxes inside `.rotation-toggles` and sends them as a comma-separated string to `/api/settings` (e.g., `rotation: "clock,gifs,pager"`).

### Step 2: The Configuration (`conf.ini`)
In `src/core/ConfigLoader.cpp`, the string is parsed and saved to the SD card:
```cpp
// In ConfigLoader::parseConfig() under "[IDLE]"
if (key == "ROTATION") idle.rotation = value;

// And to save it in ConfigLoader::saveConfig()
out += "ROTATION=" + idle.rotation + "\n";
```
*Note: Since it's stored as a string, `idle.rotation` will natively store `"clock,gifs,pager"` without needing extra parser modifications.*

### Step 3: The Engine
Create a new engine (e.g., `src/engines/PagerEngine.h`). It must implement the base drawing methods.
```cpp
#pragma once
#include "core/Matrix.h"

class PagerEngine {
public:
    void init() {
        // Setup HTTP client to fetch news here
    }
    
    void draw() {
        matrix->clearScreen();
        // Draw the pager UI using matrix->setCursor() and matrix->print()
    }
    
    void stop() {
        // Clean up memory
    }
};
```

### Step 4: The Rotation Loop
In `src/core/RotationManager.cpp` (or `main.cpp` depending on the loop architecture), the rotation string is split into an array. When it is the pager's turn to play:
```cpp
if (currentModule == "pager") {
    pagerEngine->init();
    // In the main loop()
    pagerEngine->draw();
}
```
