#include "GifEngine.h"
#include "../core/SpiRamJsonDocument.h"
#include "../core/MatrixEngine.h"
#include "../core/RenderStats.h"

extern MatrixEngine matrixEngine;
#include <ArduinoJson.h>
#include "../core/SDUtils.h"
#include "../core/SdLockGuard.h"
#include "../core/Logger.h"
#include "../core/ConfigLoader.h"
#include "../core/Globals.h"

GifEngine* GifEngine::instance = nullptr;

GifEngine::GifEngine() : matrix(nullptr), isPlaying(false), playlistMode(false), isRaw(false), isPng(false), needsInitialFlip(false), rawLastFrameTime(0), pngShowStartTime(0), psramBuffer(nullptr), psramBufferSize(0) {
    instance = this;
}

GifEngine::~GifEngine() {
    freeShadows();
    stop();
    if (recentHashes) {
        free(recentHashes);
        recentHashes = nullptr;
    }
    delete png;
}

#include "../core/LayoutHelper.h"
#include "../core/DisplayOrientationManager.h"

bool GifEngine::isDisplayVertical() const {
    DisplayGeometry g = displayOrientationManager.getGeometry();
    if (g.height > g.width || g.layoutClass == LayoutClass::PORTRAIT || g.layoutClass == LayoutClass::TALL) return true;
    if (matrix && (matrix->height() > matrix->width() || matrix->width() < 48 || matrix->height() > (matrix->width() * 3) / 2)) return true;
    if (m_context) {
        DisplayGeometry cg = m_context->getGeometry();
        if (cg.height > cg.width || cg.width < 48 || cg.height > (cg.width * 3) / 2) return true;
        if (cg.layoutClass == LayoutClass::PORTRAIT || cg.layoutClass == LayoutClass::TALL) return true;
    }
    return false;
}

String GifEngine::resolveDefaultFolder() const {
    return isDisplayVertical() ? "/gifs_tate" : "/gifs";
}

void GifEngine::rebuildActivePlaylists() {
    bool vertical = isDisplayVertical();
    std::vector<String> filtered;

    bool isAll = m_configuredFolders.empty() || 
                 (m_configuredFolders.size() == 1 && (m_configuredFolders[0] == "all" || m_configuredFolders[0] == "/gifs" || m_configuredFolders[0] == "/gifs_tate" || m_configuredFolders[0].length() == 0));

    auto safeExists = [](const String& path) -> bool {
        if (path.isEmpty()) return false;
        bool exists = false;
        if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
            exists = sd.exists(path.c_str());
            xSemaphoreGive(sdMutex);
        }
        return exists;
    };

    if (vertical) {
        // VERTICAL (TATE) DISPLAY: Only play /gifs_tate folders!
        if (isAll) {
            std::vector<String> input = { "/gifs_tate" };
            expandPlaylists(input, filtered);
        } else {
            for (const String& f : m_configuredFolders) {
                String clean = sanitizePlaylistPath(f);
                if (clean == "/gifs_tate" || clean == "/gifs") {
                    std::vector<String> input = { "/gifs_tate" };
                    std::vector<String> expandedAll;
                    expandPlaylists(input, expandedAll);
                    for (const String& e : expandedAll) filtered.push_back(e);
                    continue;
                }
                // Cross-orientation mapping: a playlist selected as "test", "/gifs/test" or
                // "/gifs_tate/test" must all resolve to "/gifs_tate/test" in vertical mode.
                String leaf = extractPlaylistLeaf(clean);
                if (leaf.isEmpty()) continue;
                String tatePath = "/gifs_tate/" + leaf;
                if (safeExists(tatePath)) filtered.push_back(tatePath);
            }
            if (filtered.empty()) {
                // Fallback to all vertical folders if specific selection had no vertical match
                std::vector<String> input = { "/gifs_tate" };
                expandPlaylists(input, filtered);
            }
        }
        if (filtered.empty()) {
            LOGW("GifEngine", "Vertical (TATE) display active, but NO vertical GIF folders found in /gifs_tate.");
        }
    } else {
        // HORIZONTAL (YOKO) DISPLAY: Only play /gifs folders!
        if (isAll) {
            std::vector<String> input = { "/gifs" };
            expandPlaylists(input, filtered);
        } else {
            for (const String& f : m_configuredFolders) {
                String clean = sanitizePlaylistPath(f);
                if (clean == "/gifs" || clean == "/gifs_tate") {
                    std::vector<String> input = { "/gifs" };
                    std::vector<String> expandedAll;
                    expandPlaylists(input, expandedAll);
                    for (const String& e : expandedAll) filtered.push_back(e);
                    continue;
                }
                // Cross-orientation mapping, symmetric with the TATE branch above.
                String leaf = extractPlaylistLeaf(clean);
                if (leaf.isEmpty()) continue;
                String yokoPath = "/gifs/" + leaf;
                if (safeExists(yokoPath)) filtered.push_back(yokoPath);
            }
            if (filtered.empty()) {
                // Fallback to all horizontal folders
                std::vector<String> input = { "/gifs" };
                expandPlaylists(input, filtered);
            }
        }
        if (filtered.empty()) {
            LOGW("GifEngine", "Horizontal (YOKO) display active, but NO horizontal GIF folders found in /gifs.");
        }
    }

    defaultPlaylists = filtered;
    LOGI("GifEngine", "Active playlists rebuilt for %s mode: %u folders.", vertical ? "TATE (Vertical)" : "YOKO (Horizontal)", (unsigned int)defaultPlaylists.size());
}

EngineError GifEngine::initialize(EngineContext* context, const EngineConfig* config) {
    instance = this;
    if (!context || !context->getMatrix()) return EngineError::InitializationFailed;
    m_context = context;
    m_instanceConfig = config;
    m_hasPsram = context->hasPsram();
    if (!begin(context->getMatrix())) return EngineError::InitializationFailed;
    if (config) onConfigChanged(config);
    else {
        m_configuredFolders = { "all" };
        rebuildActivePlaylists();
    }
    return EngineError::OK;
}

