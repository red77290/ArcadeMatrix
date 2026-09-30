/**
 * @file IconService.cpp
 * @brief Implementation of the centralized IconService.
 */
#include "IconService.h"
#include "../core/Logger.h"

#if defined(ARDUINO)
#include "../core/SDUtils.h"
#include "../core/SdLockGuard.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <PNGdec.h>
#ifdef INTELSHORT
#undef INTELSHORT
#endif
#ifdef INTELLONG
#undef INTELLONG
#endif
#ifdef MOTOSHORT
#undef MOTOSHORT
#endif
#ifdef MOTOLONG
#undef MOTOLONG
#endif
#endif

IconService iconService;

#if defined(ARDUINO)
static PNG* s_png = nullptr;
static uint16_t* s_targetBuf = nullptr;
static int s_targetW = 16;
static int s_targetH = 16;
static int s_srcW = 16;
static int s_srcH = 16;
static uint16_t s_lineBuf[256];

static int iconPngDrawCallback(PNGDRAW* pDraw) {
    if (!s_targetBuf || !s_png || s_targetW <= 0 || s_targetH <= 0) return 0;

    int srcW = pDraw->iWidth;
    int srcH = (s_srcH > 0) ? s_srcH : s_targetH;

    int targetY = (pDraw->y * s_targetH) / srcH;
    if (targetY < 0 || targetY >= s_targetH) return 1;

    int rowBucketStart = (targetY * srcH) / s_targetH;
    if (pDraw->y != rowBucketStart) return 1;

    int decodeW = (srcW <= 256) ? srcW : 256;
    s_png->getLineAsRGB565(pDraw, s_lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);

    for (int tx = 0; tx < s_targetW; tx++) {
        int srcX = (tx * srcW) / s_targetW;
        if (srcX < decodeW) {
            s_targetBuf[targetY * s_targetW + tx] = s_lineBuf[srcX];
        }
    }
    return 1;
}
#endif

String IconService::sanitizeSymbol(const String& symbol) {
    String clean = symbol;
    clean.trim();
    clean.toLowerCase();
    for (size_t i = 0; i < clean.length(); ++i) {
        char c = clean[i];
        if (c == '^' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            clean[i] = '_';
        }
    }
    return clean;
}

String IconService::getSdPath(const char* category, const String& symbol) {
    String safe = sanitizeSymbol(symbol);
    return "/" + String(category) + "_icons/" + safe + ".png";
}

bool IconService::hasIconOnSd(const char* category, const String& symbol) const {
#if !defined(ARDUINO)
    (void)category;
    (void)symbol;
    return false;
#else
    String path = getSdPath(category, symbol);
    SdLockGuard guard(pdMS_TO_TICKS(1500));
    if (guard) {
        return sd.exists(path.c_str());
    }
    return false;
#endif
}

