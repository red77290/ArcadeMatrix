#pragma once
#include <Adafruit_GFX.h>
#include "../../core/ConfigLoader.h"
#include "../../core/BitmapFontLoader.h"

/**
 * Resolves a clock instance's `clock_font` setting into a GFXfont the animated faces can draw with,
 * the same way ArcadeClock does: built-in GFX names first, then a user `.amf` loaded from the card,
 * "Default"/empty meaning the built-in 5x7 font. The RPi build shapes every face from the selected
 * font's glyphs; this gives the ESP32 faces the same behaviour.
 *
 * Two Adafruit_GFX conventions differ between the built-in font and custom fonts and are the reason
 * faces cannot simply swap `setFont(NULL)` for a real font:
 *  - the cursor is the glyph's top-left for the built-in font but the BASELINE for custom fonts, so a
 *    top-left target `y` must be printed at `y - by` (getTextBounds' `by` is 0 for the built-in font
 *    and negative for custom fonts);
 *  - glyph metrics differ, so any `6 * n` / `8 * scale` width/height arithmetic must come from
 *    getTextBounds instead.
 */
class ClockFaceFont {
public:
    /// Read `clock_font` (and its legacy aliases) from the instance config and load it.
    void load(const EngineConfig* cfg);

    /// The configured font, or nullptr for the built-in 5x7.
    const GFXfont* get() const { return active; }

    /**
     * Select the configured font on `gfx` at `textSize` if `ref` fits inside maxW x maxH, otherwise
     * fall back to the built-in font so nothing is drawn off-panel. Returns the font applied.
     */
    const GFXfont* apply(Adafruit_GFX& gfx, uint8_t textSize, const char* ref, int maxW, int maxH) const {
        gfx.setTextSize(textSize);
        if (active) {
            gfx.setFont(active);
            int16_t bx, by;
            uint16_t bw, bh;
            gfx.getTextBounds(ref, 0, 0, &bx, &by, &bw, &bh);
            if ((int)bw <= maxW && (int)bh <= maxH) return active;
        }
        gfx.setFont(nullptr);
        return nullptr;
    }

    /// Horizontal cursor advance of one glyph at text size 1 (built-in font: 6 px per character).
    static int advance(const GFXfont* f, char c) {
        if (!f) return 6;
        uint8_t u = (uint8_t)c;
        if (u < f->first || u > f->last) return 0;
        return f->glyph[u - f->first].xAdvance;
    }

private:
    BitmapFontLoader loader;          ///< owns a user .amf font read from the card
    const GFXfont* active = nullptr;  ///< resolved font; nullptr = built-in 5x7
};
