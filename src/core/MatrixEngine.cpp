#include "MatrixEngine.h"
#include "RenderStats.h"

RenderStats g_renderStats;
#include "../hal/HardwareHAL.h"
#include "Logger.h"
#include "../../include/HardwareProfile.h"

/**
 * @brief Construct a new Matrix Engine object.
 * Initializes the display pointer to nullptr.
 */
MatrixEngine::MatrixEngine() : display(nullptr) {}

/**
 * @brief Destroy the Matrix Engine object.
 * Safely deletes the display instance and frees DMA memory.
 */
MatrixEngine::~MatrixEngine() {
    if (display) {
        delete display;
    }
}

/**
 * @brief Initialize the hardware matrix panel.
 * 
 * Automatically adjusts color depth and double-buffering based on the total 
 * physical pixel count to prevent ESP32 memory limits from being exceeded.
 * 
 * @param config The MatrixConfig loaded from config.json
 * @return true if DMA allocation and initialization succeeded.
 * @return false if out of memory or initialization failed.
 */
bool MatrixEngine::begin(const MatrixConfig& config) {
    int8_t out1[3] = {MATRIX_R1_PIN, MATRIX_G1_PIN, MATRIX_B1_PIN};
    int8_t out2[3] = {MATRIX_R2_PIN, MATRIX_G2_PIN, MATRIX_B2_PIN};
    int8_t pins1[3] = {MATRIX_R1_PIN, MATRIX_G1_PIN, MATRIX_B1_PIN};
    int8_t pins2[3] = {MATRIX_R2_PIN, MATRIX_G2_PIN, MATRIX_B2_PIN};
    
    if (config.rgbSequence.length() >= 3) {
        String seq = config.rgbSequence;
        seq.toUpperCase();
        for(int i = 0; i < 3; i++) {
            if(seq[i] == 'R') { out1[0] = pins1[i]; out2[0] = pins2[i]; }
            else if(seq[i] == 'G') { out1[1] = pins1[i]; out2[1] = pins2[i]; }
            else if(seq[i] == 'B') { out1[2] = pins1[i]; out2[2] = pins2[i]; }
        }
    }

    HUB75_I2S_CFG::i2s_pins _pins = {
        out1[0], out1[1], out1[2],
        out2[0], out2[1], out2[2],
        MATRIX_A_PIN, MATRIX_B_PIN, MATRIX_C_PIN,
        MATRIX_D_PIN, MATRIX_E_PIN,
        MATRIX_LAT_PIN, MATRIX_OE_PIN, MATRIX_CLK_PIN
    };

    HUB75_I2S_CFG mxconfig(
        config.width,      // Module width
        config.height,     // Module height
        config.chainLength,// Chain length
        _pins              // Custom pin mapping
    );
    
    // Use configured per-channel color depth (2 to 8, default 8)
    int depth = config.colorDepth;
    if (depth <= 0) {
        depth = 8; // Default fallback
    } else if (depth > 8) {
        // Clamp to 8 (max supported color depth bits per channel by the library).
        // A value of 11 (legacy PWM bits) should map to 8 bits color depth, not 3.
        depth = 8;
    }
    if (depth < 2 || depth > 8) {
        depth = 8; // Safe fallback if invalid range
    }
    
    mxconfig.setPixelColorDepthBits(depth);
    mxconfig.min_refresh_rate = config.limitRefreshRateHz > 0 ? config.limitRefreshRateHz : 90;
    mxconfig.latch_blanking = config.latchBlanking > 0 ? config.latchBlanking : 8;
    mxconfig.clkphase = config.clkPhase;

    String chip = config.driverChip;
    chip.toUpperCase();
    if (chip == "FM6124") {
        mxconfig.driver = HUB75_I2S_CFG::FM6124;
    } else if (chip == "FM6126" || chip == "FM6126A") {
        mxconfig.driver = HUB75_I2S_CFG::FM6126A;
    } else if (chip == "ICN2038S" || chip == "ICN2037" || chip == "SM16208") {
        mxconfig.driver = HUB75_I2S_CFG::ICN2038S;
    } else {
        mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
    }

    // Apply double buffering if not forced to single
    mxconfig.double_buff = !config.forceSingleBuffer;
    
    // PSRAM Warning for large panels
    if (config.width * config.height * config.chainLength >= 16384) { 
        if (!hardwareHAL.capabilities().hasPsram) {
            Serial.println("WARNING: 256x64 requested but no PSRAM found! This WILL cause Out-Of-Memory bootloops on a standard ESP32 WROOM.");
            // We no longer force 3-bit color here, because the user explicitly wants 24-bit on ESP32-S3.
        } else {
            LOGI("MatrixEngine", "PSRAM found. 256x64 will use PSRAM for DMA buffering safely.");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
            LOGW("MatrixEngine", "WARNING: default HUB75 pin map uses GPIO32/33 which conflicts with ESP32-S3 octal PSRAM. Verify/adjust pin map if needed.");
#endif
        }
    }

    // Initialize display object
    m_panel = new FastMatrixPanel(mxconfig);
    display = m_panel;
    
    // Allocate memory and start DMA
    if (!display->begin()) {
        LOGE("MatrixEngine", "Failed to allocate memory for Matrix DMA!");
        return false;
    }

    m_doubleBuffered = mxconfig.double_buff;
    m_panel->setBuffering(m_doubleBuffered);
    display->setBrightness8(64); // Safe default brightness
    m_panel->rememberBrightness8(64);
    display->clearScreen();
    present();
    display->clearScreen();
    present();

    return true;
}

