#pragma once
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "FeedPayloads.h"

/**
 * Draws one page of a "weather" payload: page 0 is NOW (the live station reading)
 * when the payload has one, then one page per forecast day. It fills a weather_layout::Page and
 * draws it with the same renderer as the Weather engine, so both look alike. Text is built in
 * stack buffers; nothing allocates.
 */
namespace weather_page {

void draw(MatrixPanel_I2S_DMA* matrix, const feed::WeatherFeed& wx, uint8_t page);

}  // namespace weather_page
