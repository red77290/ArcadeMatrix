#include "ClockSpriteBank.h"
#include "PacmanSprites.h"
#include <Arduino.h>
#include <string.h>
#include <strings.h>

#if defined(HARDWARE_PROFILE_ESP32_DEV)
#pragma GCC optimize("Os")
#endif

#if !defined(ESP32)
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(reinterpret_cast<const uint8_t*>(addr)))
#endif
#ifndef pgm_read_word
#define pgm_read_word(addr) (*(reinterpret_cast<const uint16_t*>(addr)))
#endif
#include "MarioAssets.h"
#include "SonicAssets.h"
#include "MegamanAssets.h"

namespace ClockSpriteBank {

SpriteFrameRef marioClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex) {
    SpriteFrameRef ref;
    ref.maskColor = _MASK;
    switch (sprite) {
        case CameoSprite::MARIO_IDLE:
            ref.pixels565 = MARIO_IDLE; ref.w = 13; ref.h = 16; break;
        case CameoSprite::MARIO_RUN: {
            uint8_t idx = frameIndex % 3;
            ref.pixels565 = MARIO_RUN[idx]; ref.w = MARIO_RUN_W[idx]; ref.h = 16; break;
        }
        case CameoSprite::MARIO_JUMP:
            ref.pixels565 = MARIO_JUMP; ref.w = 17; ref.h = 16; break;
        case CameoSprite::GOLD_COIN:
            ref.pixels565 = GOLD_COIN; ref.w = 8; ref.h = 8; break;
        case CameoSprite::KOOPA_SHELL:
            ref.pixels565 = KOOPA_SHELL; ref.w = KOOPA_SHELL_W; ref.h = KOOPA_SHELL_H; break;
        case CameoSprite::GROUND_TILE_MARIO:
            ref.pixels565 = GROUND; ref.w = 8; ref.h = 8; break;
        default: break;
    }
    return ref;
}

SpriteFrameRef sonicClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex) {
    using namespace SonicAssets;
    SpriteFrameRef ref;
    ref.maskColor = MASK;
    switch (sprite) {
        case CameoSprite::SONIC_SMS_RUN:
            ref.indexedPixels = SONIC_SMS_RUN_PIXELS[frameIndex & 3U];
            ref.palette = SONIC_SMS_IDLE_PAL; ref.w = 20; ref.h = 24; break;
        case CameoSprite::SONIC_MD_RUN:
            ref.indexedPixels = SONIC_MD_RUN_PIXELS[frameIndex & 3U];
            ref.palette = SONIC_MD_IDLE_PAL; ref.w = 32; ref.h = 36; break;
        case CameoSprite::SONIC_MD_BALL:
            ref.indexedPixels = SONIC_MD_BALL_PIXELS[frameIndex & 3U];
            ref.palette = SONIC_MD_IDLE_PAL; ref.w = 24; ref.h = 24; break;
        case CameoSprite::SONIC_RING:
            ref.indexedPixels = SONIC_RING_PIXELS[frameIndex & 3U];
            ref.palette = SONIC_RING_PALS[frameIndex & 3U]; ref.w = 16; ref.h = 16; break;
        default: break;
    }
    return ref;
}

