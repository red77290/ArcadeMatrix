# Custom Fonts (`/fonts`)

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

Drop `.amf` bitmap font files here to use them for the Clock, Date, or scrolling Message instead
of the ~6 fonts compiled into the firmware.

## How to get a `.amf` file

1. Grab (or write) standard `.bdf` bitmap fonts - e.g. the same fonts `ArcadeMatrix_RPi` ships in
   its own `fonts/*.bdf` folder.
2. Copy the `.bdf` file(s) directly into this `/fonts` folder on your SD card.
3. Run `tools/font_conversion/generate_fonts.py` pointed at your SD card (root or this folder) -
   it batch-converts every `.bdf` here to `.amf` in place and removes the `.bdf` originals:
   ```bash
   python3 tools/font_conversion/generate_fonts.py /path/to/sd/card
   ```
   (Converting a single file by hand is also possible via
   `tools/bdf_to_amfont/bdf_to_amfont.py myfont.bdf myfont.amf`, but the batch tool above is the
   recommended workflow.)

## How to use it

- **Web UI (recommended)**: Settings > Clock or Date > "Font" dropdown. It's populated live from
  `GET /api/fonts`, which lists every `.amf` file found here - no reboot needed.
- **`conf.ini`**: set `CLOCK_FONT_PATH=/fonts/myfont.amf` under `[TIME]` and/or
  `DATE_FONT_PATH=/fonts/myfont.amf` under `[DATE]`, or `CUSTOM_FONT_PATH=/fonts/myfont.amf` under
  `[FONTS]` for the scrolling Message engine.
