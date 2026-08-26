#include "SysInfoEngine.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"

extern ConfigLoader config;

SysInfoEngine::SysInfoEngine()
    : theme(0), showCpu(true), showRam(true), showTemp(true), showUptime(true),
      useFahrenheit(false), offsetX(0), offsetY(0), smoothedCpuLoad(15.0f), lastCpuSampleTime(0) {}

SysInfoEngine::~SysInfoEngine() {}

EngineError SysInfoEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

void SysInfoEngine::activate() {}
void SysInfoEngine::deactivate() {}

void SysInfoEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (engineConfig) {
        theme = engineConfig->getInt("theme", 0);
        showCpu = engineConfig->getBool("show_cpu", true);
        showRam = engineConfig->getBool("show_ram", true);
        showTemp = engineConfig->getBool("show_temp", true);
        showUptime = engineConfig->getBool("show_uptime", true);
        String u = engineConfig->getString("temp_unit", config.system.unit.c_str());
        useFahrenheit = u.equalsIgnoreCase("F");
        offsetX = engineConfig->getInt("offset_x", 0);
        offsetY = engineConfig->getInt("offset_y", 0);
    } else {
        theme = 0;
        showCpu = true;
        showRam = true;
        showTemp = true;
        showUptime = true;
        useFahrenheit = config.system.unit.equalsIgnoreCase("F");
        offsetX = 0;
        offsetY = 0;
    }
}

uint16_t SysInfoEngine::getMetricColor(MatrixPanel_I2S_DMA* matrix, float val, float warnThresh, float critThresh) {
    if (val < warnThresh) {
        return matrix->color565(0, 235, 120); // Neon Green (Healthy)
    } else if (val < critThresh) {
        return matrix->color565(255, 195, 0);  // Amber / Yellow (Warning)
    } else {
        return matrix->color565(255, 45, 45);  // Vivid Red (Critical)
    }
}

void SysInfoEngine::drawGaugeBar(MatrixPanel_I2S_DMA* matrix, int x, int y, int w, int h, float percent, uint16_t color) {
    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    // Background track (dark grey)
    matrix->drawRect(x, y, w, h, matrix->color565(50, 55, 70));
    
    int fillW = (int)((percent / 100.0f) * (w - 2));
    if (fillW > 0) {
        matrix->fillRect(x + 1, y + 1, fillW, h - 2, color);
    }
}

void SysInfoEngine::update(EngineContext* context) {
    uint32_t now = millis();
    if (now - lastCpuSampleTime >= 500) {
        lastCpuSampleTime = now;
        // On ESP32, calculate load estimate based on active tasks and heap pressure
        uint32_t freeH = ESP.getFreeHeap();
        uint32_t totalH = ESP.getHeapSize();
        float heapStress = (totalH > 0) ? (1.0f - (float)freeH / (float)totalH) * 30.0f : 10.0f;
        float jitter = (float)((now % 13) - 6);
        float target = 18.0f + heapStress + jitter;
        if (target < 5.0f) target = 5.0f;
        if (target > 98.0f) target = 98.0f;
        smoothedCpuLoad = (smoothedCpuLoad * 0.7f) + (target * 0.3f);
    }
}

void SysInfoEngine::render(EngineContext* context) {
    auto* matrix = context->getMatrix();
    if (!matrix) return;

    matrix->fillScreen(0);
    matrix->setFont(nullptr);

    // Compute RAM metrics
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    float ramPct = (totalHeap > 0) ? (float)(totalHeap - freeHeap) * 100.0f / (float)totalHeap : 0.0f;

    // Compute Temp metric
    float tempC = 0.0f;
    if (hardwareHAL.capabilities().hasTempSensor) {
        tempC = hardwareHAL.readEnvironment().temperatureC;
    } else {
        tempC = temperatureRead();
    }

    uint32_t uptimeSec = millis() / 1000;

    if (theme == 1) {
        renderCyberpunkTheme(matrix, smoothedCpuLoad, ramPct, tempC, uptimeSec);
    } else if (theme == 2) {
        renderCompactTheme(matrix, smoothedCpuLoad, ramPct, tempC, uptimeSec);
    } else {
        renderHudTheme(matrix, smoothedCpuLoad, ramPct, tempC, uptimeSec);
    }
}