void GifEngine::activate() {
    instance = this;
    invalidateShadows();
    int count = 1;
    if (m_rotationBudget > 0) {
        count = (int)m_rotationBudget;
    } else if (m_instanceConfig) {
        int cfgCount = m_instanceConfig->getInt("gifs_count", 0);
        if (cfgCount > 0) count = cfgCount;
    }
    rebuildActivePlaylists();
    if (hasDefaultPlaylists()) {
        playDefaultPlaylists(count);
    } else {
        stop();
        if (matrix) matrix->fillScreen(0);
    }
}

void GifEngine::update(EngineContext* context) {
    instance = this;
    m_context = context;
    m_lastFrameDrew = loop();
}

void GifEngine::render(EngineContext* context) {
    instance = this;
}

void GifEngine::deactivate() {
    instance = this;
    stop();
}

void GifEngine::onConfigChanged(const EngineConfig* config) {
    instance = this;
    m_instanceConfig = config;
    m_configuredFolders.clear();
    if (config) {
        String folders = config->getString("folder", "all");
        if (folders == "all" || folders.isEmpty()) {
            m_configuredFolders.push_back("all");
        } else {
            int start = 0;
            while (start < folders.length()) {
                int comma = folders.indexOf(',', start);
                if (comma == -1) comma = folders.length();
                String path = folders.substring(start, comma);
                path.trim();
                if (path.length() > 0) {
                    m_configuredFolders.push_back(path);
                }
                start = comma + 1;
            }
        }
    } else {
        m_configuredFolders.push_back("all");
    }
    rebuildActivePlaylists();

    // Immediately restart playback with newly selected playlists if currently active
    if (isActive() || isPlaying || playlistMode) {
        int count = 1;
        if (m_rotationBudget > 0) {
            count = (int)m_rotationBudget;
        } else if (m_instanceConfig) {
            int cfgCount = m_instanceConfig->getInt("gifs_count", 0);
            if (cfgCount > 0) count = cfgCount;
        }
        stop();
        if (hasDefaultPlaylists()) {
            playDefaultPlaylists(count);
        } else {
            if (matrix) matrix->fillScreen(0);
        }
    }
}

void GifEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    instance = this;
    if (matrix) {
        size_t matrixPixels = matrix->width() * matrix->height();
        if (canvasBuffer) {
            heap_caps_free(canvasBuffer);
            canvasBuffer = nullptr;
        }
        canvasBuffer = allocateCanvasBuffer(matrixPixels);
        if (canvasBuffer) {
            memset(canvasBuffer, 0, matrixPixels * 2);
        }
        allocateShadows(matrixPixels);
        if (m_srcW > 0) updateFitGeometry(m_srcW, m_srcH);
    }
    
    rebuildActivePlaylists();
    
    if (isActive() || hasDefaultPlaylists()) {
        int count = 1;
        if (m_rotationBudget > 0) count = (int)m_rotationBudget;
        else if (m_instanceConfig) count = m_instanceConfig->getInt("gifs_count", 0);
        
        if (hasDefaultPlaylists()) {
            playDefaultPlaylists(count > 0 ? count : -1);
        } else {
            stop();
            if (matrix) matrix->fillScreen(0);
        }
    }
}

bool GifEngine::isFinished() const {
    return !isPlaying && !playlistMode && !hasPendingPlaylists;
}

bool GifEngine::begin(MatrixPanel_I2S_DMA* display) {
    if (!display) return false;
    matrix = display;
    gif.begin(LITTLE_ENDIAN_PIXELS);

    // Scanline canvas: written pixel-by-pixel by GIFDraw and read pixel-by-pixel by render(),
    // both on the Core 1 hot path. PSRAM random access is an order of magnitude slower than
    // internal SRAM, so internal is mandatory here; PSRAM is only a last-resort fallback.
    size_t matrixPixels = matrix->width() * matrix->height();
    canvasBuffer = allocateCanvasBuffer(matrixPixels);
    if (canvasBuffer) {
        memset(canvasBuffer, 0, matrixPixels * 2);
    }
    allocateShadows(matrixPixels);
    return true;
}

void GifEngine::allocateShadows(size_t matrixPixels) {
    freeShadows();
    if (matrixPixels == 0) return;
    // Read sequentially once per frame, so PSRAM is fine here; internal DRAM stays free for TLS/DMA.
    for (int i = 0; i < 2; i++) {
        m_shadow[i] = (uint16_t*)heap_caps_malloc(matrixPixels * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!m_shadow[i]) m_shadow[i] = (uint16_t*)heap_caps_malloc(matrixPixels * 2, MALLOC_CAP_8BIT);
        if (!m_shadow[i]) {
            LOGW("GifEngine", "Shadow buffer %d allocation failed (%u bytes); frames will be repainted in full.", i, (unsigned)(matrixPixels * 2));
        }
    }
    invalidateShadows();
}

void GifEngine::freeShadows() {
    for (int i = 0; i < 2; i++) {
        if (m_shadow[i]) { heap_caps_free(m_shadow[i]); m_shadow[i] = nullptr; }
    }
    invalidateShadows();
}

void GifEngine::updateFitGeometry(int srcW, int srcH) {
    if (!matrix) return;
    if (srcW <= 0) srcW = 128;
    if (srcH <= 0) srcH = 32;
    m_srcW = srcW; m_srcH = srcH;
    int mw = matrix->width(), mh = matrix->height();
    String mode = m_fitMode;
    mode.toLowerCase();
    if (mode == "stretch") {
        m_fitScaleX = max(1, mw / srcW);
        m_fitScaleY = max(1, mh / srcH);
    } else if (mode == "center") {
        m_fitScaleX = m_fitScaleY = 1;
    } else { // "fit" (default): integer scale that keeps the aspect ratio
        int sc = max(1, min(mw / srcW, mh / srcH));
        m_fitScaleX = m_fitScaleY = sc;
    }
    m_fitOffX = (mw - srcW * m_fitScaleX) / 2;
    m_fitOffY = (mh - srcH * m_fitScaleY) / 2;
}

/**
 * Push the canvas to the current back buffer, writing only pixels that differ from what that buffer
 * last received. Returns true when a frame was pushed (a flip is due either way: the front buffer
 * shows the previous frame).
 */
