#include "VisualizerEngine.h"
#include "../core/ConfigLoader.h"
#include "../core/Logger.h"
#include <math.h>

extern ConfigLoader config;

VisualizerEngine::VisualizerEngine() 
    : active(false), currentMode(VISUALIZER_SPECTRUM), lastPeakDecay(0) {
    for (int i = 0; i < 128; i++) peakHold[i] = 0.0f;
}

VisualizerEngine::~VisualizerEngine() {}

EngineError VisualizerEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

void VisualizerEngine::activate() {
    active = true;
    hardwareHAL.startAudioSampling();
    LOGI("VisualizerEngine", "Music Visualizer STARTED.");
}

void VisualizerEngine::deactivate() {
    active = false;
    hardwareHAL.stopAudioSampling();
    LOGI("VisualizerEngine", "Music Visualizer STOPPED.");
}

void VisualizerEngine::setMode(const String& modeStr) {
    if (modeStr.equalsIgnoreCase("waveform")) {
        currentMode = VISUALIZER_WAVEFORM;
    } else if (modeStr.equalsIgnoreCase("radial")) {
        currentMode = VISUALIZER_RADIAL;
    } else if (modeStr.equalsIgnoreCase("neon_fire") || modeStr.equalsIgnoreCase("fire")) {
        currentMode = VISUALIZER_NEON_FIRE;
    } else {
        currentMode = VISUALIZER_SPECTRUM;
    }
}

void VisualizerEngine::onConfigChanged(const EngineConfig* engineConfig) {}

static uint16_t matrix_color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t VisualizerEngine::getSpectrumColor(int heightIndex, int maxHeight) {
    // We cannot use matrix color565 without context, but since this is an internal helper,
    // we can re-implement it without `matrix->color565`. It's basically RGB565.
    if (maxHeight <= 0) return 0xFFFF;
    float ratio = (float)heightIndex / (float)maxHeight;
    if (ratio < 0.4f) {
        // Cyan to Green
        return matrix_color565(0, (uint8_t)(200 * (ratio / 0.4f)), 255);
    } else if (ratio < 0.75f) {
        // Yellow to Orange
        float sub = (ratio - 0.4f) / 0.35f;
        return matrix_color565((uint8_t)(255 * sub), 255, 0);
    } else {
        // Red to Magenta
        float sub = (ratio - 0.75f) / 0.25f;
        return matrix_color565(255, (uint8_t)(50 * (1.0f - sub)), (uint8_t)(200 * sub));
    }
}



void VisualizerEngine::drawSpectrum(MatrixPanel_I2S_DMA* matrix) {
    int width = matrix->width();
    int height = matrix->height();

    // Scale number of spectrum bars based on width
    int numBars = width / 2;
    if (numBars > 128) numBars = 128;
    if (numBars < 8) numBars = 8;

    float bands[128];
    hardwareHAL.getAudioSpectrum(bands, numBars);

    // Peak decay timing
    bool decay = false;
    if (millis() - lastPeakDecay > 30) {
        decay = true;
        lastPeakDecay = millis();
    }

    int barWidth = width / numBars;
    if (barWidth < 1) barWidth = 1;

    for (int i = 0; i < numBars; i++) {
        float val = bands[i];
        int barHeight = (int)(val * (float)height);
        if (barHeight > height) barHeight = height;

        int x = i * barWidth;

        // Draw spectrum bar with vertical gradient
        for (int y = 0; y < barHeight; y++) {
            uint16_t col = getSpectrumColor(y, height);
            matrix->fillRect(x, height - 1 - y, barWidth - (barWidth > 1 ? 1 : 0), 1, col);
        }

        // Peak Hold Dot
        if (val > peakHold[i]) {
            peakHold[i] = val;
        } else if (decay) {
            peakHold[i] -= 0.05f;
            if (peakHold[i] < 0.0f) peakHold[i] = 0.0f;
        }

        int peakY = height - 1 - (int)(peakHold[i] * (float)height);
        if (peakY >= 0 && peakY < height) {
            matrix->fillRect(x, peakY, barWidth - (barWidth > 1 ? 1 : 0), 1, matrix->color565(255, 255, 255));
        }
    }
}

