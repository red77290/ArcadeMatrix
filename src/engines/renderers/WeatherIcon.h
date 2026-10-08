#pragma once

class IDrawingSurface;

/**
 * The weather screen's 24x24 vector icons (sun, cloud, rain, storm, snow), keyed by OpenWeatherMap
 * icon code ("01d", "10n", ...) and drawn at an integer `scale`, as used by the shared weather page
 * layout (renderers/WeatherLayout) that both the Weather engine and the MQTT Data engine's weather
 * pages draw with.
 */
namespace WeatherIcon {

void draw(IDrawingSurface* matrix, const char* icon, int x, int y, int scale = 1);

}  // namespace WeatherIcon