void MatrixEngine::present() {
    if (!display) return;
    display->flipDMABuffer();
    if (m_panel) m_panel->noteFlip();
    m_flipCount++;
    g_renderStats.presents++;
}

void MatrixEngine::clear() {
    if (display) {
        display->clearScreen();
    }
}

void MatrixEngine::setBrightness(uint8_t brightness) {
    if (display) {
        if (brightness == 0) {
            display->setBrightness8(0);
            if (m_panel) m_panel->rememberBrightness8(0);
            return;
        }
        // brightness is 0-100 (percentage), setBrightness8 expects 0-255
        uint8_t scaledBrightness = (brightness * 255) / 100;
        if (scaledBrightness < 25) {
            scaledBrightness = 25; // Minimum floor to prevent driver chip OE pulse blanking
        }
        display->setBrightness8(scaledBrightness);
        if (m_panel) m_panel->rememberBrightness8(scaledBrightness);
    }
}

MatrixPanel_I2S_DMA* MatrixEngine::getDisplay() {
    return display;
}

void MatrixEngine::blitCanvas565(const uint16_t* src, int canvasWidth, int canvasHeight) {
    if (m_panel) {
        m_panel->blitCanvas565(src, canvasWidth, canvasHeight);
    }
}

void FastMatrixPanel::fillScreen(uint16_t color) {
    if (color != 0) {
        MatrixPanel_I2S_DMA::fillScreen(color);
        return;
    }
    clearFrameBuffer(m_back);
    setBrightness8(m_brightness8);
}

void FastMatrixPanel::setBuffering(bool doubleBuffered) {
    m_double = doubleBuffered;
    m_back = doubleBuffered ? 1 : 0;
    initLuts(m_cfg.getPixelColorDepthBits());
}

void FastMatrixPanel::initLuts(uint8_t depth) {
    if (depth < 2) depth = 8;
    if (depth > 8) depth = 8;
    m_depth = depth;
    uint8_t shift = 8 - depth;
    uint8_t round = (shift > 0) ? (1 << (shift - 1)) : 0;
    uint16_t maxVal = (1 << depth) - 1;

    for (int r = 0; r < 32; r++) {
        uint8_t r8 = (r << 3) | (r >> 2);
        uint16_t val = lumConvTab_8bit[r8];
        uint16_t scaled = (val + round) >> shift;
        m_lut_r[r] = (scaled > maxVal) ? (uint8_t)maxVal : (uint8_t)scaled;
    }
    for (int g = 0; g < 64; g++) {
        uint8_t g8 = (g << 2) | (g >> 4);
        uint16_t val = lumConvTab_8bit[g8];
        uint16_t scaled = (val + round) >> shift;
        m_lut_g[g] = (scaled > maxVal) ? (uint8_t)maxVal : (uint8_t)scaled;
    }
    for (int b = 0; b < 32; b++) {
        uint8_t b8 = (b << 3) | (b >> 2);
        uint16_t val = lumConvTab_8bit[b8];
        uint16_t scaled = (val + round) >> shift;
        m_lut_b[b] = (scaled > maxVal) ? (uint8_t)maxVal : (uint8_t)scaled;
    }
}

