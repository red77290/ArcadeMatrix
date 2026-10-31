#include "ShowcaseCardRenderer.h"

#include "../fonts/ArcadeFonts.h"
#include "../../core/ConfigLoader.h"
#include "../../core/I18n.h"
#include "../../core/drawing/IDrawingSurface.h"
#include <algorithm>
#include <cstring>
#include <strings.h>

extern ConfigLoader config;

namespace engines::showcase {

namespace {

void copySafeCardStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0U) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = 0U;
    while (n + 1U < cap && src[n] != '\0') {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}

struct CustomGlyph5x7 {
    uint16_t codepoint;
    uint8_t cols[5];
};

// Uppercase accented Latin glyphs & typographic symbols absent from CP437
constexpr CustomGlyph5x7 kCustomGlyphs5x7[] = {
    {0x00C0, {0x78, 0x25, 0x26, 0x24, 0x78}}, // A-grave (À)
    {0x00C1, {0x78, 0x24, 0x26, 0x25, 0x78}}, // A-acute (Á)
    {0x00C2, {0x78, 0x26, 0x25, 0x26, 0x78}}, // A-circumflex (Â)
    {0x00C3, {0x78, 0x26, 0x25, 0x26, 0x78}}, // A-tilde (Ã)
    {0x00C8, {0x7C, 0x55, 0x56, 0x44, 0x00}}, // E-grave (È)
    {0x00CA, {0x7C, 0x56, 0x55, 0x46, 0x00}}, // E-circumflex (Ê)
    {0x00CB, {0x7C, 0x55, 0x54, 0x45, 0x00}}, // E-diaeresis (Ë)
    {0x00CC, {0x00, 0x45, 0x7E, 0x44, 0x00}}, // I-grave (Ì)
    {0x00CD, {0x00, 0x44, 0x7E, 0x45, 0x00}}, // I-acute (Í)
    {0x00CE, {0x00, 0x46, 0x7D, 0x46, 0x00}}, // I-circumflex (Î)
    {0x00CF, {0x00, 0x45, 0x7C, 0x45, 0x00}}, // I-diaeresis (Ï)
    {0x00D2, {0x38, 0x45, 0x46, 0x44, 0x38}}, // O-grave (Ò)
    {0x00D3, {0x38, 0x44, 0x46, 0x45, 0x38}}, // O-acute (Ó)
    {0x00D4, {0x38, 0x46, 0x45, 0x46, 0x38}}, // O-circumflex (Ô)
    {0x00D5, {0x38, 0x46, 0x45, 0x46, 0x38}}, // O-tilde (Õ)
    {0x00D9, {0x3C, 0x41, 0x42, 0x40, 0x3C}}, // U-grave (Ù)
    {0x00DA, {0x3C, 0x40, 0x42, 0x41, 0x3C}}, // U-acute (Ú)
    {0x00DB, {0x3C, 0x42, 0x41, 0x42, 0x3C}}, // U-circumflex (Û)
    {0x0178, {0x0C, 0x11, 0x60, 0x11, 0x0C}}, // Y-diaeresis (Ÿ)
    {0x2026, {0x40, 0x00, 0x40, 0x00, 0x40}}, // Ellipsis (…)
    {0x20AC, {0x14, 0x3E, 0x55, 0x55, 0x22}}, // Euro (€)
};

uint32_t foldCombiningAccent(uint32_t base, uint32_t comb) {
    switch (comb) {
        case 0x0300:
            if (base == 'a') return 0x00E0;
            if (base == 'e') return 0x00E8;
            if (base == 'i') return 0x00EC;
            if (base == 'o') return 0x00F2;
            if (base == 'u') return 0x00F9;
            if (base == 'A') return 0x00C0;
            if (base == 'E') return 0x00C8;
            if (base == 'I') return 0x00CC;
            if (base == 'O') return 0x00D2;
            if (base == 'U') return 0x00D9;
            break;
        case 0x0301:
            if (base == 'a') return 0x00E1;
            if (base == 'e') return 0x00E9;
            if (base == 'i') return 0x00ED;
            if (base == 'o') return 0x00F3;
            if (base == 'u') return 0x00FA;
            if (base == 'A') return 0x00C1;
            if (base == 'E') return 0x00C9;
            if (base == 'I') return 0x00CD;
            if (base == 'O') return 0x00D3;
            if (base == 'U') return 0x00DA;
            break;
        case 0x0302:
            if (base == 'a') return 0x00E2;
            if (base == 'e') return 0x00EA;
            if (base == 'i') return 0x00EE;
            if (base == 'o') return 0x00F4;
            if (base == 'u') return 0x00FB;
            if (base == 'A') return 0x00C2;
            if (base == 'E') return 0x00CA;
            if (base == 'I') return 0x00CE;
            if (base == 'O') return 0x00D4;
            if (base == 'U') return 0x00DB;
            break;
        case 0x0303:
            if (base == 'n') return 0x00F1;
            if (base == 'N') return 0x00D1;
            if (base == 'a') return 0x00E3;
            if (base == 'o') return 0x00F5;
            if (base == 'A') return 0x00C3;
            if (base == 'O') return 0x00D5;
            break;
        case 0x0308:
            if (base == 'a') return 0x00E4;
            if (base == 'e') return 0x00EB;
            if (base == 'i') return 0x00EF;
            if (base == 'o') return 0x00F6;
            if (base == 'u') return 0x00FC;
            if (base == 'y') return 0x00FF;
            if (base == 'A') return 0x00C4;
            if (base == 'E') return 0x00CB;
            if (base == 'I') return 0x00CF;
            if (base == 'O') return 0x00D6;
            if (base == 'U') return 0x00DC;
            if (base == 'Y') return 0x0178;
            break;
        case 0x0327:
            if (base == 'c') return 0x00E7;
            if (base == 'C') return 0x00C7;
            break;
        default:
            break;
    }
    return base;
}

uint32_t decodeRawUtf8Unit(const char*& p) {
    const uint8_t b0 = static_cast<uint8_t>(*p);
    if (b0 == 0U) return 0U;
    ++p;
    if (b0 < 0x80U) {
        return b0;
    }
    if ((b0 & 0xE0U) == 0xC0U) {
        const uint8_t b1 = static_cast<uint8_t>(*p);
        if ((b1 & 0xC0U) != 0x80U) return '?';
        ++p;
        return (static_cast<uint32_t>(b0 & 0x1FU) << 6) | static_cast<uint32_t>(b1 & 0x3FU);
    }
    if ((b0 & 0xF0U) == 0xE0U) {
        const uint8_t b1 = static_cast<uint8_t>(*p);
        if ((b1 & 0xC0U) != 0x80U) return '?';
        const uint8_t b2 = static_cast<uint8_t>(*(p + 1));
        if ((b2 & 0xC0U) != 0x80U) {
            ++p;
            return '?';
        }
        p += 2;
        return (static_cast<uint32_t>(b0 & 0x0FU) << 12) |
               (static_cast<uint32_t>(b1 & 0x3FU) << 6) |
               static_cast<uint32_t>(b2 & 0x3FU);
    }
    if ((b0 & 0xF8U) == 0xF0U) {
        const uint8_t b1 = static_cast<uint8_t>(*p);
        if ((b1 & 0xC0U) != 0x80U) return '?';
        const uint8_t b2 = static_cast<uint8_t>(*(p + 1));
        if ((b2 & 0xC0U) != 0x80U) {
            ++p;
            return '?';
        }
        const uint8_t b3 = static_cast<uint8_t>(*(p + 2));
        if ((b3 & 0xC0U) != 0x80U) {
            p += 2;
            return '?';
        }
        p += 3;
        return (static_cast<uint32_t>(b0 & 0x07U) << 18) |
               (static_cast<uint32_t>(b1 & 0x3FU) << 12) |
               (static_cast<uint32_t>(b2 & 0x3FU) << 6) |
               static_cast<uint32_t>(b3 & 0x3FU);
    }
    return '?';
}

uint32_t nextUtf8Codepoint(const char*& p) {
    while (*p != '\0') {
        uint32_t cp = decodeRawUtf8Unit(p);
        if (cp >= 0x0300U && cp <= 0x036FU) {
            continue;
        }
        while (*p != '\0') {
            const char* peek = p;
            uint32_t nextCp = decodeRawUtf8Unit(peek);
            if (nextCp >= 0x0300U && nextCp <= 0x036FU) {
                cp = foldCombiningAccent(cp, nextCp);
                p = peek;
            } else {
                break;
            }
        }
        return cp;
    }
    return 0U;
}

bool mapUnicodeToCp437(uint32_t cp, uint8_t& outIdx) {
    if (cp >= 0x20U && cp <= 0x7EU) {
        outIdx = static_cast<uint8_t>(cp);
        return true;
    }
    switch (cp) {
        case 0x00A0: case 0x2009: case 0x202F: outIdx = ' '; return true;
        case 0x2018: case 0x2019: case 0x02BC: case 0x00B4: outIdx = '\''; return true;
        case 0x201C: case 0x201D: case 0x201E: outIdx = '"'; return true;
        case 0x2013: case 0x2014: case 0x2212: outIdx = '-'; return true;
        case 0x00C7: outIdx = 0x80; return true;
        case 0x00FC: outIdx = 0x81; return true;
        case 0x00E9: outIdx = 0x82; return true;
        case 0x00E2: outIdx = 0x83; return true;
        case 0x00E4: outIdx = 0x84; return true;
        case 0x00E0: outIdx = 0x85; return true;
        case 0x00E3: outIdx = 0x83; return true;
        case 0x00E5: outIdx = 0x86; return true;
        case 0x00E7: outIdx = 0x87; return true;
        case 0x00EA: outIdx = 0x88; return true;
        case 0x00EB: outIdx = 0x89; return true;
        case 0x00E8: outIdx = 0x8A; return true;
        case 0x00EF: outIdx = 0x8B; return true;
        case 0x00EE: outIdx = 0x8C; return true;
        case 0x00EC: outIdx = 0x8D; return true;
        case 0x00C4: outIdx = 0x8E; return true;
        case 0x00C5: outIdx = 0x8F; return true;
        case 0x00C9: outIdx = 0x90; return true;
        case 0x00E6: outIdx = 0x91; return true;
        case 0x00C6: outIdx = 0x92; return true;
        case 0x00F4: outIdx = 0x93; return true;
        case 0x00F6: outIdx = 0x94; return true;
        case 0x00F2: outIdx = 0x95; return true;
        case 0x00F5: outIdx = 0x93; return true;
        case 0x00FB: outIdx = 0x96; return true;
        case 0x00F9: outIdx = 0x97; return true;
        case 0x00FF: outIdx = 0x98; return true;
        case 0x00D6: outIdx = 0x99; return true;
        case 0x00DC: outIdx = 0x9A; return true;
        case 0x00A3: outIdx = 0x9C; return true;
        case 0x00A5: outIdx = 0x9D; return true;
        case 0x00E1: outIdx = 0xA0; return true;
        case 0x00ED: outIdx = 0xA1; return true;
        case 0x00F3: outIdx = 0xA2; return true;
        case 0x00FA: outIdx = 0xA3; return true;
        case 0x00F1: outIdx = 0xA4; return true;
        case 0x00D1: outIdx = 0xA5; return true;
        case 0x00BF: outIdx = 0xA8; return true;
        case 0x00A1: outIdx = 0xAD; return true;
        case 0x00AB: outIdx = 0xAE; return true;
        case 0x00BB: outIdx = 0xAF; return true;
        case 0x00DF: outIdx = 0xE1; return true;
        case 0x00B0: outIdx = 0xF8; return true;
        case 0x2022: case 0x00B7: outIdx = 0xF9; return true;
        case 0x00FD: outIdx = 'y'; return true;
        case 0x00DD: outIdx = 'Y'; return true;
        default: return false;
    }
}

void getGlyph5x7Columns(uint32_t cp, uint8_t outCols[5]) {
    for (const auto& g : kCustomGlyphs5x7) {
        if (g.codepoint == cp) {
            memcpy(outCols, g.cols, 5U);
            return;
        }
    }
    uint8_t idx = '?';
    if (!mapUnicodeToCp437(cp, idx)) {
        idx = '?';
    }
    const unsigned char* base = g_glcdFont5x7 + (static_cast<size_t>(idx) * 5U);
    for (int col = 0; col < 5; ++col) {
        outCols[col] = pgm_read_byte(base + col);
    }
}

void drawSingleGlyph5x7Clipped(
    IDrawingSurface* s,
    const uint8_t cols[5],
    int gx,
    int gy,
    int clipMinX,
    int clipMaxX,
    uint16_t color
) {
    if (gx + 5 <= clipMinX || gx >= clipMaxX) return;
    int mH = s->height();
    for (int col = 0; col < 5; ++col) {
        int px = gx + col;
        if (px < clipMinX || px >= clipMaxX) continue;
        uint8_t bits = cols[col];
        if (bits == 0U) continue;
        for (int row = 0; row < 8; ++row) {
            if ((bits >> row) & 1U) {
                int py = gy + row;
                if (py >= 0 && py < mH) {
                    s->drawPixel(px, py, color);
                }
            }
        }
    }
}

constexpr uint16_t pack3x5(uint8_t r0, uint8_t r1, uint8_t r2, uint8_t r3, uint8_t r4) {
    return static_cast<uint16_t>((r0 << 12) | (r1 << 9) | (r2 << 6) | (r3 << 3) | r4);
}

uint16_t getGlyph3x5Bitmap(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    switch (ch) {
        case '0': return pack3x5(7, 5, 5, 5, 7);
        case '1': return pack3x5(2, 6, 2, 2, 7);
        case '2': return pack3x5(7, 1, 7, 4, 7);
        case '3': return pack3x5(7, 1, 7, 1, 7);
        case '4': return pack3x5(5, 5, 7, 1, 1);
        case '5': return pack3x5(7, 4, 7, 1, 7);
        case '6': return pack3x5(7, 4, 7, 5, 7);
        case '7': return pack3x5(7, 1, 1, 2, 2);
        case '8': return pack3x5(7, 5, 7, 5, 7);
        case '9': return pack3x5(7, 5, 7, 1, 7);
        case 'A': return pack3x5(2, 5, 7, 5, 5);
        case 'B': return pack3x5(6, 5, 6, 5, 6);
        case 'C': return pack3x5(3, 4, 4, 4, 3);
        case 'D': return pack3x5(6, 5, 5, 5, 6);
        case 'E': return pack3x5(7, 4, 6, 4, 7);
        case 'F': return pack3x5(7, 4, 6, 4, 4);
        case 'G': return pack3x5(3, 4, 5, 5, 3);
        case 'H': return pack3x5(5, 5, 7, 5, 5);
        case 'I': return pack3x5(7, 2, 2, 2, 7);
        case 'J': return pack3x5(1, 1, 1, 5, 2);
        case 'K': return pack3x5(5, 5, 6, 5, 5);
        case 'L': return pack3x5(4, 4, 4, 4, 7);
        case 'M': return pack3x5(5, 7, 5, 5, 5);
        case 'N': return pack3x5(6, 5, 5, 5, 5);
        case 'O': return pack3x5(2, 5, 5, 5, 2);
        case 'P': return pack3x5(6, 5, 6, 4, 4);
        case 'Q': return pack3x5(2, 5, 5, 3, 1);
        case 'R': return pack3x5(6, 5, 6, 5, 5);
        case 'S': return pack3x5(3, 4, 2, 1, 6);
        case 'T': return pack3x5(7, 2, 2, 2, 2);
        case 'U': return pack3x5(5, 5, 5, 5, 7);
        case 'V': return pack3x5(5, 5, 5, 5, 2);
        case 'W': return pack3x5(5, 5, 5, 7, 5);
        case 'X': return pack3x5(5, 5, 2, 5, 5);
        case 'Y': return pack3x5(5, 5, 2, 2, 2);
        case 'Z': return pack3x5(7, 1, 2, 4, 7);
        case '.': return pack3x5(0, 0, 0, 0, 2);
        case ':': return pack3x5(0, 2, 0, 2, 0);
        case '-': return pack3x5(0, 0, 7, 0, 0);
        case '_': return pack3x5(0, 0, 0, 0, 7);
        case '/': return pack3x5(1, 1, 2, 4, 4);
        case '+': return pack3x5(0, 2, 7, 2, 0);
        case '!': return pack3x5(2, 2, 2, 0, 2);
        case '?': return pack3x5(6, 1, 2, 0, 2);
        case '\'': return pack3x5(2, 2, 0, 0, 0);
        case '(': return pack3x5(1, 2, 2, 2, 1);
        case ')': return pack3x5(4, 2, 2, 2, 4);
        case '%': return pack3x5(5, 1, 2, 4, 5);
        case '*': return pack3x5(5, 2, 7, 2, 5);
        default:  return 0;
    }
}

char foldCodepointToAscii3x5(uint32_t cp) {
    if (cp >= 0x20U && cp <= 0x7EU) return static_cast<char>(cp);
    switch (cp) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
            return 'A';
        case 0x00C7: case 0x00E7:
            return 'C';
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
            return 'E';
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
            return 'I';
        case 0x00D1: case 0x00F1:
            return 'N';
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6:
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6:
            return 'O';
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
            return 'U';
        case 0x00DD: case 0x00FD: case 0x00FF: case 0x0178:
            return 'Y';
        case 0x2013: case 0x2014: case 0x2212:
            return '-';
        case 0x2018: case 0x2019:
            return '\'';
        default:
            return ' ';
    }
}

} // namespace

