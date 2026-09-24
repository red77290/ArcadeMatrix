#include "SpotifyEngine.h"
#include "../core/Logger.h"
#include "../core/NetworkBudget.h"
#include "../core/SpiRamJsonDocument.h"
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

    // Same admission control as the other HTTPS providers (Binance/CoinGecko/Yahoo,
    // GoogleCast): a WiFiClientSecure handshake needs a contiguous ~16KB internal-DRAM
    // block even with setInsecure(). Attempting it while fragmented fails silently deep
    // inside HTTPClient (http.begin()/POST() just return false/-1), which looked
    // identical to "the refresh token is invalid" or "Spotify stopped answering" from the
    // engine's perspective, with nothing in the log to tell them apart.
    if (!NetworkBudget::canStartTlsSession()) {
        static unsigned long lastBudgetWarn = 0;
        unsigned long now = millis();
        if (now - lastBudgetWarn > 10000) {
            lastBudgetWarn = now;
            LOGW("Spotify", "Skipping token refresh: insufficient internal DRAM for a TLS session (free=%u, largest=%u).",
                 (unsigned)NetworkBudget::freeInternal(), (unsigned)NetworkBudget::largestInternalBlock());
        }
        return false;
    }

    NetworkBudget::ScopedTlsHandshakeLock tlsLock;
    if (!tlsLock) {
        LOGW("Spotify", "Skipping token refresh: another TLS handshake is in progress.");
        return false;
    }

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
        // PSRAM-backed: refreshAccessToken() can run every poll cycle (~1.5s)
        // when the cached token is close to expiry, same rationale as the
        // Cast status docs (see SpiRamJsonDocument.h).
        SpiRamJsonDocument doc(1024);
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

    // refreshAccessToken() only opens a TLS session when the cached token actually needs
    // renewing; on a cache hit this poll's own connect() below is the first (and only)
    // handshake this round, so it needs its own admission check.
    if (!NetworkBudget::canStartTlsSession()) {
        static unsigned long lastBudgetWarn = 0;
        unsigned long now = millis();
        if (now - lastBudgetWarn > 10000) {
            lastBudgetWarn = now;
            LOGW("Spotify", "Skipping status poll: insufficient internal DRAM for a TLS session (free=%u, largest=%u).",
                 (unsigned)NetworkBudget::freeInternal(), (unsigned)NetworkBudget::largestInternalBlock());
        }
        return;
    }

    NetworkBudget::ScopedTlsHandshakeLock tlsLock;
    if (!tlsLock) {
        LOGW("Spotify", "Skipping status poll: another TLS handshake is in progress.");
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (!http.begin(client, "https://api.spotify.com/v1/me/player")) return;
    http.addHeader("Authorization", "Bearer " + m_accessToken);

    int httpCode = http.GET();
    if (httpCode == 204) {
        http.end();
        client.stop();

        uint8_t target = 1 - m_publishedPodIdx.load(std::memory_order_relaxed);
        m_podBuffers[target].isActive = false;
        m_podBuffers[target].isPlaying = false;
        m_podBuffers[target].generation++;
        m_publishedPodIdx.store(target, std::memory_order_release);
        return;
    }

    bool isPlaying = false;
    uint32_t progressMs = 0;
    uint32_t durationMs = 0;
    uint8_t volumePercent = 50;
    String title = "";
    String artist = "";
    String album = "";
    String imageUrl = "";

    if (httpCode == 200) {
        // PSRAM-backed: this poll cycle runs every ~1.5s while the engine is active.
        SpiRamJsonDocument doc(4096);
        deserializeJson(doc, http.getString());

        isPlaying = doc["is_playing"] | false;
        progressMs = doc["progress_ms"] | 0;

        JsonObject item = doc["item"];
        if (!item.isNull()) {
            title = item["name"].as<String>();
            durationMs = item["duration_ms"] | 0;

            JsonArray artists = item["artists"];
            if (artists.size() > 0) {
                artist = artists[0]["name"].as<String>();
            }

            JsonObject albumObj = item["album"];
            if (!albumObj.isNull()) {
                album = albumObj["name"].as<String>();
                JsonArray images = albumObj["images"];
                if (images.size() > 0) {
                    imageUrl = images[images.size() - 1]["url"].as<String>();
                }
            }
        }

        JsonObject device = doc["device"];
        if (!device.isNull()) {
            volumePercent = device["volume_percent"] | 50;
        }
    }

    http.end();
    client.stop();

    String artworkId = "";
    if (m_hasPsram && m_showAlbumArt && !imageUrl.isEmpty()) {
        if (imageUrl != m_loadedImageUrl) {
            m_loadedImageUrl = imageUrl;
            int imgSize = 52;
            m_artworkId = artworkService.loadArtwork(m_loadedImageUrl, imgSize, imgSize);
        }
        artworkId = m_artworkId;
    } else if (imageUrl.isEmpty()) {
        m_artworkId = "";
        m_loadedImageUrl = "";
    }

    uint8_t target = 1 - m_publishedPodIdx.load(std::memory_order_relaxed);
    SpotifyMediaStatePOD& next = m_podBuffers[target];
    next.isActive = (httpCode == 200);
    next.isPlaying = isPlaying;
    next.progressMs = progressMs;
    next.durationMs = durationMs;
    strncpy(next.title, title.c_str(), sizeof(next.title) - 1);
    next.title[sizeof(next.title) - 1] = '\0';
    strncpy(next.artist, artist.c_str(), sizeof(next.artist) - 1);
    next.artist[sizeof(next.artist) - 1] = '\0';
    strncpy(next.album, album.c_str(), sizeof(next.album) - 1);
    next.album[sizeof(next.album) - 1] = '\0';
    strncpy(next.imageUrl, imageUrl.c_str(), sizeof(next.imageUrl) - 1);
    next.imageUrl[sizeof(next.imageUrl) - 1] = '\0';
    strncpy(next.artworkId, artworkId.c_str(), sizeof(next.artworkId) - 1);
    next.artworkId[sizeof(next.artworkId) - 1] = '\0';
    next.volumePercent = volumePercent;
    next.localTimestampMs = millis();
    next.generation++;

    m_publishedPodIdx.store(target, std::memory_order_release);
}

