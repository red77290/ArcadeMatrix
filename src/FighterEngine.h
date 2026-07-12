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
#include "FS.h"
#include "SD.h"

/**
 * @enum FighterState
 * @brief Defines the current behavioral state of a fighter on screen.
 */
enum FighterState {
    FIGHTER_WALK,   ///< Walking towards the center or opponent
    FIGHTER_ATTACK, ///< Performing an attack animation
    FIGHTER_HIT,    ///< Taking damage from an attack
    FIGHTER_WIN     ///< Celebration animation upon victory
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
};

/**
 * @struct FighterPlayer
 * @brief Represents an active combatant on the screen.
 */
struct FighterPlayer {
    String name;                ///< Name of the character directory
    FgtAnimation animWalk;      ///< Walking animation data
    FgtAnimation animAttack;    ///< Attack animation data
    FgtAnimation animHit;       ///< Hit animation data
    FgtAnimation animWin;       ///< Win animation data
    FighterState state;         ///< Current state in the state machine
    
    File activeFile;            ///< File handle currently open for streaming pixels
    
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
class FighterEngine {
public:
    /**
     * @brief Construct a new Fighter Engine object.
     * @param display Pointer to the DMA Matrix Engine.
     */
    FighterEngine(MatrixPanel_I2S_DMA* display);
    ~FighterEngine();

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
    void loop();
    
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
    
    int numAvailableFighters = 0;   ///< Number of total indexed fighters on SD
    uint32_t* fighterOffsets = nullptr; ///< File offsets for the fighter index
    uint32_t retryDelayEnd = 0;     ///< Delay timer for retry logic
    uint32_t lastMoveTime = 0;      ///< Timer for horizontal movement pacing
    
    String getFightersDir();
    String getRandomFighterName();
    
    void loadRoster();
    bool loadFighterAnim(FgtAnimation& anim, const char* filepath);
    void freeFighter(FighterPlayer& p);
    void freeAnim(FgtAnimation& anim);
    
    void setPlayerState(FighterPlayer& p, FighterState newState);
    void drawPlayer(FighterPlayer& p);
    
    uint32_t fightStartTime;        ///< Timestamp when the fight began
    uint32_t fightEndTime = 0;      ///< Timestamp when the fight concludes
    
    /**
     * @brief State machine for asynchronous loading from SD to avoid blocking.
     */
    enum LoadState {
        LOAD_IDLE,
        LOAD_INIT,
        LOAD_P1_WALK, LOAD_P1_ATTACK, LOAD_P1_HIT, LOAD_P1_WIN,
        LOAD_P2_WALK, LOAD_P2_ATTACK, LOAD_P2_HIT, LOAD_P2_WIN,
        LOAD_FINISH
    };
    LoadState currentLoadState = LOAD_IDLE;
    String loadDir;
    void processLoadState();
};

#endif