const char* ShowcaseCardRenderer::domainSlug(ShowcaseDomainId domain) {
    switch (domain) {
        case ShowcaseDomainId::Games:  return "games";
        case ShowcaseDomainId::Movies: return "movies";
        case ShowcaseDomainId::Series: return "series";
        case ShowcaseDomainId::Anime:  return "anime";
        default:                       return "games";
    }
}

const char* ShowcaseCardRenderer::domainBadgeLabel(ShowcaseDomainId domain) {
    switch (domain) {
        case ShowcaseDomainId::Games:  return "RETRO GAMES";
        case ShowcaseDomainId::Movies: return "MOVIES";
        case ShowcaseDomainId::Series: return "TV SERIES";
        case ShowcaseDomainId::Anime:  return "ANIME";
        default:                       return "SHOWCASE";
    }
}

uint16_t ShowcaseCardRenderer::defaultAccent565(ShowcaseDomainId domain) {
    switch (domain) {
        case ShowcaseDomainId::Games:  return 0xFD20; // Arcade Gold/Orange
        case ShowcaseDomainId::Movies: return 0x07FF; // Cinema Cyan
        case ShowcaseDomainId::Series: return 0xF945; // Series Crimson
        case ShowcaseDomainId::Anime:  return 0xF81F; // Anime Magenta
        default:                       return 0xFD20;
    }
}

