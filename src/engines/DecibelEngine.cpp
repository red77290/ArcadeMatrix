#include "DecibelEngine.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"



DecibelEngine::DecibelEngine() 
    : active(false), currentDb(40.0f), currentLevel(NOISE_CALM) {}

DecibelEngine::~DecibelEngine() {}

EngineError DecibelEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

void DecibelEngine::activate() {
    active = true;
    hardwareHAL.startAudioSampling();
    LOGI("DecibelEngine", "DecibelEngine ACTIVATED (Lazy Audio Sampling started).");
}

void DecibelEngine::deactivate() {
    active = false;
    // Only stop audio sampling if Visualizer is NOT running
    if (!config_visualizer_enabled) {
        hardwareHAL.stopAudioSampling();
    }
    LOGI("DecibelEngine", "DecibelEngine DEACTIVATED (Lazy Audio Sampling stopped).");
}

void DecibelEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (engineConfig) {
        config_visualizer_enabled = engineConfig->getBool("visualizer_enabled", false);
    }
}

void DecibelEngine::updateStatusLevel(float db) {
    if (db < 20.0f) {
        currentLevel = NOISE_CALM;
    } else if (db < 40.0f) {
        currentLevel = NOISE_NORMAL;
    } else if (db < 60.0f) {
        currentLevel = NOISE_MODERATE;
    } else if (db < 80.0f) {
        currentLevel = NOISE_VIGILANCE;
    } else if (db < 100.0f) {
        currentLevel = NOISE_LIMIT;
    } else {
        currentLevel = NOISE_ALERT;
    }
}

uint16_t DecibelEngine::getGaugeColorForDb(MatrixPanel_I2S_DMA* matrix, float dbVal) {
    if (!matrix) return 0xFFFF;
    if (dbVal < 20.0f) {
        return matrix->color565(0, 140, 255);   // 🟦 Bleu (0-20 dB) — Calme / Silence
    } else if (dbVal < 40.0f) {
        return matrix->color565(0, 220, 80);    // 🟩 Vert (20-40 dB) — Bruit agréable
    } else if (dbVal < 60.0f) {
        return matrix->color565(255, 220, 0);   // 🟨 Jaune (40-60 dB) — Bruit tolérable
    } else if (dbVal < 80.0f) {
        return matrix->color565(255, 130, 0);   // 🟧 Orange (60-80 dB) — Bruit fatigant
    } else if (dbVal < 100.0f) {
        return matrix->color565(255, 30, 30);   // 🟥 Rouge (80-100 dB) — Bruit dangereux
    } else {
        return matrix->color565(180, 0, 255);   // 🟪 Violet (> 100 dB) — Bruit douloureux
    }
}

uint16_t DecibelEngine::getLevelColor(MatrixPanel_I2S_DMA* matrix, NoiseStatusLevel level) {
    if (!matrix) return 0xFFFF;
    switch (level) {
        case NOISE_CALM:     return matrix->color565(0, 140, 255);  // 🟦 Bleu (0-20 dB)
        case NOISE_NORMAL:   return matrix->color565(0, 220, 80);   // 🟩 Vert (20-40 dB)
        case NOISE_MODERATE: return matrix->color565(255, 220, 0);  // 🟨 Jaune (40-60 dB)
        case NOISE_VIGILANCE:return matrix->color565(255, 130, 0);  // 🟧 Orange (60-80 dB)
        case NOISE_LIMIT:    return matrix->color565(255, 30, 30);  // 🟥 Rouge (80-100 dB)
        case NOISE_ALERT:
        default:             return matrix->color565(180, 0, 255);  // 🟪 Violet (> 100 dB)
    }
}

const char* DecibelEngine::getLevelText(NoiseStatusLevel level) {
    extern ConfigLoader config;
    String lang = config.getInstance("weather_main") ? config.getInstance("weather_main")->config.getString("lang") : "en";
    lang.toLowerCase();

    if (lang == "fr") {
        switch (level) {
            case NOISE_CALM:     return "SILENCE";
            case NOISE_NORMAL:   return "PAISIBLE";
            case NOISE_MODERATE: return "MODERE";
            case NOISE_VIGILANCE:return "ELEVE";
            case NOISE_LIMIT:    return "BRUYANT";
            case NOISE_ALERT:
            default:             return "ALERTE";
        }
    } else if (lang == "es") {
        switch (level) {
            case NOISE_CALM:     return "SILENCIO";
            case NOISE_NORMAL:   return "TRANQUIL";
            case NOISE_MODERATE: return "MODERADO";
            case NOISE_VIGILANCE:return "ELEVADO";
            case NOISE_LIMIT:    return "RUIDOSO";
            case NOISE_ALERT:
            default:             return "ALERTA";
        }
    } else {
        // English default
        switch (level) {
            case NOISE_CALM:     return "SILENT";
            case NOISE_NORMAL:   return "QUIET";
            case NOISE_MODERATE: return "MODERATE";
            case NOISE_VIGILANCE:return "ELEVATED";
            case NOISE_LIMIT:    return "LOUD";
            case NOISE_ALERT:
            default:             return "ALERT";
        }
    }
}