bool GifEngine::blitCanvas() {
    if (!canvasBuffer || !matrix) return false;
    const int w = matrix->width();
    const int h = matrix->height();
    const size_t n = (size_t)w * h;

    uint32_t gen = matrixEngine.externalDrawGeneration();
    if (gen != m_shadowGeneration) {
        m_shadowGeneration = gen;
        invalidateShadows();
    }
    int idx = matrixEngine.isDoubleBuffered() ? (int)(matrixEngine.flipCount() & 1u) : 0;
    uint16_t* shadow = m_shadow[idx];
    bool full = (shadow == nullptr) || !m_shadowValid[idx];

    // Only pixels that differ from the shadow are written. Each drawPixel is expensive on this board
    // (per colour plane: a read-modify-write into the PSRAM DMA buffer plus a cache write-back, about
    // 14 us per pixel measured on the S3 Waveshare at 256x64), so the number of pixels written is the
    // whole cost; the scan over canvas and shadow is a few milliseconds. Writing runs through the
    // library's hlineDMA is no cheaper per pixel and needs a global cache write-back that collides
    // with flash writes (OTA failed while a GIF was playing), so the per-pixel path stays.
    uint32_t t0 = micros();
    size_t written = 0;
    if (full) {
        matrixEngine.blitCanvas565(canvasBuffer, w, h);
        if (shadow) {
            memcpy(shadow, canvasBuffer, n * sizeof(uint16_t));
            m_shadowValid[idx] = true;
        }
        written = n;
    } else {
        // Fast dirty-pixel threshold scan using 32-bit word comparisons (2 pixels per test)
        const uint32_t* c32 = (const uint32_t*)canvasBuffer;
        const uint32_t* s32 = (const uint32_t*)shadow;
        size_t n32 = n / 2;
        size_t dirtyWords = 0;
        const size_t dirtyThresholdWords = 200; // ~400 dirty pixels threshold (2.4% of 256x64)

        for (size_t i = 0; i < n32; i++) {
            if (c32[i] != s32[i]) {
                dirtyWords++;
                if (dirtyWords >= dirtyThresholdWords) break;
            }
        }

        if (dirtyWords >= dirtyThresholdWords) {
            // High motion frame: FastBlit sequential row writes are faster than hundreds of drawPixel calls
            matrixEngine.blitCanvas565(canvasBuffer, w, h);
            memcpy(shadow, canvasBuffer, n * sizeof(uint16_t));
            written = n;
        } else {
            // Low delta: update only the sparse changed pixels
            for (int y = 0; y < h; y++) {
                const uint16_t* row = canvasBuffer + (size_t)y * w;
                uint16_t* srow = shadow + (size_t)y * w;
                for (int x = 0; x < w; x++) {
                    uint16_t c = row[x];
                    if (c != srow[x]) {
                        matrix->drawPixel(x, y, c);
                        srow[x] = c;
                        written++;
                    }
                }
            }
        }
    }
    g_renderStats.gifBlitMicros += micros() - t0;
    g_renderStats.gifPixelsWritten += (uint32_t)written;
    g_renderStats.gifPixelsTotal += (uint32_t)n;
    g_renderStats.gifFrames++;
    return true;
}

uint32_t GifEngine::nextFrameDueInMs() const {
    if (!isPlaying || isPng) return 0xFFFFFFFFu;
    unsigned long due = isRaw ? (rawLastFrameTime + 50) : (gifLastFrameTime + gifCurrentDelay);
    long remaining = (long)(due - millis());
    return remaining <= 0 ? 0u : (uint32_t)remaining;
}

uint16_t* GifEngine::allocateCanvasBuffer(size_t matrixPixels) {
    if (matrixPixels == 0) return nullptr;
    uint16_t* buf = nullptr;
    if (m_hasPsram) {
        buf = (uint16_t*)heap_caps_malloc(matrixPixels * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf) {
            LOGI("GifEngine", "Allocated %u bytes canvas buffer in PSRAM.", (unsigned)(matrixPixels * 2));
            return buf;
        }
        LOGW("GifEngine", "PSRAM canvas allocation failed (%u bytes), falling back to internal DRAM.",
             (unsigned)(matrixPixels * 2));
    }
    buf = (uint16_t*)heap_caps_malloc(matrixPixels * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        LOGE("GifEngine", "Canvas buffer allocation failed (%u bytes).", (unsigned)(matrixPixels * 2));
    }
    return buf;
}