size_t ShowcaseCardRenderer::countUtf8Glyphs(const char* utf8) {
    if (!utf8 || utf8[0] == '\0') return 0U;
    size_t count = 0U;
    const char* p = utf8;
    while (*p != '\0') {
        uint32_t cp = nextUtf8Codepoint(p);
        if (cp == 0U) break;
        if (cp == 0x0152U || cp == 0x0153U) {
            count += 2U;
        } else {
            count += 1U;
        }
    }
    return count;
}

void ShowcaseCardRenderer::computeCardPosterDims(
    uint16_t panelW,
    uint16_t panelH,
    uint16_t& outW,
    uint16_t& outH
) {
    if (panelH > panelW) {
        uint16_t side = (panelW >= 64U) ? 40U : 24U;
        outW = side;
        outH = side;
    } else if (panelW == panelH) {
        outW = 32U;
        outH = 32U;
    } else if (panelH >= 64U) {
        outW = 48U;
        outH = 64U;
    } else {
        outW = 32U;
        outH = 32U;
    }
}

int ShowcaseCardRenderer::computeCardTextViewportWidth(
    uint16_t panelW,
    uint16_t panelH,
    bool showPoster
) {
    int mW = static_cast<int>(panelW);
    int mH = static_cast<int>(panelH);
    if (mH > mW || mW == mH) {
        return std::max(16, mW - 4);
    }
    if (mH >= 64) {
        int textX = showPoster ? (48 + 4) : 3;
        return std::max(16, (mW - 2) - textX);
    }
    int textX = showPoster ? 35 : 3;
    return std::max(16, (mW - 2) - textX);
}