void DecibelEngine::drawVsGauge(MatrixPanel_I2S_DMA* matrix, float db) {
    if (!matrix) return;
    int width = matrix->width();
    int height = matrix->height();

    // Gauge height: 3px for standard 32px height displays, 4px for 64px+
    int gaugeHeight = (height >= 64) ? 4 : 3;

    // Dark empty frame background (VS fighting healthbar frame)
    matrix->fillRect(0, 0, width, gaugeHeight, matrix->color565(25, 25, 30));

    // Filled width proportional to 0-110 dB SPL
    int filledWidth = (int)((db / 110.0f) * (float)width);
    if (filledWidth < 0) filledWidth = 0;
    if (filledWidth > width) filledWidth = width;

    // Draw multi-color scale per column
    for (int x = 0; x < filledWidth; x++) {
        float dbAtCol = ((float)x / (float)width) * 110.0f;
        uint16_t col = getGaugeColorForDb(matrix, dbAtCol);
        matrix->fillRect(x, 0, 1, gaugeHeight, col);
    }

    // Lead white tip cursor for VS fighting healthbar effect
    if (filledWidth > 0 && filledWidth <= width) {
        matrix->fillRect(filledWidth - 1, 0, 1, gaugeHeight, matrix->color565(255, 255, 255));
    }

    // Bottom border frame line
    matrix->drawFastHLine(0, gaugeHeight, width, matrix->color565(70, 70, 85));
}

void DecibelEngine::drawSmileyIcon(MatrixPanel_I2S_DMA* matrix, int x, int y, NoiseStatusLevel level) {
    if (!matrix) return;
    uint16_t color = getLevelColor(matrix, level);
    uint16_t eyeColor = (level == NOISE_ALERT || level == NOISE_LIMIT) ? matrix->color565(255, 255, 255) : matrix->color565(0, 0, 0);
    uint16_t faceBg = (level == NOISE_ALERT) ? matrix->color565(180, 0, 255) : color;

    // Outer face circle (14x14)
    matrix->fillCircle(x + 7, y + 7, 7, faceBg);

    // Eyes
    matrix->fillRect(x + 4, y + 4, 2, 3, eyeColor);
    matrix->fillRect(x + 8, y + 4, 2, 3, eyeColor);

    // Mouth variations according to dB status scale
    switch (level) {
        case NOISE_CALM:
        case NOISE_NORMAL:
            // Big smile 😊 🙂
            matrix->drawPixel(x + 3, y + 8, eyeColor);
            matrix->drawLine(x + 4, y + 9, x + 9, y + 9, eyeColor);
            matrix->drawPixel(x + 10, y + 8, eyeColor);
            break;

        case NOISE_MODERATE:
            // Straight neutral line 😐
            matrix->drawLine(x + 4, y + 9, x + 9, y + 9, eyeColor);
            break;

        case NOISE_VIGILANCE:
            // Slightly worried curve ⚠️
            matrix->drawLine(x + 4, y + 9, x + 6, y + 8, eyeColor);
            matrix->drawLine(x + 7, y + 8, x + 9, y + 9, eyeColor);
            break;

        case NOISE_LIMIT:
            // Frown / Sad mouth 🙁
            matrix->drawPixel(x + 3, y + 10, eyeColor);
            matrix->drawLine(x + 4, y + 9, x + 9, y + 9, eyeColor);
            matrix->drawPixel(x + 10, y + 10, eyeColor);
            break;

        case NOISE_ALERT:
        default:
            // Open shocked/angry mouth (flashing) 🚨
            if ((millis() / 250) % 2 == 0) {
                matrix->fillRect(x + 5, y + 8, 4, 4, matrix->color565(255, 255, 255));
            } else {
                matrix->fillRect(x + 4, y + 8, 6, 4, matrix->color565(0, 0, 0));
            }
            break;
    }
}

void DecibelEngine::update(EngineContext* context) {
    extern ConfigLoader config;
    auto inst = config.getInstance("decibel_main"); if (inst) hardwareHAL.setMicGain(inst->config.getFloat("gain", 1.0f));
    auto vis = config.getInstance("visualizer_main");
    float rawDb = hardwareHAL.getDecibels(vis ? vis->config.getFloat("db_calibration", 0.0f) : 0.0f);

    // Fast Attack (0.75), Smooth Decay (0.15) for immediate clap response and fluid movement
    if (rawDb > currentDb) {
        currentDb = currentDb * 0.25f + rawDb * 0.75f;
    } else {
        currentDb = currentDb * 0.85f + rawDb * 0.15f;
    }

    updateStatusLevel(currentDb);
}

