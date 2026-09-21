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
#ifdef INTELSHORT
#undef INTELSHORT
#endif
#ifdef INTELLONG
#undef INTELLONG
#endif
#include <PNGdec.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#ifdef FILE_READ
#undef FILE_READ
#endif
#ifdef FILE_WRITE
#undef FILE_WRITE
#endif
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
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;
    bool isFinished() const override;
    bool isRealtime() const override { return true; }
    bool selfPaced() const override { return true; }
    void setRotationBudget(uint32_t budget) override { m_rotationBudget = budget; }
    bool hasNewFrame() const override { return m_lastFrameDrew; }
    bool needsClear() const override { return false; }
    uint32_t nextFrameDueInMs() const override;
    void resume() override { invalidateShadows(); }

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

    /**
     * @brief Determine whether the current active matrix layout is vertical (Portrait / Tate).
     */
    bool isDisplayVertical() const;

    void setFitMode(const String& mode) { m_fitMode = mode; if (m_srcW > 0) updateFitGeometry(m_srcW, m_srcH); }
    const String& getFitMode() const { return m_fitMode; }
    void setSpeedMultiplier(float speed) { m_speedMultiplier = (speed > 0.05f) ? speed : 1.0f; }
    float getSpeedMultiplier() const { return m_speedMultiplier; }

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
    /**
     * File count per entry of `playlists`, read from each root's playlists.json, so the next folder
     * is chosen in proportion to its size: a file in a 1,400-file folder then plays as often as one
     * in a 30-file folder, which is what the RPi build gets by pooling every file before choosing.
     * Picking the folder uniformly first made small folders repeat dozens of times more often.
     */
    // A folder is chosen in proportion to its size; within it, a file that has come up recently is
    // passed over, so a long evening walks the library instead of circling a handful of files.
    static constexpr uint16_t RECENT_MAX = 4096;   ///< Files remembered (PSRAM ring, 16 KB)
    uint32_t* recentHashes = nullptr;
    uint16_t recentCap = 0, recentCount = 0, recentHead = 0;
    static uint32_t pathHash(const char* s);
    bool playedRecently(uint32_t h, size_t folderFiles);
    void rememberPlayed(uint32_t h);
    std::vector<uint32_t> playlistWeights;
    uint32_t playlistWeightsLoadedMs = 0;
    void refreshPlaylistWeights();
    int pickPlaylistIndex();
    std::vector<String> defaultPlaylists;
    std::vector<String> m_configuredFolders;
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
    EngineContext* m_context = nullptr;
    String resolveDefaultFolder() const;
    void rebuildActivePlaylists();
    
    void loadNextFileInPlaylist();
    bool playRawFrame();
    bool decodePng(const char* filepath);
    
    // PSRAM caching
    uint16_t* canvasBuffer = nullptr;
    uint8_t* psramBuffer = nullptr;
    size_t psramBufferSize = 0;
    void freePsramBuffer();

    /**
     * Dirty-pixel presentation. Pushing a whole 256x64 frame through drawPixel costs 8 PSRAM
     * read-modify-writes plus 8 cache write-backs per pixel (the HUB75 DMA buffer lives in PSRAM),
     * about 100 ms per frame, which is what held GIF playback to ~7 fps. We keep one shadow copy of
     * what was last drawn into each DMA buffer and write only the pixels that differ; a typical
     * animation changes a small fraction of the panel per frame. The shadows are invalidated whenever
     * something else may have drawn into the buffers (activation, resume, overlays, notices,
     * transitions: see MatrixEngine::markExternalDraw), which falls back to a full repaint.
     */
    uint16_t* m_shadow[2] = { nullptr, nullptr };
    bool m_shadowValid[2] = { false, false };
    uint32_t m_shadowGeneration = 0;
    void allocateShadows(size_t matrixPixels);
    void freeShadows();
    void invalidateShadows() { m_shadowValid[0] = m_shadowValid[1] = false; }
    bool blitCanvas();

    /// Placement of the current file on the panel, computed once per file/fit-mode/geometry change
    /// instead of on every scanline.
    int m_srcW = 0, m_srcH = 0;
    int m_fitScaleX = 1, m_fitScaleY = 1, m_fitOffX = 0, m_fitOffY = 0;
    void updateFitGeometry(int srcW, int srcH);

    /**
     * @brief Normalize a user/config-provided playlist path to a full SD path under /gifs or
     * /sprites. Handles the exact-match case ("/gifs" or "/sprites" with no trailing content),
     * which a naive startsWith("/gifs/") check would otherwise miss (and incorrectly prefix
     * again into "/gifs/gifs").
     */
    static String sanitizePlaylistPath(String p);

    /**
     * @brief Extract the sub-folder name of a sanitized playlist path, stripped of its orientation
     * root ("/gifs/" or "/gifs_tate/"). Returns an empty String for orientation roots themselves or
     * for paths belonging to another asset family (e.g. "/sprites/..."). This is what allows a
     * playlist configured in one orientation to be resolved symmetrically in the other one.
     */
    static String extractPlaylistLeaf(const String& cleanPath);
    static void expandPlaylists(const std::vector<String>& inputPaths, std::vector<String>& outPaths);

    /**
     * @brief Allocate the scanline canvas in internal SRAM (mandatory: it is accessed pixel-by-pixel
     * on the Core 1 hot path), falling back to PSRAM only if internal allocation fails.
     */
    uint16_t* allocateCanvasBuffer(size_t matrixPixels);

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
    bool m_lastFrameDrew = true;
    String m_fitMode = "fit";
    float m_speedMultiplier = 1.0f;
};

class GifEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};

