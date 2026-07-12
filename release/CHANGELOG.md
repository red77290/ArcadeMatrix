# Changelog

## [1.1.0] - 2026-07-09

### Added
- **12 New Fonts/Themes:** Fully integrated 12 iconic publisher themes (Nintendo, Capcom, Sega, SNK, Taito, etc.) applicable to both the Clock and Date modules.
- **Precision Offsets (X/Y):** Added new X and Y offset configurations via the Web UI for Clock, Date, and Weather modules to allow pixel-perfect manual positioning.
- **Live Rotation Settings:** Changes to rotation durations (Idle Settings) and Weather configuration (API/City) now apply live instantly without requiring a reboot.
- **Live Timezone Updates:** Updating the Timezone setting now syncs the ESP32 RTC immediately.
- **Multiple Sizes:** Added Clock Size and Date Size selection (Size 1 to 3) in the Web UI.

### Fixed
- **Clock Skipping Seconds:** Re-engineered the time tracking loop to fetch hardware RTC time directly every frame (33ms) instead of relying on blocking `millis()` delays, resulting in perfectly fluid seconds.
- **LED Ghosting (Green Pixels):** Added `latch_blanking = 4` to the HUB75 DMA configuration, eliminating visual artifacts and green flickering in matrix corners on faster panels.
- **Playlist Checkbox Bug:** Fixed a bug where checking a sprite playlist in the UI would deselect others by resolving DOM state discrepancies.
- **Missing Save Button:** Added the missing "Save Clock & Date" button logic for the unified Clock/Date settings panel.
- **Form Resiliency:** Prevented `NaN` values from crashing or polluting the JSON configuration if offset fields were left blank in the Web UI.

## [1.0.0] - 2024-07-07

### Added
- **Web Dashboard (Vite/VanillaJS):** A complete, aesthetic, and responsive web interface to control the matrix over Wi-Fi without flashing.
- **REST API:** C++ Asynchronous Web Server (`WebServerAPI.cpp`) to handle all requests JSON endpoints.
- **Clock Engine:** OOP architecture for handling standard Clocks (Word Clock, Cyberpunk) and Retro Arcade interactive clocks (Mario, Ryu, Mega Man) triggering at minute changes.
- **Batocera & Recalbox MQTT Integration:** Listen to `batocera/events` and `/Recalbox/EmulationStation/Event` to trigger game-specific GIFs instantly.
- **Custom Marquee Messages:** `MessageEngine` allows typing custom scrolling text from the Web UI with color, size, speed, and direction controls.
- **Native JSON Indexing Scripts:** `generate_index.sh` (Mac/Linux) and `generate_index.ps1` (Windows) to instantly index SD card contents without requiring Python.
- **Night Mode / Standby:** Configurable turn-off/wake-up scheduling to save power and LEDs.
- **Pre-compiled Assets:** Ready-to-use `firmware.bin` and SD card `www` bundle for end-users without development environments.

### Changed
- Complete rewrite from the monolithic `.ino` file to modular C++ object-oriented files.
- SD Card structure now expects Web UI files in `/www/` to save ESP32 Flash memory.

### Removed
- Python script requirement for generating index (replaced with native Shell/PowerShell scripts).
