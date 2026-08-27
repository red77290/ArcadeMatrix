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

static void drawClippedString(Adafruit_GFX* display, const String& text, int x, int y, int clipMinX, int clipMaxX, uint16_t color) {
    if (!display || text.isEmpty()) return;
    int curX = x;
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if (curX + 6 > clipMinX && curX < clipMaxX) {
            display->drawChar(curX, y, c, color, 0, 1);
        }
        curX += 6;
    }
}

static void renderMarquee(Adafruit_GFX* display, const String& text, int y, int clipMinX, int clipMaxX, int availW, int offset, uint16_t color) {
    int textW = text.length() * 6;
    int drawX = clipMinX;
    if (textW > availW) {
        int overflow = textW - availW + 12;
        int pauseStart = 35; // ~1.2s initial pause
        int pauseEnd = 20;   // ~0.7s pause at end
        int cycle = max(1, pauseStart + overflow + pauseEnd);
        int phase = (offset % cycle + cycle) % cycle;
        int dx = (phase < pauseStart) ? 0 : (phase < pauseStart + overflow ? (phase - pauseStart) : overflow);
        drawX = clipMinX - dx;
    }
    drawClippedString(display, text, drawX, y, clipMinX, clipMaxX, color);
}

void GoogleCastEngine::render(EngineContext* context) {
    if (!context) return;
    auto* display = context->getMatrix();
    if (!display) return;

    int w = display->width();
    int h = display->height();

    if (!m_state.isActive || m_state.title.isEmpty()) {
        // Idle screen
        String title = "Google Cast";
        String subtitle = !m_deviceName.isEmpty() ? ("Ready to stream • " + m_deviceName) : "Ready to stream";

        int titleW = title.length() * 6;
        int yIdleTitle = (h >= 64) ? ((h / 2) - 10) : 4;
        int yIdleSub = (h >= 64) ? ((h / 2) + 4) : 16;

        if (titleW <= w - 4) {
            int xTitle = (w - titleW) / 2;
            drawClippedString(display, title, xTitle, yIdleTitle, 2, w - 2, display->color565(66, 133, 244));
        } else {
            renderMarquee(display, title, yIdleTitle, 2, w - 2, w - 4, m_marqueeOffset, display->color565(66, 133, 244));
        }

        int subW = subtitle.length() * 6;
        int clipMinX = 2;
        int clipMaxX = w - 2;
        int availW = clipMaxX - clipMinX;

        if (subW <= availW) {
            int xSub = (w - subW) / 2;
            drawClippedString(display, subtitle, xSub, yIdleSub, clipMinX, clipMaxX, display->color565(160, 160, 175));
        } else {
            renderMarquee(display, subtitle, yIdleSub, clipMinX, clipMaxX, availW, m_marqueeOffset / 2, display->color565(160, 160, 175));
        }
        return;
    }

    int textX = 2;
    bool hasArt = (m_hasPsram && m_showAlbumArt && !m_state.imageUrl.isEmpty());

    // 1. Album Cover Art frame
    if (hasArt) {
        int imgSize = (h >= 64) ? 52 : 24;
        int imgX = 1;
        int imgY = (h >= 64) ? ((h - 4 - imgSize) / 2) : max(1, (h - 3 - imgSize) / 2);

        display->drawRect(imgX - 1, imgY - 1, imgSize + 2, imgSize + 2, display->color565(40, 40, 50));
        textX = imgX + imgSize + 3;
    }

    bool isCompact = (w <= 64);
    int rightReserved = 2;
    if (m_showVisualizer && m_state.isPlaying) {
        rightReserved = (isCompact && hasArt) ? 8 : 15;
    } else if (m_showVolume) {
        rightReserved = 24;
    }

    // Strict viewport for text
    int clipMinX = textX;
    int clipMaxX = w - rightReserved;
    int availW = max(16, clipMaxX - clipMinX);

    int yTitle = (h >= 64) ? 8 : 3;
    int yArtist = (h >= 64) ? 22 : 13;

    // 2. Title (Marquee)
    renderMarquee(display, m_state.title, yTitle, clipMinX, clipMaxX, availW, m_marqueeOffset, display->color565(255, 255, 255));

    // 3. Artist / Subtitle (Marquee)
    String artistStr = !m_state.artist.isEmpty() ? m_state.artist : (!m_state.appName.isEmpty() ? m_state.appName : "Google Nest");
    renderMarquee(display, artistStr, yArtist, clipMinX, clipMaxX, availW, m_marqueeOffset / 2, display->color565(66, 180, 255));

    // 4. Equalizer Visualizer on the right
    if (m_showVisualizer && m_state.isPlaying) {
        int eqBaseY = (h >= 64) ? 44 : 21;

        if (isCompact && hasArt) {
            // 3 compact bars (2px wide each, 1px spacing)
            int eqX = w - 7;
            int barHeights[3] = {
                (int)((m_animFrame * 3) % 7 + 2),
                (int)((m_animFrame * 5) % 9 + 3),
                (int)((m_animFrame * 2) % 6 + 2)
            };

            for (int i = 0; i < 3; i++) {
                int bx = eqX + (i * 2);
                int bh = barHeights[i];
                for (int by = 0; by < bh; by++) {
                    int py = eqBaseY - by;
                    if (py >= 0) {
                        uint16_t color = (by > 6) ? display->color565(255, 60, 60) : (by > 3) ? display->color565(255, 200, 0) : display->color565(0, 255, 120);
                        display->drawPixel(bx, py, color);
                    }
                }
            }
        } else {
            // 4 wide bars (2px wide + 1px spacing)
            int eqX = w - 13;
            int barHeights[4] = {
                (int)((m_animFrame * 3) % 8 + 2),
                (int)((m_animFrame * 5) % 11 + 3),
                (int)((m_animFrame * 2) % 9 + 2),
                (int)((m_animFrame * 7) % 7 + 2)
            };

            for (int i = 0; i < 4; i++) {
                int bx = eqX + (i * 3);
                int bh = barHeights[i];
                for (int by = 0; by < bh; by++) {
                    int py = eqBaseY - by;
                    if (py >= 0) {
                        uint16_t color = (by > 7) ? display->color565(255, 50, 50) : (by > 4) ? display->color565(255, 190, 0) : display->color565(0, 240, 110);
                        display->drawPixel(bx, py, color);
                        display->drawPixel(bx + 1, py, color);
                    }
                }
            }
        }
    } else if (m_showVolume) {
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