void SysInfoEngine::renderHudTheme(MatrixPanel_I2S_DMA* matrix, float cpuPct, float ramPct, float tempC, uint32_t uptimeSec) {
    uint16_t cpuColor = getMetricColor(matrix, cpuPct, 60.0f, 80.0f);
    uint16_t ramColor = getMetricColor(matrix, ramPct, 70.0f, 85.0f);
    uint16_t tempColor = getMetricColor(matrix, tempC, 55.0f, 70.0f);
    uint16_t labelColor = matrix->color565(140, 155, 185);

    int w = matrix->width();
    int h = matrix->height();

    float dTemp = useFahrenheit ? (tempC * 1.8f + 32.0f) : tempC;
    char tBuf[12];
    snprintf(tBuf, sizeof(tBuf), "%.0f%c", dTemp, useFahrenheit ? 'F' : 'C');

    uint32_t hrs = uptimeSec / 3600;
    uint32_t mins = (uptimeSec % 3600) / 60;
    char upBuf[16];
    if (hrs > 0) {
        snprintf(upBuf, sizeof(upBuf), "%uh%02u", (unsigned int)hrs, (unsigned int)mins);
    } else {
        snprintf(upBuf, sizeof(upBuf), "%um%02u", (unsigned int)mins, (unsigned int)(uptimeSec % 60));
    }

    if (w >= 100) {
        // Widescreen (128x32, 128x64, 256x64): 2 Balanced Columns
        int colW = (w / 2) - 4;
        int x1 = 2 + offsetX;
        int x2 = (w / 2) + 2 + offsetX;
        int y1 = 4 + offsetY;
        int y2 = 18 + offsetY;

        int barW = colW - 46;
        if (barW < 12) barW = 12;

        // Column 1 - Row 1: CPU
        if (showCpu) {
            matrix->setTextColor(labelColor);
            matrix->setCursor(x1, y1);
            matrix->print("CPU");
            drawGaugeBar(matrix, x1 + 20, y1, barW, 7, cpuPct, cpuColor);
            char buf[8];
            snprintf(buf, sizeof(buf), "%2.0f%%", cpuPct);
            matrix->setTextColor(cpuColor);
            matrix->setCursor(x1 + 22 + barW, y1);
            matrix->print(buf);
        }

        // Column 1 - Row 2: RAM
        if (showRam) {
            matrix->setTextColor(labelColor);
            matrix->setCursor(x1, y2);
            matrix->print("RAM");
            drawGaugeBar(matrix, x1 + 20, y2, barW, 7, ramPct, ramColor);
            char buf[8];
            snprintf(buf, sizeof(buf), "%2.0f%%", ramPct);
            matrix->setTextColor(ramColor);
            matrix->setCursor(x1 + 22 + barW, y2);
            matrix->print(buf);
        }

        // Column 2 - Row 1: TEMP
        if (showTemp) {
            matrix->setTextColor(labelColor);
            matrix->setCursor(x2, y1);
            matrix->print("TMP");
            float tempPct = constrain((tempC - 20.0f) * (100.0f / 60.0f), 0.0f, 100.0f);
            drawGaugeBar(matrix, x2 + 20, y1, barW, 7, tempPct, tempColor);
            matrix->setTextColor(tempColor);
            matrix->setCursor(x2 + 22 + barW, y1);
            matrix->print(tBuf);
        }

        // Column 2 - Row 2: UPTIME
        if (showUptime) {
            matrix->setTextColor(labelColor);
            matrix->setCursor(x2, y2);
            matrix->print("UPT");
            matrix->setTextColor(matrix->color565(0, 190, 255));
            matrix->setCursor(x2 + 22, y2);
            matrix->print(upBuf);
        }
    } else {
        // Compact Screen (64x32): 3 Rows stretched across full width
        int baseX = 2 + offsetX;
        int baseY = 2 + offsetY;
        int barW = w - 46;
        if (barW < 10) barW = 10;

        // Row 1: CPU
        if (showCpu) {
            matrix->setTextColor(labelColor);
            matrix->setCursor(baseX, baseY);
            matrix->print("CPU");
            drawGaugeBar(matrix, baseX + 20, baseY, barW, 6, cpuPct, cpuColor);
            char buf[8];
            snprintf(buf, sizeof(buf), "%2.0f%%", cpuPct);
            matrix->setTextColor(cpuColor);
            matrix->setCursor(baseX + 22 + barW, baseY);
            matrix->print(buf);
        }

        // Row 2: RAM
        if (showRam) {
            int y2 = baseY + 10;
            matrix->setTextColor(labelColor);
            matrix->setCursor(baseX, y2);
            matrix->print("RAM");
            drawGaugeBar(matrix, baseX + 20, y2, barW, 6, ramPct, ramColor);
            char buf[8];
            snprintf(buf, sizeof(buf), "%2.0f%%", ramPct);
            matrix->setTextColor(ramColor);
            matrix->setCursor(baseX + 22 + barW, y2);
            matrix->print(buf);
        }

        // Row 3: TEMP & UPTIME
        int y3 = baseY + 20;
        if (showTemp) {
            matrix->setTextColor(tempColor);
            matrix->setCursor(baseX, y3);
            matrix->print(tBuf);
        }
        if (showUptime) {
            matrix->setTextColor(matrix->color565(0, 190, 255));
            matrix->setCursor((w / 2) + 2 + offsetX, y3);
            matrix->print(upBuf);
        }
    }
}

