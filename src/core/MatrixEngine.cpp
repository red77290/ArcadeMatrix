#include "MatrixEngine.h"
#include "RenderStats.h"
#if defined(SPIRAM_DMA_BUFFER)
#include "rom/cache.h"   // the DMA engine reads PSRAM directly: every write below must be written back
#endif
#include <esp_task_wdt.h>

RenderStats g_renderStats;
#include "../hal/HardwareHAL.h"
#include "../hal/BoardProfile.h"
#include "Logger.h"
#include "drawing/Hub75BulkEncoder.h"
#include "drawing/Hub75PresentationBackend.h"
#include "drawing/PipelineSelectionPolicy.h"
#include "drawing/DmaMemoryLayout.h"
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
    m_presentationBackend.reset();
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
bool MatrixEngine::begin(const MatrixConfig& config, uint8_t effectiveColorDepth) {
    if (display) {
        delete display;
        display = nullptr;
        m_panel = nullptr;
    }
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

    HUB75_I2S_CFG::i2s_pins _pins;
    BoardProfile::current().populateMatrixPins(_pins);
    _pins.r1 = out1[0]; _pins.g1 = out1[1]; _pins.b1 = out1[2];
    _pins.r2 = out2[0]; _pins.g2 = out2[1]; _pins.b2 = out2[2];
    if (config.height < 64) {
        _pins.e = -1;
    }

    HUB75_I2S_CFG mxconfig(
        config.width,      // Module width
        config.height,     // Module height
        config.chainLength,// Chain length
        _pins              // Custom pin mapping
    );
    
    uint16_t totalWidth = config.width * (config.chainLength > 0 ? config.chainLength : 1);
    bool hasPsram = hardwareHAL.capabilities().hasPsram;

    uint8_t depth = effectiveColorDepth;
    if (depth == 0) {
        auto pipeRes = PipelineSelectionPolicy::evaluate(
            totalWidth, config.height, config.colorDepth, config.render_pipeline, hasPsram
        );
        depth = pipeRes.effectiveColorDepth;
    }

    LOGI("MatrixEngine", "Effective color depth: %u bits (configured=%d, geometry=%ux%u, psram=%d)",
         depth, config.colorDepth, totalWidth, config.height, hasPsram);
    
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
    } else if (chip == "MBI5124") {
        mxconfig.driver = HUB75_I2S_CFG::MBI5124;
    } else if (chip == "DP3246") {
        mxconfig.driver = HUB75_I2S_CFG::DP3246;
    } else {
        mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
    }

    // Evaluate canonical rendering pipeline & buffering using PipelineSelectionPolicy
    auto pipeRes = PipelineSelectionPolicy::evaluate(
        totalWidth, config.height, depth, config.render_pipeline, hasPsram
    );
    mxconfig.double_buff = pipeRes.descriptor.dmaDoubleBuffered;

    if (hasPsram) {
        LOGI("MatrixEngine", "PSRAM found. DMA buffering will use PSRAM safely.");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        LOGW("MatrixEngine", "WARNING: default HUB75 pin map uses GPIO32/33 which conflicts with ESP32-S3 octal PSRAM. Verify/adjust pin map if needed.");
#endif
    }

    // Initialize display object
    LOGI("MatrixEngine", "Allocating FastMatrixPanel...");
    m_panel = new FastMatrixPanel(mxconfig);
    display = m_panel;
    
    // Allocate memory and start DMA
    LOGI("MatrixEngine", "Calling display->begin()...");
    esp_task_wdt_reset();
    if (!display->begin()) {
        LOGE("MatrixEngine", "Failed to allocate memory for Matrix DMA!");
        return false;
    }
    esp_task_wdt_reset();
    LOGI("MatrixEngine", "display->begin() succeeded.");

    m_doubleBuffered = mxconfig.double_buff;
    m_panel->setBuffering(m_doubleBuffered);
    display->setBrightness8(64); // Safe default brightness
    m_panel->rememberBrightness8(64);

    m_cachedConfig = config;
    m_activeColorDepth = depth;
    m_oePin = _pins.oe;
    m_panel->initLuts(depth);

    // Initialize Presentation Backend FIRST, before any screen clears or presentations
    LOGI("MatrixEngine", "Initializing Hub75PresentationBackend...");
    m_presentationBackend.reset(new Hub75PresentationBackend(
        this, totalWidth, config.height, depth, m_doubleBuffered
    ));

    LOGI("MatrixEngine", "Clearing screen...");
    display->clearScreen();
    present();
    display->clearScreen();
    present();
    esp_task_wdt_reset();
    LOGI("MatrixEngine", "MatrixEngine::begin complete.");

    return true;
}