bool GifEngine::playGif(const char* filepath) {
    instance = this;
    stop();
    playlistMode = false; // Playing a single GIF stops the playlist
    
    String path = String(filepath);
    LOGI("GifEngine", "Trying to play: %s", path.c_str());
    
    if (path.endsWith(".raw") || path.endsWith(".RAW")) {
        if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
            currentFile = sd.open(path.c_str(), FILE_OPEN_READ);
            xSemaphoreGive(sdMutex);
        }
        if (!currentFile) {
            LOGE("GifEngine", "Failed to open RAW file: %s", path.c_str());
            return false;
        }
        isRaw = true;
        isPng = false;
        isPlaying = true;
        rawLastFrameTime = 0;
        return true;
    } else if (path.endsWith(".png") || path.endsWith(".PNG")) {
        // Clear back buffer before drawing PNG to remove any leftover text
        if (matrix) {
            matrix->fillScreen(0);
        }
        
        if (decodePng(filepath)) {
            isRaw = false;
            isPng = true;
            isPlaying = true;
            pngShowStartTime = millis();
            needsInitialFlip = true; // Request a flip in the main loop so the drawn PNG becomes visible
            return true;
        }
        return false;
    } else {
        if (m_hasPsram) {
            bool loaded = false;
            if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
                FsFile f = sd.open(path.c_str(), FILE_OPEN_READ);
                if (f) {
                    size_t fileSize = f.size();
                    // Check if we have enough free PSRAM, leave some headroom (e.g. 500KB)
                    if (ESP.getFreePsram() > fileSize + 512000) {
                        psramBuffer = (uint8_t*)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM);
                        if (psramBuffer) {
                            size_t toRead = fileSize;
                            size_t offset = 0;
                            while (toRead > 0) {
                                size_t chunk = (toRead > 8192) ? 8192 : toRead;
                                size_t r = f.read(psramBuffer + offset, chunk);
                                if (r == 0) break;
                                offset += r;
                                toRead -= r;
                            }
                            size_t bytesRead = offset;
                            f.close();
                            if (bytesRead == fileSize) {
                                psramBufferSize = fileSize;
                                loaded = true;
                            } else {
                                freePsramBuffer();
                            }
                        }
                    } else {
                        f.close();
                    }
                }
                xSemaphoreGive(sdMutex);
            }
            if (loaded) {
                if (gif.open(psramBuffer, psramBufferSize, GIFDraw)) {
                    updateFitGeometry(gif.getCanvasWidth(), gif.getCanvasHeight());
                    LOGI("GifEngine", "GIF loaded directly into PSRAM: %s (%zu bytes)", path.c_str(), psramBufferSize);
                    if (canvasBuffer && matrix) {
                        memset(canvasBuffer, 0, matrix->width() * matrix->height() * 2);
                    }
                    isRaw = false;
                    isPng = false;
                    isPlaying = true;
                    return true;
                }
                freePsramBuffer();
            }
        }
        // Fallback to streaming from SD card
        bool streamOpened = false;
        if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
            streamOpened = gif.open(filepath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
            xSemaphoreGive(sdMutex);
        }
        if (streamOpened) {
            updateFitGeometry(gif.getCanvasWidth(), gif.getCanvasHeight());
            LOGD("GifEngine", "GIF opened (streaming from SD): %s", filepath);
            if (canvasBuffer && matrix) {
                memset(canvasBuffer, 0, matrix->width() * matrix->height() * 2);
            }
            isRaw = false;
            isPng = false;
            isPlaying = true;
            return true;
        } else {
            LOGE("GifEngine", "gif.open() failed for: %s", filepath);
        }
    }
    return false;
}

void GifEngine::freePsramBuffer() {
    if (psramBuffer) {
        heap_caps_free(psramBuffer);
        psramBuffer = nullptr;
        psramBufferSize = 0;
    }
}

bool GifEngine::decodePng(const char* filepath) {
    instance = this;
    // Lazily allocate the ~38KB PNGdec decoder only on first actual use - see the `png` member
    // comment in GifEngine.h for why this isn't a permanent value member.
    if (!png) png = new PNG();

    if (canvasBuffer && matrix) {
        memset(canvasBuffer, 0, (size_t)matrix->width() * matrix->height() * sizeof(uint16_t));
    }

    if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        LOGE("GifEngine", "Failed to acquire sdMutex for %s", filepath);
        return false;
    }

    // PNGdec has no concept of animation: decode the whole image once, directly onto the
    // matrix (via PNGDrawCallback -> matrix->drawPixel), then just leave it on screen. loop()
    // only needs to track pngShowStartTime to know when to advance/loop - see loop()/GifEngine.h.
    int rc = png->open(filepath, PNGOpenFile, PNGCloseFile, PNGReadFile, PNGSeekFile, PNGDrawCallback);
    if (rc != PNG_SUCCESS) {
        LOGE("GifEngine", "png.open() failed for %s (rc=%d)", filepath, rc);
        xSemaphoreGive(sdMutex);
        return false;
    }
    updateFitGeometry(png->getWidth(), png->getHeight());
    rc = png->decode((void*)this, 0);
    png->close();
    xSemaphoreGive(sdMutex);
    if (rc != PNG_SUCCESS) {
        LOGE("GifEngine", "png.decode() failed for %s (rc=%d)", filepath, rc);
        return false;
    }
    return true;
}

