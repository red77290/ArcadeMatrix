#pragma once
#include <Arduino.h>
#include <limits.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GraphPayload.h"

/**
 * Text helpers for the MQTT Data engine's pages (value, table, graph, weather), matching the RPi firmware's
 * layouts pixel for pixel. Two font families:
 *  - G<n>: the built-in 5x7 font at integer scale n (glyph 5n x 7n, advance 6n);
 *  - T:    a 3x5 "tiny" font (advance 4), uppercase, digits and a few symbols.
 * A font is given as its scale, with TINY (0) meaning T. graph::DEGREE (stored for UTF-8 "°") is
 * drawn as the degree glyph in both. Nothing allocates.
 */
namespace panel_text {

static constexpr int TINY = 0;

inline int height(int font) { return font == TINY ? 5 : 7 * font; }

/// Width in pixels of `text` (no trailing gap column): len*4-1 for T, len*6n-n for G<n>.
inline int width(const char* text, int font) {
    size_t n = text ? strlen(text) : 0;
    if (!n) return 0;
    return font == TINY ? (int)n * 4 - 1 : (int)n * 6 * font - font;
}

/**
 * Draws `text` with its top-left at (x, y). Pixels at or right of `clipX` are not drawn (a glyph
 * that straddles it is drawn and the overflow blanked, so draw left to right before anything
 * that sits beyond the clip).
 */
void print(MatrixPanel_I2S_DMA* m, int x, int y, const char* text, int font, uint16_t color, int clipX = INT_MAX);

/// The localized "NO DATA" notice, centered, G1 #787878 (EN NO DATA / FR PAS DE DONNEES / ES SIN DATOS).
void drawNoData(MatrixPanel_I2S_DMA* m);

enum class Message : uint8_t { Connecting, NoConnection, Unsupported };

/// The MQTT Data engine's notices, placed like NO DATA: CONNECTING / CONNEXION / CONECTANDO in
/// #787878, NO CONNECTION / PAS DE CONNEXION / SIN CONEXION in #B45050, UNSUPPORTED /
/// NON PRIS EN CHARGE / NO COMPATIBLE in #C08000.
void drawNotice(MatrixPanel_I2S_DMA* m, Message msg);

}  // namespace panel_text