void FastMatrixPanel::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (!initialized || x < 0 || x >= _width || y < 0 || y >= _height) return;
    int16_t w = 1, h = 1;
    transform(x, y, w, h);

    if (x < 0 || x >= (int16_t)PIXELS_PER_ROW || y < 0 || y >= (int16_t)m_cfg.mx_height) return;

    uint8_t r_val = m_lut_r[(color >> 11) & 0x1F];
    uint8_t g_val = m_lut_g[(color >> 5) & 0x3F];
    uint8_t b_val = m_lut_b[color & 0x1F];

    uint16_t x_adj = MATRIX_TX_ADJUST(x);
    uint16_t colourbitclear = BITMASK_RGB1_CLEAR;
    uint16_t colourbitoffset = 0;

    if (y >= ROWS_PER_FRAME) {
        colourbitoffset = BITS_RGB2_OFFSET;
        colourbitclear = BITMASK_RGB2_CLEAR;
        y -= ROWS_PER_FRAME;
    }

    auto& targetFb = frame_buffer[m_back];
    if (y >= (int16_t)targetFb.rowBits.size()) return;

    for (uint8_t p = 0; p < m_depth; ++p) {
        uint16_t mask = (1 << p);
        uint16_t rgb = 0;
        if (r_val & mask) rgb |= 1;
        if (g_val & mask) rgb |= 2;
        if (b_val & mask) rgb |= 4;
        rgb <<= colourbitoffset;

        uint16_t* ptr = targetFb.rowBits[y]->getDataPtr(p);
        ptr[x_adj] = (ptr[x_adj] & colourbitclear) | rgb;
    }
}

void FastMatrixPanel::blitCanvas565(const uint16_t* src, int canvasWidth, int canvasHeight) {
    if (!src || !initialized) return;

    if (getRotation() != 0 && getRotation() != 2) {
        for (int y = 0; y < canvasHeight; y++) {
            const uint16_t* r = src + (size_t)y * canvasWidth;
            for (int x = 0; x < canvasWidth; x++) {
                drawPixel(x, y, r[x]);
            }
        }
        return;
    }

    const int w = PIXELS_PER_ROW;
    const int rpf = ROWS_PER_FRAME;
    if (canvasWidth != w || canvasHeight != (int)m_cfg.mx_height) {
        return;
    }

    auto& targetFb = frame_buffer[m_back];
    if ((int)targetFb.rowBits.size() < rpf) return;

    bool rot180 = (getRotation() == 2);

    for (int y = 0; y < rpf; y++) {
        const uint16_t* src1;
        const uint16_t* src2;
        if (!rot180) {
            src1 = src + (size_t)y * w;
            src2 = src + (size_t)(y + rpf) * w;
        } else {
            src1 = src + (size_t)(m_cfg.mx_height - 1 - y) * w;
            src2 = src + (size_t)(m_cfg.mx_height - 1 - (y + rpf)) * w;
        }

        for (uint8_t p = 0; p < m_depth; p++) {
            uint16_t* dmaRow = targetFb.rowBits[y]->getDataPtr(p);
            for (int x = 0; x < w; x++) {
                uint16_t c1, c2;
                if (!rot180) {
                    c1 = src1[x];
                    c2 = src2[x];
                } else {
                    c1 = src1[w - 1 - x];
                    c2 = src2[w - 1 - x];
                }

                uint8_t r1 = (m_lut_r[(c1 >> 11) & 0x1F] >> p) & 1;
                uint8_t g1 = (m_lut_g[(c1 >> 5) & 0x3F] >> p) & 1;
                uint8_t b1 = (m_lut_b[c1 & 0x1F] >> p) & 1;
                uint8_t r2 = (m_lut_r[(c2 >> 11) & 0x1F] >> p) & 1;
                uint8_t g2 = (m_lut_g[(c2 >> 5) & 0x3F] >> p) & 1;
                uint8_t b2 = (m_lut_b[c2 & 0x1F] >> p) & 1;

                uint16_t rgb = r1 | (g1 << 1) | (b1 << 2) | (r2 << 3) | (g2 << 4) | (b2 << 5);
                int ax = MATRIX_TX_ADJUST(x);
                dmaRow[ax] = (dmaRow[ax] & BITMASK_RGB12_CLEAR) | rgb;
            }
        }
    }
}