void GifEngine::expandPlaylists(const std::vector<String>& inputPaths, std::vector<String>& outPaths) {
    outPaths.clear();
    for (String p : inputPaths) {
        String cleanPath = sanitizePlaylistPath(p);
        
        // If the path is /gifs, /gifs_tate or all, load all folders directly from playlists.json
        if (cleanPath == "/gifs" || cleanPath == "/gifs_tate" || cleanPath == "/all" || cleanPath == "all") {
            String rootP = (cleanPath == "/gifs_tate") ? "/gifs_tate" : "/gifs";
            String plJson = rootP + "/playlists.json";
            size_t beforeCount = outPaths.size();
            if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
                if (sd.exists(plJson.c_str())) {
                    FsFile plFile = sd.open(plJson.c_str(), FILE_OPEN_READ);
                    if (plFile) {
                        size_t sz = plFile.size();
                        if (sz > 0 && sz < 131072) {
                            char* buf = psramFound() ? (char*)ps_malloc(sz + 1) : (char*)malloc(sz + 1);
                            if (buf) {
                                size_t n = plFile.read((uint8_t*)buf, sz);
                                buf[n] = '\0';
                                char* jsonStart = buf;
                                while (*jsonStart && *jsonStart != '{') jsonStart++;
                                DynamicJsonDocument doc(sz + 2048);
                                DeserializationError err = deserializeJson(doc, jsonStart);
                                free(buf);
                                if (!err && doc.is<JsonObject>()) {
                                    for (JsonPair kv : doc.as<JsonObject>()) {
                                        if (kv.value().is<JsonObject>()) {
                                            String subPath = kv.value()["path"].as<String>();
                                            if (subPath.length() > 0) {
                                                // Re-anchor on the requested root: a playlists.json
                                                // stored under /gifs_tate may still declare bare or
                                                // /gifs-prefixed paths.
                                                String leaf = extractPlaylistLeaf(sanitizePlaylistPath(subPath));
                                                if (leaf.isEmpty()) continue;
                                                outPaths.push_back(rootP + "/" + leaf);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        plFile.close();
                    }
                }
                // Fallback: no (or unusable) playlists.json - enumerate the sub-directories of the
                // orientation root directly, so a freshly copied SD card still plays.
                if (outPaths.size() == beforeCount) {
                    FsFile rootDir = sd.open(rootP.c_str(), FILE_OPEN_READ);
                    if (rootDir && isDirectory(rootDir)) {
                        FsFile entry;
                        while (getNextFile(rootDir, entry)) {
                            if (!isDirectory(entry)) continue;
                            String name = getFileName(entry);
                            if (isMacJunk(name)) continue;
                            outPaths.push_back(rootP + "/" + name);
                        }
                        if (entry) entry.close();
                    }
                    if (rootDir) rootDir.close();
                    if (outPaths.size() == beforeCount) {
                        // Last resort: the root folder itself may hold flat GIF files.
                        outPaths.push_back(rootP);
                    }
                }
                xSemaphoreGive(sdMutex);
            }
            continue;
        } else {
            outPaths.push_back(cleanPath);
        }
    }
    
    // Safety fallback
    if (outPaths.empty() && !inputPaths.empty()) {
        for (String p : inputPaths) {
            outPaths.push_back(sanitizePlaylistPath(p));
        }
    }
}

String GifEngine::extractPlaylistLeaf(const String& cleanPath) {
    if (cleanPath.startsWith("/gifs_tate/")) return cleanPath.substring(11);
    if (cleanPath.startsWith("/gifs/")) return cleanPath.substring(6);
    return String();
}

void GifEngine::playPlaylists(std::vector<String> playlistPaths) {
    if (playlistPaths.empty()) return;
    std::vector<String> expanded;
    expandPlaylists(playlistPaths, expanded);
    pendingPlaylists = expanded;
    hasPendingPlaylists = true;
    remainingGifsToPlay = -1;
    stop();
    loop();
}

void GifEngine::setDefaultPlaylists(std::vector<String> playlistPaths) {
    std::vector<String> expanded;
    expandPlaylists(playlistPaths, expanded);
    defaultPlaylists = expanded;
    LOGI("GifEngine", "Set %u default playlist folders.", (unsigned int)expanded.size());
}

String GifEngine::sanitizePlaylistPath(String p) {
    p.trim();
    if (!p.startsWith("/")) p = "/" + p;
    // Exact match ("/gifs", "/gifs_tate" or "/sprites", no trailing slash) must NOT be re-prefixed
    if (p == "/gifs" || p == "/gifs_tate" || p == "/sprites") return p;
    if (p.startsWith("/gifs_tate/")) return p;
    if (p.startsWith("/sprites/")) return p;
    if (!p.startsWith("/gifs/")) p = "/gifs" + p;
    return p;
}

void GifEngine::playDefaultPlaylists(int numGifs) {
    if (defaultPlaylists.empty()) return;
    pendingPlaylists = defaultPlaylists;
    hasPendingPlaylists = true;
    remainingGifsToPlay = numGifs;
}

// Weight every active folder by its file count from the root's playlists.json (kept current by the
// rescan/upload/delete handlers). Folders missing from it weigh 1. Refreshed when the playlist set
// changes and every 10 minutes; one small file read per orientation root under the SD lock.
void GifEngine::refreshPlaylistWeights() {
    playlistWeights.assign(playlists.size(), 1);
    playlistWeightsLoadedMs = millis();
    static const char* ROOTS[] = { "/gifs", "/gifs_tate" };
    for (const char* rootP : ROOTS) {
        String prefix = String(rootP) + "/";
        bool rootUsed = false;
        for (const auto& p : playlists) { if (p.startsWith(prefix)) { rootUsed = true; break; } }
        if (!rootUsed) continue;
        String plJson = String(rootP) + "/playlists.json";
        SdLockGuard guard(pdMS_TO_TICKS(1500));
        if (!guard) continue;
        if (sd.exists(plJson.c_str())) {
            FsFile f = sd.open(plJson.c_str(), FILE_OPEN_READ);
            if (f) {
                size_t sz = f.size();
                if (sz > 0 && sz < 131072) {
                    char* buf = psramFound() ? (char*)ps_malloc(sz + 1) : (char*)malloc(sz + 1);
                    if (buf) {
                        size_t n = f.read((uint8_t*)buf, sz);
                        buf[n] = '\0';
                        char* js = buf;
                        while (*js && *js != '{') js++;
                        SpiRamJsonDocument doc(sz + 2048);   // Golden Rule: JSON scratch lives in PSRAM
                        if (!deserializeJson(doc, js) && doc.is<JsonObject>()) {
                            for (size_t i = 0; i < playlists.size(); i++) {
                                if (!playlists[i].startsWith(prefix)) continue;
                                String leaf = extractPlaylistLeaf(playlists[i]);
                                if (leaf.isEmpty()) continue;
                                JsonVariant e = doc[leaf.c_str()];
                                if (!e.isNull() && e["count"].is<int>() && e["count"].as<int>() > 0) {
                                    playlistWeights[i] = (uint32_t)e["count"].as<int>();
                                }
                            }
                        }
                        free(buf);
                    }
                }
                f.close();
            }
        }
    }
}

uint32_t GifEngine::pathHash(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

// True when this file is inside the window of recent plays. The window never covers more than half
// of a folder, so small folders keep playing and only genuine near-repeats are skipped.
bool GifEngine::playedRecently(uint32_t h, size_t folderFiles) {
    if (!recentHashes || recentCount == 0 || folderFiles < 8) return false;
    uint32_t window = (folderFiles * 3) / 4;   // never the whole folder, so a pick is always possible
    if (window > recentCount) window = recentCount;
    if (window > recentCap) window = recentCap;
    for (uint32_t back = 1; back <= window; back++) {
        uint16_t idx = (uint16_t)((recentHead + recentCap - back) % recentCap);
        if (recentHashes[idx] == h) return true;
    }
    return false;
}

void GifEngine::rememberPlayed(uint32_t h) {
    if (!recentHashes) {
        recentCap = psramFound() ? RECENT_MAX : 256;
        recentHashes = (uint32_t*)(psramFound() ? ps_malloc(recentCap * sizeof(uint32_t))
                                                : malloc(recentCap * sizeof(uint32_t)));
        if (!recentHashes) { recentCap = 0; return; }   // no window: behaves as before
        memset(recentHashes, 0, recentCap * sizeof(uint32_t));
        recentCount = 0; recentHead = 0;
    }
    recentHashes[recentHead] = h;
    recentHead = (uint16_t)((recentHead + 1) % recentCap);
    if (recentCount < recentCap) recentCount++;
}

int GifEngine::pickPlaylistIndex() {
    if (playlists.empty()) return -1;
    if (playlistWeights.size() != playlists.size() || millis() - playlistWeightsLoadedMs > 600000UL) {
        refreshPlaylistWeights();
    }
    uint32_t total = 0;
    for (uint32_t w : playlistWeights) total += w;
    if (total == 0) return random(playlists.size());
    uint32_t r = (uint32_t)random(total);
    for (size_t i = 0; i < playlistWeights.size(); i++) {
        if (r < playlistWeights[i]) return (int)i;
        r -= playlistWeights[i];
    }
    return (int)playlists.size() - 1;
}

void GifEngine::loadNextFileInPlaylist() {
    if (playlists.empty()) {
        stop();
        return;
    }
    
    if (remainingGifsToPlay > 0) {
        remainingGifsToPlay--;
    } else if (remainingGifsToPlay == 0) {
        stop();
        return;
    }
    
    int attempts = 0;
    while(attempts < 5) {
        int pIndex = pickPlaylistIndex();
        if (pIndex < 0) { stop(); return; }
        String pPath = playlists[pIndex];
        
        String indexPath = pPath + "/index.txt";
        indexPath.replace("//", "/");
        String targetPath = "";
        std::vector<String> validFiles;
        // Distinguishes "this folder is genuinely empty" from "the SD bus was busy this round".
        // Evicting a playlist on a transient mutex timeout permanently empties the rotation.
        bool sdAccessOk = false;

        if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
            sdAccessOk = true;
            if (sd.exists(indexPath.c_str())) {
                FsFile indexFile = sd.open(indexPath.c_str(), FILE_OPEN_READ);
                if (indexFile && indexFile.size() > 0) {
                    while (indexFile.available()) {
                        String line = indexFile.readStringUntil('\n');
                        line.trim();
                        while (line.length() > 0 && ((uint8_t)line[0] < 32 || (uint8_t)line[0] > 126)) {
                            line = line.substring(1);
                        }
                        if (line.length() > 0 && !isMacJunk(line)) {
                            if (line.indexOf("._") == -1 && line.indexOf("System Volume") == -1) {
                                validFiles.push_back(line);
                            }
                        }
                    }
                    indexFile.close();
                }
            }
            xSemaphoreGive(sdMutex);
        }

        // Single-folder directory scan fallback if index.txt is not found or empty (v3.1.0 compatibility)
        if (validFiles.empty()) {
            if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1500)) == pdTRUE) {
                sdAccessOk = true;
                FsFile pDir = sd.open(pPath.c_str(), FILE_OPEN_READ);
                if (pDir && isDirectory(pDir)) {
                    FsFile fileEntry;
                    while (getNextFile(pDir, fileEntry)) {
                        if (!isDirectory(fileEntry)) {
                            String name = getFileName(fileEntry);
                            if (!isMacJunk(name) && name.indexOf("._") == -1 && name.indexOf("System Volume") == -1) {
                                String lower = name;
                                lower.toLowerCase();
                                if (lower.endsWith(".gif") || lower.endsWith(".png") || lower.endsWith(".raw")) {
                                    validFiles.push_back(name);
                                }
                            }
                        }
                    }
                    pDir.close();
                }
                xSemaphoreGive(sdMutex);
            }
        }
        
        if (!validFiles.empty()) {
            int selectedIdx = random(validFiles.size());
            String candidate = pPath + "/" + validFiles[selectedIdx];
            if (validFiles.size() > 1 && candidate == lastPlayedGif) {
                selectedIdx = (selectedIdx + 1 + random(validFiles.size() - 1)) % validFiles.size();
                candidate = pPath + "/" + validFiles[selectedIdx];
            }
            // Pass over files seen recently; give up after a few tries so a folder always yields one.
            for (int tries = 0; tries < 16 && validFiles.size() > 1; tries++) {
                if (!playedRecently(pathHash(candidate.c_str()), validFiles.size())) break;
                selectedIdx = random(validFiles.size());
                candidate = pPath + "/" + validFiles[selectedIdx];
            }
            targetPath = candidate;
        }
        
        if (targetPath.length() == 0) {
            if (sdAccessOk && playlists.size() > 1) {
                LOGW("GifEngine", "No valid files or index.txt found in %s. Removing from active playlists.", pPath.c_str());
                playlists.erase(playlists.begin() + pIndex);
                if (pIndex < (int)playlistWeights.size()) playlistWeights.erase(playlistWeights.begin() + pIndex);
            } else {
                LOGW("GifEngine", "Could not resolve a file in %s this round (sdAccess=%d). Keeping playlist.",
                     pPath.c_str(), (int)sdAccessOk);
            }
            attempts++;
            continue;
        }
        
        targetPath.replace("//", "/");
        if (targetPath.indexOf("._") == -1 && targetPath.indexOf("System Volume") == -1) {
            if (playGif(targetPath.c_str())) {
                playlistMode = true;
                lastPlayedGif = targetPath;
                rememberPlayed(pathHash(targetPath.c_str()));
                return; // SUCCESS!
            }
        }
        attempts++;
    }
    
    // Fallback if empty or invalid after attempts
    stop();
    playlistMode = false;
}

void GifEngine::stop() {
    if (isPlaying) {
        if (!isRaw && !isPng) gif.close();
        if (currentFile) currentFile.close();
        isPlaying = false;
    }
    playlistMode = false;
    hasPendingPlaylists = false;
    freePsramBuffer();
}

bool GifEngine::loop() {
    instance = this;
    if (hasPendingPlaylists) {
        stop();
        playlists = pendingPlaylists;
        playlistWeights.clear();   // re-read the folder sizes for the new set
        playlistMode = true;
        hasPendingPlaylists = false;
        loadNextFileInPlaylist();
        return true;
    }

    if (!isPlaying) {
        if (playlistMode) loadNextFileInPlaylist();
        return false;
    }

    if (isRaw) {
        return playRawFrame();
    } else if (isPng) {
        blitCanvas();   // both DMA buffers end up holding the image; cheap once they do
        if (millis() - pngShowStartTime > pngHoldDurationMs) {
            if (playlistMode) {
                loadNextFileInPlaylist();
                return true; // We switched image, flip required
            } else {
                pngShowStartTime = millis(); // Loop single file
            }
        }
        return true; // Continuously render static PNG onto alternating DMA framebuffers
    } else {
        if (millis() - gifLastFrameTime < gifCurrentDelay) return false;
        // Advance target time by the intended delay.
        // If we're lagging severely (e.g. CPU stall > 100ms), snap to current time to avoid fast-forwarding
        gifLastFrameTime += gifCurrentDelay;
        if (millis() - gifLastFrameTime > 100) {
            gifLastFrameTime = millis();
        }
        
        unsigned long startDecode = millis();
        uint32_t decodeStartUs = micros();
        int delayMs = 0;
        int result = gif.playFrame(false, &delayMs, (void*)this);
        g_renderStats.gifDecodeMicros += micros() - decodeStartUs;

        blitCanvas();
        
        unsigned long decodeTime = millis() - startDecode;
        
        // Ensure minimum delay so we don't completely freeze the ESP32 with 0ms delay GIFs
        if (delayMs < 20) {
            delayMs = 20; // Cap at 50fps max to prevent matrix stuttering
        }
        
        if (m_speedMultiplier > 0.05f) {
            delayMs = (int)((float)delayMs / m_speedMultiplier);
            if (delayMs < 15) delayMs = 15;
        }
        gifCurrentDelay = delayMs;
        
        static unsigned long lastLog = 0;
        if (millis() - lastLog > 2000) {
            LOGD("GifEngine", "Frame: delayMs=%d, decodeTime=%lums", delayMs, decodeTime);
            lastLog = millis();
        }
        
        if (result <= 0) {
            // End of GIF or error
            if (playlistMode) {
                loadNextFileInPlaylist();
            } else {
                gif.reset(); // Loop single file
            }
        }
        return true; // Frame decoded, requires flip
    }
}


bool GifEngine::playRawFrame() {
    instance = this;
    if (millis() - rawLastFrameTime < 50) return false; // ~20 FPS limit for raw files
    rawLastFrameTime = millis();
    
    if (!currentFile) return false;
    
    int w = matrix->width();
    int h = matrix->height();
    int bytesToRead = w * h * 2;
    
    uint8_t buffer[1024]; // Stack buffer
    
    int bytesReadTotal = 0;
    int y = 0;
    int x = 0;
    
    while (bytesReadTotal < bytesToRead) {
        int toRead = min((int)sizeof(buffer), bytesToRead - bytesReadTotal);
        int read = currentFile.read(buffer, toRead);
        if (read <= 0) break;
        
        // Draw directly to matrix
        for (int i = 0; i < read; i += 2) {
            uint16_t color = buffer[i] | (buffer[i+1] << 8); // Little endian
            matrix->drawPixel(x, y, color);
            x++;
            if (x >= w) {
                x = 0;
                y++;
            }
        }
        bytesReadTotal += read;
    }
    
    if (bytesReadTotal < bytesToRead) {
        // End of file
        if (playlistMode) {
            loadNextFileInPlaylist();
        } else {
            currentFile.seek(0); // Loop single file
        }
    }
    return true;
}

// --- AnimatedGIF Callbacks ---

// Symmetric with GIFReadFile/GIFSeekFile: no sdMutex here either. The caller (playGif) already
// owns the mutex while gif.open() runs, and re-entering it would deadlock.
void* GifEngine::GIFOpenFile(const char *fname, int32_t *pSize) {
    if (!instance) return nullptr;
    instance->currentFile = sd.open(fname, FILE_OPEN_READ);
    if (instance->currentFile) {
        *pSize = instance->currentFile.size();
        return (void*)&instance->currentFile;
    }
    return nullptr;
}

void GifEngine::GIFCloseFile(void *pHandle) {
    FsFile *f = static_cast<FsFile *>(pHandle);
    if (f && *f) f->close();
}

// NOTE (Core 1 hot-path, zero-mutex invariant): these callbacks are invoked synchronously by
// AnimatedGIF, both from gif.open() - which already runs under sdMutex - and from playFrame() on
// the Core 1 render path. sdMutex is a non-recursive FreeRTOS mutex, so taking it here would
// self-deadlock during open and would break the lock-free render contract during playback.
// Ownership is coarse-grained instead: the streaming FsFile handle belongs exclusively to Core 1
// for the whole lifetime of the playback session.
int32_t GifEngine::GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    FsFile *f = static_cast<FsFile *>(pFile->fHandle);
    if (!f || !*f) return 0;

    int32_t iBytesRead = f->read(pBuf, iLen);
    if (iBytesRead < 0) iBytesRead = 0;
    pFile->iPos = f->position();
    return iBytesRead;
}

int32_t GifEngine::GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
    FsFile *f = static_cast<FsFile *>(pFile->fHandle);
    if (!f || !*f) return 0;

    f->seek(iPosition);
    pFile->iPos = f->position();
    return pFile->iPos;
}

