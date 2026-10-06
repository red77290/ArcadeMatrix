#pragma once
#include "GraphPayload.h"

class IDrawingSurface;

/**
 * Draws a graph page: header (title, summary), optional y-axis labels and bottom legend on tall
 * panels, bands, zero line, gridline, marks, bars and lines. Nothing allocates.
 */
namespace graph_renderer {

void drawGraph(IDrawingSurface* matrix, const graph::GraphData& g, bool showHeader);

}  // namespace graph_renderer
