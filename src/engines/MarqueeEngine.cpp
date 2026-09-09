#include "MarqueeEngine.h"
#include <string.h>
#include "core/BuildInfo.h"
#include "../core/Logger.h"

MarqueeEngine::MarqueeEngine()
    : panelWidth(0), panelHeight(0), m_rawBuffer(nullptr),
      m_active(false), m_hasRawBuffer(false), m_rawStartTime(0), m_rawDurationMs(0),
      m_hasPsram(false), m_filePath("/marquees/custom_marquee.gif"),
      m_speedMultiplier(1.0f), m_fitMode("fit"), m_gifEngine(nullptr) {
}

EngineError MarqueeEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (!context) return EngineError::InitializationFailed;
    auto matrix = context->getMatrix();
    if (!matrix) return EngineError::HardwareUnavailable;

    m_hasPsram = context->hasPsram();
    panelWidth = matrix->width();
    panelHeight = matrix->height();

    size_t bufferSize = (size_t)panelWidth * panelHeight * sizeof(uint16_t);
    if (m_hasPsram) {
        m_rawBuffer = (uint16_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
    } else {
        m_rawBuffer = (uint16_t*)malloc(bufferSize);
    }

    if (!m_gifEngine) {
        m_gifEngine = new GifEngine();
        m_gifEngine->initialize(context, engineConfig);
        m_gifEngine->begin(matrix);
    }

    if (engineConfig) onConfigChanged(engineConfig);
    return EngineError::OK;
}

MarqueeEngine::~MarqueeEngine() {
    if (m_rawBuffer) {
        if (m_hasPsram) heap_caps_free(m_rawBuffer);
        else free(m_rawBuffer);
        m_rawBuffer = nullptr;
    }
    if (m_gifEngine) {
        delete m_gifEngine;
        m_gifEngine = nullptr;
    }
}

void MarqueeEngine::show(const uint8_t* rgb565Data, size_t len, unsigned long durationSeconds) {
    if (!m_rawBuffer || len != expectedBufferBytes()) return;
    memcpy(m_rawBuffer, rgb565Data, len);
    m_hasRawBuffer = true;
    m_active = true;
    m_rawStartTime = millis();
    m_rawDurationMs = durationSeconds * 1000UL;
    if (m_gifEngine) {
        m_gifEngine->stop();
    }
}

void MarqueeEngine::setMarqueeFile(const char* path) {
    if (!path || strlen(path) == 0) return;
    m_filePath = String(path);
    m_hasRawBuffer = false;
    if (m_active && m_gifEngine) {
        m_gifEngine->playGif(m_filePath.c_str());
    }
}

String MarqueeEngine::resolveMarqueeFile() const {
    if (m_filePath.length() > 0 && sd.exists(m_filePath.c_str())) {
        return m_filePath;
    }
    const char* fallbacks[] = {
        "/marquees/custom_marquee.gif",
        "/marquees/custom_marquee.png",
        "/marquees/custom_marquee.raw",
        "/marquees/marquee.gif",
        "/marquees/marquee.png",
        "/marquees/marquee.raw"
    };
    for (const char* fb : fallbacks) {
        if (sd.exists(fb)) {
            return String(fb);
        }
    }
    return "";
}

void MarqueeEngine::activate() {
    m_active = true;
    if (m_hasRawBuffer) {
        return;
    }
    String file = resolveMarqueeFile();
    if (file.length() > 0 && m_gifEngine) {
        LOGI("MarqueeEngine", "Activating marquee file: %s", file.c_str());
        m_gifEngine->playGif(file.c_str());
    } else {
        LOGD("MarqueeEngine", "No marquee file found on SD, idling.");
    }
}

void MarqueeEngine::deactivate() {
    m_active = false;
    if (m_gifEngine) {
        m_gifEngine->deactivate();
    }
}

void MarqueeEngine::onConfigChanged(const EngineConfig* engineConfig) {
    if (!engineConfig) return;
    m_filePath = engineConfig->getString("file_path", "/marquees/custom_marquee.gif");
    m_speedMultiplier = engineConfig->getFloat("speed_multiplier", 1.0f);
    m_fitMode = engineConfig->getString("fit_mode", "fit");
}

void MarqueeEngine::update(EngineContext* context) {
    if (!m_active) return;

    if (m_hasRawBuffer) {
        if (millis() - m_rawStartTime >= m_rawDurationMs) {
            m_active = false;
            m_hasRawBuffer = false;
        }
    } else if (m_gifEngine) {
        m_gifEngine->update(context);
        if (m_gifEngine->isFinished()) {
            String file = resolveMarqueeFile();
            if (file.length() > 0) {
                m_gifEngine->playGif(file.c_str());
            }
        }
    }
}

void MarqueeEngine::render(EngineContext* context) {
    if (!m_active) return;

    if (m_hasRawBuffer && m_rawBuffer) {
        auto matrix = context ? context->getMatrix() : nullptr;
        if (!matrix) return;
        for (int y = 0; y < panelHeight; y++) {
            for (int x = 0; x < panelWidth; x++) {
                matrix->drawPixel(x, y, m_rawBuffer[y * panelWidth + x]);
            }
        }
    }
    // GIF/PNG decoding draws directly to matrix in update()/callbacks
}

void MarqueeEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    if (panelWidth != geometry.width || panelHeight != geometry.height) {
        panelWidth = geometry.width;
        panelHeight = geometry.height;
        size_t newSize = (size_t)panelWidth * panelHeight * sizeof(uint16_t);
        if (m_rawBuffer) {
            if (m_hasPsram) heap_caps_free(m_rawBuffer);
            else free(m_rawBuffer);
            m_rawBuffer = nullptr;
        }
        if (m_hasPsram) {
            m_rawBuffer = (uint16_t*)heap_caps_malloc(newSize, MALLOC_CAP_SPIRAM);
        } else {
            m_rawBuffer = (uint16_t*)malloc(newSize);
        }
    }
    if (m_gifEngine) {
        m_gifEngine->onDisplayGeometryChanged(geometry);
    }
}

EngineDescriptor MarqueeEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"marquee", "Gameroom Marquee", "arcade", FIRMWARE_VERSION};
    desc.capabilities.allowRotation = true;
    desc.capabilities.realtime = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsAudio = false;
    desc.requirements.needsNetwork = false;
    desc.schema.fields = {
        ConfigField("file_path", ConfigType::STRING, "Marquee File", "Path to marquee GIF or image on SD", "/marquees/custom_marquee.gif", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("speed_multiplier", ConfigType::FLOAT, "Speed Multiplier", "Animation playback speed factor", "1.0", false, "0.25", "3.0", "0.25", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("fit_mode", ConfigType::ENUM, "Fit Mode", "Display scaling mode (fit, center, stretch)", "fit", false, "", "", "", "fit,center,stretch", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MarqueeEngine()); };
    return desc;
}

