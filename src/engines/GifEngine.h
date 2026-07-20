/**
 * @file GifEngine.h
 * @brief Handles playback of Animated GIFs, raw pixel sequences, and static PNG images.
 * 
 * Uses the AnimatedGIF library to decode and render GIF files, and PNGdec (same author,
 * bitbank2, near-identical open/read/seek/draw callback API) for static .png assets, directly
 * from the SD card onto the I2S DMA Matrix. Supports playlists and randomized playback.
 */
#pragma once
#include <Arduino.h>
#include <AnimatedGIF.h>
#include <PNGdec.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <FS.h>
#include <SD.h>
#include <vector>

/**
 * @class GifEngine
 * @brief Orchestrates GIF decoding, file streaming, and matrix rendering.
 */
class GifEngine {
public:
    GifEngine();
    ~GifEngine();

    /**
     * @brief Initialize the engine with the matrix display pointer.
     */
    bool begin(MatrixPanel_I2S_DMA* display);
    
    /**
     * @brief Play a single GIF file repeatedly.
     * @param filepath Path on the SD card (e.g. "/gifs/mario.gif").
     */
    bool playGif(const char* filepath);
    
    /**
     * @brief Queue and play a list of playlists containing multiple GIFs.
     * @param playlistPaths Vector of SD paths to folder/playlist directories.
     */
    void playPlaylists(std::vector<String> playlistPaths);
    
    /**
     * @brief Set default playlists to fall back to when idle.
     */
    void setDefaultPlaylists(std::vector<String> playlistPaths);
    
    bool hasDefaultPlaylists() const { return !defaultPlaylists.empty(); }
    
    /**
     * @brief Play a specific number of GIFs from the default playlists.
     */
    void playDefaultPlaylists(int numGifs);
    
    /**
     * @brief Stop playback immediately and close files.
     */
    void stop();
    
    /**
     * @brief Main processing loop for pushing pixels frame-by-frame.
     */
    void loop();
    
    /**
     * @brief Check if the engine is currently processing or playing a GIF.
     */
    bool isActive() const { return isPlaying || playlistMode || hasPendingPlaylists; }

private:
    AnimatedGIF gif;                 ///< The AnimatedGIF decoder instance
    PNG png;                         ///< The PNGdec decoder instance, for static .png assets
    MatrixPanel_I2S_DMA* matrix;     ///< Matrix hardware reference
    bool isPlaying;                  ///< State flag for active playback
    bool playlistMode;               ///< State flag for playlist rotation
    
    // Concurrency queue
    std::vector<String> pendingPlaylists;
    bool hasPendingPlaylists = false;
    
    // Playlist state
    std::vector<String> playlists;
    std::vector<String> defaultPlaylists;
    std::vector<String> activeFiles;
    
    File currentFile;                ///< Handle to the currently streaming file (GIF/raw)
    File pngFile;                    ///< Separate handle for PNGdec's callbacks (synchronous decode)
    bool isRaw;                      ///< Flag indicating if file is .raw instead of .gif
    bool isPng;                      ///< Flag indicating if file is a static .png image
    unsigned long rawLastFrameTime;
    unsigned long pngShowStartTime;   ///< millis() when the current PNG was decoded/shown
    // A static PNG has no natural "end of animation" signal like GIF/raw sequences do, so it's
    // held on screen for this long before advancing the playlist (or looping, for a single play).
    static const unsigned long pngHoldDurationMs = 5000;
    
    int remainingGifsToPlay;         ///< Counter for rotation limits
    
    void loadNextFileInPlaylist();
    void playRawFrame();
    bool decodePng(const char* filepath);

    // Static instance pointer for C-style callbacks in AnimatedGIF
    static GifEngine* instance;

    // Callbacks for AnimatedGIF library to read from SD
    static void* GIFOpenFile(const char *fname, int32_t *pSize);
    static void GIFCloseFile(void *pHandle);
    static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen);
    static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition);
    static void GIFDraw(GIFDRAW *pDraw);

    // Callbacks for PNGdec to read from SD (separate File handle from GIF's, but same pattern)
    static void* PNGOpenFile(const char *fname, int32_t *pSize);
    static void PNGCloseFile(void *pHandle);
    static int32_t PNGReadFile(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen);
    static int32_t PNGSeekFile(PNGFILE *pFile, int32_t iPosition);
    static int PNGDrawCallback(PNGDRAW *pDraw);
};