ReconfigureResult MatrixEngine::reconfigurePresentationPipeline(uint8_t targetDepth) {
    ReconfigureResult res;
    res.previousDepth = m_activeColorDepth;
    res.effectiveDepth = m_activeColorDepth;

    if (targetDepth < 2 || targetDepth > 8) {
        res.failureReason = "Invalid target depth (must be 2..8)";
        return res;
    }

    if (targetDepth == m_activeColorDepth && display != nullptr) {
        res.success = true;
        res.effectiveDepth = m_activeColorDepth;
        res.dmaBytes = m_panel ? m_panel->getDmaAllocatedBytes() : 0;
        return res;
    }

    LOGI("MatrixEngine", "Starting Presentation Pipeline Reconfiguration: %u -> %u bits...",
         m_activeColorDepth, targetDepth);

    uint32_t startUs = micros();

    // Invariant 21 (HUB75 Output Isolation):
    // Blank display output immediately via hardware OE pin to prevent any optical glitches or scanline debris
    setBlank(true);
    if (m_oePin >= 0) {
        pinMode(m_oePin, OUTPUT);
        digitalWrite(m_oePin, HIGH); // OE active-low: HIGH = LEDs completely disabled
    }

    uint8_t prevBrightness = m_panel ? m_panel->getBrightness8() : 64;

    // Teardown previous DMA display
    if (display) {
        delete display;
        display = nullptr;
        m_panel = nullptr;
    }

    // Allocate new panel at targetDepth
    bool allocSuccess = begin(m_cachedConfig, targetDepth);
    if (!allocSuccess) {
        LOGE("MatrixEngine", "Failed to allocate pipeline at %u bits! Attempting fallback to previous depth %u bits...",
             targetDepth, res.previousDepth);
        res.fallbackUsed = true;
        bool fallbackOk = begin(m_cachedConfig, res.previousDepth);
        if (!fallbackOk) {
            LOGE("MatrixEngine", "CRITICAL: Fallback allocation failed! Trying safe static 4-bit baseline...");
            begin(m_cachedConfig, 4);
        }
        res.failureReason = "Target depth DMA allocation failed";
        res.success = false;
        res.effectiveDepth = m_activeColorDepth;
    } else {
        res.success = true;
        res.effectiveDepth = m_activeColorDepth;
        res.dmaBytes = m_panel ? m_panel->getDmaAllocatedBytes() : 0;
    }

    // Restore brightness
    if (display) {
        display->setBrightness8(prevBrightness);
        if (m_panel) m_panel->rememberBrightness8(prevBrightness);
    }

    // Release OE blanking (Invariant 21: only after frame 1 commit)
    setBlank(false);
    if (m_oePin >= 0) {
        digitalWrite(m_oePin, LOW); // OE enabled
    }

    res.blankDurationUs = micros() - startUs;
    LOGI("MatrixEngine", "Presentation Pipeline Reconfiguration completed in %u us (%s, effectiveDepth=%u bits, dmaBytes=%u)",
         res.blankDurationUs, res.success ? "SUCCESS" : "FALLBACK_FAILED", res.effectiveDepth, (unsigned)res.dmaBytes);

    return res;
}

IPresentationBackend* MatrixEngine::getPresentationBackend() {
    return m_presentationBackend.get();
}

