#include "SpotifyEngine.h"
#include "../core/Logger.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

SpotifyEngine::SpotifyEngine() {
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

EngineError SpotifyEngine::initialize(EngineContext* context, const EngineConfig* config) {
    applyConfig(config);
    LOGI("Spotify", "Initialized with client_id: %s", m_clientId.c_str());
    return EngineError::OK;
}

void SpotifyEngine::activate() {
    m_marqueeOffset = 0;
    m_lastMarqueeTick = millis();
    m_lastAnimTick = millis();
    pollSpotifyStatus();
}

void SpotifyEngine::deactivate() {
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
        return true;
    }

    http.end();
    return false;
}

void SpotifyEngine::pollSpotifyStatus() {
    if (WiFi.status() != WL_CONNECTED) return;
    m_state.lastPollTime = millis();

    if (!refreshAccessToken()) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    if (!http.begin(client, "https://api.spotify.com/v1/me/player")) return;
    http.addHeader("Authorization", "Bearer " + m_accessToken);

    int httpCode = http.GET();
    if (httpCode == 204) {
        // No music currently playing
        m_state.isActive = false;
        m_state.isPlaying = false;
        http.end();
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
}

void SpotifyEngine::update(EngineContext* context) {
    uint32_t now = millis();

    if (now - m_state.lastPollTime >= 2000) {
        pollSpotifyStatus();
    }

    if (now - m_lastMarqueeTick >= 40) {
        m_marqueeOffset++;
        m_lastMarqueeTick = now;
    }

    if (m_state.isPlaying && (now - m_lastAnimTick >= 80)) {
        m_animFrame = (m_animFrame + 1) % 100;
        m_lastAnimTick = now;
    }
}

void SpotifyEngine::render(EngineContext* context) {
    if (!context) return;
    auto* display = context->getMatrix();
    if (!display) return;

    int w = display->width();
    int h = display->height();

    if (!m_state.isActive || m_state.title.isEmpty()) {
        display->setTextColor(display->color565(30, 215, 96));
        display->setCursor((w / 2) - 20, (h / 2) - 6);
        display->print("Spotify");

        display->setTextColor(display->color565(140, 140, 140));
        display->setCursor((w / 2) - 34, (h / 2) + 3);
        display->print("No music playing");
        return;
    }

    int textX = 2;

    // 1. Album Cover Art frame
    if (m_showAlbumArt && !m_state.imageUrl.isEmpty()) {
        int imgSize = 26;
        int imgX = 1;
        int imgY = (h - 4 - imgSize) / 2;
        display->drawRect(imgX - 1, imgY - 1, imgSize + 2, imgSize + 2, display->color565(30, 45, 35));
        textX = imgX + imgSize + 3;
    }

    int rightReserved = 2;
    if (m_showVisualizer && m_state.isPlaying) {
        rightReserved += 16;
    } else if (m_showVolume && m_state.volumePercent > 0) {
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

    // 3. Artist / Album (Marquee)
    String artistStr = !m_state.artist.isEmpty() ? m_state.artist : (!m_state.album.isEmpty() ? m_state.album : "Spotify");
    int artistW = artistStr.length() * 6;
    int artistDrawX = textX;
    if (artistW > availW) {
        int overflow = artistW - availW + 16;
        artistDrawX = textX - ((m_marqueeOffset / 2) % overflow);
    }

    display->setTextColor(display->color565(30, 215, 96));
    display->setCursor(artistDrawX, yArtist);
    display->print(artistStr);

    // 4. Animated Equalizer (Far Right)
    if (m_showVisualizer && m_state.isPlaying) {
        int eqX = w - 14;
        int barHeights[4] = {
            (int)((m_animFrame * 4) % 7 + 2),
            (int)((m_animFrame * 6) % 9 + 2),
            (int)((m_animFrame * 3) % 8 + 2),
            (int)((m_animFrame * 5) % 6 + 2)
        };

        int eqBaseY = (h >= 64) ? 28 : 20;

        for (int i = 0; i < 4; i++) {
            int bx = eqX + (i * 3);
            int bh = barHeights[i];
            for (int by = 0; by < bh; by++) {
                int py = eqBaseY - by;
                if (py >= 0) {
                    uint16_t color = (by > 6) ? display->color565(255, 60, 60) : (by > 3) ? display->color565(255, 220, 0) : display->color565(30, 215, 96);
                    display->drawPixel(bx, py, color);
                    display->drawPixel(bx + 1, py, color);
                }
            }
        }
    } else if (m_showVolume && m_state.volumePercent > 0) {
        // 5. Volume (Top-Right, right-aligned)
        char vBuf[8];
        snprintf(vBuf, sizeof(vBuf), "%d%%", m_state.volumePercent);
        int vLen = strlen(vBuf);
        display->setTextColor(display->color565(180, 180, 180));
        display->setCursor(w - (vLen * 6) - 1, yTitle);
        display->print(vBuf);
    }

    // 6. Progress Bar (Bottom 2 pixels)
    if (m_showProgress && m_state.durationMs > 0) {
        float progress = constrain((float)m_state.progressMs / (float)m_state.durationMs, 0.0f, 1.0f);
        int barW = (int)((w - 2) * progress);

        display->drawFastHLine(1, h - 2, w - 2, display->color565(30, 35, 30));
        display->drawFastHLine(1, h - 1, w - 2, display->color565(18, 22, 18));

        if (barW > 0) {
            display->drawFastHLine(1, h - 2, barW, display->color565(30, 215, 96));
            display->drawFastHLine(1, h - 1, barW, display->color565(20, 150, 65));
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
    desc.requirements.needsPsram = true; // Requires PSRAM (ESP32-S3)

    desc.schema.fields = {
        ConfigField("client_id", ConfigType::STRING, "Spotify Client ID", "Your Spotify Developer API Client ID.", "", true, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("client_secret", ConfigType::STRING, "Spotify Client Secret", "Your Spotify Developer API Client Secret.", "", false, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("refresh_token", ConfigType::STRING, "Spotify Refresh Token", "Your OAuth2 Refresh Token for continuous playback sync.", "", true, "", "", "", "", "", false, "", ValidationPolicy::Ignore),
        ConfigField("show_album_art", ConfigType::BOOLEAN, "Show Album Cover", "Display Spotify album cover art on the matrix.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_progress", ConfigType::BOOLEAN, "Show Progress Bar", "Render track playback progress bar at the bottom.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_visualizer", ConfigType::BOOLEAN, "Animated Equalizer", "Display dancing equalizer frequency bars while music is playing.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_volume", ConfigType::BOOLEAN, "Show Volume Indicator", "Display Spotify active playback volume percentage.", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault)
    };

    desc.factory = []() {
        return std::unique_ptr<IEngine>(new SpotifyEngine());
    };

    return desc;
}