void GifEngine::GIFDraw(GIFDRAW *pDraw) {
    GifEngine* self = static_cast<GifEngine*>(pDraw->pUser);
    if (!self) self = instance;
    if (!self || !self->matrix) return;
    
    // Placement was computed when the file opened (updateFitGeometry); nothing per scanline.
    const int scaleX = self->m_fitScaleX;
    const int scaleY = self->m_fitScaleY;
    const int offsetX = self->m_fitOffX;
    const int offsetY = self->m_fitOffY;

    uint8_t *s;
    uint16_t *usPalette;
    int x, y, iWidth;

    iWidth = pDraw->iWidth;
    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y;
    
    int baseY = offsetY + y * scaleY;

    s = pDraw->pPixels;
    
    if (pDraw->ucHasTransparency) {
        uint8_t c, ucTransparent = pDraw->ucTransparent;
        for (x = 0; x < iWidth; x++) {
            c = *s++;
            if (c != ucTransparent) {
                int px = offsetX + (pDraw->iX + x) * scaleX;
                if (scaleX == 1 && scaleY == 1) {
                    if (px >= 0 && px < self->matrix->width() && baseY >= 0 && baseY < self->matrix->height()) {
                        if (self->canvasBuffer) {
                            self->canvasBuffer[baseY * self->matrix->width() + px] = usPalette[c];
                        } else {
                            self->matrix->drawPixel(px, baseY, usPalette[c]);
                        }
                    }
                } else {
                    if (self->canvasBuffer) {
                        int mw = self->matrix->width();
                        int mh = self->matrix->height();
                        for (int sy = 0; sy < scaleY; sy++) {
                            for (int sx = 0; sx < scaleX; sx++) {
                                if (px+sx >= 0 && px+sx < mw && baseY+sy >= 0 && baseY+sy < mh)
                                    self->canvasBuffer[(baseY + sy) * mw + (px + sx)] = usPalette[c];
                            }
                        }
                    } else {
                        self->matrix->fillRect(px, baseY, scaleX, scaleY, usPalette[c]);
                    }
                }
            }
        }
    } else {
        for (x = 0; x < iWidth; x++) {
            uint16_t color = usPalette[*s++];
            int px = offsetX + (pDraw->iX + x) * scaleX;
            if (scaleX == 1 && scaleY == 1) {
                if (px >= 0 && px < self->matrix->width() && baseY >= 0 && baseY < self->matrix->height()) {
                    if (self->canvasBuffer) {
                        self->canvasBuffer[baseY * self->matrix->width() + px] = color;
                    } else {
                        self->matrix->drawPixel(px, baseY, color);
                    }
                }
            } else {
                if (self->canvasBuffer) {
                    int mw = self->matrix->width();
                    int mh = self->matrix->height();
                    for (int sy = 0; sy < scaleY; sy++) {
                        for (int sx = 0; sx < scaleX; sx++) {
                            if (px+sx >= 0 && px+sx < mw && baseY+sy >= 0 && baseY+sy < mh)
                                self->canvasBuffer[(baseY + sy) * mw + (px + sx)] = color;
                        }
                    }
                } else {
                    self->matrix->fillRect(px, baseY, scaleX, scaleY, color);
                }
            }
        }
    }
}

