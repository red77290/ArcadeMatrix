#include "SpotifyEngine.h"
#include "../core/Logger.h"
#include "../services/ArtworkService.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

SpotifyEngine::SpotifyEngine() {
}

SpotifyEngine::~SpotifyEngine() {
    m_taskRunning = false;
    m_isActive = false;
    if (m_pollTaskHandle) {
        vTaskDelete(m_pollTaskHandle);
        m_pollTaskHandle = nullptr;
    }
}

void SpotifyEngine::applyConfig(const EngineConfig* config) {
    if (!config) return;
    m_clientId = config->getString("client_id", "");
    m_clientSecret = config->getString("client_secret", "");
    m_refreshToken = config->getString("refresh_token", "");
    m_showAlbumArt = config->getBool("show_album_art", true);
    m_showProgress = config->getBool("show_progress", true);
    m_showVolume = config->getBool("show_volume", true);
    m_showVisualizer = config->getBool("show_visualizer", true);
    m_accessToken = "";
    m_tokenExpiry = 0;
}

void SpotifyEngine::pollTaskStatic(void* pvParameters) {
    auto* self = static_cast<SpotifyEngine*>(pvParameters);
    if (self) {
        self->pollTaskLoop();
    }
    vTaskDelete(NULL);
}

void SpotifyEngine::pollTaskLoop() {
    while (m_taskRunning) {
        if (m_isActive && WiFi.status() == WL_CONNECTED) {
            pollSpotifyStatus();
        }
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

EngineError SpotifyEngine::initialize(EngineContext* context, const EngineConfig* config) {
    applyConfig(config);
    m_hasPsram = (context && context->hasPsram()) || psramFound();

    if (!m_pollTaskHandle) {
        m_taskRunning = true;
        BaseType_t ret = xTaskCreatePinnedToCore(
            pollTaskStatic,
            "SpotPoll",
            8192,
            this,
            1,
            &m_pollTaskHandle,
            0
        );
        if (ret != pdPASS) {
            LOGE("Spotify", "Failed to create SpotPoll worker task!");
            m_taskRunning = false;
        }
    }

    LOGI("Spotify", "Initialized with client_id: %s (PSRAM: %s)", m_clientId.c_str(), m_hasPsram ? "ENABLED" : "DISABLED");
    return EngineError::OK;
}

void SpotifyEngine::activate() {
    m_marqueeOffset = 0;
    m_lastMarqueeTick = millis();
    m_lastAnimTick = millis();
    m_isActive = true;
}

void SpotifyEngine::deactivate() {
    m_isActive = false;
}

void SpotifyEngine::onConfigChanged(const EngineConfig* config) {
    applyConfig(config);
}

bool SpotifyEngine::refreshAccessToken() {
    if (m_clientId.isEmpty() || m_refreshToken.isEmpty()) return false;
    if (!m_accessToken.isEmpty() && millis() < m_tokenExpiry) return true;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (!http.begin(client, "https://accounts.spotify.com/api/token")) return false;

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    if (!m_clientSecret.isEmpty()) {
        http.setAuthorization(m_clientId.c_str(), m_clientSecret.c_str());
    }

    String postData = "grant_type=refresh_token&refresh_token=" + m_refreshToken;
    if (m_clientSecret.isEmpty()) {
        postData += "&client_id=" + m_clientId;
    }

    int httpCode = http.POST(postData);
    if (httpCode == 200) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, http.getString());
        m_accessToken = doc["access_token"].as<String>();
        uint32_t expiresIn = doc["expires_in"] | 3600;
        m_tokenExpiry = millis() + (expiresIn * 1000) - 60000;
        http.end();
        client.stop();
        return true;
    }

    http.end();
    client.stop();
    return false;
}

void SpotifyEngine::pollSpotifyStatus() {
    if (!refreshAccessToken()) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (!http.begin(client, "https://api.spotify.com/v1/me/player")) return;
    http.addHeader("Authorization", "Bearer " + m_accessToken);

    int httpCode = http.GET();
    if (httpCode == 204) {
        m_state.isActive = false;
        m_state.isPlaying = false;
        http.end();
        client.stop();

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_renderState = m_state;
        }
        return;
    }

    if (httpCode == 200) {
        DynamicJsonDocument doc(4096);
        deserializeJson(doc, http.getString());

        m_state.isActive = true;
        m_state.isPlaying = doc["is_playing"] | false;
        m_state.progressMs = doc["progress_ms"] | 0;

        JsonObject item = doc["item"];
        if (!item.isNull()) {
            m_state.title = item["name"].as<String>();
            m_state.durationMs = item["duration_ms"] | 0;

            JsonArray artists = item["artists"];
            if (artists.size() > 0) {
                m_state.artist = artists[0]["name"].as<String>();
            }

            JsonObject album = item["album"];
            if (!album.isNull()) {
                m_state.album = album["name"].as<String>();
                JsonArray images = album["images"];
                if (images.size() > 0) {
                    m_state.imageUrl = images[images.size() - 1]["url"].as<String>();
                }
            }
        }

        JsonObject device = doc["device"];
        if (!device.isNull()) {
            m_state.volumePercent = device["volume_percent"] | 50;
        }
    }

    http.end();
    client.stop();

    if (m_hasPsram && m_showAlbumArt && !m_state.imageUrl.isEmpty()) {
        if (m_state.imageUrl != m_loadedImageUrl) {
            m_loadedImageUrl = m_state.imageUrl;
            int imgSize = 52;
            m_artworkId = artworkService.loadArtwork(m_loadedImageUrl, imgSize, imgSize);
        }
    } else if (m_state.imageUrl.isEmpty()) {
        m_artworkId = "";
        m_loadedImageUrl = "";
    }

    m_state.localTimestampMs = millis();

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_renderState = m_state;
    }
}

