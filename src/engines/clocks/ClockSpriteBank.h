#pragma once

#include <stdint.h>
#include "../../core/drawing/IDrawingSurface.h"

#if !defined(HARDWARE_PROFILE_ESP32_DEV)
#define ARCADEMATRIX_HAS_EXTENDED_CLOCKS 1
#else
#define ARCADEMATRIX_HAS_EXTENDED_CLOCKS 0
#endif

namespace ClockSpriteBank {

/**
 * @brief Lightweight zero-RAM, zero-Flash-duplication descriptor pointing to existing PROGMEM sprite arrays.
 */
struct SpriteFrameRef {
    const uint16_t* pixels565 = nullptr;
    const uint8_t* indexedPixels = nullptr;
    const uint16_t* palette = nullptr;
    uint8_t w = 0;
    uint8_t h = 0;
    uint16_t maskColor = 0x000E;

    constexpr bool isValid() const {
        return (w > 0 && h > 0) && (pixels565 != nullptr || (indexedPixels != nullptr && palette != nullptr));
    }
};

enum class CameoSprite : uint8_t {
    MARIO_IDLE = 0,
    MARIO_RUN,
    MARIO_JUMP,
    GOLD_COIN,
    KOOPA_SHELL,
    GROUND_TILE_MARIO,
    SONIC_SMS_RUN,
    SONIC_MD_RUN,
    SONIC_MD_BALL,
    SONIC_RING,
    MEGAMAN_RUN,
    MEGAMAN_SHOOT,
    MEGAMAN_JUMP_SHOOT,
    BUSTER_BULLET,
    HIT_SPARK,
    METOOL_SLEEP,
    METOOL_PEEK,
    GROUND_TILE_MEGAMAN,
    MARCO_VICTORY,
    PIKACHU
};

enum AgendaSpriteId : uint8_t {
    SPRITE_CALENDAR_DEFAULT = 0,
    SPRITE_BIRTHDAY_CAKE    = 1,
    SPRITE_CYBER_CALL       = 2,
    SPRITE_MEDICAL_HEART    = 3,
    SPRITE_FOOD_PIZZA       = 4,
    SPRITE_SPORT_SONIC      = 5,
    SPRITE_TRAVEL_PLANE     = 6,
    SPRITE_GAMING_PACMAN    = 7,
    SPRITE_METOOL_SLEEP     = 8,
    SPRITE_GOLD_COIN        = 9,
    SPRITE_SONIC_RING       = 10,
    SPRITE_TROPHY_CUP       = 11,
    SPRITE_CROWN_RECORD     = 12
};

enum class HeroFamily : uint8_t {
    Nintendo = 0,
    Sega     = 1,
    Capcom   = 2,
    NeoGeo   = 3,
    Arcade   = 4
};

/**
 * @brief Retrieves a zero-copy reference to a clock sprite frame in Flash ROM.
 */
SpriteFrameRef getCameoFrame(CameoSprite sprite, uint8_t frameIndex = 0);

/**
 * @brief Blits a SpriteFrameRef (RGB565 or 8-bit indexed palette) with transparent mask and panel clipping.
 */
void blitSprite(IDrawingSurface* surface, const SpriteFrameRef& ref, int16_t x, int16_t y, bool flipH = false);

/**
 * @brief Draws classic 13x13 Pac-Man (3 mouth frames) at (x, y).
 */
void drawPacman(IDrawingSurface* surface, int16_t x, int16_t y, uint8_t frameIndex, bool facingRight = true);

/**
 * @brief Draws 14x14 frightened blue arcade ghost at (x, y).
 */
void drawScaredGhost(IDrawingSurface* surface, int16_t x, int16_t y, uint8_t frameIndex);

/**
 * @brief Draws a contextual pixel-art icon (12x12 or 16x16) for AgendaEngine.
 */
void drawAgendaIcon(IDrawingSurface* surface, uint8_t spriteId, int16_t x, int16_t y, uint8_t size, uint32_t animTick);

/**
 * @brief Maps an emulated console system ID (e.g. "snes", "megadrive", "cps2", "neogeo", "mame")
 *        to its celebration hero family.
 */
HeroFamily classifySystemHero(const char* systemId);

/**
 * @brief Renders the animated celebration hero cameo along the bottom ground line at 60 FPS.
 */
void drawCelebrationHero(IDrawingSurface* surface, HeroFamily family, int16_t groundY,
                         uint16_t panelW, uint16_t panelH, uint16_t frameTick);

// Internal per-translation-unit hooks avoiding PROGMEM duplication across .cpp files
SpriteFrameRef marioClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex);
SpriteFrameRef sonicClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex);
SpriteFrameRef megamanClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex);
#if ARCADEMATRIX_HAS_EXTENDED_CLOCKS
SpriteFrameRef metalSlugClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex);
SpriteFrameRef pokedexClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex);
#endif

} // namespace ClockSpriteBank
