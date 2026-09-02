/**
 * @file FighterEngine.h
 * @brief Manages M.U.G.E.N fighter animations and automated combat logic.
 * 
 * Handles loading raw extracted animations from SD card, tracking combat states,
 * and drawing the animated sprites onto the matrix.
 */
#ifndef FIGHTER_ENGINE_H
#define FIGHTER_ENGINE_H

#include <Arduino.h>
#include <vector>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../core/SDUtils.h"
#include "FS.h"

/**
 * @enum FighterState
 * @brief Defines the current behavioral state of a fighter on screen.
 */
enum FighterState {
    FIGHTER_WALK,   ///< Walking towards the center or opponent
    FIGHTER_STAND,  ///< Standing in idle ready stance waiting for opponent
    FIGHTER_ATTACK, ///< Performing an attack animation
    FIGHTER_HIT,    ///< Taking damage from an attack
    FIGHTER_WIN,    ///< Celebration animation upon victory
    FIGHTER_SPECIAL,///< Performing a special attack
    FIGHTER_SUPER,  ///< Performing a super attack
    FIGHTER_FALL    ///< Hard knockdown reaction
};

/**
 * @struct FgtAnimation
 * @brief Holds metadata and frame data for a specific animation sequence.
 */
struct FgtAnimation {
    String filepath;          ///< SD Card path to the raw animation file
    bool loaded = false;      ///< True if metadata has been parsed successfully
    uint16_t width;           ///< Width of the sprite
    uint16_t height;          ///< Height of the sprite
    uint16_t numFrames;       ///< Total number of frames in this animation
    uint16_t transparentColor;///< RGB565 color to treat as transparent
    uint16_t* frameDelays = nullptr; ///< Array of millisecond delays per frame
    uint32_t pixelsOffset = 0;///< Byte offset in the file where pixel data begins
    
    int cachedFrameIndex = -1;///< Used for frame caching optimization
    
    uint8_t* psramBuffer = nullptr; ///< Complete animation cached in PSRAM (if available)
    uint32_t totalPixelsSize = 0;   ///< Total size of pixel data in bytes
};

/**
 * @struct FighterPlayer
 * @brief Represents an active combatant on the screen.
 */
struct FighterPlayer {
    String name;                ///< Name of the character directory
    int height;                 ///< Native height of the character
    int ground_y;               ///< Y position of the ground (origin)
    int head_y;                 ///< Y position of the head in the stand animation
    int origin_x;               ///< X position of the origin
    int width_px;               ///< Native width of the character
    FgtAnimation animStand;     ///< Stand / Idle stance animation data
    FgtAnimation animWalk;      ///< Walking animation data
    FgtAnimation animAttack;    ///< Attack animation data
    FgtAnimation animHit;       ///< Hit animation data
    FgtAnimation animWin;       ///< Win animation data
    FgtAnimation animSpecial;   ///< Special attack animation data
    FgtAnimation animSuper;     ///< Super attack animation data
    FgtAnimation animFall;      ///< Fall animation data
    FighterState state;         ///< Current state in the state machine
    
    FsFile activeFile;          ///< File handle currently open for streaming pixels
    
    uint8_t* currentFrameBuffer = nullptr; ///< RAM buffer for the current frame
    int currentBufferSize = 0;             ///< Size of the allocated frame buffer
    
    int x;                      ///< X coordinate position on the matrix
    int y;                      ///< Y coordinate position on the matrix
    int direction;              ///< 1 for moving right, -1 for moving left
    
    int currentFrame;           ///< Index of the currently displayed frame
    uint32_t lastFrameTime;     ///< Millis timestamp of the last frame update
    
    bool hasHit;                ///< True if this player successfully landed a hit
    bool isDead;                ///< True if this player lost the match
};

/**
 * @class FighterEngine
 * @brief The orchestrator for the automated combat display.
 * 
 * Manages loading fighters, pacing the fight sequence, and rendering pixels
 * to the display while managing memory efficiently.
 */
#include "../../include/core/EngineContract.h"
#include "../core/AppEngineContext.h"

class FighterEngine : public IEngine {
public:
    FighterEngine();
    ~FighterEngine();

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

    /**
     * @brief Initialize the engine (e.g. read the SD card index).
     */
    void initialize();
    
    /**
     * @brief Start a new fight between two random characters.
     */
    void startFight();
    
    /**
     * @brief Stop the current fight and free memory.
     */
    void stop();
    
    /**
     * @brief Main loop to be called frequently to update animation logic.
     */
    bool loop();
    
    /**
     * @brief Draw the current frame of both fighters to the matrix.
     */
    void draw();
    
    /**
     * @brief Check if a fight is currently ongoing.
     * @return true if combat is active.
     */
    bool isActive() const { return active; }

private:

    
    MatrixPanel_I2S_DMA* matrix; ///< DMA Matrix instance
    bool active = false;         ///< Is the engine currently active?
    
    FighterPlayer p1;            ///< Player 1 (Left)
    FighterPlayer p2;            ///< Player 2 (Right)

    // Background Preloader (Core 0 FreeRTOS task)
    FighterPlayer nextP1;
    FighterPlayer nextP2;
    volatile bool isNextReady = false;
    volatile bool isPreloading = false;
    TaskHandle_t loaderTaskHandle = nullptr;

    static void loaderTaskFunc(void* param);
    void runBackgroundPreload();
    void triggerBackgroundPreload();

    int numAvailableFighters = 0;   ///< Number of total indexed fighters on SD
    uint32_t* fighterOffsets = nullptr; ///< File offsets for the fighter index
    uint32_t retryDelayEnd = 0;     ///< Delay timer for retry logic
    uint32_t lastMoveTime = 0;      ///< Timer for horizontal movement pacing
    
    String getFightersDir();
    bool getRandomFighter(FighterPlayer& p);
    
    void loadRoster();
    bool loadFighterAnim(FgtAnimation& anim, const char* filepath);
    void freeFighter(FighterPlayer& p);
    void freeAnim(FgtAnimation& anim);
    
    void setPlayerState(FighterPlayer& p, FighterState newState);
    void drawPlayer(FighterPlayer& p, int offsetY = 0);
    
    uint32_t fightStartTime;        ///< Timestamp when the fight began
    uint32_t fightEndTime = 0;      ///< Timestamp when the fight concludes
    uint32_t faceoffStartTime = 0;  ///< Timestamp when both fighters reached the face-off position
    
    uint32_t hitStopUntilMillis = 0; ///< Hit-stop pause end time
    int shakeRemainingFrames = 0;    ///< Number of frames left for screen shake
    
    String loadDir;
    String cachedFightersDir;
    int cachedScaleClass = 0;
    bool m_hasPsram = false;
};

#endif

