#pragma once
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

class SparklineRenderer {
public:
    static void drawSparkline(
        MatrixPanel_I2S_DMA* matrix,
        const float* points,
        size_t count,
        float minVal,
        float maxVal,
        int x,
        int y,
        int w,
        int h,
        uint16_t lineColor,
        uint16_t fillColor = 0
    ) {
        if (!matrix || !points || count == 0 || w < 2 || h < 2) return;

        float range = maxVal - minVal;
        if (range <= 0.00001f) range = 0.00001f;

        int prevX = -1;
        int prevY = -1;

        for (size_t i = 0; i < count; ++i) {
            float p = points[i];
            int curX = x + (int)roundf((float)i / (float)(count > 1 ? count - 1 : 1) * (float)(w - 1));
            float norm = (p - minVal) / range;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            int curY = (y + h - 1) - (int)roundf(norm * (float)(h - 1));

            if (fillColor != 0) {
                int bottomY = y + h - 1;
                for (int fillY = curY; fillY <= bottomY; ++fillY) {
                    matrix->drawPixel(curX, fillY, fillColor);
                }
            }

            if (prevX >= 0 && prevY >= 0) {
                matrix->drawLine(prevX, prevY, curX, curY, lineColor);
            } else {
                matrix->drawPixel(curX, curY, lineColor);
            }

            prevX = curX;
            prevY = curY;
        }
    }
};
