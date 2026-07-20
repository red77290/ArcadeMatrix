# Custom Fonts (`/fonts`)

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

Drop `.amf` bitmap font files here to use them for the Clock, Date, or scrolling Message instead
of the ~6 fonts compiled into the firmware.

## How to get a `.amf` file

1. Grab (or write) a standard `.bdf` bitmap font - e.g. one of the same fonts
   `ArcadeMatrix_RPi` ships in its own `fonts/*.bdf` folder.
2. Convert it with `tools/bdf_to_amfont/bdf_to_amfont.py`:
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py myfont.bdf myfont.amf
   ```
3. Copy the resulting `myfont.amf` into this folder.

## How to use it

- **Web UI (recommended)**: Settings > Clock or Date > "Font" dropdown. It's populated live from
  `GET /api/fonts`, which lists every `.amf` file found here - no reboot needed.
- **`conf.ini`**: set `CLOCK_FONT_PATH=/fonts/myfont.amf` under `[TIME]` and/or
  `DATE_FONT_PATH=/fonts/myfont.amf` under `[DATE]`, or `CUSTOM_FONT_PATH=/fonts/myfont.amf` under
  `[FONTS]` for the scrolling Message engine.