void ShowcaseCardRenderer::drawClippedText5x7(
    IDrawingSurface* s,
    const char* utf8,
    int x,
    int y,
    int clipMinX,
    int clipMaxX,
    uint16_t color
) {
    if (!s || !utf8 || clipMinX >= clipMaxX) return;
    int curX = x;
    const char* p = utf8;
    uint8_t cols[5];
    while (*p != '\0') {
        if (curX >= clipMaxX) break;
        uint32_t cp = nextUtf8Codepoint(p);
        if (cp == 0U) break;
        if (cp == '\r' || cp == '\n' || cp == '\t') {
            cp = ' ';
        }
        if (cp == 0x0153U) { // œ -> oe
            getGlyph5x7Columns('o', cols);
            drawSingleGlyph5x7Clipped(s, cols, curX, y, clipMinX, clipMaxX, color);
            curX += 6;
            getGlyph5x7Columns('e', cols);
            drawSingleGlyph5x7Clipped(s, cols, curX, y, clipMinX, clipMaxX, color);
            curX += 6;
            continue;
        }
        if (cp == 0x0152U) { // Œ -> OE
            getGlyph5x7Columns('O', cols);
            drawSingleGlyph5x7Clipped(s, cols, curX, y, clipMinX, clipMaxX, color);
            curX += 6;
            getGlyph5x7Columns('E', cols);
            drawSingleGlyph5x7Clipped(s, cols, curX, y, clipMinX, clipMaxX, color);
            curX += 6;
            continue;
        }
        getGlyph5x7Columns(cp, cols);
        drawSingleGlyph5x7Clipped(s, cols, curX, y, clipMinX, clipMaxX, color);
        curX += 6;
    }
}

void ShowcaseCardRenderer::drawClippedText3x5(
    IDrawingSurface* s,
    const char* utf8,
    int x,
    int y,
    int clipMinX,
    int clipMaxX,
    uint16_t color
) {
    if (!s || !utf8 || clipMinX >= clipMaxX) return;
    int mH = s->height();
    int curX = x;
    const char* p = utf8;
    while (*p != '\0') {
        if (curX >= clipMaxX) break;
        uint32_t cp = nextUtf8Codepoint(p);
        if (cp == 0U) break;
        char ch = foldCodepointToAscii3x5(cp);
        uint16_t bmp = getGlyph3x5Bitmap(ch);
        if (bmp != 0U && curX + 3 > clipMinX) {
            for (int row = 0; row < 5; ++row) {
                int py = y + row;
                if (py < 0 || py >= mH) continue;
                uint8_t rowBits = static_cast<uint8_t>((bmp >> ((4 - row) * 3)) & 0x07U);
                if (rowBits == 0U) continue;
                for (int col = 0; col < 3; ++col) {
                    if ((rowBits >> (2 - col)) & 1U) {
                        int px = curX + col;
                        if (px >= clipMinX && px < clipMaxX) {
                            s->drawPixel(px, py, color);
                        }
                    }
                }
            }
        }
        curX += 4;
    }
}

