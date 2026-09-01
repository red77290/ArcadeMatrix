#include "TempEngine.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"

extern ConfigLoader config;

TempEngine::TempEngine() 
    : useFahrenheit(false), tempOffset(0.0f), offsetX(0), offsetY(0) {}

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
    extern ConfigLoader config;
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    String sysUnit = guard->system.unit.length() > 0 ? guard->system.unit : "C";
    float sysOffset = guard->system.temp_offset;

    if (engineConfig) {
        String u = engineConfig->getString("units", "system");
        if (u.isEmpty() || u.equalsIgnoreCase("system")) u = sysUnit;
        setUnit(u);
        float off = engineConfig->getFloat("temp_offset", -999.0f);
        tempOffset = (off < -50.0f) ? sysOffset : off;
        offsetX = engineConfig->getInt("temp_offset_x", 0);
        offsetY = engineConfig->getInt("temp_offset_y", 0);
    } else {
        setUnit(sysUnit);
        tempOffset = sysOffset;
        offsetX = 0;
        offsetY = 0;
    }
}

void TempEngine::render(EngineContext* context) {
    auto* matrix = context->getMatrix();
    if (!matrix) return;

    matrix->fillScreen(0);

    EnvironmentData envData = hardwareHAL.readEnvironment(0.0f);
    if (!envData.available) {
        // Fallback or warning if sensor not detected
        matrix->setTextSize(1);
        matrix->setTextColor(matrix->color565(150, 150, 150));
        matrix->setCursor(4 + offsetX, 12 + offsetY);
        matrix->print("Sensor N/A");
        return;
    }

    float rawTemp = useFahrenheit ? envData.temperatureF : envData.temperatureC;
    float displayTemp = rawTemp + tempOffset;
    float tempCForColor = useFahrenheit ? ((displayTemp - 32.0f) * 5.0f / 9.0f) : displayTemp;
    uint16_t tempColor = getTemperatureColor(matrix, tempCForColor);
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
        drawThermometerIcon(matrix, 4 + offsetX, (height / 2) - 8 + offsetY, tempColor);
        matrix->setTextColor(tempColor);
        matrix->setCursor(20 + offsetX, (height / 2) - (textSize * 4) + offsetY);
        matrix->print(tempBuf);

        // Draw Water Drop + Humidity on Right
        int rightStartX = (width / 2) + 10;
        drawWaterDropIcon(matrix, rightStartX + offsetX, (height / 2) - 8 + offsetY, humColor);
        matrix->setTextColor(humColor);
        matrix->setCursor(rightStartX + 16 + offsetX, (height / 2) - (textSize * 4) + offsetY);
        matrix->print(humBuf);
    } else if (width < 48 || height > (width * 3) / 2) {
        // Portrait / Tate layout (e.g. 32x64, 32x128, 64x128)
        matrix->setTextSize(1);
        
        int iconX = (width - 16) / 2;
        int stepY = height / 2;

        // Top half: Thermometer icon + Temperature
        drawThermometerIcon(matrix, iconX + offsetX, (stepY / 4) - 4 + offsetY, tempColor);
        int16_t bx, by;
        uint16_t bw, bh;
        matrix->getTextBounds(tempBuf, 0, 0, &bx, &by, &bw, &bh);
        matrix->setTextColor(tempColor);
        matrix->setCursor((width - bw) / 2 + offsetX, (stepY / 4) + 14 + offsetY);
        matrix->print(tempBuf);

        // Bottom half: Water drop icon + Humidity
        drawWaterDropIcon(matrix, iconX + offsetX, stepY + (stepY / 4) - 4 + offsetY, humColor);
        matrix->getTextBounds(humBuf, 0, 0, &bx, &by, &bw, &bh);
        matrix->setTextColor(humColor);
        matrix->setCursor((width - bw) / 2 + offsetX, stepY + (stepY / 4) + 14 + offsetY);
        matrix->print(humBuf);
    } else {
        // Standard 64x32 display layout
        matrix->setTextSize(1);

        // Draw Thermometer Icon on top-left
        drawThermometerIcon(matrix, 2 + offsetX, 2 + offsetY, tempColor);

        // Temp text top-right
        matrix->setTextColor(tempColor);
        matrix->setCursor(18 + offsetX, 5 + offsetY);
        matrix->print(tempBuf);

        // Draw Water Drop Icon on bottom-left
        drawWaterDropIcon(matrix, 2 + offsetX, 16 + offsetY, humColor);

        // Humidity text bottom-right
        matrix->setTextColor(humColor);
        matrix->setCursor(18 + offsetX, 19 + offsetY);
        matrix->print(humBuf);
    }
}

EngineDescriptor TempEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_temp;
    desc_temp.metadata = {"temp", "Environment Sensor", "sensor", FIRMWARE_VERSION};
    desc_temp.capabilities.realtime = false;
    desc_temp.requirements.needsTempSensor = false;
    desc_temp.schema.fields = {
        ConfigField("units", ConfigType::ENUM, "Units", "Temperature measurement units", "system", false, "", "", "", "system:System (General),C:Celsius (°C),F:Fahrenheit (°F)", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("temp_offset", ConfigType::FLOAT, "Calibration Offset", "Calibration offset in selected temperature unit added to raw sensor reading", "0.0", false, "-30.0", "30.0", "0.5", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("temp_offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("temp_offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_temp.factory = []() { return std::unique_ptr<IEngine>(new TempEngine()); };
    return desc_temp;
}



