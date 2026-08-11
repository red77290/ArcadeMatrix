#include "DecibelEngine.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"

extern ConfigLoader config;

DecibelEngine::DecibelEngine(MatrixPanel_I2S_DMA* display) 
    : matrix(display), active(false), currentDb(40.0f), currentLevel(NOISE_CALM) {}

DecibelEngine::~DecibelEngine() {}

void DecibelEngine::onActivate() {
    active = true;
    hardwareHAL.startAudioSampling();
    LOGI("DecibelEngine", "DecibelEngine ACTIVATED (Lazy Audio Sampling started).");
}

void DecibelEngine::onDeactivate() {
    active = false;
    // Only stop audio sampling if Visualizer is NOT running
    if (!config.audio.visualizer_enabled) {
        hardwareHAL.stopAudioSampling();
    }
    LOGI("DecibelEngine", "DecibelEngine DEACTIVATED (Lazy Audio Sampling stopped).");
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

uint16_t DecibelEngine::getGaugeColorForDb(float dbVal) {
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

uint16_t DecibelEngine::getLevelColor(NoiseStatusLevel level) {
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
    switch (level) {
        case NOISE_CALM:     return "SILENCE";
        case NOISE_NORMAL:   return "PAISIBLE";
        case NOISE_MODERATE: return "TOLERABLE";
        case NOISE_VIGILANCE:return "FATIGANT";
        case NOISE_LIMIT:    return "DANGEREUX";
        case NOISE_ALERT:
        default:             return "DOULOUREUX";
    }
}

void DecibelEngine::drawVsGauge(float db) {
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
        uint16_t col = getGaugeColorForDb(dbAtCol);
        matrix->fillRect(x, 0, 1, gaugeHeight, col);
    }

    // Lead white tip cursor for VS fighting healthbar effect
    if (filledWidth > 0 && filledWidth <= width) {
        matrix->fillRect(filledWidth - 1, 0, 1, gaugeHeight, matrix->color565(255, 255, 255));
    }

    // Bottom border frame line
    matrix->drawFastHLine(0, gaugeHeight, width, matrix->color565(70, 70, 85));
}

void DecibelEngine::drawSmileyIcon(int x, int y, NoiseStatusLevel level) {
    if (!matrix) return;
    uint16_t color = getLevelColor(level);
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

bool DecibelEngine::loop() {
    if (!matrix) return false;

    matrix->fillScreen(0);

    hardwareHAL.setMicGain(config.audio.mic_gain);
    float rawDb = hardwareHAL.getDecibels(config.audio.db_calibration);
    currentDb = rawDb;
    updateStatusLevel(currentDb);

    int width = matrix->width();
    int height = matrix->height();
    uint16_t levelCol = getLevelColor(currentLevel);

    // 1. Draw VS Fighting Healthbar Gauge on top of display (y = 0)
    drawVsGauge(currentDb);

    int topOffset = (height >= 64) ? 5 : 4; // Start content below top VS gauge

    matrix->setFont(nullptr);

    char dbBuf[16];
    snprintf(dbBuf, sizeof(dbBuf), "%.0fdB", currentDb);

    if (width >= 128) {
        // Wide display layout (128x32, 256x64, etc.)
        int textSize = (width >= 256 && height >= 64) ? 2 : 1;
        matrix->setTextSize(textSize);

        // Draw Smiley Icon (Matching gauge color & level)
        drawSmileyIcon(4, topOffset + ((height - topOffset) / 2) - 8, currentLevel);

        // dB Numeric Text
        matrix->setTextColor(levelCol);
        matrix->setCursor(22, topOffset + ((height - topOffset) / 2) - (textSize * 4));
        matrix->print(dbBuf);

        // Status Label Text
        matrix->setCursor(64, topOffset + ((height - topOffset) / 2) - (textSize * 4));
        matrix->setTextColor(matrix->color565(200, 200, 200));
        matrix->print(getLevelText(currentLevel));

        // Horizontal Segmented VU Meter on bottom
        int vuWidth = width - 8;
        int filledWidth = (int)(((currentDb - 30.0f) / 70.0f) * (float)vuWidth);
        if (filledWidth < 0) filledWidth = 0;
        if (filledWidth > vuWidth) filledWidth = vuWidth;

        matrix->drawRect(4, height - 4, vuWidth, 3, matrix->color565(60, 60, 60));
        matrix->fillRect(4, height - 4, filledWidth, 3, levelCol);
    } else {
        // Standard 64x32 display layout
        matrix->setTextSize(1);

        // Draw Smiley Icon on Left (Matching gauge color & level)
        drawSmileyIcon(2, topOffset + ((height - topOffset) / 2) - 7, currentLevel);

        // Top Right: dB Numeric Value
        matrix->setTextColor(levelCol);
        matrix->setCursor(18, topOffset + 2);
        matrix->print(dbBuf);

        // Bottom Right: Status String ("SILENCE", "PAISIBLE", "TOLERABLE", "FATIGANT", "DANGEREUX", "DOULOUREUX")
        matrix->setTextColor(matrix->color565(200, 200, 200));
        matrix->setCursor(18, topOffset + 14);
        matrix->print(getLevelText(currentLevel));

        // Right-hand side mini LED Level Indicator bar
        int availableH = height - topOffset - 4;
        int barHeight = (int)(((currentDb - 30.0f) / 70.0f) * (float)availableH);
        if (barHeight < 0) barHeight = 0;
        if (barHeight > availableH) barHeight = availableH;
        matrix->fillRect(width - 3, height - 2 - barHeight, 2, barHeight, levelCol);
    }

    return true;
}