void ShowcaseCardRenderer::drawScrollingLine(
    IDrawingSurface* s,
    const char* text,
    int minX,
    int maxX,
    int y,
    uint16_t color,
    uint32_t stepPx
) {
    if (!s || !text || text[0] == '\0' || minX >= maxX) return;
    int availW = maxX - minX;
    int textW = static_cast<int>(countUtf8Glyphs(text)) * 6;

    if (textW <= availW) {
        drawClippedText5x7(s, text, minX, y, minX, maxX, color);
        return;
    }

    int totalScroll = textW - availW + 12;
    int holdSteps = std::min(64, std::max(22, (availW * 22 + 45) / 91));
    int cycleSteps = totalScroll + holdSteps * 2;
    int step = static_cast<int>(stepPx % static_cast<uint32_t>(cycleSteps));
    int offset = std::min(totalScroll, std::max(0, step - holdSteps));
    drawClippedText5x7(s, text, minX - offset, y, minX, maxX, color);
}

void ShowcaseCardRenderer::drawSinglePassSynopsis(
    IDrawingSurface* s,
    const char* text,
    int minX,
    int maxX,
    int y,
    uint16_t color,
    uint32_t scrollPx
) {
    if (!s || !text || text[0] == '\0' || minX >= maxX) return;
    int availW = maxX - minX;
    int textW = static_cast<int>(countUtf8Glyphs(text)) * 6;

    if (textW <= availW) {
        drawClippedText5x7(s, text, minX, y, minX, maxX, color);
        return;
    }

    int maxScrollPx = (textW - availW) + (availW / 2);
    int clampedPx = std::min(maxScrollPx, static_cast<int>(scrollPx));
    drawClippedText5x7(s, text, minX - clampedPx, y, minX, maxX, color);
}