void MatrixEngine::present() {
    if (!display) return;
    if (m_panel) m_panel->flushDirtyRows();
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

void MatrixEngine::setBlank(bool blank) {
    if (!display) return;
    if (blank) {
        display->setBrightness8(0);
        m_blanked = true;
    } else {
        uint8_t b = m_panel ? m_panel->getBrightness8() : 64;
        display->setBrightness8(b);
        m_blanked = false;
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

size_t FastMatrixPanel::getDmaAllocatedBytes() const {
    return DmaMemoryLayout::calculateTotalBytes((uint16_t)PIXELS_PER_ROW, (uint16_t)m_cfg.mx_height, m_depth, m_double);
}


void FastMatrixPanel::fillScreen(uint16_t color) {
    if (color != 0) {
        MatrixPanel_I2S_DMA::fillScreen(color);
        return;
    }
    auto& targetFb = frame_buffer[m_back];
    for (uint8_t p = 0; p < m_depth; ++p) {
        for (size_t y = 0; y < targetFb.rowBits.size(); ++y) {
            uint16_t* ptr = targetFb.rowBits[y]->getDataPtr(p);
            size_t w = targetFb.rowBits[y]->width;
            for (size_t x = 0; x < w; ++x) {
                ptr[x] &= BITMASK_RGB12_CLEAR;
            }
#if defined(SPIRAM_DMA_BUFFER)
            Cache_WriteBack_Addr((uint32_t)ptr, w * sizeof(uint16_t));
#endif
        }
    }
    m_dirtyRows[m_back] = 0;
}

void FastMatrixPanel::flushDirtyRows() {
#if defined(SPIRAM_DMA_BUFFER)
    uint32_t dirty = m_dirtyRows[m_back];
    if (dirty != 0) {
        auto& targetFb = frame_buffer[m_back];
        for (size_t y = 0; y < targetFb.rowBits.size(); ++y) {
            if (dirty & (1UL << y)) {
                for (uint8_t p = 0; p < m_depth; ++p) {
                    uint16_t* ptr = targetFb.rowBits[y]->getDataPtr(p);
                    Cache_WriteBack_Addr((uint32_t)ptr, (uint32_t)targetFb.rowBits[y]->width * sizeof(uint16_t));
                }
            }
        }
        m_dirtyRows[m_back] = 0;
    }
#endif
}

void FastMatrixPanel::setBuffering(bool doubleBuffered) {
    m_double = doubleBuffered;
    m_back = doubleBuffered ? 1 : 0;
    initLuts(m_cfg.getPixelColorDepthBits());
}

uint16_t* FastMatrixPanel::getBackbufferRowPlane(uint8_t row, uint8_t plane) {
    if (!initialized) return nullptr;
    auto& targetFb = frame_buffer[m_back];
    if (row < targetFb.rowBits.size()) {
        return targetFb.rowBits[row]->getDataPtr(plane);
    }
    return nullptr;
}

void FastMatrixPanel::initLuts(uint8_t depth) {
    if (depth < 2) depth = 2;
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

#if defined(SPIRAM_DMA_BUFFER)
    m_dirtyRows[m_back] |= (1UL << y);
#endif

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

    const int w = PIXELS_PER_ROW;
    const int rpf = ROWS_PER_FRAME;
    if (canvasWidth != w || canvasHeight != (int)m_cfg.mx_height) {
        return;
    }

    auto& targetFb = frame_buffer[m_back];
    if ((int)targetFb.rowBits.size() < rpf) return;

    Hub75EncodingParams params;
    params.colorDepth = m_depth;
    params.rowsPerFrame = rpf;
    params.width = w;
    params.height = m_cfg.mx_height;
    params.lutR = m_lut_r;
    params.lutG = m_lut_g;
    params.lutB = m_lut_b;
    params.rotation = 0;

    auto rowAccessor = [](void* ctx, uint8_t row, uint8_t plane) -> uint16_t* {
        auto* fb = static_cast<decltype(&targetFb)>(ctx);
        if (row < fb->rowBits.size()) {
            return fb->rowBits[row]->getDataPtr(plane);
        }
        return nullptr;
    };

    Hub75BulkEncoder::encode(src, w, rowAccessor, &targetFb, params);

#if defined(SPIRAM_DMA_BUFFER)
    for (int y = 0; y < rpf; y++) {
        for (uint8_t p = 0; p < m_depth; p++) {
            uint16_t* dmaRow = targetFb.rowBits[y]->getDataPtr(p);
            Cache_WriteBack_Addr((uint32_t)dmaRow, (uint32_t)w * sizeof(uint16_t));
        }
    }
#endif
    m_dirtyRows[m_back] = 0;
}
