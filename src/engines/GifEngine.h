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
#include "core/SDUtils.h"
#include <vector>

/**
 * @class GifEngine
 * @brief Orchestrates GIF decoding, file streaming, and matrix rendering.
 */
#include "../../include/core/EngineContract.h"
#include "../core/AppEngineContext.h"

class GifEngine : public IEngine {
public:
    GifEngine();
    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    bool isFinished() const override;
    bool isRealtime() const override { return true; }
    bool selfPaced() const override { return true; }
    void setRotationBudget(uint32_t budget) override { m_rotationBudget = budget; }
    bool allowsOverlay() const override { return false; }

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
    bool loop();
    
    /**
     * @brief Check if the engine is currently processing or playing a GIF.
     */
    bool isActive() const { return isPlaying || playlistMode || hasPendingPlaylists; }

private:
    AnimatedGIF gif;                 ///< The AnimatedGIF decoder instance
    // The PNGdec PNGIMAGE struct embeds ~38KB of fixed-size buffers (32KB zlib window, palette,
    // pixel buffer, file buffer) directly as class members - NOT heap-allocated. Embedding a
    // `PNG png;` value member here would permanently reserve that ~38KB of static RAM for the
    // entire firmware lifetime, even on setups that never show a static PNG image (most
    // playlists are all-GIF). Allocated lazily on first actual PNG decode instead, so that RAM
    // stays available for the matrix DMA buffers / Wi-Fi / AsyncTCP / other engines unless this
    // specific feature is actually used. See docs/HARDWARE.md and the "AsyncTCP failed to start
    // task" troubleshooting entry in docs/GETTING_STARTED.md for why this matters on a
    // non-PSRAM classic ESP32 (only ~320KB total internal RAM).
    PNG* png = nullptr;              ///< The PNGdec decoder instance, lazily allocated on first PNG decode
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
    
    FsFile currentFile;              ///< Handle to the currently streaming file (GIF/raw)
    FsFile pngFile;                  ///< Separate handle for PNGdec's callbacks (synchronous decode)
    bool isRaw;                      ///< Flag indicating if file is a raw uncompressed frame
    bool isPng;                      ///< Flag indicating if file is a static .png image
    bool needsInitialFlip;           ///< Flag to force a single matrix flip when a static PNG is first loaded
    uint32_t rawLastFrameTime;
    uint32_t gifLastFrameTime = 0;
    int gifCurrentDelay = 0;
    uint32_t pngShowStartTime;   ///< millis() when the current PNG was decoded/shown
    // A static PNG has no natural "end of animation" signal like GIF/raw sequences do, so it's
    // held on screen for this long before advancing the playlist (or looping, for a single play).
    static const unsigned long pngHoldDurationMs = 5000;
    
    int remainingGifsToPlay;         ///< Counter for rotation limits
    String lastPlayedGif;            ///< Tracks last played GIF path to prevent consecutive duplicate playback
    uint32_t m_rotationBudget = 0;
    const EngineConfig* m_instanceConfig = nullptr;
    
    void loadNextFileInPlaylist();
    bool playRawFrame();
    bool decodePng(const char* filepath);
    
    // PSRAM caching
    uint16_t* canvasBuffer = nullptr;
    uint8_t* psramBuffer = nullptr;
    size_t psramBufferSize = 0;
    void freePsramBuffer();

    /**
     * @brief Normalize a user/config-provided playlist path to a full SD path under /gifs or
     * /sprites. Handles the exact-match case ("/gifs" or "/sprites" with no trailing content),
     * which a naive startsWith("/gifs/") check would otherwise miss (and incorrectly prefix
     * again into "/gifs/gifs").
     */
    static String sanitizePlaylistPath(String p);

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

    bool m_hasPsram = false;
};