bool IconService::downloadIconToSd(const char* category, const String& symbol, const String& rawUrl, int width, int height) {
#if !defined(ARDUINO)
    return false;
#else
    if (WiFi.status() != WL_CONNECTED) return false;

    String safeName = sanitizeSymbol(symbol);
    std::vector<String> candidates;

    if (!rawUrl.isEmpty()) {
        candidates.push_back(rawUrl);
    }

    if (strcmp(category, "crypto") == 0) {
        candidates.push_back("assets.coincap.io/assets/icons/" + safeName + "@2x.png");
        candidates.push_back("coinicons-api.vercel.app/api/icon/" + safeName);
    } else if (strcmp(category, "stock") == 0) {
        candidates.push_back("assets.parqet.com/logos/symbol/" + safeName + "?format=png");
        candidates.push_back("financialmodelingprep.com/image-stock/" + symbol + ".png");
        candidates.push_back("eodhd.com/img/logos/US/" + safeName + ".png");
    } else if (strcmp(category, "media") == 0 || strcmp(category, "app") == 0) {
        if (safeName == "spotify") {
            candidates.push_back("upload.wikimedia.org/wikipedia/commons/thumb/1/19/Spotify_logo_without_text.svg/1024px-Spotify_logo_without_text.svg.png");
        } else if (safeName == "cast" || safeName == "googlecast") {
            candidates.push_back("upload.wikimedia.org/wikipedia/commons/thumb/2/26/Chromecast_cast_button_icon.svg/512px-Chromecast_cast_button_icon.svg.png");
        }
    }

    if (candidates.empty()) return false;

    HTTPClient http;
    WiFiClient client;
    http.setTimeout(3000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("Mozilla/5.0 ArcadeMatrix/3.4");

    bool downloaded = false;

    for (const auto& candUrl : candidates) {
        esp_task_wdt_reset();

        String proxyUrl = "http://images.weserv.nl/?url=" + candUrl + "&w=" + String(width) + "&h=" + String(height) + "&output=png";
        LOGI("IconService", "Downloading %s icon for %s via proxy: %s", category, symbol.c_str(), proxyUrl.c_str());

        if (http.begin(client, proxyUrl)) {
            int code = http.GET();
            if (code == 200) {
                int len = http.getSize();
                if (len > 0 && len < 16384) {
                    SdLockGuard guard(pdMS_TO_TICKS(1500));
                    if (guard) {
                        String dir = "/" + String(category) + "_icons";
                        if (!sd.exists(dir.c_str())) {
                            sd.mkdir(dir.c_str());
                        }
                        String sdPath = getSdPath(category, symbol);
                        FsFile f = sd.open(sdPath.c_str(), FILE_OPEN_WRITE);
                        if (f) {
                            http.writeToStream(&f);
                            f.close();
                            downloaded = true;
                            LOGI("IconService", "Saved %s icon for %s to %s (%d bytes)", category, symbol.c_str(), sdPath.c_str(), len);
                        }
                    }
                }
            } else {
                LOGW("IconService", "Candidate URL failed for %s:%s (HTTP %d)", category, symbol.c_str(), code);
            }
            http.end();
            client.stop();
        }

        if (downloaded) break;
    }

    esp_task_wdt_reset();
    return downloaded;
#endif
}

bool IconService::decodeIconFromSd(const char* category, const String& symbol, uint16_t* outPixels, int width, int height) {
    if (!outPixels || width <= 0 || height <= 0) return false;

#if !defined(ARDUINO)
    return false;
#else
    String sdPath = getSdPath(category, symbol);
    size_t size = 0;
    uint8_t* buf = nullptr;

    {
        SdLockGuard guard(pdMS_TO_TICKS(1500));
        if (guard && sd.exists(sdPath.c_str())) {
            FsFile f = sd.open(sdPath.c_str(), FILE_OPEN_READ);
            if (f) {
                size = f.size();
                if (size > 0 && size <= 16384) {
                    buf = (uint8_t*)malloc(size);
                    if (buf) {
                        f.read(buf, size);
                    }
                }
                f.close();
            }
        }
    }

    if (!buf || size == 0) {
        if (buf) free(buf);
        return false;
    }

    // Invariant: PNGdec allocates ~45KB internally; verify contiguous DRAM headroom
    const size_t reqPngHeap = sizeof(PNG) + 1024;
    if (ESP.getMaxAllocHeap() < reqPngHeap) {
        LOGW("IconService", "Skipping decode for %s:%s: insufficient contiguous heap (largest=%u, need ~%u bytes)",
             category, symbol.c_str(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)reqPngHeap);
        free(buf);
        return false;
    }

    PNG* png = new (std::nothrow) PNG();
    if (!png) {
        LOGW("IconService", "Failed to allocate PNGdec for %s:%s", category, symbol.c_str());
        free(buf);
        return false;
    }

    memset(outPixels, 0, width * height * sizeof(uint16_t));
    s_png = png;
    s_targetBuf = outPixels;
    s_targetW = width;
    s_targetH = height;

    bool success = false;
    int rc = png->openRAM(buf, size, iconPngDrawCallback);
    if (rc == PNG_SUCCESS) {
        s_srcW = png->getWidth();
        s_srcH = png->getHeight();

        if (s_srcW > 0 && s_srcH > 0 && s_srcW <= 256 && s_srcH <= 256) {
            png->decode(nullptr, 0);
            success = true;
            LOGI("IconService", "Successfully decoded %dx%d icon for %s (%s, src=%dx%d)",
                 width, height, symbol.c_str(), category, s_srcW, s_srcH);
        } else {
            LOGW("IconService", "Invalid icon dimensions for %s:%s (%dx%d)", category, symbol.c_str(), s_srcW, s_srcH);
        }
    } else {
        LOGW("IconService", "Failed to decode PNG for %s:%s (rc=%d)", category, symbol.c_str(), rc);
    }

    png->close();
    delete png;
    s_png = nullptr;
    s_targetBuf = nullptr;
    free(buf);

    return success;
#endif
}

bool IconService::loadOrFetchIcon(const char* category, const String& symbol, const String& rawUrl,
                                 uint16_t* outPixels, int width, int height) {
    if (!outPixels) return false;

    String safeKey = String(category) + ":" + sanitizeSymbol(symbol);
    auto it = m_failedAttempts.find(safeKey);
    if (it != m_failedAttempts.end()) {
        if (millis() - it->second < 3600000UL) {
            return false; // Negative cache hit: cooldown active
        }
    }

    // 1. Try decoding from SD first if already cached
    if (hasIconOnSd(category, symbol)) {
        if (decodeIconFromSd(category, symbol, outPixels, width, height)) {
            m_failedAttempts.erase(safeKey);
            return true;
        }
    }

    // 2. Not on SD (or decode skipped): download via plain HTTP proxy
    if (downloadIconToSd(category, symbol, rawUrl, width, height)) {
        if (decodeIconFromSd(category, symbol, outPixels, width, height)) {
            m_failedAttempts.erase(safeKey);
            return true;
        }
    }

    // Record negative cache entry to prevent repeated network spam
    m_failedAttempts[safeKey] = millis();
    return false;
}

bool IconService::loadOrFetchMarketIcon(const String& symbol, const String& rawUrl,
                                       uint16_t* outPixels, int width, int height) {
    if (!outPixels) return false;

    // Check SD for crypto first, then stock
    if (hasIconOnSd("crypto", symbol)) {
        return decodeIconFromSd("crypto", symbol, outPixels, width, height);
    }
    if (hasIconOnSd("stock", symbol)) {
        return decodeIconFromSd("stock", symbol, outPixels, width, height);
    }

    // Attempt crypto fetch then stock fetch
    if (loadOrFetchIcon("crypto", symbol, rawUrl, outPixels, width, height)) {
        return true;
    }
    return loadOrFetchIcon("stock", symbol, rawUrl, outPixels, width, height);
}

void IconService::clearNegativeCache() {
    m_failedAttempts.clear();
}
