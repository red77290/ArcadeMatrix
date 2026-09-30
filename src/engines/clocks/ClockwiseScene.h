#ifndef CLOCKWISESCENE_H
#define CLOCKWISESCENE_H

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

/**
 * Helpers shared by the faces adapted from the 64x64 "Clockwise" clockfaces.
 *
 * Those faces are drawn for a square panel. Rather than stretch one across a 4:1 sign, the original
 * scene is placed in the middle at its own size and its edge columns are carried out to the sides,
 * so the extra width reads as more of the same picture. A face then draws its moving parts at the
 * scene's coordinates plus `originX()` / `originY()`, which is what keeps the middle 64x64
 * behaving exactly like the original.
 *
 * On a panel shorter than the scene (128x32), the artwork is halved so the whole picture still
 * fits, and the face scales its overlays to match via `scale()`.
 */
namespace ClockwiseScene {

constexpr int SIZE = 64;

/// 1 when the scene fits at its own size, 2 when it has to be halved to fit the panel height.
inline int divisor(int panelH) { return (panelH >= SIZE) ? 1 : 2; }
inline int scaledSize(int panelH) { return SIZE / divisor(panelH); }
inline int originX(int panelW, int panelH) { return (panelW - scaledSize(panelH)) / 2; }
inline int originY(int panelH) { return (panelH - scaledSize(panelH)) / 2; }

/// Scene coordinate -> panel coordinate.
inline int mapX(int x, int panelW, int panelH) { return originX(panelW, panelH) + x / divisor(panelH); }
inline int mapY(int y, int panelH) { return originY(panelH) + y / divisor(panelH); }

/**
 * Paint a 64x64 RGB565 background: the scene in the middle, its leftmost and rightmost columns
 * smeared out to the panel edges, and its top and bottom rows filled likewise when the panel is
 * taller than the scene.
 */
inline void drawBackground(MatrixPanel_I2S_DMA* m, const uint16_t* bg, int panelW, int panelH) {
    if (!m || !bg) return;
    const int d = divisor(panelH);
    const int size = SIZE / d;
    const int ox = originX(panelW, panelH);
    const int oy = originY(panelH);

    for (int py = 0; py < panelH; py++) {
        int sy = (py - oy) * d;
        if (sy < 0) sy = 0;
        if (sy > SIZE - 1) sy = SIZE - 1;
        const uint16_t* row = bg + sy * SIZE;
        for (int px = 0; px < panelW; px++) {
            int sx = (px - ox) * d;
            if (sx < 0) sx = 0;                 // left of the scene: repeat its first column
            if (sx > SIZE - 1) sx = SIZE - 1;   // right of it: repeat its last
            m->drawPixel(px, py, row[sx]);
        }
    }
    (void)size;
}

/// Blit a sprite at scene coordinates, skipping `maskColor` (pass 0xFFFF for an opaque blit).
inline void drawSprite(MatrixPanel_I2S_DMA* m, const uint16_t* data, int w, int h, int sceneX,
                       int sceneY, int panelW, int panelH, uint16_t maskColor) {
    if (!m || !data) return;
    const int d = divisor(panelH);
    const int left = mapX(sceneX, panelW, panelH);
    const int top = mapY(sceneY, panelH);
    for (int row = 0; row < h; row += d) {
        for (int col = 0; col < w; col += d) {
            uint16_t c = data[row * w + col];
            if (c == maskColor) continue;
            int px = left + col / d;
            int py = top + row / d;
            if (px < 0 || py < 0 || px >= panelW || py >= panelH) continue;
            m->drawPixel(px, py, c);
        }
    }
}

}  // namespace ClockwiseScene

#endif
