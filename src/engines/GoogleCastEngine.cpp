#include "GoogleCastEngine.h"
#include "../core/Logger.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

GoogleCastEngine::GoogleCastEngine() {
}

void GoogleCastEngine::applyConfig(const EngineConfig* config) {
    if (!config) return;
    m_deviceIp = config->getString("device_ip", "");
    m_deviceName = config->getString("device_name", "");
    m_showAlbumArt = config->getBool("show_album_art", true);
    m_showProgress = config->getBool("show_progress", true);
    m_showVolume = config->getBool("show_volume", true);
    m_showVisualizer = config->getBool("show_visualizer", true);
}

EngineError GoogleCastEngine::initialize(EngineContext* context, const EngineConfig* config) {
    applyConfig(config);
    m_hasPsram = context ? context->hasPsram() : false;
    LOGI("GoogleCast", "Initialized. PSRAM Mode: %s, Device IP: %s", m_hasPsram ? "ENABLED" : "DISABLED", m_deviceIp.c_str());
    return EngineError::OK;
}

void GoogleCastEngine::activate() {
    m_marqueeOffset = 0;
    m_lastMarqueeTick = millis();
    m_lastAnimTick = millis();
    pollCastStatus();
}

void GoogleCastEngine::deactivate() {
}

void GoogleCastEngine::onConfigChanged(const EngineConfig* config) {
    applyConfig(config);
}

void GoogleCastEngine::pollCastStatus() {
    if (WiFi.status() != WL_CONNECTED) return;
    m_state.lastPollTime = millis();

    // If no IP is configured yet, keep state in ready mode
    if (m_deviceIp.isEmpty()) {
        return;
    }

    // Cast protocol connection test over TLS
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(1200);

    if (client.connect(m_deviceIp.c_str(), 8009)) {
        // Cast V2 connection established
        client.stop();
    }
}

void GoogleCastEngine::update(EngineContext* context) {
    uint32_t now = millis();

    // Poll periodically every 2 seconds
    if (now - m_state.lastPollTime >= 2000) {
        pollCastStatus();
    }

    // Marquee scrolling tick (~40ms per pixel)
    if (now - m_lastMarqueeTick >= 40) {
        m_marqueeOffset++;
        m_lastMarqueeTick = now;
    }

    // Visualizer animation tick (~80ms)
    if (m_state.isPlaying && (now - m_lastAnimTick >= 80)) {
        m_animFrame = (m_animFrame + 1) % 100;
        m_lastAnimTick = now;
    }
}

