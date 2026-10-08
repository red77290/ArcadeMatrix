#include "WeatherIcon.h"
#include "../../core/drawing/IDrawingSurface.h"
#include <string.h>

namespace WeatherIcon {

void draw(IDrawingSurface* matrix, const char* icon, int x, int y, int scale) {
    if (!matrix || !icon) return;
    // 24x24 pixel design, scaled by an integer factor for larger panels.
    const int s = max(1, scale);
    auto X = [&](int v) { return x + v * s; };
    auto Y = [&](int v) { return y + v * s; };
    auto line = [&](int x0, int y0, int x1, int y1, uint16_t c) {
        // A scaled 1-px line becomes an s-px thick stroke: draw s parallel offsets.
        for (int o = 0; o < s; o++) matrix->drawLine(X(x0) + o, Y(y0), X(x1) + o, Y(y1), c);
    };
    if (strstr(icon, "01") != nullptr) { // Sun
        matrix->fillCircle(X(12), Y(12), 6 * s, matrix->color565(255, 255, 0));
        uint16_t ray = matrix->color565(255, 200, 0);
        line(12, 2, 12, 4, ray);
        line(12, 20, 12, 22, ray);
        line(2, 12, 4, 12, ray);
        line(20, 12, 22, 12, ray);
        line(5, 5, 7, 7, ray);
        line(19, 19, 17, 17, ray);
        line(19, 5, 17, 7, ray);
        line(5, 19, 7, 17, ray);
    } else if (strstr(icon, "02") != nullptr || strstr(icon, "03") != nullptr || strstr(icon, "04") != nullptr) { // Clouds
        if (strstr(icon, "02") != nullptr) { // Sun behind cloud
            matrix->fillCircle(X(8), Y(8), 4 * s, matrix->color565(255, 255, 0));
        }
        matrix->fillCircle(X(8), Y(14), 5 * s, matrix->color565(200, 200, 200));
        matrix->fillCircle(X(14), Y(11), 6 * s, matrix->color565(255, 255, 255));
        matrix->fillCircle(X(20), Y(14), 5 * s, matrix->color565(200, 200, 200));
        matrix->fillRect(X(8), Y(14), 12 * s, 6 * s, matrix->color565(200, 200, 200));
    } else if (strstr(icon, "09") != nullptr || strstr(icon, "10") != nullptr) { // Rain
        matrix->fillCircle(X(8), Y(10), 5 * s, matrix->color565(150, 150, 150));
        matrix->fillCircle(X(14), Y(8), 6 * s, matrix->color565(200, 200, 200));
        matrix->fillCircle(X(20), Y(10), 5 * s, matrix->color565(150, 150, 150));
        matrix->fillRect(X(8), Y(10), 12 * s, 6 * s, matrix->color565(150, 150, 150));
        uint16_t drop = matrix->color565(0, 150, 255);
        line(8, 18, 6, 22, drop);
        line(14, 18, 12, 22, drop);
        line(20, 18, 18, 22, drop);
    } else if (strstr(icon, "11") != nullptr) { // Thunder
        matrix->fillCircle(X(8), Y(10), 5 * s, matrix->color565(100, 100, 100));
        matrix->fillCircle(X(14), Y(8), 6 * s, matrix->color565(150, 150, 150));
        matrix->fillCircle(X(20), Y(10), 5 * s, matrix->color565(100, 100, 100));
        matrix->fillRect(X(8), Y(10), 12 * s, 6 * s, matrix->color565(100, 100, 100));
        uint16_t bolt = matrix->color565(255, 255, 0);
        line(14, 16, 10, 20, bolt);
        line(10, 20, 16, 20, bolt);
        line(16, 20, 12, 24, bolt);
    } else if (strstr(icon, "13") != nullptr) { // Snow
        uint16_t white = matrix->color565(255, 255, 255);
        matrix->fillCircle(X(14), Y(14), 2 * s, white);
        line(14, 8, 14, 20, white);
        line(8, 14, 20, 14, white);
        line(10, 10, 18, 18, white);
        line(18, 10, 10, 18, white);
    } else { // Unknown
        matrix->fillCircle(X(12), Y(12), 6 * s, matrix->color565(0, 255, 0)); // Green dot
    }
}

}  // namespace WeatherIcon