SpriteFrameRef megamanClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex) {
    using namespace MegamanAssets;
    SpriteFrameRef ref;
    ref.maskColor = MASK;
    switch (sprite) {
        case CameoSprite::MEGAMAN_RUN:
            if ((frameIndex & 1U) == 0U) {
                ref.pixels565 = MEGAMAN_RUN1; ref.w = MEGAMAN_RUN1_W; ref.h = MEGAMAN_RUN1_H;
            } else {
                ref.pixels565 = MEGAMAN_RUN2; ref.w = MEGAMAN_RUN2_W; ref.h = MEGAMAN_RUN2_H;
            }
            break;
        case CameoSprite::MEGAMAN_SHOOT:
            ref.pixels565 = MEGAMAN_SHOOT; ref.w = MEGAMAN_SHOOT_W; ref.h = MEGAMAN_SHOOT_H; break;
        case CameoSprite::MEGAMAN_JUMP_SHOOT:
            ref.pixels565 = MEGAMAN_JUMP_SHOOT; ref.w = MEGAMAN_JUMP_SHOOT_W; ref.h = MEGAMAN_JUMP_SHOOT_H; break;
        case CameoSprite::BUSTER_BULLET:
            ref.pixels565 = BUSTER_BULLET; ref.w = BULLET_W; ref.h = BULLET_H; break;
        case CameoSprite::HIT_SPARK:
            ref.pixels565 = HIT_SPARK; ref.w = SPARK_W; ref.h = SPARK_H; break;
        case CameoSprite::METOOL_SLEEP:
            ref.pixels565 = METOOL_SLEEP; ref.w = METOOL_W; ref.h = METOOL_H; break;
        case CameoSprite::METOOL_PEEK:
            ref.pixels565 = METOOL_PEEK; ref.w = METOOL_W; ref.h = METOOL_H; break;
        case CameoSprite::GROUND_TILE_MEGAMAN:
            ref.pixels565 = GROUND_TILE; ref.w = GROUND_TILE_W; ref.h = GROUND_TILE_H; break;
        default: break;
    }
    return ref;
}

#if ARCADEMATRIX_HAS_EXTENDED_CLOCKS
SpriteFrameRef metalSlugClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex) {
    return marioClockGetSpriteRef(CameoSprite::MARIO_JUMP, frameIndex);
}
SpriteFrameRef pokedexClockGetSpriteRef(CameoSprite sprite, uint8_t frameIndex) {
    return marioClockGetSpriteRef(CameoSprite::GOLD_COIN, frameIndex);
}
#endif

} // namespace ClockSpriteBank
#endif

