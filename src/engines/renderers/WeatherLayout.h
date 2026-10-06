#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

/**
 * The weather page layout, shared by every screen that shows a forecast page so they look the same
 * (the same pixels as the RPi firmware's weather screen):
 *  - 256x64 and 128x32: icon | day label + condition | the two temperatures right-aligned;
 *  - 64x32 and square/vertical panels: the compact layouts.
 * Temperatures carry the degree sign ("90°F"); in °F the high is on top and the low below (US
 * convention), in °C the low is on top.
 *
 * The caller fills a Page with plain text; drawing allocates nothing.
 */
namespace weather_layout {

struct Page {
    bool fahrenheit = false;      ///< decides the row order of high and low
    const char* icon = "";        ///< OWM-style icon code ("01d", "10n", ...)
    char label[24] = {0};         ///< day label, short ("TODAY", "MON") ...
    char labelLong[24] = {0};     ///< ... and unabbreviated, used when it fits
    char desc[32] = {0};          ///< condition, short ...
    char descLong[32] = {0};      ///< ... and long
    char top[32] = {0};           ///< row 1: "90°F"
    char bottom[40] = {0};        ///< row 2: "70°F"
    bool topIsHigh = false;       ///< colour of row 1: orange for the high, cyan for the low
};

/// Formats the day's range into page.top / page.bottom ("90°F") in the row order for the unit.
void setRange(Page& page, float low, float high, bool fahrenheit);

/// Draws the page. `offsetX`/`offsetY` shift it; `shadow` is the text shadow colour.
void draw(MatrixPanel_I2S_DMA* matrix, const Page& page, int offsetX = 0, int offsetY = 0, uint16_t shadow = 0);

}  // namespace weather_layout
