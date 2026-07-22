#pragma once
#include <Arduino.h>
#include "SDUtils.h"
#include <gfxfont.h>

/**
 * @file BitmapFontLoader.h
 * @brief Loads a custom bitmap font from the SD card into a GFXfont-compatible
 * runtime structure, for use with Adafruit_GFX/HUB75's matrix->setFont().
 *
 * Fonts must first be converted to ArcadeMatrix's ".amf" binary format via
 * tools/bdf_to_amfont/bdf_to_amfont.py from a standard BDF bitmap font (the same
 * font format ArcadeMatrix_RPi already ships in fonts/*.bdf and loads at runtime
 * via rgbmatrix's graphics.Font.LoadFont()).
 *
 * Unlike the project's ~7 compiled-in fonts (baked into flash at build time via
 * Adafruit's fontconvert tool), this lets end users add/change fonts by copying a
 * single file to the SD card and pointing conf.ini at it - no firmware rebuild.
 *
 * The loaded GFXfont's bitmap/glyph arrays live in heap RAM (not PROGMEM/flash),
 * built to the exact same byte layout Adafruit's own fontconvert produces, so
 * Adafruit_GFX's drawChar() consumes it completely unmodified.
 */
class BitmapFontLoader {
public:
    BitmapFontLoader();
    ~BitmapFontLoader();

    /// Loads a .amf file from the SD card into RAM, freeing any previously loaded font
    /// first. Returns true on success; on failure, no font is loaded and getFont() returns
    /// nullptr (callers should fall back to a compiled-in font or the default 5x7 font).
    bool loadFromSD(const char* path);

    /// Frees the currently loaded font's RAM. Safe to call even if nothing is loaded.
    void unload();

    /// Returns a GFXfont* ready for matrix->setFont(), or nullptr if nothing is loaded.
    GFXfont* getFont();

private:
    GFXfont font;
    GFXglyph* glyphs;
    uint8_t* bitmap;
    bool loaded;
};
