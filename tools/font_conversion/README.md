# ArcadeMatrix Font Converter (BDF → AMF)

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

`generate_fonts.py` batch-converts every `.bdf` bitmap font in your SD card's `/fonts` folder into
ArcadeMatrix's ESP32-loadable `.amf` binary format, **in place** - the same conversion
`tools/bdf_to_amfont/bdf_to_amfont.py` does for a single file, but applied automatically to a
whole folder, the same way `tools/gif_indexation`'s scripts pre-process GIFs for the ESP32.

## Why this exists

The ESP32 firmware's `BitmapFontLoader` (used by the Clock, Date, and scrolling Message engines
for custom SD-loaded fonts) has no BDF parser on-device - it only understands the compact `.amf`
format. `ArcadeMatrix_RPi` ships and loads `.bdf` fonts directly (`fonts/*.bdf`), so if you want to
reuse those same fonts on your ESP32 build, they need this one-time offline conversion first.

## What it does

Given your SD card's root (or its `fonts` folder directly), for every `*.bdf` found:
1. Converts it to a same-named `.amf` file (e.g. `tom-thumb.bdf` → `tom-thumb.amf`).
2. **Deletes the source `.bdf`** once the `.amf` is written successfully - the SD card only needs
   to carry the format the firmware can actually read.
3. Leaves any file that fails to convert untouched and reports the error, instead of silently
   deleting a working source font.

The resulting `.amf` files become immediately selectable in the Web UI's Settings page (Clock/Date
"Font" dropdowns, populated live via `GET /api/fonts`) - no reboot needed. See
`tools/bdf_to_amfont/README.md` for the full technical details of the `.amf` format itself.

## Prerequisites

- Python 3.6+ (standard library only - no external dependencies, no `pip install` actually
  needed to run `generate_fonts.py` directly). `requirements.txt` is included only for
  consistency with the project's other tools that do need one (e.g. `mugen_extractor`); it's
  intentionally empty here.

## Usage

```bash
python3 generate_fonts.py <path_to_sd_card_root_or_fonts_folder>
```

You can pass either the SD card root (a `fonts` subfolder will be used automatically) or the
`fonts` folder itself - same convention as `tools/gif_indexation/generate_index.sh`.

### Beginner-friendly wrappers

If you'd rather not use the command line directly, `start_generate_fonts.sh` (macOS/Linux) and
`start_generate_fonts.bat` (Windows) set up an isolated Python virtual environment for you,
install `requirements.txt` (a no-op here, but consistent with the project's other tools), then
prompt you interactively for your SD card path.

```bash
./start_generate_fonts.sh
```

## Example

```
$ python3 generate_fonts.py /Volumes/SDCARD
Wrote /Volumes/SDCARD/fonts/tom-thumb.amf: 95 glyphs (0 missing/blank), 176 bitmap bytes, 1233 bytes total.
Wrote /Volumes/SDCARD/fonts/5x7.amf: 95 glyphs (0 missing/blank), 475 bitmap bytes, 1532 bytes total.

Done! Converted 2 font(s) to .amf in /Volumes/SDCARD/fonts.
```

Re-running the script on an SD card that's already fully converted (no remaining `.bdf` files) is
safe - it just reports "Nothing to do."
