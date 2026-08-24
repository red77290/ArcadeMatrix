#include "TempEngine.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"

extern ConfigLoader config;

TempEngine::TempEngine() 
    : useFahrenheit(false) {}

TempEngine::~TempEngine() {}

EngineError TempEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

uint16_t TempEngine::getTemperatureColor(MatrixPanel_I2S_DMA* matrix, float tempC) {
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

void TempEngine::drawThermometerIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, uint16_t color) {
    uint16_t bg = matrix->color565(200, 200, 200);
    // Outer tube
    matrix->drawRect(x + 5, y + 2, 4, 10, bg);
    // Bulb
    matrix->fillCircle(x + 6, y + 12, 3, color);
    // Mercury level
    matrix->fillRect(x + 6, y + 5, 2, 7, color);
}

void TempEngine::drawWaterDropIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, uint16_t color) {
    // Water drop shape
    matrix->drawPixel(x + 6, y + 2, color);
    matrix->drawLine(x + 5, y + 3, x + 7, y + 3, color);
    matrix->drawLine(x + 4, y + 4, x + 8, y + 4, color);
    matrix->fillCircle(x + 6, y + 8, 3, color);
}

void TempEngine::update(EngineContext* context) {
}

void TempEngine::activate() {}
void TempEngine::deactivate() {}
void TempEngine::onConfigChanged(const EngineConfig* engineConfig) {
    setUnit(config.system.unit);
}

void TempEngine::render(EngineContext* context) {
    auto* matrix = context->getMatrix();
    if (!matrix) return;

    matrix->fillScreen(0);

    EnvironmentData envData = hardwareHAL.readEnvironment(config.system.temp_offset);
    if (!envData.available) {
        // Fallback or warning if sensor not detected
        matrix->setTextSize(1);
        matrix->setTextColor(matrix->color565(150, 150, 150));
        matrix->print("Sensor N/A");
        return;
    }

    float displayTemp = useFahrenheit ? envData.temperatureF : envData.temperatureC;
    uint16_t tempColor = getTemperatureColor(matrix, envData.temperatureC);
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
        drawThermometerIcon(matrix, 4, (height / 2) - 8, tempColor);
        matrix->setTextColor(tempColor);
        matrix->setCursor(20, (height / 2) - (textSize * 4));
        matrix->print(tempBuf);

        // Draw Water Drop + Humidity on Right
        int rightStartX = (width / 2) + 10;
        drawWaterDropIcon(matrix, rightStartX, (height / 2) - 8, humColor);
        matrix->setTextColor(humColor);
        matrix->setCursor(rightStartX + 16, (height / 2) - (textSize * 4));
        matrix->print(humBuf);
    } else {
        // Standard 64x32 display layout
        matrix->setTextSize(1);

        // Draw Thermometer Icon on top-left
        drawThermometerIcon(matrix, 2, 2, tempColor);

        // Temp text top-right
        matrix->setTextColor(tempColor);
        matrix->setCursor(18, 5);
        matrix->print(tempBuf);

        // Draw Water Drop Icon on bottom-left
        drawWaterDropIcon(matrix, 2, 16, humColor);

        // Humidity text bottom-right
        matrix->setTextColor(humColor);
        matrix->setCursor(18, 19);
        matrix->print(humBuf);
    }
}

EngineDescriptor TempEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_temp;
    desc_temp.metadata = {"temp", "Environment Sensor", "sensor", "3.0.0"};
    desc_temp.capabilities.realtime = false;
    desc_temp.capabilities.allowsOverlay = true;
    desc_temp.requirements.needsTempSensor = true;
    desc_temp.schema.fields = {
        ConfigField("units", ConfigType::ENUM, "Units", "Temperature measurement units", "C", false, "", "", "", "C,F", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("temp_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("temp_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_temp.factory = []() { return std::unique_ptr<IEngine>(new TempEngine()); };
    return desc_temp;
}



