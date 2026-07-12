# Configuration Guide (`conf.ini`)

The ArcadeMatrix is designed to be completely decoupled from its C++ code for end-users. All settings are managed either via the Web UI (at `http://arcadematrix.local`) or directly via the `conf.ini` file on the root of your SD card.

## Full Reference
The exhaustive `conf.ini` parameter list is shipped in the `release/sdcard/` folder. Please open that file to see all available values and comments.

### Key Sections:
- `[WIFI]`: SSID, Password, and mDNS hostname for Web UI access.
- `[MATRIX]`: Geometry mapping (Width, Height, Panel Type, Chain count). **Adjust these if your matrix is distorted.**
- `[MQTT]`: IP address of your Batocera/Recalbox for Live Marquee sync.
- `[TIME]`: Timezone and clock layout options.
- `[IDLE]`: The sequence of modules to play when nothing is happening (Clock, Date, Weather, GIFs, Sprites).
- `[DATE]`: Background sprites and formatting for the date module.
- `[WEATHER]`: OpenWeatherMap API keys.
- `[STANDBY]`: Night mode power-saving timers.