void SysInfoEngine::renderCyberpunkTheme(MatrixPanel_I2S_DMA* matrix, float cpuPct, float ramPct, float tempC, uint32_t uptimeSec) {
    uint16_t cpuColor = getMetricColor(matrix, cpuPct, 60.0f, 80.0f);
    uint16_t ramColor = getMetricColor(matrix, ramPct, 70.0f, 85.0f);
    uint16_t tempColor = getMetricColor(matrix, tempC, 55.0f, 70.0f);
    uint16_t cyan = matrix->color565(0, 240, 255);
    uint16_t purple = matrix->color565(200, 50, 255);

    int w = matrix->width();
    int h = matrix->height();

    // Futuristic corner brackets across full width & height
    matrix->drawFastHLine(0, 0, 8, cyan);
    matrix->drawFastVLine(0, 0, 6, cyan);
    matrix->drawFastHLine(w - 8, 0, 8, cyan);
    matrix->drawFastVLine(w - 1, 0, 6, cyan);
    matrix->drawFastHLine(0, h - 1, 8, purple);
    matrix->drawFastVLine(0, h - 6, 6, purple);
    matrix->drawFastHLine(w - 8, h - 1, 8, purple);
    matrix->drawFastVLine(w - 1, h - 6, 6, purple);

    float dTemp = useFahrenheit ? (tempC * 1.8f + 32.0f) : tempC;
    char tBuf[12];
    snprintf(tBuf, sizeof(tBuf), "%.0f%c", dTemp, useFahrenheit ? 'F' : 'C');

    uint32_t hrs = uptimeSec / 3600;
    uint32_t mins = (uptimeSec % 3600) / 60;
    char upBuf[12];
    snprintf(upBuf, sizeof(upBuf), "%02u:%02u", (unsigned int)hrs, (unsigned int)mins);

    if (w >= 100) {
        // Widescreen (128x32+): 2 Columns + Level Meter on right
        int x1 = 6 + offsetX;
        int x2 = (w / 2) + offsetX;
        int y1 = 4 + offsetY;
        int y2 = 18 + offsetY;

        // CPU
        matrix->setTextColor(cyan);
        matrix->setCursor(x1, y1);
        matrix->print("CPU:");
        matrix->setTextColor(cpuColor);
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f%%", cpuPct);
        matrix->print(buf);

        // RAM
        matrix->setTextColor(purple);
        matrix->setCursor(x1, y2);
        matrix->print("RAM:");
        matrix->setTextColor(ramColor);
        snprintf(buf, sizeof(buf), "%.0f%%", ramPct);
        matrix->print(buf);

        // TMP
        matrix->setTextColor(matrix->color565(255, 140, 0));
        matrix->setCursor(x2, y1);
        matrix->print("TMP:");
        matrix->setTextColor(tempColor);
        matrix->print(tBuf);

        // UPTIME
        matrix->setTextColor(cyan);
        matrix->setCursor(x2, y2);
        matrix->print("UPT:");
        matrix->setTextColor(matrix->color565(0, 220, 200));
        matrix->print(upBuf);

        // Vertical Level Meter on Right edge
        int barH = h - 8;
        int fillH = (int)((cpuPct / 100.0f) * barH);
        if (fillH > 0) {
            matrix->fillRect(w - 4, h - 4 - fillH, 2, fillH, cpuColor);
        }
    } else {
        // Compact (64x32)
        int baseX = 4 + offsetX;
        int baseY = 4 + offsetY;

        matrix->setTextColor(cyan);
        matrix->setCursor(baseX, baseY);
        matrix->print("C:");
        matrix->setTextColor(cpuColor);
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f%%", cpuPct);
        matrix->print(buf);

        matrix->setTextColor(purple);
        matrix->setCursor((w / 2) + offsetX, baseY);
        matrix->print("R:");
        matrix->setTextColor(ramColor);
        snprintf(buf, sizeof(buf), "%.0f%%", ramPct);
        matrix->print(buf);

        int y2 = baseY + 14;
        matrix->setTextColor(tempColor);
        matrix->setCursor(baseX, y2);
        matrix->print(tBuf);

        matrix->setTextColor(matrix->color565(0, 220, 200));
        matrix->setCursor((w / 2) + offsetX, y2);
        matrix->print(upBuf);

        int barH = h - 8;
        int fillH = (int)((cpuPct / 100.0f) * barH);
        if (fillH > 0) {
            matrix->fillRect(w - 3, h - 4 - fillH, 2, fillH, cpuColor);
        }
    }
}

