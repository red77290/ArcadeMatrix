#include "GifEngine.h"
#include <ArduinoJson.h>
#include "../core/SDUtils.h"

GifEngine* GifEngine::instance = nullptr;

GifEngine::GifEngine() : matrix(nullptr), isPlaying(false), playlistMode(false), isRaw(false), rawLastFrameTime(0) {
    instance = this;
}

GifEngine::~GifEngine() {
    stop();
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
        currentFile = SD.open(filepath);
        if (!currentFile) {
            Serial.println("Error: Failed to open RAW file!");
            return false;
        }
        isRaw = true;
        isPlaying = true;
        rawLastFrameTime = 0;
        return true;
    } else {
        if (gif.open(filepath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
            Serial.println("GIF opened successfully!");
            isRaw = false;
            isPlaying = true;
            return true;
        } else {
            Serial.println("Error: gif.open() failed for GIF file!");
        }
    }
    return false;
}

void GifEngine::playPlaylists(std::vector<String> playlistPaths) {
    if (playlistPaths.empty()) return;
    
    // Sanitize all paths immediately
    std::vector<String> sanitized;
    for (String p : playlistPaths) {
        if (!p.startsWith("/")) p = "/" + p;
        if (!p.startsWith("/gifs/") && !p.startsWith("/sprites/")) p = "/gifs" + p;
        sanitized.push_back(p);
    }
    
    pendingPlaylists = sanitized;
    hasPendingPlaylists = true;
    remainingGifsToPlay = -1;
}

void GifEngine::setDefaultPlaylists(std::vector<String> playlistPaths) {
    std::vector<String> sanitized;
    for (String p : playlistPaths) {
        if (!p.startsWith("/")) p = "/" + p;
        if (!p.startsWith("/gifs/") && !p.startsWith("/sprites/")) p = "/gifs" + p;
        sanitized.push_back(p);
    }
    defaultPlaylists = sanitized;
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
        File indexFile = SD.open(indexPath);
        
        String targetPath = "";
        if (indexFile && indexFile.size() > 0) {
            long fileSize = indexFile.size();
            long randomOffset = random(fileSize);
            indexFile.seek(randomOffset);
            
            if (randomOffset != 0) {
                indexFile.readStringUntil('\n');
            }
            
            targetPath = indexFile.readStringUntil('\n');
            targetPath.trim();
            // Skip macOS junk entries
            while (isMacJunk(targetPath) && indexFile.available()) {
                targetPath = indexFile.readStringUntil('\n');
                targetPath.trim();
            }
            
            if (targetPath.length() == 0 || isMacJunk(targetPath)) {
                indexFile.seek(0);
                targetPath = indexFile.readStringUntil('\n');
                targetPath.trim();
            }
            indexFile.close();
            
            if (targetPath.length() > 0) {
                targetPath = pPath + "/" + targetPath;
            }
        }
        
        if (targetPath.length() == 0) {
            File dir = SD.open(pPath);
            if (dir && dir.isDirectory()) {
                int count = 0;
                File f = dir.openNextFile();
                while (f) {
                    if (!f.isDirectory() && !isMacJunk(String(f.name())) &&
                        (String(f.name()).endsWith(".gif") || String(f.name()).endsWith(".GIF") ||
                         String(f.name()).endsWith(".raw") || String(f.name()).endsWith(".RAW"))) {
                        String fname = String(f.name());
                        if (fname.indexOf("._") == -1) count++;
                    }
                    f.close();
                    f = dir.openNextFile();
                }
                dir.close();
                
                if (count > 0) {
                    int target = random(count);
                    int current = 0;
                    dir = SD.open(pPath);
                    f = dir.openNextFile();
                    while (f) {
                        if (!f.isDirectory() && !isMacJunk(String(f.name())) &&
                            (String(f.name()).endsWith(".gif") || String(f.name()).endsWith(".GIF") ||
                             String(f.name()).endsWith(".raw") || String(f.name()).endsWith(".RAW"))) {
                            String fname = String(f.name());
                            if (true) {
                                if (current == target) {
                                    targetPath = fname.startsWith("/") ? fname : pPath + "/" + fname;
                                    f.close();
                                    break;
                                }
                                current++;
                            }
                        }
                        f.close();
                        f = dir.openNextFile();
                    }
                    dir.close();
                }
            }
        }
        
        if (targetPath.length() > 0) {
            targetPath.replace("//", "/");
            if (targetPath.indexOf("._") == -1 && targetPath.indexOf("System Volume") == -1) {
                if (playGif(targetPath.c_str())) {
                    playlistMode = true;
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
        if (!isRaw) gif.close();
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
    instance->currentFile = SD.open(fname);
    if (instance->currentFile) {
        *pSize = instance->currentFile.size();
        return (void*)&instance->currentFile;
    }
    return nullptr;
}

void GifEngine::GIFCloseFile(void *pHandle) {
    File *f = static_cast<File *>(pHandle);
    if (f && *f) f->close();
}

int32_t GifEngine::GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    int32_t iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    if (!f || !*f) return 0;
    
    iBytesRead = f->read(pBuf, iLen);
    pFile->iPos = f->position();
    return iBytesRead;
}

int32_t GifEngine::GIFSeekFile(GIFFILE *pFile, int32_t iPosition) {
    File *f = static_cast<File *>(pFile->fHandle);
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
