# ArcadeMatrix BDF-to-AMFONT Converter

🇬🇧 English | 🇫🇷 [Français](README_FR.md) | 🇪🇸 [Español](README_ES.md)

This Python script (`bdf_to_amfont.py`) converts a standard **BDF** bitmap font into ArcadeMatrix's
runtime-loadable `.amf` binary format, for use with the ESP32 firmware's `BitmapFontLoader`.

## What is it for?

By default, all fonts used by the ESP32 firmware are compiled directly into flash at build time
(`src/engines/fonts/`, generated via Adafruit's `fontconvert` tool). That's efficient, but it means
adding or changing a font requires a full firmware rebuild and reflash.

`BitmapFontLoader` closes that gap by loading a font from the SD card at boot instead - no rebuild
needed, just copy a file and point `config.json` at it. But the ESP32 has no BDF parser or font
rasterizer on-device (that would cost flash/RAM/CPU we don't have to spare), so fonts must be
**pre-converted offline** into a compact binary format the firmware can `malloc()` and read
directly. That's exactly what this script does.

BDF was chosen as the source format because:
- It's the exact same bitmap font format the ArcadeMatrix_RPi (Raspberry Pi) project already
  ships and loads at runtime (`fonts/*.bdf`, via `rgbmatrix`'s `graphics.Font.LoadFont()`), so any
  font that already works there is a drop-in candidate.
- It's a strict per-pixel format (no antialiasing/hinting/kerning to worry about) which matches
  how LED matrix displays actually render - a good, simple target for a from-scratch parser.
- Huge libraries of free BDF fonts already exist (X11/X BDF collections, oldschool terminal fonts,
  pixel-art fonts, etc.).

## Prerequisites

- Python 3.6+ (standard library only, no external dependencies).

## Usage

```bash
python3 bdf_to_amfont.py input.bdf output.amf [--first 0x20] [--last 0x7E]
```

- `--first`/`--last` restrict the converted codepoint range (accepts `0x..` hex or plain decimal).
  Defaults to `0x20`-`0x7E` (printable ASCII), matching the range used by ArcadeMatrix's existing
  compiled-in fonts. Widening the range increases the resulting file size (and RAM usage on the
  ESP32 once loaded), so only include what you actually need.
- Codepoints missing from the source BDF within the requested range are emitted as blank,
  zero-width glyphs rather than aborting the conversion.

Then copy the resulting `.amf` file to the SD card's `/fonts` folder (e.g. `/fonts/myfont.amf`).
It becomes immediately selectable in the Web UI's Settings page, in the "Font" dropdown for either
the Clock or the Date (each populated live from `GET /api/fonts`, which lists every `.amf` file
found in `/fonts`) - no manual `config.json` editing needed. You can also set it directly via
`config.json` (`clock_font_path=/fonts/myfont.amf` / `date_font_path=/fonts/myfont.amf` under `[time]`
/ `[date]`, or `custom_font_path=/fonts/myfont.amf` under `[fonts]` for the scrolling
`MessageEngine`/`/api/message`). See `docs/DEVELOPER.md` for the full end-to-end workflow.

## Format details

`.amf` mirrors Adafruit_GFX's own compiled-font (`GFXfont`/`GFXglyph`) memory layout exactly, so
`BitmapFontLoader` can reconstruct it in RAM and hand it straight to `matrix->setFont()` with zero
translation at draw time:

```
Header (12 bytes):
  magic        4 bytes   "AMF1"
  first        uint16 LE first codepoint
  last         uint16 LE last codepoint
  yAdvance     uint8      newline distance (from the BDF's FONTBOUNDINGBOX)
  reserved     uint8      (unused, always 0)
  glyphCount   uint16 LE  == last - first + 1

Glyph table (9 bytes x glyphCount), one entry per codepoint in [first, last]:
  bitmapOffset uint32 LE  byte offset into the bitmap blob (glyph's OWN byte boundary)
  width        uint8
  height       uint8
  xAdvance     uint8
  xOffset      int8
  yOffset      int8

Bitmap blob: remainder of the file - packed glyph bitmaps, MSB-first, each glyph individually
             byte-aligned (i.e. NOT one continuous bitstream across glyphs) - exactly matching
             what Adafruit's own fontconvert tool produces for compiled-in fonts.
```

Fonts are capped at 65535 bytes of packed bitmap data (`GFXglyph.bitmapOffset` is a `uint16_t` in
Adafruit_GFX's own struct) - the same ceiling applies to the project's compiled-in fonts, so this
isn't a limitation specific to `.amf`.