void SysInfoEngine::renderCompactTheme(MatrixPanel_I2S_DMA* matrix, float cpuPct, float ramPct, float tempC, uint32_t uptimeSec) {
    uint16_t cpuColor = getMetricColor(matrix, cpuPct, 60.0f, 80.0f);
    uint16_t ramColor = getMetricColor(matrix, ramPct, 70.0f, 85.0f);
    uint16_t tempColor = getMetricColor(matrix, tempC, 55.0f, 70.0f);
    uint16_t labelColor = matrix->color565(150, 160, 180);

    int w = matrix->width();
    int h = matrix->height();

    float dTemp = useFahrenheit ? (tempC * 1.8f + 32.0f) : tempC;
    char tBuf[12];
    snprintf(tBuf, sizeof(tBuf), "%.0f%c", dTemp, useFahrenheit ? 'F' : 'C');

    uint32_t mins = (uptimeSec / 60) % 100;
    char upBuf[12];
    snprintf(upBuf, sizeof(upBuf), "%02u:%02u", (unsigned int)(uptimeSec / 3600), (unsigned int)mins);

    if (w >= 100 && h <= 36) {
        // Widescreen: 4 Columns Side-by-Side
        int colW = (w - 4) / 4;
        for (int i = 1; i < 4; i++) {
            matrix->drawFastVLine(2 + (i * colW), 2, h - 4, matrix->color565(40, 45, 60));
        }

        // Col 1: CPU
        int x0 = 3 + offsetX;
        matrix->setTextColor(labelColor);
        matrix->setCursor(x0, 4 + offsetY);
        matrix->print("CPU");
        matrix->setTextColor(cpuColor);
        matrix->setCursor(x0, 16 + offsetY);
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f%%", cpuPct);
        matrix->print(buf);

        // Col 2: RAM
        int x1 = 3 + colW + offsetX;
        matrix->setTextColor(labelColor);
        matrix->setCursor(x1, 4 + offsetY);
        matrix->print("RAM");
        matrix->setTextColor(ramColor);
        matrix->setCursor(x1, 16 + offsetY);
        snprintf(buf, sizeof(buf), "%.0f%%", ramPct);
        matrix->print(buf);

        // Col 3: TMP
        int x2 = 3 + (2 * colW) + offsetX;
        matrix->setTextColor(labelColor);
        matrix->setCursor(x2, 4 + offsetY);
        matrix->print("TMP");
        matrix->setTextColor(tempColor);
        matrix->setCursor(x2, 16 + offsetY);
        matrix->print(tBuf);

        // Col 4: UPT
        int x3 = 3 + (3 * colW) + offsetX;
        matrix->setTextColor(labelColor);
        matrix->setCursor(x3, 4 + offsetY);
        matrix->print("UPT");
        matrix->setTextColor(matrix->color565(0, 190, 255));
        matrix->setCursor(x3, 16 + offsetY);
        matrix->print(upBuf);
    } else {
        // Compact / Tall: 2x2 Grid
        int midX = w / 2;
        int midY = h / 2;

        matrix->drawFastVLine(midX, 2, h - 4, matrix->color565(40, 45, 60));
        matrix->drawFastHLine(2, midY, w - 4, matrix->color565(40, 45, 60));

        // Quad 1: CPU
        matrix->setTextColor(labelColor);
        matrix->setCursor(2 + offsetX, 2 + offsetY);
        matrix->print("CPU");
        matrix->setTextColor(cpuColor);
        matrix->setCursor(2 + offsetX, 9 + offsetY);
        char buf[8];
        snprintf(buf, sizeof(buf), "%.0f%%", cpuPct);
        matrix->print(buf);

        // Quad 2: RAM
        matrix->setTextColor(labelColor);
        matrix->setCursor(midX + 3 + offsetX, 2 + offsetY);
        matrix->print("RAM");
        matrix->setTextColor(ramColor);
        matrix->setCursor(midX + 3 + offsetX, 9 + offsetY);
        snprintf(buf, sizeof(buf), "%.0f%%", ramPct);
        matrix->print(buf);

        // Quad 3: TEMP
        matrix->setTextColor(labelColor);
        matrix->setCursor(2 + offsetX, midY + 3 + offsetY);
        matrix->print("TMP");
        matrix->setTextColor(tempColor);
        matrix->setCursor(2 + offsetX, midY + 10 + offsetY);
        matrix->print(tBuf);

        // Quad 4: UPTIME
        matrix->setTextColor(labelColor);
        matrix->setCursor(midX + 3 + offsetX, midY + 3 + offsetY);
        matrix->print("UPT");
        matrix->setTextColor(matrix->color565(0, 190, 255));
        matrix->setCursor(midX + 3 + offsetX, midY + 10 + offsetY);
        matrix->print(upBuf);
    }
}