void SpotifyEngine::update(EngineContext* context) {
    uint32_t now = millis();

    SpotifyMediaState st;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        st = m_renderState;
    }

    if (now - m_lastMarqueeTick >= 40) {
        m_marqueeOffset++;
        m_lastMarqueeTick = now;
    }

    if (st.isPlaying && (now - m_lastAnimTick >= 80)) {
        m_animFrame = (m_animFrame + 1) % 100;
        m_lastAnimTick = now;
    }
}

#include <glcdfont.c>

static void drawClippedString(Adafruit_GFX* display, const String& text, int x, int y, int clipMinX, int clipMaxX, uint16_t color) {
    if (!display || text.isEmpty()) return;
    int curX = x;
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if (curX >= clipMinX && curX + 6 <= clipMaxX) {
            display->drawChar(curX, y, c, color, 0, 1);
        } else if (curX + 6 > clipMinX && curX < clipMaxX) {
            for (int col = 0; col < 5; col++) {
                int px = curX + col;
                if (px >= clipMinX && px < clipMaxX) {
                    uint8_t line = pgm_read_byte(&font[c * 5 + col]);
                    for (int row = 0; row < 8; row++) {
                        if (line & 1) {
                            display->drawPixel(px, y + row, color);
                        }
                        line >>= 1;
                    }
                }
            }
        }
        curX += 6;
    }
}

static void renderMarquee(Adafruit_GFX* display, const String& text, int y, int clipMinX, int clipMaxX, int availW, int offset, uint16_t color) {
    if (!display || text.isEmpty()) return;
    int textW = text.length() * 6;
    if (textW <= availW) {
        drawClippedString(display, text, clipMinX, y, clipMinX, clipMaxX, color);
    } else {
        int gap = 20; // 20px seamless spacing between loop repetitions (matches RPi)
        int totalW = textW + gap;
        int dx = (offset % totalW + totalW) % totalW;

        // Draw primary text instance
        int drawX1 = clipMinX - dx;
        drawClippedString(display, text, drawX1, y, clipMinX, clipMaxX, color);

        // Draw trailing secondary instance for circular looping
        int drawX2 = drawX1 + totalW;
        if (drawX2 < clipMaxX) {
            drawClippedString(display, text, drawX2, y, clipMinX, clipMaxX, color);
        }
    }
}

