#include "GifEngine.h"
#include <ArduinoJson.h>
#include "../core/SDUtils.h"
#include "../core/Logger.h"
#include "../core/ConfigLoader.h"

GifEngine* GifEngine::instance = nullptr;

GifEngine::GifEngine() : matrix(nullptr), isPlaying(false), playlistMode(false), isRaw(false), isPng(false), needsInitialFlip(false), rawLastFrameTime(0), pngShowStartTime(0), psramBuffer(nullptr), psramBufferSize(0) {
    instance = this;
}

GifEngine::~GifEngine() {
    stop();
    delete png;
}

#include "../core/LayoutHelper.h"

bool GifEngine::isDisplayVertical() const {
    if (matrix && (matrix->height() > matrix->width() || matrix->width() < 48 || matrix->height() > (matrix->width() * 3) / 2)) return true;
    if (m_context) {
        DisplayGeometry g = m_context->getGeometry();
        if (g.height > g.width || g.width < 48 || g.height > (g.width * 3) / 2) return true;
        if (g.layoutClass == LayoutClass::PORTRAIT || g.layoutClass == LayoutClass::TALL) return true;
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

    if (vertical) {
        // VERTICAL (TATE) DISPLAY: Only play /gifs_tate folders!
        if (isAll) {
            std::vector<String> input = { "/gifs_tate" };
            expandPlaylists(input, filtered);
        } else {
            for (const String& f : m_configuredFolders) {
                String clean = sanitizePlaylistPath(f);
                if (clean == "/gifs_tate") {
                    std::vector<String> input = { "/gifs_tate" };
                    expandPlaylists(input, filtered);
                } else if (clean.startsWith("/gifs_tate/")) {
                    if (sd.exists(clean.c_str())) filtered.push_back(clean);
                } else if (clean.startsWith("/gifs/")) {
                    // Check if matching tate folder exists
                    String sub = clean.substring(6);
                    String tatePath = "/gifs_tate/" + sub;
                    if (sd.exists(tatePath.c_str())) filtered.push_back(tatePath);
                } else {
                    String tatePath = "/gifs_tate/" + f;
                    if (sd.exists(tatePath.c_str())) filtered.push_back(tatePath);
                }
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
                if (clean == "/gifs") {
                    std::vector<String> input = { "/gifs" };
                    expandPlaylists(input, filtered);
                } else if (clean.startsWith("/gifs_tate/")) {
                    // Check if matching yoko folder exists
                    String sub = clean.substring(11);
                    String yokoPath = "/gifs/" + sub;
                    if (sd.exists(yokoPath.c_str())) filtered.push_back(yokoPath);
                } else if (clean.startsWith("/gifs/")) {
                    if (sd.exists(clean.c_str())) filtered.push_back(clean);
                } else {
                    String yokoPath = "/gifs/" + f;
                    if (sd.exists(yokoPath.c_str())) filtered.push_back(yokoPath);
                }
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
    m_context = context;
    m_lastFrameDrew = loop();
}

void GifEngine::render(EngineContext* context) {}

void GifEngine::deactivate() {
    stop();
}

void GifEngine::onConfigChanged(const EngineConfig* config) {
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
    if (matrix) {
        size_t matrixPixels = matrix->width() * matrix->height();
        if (canvasBuffer) {
            heap_caps_free(canvasBuffer);
            canvasBuffer = nullptr;
        }
        canvasBuffer = (uint16_t*)heap_caps_malloc(matrixPixels * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (canvasBuffer) {
            memset(canvasBuffer, 0, matrixPixels * 2);
        }
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

    // Allocate canvasBuffer in fast INTERNAL RAM to composite GIF delta frames.
    // This is required when using double-buffering on the matrix, otherwise delta frames
    // are drawn to alternating buffers causing horrible flickering and ghosting.
    size_t matrixPixels = matrix->width() * matrix->height();
    canvasBuffer = (uint16_t*)heap_caps_malloc(matrixPixels * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (canvasBuffer) {
        memset(canvasBuffer, 0, matrixPixels * 2);
    }
    return true;
}

bool GifEngine::playGif(const char* filepath) {
    stop();
    playlistMode = false; // Playing a single GIF stops the playlist
    
    String path = String(filepath);
    LOGI("GifEngine", "Trying to play: %s", path.c_str());
    
    if (path.endsWith(".raw") || path.endsWith(".RAW")) {
        currentFile = sd.open(path.c_str(), FILE_OPEN_READ);
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
                            if (gif.open(psramBuffer, psramBufferSize, GIFDraw)) {
                                LOGD("GifEngine", "GIF loaded directly from PSRAM: %s (%zu bytes)", path.c_str(), psramBufferSize);
                                if (canvasBuffer && matrix) {
                                    memset(canvasBuffer, 0, matrix->width() * matrix->height() * 2);
                                }
                                isRaw = false;
                                isPng = false;
                                isPlaying = true;
                                return true;
                            }
                        }
                        // Fallback if failed
                        freePsramBuffer();
                    }
                } else {
                    f.close();
                }
            }
        }
        // Fallback to streaming from SD card
        if (gif.open(filepath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
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
    // Lazily allocate the ~38KB PNGdec decoder only on first actual use - see the `png` member
    // comment in GifEngine.h for why this isn't a permanent value member.
    if (!png) png = new PNG();

    // PNGdec has no concept of animation: decode the whole image once, directly onto the
    // matrix (via PNGDrawCallback -> matrix->drawPixel), then just leave it on screen. loop()
    // only needs to track pngShowStartTime to know when to advance/loop - see loop()/GifEngine.h.
    int rc = png->open(filepath, PNGOpenFile, PNGCloseFile, PNGReadFile, PNGSeekFile, PNGDrawCallback);
    if (rc != PNG_SUCCESS) {
        LOGE("GifEngine", "png.open() failed for %s (rc=%d)", filepath, rc);
        return false;
    }
    rc = png->decode(NULL, 0);
    png->close();
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
            bool loadedFromJson = false;
            if (sd.exists(plJson.c_str())) {
                FsFile plFile = sd.open(plJson.c_str(), FILE_OPEN_READ);
                if (plFile) {
                    size_t sz = plFile.size();
                    if (sz > 0 && sz < 65536) {
                        char* buf = (char*)malloc(sz + 1);
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
                                            outPaths.push_back(sanitizePlaylistPath(subPath));
                                            loadedFromJson = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    plFile.close();
                }
            }
            if (!loadedFromJson) {
                // Fallback to SD directory check if playlists.json is absent or invalid
                FsFile dir = sd.open(rootP.c_str(), FILE_OPEN_READ);
                if (!dir || !isDirectory(dir)) {
                    if (rootP.startsWith("/")) dir = sd.open(rootP.substring(1).c_str(), FILE_OPEN_READ);
                }
                if (dir && isDirectory(dir)) {
                    FsFile entry;
                    while (getNextFile(dir, entry)) {
                        if (isDirectory(entry)) {
                            String subName = getFileName(entry);
                            if (!isMacJunk(subName)) {
                                outPaths.push_back(rootP + "/" + subName);
                            }
                        }
                    }
                    dir.close();
                }
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
        int pIndex = random(playlists.size());
        String pPath = playlists[pIndex];
        
        String indexPath = pPath + "/index.txt";
        indexPath.replace("//", "/");
        FsFile indexFile;
        if (sd.exists(indexPath.c_str())) {
            indexFile = sd.open(indexPath.c_str(), FILE_OPEN_READ);
        }
        
        String targetPath = "";
        std::vector<String> validFiles;
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
        
        // Dynamic directory scan fallback if index.txt not present or empty
        if (validFiles.empty()) {
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
        }
        
        if (!validFiles.empty()) {
            int selectedIdx = random(validFiles.size());
            String candidate = pPath + "/" + validFiles[selectedIdx];
            if (validFiles.size() > 1 && candidate == lastPlayedGif) {
                selectedIdx = (selectedIdx + 1 + random(validFiles.size() - 1)) % validFiles.size();
                candidate = pPath + "/" + validFiles[selectedIdx];
            }
            targetPath = candidate;
        }
        
        if (targetPath.length() == 0) {
            LOGW("GifEngine", "No valid files or index.txt found in %s", pPath.c_str());
        }
        
        if (targetPath.length() > 0) {
            targetPath.replace("//", "/");
            if (targetPath.indexOf("._") == -1 && targetPath.indexOf("System Volume") == -1) {
                if (playGif(targetPath.c_str())) {
                    playlistMode = true;
                    lastPlayedGif = targetPath;
                    return; // SUCCESS!
                }
            }
        }
        attempts++;
    }
    
    // Fallback if empty or invalid after 5 attempts
    stop();
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
    if (hasPendingPlaylists) {
        stop();
        playlists = pendingPlaylists;
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
        if (needsInitialFlip) {
            needsInitialFlip = false;
            return true; // Return true to force main.cpp to flip the buffer once
        }
        if (millis() - pngShowStartTime > pngHoldDurationMs) {
            if (playlistMode) {
                loadNextFileInPlaylist();
                return true; // We switched image, flip required
            } else {
                pngShowStartTime = millis(); // Loop single file
            }
        }
        return false; // PNG is static, no need to flip once displayed
    } else {
        if (millis() - gifLastFrameTime < gifCurrentDelay) return false;
        // Advance target time by the intended delay.
        // If we're lagging severely (e.g. CPU stall > 100ms), snap to current time to avoid fast-forwarding
        gifLastFrameTime += gifCurrentDelay;
        if (millis() - gifLastFrameTime > 100) {
            gifLastFrameTime = millis();
        }
        
        unsigned long startDecode = millis();
        int delayMs = 0;
        int result = gif.playFrame(false, &delayMs);
        
        if (canvasBuffer && matrix) {
            int canvasW = gif.getCanvasWidth();
            int canvasH = gif.getCanvasHeight();
            if (canvasW <= 0) canvasW = 128;
            if (canvasH <= 0) canvasH = 32;
            
            int scaleX = max(1, matrix->width() / canvasW);
            int scaleY = max(1, matrix->height() / canvasH);
            
            int offsetX = (matrix->width() - (canvasW * scaleX)) / 2;
            int offsetY = (matrix->height() - (canvasH * scaleY)) / 2;
            int drawW = canvasW * scaleX;
            int drawH = canvasH * scaleY;
            
            // Only push pixels within the GIF's actual scaled bounding box
            for (int y = offsetY; y < offsetY + drawH; y++) {
                if (y < 0 || y >= matrix->height()) continue;
                for (int x = offsetX; x < offsetX + drawW; x++) {
                    if (x < 0 || x >= matrix->width()) continue;
                    matrix->drawPixel(x, y, canvasBuffer[y * matrix->width() + x]);
                }
            }
        }
        
        unsigned long decodeTime = millis() - startDecode;
        
        // Ensure minimum delay so we don't completely freeze the ESP32 with 0ms delay GIFs
        if (delayMs < 20) {
            delayMs = 20; // Cap at 50fps max to prevent matrix stuttering
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

int32_t GifEngine::GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    int32_t iBytesRead = iLen;
    FsFile *f = static_cast<FsFile *>(pFile->fHandle);
    if (!f || !*f) return 0;
    
    iBytesRead = f->read(pBuf, iLen);
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
    if (!instance || !instance->matrix) return;
    
    int canvasW = instance->gif.getCanvasWidth();
    int canvasH = instance->gif.getCanvasHeight();
    if (canvasW <= 0) canvasW = 128; // Fallback
    if (canvasH <= 0) canvasH = 32;
    
    int scaleX = instance->matrix->width() / canvasW;
    int scaleY = instance->matrix->height() / canvasH;
    int scale = min(scaleX, scaleY);
    if (scale < 1) scale = 1;
    scaleX = scale;
    scaleY = scale;
    
    int offsetX = (instance->matrix->width() - (canvasW * scaleX)) / 2;
    int offsetY = (instance->matrix->height() - (canvasH * scaleY)) / 2;

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
                    if (instance->canvasBuffer) {
                        if (px >= 0 && px < instance->matrix->width() && baseY >= 0 && baseY < instance->matrix->height())
                            instance->canvasBuffer[baseY * instance->matrix->width() + px] = usPalette[c];
                    } else {
                        instance->matrix->drawPixel(px, baseY, usPalette[c]);
                    }
                } else {
                    if (instance->canvasBuffer) {
                        int mw = instance->matrix->width();
                        int mh = instance->matrix->height();
                        for (int sy = 0; sy < scaleY; sy++) {
                            for (int sx = 0; sx < scaleX; sx++) {
                                if (px+sx >= 0 && px+sx < mw && baseY+sy >= 0 && baseY+sy < mh)
                                    instance->canvasBuffer[(baseY + sy) * mw + (px + sx)] = usPalette[c];
                            }
                        }
                    } else {
                        instance->matrix->fillRect(px, baseY, scaleX, scaleY, usPalette[c]);
                    }
                }
            }
        }
    } else {
        for (x = 0; x < iWidth; x++) {
            uint16_t color = usPalette[*s++];
            int px = offsetX + (pDraw->iX + x) * scaleX;
            if (scaleX == 1 && scaleY == 1) {
                if (instance->canvasBuffer) {
                    if (px >= 0 && px < instance->matrix->width() && baseY >= 0 && baseY < instance->matrix->height())
                        instance->canvasBuffer[baseY * instance->matrix->width() + px] = color;
                } else {
                    instance->matrix->drawPixel(px, baseY, color);
                }
            } else {
                if (instance->canvasBuffer) {
                    int mw = instance->matrix->width();
                    int mh = instance->matrix->height();
                    for (int sy = 0; sy < scaleY; sy++) {
                        for (int sx = 0; sx < scaleX; sx++) {
                            if (px+sx >= 0 && px+sx < mw && baseY+sy >= 0 && baseY+sy < mh)
                                instance->canvasBuffer[(baseY + sy) * mw + (px + sx)] = color;
                        }
                    }
                } else {
                    instance->matrix->fillRect(px, baseY, scaleX, scaleY, color);
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
    if (!instance || !instance->matrix || !instance->png) return 0;

    int canvasW = instance->png->getWidth();
    int canvasH = instance->png->getHeight();
    if (canvasW <= 0) canvasW = 128;
    if (canvasH <= 0) canvasH = 32;

    int scaleX = instance->matrix->width() / canvasW;
    int scaleY = instance->matrix->height() / canvasH;
    int scale = min(scaleX, scaleY);
    if (scale < 1) scale = 1;
    scaleX = scale;
    scaleY = scale;

    int offsetX = (instance->matrix->width() - (canvasW * scaleX)) / 2;
    int offsetY = (instance->matrix->height() - (canvasH * scaleY)) / 2;

    static uint16_t lineBuffer[512]; // Increased to 512 for safety
    int iWidth = pDraw->iWidth;
    if (iWidth > 512) iWidth = 512;

    instance->png->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

    int y = pDraw->y;
    int baseY = offsetY + y * scaleY;

    for (int x = 0; x < iWidth; x++) {
        uint16_t color = lineBuffer[x];
        // Don't draw absolute black as transparent if we don't want to, but for PNG usually we respect alpha.
        // The PNG library blends to a background if we set it, or returns true RGB565.
        // Assuming we just draw it:
        int px = offsetX + x * scaleX;
        if (scaleX == 1 && scaleY == 1) {
            instance->matrix->drawPixel(px, baseY, color);
        } else {
            instance->matrix->fillRect(px, baseY, scaleX, scaleY, color);
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

