#include "MarqueeEngine.h"
#include <string.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "../core/net/SecureHttpClient.h"
#include "../core/Globals.h"
#include "../core/SdLockGuard.h"
#include "../core/NetworkBudget.h"
#include "core/BuildInfo.h"
#include "../core/Logger.h"
#include "../core/drawing/IDrawingSurface.h"

MarqueeEngine::MarqueeEngine()
    : panelWidth(0), panelHeight(0), m_rawBuffer(nullptr),
      m_active(false), m_hasRawBuffer(false), m_rawStartTime(0), m_rawDurationMs(0),
      m_hasPsram(false), m_filePath("/marquees/custom_marquee.gif"),
      m_speedMultiplier(1.0f), m_fitMode("fit"), m_gifEngine(nullptr) {
}

EngineError MarqueeEngine::initialize(EngineContext* context, const EngineConfig* engineConfig) {
    if (!context) return EngineError::InitializationFailed;
    auto surface = context->getSurface();
    auto matrix = context->getMatrix();
    if (!surface && !matrix) return EngineError::HardwareUnavailable;

    m_context = context;
    m_hasPsram = context->hasPsram();
    panelWidth = surface ? surface->width() : matrix->width();
    panelHeight = surface ? surface->height() : matrix->height();

    // MEMORY OPTIMIZATION: Do not eagerly allocate 8KB m_rawBuffer at boot time.
    // Instead, allocate it on-demand in show() if raw pixel streaming is actually used.
    // REVERT INSTRUCTION: Uncomment the eager allocation below if immediate pre-allocation is required.
    m_rawBuffer = nullptr;
    /*
    size_t bufferSize = (size_t)panelWidth * panelHeight * sizeof(uint16_t);
    if (m_hasPsram) {
        m_rawBuffer = (uint16_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
    } else {
        m_rawBuffer = (uint16_t*)malloc(bufferSize);
    }
    */

    if (!m_gifEngine) {
        m_gifEngine = new GifEngine();
        m_gifEngine->initialize(context, engineConfig);
        m_gifEngine->begin(surface ? surface : context->getSurface());
        m_gifEngine->setFitMode(m_fitMode);
        m_gifEngine->setSpeedMultiplier(m_speedMultiplier);
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
    size_t expected = expectedBufferBytes();
    if (len != expected) return;
    if (!m_rawBuffer) {
        if (m_hasPsram) {
            m_rawBuffer = (uint16_t*)heap_caps_malloc(expected, MALLOC_CAP_SPIRAM);
        } else {
            m_rawBuffer = (uint16_t*)malloc(expected);
        }
    }
    if (!m_rawBuffer) return;
    memcpy(m_rawBuffer, rgb565Data, len);
    m_hasRawBuffer = true;
    m_active = true;
    m_rawStartTime = millis();
    m_rawDurationMs = durationSeconds * 1000UL;
    m_clearFramesRemaining.store(2, std::memory_order_relaxed);
    auto surface = m_context ? m_context->getSurface() : nullptr;
    if (surface) {
        surface->clear(0);
    }
    if (m_gifEngine) {
        m_gifEngine->stop();
    }
}

void MarqueeEngine::setMarqueeFile(const char* path) {
    if (!path || strlen(path) == 0) return;
    m_filePath = String(path);
    m_hasRawBuffer = false;
    m_clearFramesRemaining.store(2, std::memory_order_relaxed);
    if (m_active && m_gifEngine) {
        m_gifEngine->setFitMode(m_fitMode);
        m_gifEngine->setSpeedMultiplier(m_speedMultiplier);
        m_gifEngine->playGif(m_filePath.c_str());
    }
}

namespace {

struct ImageHeaderInfo {
    int width = -1;
    int height = -1;
    bool isGif = false;
    bool isPng = false;
    bool valid = false;
};

static ImageHeaderInfo parseImageDimensions(const uint8_t* header, size_t len) {
    ImageHeaderInfo info;
    if (len >= 10 && memcmp(header, "GIF8", 4) == 0) {
        info.width = header[6] | (header[7] << 8);
        info.height = header[8] | (header[9] << 8);
        info.isGif = true;
        info.valid = (info.width > 0 && info.height > 0);
        return info;
    }
    if (len >= 24 && header[0] == 0x89 && header[1] == 'P' && header[2] == 'N' && header[3] == 'G') {
        info.width = (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];
        info.height = (header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23];
        info.isPng = true;
        info.valid = (info.width > 0 && info.height > 0);
        return info;
    }
    return info;
}

static bool writeStreamToSd(Stream& s, const uint8_t* initialHeader, size_t initialHeaderLen, size_t totalExpectedLen, const String& destPath) {
    SdLockGuard guard(pdMS_TO_TICKS(3000));
    if (!guard) return false;
    if (!sd.exists("/marquees")) sd.mkdir("/marquees");
    FsFile f = sd.open(destPath.c_str(), FILE_OPEN_WRITE);
    if (!f) return false;

    if (initialHeader && initialHeaderLen > 0) {
        f.write(initialHeader, initialHeaderLen);
    }
    uint8_t chunk[512];
    size_t written = initialHeaderLen;
    while (totalExpectedLen == 0 || written < totalExpectedLen) {
        int avail = s.available();
        if (avail > 0) {
            int toRead = std::min(avail, (int)sizeof(chunk));
            if (totalExpectedLen > 0 && written + toRead > totalExpectedLen) {
                toRead = totalExpectedLen - written;
            }
            int r = s.readBytes(reinterpret_cast<char*>(chunk), toRead);
            if (r > 0) {
                f.write(chunk, r);
                written += r;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
            if (!s.available()) break;
        }
    }
    f.close();
    return written > 0;
}

} // anonymous namespace

bool MarqueeEngine::downloadUrlWithResizeCheck(const String& targetUrl, String& outDestPath) {
    if (WiFi.status() != WL_CONNECTED || targetUrl.isEmpty()) return false;

    int w = panelWidth > 0 ? panelWidth : 128;
    int h = panelHeight > 0 ? panelHeight : 32;

    // 1. Inspect target image resolution directly
    bool isHttps = targetUrl.startsWith("https://");
    uint8_t header[32];
    size_t headerBytes = 0;
    ImageHeaderInfo headerInfo;
    bool directInspectionSuccess = false;

    if (isHttps) {
        net::SecureHttpOptions options;
        options.requestTimeoutMs = 5000;
        options.userAgent = "Mozilla/5.0 ArcadeMatrix/3.2";
        auto response = net::SecureHttpClient::get(targetUrl, options);
        if (response.ok()) {
            Stream& s = response.stream();
            uint32_t startMs = millis();
            while (headerBytes < sizeof(header) && (millis() - startMs < 2000)) {
                if (s.available()) {
                    header[headerBytes++] = (uint8_t)s.read();
                } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
            }
            headerInfo = parseImageDimensions(header, headerBytes);
            if (headerInfo.valid && headerInfo.width == w && headerInfo.height == h) {
                outDestPath = headerInfo.isGif ? "/marquees/marquee.gif" : "/marquees/marquee.png";
                LOGI("MarqueeEngine", "Marquee matches panel resolution (%dx%d), downloading directly as %s", w, h, outDestPath.c_str());
                return writeStreamToSd(s, header, headerBytes, response.contentLength(), outDestPath);
            }
            directInspectionSuccess = true;
        }
    } else {
        HTTPClient http;
        WiFiClient client;
        http.setTimeout(5000);
        http.setUserAgent("Mozilla/5.0 ArcadeMatrix/3.2");
        if (http.begin(client, targetUrl)) {
            int code = http.GET();
            if (code == 200) {
                int contentLen = http.getSize();
                WiFiClient* s = http.getStreamPtr();
                uint32_t startMs = millis();
                while (headerBytes < sizeof(header) && (millis() - startMs < 2000)) {
                    if (s->available()) {
                        header[headerBytes++] = (uint8_t)s->read();
                    } else {
                        vTaskDelay(pdMS_TO_TICKS(5));
                    }
                }
                headerInfo = parseImageDimensions(header, headerBytes);
                if (headerInfo.valid && headerInfo.width == w && headerInfo.height == h) {
                    outDestPath = headerInfo.isGif ? "/marquees/marquee.gif" : "/marquees/marquee.png";
                    LOGI("MarqueeEngine", "Marquee matches panel resolution (%dx%d), downloading directly as %s", w, h, outDestPath.c_str());
                    bool ok = writeStreamToSd(*s, header, headerBytes, contentLen, outDestPath);
                    http.end();
                    client.stop();
                    return ok;
                }
                directInspectionSuccess = true;
            }
            http.end();
            client.stop();
        }
    }

    if (directInspectionSuccess && headerInfo.valid) {
        LOGI("MarqueeEngine", "Marquee resolution (%dx%d) != panel (%dx%d), resizing via proxy", headerInfo.width, headerInfo.height, w, h);
    } else {
        LOGI("MarqueeEngine", "Marquee format requires proxy normalization/resize (%dx%d)", w, h);
    }

    // 2. Fetch resized image via proxy (wsrv.nl HTTPS first, weserv.nl HTTP fallback)
    String fitParam = "contain";
    if (m_fitMode == "stretch") fitParam = "fill";
    else if (m_fitMode == "center") fitParam = "cover";

    outDestPath = "/marquees/marquee.png";
    String secureProxyUrl = "https://wsrv.nl/?url=" + targetUrl + "&w=" + String(w) + "&h=" + String(h) + "&fit=" + fitParam + "&output=png";
    net::SecureHttpOptions options;
    options.requestTimeoutMs = 6000;
    options.userAgent = "Mozilla/5.0 ArcadeMatrix/3.2";
    auto response = net::SecureHttpClient::get(secureProxyUrl, options);
    if (response.ok()) {
        return writeStreamToSd(response.stream(), nullptr, 0, response.contentLength(), outDestPath);
    }

    String proxyUrl = "http://images.weserv.nl/?url=" + targetUrl + "&w=" + String(w) + "&h=" + String(h) + "&fit=" + fitParam + "&output=png";
    HTTPClient http;
    WiFiClient client;
    http.setTimeout(6000);
    http.setUserAgent("Mozilla/5.0 ArcadeMatrix/3.2");
    if (http.begin(client, proxyUrl)) {
        int code = http.GET();
        if (code == 200) {
            bool ok = writeStreamToSd(*http.getStreamPtr(), nullptr, 0, http.getSize(), outDestPath);
            http.end();
            client.stop();
            return ok;
        }
        http.end();
        client.stop();
    }

    return false;
}

String MarqueeEngine::resolveMarqueeFile() {
    if (m_filePath.startsWith("http://") || m_filePath.startsWith("https://")) {
        String downloadedFile;
        if (downloadUrlWithResizeCheck(m_filePath, downloadedFile)) {
            return downloadedFile;
        }
    }
    SdLockGuard guard(pdMS_TO_TICKS(1500));
    if (!guard) return "";

    if (m_filePath.length() > 0 && sd.exists(m_filePath.c_str())) {
        return m_filePath;
    }
    const char* fallbacks[] = {
        "/marquees/marquee.gif",
        "/marquees/marquee.png",
        "/marquees/marquee.jpg",
        "/marquees/marquee.raw",
        "/marquees/custom_marquee.gif",
        "/marquees/custom_marquee.png",
        "/marquees/custom_marquee.raw"
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
    m_clearFramesRemaining.store(2, std::memory_order_relaxed);
    auto surface = m_context ? m_context->getSurface() : nullptr;
    if (surface) {
        surface->clear(0);
    }
    if (m_hasRawBuffer) {
        return;
    }
    String file = resolveMarqueeFile();
    if (file.length() > 0 && m_gifEngine) {
        LOGI("MarqueeEngine", "Activating marquee file: %s (fit=%s, speed=%.2f)", file.c_str(), m_fitMode.c_str(), m_speedMultiplier);
        m_gifEngine->setFitMode(m_fitMode);
        m_gifEngine->setSpeedMultiplier(m_speedMultiplier);
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
    m_filePath = engineConfig->getString("file_path", "/marquees/marquee.gif");
    m_speedMultiplier = engineConfig->getFloat("speed_multiplier", 1.0f);
    m_fitMode = engineConfig->getString("fit_mode", "fit");
    if (m_gifEngine) {
        m_gifEngine->setFitMode(m_fitMode);
        m_gifEngine->setSpeedMultiplier(m_speedMultiplier);
    }
}

void MarqueeEngine::update(EngineContext* context) {
    if (!m_active) return;

    if (m_hasRawBuffer) {
        if (millis() - m_rawStartTime >= m_rawDurationMs) {
            m_active = false;
            m_hasRawBuffer = false;
            // On classic ESP32 without PSRAM, free the 8KB raw buffer immediately after playback to recover DRAM
            if (m_rawBuffer && !m_hasPsram) {
                free(m_rawBuffer);
                m_rawBuffer = nullptr;
            }
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

bool MarqueeEngine::isFinished() const {
    if (m_hasRawBuffer) {
        return (millis() - m_rawStartTime >= m_rawDurationMs);
    }
    return false;
}

void MarqueeEngine::render(EngineContext* context) {
    if (!m_active) return;
    if (m_hasRawBuffer && m_rawBuffer) {
        auto surface = context ? context->getSurface() : nullptr;
        if (surface) {
            surface->blit565(m_rawBuffer, 0, 0, panelWidth, panelHeight, panelWidth);
        } else {
            auto matrix = context ? context->getMatrix() : nullptr;
            if (!matrix) return;
            for (int y = 0; y < panelHeight; y++) {
                for (int x = 0; x < panelWidth; x++) {
                    matrix->drawPixel(x, y, m_rawBuffer[y * panelWidth + x]);
                }
            }
        }
    } else if (m_gifEngine) {
        m_gifEngine->render(context);
    }
}

void MarqueeEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    panelWidth = geometry.width;
    panelHeight = geometry.height;
    if (m_rawBuffer) {
        if (m_hasPsram) heap_caps_free(m_rawBuffer);
        else free(m_rawBuffer);
        m_rawBuffer = nullptr;
    }
    // Buffer will be reallocated on demand in show() if needed
    if (m_gifEngine) {
        m_gifEngine->onDisplayGeometryChanged(geometry);
    }
}

EngineDescriptor MarqueeEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"marquee", "Gameroom Marquee", "arcade", FIRMWARE_VERSION};
    desc.capabilities.allowRotation = true;
    desc.capabilities.realtime = true;
    desc.capabilities.selfPaced = false;
    desc.requirements.needsAudio = false;
    desc.requirements.needsNetwork = false;
    desc.requirements.needsSd = true;
    desc.requirements.targetFps = 60;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 8000;
    desc.requirements.internalContiguousBytes = 16000;
    desc.schema.fields = {
        ConfigField("file_path", ConfigType::FILE_ASSET, "Marquee File", "Path to marquee GIF or image on SD", "/marquees/marquee.gif", false, "", "", "", ".gif,.png,.jpg,.jpeg", "/api/upload?target=marquee", false, "", ValidationPolicy::Accept),
        ConfigField("speed_multiplier", ConfigType::FLOAT, "Speed Multiplier", "Animation playback speed factor", "1.0", false, "0.25", "3.0", "0.25", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("fit_mode", ConfigType::ENUM, "Fit Mode", "Display scaling mode (fit, center, stretch)", "fit", false, "", "", "", "fit,center,stretch", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MarqueeEngine()); };
    return desc;
}

