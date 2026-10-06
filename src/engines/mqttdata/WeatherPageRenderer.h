#pragma once
#include "FeedPayloads.h"

class IDrawingSurface;

/**
 * Draws one page of a "weather" payload: page 0 is NOW (the live station reading)
 * when the payload has one, then one page per forecast day. It fills a weather_layout::Page and
 * draws it with the same renderer as the Weather engine, so both look alike. Text is built in
 * stack buffers; nothing allocates.
 */
namespace weather_page {

void draw(IDrawingSurface* matrix, const feed::WeatherFeed& wx, uint8_t page);

}  // namespace weather_page