// --- PNGdec callbacks (static .png assets, mirrors GIF callbacks above) ---

void* GifEngine::PNGOpenFile(const char *fname, int32_t *pSize) {
    if (instance) {
        instance->pngFile = sd.open(fname, FILE_OPEN_READ);
        if (instance->pngFile) {
            *pSize = instance->pngFile.size();
            return (void*)&instance->pngFile;
        }
    }
    return nullptr;
}

void GifEngine::PNGCloseFile(void *pHandle) {
    FsFile *f = static_cast<FsFile *>(pHandle);
    if (f && *f) f->close();
}

int32_t GifEngine::PNGReadFile(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    FsFile *f = static_cast<FsFile *>(pFile->fHandle);
    if (!f || !*f) return 0;

    int32_t iBytesRead = f->read(pBuf, iLen);
    pFile->iPos = f->position();
    return iBytesRead;
}

int32_t GifEngine::PNGSeekFile(PNGFILE *pFile, int32_t iPosition) {
    FsFile *f = static_cast<FsFile *>(pFile->fHandle);
    if (!f || !*f) return 0;

    f->seek(iPosition);
    pFile->iPos = f->position();
    return pFile->iPos;
}

int GifEngine::PNGDrawCallback(PNGDRAW *pDraw) {
    GifEngine* self = static_cast<GifEngine*>(pDraw->pUser);
    if (!self) self = instance;
    if (!self || !self->matrix || !self->png) return 0;

    // Placement was computed in decodePng (updateFitGeometry) before decoding started.
    const int scaleX = self->m_fitScaleX;
    const int scaleY = self->m_fitScaleY;
    const int offsetX = self->m_fitOffX;
    const int offsetY = self->m_fitOffY;

    static uint16_t lineBuffer[512]; // Increased to 512 for safety
    int iWidth = pDraw->iWidth;
    if (iWidth > 512) iWidth = 512;

    self->png->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

    int y = pDraw->y;
    int baseY = offsetY + y * scaleY;
    int mW = self->matrix->width();
    int mH = self->matrix->height();

    for (int x = 0; x < iWidth; x++) {
        uint16_t color = lineBuffer[x];
        int px = offsetX + x * scaleX;
        if (scaleX == 1 && scaleY == 1) {
            if (px >= 0 && px < mW && baseY >= 0 && baseY < mH) {
                if (self->canvasBuffer) self->canvasBuffer[baseY * mW + px] = color;
                else self->matrix->drawPixel(px, baseY, color);
            }
        } else {
            for (int dy = 0; dy < scaleY; dy++) {
                int py = baseY + dy;
                if (py < 0 || py >= mH) continue;
                for (int dx = 0; dx < scaleX; dx++) {
                    int ppx = px + dx;
                    if (ppx < 0 || ppx >= mW) continue;
                    if (self->canvasBuffer) self->canvasBuffer[py * mW + ppx] = color;
                }
            }
            if (!self->canvasBuffer) self->matrix->fillRect(px, baseY, scaleX, scaleY, color);
        }
    }
    return 1;
}

EngineDescriptor GifEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc_gifs;
    desc_gifs.metadata = {"gifs", "GIF Player", "media", FIRMWARE_VERSION};
    desc_gifs.capabilities.realtime = true;
    desc_gifs.capabilities.selfPaced = true;
    desc_gifs.requirements.needsAudio = false;
    desc_gifs.requirements.needsNetwork = false;
    desc_gifs.schema.fields = {
        ConfigField("folder", ConfigType::LIST, "Playlists", "Active GIF playlists", "all", false, "", "", "", "", "/api/playlists", true, "", ValidationPolicy::Accept),
        ConfigField("speed_multiplier", ConfigType::FLOAT, "Speed Multiplier", "Playback speed factor", "1.0", false, "0.25", "3.0", "0.25", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("shuffle", ConfigType::BOOLEAN, "Shuffle", "Randomize animation order", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc_gifs.factory = []() { return std::unique_ptr<IEngine>(new GifEngine()); };
    return desc_gifs;
}