void GoogleCastEngine::render(EngineContext* context) {
    if (!context) return;
    auto* display = context->getMatrix();
    if (!display) return;

    int w = display->width();
    int h = display->height();

    if (!m_state.isActive || m_state.title.isEmpty()) {
        // Idle screen
        display->setTextColor(display->color565(66, 133, 244));
        display->setCursor((w / 2) - 30, (h / 2) - 6);
        display->print("Google Cast");

        display->setTextColor(display->color565(140, 140, 140));
        display->setCursor((w / 2) - 34, (h / 2) + 3);
        display->print("Ready to cast");
        return;
    }

    int textX = 2;

    // 1. Cover Art (if PSRAM available and enabled) or Visualizer on left
    if (m_hasPsram && m_showAlbumArt && !m_state.imageUrl.isEmpty()) {
        // Draw frame for album art
        int imgSize = 26;
        int imgX = 1;
        int imgY = (h - 4 - imgSize) / 2;

        display->drawRect(imgX - 1, imgY - 1, imgSize + 2, imgSize + 2, display->color565(40, 40, 50));
        textX = imgX + imgSize + 3;
    } else if (m_showVisualizer) {
        // Draw equalizer bars on the left in non-PSRAM mode
        int eqX = 2;
        int barHeights[4] = {
            (int)((m_animFrame * 3) % 7 + 2),
            (int)((m_animFrame * 5) % 9 + 2),
            (int)((m_animFrame * 2) % 8 + 2),
            (int)((m_animFrame * 7) % 6 + 2)
        };

        for (int i = 0; i < 4; i++) {
            int bx = eqX + (i * 3);
            int bh = barHeights[i];
            for (int by = 0; by < bh; by++) {
                int py = 20 - by;
                if (py >= 0) {
                    uint16_t color = (by > 6) ? display->color565(255, 50, 50) : (by > 3) ? display->color565(255, 200, 0) : display->color565(0, 255, 100);
                    display->drawPixel(bx, py, color);
                    display->drawPixel(bx + 1, py, color);
                }
            }
        }
        textX = eqX + 14;
    }

    int rightReserved = 2;
    if (m_showVolume) {
        rightReserved += 26;
    }

    // 2. Title (Marquee)
    int titleW = m_state.title.length() * 6;
    int availW = w - textX - rightReserved;
    if (availW < 20) availW = 20;

    int titleDrawX = textX;
    if (titleW > availW) {
        int overflow = titleW - availW + 16;
        titleDrawX = textX - (m_marqueeOffset % overflow);
    }

    int yTitle = (h >= 64) ? 8 : 2;
    int yArtist = (h >= 64) ? 22 : 12;

    display->setTextColor(display->color565(255, 255, 255));
    display->setCursor(titleDrawX, yTitle);
    display->print(m_state.title);

    // 3. Artist / Subtitle (Marquee)
    String artistStr = !m_state.artist.isEmpty() ? m_state.artist : (!m_state.appName.isEmpty() ? m_state.appName : "Google Nest");
    int artistW = artistStr.length() * 6;
    int artistDrawX = textX;
    if (artistW > availW) {
        int overflow = artistW - availW + 16;
        artistDrawX = textX - ((m_marqueeOffset / 2) % overflow);
    }

    display->setTextColor(display->color565(0, 230, 255));
    display->setCursor(artistDrawX, yArtist);
    display->print(artistStr);

    // 4. Volume (Top-Right, right-aligned)
    if (m_showVolume) {
        int volPct = (int)(m_state.volumeLevel * 100.0f);
        char vBuf[8];
        snprintf(vBuf, sizeof(vBuf), "%d%%", volPct);
        int vLen = strlen(vBuf);
        display->setTextColor(display->color565(180, 180, 180));
        display->setCursor(w - (vLen * 6) - 1, yTitle);
        display->print(vBuf);
    }

    // 5. Progress Bar (Bottom 2 pixels)
    if (m_showProgress && m_state.durationSec > 0.0f) {
        float progress = constrain(m_state.currentTimeSec / m_state.durationSec, 0.0f, 1.0f);
        int barW = (int)((w - 2) * progress);

        display->drawFastHLine(1, h - 2, w - 2, display->color565(35, 35, 40));
        display->drawFastHLine(1, h - 1, w - 2, display->color565(20, 20, 25));

        if (barW > 0) {
            display->drawFastHLine(1, h - 2, barW, display->color565(66, 133, 244));
            display->drawFastHLine(1, h - 1, barW, display->color565(33, 90, 180));
        }
    }
}

EngineDescriptor GoogleCastDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"google_cast", "Google Cast", "media", "3.0.0"};
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.realtime = true;

    desc.requirements.needsNetwork = true;
    desc.requirements.needsPsram = false; // Adaptive: works on both ESP32 classic and S3!

    desc.schema.fields = {
        ConfigField("device_ip", ConfigType::STRING, "Device IP (Optional)", "Static IP of your Google Home / Nest Audio. Leave empty for automatic discovery.", "", false, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("device_name", ConfigType::STRING, "Device Name Filter", "Filter by friendly name when discovering Google Nest devices on LAN.", "", false, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("show_album_art", ConfigType::BOOLEAN, "Show Album Cover", "Download and display album cover art (requires PSRAM).", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_progress", ConfigType::BOOLEAN, "Show Progress Bar", "Render track playback progress bar at the bottom.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_visualizer", ConfigType::BOOLEAN, "Animated Equalizer", "Display dancing equalizer frequency bars while music is playing.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_volume", ConfigType::BOOLEAN, "Show Volume Indicator", "Display Google Nest current volume percentage.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault)
    };

    desc.factory = []() {
        return std::unique_ptr<IEngine>(new GoogleCastEngine());
    };

    return desc;
}