namespace ClockSpriteBank {

SpriteFrameRef getCameoFrame(CameoSprite sprite, uint8_t frameIndex) {
    switch (sprite) {
        case CameoSprite::MARIO_IDLE:
        case CameoSprite::MARIO_RUN:
        case CameoSprite::MARIO_JUMP:
        case CameoSprite::GOLD_COIN:
        case CameoSprite::KOOPA_SHELL:
        case CameoSprite::GROUND_TILE_MARIO:
            return marioClockGetSpriteRef(sprite, frameIndex);

        case CameoSprite::SONIC_SMS_RUN:
        case CameoSprite::SONIC_MD_RUN:
        case CameoSprite::SONIC_MD_BALL:
        case CameoSprite::SONIC_RING:
            return sonicClockGetSpriteRef(sprite, frameIndex);

        case CameoSprite::MEGAMAN_RUN:
        case CameoSprite::MEGAMAN_SHOOT:
        case CameoSprite::MEGAMAN_JUMP_SHOOT:
        case CameoSprite::BUSTER_BULLET:
        case CameoSprite::HIT_SPARK:
        case CameoSprite::METOOL_SLEEP:
        case CameoSprite::METOOL_PEEK:
        case CameoSprite::GROUND_TILE_MEGAMAN:
            return megamanClockGetSpriteRef(sprite, frameIndex);

        case CameoSprite::MARCO_VICTORY:
#if ARCADEMATRIX_HAS_EXTENDED_CLOCKS
            return metalSlugClockGetSpriteRef(sprite, frameIndex);
#else
            return megamanClockGetSpriteRef(CameoSprite::MEGAMAN_SHOOT, frameIndex);
#endif

        case CameoSprite::PIKACHU:
#if ARCADEMATRIX_HAS_EXTENDED_CLOCKS
            return pokedexClockGetSpriteRef(sprite, frameIndex);
#else
            return marioClockGetSpriteRef(CameoSprite::GOLD_COIN, frameIndex);
#endif
    }
    return SpriteFrameRef{};
}

void blitSprite(IDrawingSurface* surface, const SpriteFrameRef& ref, int16_t x, int16_t y, bool flipH) {
    if (!surface || !ref.isValid()) return;
    const int16_t panelW = surface->width();
    const int16_t panelH = surface->height();

    if (ref.pixels565 != nullptr) {
        for (int16_t row = 0; row < ref.h; ++row) {
            int16_t py = y + row;
            if (py < 0 || py >= panelH) continue;
            for (int16_t col = 0; col < ref.w; ++col) {
                int16_t px = x + col;
                if (px < 0 || px >= panelW) continue;
                int16_t srcCol = flipH ? (ref.w - 1 - col) : col;
                uint16_t color = pgm_read_word(&ref.pixels565[row * ref.w + srcCol]);
                if (color == ref.maskColor) continue;
                surface->drawPixel(px, py, color);
            }
        }
    } else if (ref.indexedPixels != nullptr && ref.palette != nullptr) {
        for (int16_t row = 0; row < ref.h; ++row) {
            int16_t py = y + row;
            if (py < 0 || py >= panelH) continue;
            for (int16_t col = 0; col < ref.w; ++col) {
                int16_t px = x + col;
                if (px < 0 || px >= panelW) continue;
                int16_t srcCol = flipH ? (ref.w - 1 - col) : col;
                uint8_t idx = pgm_read_byte(&ref.indexedPixels[row * ref.w + srcCol]);
                if (idx == 0) continue;
                uint16_t color = pgm_read_word(&ref.palette[idx]);
                if (color == ref.maskColor) continue;
                surface->drawPixel(px, py, color);
            }
        }
    }
}

namespace {
void blitBitmaskRows(IDrawingSurface* surface, const uint16_t* rows, int nRows, int nCols,
                     int16_t left, int16_t top, uint16_t color, bool mirror) {
    if (!surface || !rows) return;
    const int16_t w = surface->width();
    const int16_t h = surface->height();
    for (int r = 0; r < nRows; ++r) {
        int16_t py = top + r;
        if (py < 0 || py >= h) continue;
        uint16_t mask = rows[r];
        for (int c = 0; c < nCols; ++c) {
            int bitIdx = mirror ? c : (nCols - 1 - c);
            if ((mask >> bitIdx) & 1U) {
                int16_t px = left + c;
                if (px >= 0 && px < w) {
                    surface->drawPixel(px, py, color);
                }
            }
        }
    }
}
} // namespace

void drawPacman(IDrawingSurface* surface, int16_t x, int16_t y, uint8_t frameIndex, bool facingRight) {
    if (!surface) return;
    const uint16_t* rows = PAC_FRAME_HALF;
    uint8_t m = frameIndex % 4;
    if (m == 0) rows = PAC_FRAME_CLOSED;
    else if (m == 2) rows = PAC_FRAME_OPEN;
    blitBitmaskRows(surface, rows, 13, 13, x, y, 0xFFE0, !facingRight);
}

void drawScaredGhost(IDrawingSurface* surface, int16_t x, int16_t y, uint8_t frameIndex) {
    if (!surface) return;
    const bool flashWhite = ((frameIndex >> 2) & 1U) != 0U;
    const uint16_t bodyCol = flashWhite ? 0xFFFF : 0x19FF;
    const uint16_t faceCol = flashWhite ? 0xF800 : 0xFFDF;
    const uint16_t* skirt = (frameIndex & 1U) ? SKIRT_B : SKIRT_A;
    blitBitmaskRows(surface, GHOST_BODY, GHOST_BODY_ROWS, GHOST_BODY_COLS, x, y, bodyCol, false);
    blitBitmaskRows(surface, skirt, SKIRT_A_ROWS, SKIRT_A_COLS, x, y + 12, bodyCol, false);
    blitBitmaskRows(surface, FRIGHT_FACE, FRIGHT_FACE_ROWS, FRIGHT_FACE_COLS, x, y + 5, faceCol, false);
}

void drawAgendaIcon(IDrawingSurface* surface, uint8_t spriteId, int16_t x, int16_t y, uint8_t size, uint32_t animTick) {
    if (!surface) return;
    const uint8_t frame = static_cast<uint8_t>((animTick / 4U) & 3U);

    switch (spriteId) {
        case SPRITE_BIRTHDAY_CAKE: {
            // Animated Birthday Cake (flickering candle flame + gold sparkles)
            int16_t w = (size >= 16) ? 14 : 11;
            int16_t h = (size >= 16) ? 14 : 11;
            // Base sponge layer
            surface->fillRect(x + 1, y + h - 4, w - 2, 4, 0xFD20); // Golden sponge
            // Pink/White icing layer
            surface->fillRect(x + 1, y + h - 7, w - 2, 3, 0xF81F); // Magenta frosting
            surface->drawFastHLine(x + 1, y + h - 7, w - 2, 0xFFFF); // White cream top
            // Candle stem
            int16_t cx = x + (w / 2);
            surface->drawFastVLine(cx, y + h - 10, 3, 0xFFFF);
            // Flickering flame
            uint16_t flameCol = (frame & 1U) ? 0xFFE0 : 0xFD20;
            surface->drawPixel(cx, y + h - 11, flameCol);
            surface->drawPixel(cx + ((frame & 1U) ? 1 : -1), y + h - 12, 0xFFE0);
            // Micro gold sparkles
            if (frame == 0 || frame == 2) {
                surface->drawPixel(x, y + 2, 0xFFE0);
                surface->drawPixel(x + w - 1, y + 3, 0x07FF);
            } else {
                surface->drawPixel(x + 1, y + 1, 0x07FF);
                surface->drawPixel(x + w - 2, y + 1, 0xFFE0);
            }
            break;
        }
        case SPRITE_CYBER_CALL: {
            // Cyber Terminal / Headset with animated radio wave
            surface->drawRect(x + 1, y + 2, 8, 7, 0x07FF);
            surface->fillRect(x + 2, y + 3, 6, 5, 0x0210);
            surface->drawFastHLine(x, y + 9, 10, 0x07FF);
            // Animated wave arcs on the right
            if (frame >= 1) surface->drawPixel(x + 10, y + 4, 0x07E0);
            if (frame >= 2) {
                surface->drawPixel(x + 11, y + 3, 0xFFE0);
                surface->drawPixel(x + 11, y + 5, 0xFFE0);
            }
            break;
        }
        case SPRITE_MEDICAL_HEART: {
            // Pulsing Arcade Heart / +1UP Cross
            uint16_t heartCol = (frame & 1U) ? 0xF800 : 0xF986;
            // Classic pixel heart 9x8
            static const uint16_t HEART_ROWS[8] = {
                0b011001100,
                0b111111110,
                0b111111110,
                0b111111110,
                0b011111100,
                0b001111000,
                0b000110000,
                0b000000000
            };
            blitBitmaskRows(surface, HEART_ROWS, 8, 9, x + 1, y + 2, heartCol, false);
            // White shine pixel
            surface->drawPixel(x + 2, y + 3, 0xFFFF);
            break;
        }
        case SPRITE_FOOD_PIZZA: {
            // Pixel-Art Pizza Slice / Arcade Mug
            for (int r = 0; r < 9; ++r) {
                int span = 1 + (r * 8) / 9;
                int sx = x + 5 - span / 2;
                uint16_t col = (r < 2) ? 0xD480 : 0xFFE0; // Crust vs melted cheese
                surface->drawFastHLine(sx, y + 1 + r, span, col);
            }
            // Pepperoni dots
            surface->drawPixel(x + 5, y + 5, 0xF800);
            surface->drawPixel(x + 4, y + 7, 0xF800);
            surface->drawPixel(x + 6, y + 7, 0xF800);
            break;
        }
        case SPRITE_SPORT_SONIC: {
            if (size >= 16) {
                SpriteFrameRef ring = getCameoFrame(CameoSprite::SONIC_RING, frame);
                blitSprite(surface, ring, x, y, false);
            } else {
                // Bouncing arcade ball / ring
                int16_t by = y + ((frame & 1U) ? 1 : 3);
                surface->drawCircle(x + 5, by + 4, 4, 0x07FF);
                surface->drawPixel(x + 4, by + 3, 0xFFFF);
            }
            break;
        }
        case SPRITE_TRAVEL_PLANE: {
            // Pixel-art jet / train
            int16_t ox = (frame & 1U) ? 1 : 0;
            surface->drawFastHLine(x + 1 + ox, y + 5, 9, 0xFFFF);
            surface->drawFastHLine(x + 3 + ox, y + 4, 5, 0x07FF);
            surface->drawLine(x + 4 + ox, y + 2, x + 6 + ox, y + 8, 0x07FF);
            surface->drawPixel(x + ox, y + 5, 0xFD20); // Jet thruster
            break;
        }
        case SPRITE_GAMING_PACMAN: {
            if (size >= 13) {
                drawPacman(surface, x, y, frame, true);
            } else {
                SpriteFrameRef coin = getCameoFrame(CameoSprite::GOLD_COIN, frame);
                blitSprite(surface, coin, x + 1, y + 2, false);
            }
            break;
        }
        case SPRITE_METOOL_SLEEP: {
            SpriteFrameRef metool = getCameoFrame((frame == 3) ? CameoSprite::METOOL_PEEK : CameoSprite::METOOL_SLEEP, frame);
            blitSprite(surface, metool, x, y, false);
            // Floating pixel 'z' bubbles
            int16_t zx = x + 14 + (frame & 1U);
            int16_t zy = y - static_cast<int16_t>(frame * 2);
            surface->drawFastHLine(zx, zy, 3, 0x07FF);
            surface->drawPixel(zx + 1, zy + 1, 0x07FF);
            surface->drawFastHLine(zx, zy + 2, 3, 0x07FF);
            break;
        }
        case SPRITE_GOLD_COIN: {
            SpriteFrameRef coin = getCameoFrame(CameoSprite::GOLD_COIN, frame);
            blitSprite(surface, coin, x, y, false);
            break;
        }
        case SPRITE_SONIC_RING: {
            SpriteFrameRef ring = getCameoFrame(CameoSprite::SONIC_RING, frame);
            blitSprite(surface, ring, x, y, false);
            break;
        }
        case SPRITE_TROPHY_CUP: {
            // Golden Arcade Trophy Cup (10x10)
            surface->fillRect(x + 2, y + 1, 6, 4, 0xFFE0);
            surface->drawPixel(x + 1, y + 2, 0xFD20);
            surface->drawPixel(x + 8, y + 2, 0xFD20);
            surface->fillRect(x + 3, y + 5, 4, 2, 0xFD20);
            surface->drawFastVLine(x + 4, y + 7, 2, 0xFFE0);
            surface->drawFastVLine(x + 5, y + 7, 2, 0xFFE0);
            surface->drawFastHLine(x + 2, y + 9, 6, 0xFFE0);
            if (frame & 1U) surface->drawPixel(x + 3, y + 2, 0xFFFF);
            break;
        }
        case SPRITE_CROWN_RECORD: {
            // Royal Arcade Crown (10x8)
            surface->fillRect(x + 1, y + 5, 8, 3, 0xFFE0);
            surface->drawPixel(x + 1, y + 3, 0xFFE0);
            surface->drawPixel(x + 2, y + 4, 0xFFE0);
            surface->drawPixel(x + 4, y + 2, 0xFFE0);
            surface->drawPixel(x + 5, y + 2, 0xFFE0);
            surface->drawPixel(x + 4, y + 3, 0xFFE0);
            surface->drawPixel(x + 5, y + 3, 0xFFE0);
            surface->drawPixel(x + 7, y + 4, 0xFFE0);
            surface->drawPixel(x + 8, y + 3, 0xFFE0);
            surface->drawPixel(x + 2, y + 6, 0xF800); // Ruby gem
            surface->drawPixel(x + 5, y + 6, 0x07FF); // Diamond gem
            surface->drawPixel(x + 7, y + 6, 0x07E0); // Emerald gem
            break;
        }
        default: {
            // Default Calendar Page Icon (10x10)
            surface->drawRect(x + 1, y + 2, 9, 8, 0xFFFF);
            surface->fillRect(x + 1, y + 2, 9, 2, 0xF800);
            surface->drawPixel(x + 3, y + 1, 0xFFE0);
            surface->drawPixel(x + 7, y + 1, 0xFFE0);
            surface->drawPixel(x + 3, y + 6, 0x07FF);
            surface->drawPixel(x + 5, y + 6, 0x07FF);
            surface->drawPixel(x + 7, y + 6, 0x07FF);
            break;
        }
    }
}

namespace {
bool containsTokenIgnoreCase(const char* haystack, const char* needle) {
    if (!haystack || !needle || *needle == '\0') return false;
    size_t nlen = strlen(needle);
    for (const char* p = haystack; *p != '\0'; ++p) {
        if (strncasecmp(p, needle, nlen) == 0) return true;
    }
    return false;
}
} // namespace

HeroFamily classifySystemHero(const char* systemId) {
    if (!systemId || *systemId == '\0') return HeroFamily::Arcade;
    if (containsTokenIgnoreCase(systemId, "nes") ||
        containsTokenIgnoreCase(systemId, "snes") ||
        containsTokenIgnoreCase(systemId, "gb") ||
        containsTokenIgnoreCase(systemId, "n64") ||
        containsTokenIgnoreCase(systemId, "fds") ||
        containsTokenIgnoreCase(systemId, "nintendo") ||
        containsTokenIgnoreCase(systemId, "fam") ||
        containsTokenIgnoreCase(systemId, "nds") ||
        containsTokenIgnoreCase(systemId, "3ds") ||
        containsTokenIgnoreCase(systemId, "wii") ||
        containsTokenIgnoreCase(systemId, "switch") ||
        containsTokenIgnoreCase(systemId, "gamecube")) {
        return HeroFamily::Nintendo;
    }
    if (containsTokenIgnoreCase(systemId, "sega") ||
        containsTokenIgnoreCase(systemId, "megadrive") ||
        containsTokenIgnoreCase(systemId, "genesis") ||
        containsTokenIgnoreCase(systemId, "mastersystem") ||
        containsTokenIgnoreCase(systemId, "gamegear") ||
        containsTokenIgnoreCase(systemId, "saturn") ||
        containsTokenIgnoreCase(systemId, "dreamcast") ||
        containsTokenIgnoreCase(systemId, "32x")) {
        return HeroFamily::Sega;
    }
    if (containsTokenIgnoreCase(systemId, "cps") ||
        containsTokenIgnoreCase(systemId, "pcengine") ||
        containsTokenIgnoreCase(systemId, "tg16") ||
        containsTokenIgnoreCase(systemId, "psx") ||
        containsTokenIgnoreCase(systemId, "playstation") ||
        containsTokenIgnoreCase(systemId, "capcom")) {
        return HeroFamily::Capcom;
    }
    if (containsTokenIgnoreCase(systemId, "neogeo") ||
        containsTokenIgnoreCase(systemId, "snk")) {
        return HeroFamily::NeoGeo;
    }
    return HeroFamily::Arcade;
}

void drawCelebrationHero(IDrawingSurface* surface, HeroFamily family, int16_t groundY,
                         uint16_t panelW, uint16_t panelH, uint16_t frameTick) {
    if (!surface) return;
    const uint8_t animFrame = static_cast<uint8_t>((frameTick / 4U) & 3U);

    switch (family) {
        case HeroFamily::Nintendo: {
            // Mario runs in and jumps at t = 0.6s (frame 36..56), spawning 3 Gold Coins
            bool jumping = (frameTick >= 32 && frameTick <= 56) || ((frameTick % 90) >= 32 && (frameTick % 90) <= 52);
            SpriteFrameRef mario = getCameoFrame(jumping ? CameoSprite::MARIO_JUMP : CameoSprite::MARIO_RUN, animFrame);
            int16_t mx = 4;
            int16_t my = groundY - mario.h - (jumping ? 5 : 0);
            blitSprite(surface, mario, mx, my, false);
            if (jumping) {
                SpriteFrameRef coin = getCameoFrame(CameoSprite::GOLD_COIN, animFrame);
                blitSprite(surface, coin, mx + 4, my - 9, false);
                if (panelW >= 96) {
                    blitSprite(surface, coin, panelW - 14, my - 6, false);
                }
            }
            break;
        }
        case HeroFamily::Sega: {
            // Sonic runs or spins in ball with golden rings
            bool spinJump = (frameTick >= 34 && frameTick <= 58);
            CameoSprite sp = spinJump
                ? CameoSprite::SONIC_MD_BALL
                : ((panelH >= 64) ? CameoSprite::SONIC_MD_RUN : CameoSprite::SONIC_SMS_RUN);
            SpriteFrameRef sonic = getCameoFrame(sp, animFrame);
            int16_t sx = 2;
            int16_t sy = groundY - sonic.h - (spinJump ? 4 : 0);
            blitSprite(surface, sonic, sx, sy, false);
            if (panelW >= 96) {
                SpriteFrameRef ring = getCameoFrame(CameoSprite::SONIC_RING, animFrame);
                blitSprite(surface, ring, panelW - 18, groundY - ring.h - 2, false);
            }
            break;
        }
        case HeroFamily::Capcom: {
            // Mega Man shoots Buster bullet + Hit Spark
            SpriteFrameRef mm = getCameoFrame(CameoSprite::MEGAMAN_SHOOT, animFrame);
            int16_t mx = 2;
            int16_t my = groundY - mm.h;
            blitSprite(surface, mm, mx, my, false);
            int16_t bulletX = mx + mm.w + static_cast<int16_t>((frameTick * 3U) % (panelW > 48 ? (panelW - 36) : 24));
            SpriteFrameRef bullet = getCameoFrame(CameoSprite::BUSTER_BULLET, animFrame);
            blitSprite(surface, bullet, bulletX, my + 8, false);
            if ((frameTick & 7U) < 3U && panelW >= 96) {
                SpriteFrameRef spark = getCameoFrame(CameoSprite::HIT_SPARK, animFrame);
                blitSprite(surface, spark, panelW - 16, my + 4, false);
            }
            break;
        }
        case HeroFamily::NeoGeo: {
#if ARCADEMATRIX_HAS_EXTENDED_CLOCKS
            if (panelH >= 64) {
                SpriteFrameRef marco = getCameoFrame(CameoSprite::MARCO_VICTORY, animFrame);
                blitSprite(surface, marco, 2, groundY - marco.h, false);
                break;
            }
#endif
            drawPacman(surface, 3, groundY - 14, animFrame, true);
            if (panelW >= 96) {
                drawScaredGhost(surface, panelW - 17, groundY - 14, animFrame);
            }
            break;
        }
        case HeroFamily::Arcade:
        default: {
            drawPacman(surface, 3, groundY - 14, animFrame, true);
            if (panelW >= 96) {
                drawScaredGhost(surface, panelW - 17, groundY - 14, animFrame);
            }
            break;
        }
    }
}

} // namespace ClockSpriteBank