void DecibelEngine::render(EngineContext* context) {
    auto* matrix = context->getMatrix();
    if (!matrix) return;

    matrix->fillScreen(0);
    matrix->setTextWrap(false); // CRITICAL: Prevent text wrapping to avoid overlapping lines!

    int width = matrix->width();
    int height = matrix->height();
    uint16_t levelCol = getLevelColor(matrix, currentLevel);

    // 1. Draw VS Fighting Healthbar Gauge on top of display (y = 0)
    drawVsGauge(matrix, currentDb);

    int topOffset = (height >= 64) ? 5 : 4; // Start content below top VS gauge

    matrix->setFont(nullptr);

    char dbBuf[16];
    snprintf(dbBuf, sizeof(dbBuf), "%.0f dB", currentDb);

    if (width >= 128) {
        // Wide display layout (128x32, 256x64, etc.)
        int textSize = (width >= 256 && height >= 64) ? 2 : 1;
        matrix->setTextSize(textSize);

        // Draw Smiley Icon on Left
        int smileySize = (textSize == 2) ? 28 : 14;
        int smileyY = topOffset + ((height - topOffset - smileySize) / 2);
        drawSmileyIcon(matrix, 6, smileyY, currentLevel);

        int textX = (textSize == 2) ? 42 : 22;

        if (textSize == 2) {
            // 256x64 Large Matrix Display: Print dB on top line (y=12) and Status text on bottom line (y=36)
            matrix->setTextColor(levelCol);
            matrix->setCursor(textX, 12);
            matrix->print(dbBuf);

            matrix->setTextColor(matrix->color565(180, 185, 200));
            matrix->setCursor(textX, 36);
            matrix->print(getLevelText(currentLevel));
        } else {
            // 128x32 Medium Matrix Display
            matrix->setTextColor(levelCol);
            matrix->setCursor(textX, 5);
            matrix->print(dbBuf);

            matrix->setTextColor(matrix->color565(180, 185, 200));
            matrix->setCursor(textX, 18);
            matrix->print(getLevelText(currentLevel));
        }

        // Horizontal Segmented VU Meter on bottom
        int vuWidth = width - 8;
        int filledWidth = (int)(((currentDb - 30.0f) / 70.0f) * (float)vuWidth);
        if (filledWidth < 0) filledWidth = 0;
        if (filledWidth > vuWidth) filledWidth = vuWidth;

        matrix->drawRect(4, height - 4, vuWidth, 3, matrix->color565(60, 60, 60));
        matrix->fillRect(4, height - 4, filledWidth, 3, levelCol);
    } else {
        // Standard 64x32 display layout
        matrix->setFont(NULL);
        matrix->setTextSize(1);
        matrix->setTextWrap(false);

        // Draw Smiley Icon on Left (14x14 pixels, centered vertically at y=10)
        drawSmileyIcon(matrix, 1, 10, currentLevel);

        // Top Right: dB Numeric Value (y = 5..11)
        matrix->setTextColor(levelCol);
        matrix->setCursor(17, 5);
        matrix->print(dbBuf);

        // Bottom Right: Status String in FR / EN / ES (y = 19..25)
        matrix->setTextColor(matrix->color565(180, 185, 200));
        matrix->setCursor(17, 19);
        matrix->print(getLevelText(currentLevel));

        // Right-hand side mini LED Level Indicator bar (x = 62..63, y = 5..28)
        int availableH = height - 7;
        int barHeight = (int)(((currentDb - 30.0f) / 80.0f) * (float)availableH);
        if (barHeight < 0) barHeight = 0;
        if (barHeight > availableH) barHeight = availableH;
        matrix->fillRect(width - 2, height - 2 - barHeight, 2, barHeight, levelCol);
    }
}

EngineDescriptor DecibelEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_decibel;
    desc_decibel.metadata = {"decibelMeter", "Noise Level", "audio", FIRMWARE_VERSION};
    desc_decibel.capabilities.realtime = true;
    desc_decibel.requirements.needsAudio = true;
    desc_decibel.schema.fields = {
        ConfigField("threshold", ConfigType::INTEGER, "Alert Threshold (dB)", "Warning threshold level", "80", false, "40", "120", "5", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc_decibel.factory = []() { return std::unique_ptr<IEngine>(new DecibelEngine()); };
    return desc_decibel;
}



