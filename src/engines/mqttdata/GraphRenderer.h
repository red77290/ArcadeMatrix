#pragma once
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GraphPayload.h"

/**
 * Draws a graph page: header (title, summary), optional y-axis labels and bottom legend on tall
 * panels, bands, zero line, gridline, marks, bars and lines. Nothing allocates.
 */
namespace graph_renderer {

void drawGraph(MatrixPanel_I2S_DMA* matrix, const graph::GraphData& g, bool showHeader);

}  // namespace graph_renderer