void VisualizerEngine::drawWaveform(MatrixPanel_I2S_DMA* matrix) {
    int width = matrix->width();
    int height = matrix->height();
    int midY = height / 2;

    float bands[128];
    int numBands = width;
    if (numBands > 128) numBands = 128;
    hardwareHAL.getAudioSpectrum(bands, numBands);

    uint16_t waveColor = matrix->color565(0, 255, 200);
    uint16_t glowColor = matrix->color565(0, 100, 255);

    int prevX = 0;
    int prevY = midY;

    for (int x = 0; x < width; x++) {
        int bandIdx = (x * numBands) / width;
        float amp = bands[bandIdx] * (height / 2.2f);
        int offset = (int)(amp * sinf((float)x * 0.2f + (millis() * 0.01f)));
        int y = midY + offset;

        if (y < 0) y = 0;
        if (y >= height) y = height - 1;

        if (x > 0) {
            matrix->drawLine(prevX, prevY + 1, x, y + 1, glowColor);
            matrix->drawLine(prevX, prevY, x, y, waveColor);
        }
        prevX = x;
        prevY = y;
    }
}

void VisualizerEngine::drawRadial(MatrixPanel_I2S_DMA* matrix) {
    int width = matrix->width();
    int height = matrix->height();
    int cx = width / 2;
    int cy = height / 2;

    float bands[16];
    hardwareHAL.getAudioSpectrum(bands, 16);

    float bassSum = 0.0f;
    for (int i = 0; i < 4; i++) bassSum += bands[i];
    float bass = bassSum / 4.0f;

    int maxRadius = (height < width ? height : width) / 2 - 1;
    int currentRadius = (int)(bass * maxRadius * 1.5f);
    if (currentRadius > maxRadius) currentRadius = maxRadius;
    if (currentRadius < 2) currentRadius = 2;

    uint16_t ringColor = matrix->color565(255, (uint8_t)(bass * 255), (uint8_t)(255 - bass * 255));
    matrix->drawCircle(cx, cy, currentRadius, ringColor);
    matrix->drawCircle(cx, cy, currentRadius / 2, matrix->color565(0, 200, 255));
}

void VisualizerEngine::drawNeonFire(MatrixPanel_I2S_DMA* matrix) {
    int width = matrix->width();
    int height = matrix->height();

    float bands[32];
    hardwareHAL.getAudioSpectrum(bands, 32);

    for (int x = 0; x < width; x++) {
        int bIdx = (x * 32) / width;
        float energy = bands[bIdx];
        int fireLen = (int)(energy * (float)height * 1.2f);
        if (fireLen > height) fireLen = height;

        for (int y = 0; y < fireLen; y++) {
            uint16_t col;
            if (y < height / 3) {
                col = matrix->color565(255, 0, (uint8_t)(200 * (y / (float)height))); // Neon Purple/Red
            } else if (y < (2 * height) / 3) {
                col = matrix->color565(255, 150, 0); // Orange
            } else {
                col = matrix->color565(255, 255, 100); // Yellow/White
            }
            matrix->drawPixel(x, height - 1 - y, col);
        }
    }
}

void VisualizerEngine::update(EngineContext* context) {
    auto inst = config.getInstance("visualizer_main"); if (inst) hardwareHAL.setMicGain(inst->config.getFloat("gain", 1.0f));
    auto inst2 = config.getInstance("visualizer_main"); if (inst2) setMode(inst2->config.getString("mode", "frequency"));
}

void VisualizerEngine::render(EngineContext* context) {
    auto* matrix = context->getMatrix();
    if (!matrix || !active) return;

    matrix->fillScreen(0);

    switch (currentMode) {
        case VISUALIZER_WAVEFORM: drawWaveform(matrix); break;
        case VISUALIZER_RADIAL: drawRadial(matrix); break;
        case VISUALIZER_NEON_FIRE: drawNeonFire(matrix); break;
        case VISUALIZER_SPECTRUM:
        default: drawSpectrum(matrix); break;
    }
}