void SpotifyEngine::update(EngineContext* context) {
    uint32_t now = millis();

    // Lock-free atomic acquisition of published snapshot (zero allocation, zero mutex)
    uint8_t idx = m_publishedPodIdx.load(std::memory_order_acquire);
    m_cachedRenderState = m_podBuffers[idx];

    if (now - m_lastMarqueeTick >= 40) {
        m_marqueeOffset++;
        m_lastMarqueeTick = now;
    }

    if (m_cachedRenderState.isPlaying && (now - m_lastAnimTick >= 80)) {
        m_animFrame = (m_animFrame + 1) % 100;
        m_lastAnimTick = now;
    }
}

#include <glcdfont.c>

static void drawClippedString(Adafruit_GFX* display, const char* text, int x, int y, int clipMinX, int clipMaxX, uint16_t color) {
    if (!display || !text || text[0] == '\0') return;
    int curX = x;
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i++) {
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

static void renderMarquee(Adafruit_GFX* display, const char* text, int y, int clipMinX, int clipMaxX, int availW, int offset, uint16_t color) {
    if (!display || !text || text[0] == '\0') return;
    int textW = (int)strlen(text) * 6;
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

    // Zero-allocation, zero-mutex: use cached snapshot updated in update()
    const SpotifyMediaStatePOD& st = m_cachedRenderState;

    if (!st.isActive || st.title[0] == '\0') {
        const char* title = "Spotify";
        const char* subtitle = "Ready to stream";

        int titleW = (int)strlen(title) * 6;
        int yIdleTitle = (h >= 64) ? ((h / 2) - 10) : 4;
        int yIdleSub = (h >= 64) ? ((h / 2) + 4) : 16;

        if (titleW <= w - 4) {
            int xTitle = (w - titleW) / 2;
            drawClippedString(display, title, xTitle, yIdleTitle, 2, w - 2, display->color565(30, 215, 96));
        } else {
            renderMarquee(display, title, yIdleTitle, 2, w - 2, w - 4, m_marqueeOffset, display->color565(30, 215, 96));
        }

        int subW = (int)strlen(subtitle) * 6;
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
    bool hasArt = (m_hasPsram && m_showAlbumArt && st.imageUrl[0] != '\0');

    if (hasArt) {
        int imgSize = (h >= 64) ? 52 : 24;
        int imgX = 1;
        int imgY = (h >= 64) ? ((h - 4 - imgSize) / 2) : max(1, (h - 3 - imgSize) / 2);

        display->drawRect(imgX - 1, imgY - 1, imgSize + 2, imgSize + 2, display->color565(30, 45, 35));

        if (st.artworkId[0] != '\0') {
            int artW = 0, artH = 0;
            const uint16_t* artBmp = artworkService.getArtworkBitmap(st.artworkId, artW, artH);
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

    const char* artistStr = (st.artist[0] != '\0') ? st.artist : ((st.album[0] != '\0') ? st.album : "Spotify");
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
