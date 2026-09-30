#pragma once
#include <Adafruit_GFX.h>
#include "../../core/ConfigLoader.h"
#include "../../core/BitmapFontLoader.h"
#include <stdlib.h>

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

    /// A dimmed copy of an RGB565 colour, for the halo the Matrix face draws behind its digits.
    static uint16_t dim(uint16_t c, uint8_t numerator, uint8_t denominator) {
        if (!denominator) return c;
        uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        r = (uint8_t)((uint16_t)r * numerator / denominator);
        g = (uint8_t)((uint16_t)g * numerator / denominator);
        b = (uint8_t)((uint16_t)b * numerator / denominator);
        return (uint16_t)(r << 11) | (uint16_t)(g << 5) | b;
    }

    /// "#rrggbb" (or "rrggbb") to RGB565. False when the string is not a colour at all, which is
    /// what lets a caller tell "black" apart from "not set".
    static bool tryParseHex(const char* hex, uint16_t& out) {
        if (!hex) return false;
        while (*hex == ' ') hex++;
        if (*hex == '#') hex++;
        int n = 0; while (hex[n] && n < 7) n++;
        if (n < 6) return false;
        char buf[7]; for (int i = 0; i < 6; i++) buf[i] = hex[i];
        buf[6] = '\0';
        char* end = nullptr;
        long v = strtol(buf, &end, 16);
        if (end != buf + 6) return false;
        uint8_t r = (uint8_t)((v >> 16) & 0xFF), g = (uint8_t)((v >> 8) & 0xFF), b = (uint8_t)(v & 0xFF);
        out = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        return true;
    }

    /// "#rrggbb" (or "rrggbb") to RGB565; returns `fallback` when the string is not a colour.
    static uint16_t parseHex(const char* hex, uint16_t fallback) {
        uint16_t v = 0;
        return tryParseHex(hex, v) ? v : fallback;
    }

    /// A colour blended most of the way to white, the pale centre the Matrix face draws.
    static uint16_t paled(uint16_t c) {
        uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        r = (uint8_t)(r + (31 - r) * 3 / 4);
        g = (uint8_t)(g + (63 - g) * 3 / 4);
        b = (uint8_t)(b + (31 - b) * 3 / 4);
        return (uint16_t)(r << 11) | (uint16_t)(g << 5) | b;
    }

    /**
     * Resolve the instance's glow setting for digits drawn in `textColor`.
     *   0 / absent : no halo.
     *   1 (neon)   : the Matrix face's recipe - the chosen colour becomes the outline at full
     *                strength and the centre is drawn near-white, which is what makes the digits
     *                read as outlined rather than merely shadowed.
     *   2 (custom) : the outline is `clock_glow_color` and the centre keeps its own colour.
     * `core` comes back as the colour the centre pass should use. Returns false for no halo.
     */
    struct Glow {
        uint8_t mode = 0;        ///< 0 none, 1 neon, 2 a colour of its own
        uint16_t color = 0;      ///< the outline colour when `mode` is 2 and `hasColor` is set
        bool hasColor = false;
    };

    /**
     * Read the glow settings once. Every lookup on an EngineConfig builds a String for the key and
     * another for the value, so a face that asked on each draw was allocating on the render core
     * several times a frame. A face resolves this when it is built, which is also when a settings
     * change rebuilds it, and draws from the result.
     */
    static Glow resolveGlow(const EngineConfig* cfg) {
        Glow g;
        if (!cfg) return g;
        const int mode = cfg->getInt("clock_glow", 0);
        if (mode <= 0) return g;
        g.mode = (uint8_t)mode;
        if (mode >= 2) {
            String hex = cfg->getString("clock_glow_color", "");
            g.hasColor = tryParseHex(hex.c_str(), g.color);
        }
        return g;
    }

    static bool glowFor(const Glow& g, uint16_t textColor, uint16_t& halo, uint16_t& core) {
        core = textColor;
        if (g.mode == 0) return false;
        if (g.mode == 1) { halo = textColor; core = paled(textColor); return true; }
        halo = g.hasColor ? g.color : textColor;
        return true;
    }

    /**
     * Print `str` at (x, y) in `color`, adding the configured glow outline. Every animated face draws
     * its text through this, so one font at one size looks the same whichever face is showing.
     */
    /**
     * @param ringWhenOff true for the faces that drew a black ring of their own before the glow
     *        setting existed, so turning the glow off leaves them exactly as they were. False for a
     *        face whose text was printed once, where a ring would thicken the glyphs.
     */
    static void print(Adafruit_GFX& gfx, const Glow& g, int x, int y, const char* str,
                      uint16_t color, bool ringWhenOff = true) {
        uint16_t halo = 0, core = color;
        if (!glowFor(g, color, halo, core)) {
            if (!ringWhenOff) {          // print once, as this face always did
                gfx.setTextColor(core);
                gfx.setCursor(x, y);
                gfx.print(str);
                return;
            }
            halo = 0;
        }
        gfx.setTextColor(halo);
        gfx.setCursor(x - 1, y); gfx.print(str);
        gfx.setCursor(x + 1, y); gfx.print(str);
        gfx.setCursor(x, y - 1); gfx.print(str);
        gfx.setCursor(x, y + 1); gfx.print(str);
        gfx.setTextColor(core);
        gfx.setCursor(x, y);
        gfx.print(str);
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
