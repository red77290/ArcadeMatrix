#pragma once
#include "FeedPayloads.h"

class IDrawingSurface;

/**
 * Draws a values page: 1-4 stacked tiles (label above, value and unit under it), in columns or a
 * 2x2 grid, with an optional title row. Values are never cut. Nothing allocates.
 */
namespace values_renderer {

void drawValues(IDrawingSurface* matrix, const feed::ValuesData& v, bool showTitle);

}  // namespace values_renderer
