#include "GifEngine.h"
#include <ArduinoJson.h>
#include "../core/SDUtils.h"

GifEngine* GifEngine::instance = nullptr;

GifEngine::GifEngine() : matrix(nullptr), isPlaying(false), playlistMode(false), isRaw(false), isPng(false), rawLastFrameTime(0), pngShowStartTime(0) {
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
        currentFile = sd.open(path.c_str(), O_READ);
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
        if (gif.open(filepath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
            Serial.println("GIF opened successfully!");
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
        FsFile indexFile = sd.open(indexPath, O_READ);
        
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
        int result = gif.playFrame(true, nullptr);
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
    instance->currentFile = sd.open(fname, O_READ);
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
    
    uint8_t *s;
    uint16_t *usPalette;
    int x, y, iWidth;

    iWidth = pDraw->iWidth;
    if (iWidth > instance->matrix->width()) iWidth = instance->matrix->width();

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y;
    
    if (y >= instance->matrix->height() || pDraw->iX >= instance->matrix->width()) return;

    s = pDraw->pPixels;
    
    if (pDraw->ucHasTransparency) {
        uint8_t c, ucTransparent = pDraw->ucTransparent;
        int xOffset = pDraw->iX;
        for (x = 0; x < iWidth; x++) {
            c = *s++;
            if (c != ucTransparent) {
                instance->matrix->drawPixel(xOffset + x, y, usPalette[c]);
            }
        }
    } else {
        int xOffset = pDraw->iX;
        for (x = 0; x < iWidth; x++) {
            instance->matrix->drawPixel(xOffset + x, y, usPalette[*s++]);
        }
    }
}

// --- PNGdec callbacks (static .png assets, mirrors GIF callbacks above) ---

void* GifEngine::PNGOpenFile(const char *fname, int32_t *pSize) {
    if (instance) {
        instance->pngFile = sd.open(fname, O_READ);
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

    // 256px covers the widest supported panel (ESP32-S3 @ 256x64); getLineAsRGB565() writes
    // exactly pDraw->iWidth pixels so this is a safe upper bound for either target.
    static uint16_t lineBuffer[256];
    int iWidth = pDraw->iWidth;
    if (iWidth > 256) iWidth = 256;

    instance->png->getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);

    int y = pDraw->y;
    if (y >= instance->matrix->height()) return 1;

    for (int x = 0; x < iWidth && x < instance->matrix->width(); x++) {
        instance->matrix->drawPixel(x, y, lineBuffer[x]);
    }
    return 1;
}