void ShowcaseCardRenderer::drawWaitingPixelArt(
    IDrawingSurface* s,
    ShowcaseDomainId domain,
    uint8_t statusState,
    int mW,
    int mH,
    uint32_t elapsedMs
) {
    if (!s) return;
    uint32_t frameStep = elapsedMs / 180U;
    uint16_t accent = defaultAccent565(domain);

    int textY = (mH >= 64) ? (mH - 9) : (mH - 6);
    int midTop = (mH >= 64) ? 12 : 9;
    int midBot = std::max(midTop + 8, textY - 2);
    int midH = std::max(8, midBot - midTop);
    int midCy = midTop + (midH / 2);
    int cx = mW / 2;

    switch (domain) {
        case ShowcaseDomainId::Games: {
            uint16_t starBright = s->color565(90, 120, 180);
            uint16_t starDim = s->color565(45, 65, 105);
            int fieldMargin = std::min(12, mW / 8);
            int fieldW = std::max(8, mW - fieldMargin * 2);
            for (int i = 0; i < 12; ++i) {
                int sx = fieldMargin + static_cast<int>((i * 19U + elapsedMs / (22U + (i % 3) * 10U)) % static_cast<uint32_t>(fieldW));
                int sy = midTop + 1 + ((i * 7) % std::max(1, midH - 2));
                s->drawPixel(sx, sy, (i % 2 == 0) ? starBright : starDim);
            }
            static const uint16_t kInvaderF0[8] = {
                0x0104, 0x0088, 0x01FC, 0x0376,
                0x07FF, 0x05FD, 0x0505, 0x00D8
            };
            static const uint16_t kInvaderF1[8] = {
                0x0104, 0x0489, 0x05FD, 0x0777,
                0x07FF, 0x03FE, 0x0104, 0x0202
            };
            const uint16_t* sprite = ((frameStep & 1U) == 0U) ? kInvaderF0 : kInvaderF1;
            int invY = midCy - 4;
            int patrolSpan = std::max(12, std::min(56, mW - 36));
            uint32_t pingPong = (elapsedMs / 40U) % static_cast<uint32_t>(patrolSpan * 2);
            int patrolOff = (pingPong <= static_cast<uint32_t>(patrolSpan))
                ? static_cast<int>(pingPong)
                : (patrolSpan * 2 - static_cast<int>(pingPong));
            int invX = cx - (patrolSpan / 2) - 5 + patrolOff;
            uint16_t invCol = s->color565(0, 255, 128);
            for (int ry = 0; ry < 8; ++ry) {
                uint16_t bits = sprite[ry];
                for (int rx = 0; rx < 11; ++rx) {
                    if ((bits >> (10 - rx)) & 1U) {
                        s->drawPixel(invX + rx, invY + ry, invCol);
                    }
                }
            }
            uint16_t pelletCol = s->color565(255, 215, 0);
            bool movingRight = (pingPong <= static_cast<uint32_t>(patrolSpan));
            for (int p = 1; p <= 3; ++p) {
                int px = movingRight ? (invX + 12 + p * 6) : (invX - 2 - p * 6);
                if (px >= fieldMargin && px + 1 < mW - fieldMargin) {
                    s->fillRect(px, invY + 3, 2, 2, pelletCol);
                }
            }
            break;
        }
        case ShowcaseDomainId::Movies: {
            int sceneHalf = std::min(26, std::max(14, (mW - 20) / 2));
            int rRad = (mH >= 64) ? 5 : 3;
            int r1X = cx - sceneHalf + rRad + 2;
            int r2X = r1X + rRad * 2 + 2;
            int reelY = midCy - 2;
            uint16_t reelCol = s->color565(0, 229, 255);
            s->drawCircle(r1X, reelY, rRad, reelCol);
            s->drawCircle(r2X, reelY, rRad, reelCol);
            int spoke = static_cast<int>(frameStep % 4U);
            int dx = (spoke == 0) ? (rRad - 1) : (spoke == 2 ? 0 : (spoke == 1 ? (rRad - 1) : -(rRad - 1)));
            int dy = (spoke == 0) ? 0 : (spoke == 2 ? (rRad - 1) : (rRad - 1));
            s->drawLine(r1X - dx, reelY - dy, r1X + dx, reelY + dy, 0xFFFF);
            s->drawLine(r2X - dx, reelY - dy, r2X + dx, reelY + dy, 0xFFFF);

            int bodyW = rRad * 2 + 4;
            s->fillRect(r1X - 1, midCy + 1, bodyW, 4, s->color565(120, 140, 165));
            int lensX = r1X - 1 + bodyW;
            int screenX = cx + sceneHalf;
            int beamHalfH = std::min(6, midH / 2 - 1);
            uint8_t flicker = ((elapsedMs / 110U) % 3U == 0U) ? 215 : 255;
            uint16_t beamCol = s->color565(flicker, 200, 75);
            s->drawLine(lensX, midCy + 2, screenX - 1, midCy - beamHalfH, beamCol);
            s->drawLine(lensX, midCy + 2, screenX - 1, midCy + beamHalfH, beamCol);
            s->drawFastVLine(screenX, midCy - beamHalfH, beamHalfH * 2 + 1, 0xFFFF);
            break;
        }
        case ShowcaseDomainId::Series: {
            int tvW = (mW >= 128) ? 22 : 16;
            int tvH = (mH >= 64) ? 16 : 10;
            int tvX = cx - (tvW / 2);
            int tvY = midCy - (tvH / 2) + 2;
            s->drawRoundRect(tvX, tvY, tvW, tvH, 2, s->color565(229, 57, 53));
            uint16_t antCol = s->color565(200, 210, 230);
            s->drawLine(cx, tvY, cx - 4, tvY - 3, antCol);
            s->drawLine(cx, tvY, cx + 4, tvY - 3, antCol);

            int wavePhase = static_cast<int>((elapsedMs / 200U) % 3U);
            uint16_t waveCol = s->color565(0, 229, 255);
            for (int w = 0; w <= wavePhase; ++w) {
                int wx = 7 + w * 4;
                s->drawFastVLine(cx - wx, tvY - 2 - w, 3 + w, waveCol);
                s->drawFastVLine(cx + wx, tvY - 2 - w, 3 + w, waveCol);
            }

            int innerW = std::max(6, tvW - 4);
            int innerH = std::max(4, tvH - 4);
            const uint16_t kBars[6] = {
                s->color565(235, 235, 235),
                s->color565(240, 220, 0),
                s->color565(0, 220, 220),
                s->color565(0, 210, 80),
                s->color565(220, 0, 220),
                s->color565(220, 30, 30)
            };
            for (int b = 0; b < 6; ++b) {
                int bx0 = tvX + 2 + (b * innerW) / 6;
                int bx1 = tvX + 2 + ((b + 1) * innerW) / 6;
                s->fillRect(bx0, tvY + 2, std::max(1, bx1 - bx0), innerH, kBars[b]);
            }
            int scanY = tvY + 2 + static_cast<int>((elapsedMs / 80U) % innerH);
            s->drawFastHLine(tvX + 2, scanY, innerW, 0xFFFF);
            break;
        }
        case ShowcaseDomainId::Anime:
        default: {
            uint16_t lineCol = s->color565(65, 28, 85);
            for (int i = 0; i < 6; ++i) {
                int ly = midTop + 1 + (i * std::max(4, midH - 2)) / 6;
                int len = 8 + (i * 5) % 12;
                int lx = std::max(4, mW - 4 - static_cast<int>((elapsedMs / 14U + i * 21U) % static_cast<uint32_t>(std::max(8, mW - 8))) - len);
                if (lx + len > mW - 4) len = (mW - 4) - lx;
                if (len > 0) s->drawFastHLine(lx, ly, len, lineCol);
            }
            int arm = (mH >= 64) ? 7 : 5;
            uint16_t cyanCol = s->color565(0, 229, 255);
            uint16_t pinkCol = s->color565(255, 64, 129);
            if ((frameStep & 1U) == 0U) {
                s->drawFastHLine(cx - arm, midCy, arm * 2 + 1, cyanCol);
                s->drawFastVLine(cx, midCy - arm, arm * 2 + 1, cyanCol);
                s->drawLine(cx - 3, midCy - 3, cx + 3, midCy + 3, pinkCol);
                s->drawLine(cx - 3, midCy + 3, cx + 3, midCy - 3, pinkCol);
            } else {
                s->drawLine(cx - arm, midCy - arm, cx + arm, midCy + arm, cyanCol);
                s->drawLine(cx - arm, midCy + arm, cx + arm, midCy - arm, cyanCol);
                s->drawFastHLine(cx - 3, midCy, 7, pinkCol);
                s->drawFastVLine(cx, midCy - 3, 7, pinkCol);
            }
            s->fillCircle(cx, midCy, 1, 0xFFFF);
            uint16_t sakuraCol = s->color565(255, 130, 190);
            for (int p = 0; p < 5; ++p) {
                int px = 8 + static_cast<int>((p * 23U + elapsedMs / 45U) % static_cast<uint32_t>(std::max(8, mW - 16)));
                int py = midTop + 1 + static_cast<int>((p * 5U + elapsedMs / 95U) % static_cast<uint32_t>(std::max(3, midH - 3)));
                s->fillRect(px, py, 2, 2, sakuraCol);
            }
            break;
        }
    }

    const char* badge = domainBadgeLabel(domain);
    int badgeGlyphs = static_cast<int>(countUtf8Glyphs(badge));
    int badgeTextW = std::max(0, badgeGlyphs * 4 - 1);
    int pillW = badgeTextW + 6;
    int pillX = std::max(1, (mW - pillW) / 2);
    s->fillRoundRect(pillX, 1, pillW, 7, 2, s->color565(18, 22, 34));
    s->drawRoundRect(pillX, 1, pillW, 7, 2, accent);
    drawClippedText3x5(s, badge, pillX + 3, 2, 0, mW, accent);

    ConfigSnapshotGuard guard = config.acquireSnapshot();
    Lang sysLang = I18n::parseLang(guard.get().system.lang.c_str());
    const char* statusText = (statusState == 3U)
        ? I18n::getShowcaseUnconfiguredLabel(sysLang)
        : (statusState == 2U)
            ? I18n::getShowcaseOfflineLabel(sysLang)
            : I18n::getShowcaseWarmingLabel(sysLang);

    char stemBuf[48];
    copySafeCardStr(stemBuf, sizeof(stemBuf), statusText);
    size_t rawLen = strlen(stemBuf);
    bool hasEllipsis = false;
    while (rawLen > 0U && stemBuf[rawLen - 1U] == '.') {
        stemBuf[--rawLen] = '\0';
        hasEllipsis = true;
    }

    int stemGlyphs = static_cast<int>(countUtf8Glyphs(stemBuf));
    int totalSlots = stemGlyphs + (hasEllipsis ? 3 : 0);
    int totalTextW = std::max(0, totalSlots * 4 - 1);
    uint16_t steadyLabelCol = s->color565(225, 232, 242);

    if (totalTextW <= mW - 4) {
        int tx = (mW - totalTextW) / 2;
        drawClippedText3x5(s, stemBuf, tx, textY, 2, mW - 2, steadyLabelCol);
        if (hasEllipsis) {
            int dotBaseX = tx + stemGlyphs * 4;
            int litDots = 1 + static_cast<int>((elapsedMs / 400U) % 3U);
            uint16_t dimDotCol = s->color565(55, 65, 85);
            for (int d = 0; d < 3; ++d) {
                int dx = dotBaseX + d * 4 + 1;
                if (dx >= 2 && dx < mW - 2) {
                    s->drawPixel(dx, textY + 4, (d < litDots) ? accent : dimDotCol);
                }
            }
        }
    } else {
        int availW = std::max(8, mW - 4);
        int fullW = static_cast<int>(countUtf8Glyphs(statusText)) * 4;
        int scrollRange = std::max(1, fullW - availW + 8);
        int step = static_cast<int>((elapsedMs / 55U) % static_cast<uint32_t>(scrollRange + 20));
        int off = std::min(scrollRange, std::max(0, step - 10));
        drawClippedText3x5(s, statusText, 2 - off, textY, 2, mW - 2, steadyLabelCol);
    }
}

