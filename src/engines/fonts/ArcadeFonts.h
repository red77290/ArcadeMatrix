#ifndef ARCADE_FONTS_H
#define ARCADE_FONTS_H

#include <Arduino.h>
#include <Adafruit_GFX.h>

// Shared arcade and system proportional fonts (defined once in ArcadeFonts.cpp
// to avoid duplicating ~70 KB of PROGMEM glyph/bitmap tables across TUs).
extern const GFXfont namco__9pt7b PROGMEM;
extern const GFXfont namco__12pt7b PROGMEM;
extern const GFXfont PressStart2P9pt7b PROGMEM;
extern const GFXfont PressStart2P12pt7b PROGMEM;
extern const GFXfont Retro_Gaming9pt7b PROGMEM;
extern const GFXfont Retro_Gaming12pt7b PROGMEM;

extern const GFXfont FreeSansBold9pt7b PROGMEM;
extern const GFXfont FreeSansBold12pt7b PROGMEM;
extern const GFXfont FreeMonoBold9pt7b PROGMEM;
extern const GFXfont FreeMonoBold12pt7b PROGMEM;

// Shared classic 5x7 GLCD font table (1,280 bytes in Flash)
extern const unsigned char* const g_glcdFont5x7;

#endif