void SpotifyEngine::render(EngineContext* context) {
    if (!context) return;
    auto* display = context->getMatrix();
    if (!display) return;

    int w = display->width();
    int h = display->height();

    SpotifyMediaState st;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        st = m_renderState;
    }

    if (!st.isActive || st.title.isEmpty()) {
        String title = "Spotify";
        String subtitle = "Ready to stream";

        int titleW = title.length() * 6;
        int yIdleTitle = (h >= 64) ? ((h / 2) - 10) : 4;
        int yIdleSub = (h >= 64) ? ((h / 2) + 4) : 16;

        if (titleW <= w - 4) {
            int xTitle = (w - titleW) / 2;
            drawClippedString(display, title, xTitle, yIdleTitle, 2, w - 2, display->color565(30, 215, 96));
        } else {
            renderMarquee(display, title, yIdleTitle, 2, w - 2, w - 4, m_marqueeOffset, display->color565(30, 215, 96));
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
    bool hasArt = (m_hasPsram && m_showAlbumArt && !st.imageUrl.isEmpty());

    if (hasArt) {
        int imgSize = (h >= 64) ? 52 : 24;
        int imgX = 1;
        int imgY = (h >= 64) ? ((h - 4 - imgSize) / 2) : max(1, (h - 3 - imgSize) / 2);

        display->drawRect(imgX - 1, imgY - 1, imgSize + 2, imgSize + 2, display->color565(30, 45, 35));

        if (!m_artworkId.isEmpty()) {
            int artW = 0, artH = 0;
            const uint16_t* artBmp = artworkService.getArtworkBitmap(m_artworkId, artW, artH);
            if (artBmp && artW > 0 && artH > 0) {
                int drawW = min(imgSize, artW);
                int drawH = min(imgSize, artH);
                display->drawRGBBitmap(imgX, imgY, artBmp, drawW, drawH);
            }
        }
        textX = imgX + imgSize + 4;
    }

    bool isCompact = (w <= 64);
    int rightReserved = 2;
    if (m_showVisualizer && st.isPlaying) {
        rightReserved = (isCompact && hasArt) ? 8 : 15;
    } else if (m_showVolume && st.volumePercent > 0) {
        rightReserved = 24;
    }

    int clipMinX = textX;
    int clipMaxX = w - rightReserved;
    int availW = max(16, clipMaxX - clipMinX);

    int yTitle = (h >= 64) ? 8 : 3;
    int yArtist = (h >= 64) ? 22 : 13;

    renderMarquee(display, st.title, yTitle, clipMinX, clipMaxX, availW, m_marqueeOffset, display->color565(255, 255, 255));

    String artistStr = !st.artist.isEmpty() ? st.artist : (!st.album.isEmpty() ? st.album : "Spotify");
    renderMarquee(display, artistStr, yArtist, clipMinX, clipMaxX, availW, m_marqueeOffset / 2, display->color565(30, 215, 96));

    if (m_showVisualizer && st.isPlaying) {
        int eqBaseY = (h >= 64) ? 44 : 21;

        if (isCompact && hasArt) {
            int eqX = w - 7;
            int barHeights[3] = {
                (int)((m_animFrame * 4) % 7 + 2),
                (int)((m_animFrame * 6) % 9 + 3),
                (int)((m_animFrame * 3) % 6 + 2)
            };

            for (int i = 0; i < 3; i++) {
                int bx = eqX + (i * 2);
                int bh = barHeights[i];
                for (int by = 0; by < bh; by++) {
                    int py = eqBaseY - by;
                    if (py >= 0) {
                        uint16_t color = (by > 6) ? display->color565(255, 60, 60) : (by > 3) ? display->color565(255, 220, 0) : display->color565(30, 215, 96);
                        display->drawPixel(bx, py, color);
                    }
                }
            }
        } else {
            int eqX = w - 13;
            int barHeights[4] = {
                (int)((m_animFrame * 4) % 8 + 2),
                (int)((m_animFrame * 6) % 11 + 3),
                (int)((m_animFrame * 3) % 9 + 2),
                (int)((m_animFrame * 5) % 7 + 2)
            };

            for (int i = 0; i < 4; i++) {
                int bx = eqX + (i * 3);
                int bh = barHeights[i];
                for (int by = 0; by < bh; by++) {
                    int py = eqBaseY - by;
                    if (py >= 0) {
                        uint16_t color = (by > 7) ? display->color565(255, 60, 60) : (by > 4) ? display->color565(255, 220, 0) : display->color565(30, 215, 96);
                        display->drawPixel(bx, py, color);
                        display->drawPixel(bx + 1, py, color);
                    }
                }
            }
        }
    } else if (m_showVolume && st.volumePercent > 0) {
        char vBuf[8];
        snprintf(vBuf, sizeof(vBuf), "%d%%", st.volumePercent);
        int vLen = strlen(vBuf);
        display->setTextColor(display->color565(180, 180, 180));
        display->setCursor(w - (vLen * 6) - 1, yTitle);
        display->print(vBuf);
    }

    if (m_showProgress && st.durationMs > 0) {
        uint32_t curMs = st.progressMs;
        if (st.isPlaying && st.localTimestampMs > 0) {
            curMs += (millis() - st.localTimestampMs);
        }
        float progress = constrain((float)curMs / (float)st.durationMs, 0.0f, 1.0f);
        int barW = (int)((w - 2) * progress);

        display->drawFastHLine(1, h - 2, w - 2, display->color565(35, 45, 35));
        display->drawFastHLine(1, h - 1, w - 2, display->color565(20, 30, 20));

        if (barW > 0) {
            display->drawFastHLine(1, h - 2, barW, display->color565(30, 215, 96));
            display->drawFastHLine(1, h - 1, barW, display->color565(15, 140, 60));
        }
    }
}

EngineDescriptor SpotifyDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"spotify", "Spotify Player", "media", "3.0.0"};
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.realtime = true;

    desc.requirements.needsNetwork = true;
    desc.requirements.needsPsram = false;

    desc.schema.fields = {
        ConfigField("client_id", ConfigType::STRING, "Client ID", "Spotify Developer Client ID.", "", true, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("client_secret", ConfigType::STRING, "Client Secret", "Spotify Developer Client Secret.", "", false, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("refresh_token", ConfigType::STRING, "Refresh Token", "Spotify OAuth2 Refresh Token.", "", true, "", "", "", "", "", false, "", ValidationPolicy::Accept),
        ConfigField("show_album_art", ConfigType::BOOLEAN, "Show Album Art", "Download and display Spotify album cover art (requires PSRAM).", "true", false, "", "", "", "", "", false, "psram=true", ValidationPolicy::FallbackDefault),
        ConfigField("show_progress", ConfigType::BOOLEAN, "Show Progress Bar", "Render playback progress bar at the bottom.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_visualizer", ConfigType::BOOLEAN, "Animated Equalizer", "Display equalizer frequency bars while music is playing.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_volume", ConfigType::BOOLEAN, "Show Volume Indicator", "Display current Spotify device volume level.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault)
    };

    desc.factory = []() {
        return std::unique_ptr<IEngine>(new SpotifyEngine());
    };

    return desc;
}
