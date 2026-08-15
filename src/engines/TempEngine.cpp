#include "TempEngine.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"

extern ConfigLoader config;

TempEngine::TempEngine(MatrixPanel_I2S_DMA* display) 
    : matrix(display), useFahrenheit(false) {}

TempEngine::~TempEngine() {}

uint16_t TempEngine::getTemperatureColor(float tempC) {
    if (!matrix) return 0xFFFF;
    if (tempC < 18.0f) {
        return matrix->color565(0, 180, 255); // Blue / Cyan (Cold)
    } else if (tempC <= 24.0f) {
        return matrix->color565(0, 230, 120); // Emerald Green (Comfort)
    } else if (tempC <= 28.0f) {
        return matrix->color565(255, 180, 0); // Warm Amber
    } else {
        return matrix->color565(255, 50, 50);  // Red (Hot)
    }
}

void TempEngine::drawThermometerIcon(int x, int y, uint16_t color) {
    if (!matrix) return;
    uint16_t bg = matrix->color565(200, 200, 200);
    // Outer tube
    matrix->drawRect(x + 5, y + 2, 4, 10, bg);
    // Bulb
    matrix->fillCircle(x + 6, y + 12, 3, color);
    // Mercury level
    matrix->fillRect(x + 6, y + 5, 2, 7, color);
}

void TempEngine::drawWaterDropIcon(int x, int y, uint16_t color) {
    if (!matrix) return;
    // Water drop shape
    matrix->drawPixel(x + 6, y + 2, color);
    matrix->drawLine(x + 5, y + 3, x + 7, y + 3, color);
    matrix->drawLine(x + 4, y + 4, x + 8, y + 4, color);
    matrix->fillCircle(x + 6, y + 8, 3, color);
}

bool TempEngine::loop() {
    if (!matrix) return false;

    matrix->fillScreen(0);

    // Check config for unit
    setUnit(config.env.unit);

    EnvironmentData envData = hardwareHAL.readEnvironment(config.env.temp_offset);
    if (!envData.available) {
        // Fallback or warning if sensor not detected
        matrix->setTextSize(1);
        matrix->setTextColor(matrix->color565(150, 150, 150));
        matrix->setCursor(4, (matrix->height() / 2) - 4);
        matrix->print("Sensor N/A");
        return true;
    }

    float displayTemp = useFahrenheit ? envData.temperatureF : envData.temperatureC;
    uint16_t tempColor = getTemperatureColor(envData.temperatureC);
    uint16_t humColor = matrix->color565(0, 200, 255);

    int width = matrix->width();
    int height = matrix->height();

    char tempBuf[16];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f%s", displayTemp, useFahrenheit ? "F" : "C");

    char humBuf[16];
    snprintf(humBuf, sizeof(humBuf), "%.0f%%", envData.humidity);

    matrix->setFont(nullptr);

    if (width >= 128) {
        // Wide display layout (128x32, 256x64, etc.)
        int textSize = (width >= 256 && height >= 64) ? 2 : 1;
        matrix->setTextSize(textSize);

        // Draw Thermometer + Temp on Left
        drawThermometerIcon(4, (height / 2) - 8, tempColor);
        matrix->setTextColor(tempColor);
        matrix->setCursor(20, (height / 2) - (textSize * 4));
        matrix->print(tempBuf);

        // Draw Water Drop + Humidity on Right
        int rightStartX = (width / 2) + 10;
        drawWaterDropIcon(rightStartX, (height / 2) - 8, humColor);
        matrix->setTextColor(humColor);
        matrix->setCursor(rightStartX + 16, (height / 2) - (textSize * 4));
        matrix->print(humBuf);
    } else {
        // Standard 64x32 display layout
        matrix->setTextSize(1);

        // Draw Thermometer Icon on top-left
        drawThermometerIcon(2, 2, tempColor);

        // Temp text top-right
        matrix->setTextColor(tempColor);
        matrix->setCursor(18, 5);
        matrix->print(tempBuf);

        // Draw Water Drop Icon on bottom-left
        drawWaterDropIcon(2, 16, humColor);

        // Humidity text bottom-right
        matrix->setTextColor(humColor);
        matrix->setCursor(18, 19);
        matrix->print(humBuf);
    }

    return true;
}
