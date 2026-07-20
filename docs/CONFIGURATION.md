# Configuration Guide (`conf.ini`)

🇬🇧 English | 🇫🇷 [Français](CONFIGURATION_FR.md) | 🇪🇸 [Español](CONFIGURATION_ES.md)

The ArcadeMatrix is designed to be completely decoupled from its C++ code for end-users. All settings are managed either via the Web UI (at `http://arcadematrix.local`) or directly via the `conf.ini` file on the root of your SD card.

## Full Reference
The exhaustive `conf.ini` parameter list is shipped in the `release/sdCard/` folder. Please open that file to see all available values and comments.

### Key Sections:
- `[WIFI]`: SSID, Password, and mDNS hostname for Web UI access.
- `[MATRIX]`: Geometry mapping (Width, Height, Chain count), color depth, and buffering. **Adjust these if your matrix is distorted.** `PANEL_TYPE` is accepted and saved but currently has no effect on the driver (single hardcoded pin map for all panel types - see `docs/HARDWARE.md`).
- `[MQTT]`: IP address of your Batocera/Recalbox for Live Marquee sync.
- `[TIME]`: Timezone, clock layout options, and `clock_color_1`/`clock_color_2` gradient colors.
- `[IDLE]`: The sequence of modules to play when nothing is happening (Clock, Date, Weather, GIFs, Sprites), including `fighter_interval_sec` (delay between MUGEN fights).
- `[DATE]`: Background sprites, formatting, and `date_color_1`/`date_color_2` gradient colors for the date module.
- `[WEATHER]`: OpenWeatherMap API keys.
- `[STANDBY]`: Night mode power-saving timers and `night_brightness` level (set to 0 to completely turn off the panel at night).
- `[FONTS]`: Optional `custom_font_path` to an SD-loaded `.amf` bitmap font (see `tools/bdf_to_amfont/README.md`).
