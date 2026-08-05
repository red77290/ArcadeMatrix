#include "GifEngine.h"
#include <ArduinoJson.h>
#include "../core/SDUtils.h"

GifEngine* GifEngine::instance = nullptr;

GifEngine::GifEngine() : matrix(nullptr), isPlaying(false), playlistMode(false), isRaw(false), isPng(false), rawLastFrameTime(0), pngShowStartTime(0), psramBuffer(nullptr), psramBufferSize(0) {
    instance = this;
}

GifEngine::~GifEngine() {
    stop();
    delete png;
}

bool GifEngine::begin(MatrixPanel_I2S_DMA* display) {
    if (!display) return false;
    matrix = display;
    gif.begin(LITTLE_ENDIAN_PIXELS);
    return true;
}

bool GifEngine::playGif(const char* filepath) {
    stop();
    playlistMode = false; // Playing a single GIF stops the playlist
    
    String path = String(filepath);
    Serial.print("GifEngine trying to play: ");
    Serial.println(path);
    
    if (path.endsWith(".raw") || path.endsWith(".RAW")) {
        currentFile = sd.open(path.c_str(), FILE_OPEN_READ);
        if (!currentFile) {
            Serial.println("Error: Failed to open RAW file!");
            return false;
        }
        isRaw = true;
        isPng = false;
        isPlaying = true;
        rawLastFrameTime = 0;
        return true;
    } else if (path.endsWith(".png") || path.endsWith(".PNG")) {
        if (decodePng(filepath)) {
            isRaw = false;
            isPng = true;
            isPlaying = true;
            pngShowStartTime = millis();
            return true;
        }
        return false;
    } else {
#if defined(BOARD_HAS_PSRAM)
        if (psramFound()) {
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
                                Serial.println("GIF loaded directly from PSRAM!");
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
#endif
        // Fallback to streaming from SD card
        if (gif.open(filepath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
            Serial.println("GIF opened successfully (streaming from SD)!");
            isRaw = false;
            isPng = false;
            isPlaying = true;
            return true;
        } else {
            Serial.println("Error: gif.open() failed for GIF file!");
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
        Serial.printf("Error: png.open() failed for %s (rc=%d)\n", filepath, rc);
        return false;
    }
    rc = png->decode(NULL, 0);
    png->close();
    if (rc != PNG_SUCCESS) {
        Serial.printf("Error: png.decode() failed for %s (rc=%d)\n", filepath, rc);
        return false;
    }
    return true;
}

void GifEngine::playPlaylists(std::vector<String> playlistPaths) {
    if (playlistPaths.empty()) return;
    
    // Sanitize all paths immediately
    std::vector<String> sanitized;
    for (String p : playlistPaths) {
        sanitized.push_back(sanitizePlaylistPath(p));
    }
    
    pendingPlaylists = sanitized;
    hasPendingPlaylists = true;
    remainingGifsToPlay = -1;
}

void GifEngine::setDefaultPlaylists(std::vector<String> playlistPaths) {
    std::vector<String> sanitized;
    for (String p : playlistPaths) {
        sanitized.push_back(sanitizePlaylistPath(p));
    }
    defaultPlaylists = sanitized;
}

String GifEngine::sanitizePlaylistPath(String p) {
    if (!p.startsWith("/")) p = "/" + p;
    // Exact match ("/gifs" or "/sprites", no trailing slash) must NOT be re-prefixed - only
    // startsWith("/gifs/")/("/sprites/") checks the sub-path case. Without this exact-match
    // check, the common default playlist value "/gifs" would incorrectly become "/gifs/gifs"
    // (a real bug seen in production: every open() on the resulting bad path silently fails).
    if (p == "/gifs" || p == "/sprites") return p;
    if (!p.startsWith("/gifs/") && !p.startsWith("/sprites/")) p = "/gifs" + p;
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
        FsFile indexFile = sd.open(indexPath, FILE_OPEN_READ);
        
        String targetPath = "";
        if (indexFile && indexFile.size() > 0) {
            std::vector<String> validFiles;
            while (indexFile.available()) {
                String line = indexFile.readStringUntil('\n');
                line.trim();
                if (line.length() > 0 && !isMacJunk(line)) {
                    if (line.indexOf("._") == -1 && line.indexOf("System Volume") == -1) {
                        validFiles.push_back(line);
                    }
                }
            }
            indexFile.close();
            
            if (!validFiles.empty()) {
                int selectedIdx = random(validFiles.size());
                String candidate = pPath + "/" + validFiles[selectedIdx];
                // Prevent playing the exact same GIF twice in a row if playlist has multiple files
                if (validFiles.size() > 1 && candidate == lastPlayedGif) {
                    selectedIdx = (selectedIdx + 1 + random(validFiles.size() - 1)) % validFiles.size();
                    candidate = pPath + "/" + validFiles[selectedIdx];
                }
                targetPath = candidate;
            }
        }
        
        if (targetPath.length() == 0) {
            Serial.printf("GifEngine: No index.txt found in %s, please run generate_index.sh\n", pPath.c_str());
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
    freePsramBuffer();
}

void GifEngine::loop() {
    if (hasPendingPlaylists) {
        stop();
        playlists = pendingPlaylists;
        playlistMode = true;
        hasPendingPlaylists = false;
        loadNextFileInPlaylist();
        return;
    }

    if (!isPlaying) {
        if (playlistMode) loadNextFileInPlaylist();
        return;
    }

    if (isRaw) {
        playRawFrame();
    } else if (isPng) {
        // Static image: already decoded directly onto the matrix by decodePng(). Nothing to
        // redraw every tick - just wait out pngHoldDurationMs, then advance/loop like a
        // single-frame raw sequence would.
        if (millis() - pngShowStartTime >= pngHoldDurationMs) {
            if (playlistMode) {
                loadNextFileInPlaylist();
            } else {
                pngShowStartTime = millis(); // Loop: just keep showing the same static image
            }
        }
    } else {
        if (millis() - gifLastFrameTime < gifCurrentDelay) return;
        gifLastFrameTime = millis();
        
        int delayMs = 0;
        int result = gif.playFrame(false, &delayMs);
        
        if (canvasBuffer && matrix) {
            matrix->drawRGBBitmap(0, 0, canvasBuffer, matrix->width(), matrix->height());
        }
        
        // Browser-like GIF delay normalization for badly encoded GIFs
        if (delayMs <= 10) {
            delayMs = 100; // Force 100ms for 0/10ms delays (like Chrome/Firefox)
        } else if (delayMs < 20) {
            delayMs = 20; // Cap at 50fps max to prevent matrix stuttering
        }
        
        gifCurrentDelay = delayMs;
        
        if (result <= 0) {
            // End of GIF or error
            if (playlistMode) {
                loadNextFileInPlaylist();
            } else {
                gif.reset(); // Loop single file
            }
        }
    }
}

void GifEngine::playRawFrame() {
    if (millis() - rawLastFrameTime < 50) return; // ~20 FPS limit for raw files
    rawLastFrameTime = millis();
    
    if (!currentFile) return;
    
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
    if (scaleX < 1) scaleX = 1;
    if (scaleY < 1) scaleY = 1;
    
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
    if (scaleX < 1) scaleX = 1;
    if (scaleY < 1) scaleY = 1;

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