void ShowcaseCardRenderer::drawCardPhase(
    IDrawingSurface* s,
    ShowcaseDomainId domain,
    uint8_t statusState,
    const ShowcaseCardData& card,
    const uint16_t* poster565,
    bool showPoster,
    bool showSynopsis,
    uint32_t titleStepPx,
    uint32_t synScrollPx,
    uint32_t elapsedMs
) {
    if (!s) return;
    int mW = s->width();
    int mH = s->height();

    if (!card.hasData) {
        drawWaitingPixelArt(s, domain, statusState, mW, mH, elapsedMs);
        return;
    }

    uint16_t sepCol = s->color565(28, 36, 52);
    uint16_t yearCol = s->color565(165, 180, 200);
    uint16_t synCol = s->color565(205, 214, 226);

    // 1. Vertical TATE Mode (e.g. 64x128, 64x256, 32x128)
    if (mH > mW) {
        drawClippedText3x5(s, card.badge, 2, 2, 2, mW - 2, card.badgeColor565);
        if (card.rating[0] != '\0') {
            int rw = static_cast<int>(countUtf8Glyphs(card.rating)) * 4;
            drawClippedText3x5(s, card.rating, std::max(2, mW - rw - 2), 2, 2, mW - 2, 0xFFE0);
        }
        int posterBottom = 10;
        if (showPoster) {
            int pw = static_cast<int>(card.posterW);
            int ph = static_cast<int>(card.posterH);
            int px = std::max(0, (mW - pw) / 2);
            int py = 10;
            if (card.hasPoster && poster565) {
                s->blit565(poster565, px, py, pw, ph);
            } else {
                s->drawRect(px, py, pw, ph, card.badgeColor565);
            }
            posterBottom = py + ph + 3;
        }
        drawScrollingLine(s, card.title, 2, mW - 2, posterBottom, 0xFFFF, titleStepPx);
        int sepY = posterBottom + 10;
        if (sepY < mH - 10) {
            s->drawFastHLine(2, sepY, mW - 4, sepCol);
            if (showSynopsis && card.synopsis[0] != '\0') {
                drawSinglePassSynopsis(s, card.synopsis, 2, mW - 2, sepY + 3, synCol, synScrollPx);
            }
        }
        return;
    }

    // 2. Square Panel (e.g. 64x64)
    if (mW == mH) {
        int rightX = showPoster ? 35 : 2;
        if (showPoster) {
            if (card.hasPoster && poster565) {
                s->blit565(poster565, 0, 0, 32, 32);
            } else {
                s->drawRect(0, 0, 32, 32, card.badgeColor565);
            }
            s->drawFastVLine(33, 1, 30, sepCol);
        }
        drawClippedText3x5(s, card.badge, rightX, 3, rightX, mW - 2, card.badgeColor565);
        if (card.year[0] != '\0') {
            drawClippedText3x5(s, card.year, rightX, 13, rightX, mW - 2, yearCol);
        }
        if (card.rating[0] != '\0') {
            drawClippedText3x5(s, card.rating, rightX, 23, rightX, mW - 2, 0xFFE0);
        }
        drawScrollingLine(s, card.title, 2, mW - 2, 35, 0xFFFF, titleStepPx);
        s->drawFastHLine(2, 45, mW - 4, sepCol);
        if (showSynopsis && card.synopsis[0] != '\0') {
            drawSinglePassSynopsis(s, card.synopsis, 2, mW - 2, 49, synCol, synScrollPx);
        }
        return;
    }

    // 3. Tall / XL Landscape (e.g. 128x64, 256x64 on ESP32-S3 PSRAM)
    if (mH >= 64) {
        int pw = static_cast<int>(card.posterW);
        int ph = static_cast<int>(card.posterH);
        int textX = showPoster ? (pw + 4) : 3;
        int maxX = mW - 2;

        if (showPoster) {
            if (card.hasPoster && poster565) {
                s->blit565(poster565, 0, 0, pw, ph);
            } else {
                s->fillRoundRect(0, 0, pw, ph, 3, s->color565(16, 20, 32));
                s->drawRoundRect(0, 0, pw, ph, 3, card.badgeColor565);
            }
            s->drawFastVLine(pw + 1, 2, mH - 4, sepCol);
        }

        int curX = textX;
        if (card.badge[0] != '\0') {
            int bw = static_cast<int>(countUtf8Glyphs(card.badge)) * 6 + 4;
            int drawBw = std::min(bw, std::max(8, maxX - curX));
            s->fillRoundRect(curX, 1, drawBw, 9, 2, s->color565(20, 24, 36));
            s->drawRoundRect(curX, 1, drawBw, 9, 2, card.badgeColor565);
            drawClippedText5x7(s, card.badge, curX + 2, 2, curX + 2, curX + drawBw - 1, card.badgeColor565);
            curX += drawBw + 4;
        }
        if (card.rating[0] != '\0') {
            int rw = static_cast<int>(countUtf8Glyphs(card.rating)) * 6;
            int rx = std::max(curX, maxX - rw);
            if (rx < maxX) {
                drawClippedText5x7(s, card.rating, rx, 2, curX, maxX, 0xFFE0);
            }
        }
        drawScrollingLine(s, card.title, textX, maxX, 14, 0xFFFF, titleStepPx);
        if (card.year[0] != '\0') {
            drawClippedText5x7(s, card.year, textX, 25, textX, maxX, yearCol);
        }
        s->drawFastHLine(textX, 36, std::max(1, maxX - textX), sepCol);
        if (showSynopsis && card.synopsis[0] != '\0') {
            drawSinglePassSynopsis(s, card.synopsis, textX, maxX, 43, synCol, synScrollPx);
        }
        return;
    }

    // 4. Standard Landscape (128x32)
    int textX = showPoster ? 35 : 3;
    int maxX = mW - 2;

    if (showPoster) {
        if (card.hasPoster && poster565) {
            s->blit565(poster565, 0, 0, 32, 32);
        } else {
            s->fillRoundRect(0, 0, 32, 32, 3, s->color565(16, 20, 32));
            s->drawRoundRect(0, 0, 32, 32, 3, card.badgeColor565);
            char shortBadge[7];
            copySafeCardStr(shortBadge, sizeof(shortBadge), card.badge);
            int sbW = std::max(0, static_cast<int>(countUtf8Glyphs(shortBadge)) * 4 - 1);
            int sbX = std::max(2, (32 - sbW) / 2);
            drawClippedText3x5(s, shortBadge, sbX, 13, 2, 30, card.badgeColor565);
        }
        s->drawFastVLine(33, 1, 30, sepCol);
    }

    int curX = textX;
    if (card.badge[0] != '\0') {
        int bGlyphs = static_cast<int>(countUtf8Glyphs(card.badge));
        int bTextW = std::max(0, bGlyphs * 4 - 1);
        int pillW = std::min(bTextW + 6, std::max(8, maxX - curX));
        s->fillRoundRect(curX, 1, pillW, 7, 2, s->color565(20, 24, 36));
        s->drawRoundRect(curX, 1, pillW, 7, 2, card.badgeColor565);
        drawClippedText3x5(s, card.badge, curX + 3, 2, curX + 2, curX + pillW - 1, card.badgeColor565);
        curX += pillW + 4;
    }

    int ratingStartX = maxX;
    if (card.rating[0] != '\0') {
        int rGlyphs = static_cast<int>(countUtf8Glyphs(card.rating));
        int rw = std::max(0, rGlyphs * 4 - 1);
        int rx = maxX - rw;
        if (rx >= curX) {
            drawClippedText3x5(s, card.rating, rx, 2, curX, maxX, 0xFFE0);
            ratingStartX = rx - 3;
        }
    }

    if (card.year[0] != '\0') {
        int yGlyphs = static_cast<int>(countUtf8Glyphs(card.year));
        int yw = std::max(0, yGlyphs * 4 - 1);
        if (curX + yw <= ratingStartX) {
            drawClippedText3x5(s, card.year, curX, 2, curX, ratingStartX, yearCol);
        }
    }

    drawScrollingLine(s, card.title, textX, maxX, 10, 0xFFFF, titleStepPx);
    s->drawFastHLine(textX, 19, std::max(1, maxX - textX), sepCol);

    if (showSynopsis && card.synopsis[0] != '\0') {
        drawSinglePassSynopsis(s, card.synopsis, textX, maxX, 22, synCol, synScrollPx);
    }
}

} // namespace engines::showcase