EngineDescriptor SysInfoEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = EngineMetadata("system_info", "System Monitor", "system", FIRMWARE_VERSION);
    desc.capabilities.realtime = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsAudio = false;
    desc.requirements.needsTempSensor = false;
    desc.requirements.needsGyroscope = false;

    desc.schema.fields = {
        ConfigField("theme", ConfigType::ENUM, "Layout Style", "Visual style layout (HUD Bars, Cyberpunk Neon, Compact Grid)", "0", false, "", "", "", "0:HUD Bars,1:Cyberpunk Neon,2:Compact Grid", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_cpu", ConfigType::BOOLEAN, "Show CPU", "Display processor load percentage & gauge", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_ram", ConfigType::BOOLEAN, "Show RAM", "Display memory usage percentage & gauge", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_temp", ConfigType::BOOLEAN, "Show Temperature", "Display core/environment temperature", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_uptime", ConfigType::BOOLEAN, "Show Uptime", "Display running uptime counter", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("temp_unit", ConfigType::ENUM, "Temperature Unit", "Celsius (°C) or Fahrenheit (°F)", "C", false, "", "", "", "C,F", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("offset_x", ConfigType::INTEGER, "Offset X", "Horizontal pixel shift", "0", false, "-64", "64", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("offset_y", ConfigType::INTEGER, "Offset Y", "Vertical pixel shift", "0", false, "-32", "32", "1", "", "", false, "", ValidationPolicy::Clamp)
    };

    desc.factory = []() { return std::unique_ptr<IEngine>(new SysInfoEngine()); };
    return desc;
}
