#include "MediaShowcaseEngines.h"
#include "fonts/ArcadeFonts.h"
#include "../core/drawing/IDrawingSurface.h"
#include "../core/ConfigLoader.h"
#include "../core/I18n.h"
#include "../core/Logger.h"
#include "../core/net/SecureHttpClient.h"
#include "../core/net/ShowcaseDiscovery.h"
#include "../hal/HardwareHAL.h"
#include <ArduinoJson.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <new>
#if defined(ESP32)
#include <esp_heap_caps.h>
#if !defined(UNIT_TEST)
#include <WiFi.h>
#include <fcntl.h>
#include <errno.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#endif
#endif

extern ConfigLoader config;

namespace {

uint16_t parseHexColor565(const char* hex, uint16_t fallback) {
    if (!hex) return fallback;
    if (*hex == '#') hex++;
    if (strlen(hex) < 6) return fallback;
    char buf[7];
    memcpy(buf, hex, 6);
    buf[6] = '\0';
    uint32_t rgb = static_cast<uint32_t>(strtoul(buf, nullptr, 16));
    uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((rgb >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(rgb & 0xFF);
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void copySafeStr(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

void appendRawToBuf(char* dst, size_t cap, size_t& pos, const char* str) {
    if (!dst || cap == 0 || !str || pos >= cap - 1) return;
    while (*str != '\0' && pos < cap - 1) {
        dst[pos++] = *str++;
    }
    dst[pos] = '\0';
}

void appendCharToBuf(char* dst, size_t cap, size_t& pos, char c) {
    if (!dst || cap == 0 || pos >= cap - 1) return;
    dst[pos++] = c;
    dst[pos] = '\0';
}

void appendJsonEscapedToBuf(char* dst, size_t cap, size_t& pos, const char* str) {
    if (!dst || cap == 0 || !str) return;
    while (*str != '\0' && pos < cap - 1) {
        char c = *str++;
        if (c == '"' || c == '\\') {
            if (pos + 2 >= cap) break;
            dst[pos++] = '\\';
            dst[pos++] = c;
        } else if (static_cast<uint8_t>(c) >= 0x20) {
            dst[pos++] = c;
        }
    }
    dst[pos] = '\0';
}

void appendUintToBuf(char* dst, size_t cap, size_t& pos, uint32_t val) {
    char num[16];
    snprintf(num, sizeof(num), "%u", static_cast<unsigned>(val));
    appendRawToBuf(dst, cap, pos, num);
}

void appendCsvAsJsonArrayToBuf(char* dst, size_t cap, size_t& pos, const char* csv) {
    appendCharToBuf(dst, cap, pos, '[');
    if (csv && csv[0] != '\0') {
        bool first = true;
        const char* p = csv;
        while (*p != '\0') {
            while (*p == ' ' || *p == ',') ++p;
            if (*p == '\0') break;
            const char* start = p;
            while (*p != '\0' && *p != ',') ++p;
            const char* end = p;
            while (end > start && *(end - 1) == ' ') --end;
            if (end > start) {
                size_t len = static_cast<size_t>(end - start);
                if (!(len == 3 && strncasecmp(start, "all", 3) == 0)) {
                    if (!first) appendCharToBuf(dst, cap, pos, ',');
                    appendCharToBuf(dst, cap, pos, '"');
                    for (size_t i = 0; i < len && pos < cap - 2; ++i) {
                        char c = start[i];
                        if (c != '"' && c != '\\') {
                            dst[pos++] = c;
                        }
                    }
                    dst[pos] = '\0';
                    appendCharToBuf(dst, cap, pos, '"');
                    first = false;
                }
            }
        }
    }
    appendCharToBuf(dst, cap, pos, ']');
}

// ============================================================================
// Flash-Resident UTF-8 Decoder + Clipped 5x7 & 3x5 Pixel Font Renderers
// (Zero heap allocation, strict [clipMinX, clipMaxX) per-column clipping)
// ============================================================================

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
        case 0x0300: // Grave
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
        case 0x0301: // Acute
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
        case 0x0302: // Circumflex
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
        case 0x0303: // Tilde
            if (base == 'n') return 0x00F1;
            if (base == 'N') return 0x00D1;
            if (base == 'a') return 0x00E3;
            if (base == 'o') return 0x00F5;
            if (base == 'A') return 0x00C3;
            if (base == 'O') return 0x00D5;
            break;
        case 0x0308: // Diaeresis
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
        case 0x0327: // Cedilla
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
    if (b0 == 0) return 0;
    ++p;
    if (b0 < 0x80) {
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0) {
        const uint8_t b1 = static_cast<uint8_t>(*p);
        if ((b1 & 0xC0) != 0x80) return '?';
        ++p;
        return (static_cast<uint32_t>(b0 & 0x1F) << 6) | static_cast<uint32_t>(b1 & 0x3F);
    }
    if ((b0 & 0xF0) == 0xE0) {
        const uint8_t b1 = static_cast<uint8_t>(*p);
        if ((b1 & 0xC0) != 0x80) return '?';
        const uint8_t b2 = static_cast<uint8_t>(*(p + 1));
        if ((b2 & 0xC0) != 0x80) {
            ++p;
            return '?';
        }
        p += 2;
        return (static_cast<uint32_t>(b0 & 0x0F) << 12) |
               (static_cast<uint32_t>(b1 & 0x3F) << 6) |
               static_cast<uint32_t>(b2 & 0x3F);
    }
    if ((b0 & 0xF8) == 0xF0) {
        const uint8_t b1 = static_cast<uint8_t>(*p);
        if ((b1 & 0xC0) != 0x80) return '?';
        const uint8_t b2 = static_cast<uint8_t>(*(p + 1));
        if ((b2 & 0xC0) != 0x80) {
            ++p;
            return '?';
        }
        const uint8_t b3 = static_cast<uint8_t>(*(p + 2));
        if ((b3 & 0xC0) != 0x80) {
            p += 2;
            return '?';
        }
        p += 3;
        return (static_cast<uint32_t>(b0 & 0x07) << 18) |
               (static_cast<uint32_t>(b1 & 0x3F) << 12) |
               (static_cast<uint32_t>(b2 & 0x3F) << 6) |
               static_cast<uint32_t>(b3 & 0x3F);
    }
    return '?';
}

uint32_t nextUtf8Codepoint(const char*& p) {
    while (*p != '\0') {
        uint32_t cp = decodeRawUtf8Unit(p);
        if (cp >= 0x0300 && cp <= 0x036F) {
            continue; // Orphan combining mark
        }
        // Fold NFD combining diacritics (e.g. 'e' + U+0301 -> 'é')
        while (*p != '\0') {
            const char* peek = p;
            uint32_t nextCp = decodeRawUtf8Unit(peek);
            if (nextCp >= 0x0300 && nextCp <= 0x036F) {
                cp = foldCombiningAccent(cp, nextCp);
                p = peek;
            } else {
                break;
            }
        }
        return cp;
    }
    return 0;
}

bool mapUnicodeToCp437(uint32_t cp, uint8_t& outIdx) {
    if (cp >= 0x20 && cp <= 0x7E) {
        outIdx = static_cast<uint8_t>(cp);
        return true;
    }
    switch (cp) {
        case 0x00A0: case 0x2009: case 0x202F: outIdx = ' '; return true;
        case 0x2018: case 0x2019: case 0x02BC: case 0x00B4: outIdx = '\''; return true;
        case 0x201C: case 0x201D: case 0x201E: outIdx = '"'; return true;
        case 0x2013: case 0x2014: case 0x2212: outIdx = '-'; return true;
        case 0x00C7: outIdx = 0x80; return true; // Ç
        case 0x00FC: outIdx = 0x81; return true; // ü
        case 0x00E9: outIdx = 0x82; return true; // é
        case 0x00E2: outIdx = 0x83; return true; // â
        case 0x00E4: outIdx = 0x84; return true; // ä
        case 0x00E0: outIdx = 0x85; return true; // à
        case 0x00E3: outIdx = 0x83; return true; // ã -> â-style fallback
        case 0x00E5: outIdx = 0x86; return true; // å
        case 0x00E7: outIdx = 0x87; return true; // ç
        case 0x00EA: outIdx = 0x88; return true; // ê
        case 0x00EB: outIdx = 0x89; return true; // ë
        case 0x00E8: outIdx = 0x8A; return true; // è
        case 0x00EF: outIdx = 0x8B; return true; // ï
        case 0x00EE: outIdx = 0x8C; return true; // î
        case 0x00EC: outIdx = 0x8D; return true; // ì
        case 0x00C4: outIdx = 0x8E; return true; // Ä
        case 0x00C5: outIdx = 0x8F; return true; // Å
        case 0x00C9: outIdx = 0x90; return true; // É
        case 0x00E6: outIdx = 0x91; return true; // æ
        case 0x00C6: outIdx = 0x92; return true; // Æ
        case 0x00F4: outIdx = 0x93; return true; // ô
        case 0x00F6: outIdx = 0x94; return true; // ö
        case 0x00F2: outIdx = 0x95; return true; // ò
        case 0x00F5: outIdx = 0x93; return true; // õ
        case 0x00FB: outIdx = 0x96; return true; // û
        case 0x00F9: outIdx = 0x97; return true; // ù
        case 0x00FF: outIdx = 0x98; return true; // ÿ
        case 0x00D6: outIdx = 0x99; return true; // Ö
        case 0x00DC: outIdx = 0x9A; return true; // Ü
        case 0x00A3: outIdx = 0x9C; return true; // £
        case 0x00A5: outIdx = 0x9D; return true; // ¥
        case 0x00E1: outIdx = 0xA0; return true; // á
        case 0x00ED: outIdx = 0xA1; return true; // í
        case 0x00F3: outIdx = 0xA2; return true; // ó
        case 0x00FA: outIdx = 0xA3; return true; // ú
        case 0x00F1: outIdx = 0xA4; return true; // ñ
        case 0x00D1: outIdx = 0xA5; return true; // Ñ
        case 0x00BF: outIdx = 0xA8; return true; // ¿
        case 0x00A1: outIdx = 0xAD; return true; // ¡
        case 0x00AB: outIdx = 0xAE; return true; // «
        case 0x00BB: outIdx = 0xAF; return true; // »
        case 0x00DF: outIdx = 0xE1; return true; // ß
        case 0x00B0: outIdx = 0xF8; return true; // °
        case 0x2022: case 0x00B7: outIdx = 0xF9; return true; // • / ·
        case 0x00FD: outIdx = 'y'; return true;
        case 0x00DD: outIdx = 'Y'; return true;
        default: return false;
    }
}

void getGlyph5x7Columns(uint32_t cp, uint8_t outCols[5]) {
    for (const auto& g : kCustomGlyphs5x7) {
        if (g.codepoint == cp) {
            memcpy(outCols, g.cols, 5);
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
        if (bits == 0) continue;
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

void drawClippedText5x7(
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
        if (cp == 0) break;
        if (cp == '\r' || cp == '\n' || cp == '\t') {
            cp = ' ';
        }
        if (cp == 0x0153) { // œ -> oe
            getGlyph5x7Columns('o', cols);
            drawSingleGlyph5x7Clipped(s, cols, curX, y, clipMinX, clipMaxX, color);
            curX += 6;
            getGlyph5x7Columns('e', cols);
            drawSingleGlyph5x7Clipped(s, cols, curX, y, clipMinX, clipMaxX, color);
            curX += 6;
            continue;
        }
        if (cp == 0x0152) { // Œ -> OE
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

constexpr uint16_t pack3x5(uint8_t r0, uint8_t r1, uint8_t r2, uint8_t r3, uint8_t r4) {
    return static_cast<uint16_t>((r0 << 12) | (r1 << 9) | (r2 << 6) | (r3 << 3) | r4);
}

// Compact 3x5 uppercase bitmap font (15 bits per glyph: row0 bits 14..12 .. row4 bits 2..0)
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
    if (cp >= 0x20 && cp <= 0x7E) return static_cast<char>(cp);
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

void drawClippedText3x5(
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
        if (cp == 0) break;
        char ch = foldCodepointToAscii3x5(cp);
        uint16_t bmp = getGlyph3x5Bitmap(ch);
        if (bmp != 0 && curX + 3 > clipMinX) {
            for (int row = 0; row < 5; ++row) {
                int py = y + row;
                if (py < 0 || py >= mH) continue;
                uint8_t rowBits = static_cast<uint8_t>((bmp >> ((4 - row) * 3)) & 0x07U);
                if (rowBits == 0) continue;
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

int computeCardTextViewportWidth(uint16_t panelW, uint16_t panelH, bool showPoster) {
    int mW = static_cast<int>(panelW);
    int mH = static_cast<int>(panelH);
    if (mH > mW || mW == mH) {
        return std::max(16, mW - 4);
    }
    if (mH >= 64) {
        uint16_t pw = 48, ph = 64;
        if (showPoster) {
            pw = 48;
            (void)ph;
        }
        int textX = showPoster ? (static_cast<int>(pw) + 4) : 3;
        return std::max(16, (mW - 2) - textX);
    }
    int textX = showPoster ? 35 : 3;
    return std::max(16, (mW - 2) - textX);
}

bool decodeBase64Fixed(const char* b64, uint8_t* dst, size_t expectedBytes) {
    if (!b64 || !dst || expectedBytes == 0) return false;
    size_t outIdx = 0;
    uint32_t buf = 0;
    uint8_t bits = 0;
    for (const char* p = b64; *p != '\0'; ++p) {
        char c = *p;
        if (c == '=') break;
        uint8_t val = 0;
        if (c >= 'A' && c <= 'Z') val = static_cast<uint8_t>(c - 'A');
        else if (c >= 'a' && c <= 'z') val = static_cast<uint8_t>(c - 'a' + 26);
        else if (c >= '0' && c <= '9') val = static_cast<uint8_t>(c - '0' + 52);
        else if (c == '+' || c == '-') val = 62;
        else if (c == '/' || c == '_') val = 63;
        else if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        else return false;

        buf = (buf << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (outIdx < expectedBytes) {
                dst[outIdx++] = static_cast<uint8_t>((buf >> bits) & 0xFF);
            }
        }
    }
    return (outIdx == expectedBytes);
}

bool parseHostAndPort(const char* rawUrl, uint16_t defaultPort, char* outHost, size_t hostCap, uint16_t& outPort) {
    if (!rawUrl || !outHost || hostCap == 0) return false;
    while (*rawUrl == ' ') rawUrl++;
    if (*rawUrl == '\0') return false;

    const char* p = rawUrl;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "mqtt://", 7) == 0) p += 7;
    else if (strncmp(p, "tcp://", 6) == 0) p += 6;

    const char* slash = strchr(p, '/');
    size_t authLen = slash ? static_cast<size_t>(slash - p) : strlen(p);
    if (authLen == 0) return false;

    char auth[96];
    size_t copyLen = (authLen < sizeof(auth) - 1) ? authLen : (sizeof(auth) - 1);
    memcpy(auth, p, copyLen);
    auth[copyLen] = '\0';

    char* colon = strrchr(auth, ':');
    outPort = defaultPort;
    if (colon) {
        *colon = '\0';
        int parsed = atoi(colon + 1);
        if (parsed > 0 && parsed <= 65535) {
            outPort = static_cast<uint16_t>(parsed);
        }
    }
    if (auth[0] == '\0') return false;
    copySafeStr(outHost, hostCap, auth);
    return true;
}

bool readExactFromStream(Stream& st, uint8_t* dst, size_t len, const std::atomic<bool>& wantActive, uint32_t timeoutMs = 2500) {
    size_t got = 0;
    uint32_t lastProgress = millis();
    while (got < len) {
        if (!wantActive.load(std::memory_order_acquire)) return false;
        int avail = st.available();
        if (avail > 0) {
            size_t toRead = len - got;
            if (static_cast<size_t>(avail) < toRead) toRead = static_cast<size_t>(avail);
            size_t r = st.readBytes(dst + got, toRead);
            if (r > 0) {
                got += r;
                lastProgress = millis();
                continue;
            }
        }
        int c = st.read();
        if (c >= 0) {
            dst[got++] = static_cast<uint8_t>(c);
            lastProgress = millis();
        } else {
            if (millis() - lastProgress > timeoutMs) return false;
#if defined(ESP32)
            vTaskDelay(pdMS_TO_TICKS(2));
#endif
        }
    }
    return true;
}

bool drainFromStream(Stream& st, size_t len, const std::atomic<bool>& wantActive, uint32_t timeoutMs = 1500) {
    uint8_t sink[64];
    size_t rem = len;
    while (rem > 0) {
        size_t chunk = (rem < sizeof(sink)) ? rem : sizeof(sink);
        if (!readExactFromStream(st, sink, chunk, wantActive, timeoutMs)) {
            return false;
        }
        rem -= chunk;
    }
    return true;
}

#if defined(ESP32) && !defined(UNIT_TEST)
size_t encodeMqttRemainingLength(uint8_t* out, size_t remLen) {
    size_t idx = 0;
    do {
        uint8_t enc = static_cast<uint8_t>(remLen % 128);
        remLen /= 128;
        if (remLen > 0) enc |= 0x80;
        out[idx++] = enc;
    } while (remLen > 0 && idx < 4);
    return idx;
}

// Writes all `len` bytes to `cli`, retrying cleanly across transient LwIP `EAGAIN` (`errno: 11`)
// TCP send-buffer backpressure instead of dropping control packets silently.
bool writeAllToClient(
    WiFiClient& cli,
    const uint8_t* data,
    size_t len,
    const std::atomic<bool>& wantActive,
    uint32_t timeoutMs = 1200
) {
    if (!data || len == 0) return true;
    size_t sent = 0;
    uint32_t lastProgress = millis();
    while (sent < len) {
        if (!wantActive.load(std::memory_order_acquire) || !cli.connected()) {
            return false;
        }
        size_t n = cli.write(data + sent, len - sent);
        if (n > 0) {
            sent += n;
            lastProgress = millis();
        } else {
            if ((millis() - lastProgress) >= timeoutMs) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    return true;
}

bool sendMqttPublishPacket(
    WiFiClient& cli,
    const char* topic,
    const char* payload,
    bool retain,
    const std::atomic<bool>& wantActive,
    char* ioScratch = nullptr,
    size_t scratchCap = 0
) {
    if (!cli.connected() || !topic || !payload) return false;
    uint16_t tLen = static_cast<uint16_t>(strlen(topic));
    size_t pLen = strlen(payload);
    size_t remLen = 2 + tLen + pLen;
    uint8_t hdr[8];
    hdr[0] = static_cast<uint8_t>(0x30 | (retain ? 0x01 : 0x00));
    size_t rlBytes = encodeMqttRemainingLength(hdr + 1, remLen);
    size_t hdrTotal = 1 + rlBytes + 2 + tLen;
    size_t totalLen = hdrTotal + pLen;

    // Coalesce MQTT fixed header + topic + payload into a single TCP segment (1 LwIP pbuf)
    if (ioScratch != nullptr && totalLen <= scratchCap) {
        if (pLen > 0) {
            memmove(ioScratch + hdrTotal, payload, pLen);
        }
        memcpy(ioScratch, hdr, 1 + rlBytes);
        ioScratch[1 + rlBytes] = static_cast<uint8_t>((tLen >> 8) & 0xFF);
        ioScratch[1 + rlBytes + 1] = static_cast<uint8_t>(tLen & 0xFF);
        memcpy(ioScratch + 1 + rlBytes + 2, topic, tLen);
        return writeAllToClient(cli, reinterpret_cast<const uint8_t*>(ioScratch), totalLen, wantActive);
    }

    uint8_t hdrBuf[136];
    if (hdrTotal <= sizeof(hdrBuf)) {
        memcpy(hdrBuf, hdr, 1 + rlBytes);
        hdrBuf[1 + rlBytes] = static_cast<uint8_t>((tLen >> 8) & 0xFF);
        hdrBuf[1 + rlBytes + 1] = static_cast<uint8_t>(tLen & 0xFF);
        memcpy(hdrBuf + 1 + rlBytes + 2, topic, tLen);
        if (!writeAllToClient(cli, hdrBuf, hdrTotal, wantActive)) return false;
    } else {
        uint8_t tLenBe[2] = {static_cast<uint8_t>((tLen >> 8) & 0xFF), static_cast<uint8_t>(tLen & 0xFF)};
        if (!writeAllToClient(cli, hdr, 1 + rlBytes, wantActive)) return false;
        if (!writeAllToClient(cli, tLenBe, 2, wantActive)) return false;
        if (!writeAllToClient(cli, reinterpret_cast<const uint8_t*>(topic), tLen, wantActive)) return false;
    }
    return (pLen == 0) || writeAllToClient(cli, reinterpret_cast<const uint8_t*>(payload), pLen, wantActive);
}

bool sendMqttSubscribePacket(WiFiClient& cli, uint16_t pktId, const char* topic, const std::atomic<bool>& wantActive) {
    if (!cli.connected() || !topic) return false;
    uint16_t tLen = static_cast<uint16_t>(strlen(topic));
    size_t remLen = 2 + 2 + tLen + 1;
    uint8_t pkt[144];
    pkt[0] = 0x82;
    size_t rlBytes = encodeMqttRemainingLength(pkt + 1, remLen);
    size_t totalLen = 1 + rlBytes + remLen;
    if (totalLen > sizeof(pkt)) return false;
    size_t off = 1 + rlBytes;
    pkt[off++] = static_cast<uint8_t>((pktId >> 8) & 0xFF);
    pkt[off++] = static_cast<uint8_t>(pktId & 0xFF);
    pkt[off++] = static_cast<uint8_t>((tLen >> 8) & 0xFF);
    pkt[off++] = static_cast<uint8_t>(tLen & 0xFF);
    memcpy(pkt + off, topic, tLen);
    off += tLen;
    pkt[off++] = 0; // QoS 0
    return writeAllToClient(cli, pkt, off, wantActive);
}

bool sendMqttUnsubscribePacket(WiFiClient& cli, uint16_t pktId, const char* topic, const std::atomic<bool>& wantActive) {
    if (!cli.connected() || !topic) return false;
    uint16_t tLen = static_cast<uint16_t>(strlen(topic));
    size_t remLen = 2 + 2 + tLen;
    uint8_t pkt[144];
    pkt[0] = 0xA2; // MQTT 3.1.1 §3.10 UNSUBSCRIBE (reserved flags 0010 mandatory)
    size_t rlBytes = encodeMqttRemainingLength(pkt + 1, remLen);
    size_t totalLen = 1 + rlBytes + remLen;
    if (totalLen > sizeof(pkt)) return false;
    size_t off = 1 + rlBytes;
    pkt[off++] = static_cast<uint8_t>((pktId >> 8) & 0xFF);
    pkt[off++] = static_cast<uint8_t>(pktId & 0xFF);
    pkt[off++] = static_cast<uint8_t>((tLen >> 8) & 0xFF);
    pkt[off++] = static_cast<uint8_t>(tLen & 0xFF);
    memcpy(pkt + off, topic, tLen);
    off += tLen;
    return writeAllToClient(cli, pkt, off, wantActive);
}

bool sendMqttConnectPacket(WiFiClient& cli, const char* clientId, const std::atomic<bool>& wantActive) {
    if (!clientId) return false;
    uint16_t cidLen = static_cast<uint16_t>(strlen(clientId));
    size_t remLen = 10 + 2 + cidLen;
    uint8_t pkt[128];
    pkt[0] = 0x10;
    size_t rlBytes = encodeMqttRemainingLength(pkt + 1, remLen);
    size_t totalLen = 1 + rlBytes + remLen;
    if (totalLen > sizeof(pkt)) return false;
    // Keepalive = 60 seconds (0x00, 0x3C) paired with active 10s PINGREQ heartbeats
    const uint8_t varHdr[10] = {0x00, 0x04, 'M', 'Q', 'T', 'T', 0x04, 0x02, 0x00, 0x3C};
    size_t off = 1 + rlBytes;
    memcpy(pkt + off, varHdr, sizeof(varHdr));
    off += sizeof(varHdr);
    pkt[off++] = static_cast<uint8_t>((cidLen >> 8) & 0xFF);
    pkt[off++] = static_cast<uint8_t>(cidLen & 0xFF);
    memcpy(pkt + off, clientId, cidLen);
    off += cidLen;
    return writeAllToClient(cli, pkt, off, wantActive);
}

void sendMqttDisconnectAndStop(WiFiClient& cli, const std::atomic<bool>& wantActive) {
    if (cli.connected()) {
        const uint8_t discPkt[2] = {0xE0, 0x00};
        writeAllToClient(cli, discPkt, sizeof(discPkt), wantActive, 300);
    }
    cli.stop();
}

bool connectMqttBrokerSession(
    WiFiClient& cli,
    const char* mqttHost,
    uint16_t mqttPort,
    const char* sessionId,
    uint32_t& ioConnSeq,
    const std::atomic<bool>& wantActive,
    uint32_t tcpTimeoutMs = 1500,
    uint32_t connAckTimeoutMs = 900
) {
    if (!wantActive.load(std::memory_order_acquire) || !mqttHost || mqttHost[0] == '\0') {
        return false;
    }
    cli.setTimeout(2);
    if (!cli.connect(mqttHost, mqttPort, tcpTimeoutMs)) {
        return false;
    }
    char mqttConnectId[64];
    snprintf(mqttConnectId, sizeof(mqttConnectId), "%s_%u", sessionId, (++ioConnSeq) % 1000U);
    if (!sendMqttConnectPacket(cli, mqttConnectId, wantActive)) {
        cli.stop();
        return false;
    }
    uint32_t connWait = millis();
    while (cli.available() < 4 && (millis() - connWait) < connAckTimeoutMs) {
        if (!wantActive.load(std::memory_order_acquire)) {
            cli.stop();
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (cli.available() < 4) {
        cli.stop();
        return false;
    }
    uint8_t connAck[4];
    cli.readBytes(connAck, 4);
    if (connAck[0] != 0x20 || connAck[3] != 0x00) {
        cli.stop();
        return false;
    }
    return true;
}

// Ephemeral (< 20 ms LAN) MQTT publish session that opens a TCP socket, publishes a single
// control event (`start_stream` or `stop_stream`), sends DISCONNECT, and closes the socket
// immediately so ZERO TCP sockets coexist with high-rate UDP `DMDF` streaming on ESP32_STD.
// Strictly refuses to allocate/connect when `wantActive` is false (Invariant 15).
bool publishFlashMqttEvent(
    const char* mqttHost,
    uint16_t mqttPort,
    const char* sessionId,
    uint32_t& ioConnSeq,
    const char* topic,
    const char* jsonPayload,
    char* ioScratch,
    size_t scratchCap,
    const std::atomic<bool>& wantActive
) {
    if (!wantActive.load(std::memory_order_acquire) || !topic || !jsonPayload) {
        return false;
    }
    WiFiClient flashCli;
    if (!connectMqttBrokerSession(flashCli, mqttHost, mqttPort, sessionId, ioConnSeq, wantActive, 1000, 600)) {
        return false;
    }
    bool ok = sendMqttPublishPacket(flashCli, topic, jsonPayload, false, wantActive, ioScratch, scratchCap);
    sendMqttDisconnectAndStop(flashCli, wantActive);
    return ok;
}

int openNonBlockingUdpSocket(uint16_t localPort, bool hasPsram) {
    int fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;

    int reuse = 1;
    lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_RCVBUF
    int rcvBuf = hasPsram ? 65536 : 8192;
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(rcvBuf));
#else
    (void)hasPsram;
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(localPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (lwip_bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        lwip_close(fd);
        return -1;
    }

    int flags = lwip_fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return fd;
}

void closeUdpSocket(int& fd) {
    if (fd >= 0) {
        lwip_close(fd);
        fd = -1;
    }
}
#endif

} // namespace

size_t MediaShowcaseBase::countUtf8Glyphs(const char* utf8) {
    if (!utf8 || utf8[0] == '\0') return 0;
    size_t count = 0;
    const char* p = utf8;
    while (*p != '\0') {
        uint32_t cp = nextUtf8Codepoint(p);
        if (cp == 0) break;
        if (cp == 0x0152 || cp == 0x0153) {
            count += 2; // Œ / œ ligatures expand to 2 glyphs
        } else {
            count += 1;
        }
    }
    return count;
}

MediaShowcaseBase::MediaShowcaseBase(ShowcaseDomainId domain) : m_domain(domain) {
    const char* slug = domainSlug();
    net::ShowcaseDiscovery::formatClientId(slug, m_width, m_height, false, m_sessionId, sizeof(m_sessionId));
    m_liveCard.badgeColor565 = defaultAccent565();
    m_liveCard.hasData = false;
    m_renderCard = m_liveCard;
}

MediaShowcaseBase::~MediaShowcaseBase() {
    shutdownForDestruction();
}

const char* MediaShowcaseBase::domainSlug() const {
    switch (m_domain) {
        case ShowcaseDomainId::Games:  return "games";
        case ShowcaseDomainId::Movies: return "movies";
        case ShowcaseDomainId::Series: return "series";
        case ShowcaseDomainId::Anime:  return "anime";
        default:                       return "games";
    }
}

const char* MediaShowcaseBase::domainBadgeLabel() const {
    switch (m_domain) {
        case ShowcaseDomainId::Games:  return "RETRO GAMES";
        case ShowcaseDomainId::Movies: return "MOVIES";
        case ShowcaseDomainId::Series: return "TV SERIES";
        case ShowcaseDomainId::Anime:  return "ANIME";
        default:                       return "SHOWCASE";
    }
}

uint16_t MediaShowcaseBase::defaultAccent565() const {
    switch (m_domain) {
        case ShowcaseDomainId::Games:  return 0xFD20; // Arcade Gold/Orange
        case ShowcaseDomainId::Movies: return 0x07FF; // Cinema Cyan
        case ShowcaseDomainId::Series: return 0xF945; // Series Crimson
        case ShowcaseDomainId::Anime:  return 0xF81F; // Anime Magenta
        default:                       return 0xFD20;
    }
}

uint32_t MediaShowcaseBase::synopsisPxPerSec(const char* speedStr) {
    if (!speedStr || speedStr[0] == '\0') return 32U;
    if (strcmp(speedStr, "1") == 0) return 14U;
    if (strcmp(speedStr, "2") == 0 || strcasecmp(speedStr, "slow") == 0) return 18U;
    if (strcmp(speedStr, "3") == 0) return 24U;
    if (strcmp(speedStr, "4") == 0 || strcasecmp(speedStr, "normal") == 0) return 32U;
    if (strcmp(speedStr, "5") == 0) return 42U;
    if (strcmp(speedStr, "6") == 0 || strcasecmp(speedStr, "fast") == 0) return 56U;
    if (strcmp(speedStr, "7") == 0) return 75U;
    if (strcmp(speedStr, "8") == 0) return 100U;
    if (strcmp(speedStr, "9") == 0) return 130U;
    if (strcmp(speedStr, "10") == 0) return 160U;
    return 32U;
}

void MediaShowcaseBase::computeCardPosterDims(uint16_t panelW, uint16_t panelH, uint16_t& outW, uint16_t& outH) {
    if (panelH > panelW) {
        uint16_t side = (panelW >= 64) ? 40 : 24;
        outW = side;
        outH = side;
    } else if (panelW == panelH) {
        outW = 32;
        outH = 32;
    } else if (panelH >= 64) {
        outW = 48;
        outH = 64;
    } else {
        outW = 32;
        outH = 32;
    }
}

bool MediaShowcaseBase::resolveMqttBroker(const ConfigSlot& cfg, char* outHost, size_t hostCap, uint16_t& outPort) {
    uint16_t showcasePort = 8890;
    char showcaseHost[64] = "";
    bool hasServerUrl = parseHostAndPort(cfg.serverUrl, 8890, showcaseHost, sizeof(showcaseHost), showcasePort);

    if (cfg.useShowcaseBroker) {
        if (!hasServerUrl) {
            return false;
        }
        copySafeStr(outHost, hostCap, showcaseHost);
        if (!net::ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, showcasePort, true)) {
            return false;
        }
        outPort = 1883;
        return true;
    }
    if (strcasecmp(cfg.mqttBrokerSource, "custom") == 0 && cfg.mqttCustomUrl[0] != '\0') {
        if (parseHostAndPort(cfg.mqttCustomUrl, 1883, outHost, hostCap, outPort)) {
            if (net::ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, showcasePort, true)) {
                return true;
            }
            return false;
        }
    }
    {
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        const auto& snap = guard.get();
        if (snap.mqtt.broker.length() > 0) {
            copySafeStr(outHost, hostCap, snap.mqtt.broker.c_str());
            if (!net::ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, showcasePort, true)) {
                return false;
            }
            outPort = (snap.mqtt.port > 0 && snap.mqtt.port <= 65535)
                ? static_cast<uint16_t>(snap.mqtt.port)
                : 1883;
            return true;
        }
    }
    if (hasServerUrl) {
        copySafeStr(outHost, hostCap, showcaseHost);
        if (!net::ShowcaseDiscovery::resolveHostInPlace(outHost, hostCap, showcasePort, true)) {
            return false;
        }
        outPort = 1883;
        return true;
    }
    return false;
}

bool MediaShowcaseBase::allocateBuffers() {
    freeBuffers();
    uint16_t posterW = 32;
    uint16_t posterH = 32;
    computeCardPosterDims(m_width, m_height, posterW, posterH);
    size_t posterBytes = static_cast<size_t>(posterW) * static_cast<size_t>(posterH) * sizeof(uint16_t);
    if (posterBytes == 0) posterBytes = 32U * 32U * sizeof(uint16_t);

    m_framePixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    size_t frameBytes = m_framePixels * sizeof(uint16_t);

#if defined(ESP32)
    m_hasPsram = hardwareHAL.capabilities().hasPsram;
    if (m_hasPsram) {
        // ESP32-S3 with PSRAM: place large poster, HD RGB565 frame (up to 256x64 = 32 KB),
        // and 32 KB MQTT/HTTP receive buffer in SPIRAM while keeping 1,280 B I/O scratch in fast SRAM.
        m_poster565 = static_cast<uint16_t*>(heap_caps_malloc(posterBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        m_frame565 = static_cast<uint16_t*>(heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        m_psramMqttRx = static_cast<char*>(heap_caps_malloc(PSRAM_MQTT_RX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (!m_poster565) {
        m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
    }
    m_ioScratch = static_cast<char*>(malloc(IO_SCRATCH_BYTES));
    if (!m_frame565) {
        if (m_hasPsram) {
            m_frame565 = static_cast<uint16_t*>(malloc(frameBytes));
        } else if (frameBytes > 0 && frameBytes <= 8192U &&
                   ESP.getFreeHeap() >= (frameBytes + 12288U) &&
                   ESP.getMaxAllocHeap() >= frameBytes) {
            // Classic ESP32 (`128x32` @ full 8-bit HUB75 DMA): Showcase uses zero TLS (plain LAN
            // MQTT/UDP/HTTP) and preallocates all buffers once at activate(), so >= 12 KB post-frame
            // free SRAM safely sustains LwIP, MQTT, UDP DMDF streaming, and AsyncWebServer.
            m_frame565 = static_cast<uint16_t*>(malloc(frameBytes));
        }
    }
#else
    m_hasPsram = true;
    m_poster565 = static_cast<uint16_t*>(malloc(posterBytes));
    m_ioScratch = static_cast<char*>(malloc(IO_SCRATCH_BYTES));
    m_frame565 = static_cast<uint16_t*>(malloc(frameBytes));
    m_psramMqttRx = static_cast<char*>(malloc(PSRAM_MQTT_RX_BYTES));
#endif

    if (m_poster565) {
        m_posterCapBytes = posterBytes;
        memset(m_poster565, 0, posterBytes);
    } else {
        m_posterCapBytes = 0;
    }
    if (m_ioScratch) {
        m_ioScratch[0] = '\0';
    }
    if (m_frame565) {
        memset(m_frame565, 0, frameBytes);
    }
    if (m_psramMqttRx) {
        m_psramMqttRx[0] = '\0';
    }
    return (m_poster565 != nullptr);
}

void MediaShowcaseBase::freeBuffers() {
    // Free in LIFO order so adjacent internal heap blocks coalesce cleanly on ESP32_STD
    if (m_psramMqttRx) {
        free(m_psramMqttRx);
        m_psramMqttRx = nullptr;
    }
    if (m_frame565) {
        free(m_frame565);
        m_frame565 = nullptr;
    }
    if (m_ioScratch) {
        free(m_ioScratch);
        m_ioScratch = nullptr;
    }
    if (m_poster565) {
        free(m_poster565);
        m_poster565 = nullptr;
    }
    m_posterCapBytes = 0;
    m_framePixels = 0;
}

void MediaShowcaseBase::clearLiveCardState() {
    m_seq.fetch_add(1, std::memory_order_acq_rel);
    m_liveCard.hasData = false;
    m_liveCard.hasPoster = false;
    m_liveCard.hasVideoFrame = false;
    m_seq.fetch_add(1, std::memory_order_release);
    if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
        m_frameSeq.fetch_add(1, std::memory_order_release);
    }
    m_consumedFrameSeq.store(m_frameSeq.load(std::memory_order_relaxed), std::memory_order_release);
    m_renderCard.hasData = false;
    m_renderCard.hasPoster = false;
    m_renderCard.hasVideoFrame = false;
}

EngineError MediaShowcaseBase::initialize(EngineContext* context, const EngineConfig* config) {
    if (!context || !context->getSurface()) {
        return EngineError::InitializationFailed;
    }
    m_surface = context->getSurface();
    DisplayGeometry geom = context->getGeometry();
    m_width = geom.width > 0 ? geom.width : 128;
    m_height = geom.height > 0 ? geom.height : 32;
    net::ShowcaseDiscovery::formatClientId(domainSlug(), m_width, m_height, false, m_sessionId, sizeof(m_sessionId));

    if (config) {
        onConfigChanged(config);
    }
    // Defer transient buffer allocation to activate() after any pipeline transition completes.
    return EngineError::OK;
}

void MediaShowcaseBase::copySafeCsvTokens(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    if (!src || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len < cap) {
        memcpy(dst, src, len + 1);
        return;
    }
    size_t maxCopy = cap - 1;
    size_t cutPos = 0;
    bool foundComma = false;
    for (size_t i = 0; i < maxCopy; ++i) {
        if (src[i] == ',') {
            cutPos = i;
            foundComma = true;
        }
    }
    if (foundComma) {
        while (cutPos > 0 && (src[cutPos - 1] == ' ' || src[cutPos - 1] == ',')) {
            --cutPos;
        }
        memcpy(dst, src, cutPos);
        dst[cutPos] = '\0';
    } else {
        memcpy(dst, src, maxCopy);
        dst[maxCopy] = '\0';
    }
}

void MediaShowcaseBase::onConfigChanged(const EngineConfig* config) {
    if (!config) return;
    uint8_t cur = m_cfgIdx.load(std::memory_order_relaxed);
    uint8_t next = (cur + 1) & 1;
    ConfigSlot& slot = m_cfg[next];

    // Read unified system showcase configuration via zero-copy snapshot guard
    {
        ConfigSnapshotGuard guard = ::config.acquireSnapshot();
        const auto& snap = guard.get();
        const auto& sc = snap.showcase;

        slot.enabled = sc.enabled;
        String srv = sc.server_url;
        srv.trim();
        if (srv.isEmpty()) {
            // Backward-compatible fallback to per-instance config if system showcase not set
            srv = config->getString("server_url", "http://dmdmediashowcase.local:8890");
        }
        srv = normalizeMdnsEndpointUrl(srv, true);
        copySafeStr(slot.serverUrl, sizeof(slot.serverUrl), srv.c_str());

        String tMode = sc.transport_mode.isEmpty() ? config->getString("transport_mode", "mqtt_udp") : sc.transport_mode;
        copySafeStr(slot.transportMode, sizeof(slot.transportMode), tMode.c_str());

        slot.useShowcaseBroker = sc.server_url.isEmpty() ? config->getBool("use_showcase_broker", true) : sc.use_showcase_broker;

        String bSource = sc.mqtt_broker_source.isEmpty() ? config->getString("mqtt_broker_source", "global") : sc.mqtt_broker_source;
        copySafeStr(slot.mqttBrokerSource, sizeof(slot.mqttBrokerSource), bSource.c_str());

        String cUrl = sc.mqtt_custom_url.isEmpty() ? config->getString("mqtt_custom_url", "") : sc.mqtt_custom_url;
        cUrl = normalizeMdnsEndpointUrl(cUrl, false);
        copySafeStr(slot.mqttCustomUrl, sizeof(slot.mqttCustomUrl), cUrl.c_str());
    }

    copySafeStr(slot.lang, sizeof(slot.lang), config->getString("lang", "system").c_str());
    copySafeStr(slot.textScrollSpeed, sizeof(slot.textScrollSpeed), config->getString("text_scroll_speed", "4").c_str());
    {
        String rawVm = config->getString("video_mode", "auto_action");
        rawVm.trim();
        rawVm.toLowerCase();
        const char* normVm = "auto_action";
        if (rawVm == "panorama") {
            normVm = "panorama";
        } else if (rawVm == "center_crop") {
            normVm = "center_crop";
        }
        copySafeStr(slot.videoMode, sizeof(slot.videoMode), normVm);
    }
    copySafeCsvTokens(slot.systems, sizeof(slot.systems), config->getString("systems", "").c_str());
    copySafeCsvTokens(slot.genres, sizeof(slot.genres), config->getString("genres", "").c_str());
    copySafeCsvTokens(slot.decades, sizeof(slot.decades), config->getString("decades", "").c_str());
    copySafeStr(slot.videoType, sizeof(slot.videoType), config->getString("video_type", "any").c_str());

    slot.snapshotPanoramaCount = static_cast<uint8_t>(std::max(0, std::min(5, config->getInt("snapshot_panorama_count", 0))));
    slot.videoMaxDurationS = static_cast<uint8_t>(std::max(2, std::min(120, config->getInt("video_max_duration_s", 8))));
    slot.videoFps = static_cast<uint8_t>(std::max(8, std::min(60, config->getInt("video_fps", 20))));

    slot.showPoster = config->getBool("show_poster", true);
    slot.showSynopsis = config->getBool("show_synopsis", true);
    slot.jacketPanorama = config->getBool("jacket_panorama", true);
    slot.marqueePanorama = config->getBool("marquee_panorama", false);
    slot.showVideo = config->getBool("show_video", true);
    {
        String rawCb = config->getString("color_boost", "auto");
        rawCb.trim();
        rawCb.toLowerCase();
        const char* normCb = "auto";
        if (rawCb == "pixel_art") {
            normCb = "pixel_art";
        } else if (rawCb == "anime") {
            normCb = "anime";
        } else if (rawCb == "cinema" || rawCb == "false" || rawCb == "0" || rawCb == "off") {
            normCb = "cinema";
        }
        copySafeStr(slot.colorBoost, sizeof(slot.colorBoost), normCb);
    }

    uint32_t budget = std::max<uint32_t>(1U, m_rotationBudget.load(std::memory_order_relaxed));
    m_fallbackCycleMs = std::max<uint32_t>(30000U, static_cast<uint32_t>(slot.videoMaxDurationS + 25) * budget * 1000U);

    m_cfgIdx.store(next, std::memory_order_release);
    m_cfgGen.fetch_add(1, std::memory_order_acq_rel);
    m_lastPublishedCfgGen = 0;
    m_consecutiveNetFailures = 0;
    m_udpBlockedSkipCounter = 0;
    m_udpBlockedHint = false;
    m_consecutiveWarmSkips.store(0, std::memory_order_release);
    if (m_wantActive.load(std::memory_order_acquire)) {
        m_skipTurnImmediately.store(false, std::memory_order_release);
        m_turnDone.store(false, std::memory_order_release);
        m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
        clearLiveCardState();
        m_lastRenderedCardSeq = 0xFFFFFFFFU;
        m_lastRenderedFrameSeq = 0xFFFFFFFFU;
        m_lastRenderedPhase = 0xFFU;
        m_dirty = true;
        startWorkerTask();
    }
}

void MediaShowcaseBase::activate() {
    m_activatedAtMs = millis();
    m_phaseStartedAtMs = m_activatedAtMs;
    m_lastScrollStepMs = m_activatedAtMs;
    m_consecutiveNetFailures = 0;
    m_itemsPlayedInTurn.store(0, std::memory_order_release);
    m_skipTurnImmediately.store(false, std::memory_order_release);
    m_warmingFallback.store(false, std::memory_order_release);
    m_turnDone.store(false, std::memory_order_release);
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
    // Strict Consume-Once & Zero-Cache: start every turn with a clean slate
    clearLiveCardState();
    m_lastRenderedCardSeq = 0xFFFFFFFFU;
    m_lastRenderedFrameSeq = 0xFFFFFFFFU;
    m_lastRenderedPhase = 0xFFU;
    m_dirty = true;
    m_renderedNewFrame = false;

    size_t wantPixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
    if (!m_poster565 || !m_ioScratch || m_framePixels != wantPixels) {
        allocateBuffers();
    }
    startWorkerTask();
}

void MediaShowcaseBase::deactivate() {
    m_wantActive.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_SHOWCASE);
    stopWorkerTaskBounded(1500);
    clearLiveCardState();
    freeBuffers();
    m_dirty = false;
    m_renderedNewFrame = false;
}

bool MediaShowcaseBase::shutdownForDestruction() {
    m_wantActive.store(false, std::memory_order_release);
    net::SecureHttpClient::abortSessionsOwnedBy(net::OWNER_SHOWCASE);
    stopWorkerTaskBounded(2000);
    clearLiveCardState();
    freeBuffers();
    m_dirty = false;
    m_renderedNewFrame = false;
    return !m_taskAlive.load(std::memory_order_acquire);
}

uint32_t MediaShowcaseBase::configuredMaxStreamMs(const ConfigSlot& cfg) {
    uint32_t videoSec = cfg.showVideo
        ? std::max<uint32_t>(2U, std::min<uint32_t>(120U, static_cast<uint32_t>(cfg.videoMaxDurationS)))
        : 0U;
    uint32_t panCount = (cfg.jacketPanorama ? 1U : 0U)
        + (cfg.marqueePanorama ? 1U : 0U)
        + std::min<uint32_t>(5U, static_cast<uint32_t>(cfg.snapshotPanoramaCount));
    uint32_t totalSec = std::max<uint32_t>(2U, panCount * 4U + videoSec);
    return totalSec * 1000U;
}

uint32_t MediaShowcaseBase::computeTurnSafetyTimeoutMs() const {
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
    uint32_t budget = std::max<uint32_t>(1U, m_rotationBudget.load(std::memory_order_relaxed));
    uint32_t perItemMs = configuredMaxStreamMs(cfg) + 45000U;
    return std::max<uint32_t>(45000U, perItemMs * budget);
}

bool MediaShowcaseBase::isFinished() const {
    if (m_skipTurnImmediately.load(std::memory_order_acquire)) {
        return true;
    }
    if (m_turnDone.load(std::memory_order_acquire)) {
        return true;
    }
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
    if (!cfg.enabled && m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
        return true;
    }
    if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
        return (millis() - m_activatedAtMs) >= computeTurnSafetyTimeoutMs();
    }
    return false;
}

void MediaShowcaseBase::finishTurnItem() {
    clearLiveCardState();
    uint32_t played = m_itemsPlayedInTurn.fetch_add(1, std::memory_order_acq_rel) + 1U;
    uint32_t budget = std::max<uint32_t>(1U, m_rotationBudget.load(std::memory_order_relaxed));
    if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1 && played >= budget) {
        m_turnDone.store(true, std::memory_order_release);
    }
}

void MediaShowcaseBase::startWorkerTask() {
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
    if (!cfg.enabled) {
        m_statusState.store(3, std::memory_order_release);
        if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
            m_skipTurnImmediately.store(true, std::memory_order_release);
        }
        return;
    }
    m_wantActive.store(true, std::memory_order_release);
#if defined(ESP32) && !defined(UNIT_TEST)
    if (m_taskAlive.load(std::memory_order_acquire)) return;
    m_taskAlive.store(true, std::memory_order_release);
    uint32_t stackBytes = m_hasPsram ? 8192U : 6656U;
    BaseType_t ok = xTaskCreatePinnedToCore(
        workerTaskEntry,
        "ShowcaseWkr",
        stackBytes,
        this,
        1,
        &m_task,
        0 // Strictly Core 0
    );
    if (ok != pdPASS) {
        m_taskAlive.store(false, std::memory_order_release);
        m_task = nullptr;
    }
#endif
}

void MediaShowcaseBase::stopWorkerTaskBounded(uint32_t timeoutMs) {
#if defined(ESP32) && !defined(UNIT_TEST)
    uint32_t start = millis();
    while (m_taskAlive.load(std::memory_order_acquire) && (millis() - start) < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    m_task = nullptr;
#else
    (void)timeoutMs;
#endif
}

void MediaShowcaseBase::workerTaskEntry(void* arg) {
    auto* self = static_cast<MediaShowcaseBase*>(arg);
    if (self) {
        self->runWorkerLoop();
        self->m_taskAlive.store(false, std::memory_order_release);
    }
#if defined(ESP32)
    vTaskDelete(nullptr);
#endif
}

void MediaShowcaseBase::runWorkerLoop() {
    while (m_wantActive.load(std::memory_order_acquire)) {
        uint32_t startGen = m_cfgGen.load(std::memory_order_acquire);
        const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
        const char* domain = domainSlug();

        if (!cfg.enabled || strlen(cfg.serverUrl) < 8 || strncmp(cfg.serverUrl, "http", 4) != 0) {
            m_statusState.store(3, std::memory_order_release); // Unconfigured / Disabled
            clearLiveCardState();
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
            return;
        }

        char reqLangBuf[12] = "en";
        if (cfg.lang[0] != '\0' && strcasecmp(cfg.lang, "system") != 0) {
            copySafeStr(reqLangBuf, sizeof(reqLangBuf), cfg.lang);
        } else {
            ConfigSnapshotGuard guard = config.acquireSnapshot();
            const String& sysLang = guard.get().system.lang;
            if (sysLang.length() > 0) {
                copySafeStr(reqLangBuf, sizeof(reqLangBuf), sysLang.c_str());
            }
        }

        net::ShowcaseDiscovery::formatClientId(domain, m_width, m_height, false, m_sessionId, sizeof(m_sessionId));

        bool useMqttMode = (strcasecmp(cfg.transportMode, "mqtt_udp") == 0 ||
                            strcasecmp(cfg.transportMode, "mqtt_tcp") == 0);
        if (useMqttMode) {
            char httpFallbackItemId[MAX_ITEM_ID_LEN] = "";
            bool handled = runMqttUdpTurn(cfg, domain, reqLangBuf, httpFallbackItemId, sizeof(httpFallbackItemId));
            if (handled && httpFallbackItemId[0] != '\0') {
                // Flat-stack HTTP DMDS stream executed AFTER `runMqttUdpTurn()` freed its stack frame,
                // MQTT TCP socket, and UDP socket. Uses `?purge=true&paced=1&drop_late=1` for real-time pacing.
                if (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen) {
                    char fallbackPath[176];
                    snprintf(
                        fallbackPath,
                        sizeof(fallbackPath),
                        "/api/v1/%s/stream/%s.rgb565.stream?purge=true&paced=1&drop_late=1",
                        domain,
                        httpFallbackItemId
                    );
                    streamUnifiedRgb565Http(cfg.serverUrl, fallbackPath);
                }
                finishTurnItem();
            } else if (!handled && m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen) {
                // Stay strictly on `mqtt_udp` instead of cross-protocol ping-ponging into HTTP `/config`.
                // Require 3 consecutive transport failures before displaying `NetworkOffline` (2).
                if (m_consecutiveNetFailures < 10U) {
                    ++m_consecutiveNetFailures;
                }
                if (shouldReportNetworkOffline(m_consecutiveNetFailures)) {
                    m_statusState.store(2, std::memory_order_release);
                    m_lastPublishedCfgGen = 0;
                    clearLiveCardState();
                    if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                        m_skipTurnImmediately.store(true, std::memory_order_release);
                    }
                } else if (m_statusState.load(std::memory_order_acquire) != 0U) {
                    m_statusState.store(1, std::memory_order_release);
                }
#if defined(ESP32)
                for (int w = 0; w < 12 && m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen; ++w) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
#endif
            }
        } else if (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen) {
            runHttpPollTurn(cfg, domain, reqLangBuf);
        }
        if (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) != startGen) {
            m_skipTurnImmediately.store(false, std::memory_order_release);
            m_turnDone.store(false, std::memory_order_release);
            clearLiveCardState();
            continue;
        }
        if (!m_turnDone.load(std::memory_order_acquire) &&
            !m_skipTurnImmediately.load(std::memory_order_acquire) &&
            m_wantActive.load(std::memory_order_acquire)) {
            // More items to play in this turn (or continuous solo mode)
#if defined(ESP32)
            vTaskDelay(pdMS_TO_TICKS(10));
#endif
            continue;
        }
        break;
    }
}

bool MediaShowcaseBase::parseReadyPayloadInPlaceBuf(
    char* jsonBuf,
    bool showPoster,
    char* outItemId,
    size_t itemIdCap,
    bool& outHasStream,
    uint32_t& outStreamMaxMs,
    bool& outExplicitWarming
) {
    outExplicitWarming = false;
    if (outItemId && itemIdCap > 0) outItemId[0] = '\0';
    if (!jsonBuf || jsonBuf[0] == '\0') return false;

    const char* domain = domainSlug();
    CardSnapshot nextCard;
    const char* posterB64 = "";

    {
        // ArduinoJson v6 zero-copy mode: passing mutable `char*` modifies strings in-place
        // inside `jsonBuf` without duplicating `poster_rgb565_b64` or `synopsis`.
        // `JSON_OBJECT_SIZE(44)` adapts automatically to 32-bit ESP32 (704 B stack) and
        // 64-bit native tests (1408 B) while holding 44 slots (> 31 Showcase keys).
        StaticJsonDocument<JSON_OBJECT_SIZE(44)> doc;
        if (deserializeJson(doc, jsonBuf) != DeserializationError::Ok) {
            return false;
        }

        bool rdy = doc["ready"] | false;
        if (!rdy) {
            outExplicitWarming = true;
            return false;
        }

        JsonObject cardObj = doc["card"].as<JsonObject>();
        if (cardObj.isNull()) {
            outExplicitWarming = true;
            return false;
        }

        copySafeStr(nextCard.title, sizeof(nextCard.title), cardObj["title"] | "UNKNOWN");
        copySafeStr(nextCard.year, sizeof(nextCard.year), cardObj["year"] | "");
        const char* badge = cardObj["badge"] | domain;
        copySafeStr(nextCard.badge, sizeof(nextCard.badge), badge);
        copySafeStr(nextCard.rating, sizeof(nextCard.rating), cardObj["rating"] | "");
        copySafeStr(nextCard.synopsis, sizeof(nextCard.synopsis), cardObj["synopsis"] | "");
        nextCard.badgeColor565 = parseHexColor565(cardObj["badge_color"] | nullptr, defaultAccent565());
        computeCardPosterDims(m_width, m_height, nextCard.posterW, nextCard.posterH);
        uint16_t srvPw = cardObj["poster_width"] | 0;
        uint16_t srvPh = cardObj["poster_height"] | 0;
        if (srvPw > 0 && srvPh > 0 && (static_cast<size_t>(srvPw) * static_cast<size_t>(srvPh) * 2U) <= m_posterCapBytes) {
            nextCard.posterW = srvPw;
            nextCard.posterH = srvPh;
        }
        nextCard.hasData = true;
        nextCard.hasPoster = false;
        nextCard.hasVideoFrame = false;

        const char* rawItemId = doc["item_id"] | (cardObj["item_id"] | (cardObj["id"] | ""));
        if (outItemId && itemIdCap > 0) {
            copySafeStr(outItemId, itemIdCap, rawItemId);
        }
        outHasStream = (cardObj["has_panoramas"] | false) || (cardObj["has_video"] | false);
        float totDur = cardObj["stream_duration_s"] | (cardObj["total_stream_duration_s"] | 8.0f);
        if (totDur < 2.0f) totDur = 2.0f;
        outStreamMaxMs = static_cast<uint32_t>((totDur + 4.0f) * 1000.0f);

        // Zero-copy string pointer resides directly inside `jsonBuf` and remains valid after `doc` scope ends
        posterB64 = cardObj["poster_rgb565_b64"] | "";
    }

    size_t expectedPosterBytes = static_cast<size_t>(nextCard.posterW) * static_cast<size_t>(nextCard.posterH) * 2U;
    if (showPoster && !m_poster565 && posterB64[0] != '\0') {
        allocateBuffers();
    }
    if (showPoster && m_poster565 && expectedPosterBytes <= m_posterCapBytes) {
        if (posterB64[0] != '\0' && decodeBase64Fixed(posterB64, reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes)) {
            nextCard.hasPoster = true;
        }
    }

    m_seq.fetch_add(1, std::memory_order_acq_rel);
    m_liveCard = nextCard;
    m_seq.fetch_add(1, std::memory_order_release);
    m_statusState.store(0, std::memory_order_release);
    m_phaseStartedAtMs = millis();
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);
    return true;
}

bool MediaShowcaseBase::parseReadyPayloadInPlace(
    char* jsonBuf,
    bool showPoster,
    String& outItemId,
    bool& outHasStream,
    uint32_t& outStreamMaxMs,
    bool& outExplicitWarming
) {
    char itemBuf[MAX_ITEM_ID_LEN] = "";
    bool ok = parseReadyPayloadInPlaceBuf(
        jsonBuf,
        showPoster,
        itemBuf,
        sizeof(itemBuf),
        outHasStream,
        outStreamMaxMs,
        outExplicitWarming
    );
    outItemId = itemBuf;
    return ok;
}

bool MediaShowcaseBase::runMqttUdpTurn(
    const ConfigSlot& cfg,
    const char* domain,
    const char* reqLang,
    char* outHttpFallbackItemId,
    size_t fallbackItemCap
) {
    if (outHttpFallbackItemId && fallbackItemCap > 0) {
        outHttpFallbackItemId[0] = '\0';
    }
#if defined(ESP32) && !defined(UNIT_TEST)
    if (!m_ioScratch) {
        return false;
    }

    char mqttHost[64] = "";
    uint16_t mqttPort = 1883;
    if (!resolveMqttBroker(cfg, mqttHost, sizeof(mqttHost), mqttPort)) {
        return false;
    }

    uint32_t startGen = m_cfgGen.load(std::memory_order_acquire);
    uint16_t localUdpPort = static_cast<uint16_t>(5568U + static_cast<uint16_t>(m_domain));
    const char* statusTopic = "dmd_showcase/server/status";
    char readyTopic[96];
    char eventsTopic[96];
    snprintf(readyTopic, sizeof(readyTopic), "dmd_showcase/clients/%s/%s/ready", m_sessionId, domain);
    snprintf(eventsTopic, sizeof(eventsTopic), "dmd_showcase/clients/%s/%s/events", m_sessionId, domain);

    IPAddress ipAddr = WiFi.localIP();
    char localIpBuf[20];
    snprintf(localIpBuf, sizeof(localIpBuf), "%u.%u.%u.%u", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);

    uint8_t prevSkips = m_consecutiveWarmSkips.load(std::memory_order_acquire);
    uint32_t playedSoFar = m_itemsPlayedInTurn.load(std::memory_order_acquire);
    bool soloSlot = (m_rotationSlotCount.load(std::memory_order_relaxed) <= 1U);
    uint32_t warmSettleMs = soloSlot
        ? 2200U
        : ((prevSkips >= 1U && playedSoFar == 0U) ? 350U : 0U);
    uint32_t maxWaitMs = soloSlot ? 2600U : 900U;

    bool gotReadyItem = false;
    bool sawReadyFalse = false;
    bool sawServerOnline = false;
    char itemIdBuf[MAX_ITEM_ID_LEN] = "";
    bool itemHasStream = false;
    uint32_t maxCfgStreamMs = configuredMaxStreamMs(cfg);
    uint32_t streamMaxMs = maxCfgStreamMs + 4000U;

    // Ensure Phase::Card is active before reusing `m_frame565` as the zero-malloc MQTT RX buffer on ESP32_STD
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

    // =========================================================================
    // STEP A: Scoped Pre-Stream MQTT Session (Connect -> Retained Read -> Claim Card -> DISCONNECT)
    // Closes and destroys `mqttCli` BEFORE Card scroll and UDP streaming so 0 TCP sockets remain open!
    // =========================================================================
    {
        WiFiClient mqttCli;
        if (!connectMqttBrokerSession(mqttCli, mqttHost, mqttPort, m_sessionId, m_mqttConnSeq, m_wantActive, 1500, 1000)) {
            if (!m_wantActive.load(std::memory_order_acquire)) {
                return true;
            }
            return false;
        }
        m_consecutiveNetFailures = 0;

        // Only publish retained `config` on boot, config mutation, or post-offline recovery
        if (m_lastPublishedCfgGen != startGen) {
            uint16_t posterW = 32;
            uint16_t posterH = 32;
            computeCardPosterDims(m_width, m_height, posterW, posterH);
            uint8_t effFps = resolveEffectiveVideoFps(cfg.videoFps, m_hasPsram);

            char configTopic[80];
            snprintf(configTopic, sizeof(configTopic), "dmd_showcase/clients/%s/config", m_sessionId);

            size_t pos = 0;
            m_ioScratch[0] = '\0';
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "{\"dmd_id\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_sessionId);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"client_ip\":\"");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, localIpBuf);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"udp_ip\":\"");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, localIpBuf);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"width\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_width);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"height\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_height);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"poster_width\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, posterW);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"poster_height\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, posterH);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"udp_port\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, localUdpPort);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"lang\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, reqLang);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"domains\":{\"");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, domain);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\":{\"systems\":");
            appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.systems);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"genres\":");
            appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.genres);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"decades\":");
            appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.decades);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_type\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoType);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"show_poster\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showPoster ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"show_synopsis\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showSynopsis ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"text_scroll_speed\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.textScrollSpeed);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"jacket_panorama\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.jacketPanorama ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"marquee_panorama\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.marqueePanorama ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"snapshot_panorama_count\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.snapshotPanoramaCount);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"show_video\":");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showVideo ? "true" : "false");
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_max_duration_s\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoMaxDurationS);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_fps\":");
            appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, effFps);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_mode\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoMode);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"color_boost\":\"");
            appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.colorBoost);
            appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\"}}}");

            if (sendMqttPublishPacket(mqttCli, configTopic, m_ioScratch, true, m_wantActive, m_ioScratch, IO_SCRATCH_BYTES)) {
                m_lastPublishedCfgGen = startGen;
            }
        }

        sendMqttSubscribePacket(mqttCli, 1, statusTopic, m_wantActive);
        sendMqttSubscribePacket(mqttCli, 2, readyTopic, m_wantActive);

        char* primaryRxBuf = m_psramMqttRx
            ? m_psramMqttRx
            : reinterpret_cast<char*>(m_frame565);
        size_t primaryRxCap = m_psramMqttRx
            ? PSRAM_MQTT_RX_BYTES
            : (m_frame565 ? (m_framePixels * sizeof(uint16_t)) : 0U);

        uint32_t waitReadyStart = millis();
        uint32_t readyFalseAtMs = 0;

        while (m_wantActive.load(std::memory_order_acquire) &&
               m_cfgGen.load(std::memory_order_acquire) == startGen &&
               (millis() - waitReadyStart) < maxWaitMs &&
               !gotReadyItem) {
            if (sawReadyFalse && (millis() - readyFalseAtMs) >= warmSettleMs) {
                break;
            }
            if (mqttCli.available() >= 2) {
                uint8_t fixedHdr = static_cast<uint8_t>(mqttCli.read());
                uint32_t remLen = 0;
                uint32_t mult = 1;
                for (int i = 0; i < 4; ++i) {
                    uint8_t b = 0;
                    if (!readExactFromStream(mqttCli, &b, 1, m_wantActive, 1000)) break;
                    remLen += static_cast<uint32_t>(b & 0x7F) * mult;
                    mult *= 128;
                    if ((b & 0x80) == 0) break;
                }
                const uint32_t maxMqttPacketBytes = m_hasPsram ? 32768U : 14336U;
                if ((fixedHdr & 0xF0) != 0x30 || remLen < 2 || remLen >= maxMqttPacketBytes) {
                    if (remLen > 0) {
                        drainFromStream(mqttCli, remLen, m_wantActive, 1500);
                    }
                    continue;
                }

                uint8_t tLenBe[2];
                if (!readExactFromStream(mqttCli, tLenBe, 2, m_wantActive, 1000)) {
                    break;
                }
                uint16_t tLen = static_cast<uint16_t>((tLenBe[0] << 8) | tLenBe[1]);
                if (static_cast<uint32_t>(tLen) + 2U > remLen) {
                    drainFromStream(mqttCli, remLen - 2U, m_wantActive, 1000);
                    continue;
                }
                char topicBuf[112] = "";
                size_t toReadT = (tLen < sizeof(topicBuf) - 1) ? tLen : (sizeof(topicBuf) - 1);
                if (toReadT > 0) {
                    readExactFromStream(mqttCli, reinterpret_cast<uint8_t*>(topicBuf), toReadT, m_wantActive, 1000);
                }
                topicBuf[toReadT] = '\0';
                if (tLen > toReadT) {
                    drainFromStream(mqttCli, tLen - toReadT, m_wantActive, 1000);
                }
                size_t payloadLen = remLen - 2U - static_cast<size_t>(tLen);

                char* payloadBuf = nullptr;
                bool freePayloadAfter = false;
                if (payloadLen + 1U <= IO_SCRATCH_BYTES) {
                    payloadBuf = m_ioScratch;
                } else if (primaryRxBuf && (payloadLen + 1U) <= primaryRxCap) {
                    payloadBuf = primaryRxBuf;
                } else if (ESP.getMaxAllocHeap() >= (payloadLen + 2048U)) {
                    payloadBuf = static_cast<char*>(malloc(payloadLen + 1U));
                    freePayloadAfter = (payloadBuf != nullptr);
                }
                if (!payloadBuf) {
                    drainFromStream(mqttCli, payloadLen, m_wantActive, 1500);
                    continue;
                }
                if (!readExactFromStream(mqttCli, reinterpret_cast<uint8_t*>(payloadBuf), payloadLen, m_wantActive, 2000)) {
                    if (freePayloadAfter) free(payloadBuf);
                    break;
                }
                payloadBuf[payloadLen] = '\0';

                if (strcmp(topicBuf, statusTopic) == 0) {
                    StaticJsonDocument<192> sDoc;
                    if (deserializeJson(sDoc, payloadBuf) == DeserializationError::Ok) {
                        bool online = sDoc["online"] | false;
                        if (!online) {
                            if (freePayloadAfter) free(payloadBuf);
                            if (payloadBuf == reinterpret_cast<char*>(m_frame565) && m_frame565) {
                                memset(m_frame565, 0, m_framePixels * sizeof(uint16_t));
                            }
                            m_statusState.store(2, std::memory_order_release);
                            m_lastPublishedCfgGen = 0;
                            clearLiveCardState();
                            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                                m_skipTurnImmediately.store(true, std::memory_order_release);
                            }
                            sendMqttDisconnectAndStop(mqttCli, m_wantActive);
                            return true;
                        }
                        sawServerOnline = true;
                    }
                } else if (strcmp(topicBuf, readyTopic) == 0) {
                    bool explicitWarming = false;
                    if (parseReadyPayloadInPlaceBuf(payloadBuf, cfg.showPoster, itemIdBuf, sizeof(itemIdBuf), itemHasStream, streamMaxMs, explicitWarming)) {
                        gotReadyItem = true;
                    } else if (explicitWarming) {
                        m_statusState.store(1, std::memory_order_release);
                        if (!sawReadyFalse) {
                            sawReadyFalse = true;
                            readyFalseAtMs = millis();
                        }
                    }
                }
                if (payloadBuf == reinterpret_cast<char*>(m_frame565) && m_frame565) {
                    memset(m_frame565, 0, m_framePixels * sizeof(uint16_t));
                }
                if (freePayloadAfter) {
                    free(payloadBuf);
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        if (!gotReadyItem) {
            sendMqttDisconnectAndStop(mqttCli, m_wantActive);
            if (!sawReadyFalse) {
                // Broker connected without a retained `ready` message yet (e.g. fresh server start):
                // force `config` re-registration on next turn so Showcase initializes this slot.
                m_lastPublishedCfgGen = 0;
            }
            if (sawReadyFalse || sawServerOnline) {
                m_statusState.store(1, std::memory_order_release);
                if (playedSoFar == 0U && prevSkips < 10U) {
                    m_consecutiveWarmSkips.store(static_cast<uint8_t>(prevSkips + 1U), std::memory_order_release);
                } else if (playedSoFar > 0U) {
                    m_consecutiveWarmSkips.store(0, std::memory_order_release);
                }
                if (!soloSlot) {
                    m_skipTurnImmediately.store(true, std::memory_order_release);
                } else {
                    for (int w = 0; w < 10 && m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen; ++w) {
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                }
                return true;
            }
            return false;
        }

        // Unsubscribe from `readyTopic` BEFORE `start_card`, claim the item, and IMMEDIATELY
        // close the MQTT TCP socket so 0 sockets remain open during Card scroll and UDP streaming!
        sendMqttUnsubscribePacket(mqttCli, 3, readyTopic, m_wantActive);
        char startCardCmd[176];
        snprintf(
            startCardCmd,
            sizeof(startCardCmd),
            "{\"action\":\"start_card\",\"item_id\":\"%s\",\"client_ip\":\"%s\",\"udp_ip\":\"%s\"}",
            itemIdBuf,
            localIpBuf,
            localIpBuf
        );
        sendMqttPublishPacket(mqttCli, eventsTopic, startCardCmd, false, m_wantActive, m_ioScratch, IO_SCRATCH_BYTES);
        sendMqttDisconnectAndStop(mqttCli, m_wantActive);
    }

    uint32_t naturalStreamMs = (streamMaxMs > 4000U) ? (streamMaxMs - 4000U) : 2000U;
    if (streamMaxMs > maxCfgStreamMs + 4500U) {
        streamMaxMs = maxCfgStreamMs + 4500U;
    }
    m_consecutiveWarmSkips.store(0, std::memory_order_release);

    // =========================================================================
    // STEP B: Zero-Socket Organic Card Dwell (Core 1 scrolls full UTF-8 synopsis)
    // =========================================================================
    uint32_t synGlyphs = static_cast<uint32_t>(countUtf8Glyphs(m_liveCard.synopsis));
    uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);
    uint32_t cardWaitMs = 4500U;
    if (cfg.showSynopsis && synGlyphs > 0) {
        uint32_t textW = synGlyphs * 6U;
        uint32_t availW = static_cast<uint32_t>(computeCardTextViewportWidth(m_width, m_height, cfg.showPoster));
        if (textW > availW) {
            uint32_t scrollDistPx = (textW - availW) + (availW / 2U);
            uint32_t synScrollMs = 900U + (scrollDistPx * 1000U) / speedPx + 700U;
            if (synScrollMs > cardWaitMs) cardWaitMs = synScrollMs;
        }
    }
    uint32_t cardT0 = millis();
    while (m_wantActive.load(std::memory_order_acquire) &&
           m_cfgGen.load(std::memory_order_acquire) == startGen &&
           (millis() - cardT0) < cardWaitMs) {
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    // =========================================================================
    // STEP C: Pure UDP Stream (`0` TCP sockets open!) + Ephemeral Flash MQTT Start/Stop
    //         (or instant zero-latency handoff when `mqtt_tcp` or `m_udpBlockedHint` is active)
    // =========================================================================
    if (m_wantActive.load(std::memory_order_acquire) &&
        m_cfgGen.load(std::memory_order_acquire) == startGen &&
        itemHasStream &&
        m_frame565) {
        if (strcasecmp(cfg.transportMode, "mqtt_tcp") == 0) {
            if (outHttpFallbackItemId && fallbackItemCap > 0) {
                copySafeStr(outHttpFallbackItemId, fallbackItemCap, itemIdBuf);
            }
            return true;
        }
        if (m_udpBlockedHint) {
            ++m_udpBlockedSkipCounter;
            if ((m_udpBlockedSkipCounter % 10U) != 0U) {
                if (outHttpFallbackItemId && fallbackItemCap > 0) {
                    copySafeStr(outHttpFallbackItemId, fallbackItemCap, itemIdBuf);
                }
                return true;
            }
        }

        int udpFd = openNonBlockingUdpSocket(localUdpPort, m_hasPsram);
        if (udpFd < 0) {
            if (outHttpFallbackItemId && fallbackItemCap > 0) {
                copySafeStr(outHttpFallbackItemId, fallbackItemCap, itemIdBuf);
            }
            return true;
        }

        constexpr size_t kUdpPktCap = 1440U;
        memset(m_frame565, 0, m_framePixels * sizeof(uint16_t));
        while (lwip_recvfrom(udpFd, m_ioScratch, kUdpPktCap, MSG_DONTWAIT, nullptr, nullptr) > 0) {}

        // Punch a client-initiated UDP return pinhole (`DMDH|<session>|<lan_ip>` -> `<showcase_ip>:5569`)
        // so Docker / Rancher Desktop VM bridges forward `DMDF` datagrams back to `localUdpPort`.
        constexpr uint16_t kShowcaseUdpPunchPort = 5569U;
        struct sockaddr_in punchAddr;
        memset(&punchAddr, 0, sizeof(punchAddr));
        punchAddr.sin_family = AF_INET;
        punchAddr.sin_port = htons(kShowcaseUdpPunchPort);
        bool hasPunchTarget = false;
        {
            char showcaseHost[64] = "";
            uint16_t showcasePort = 8890;
            if (parseHostAndPort(cfg.serverUrl, 8890, showcaseHost, sizeof(showcaseHost), showcasePort) &&
                net::ShowcaseDiscovery::resolveHostInPlace(showcaseHost, sizeof(showcaseHost), showcasePort, false) &&
                inet_aton(showcaseHost, &punchAddr.sin_addr) != 0) {
                hasPunchTarget = true;
            } else if (inet_aton(mqttHost, &punchAddr.sin_addr) != 0) {
                hasPunchTarget = true;
            }
        }
        char helloPkt[96];
        int helloLen = snprintf(helloPkt, sizeof(helloPkt), "DMDH|%s|%s", m_sessionId, localIpBuf);
        auto sendUdpPunch = [&]() {
            if (hasPunchTarget && helloLen > 0) {
                lwip_sendto(
                    udpFd,
                    helloPkt,
                    static_cast<size_t>(helloLen),
                    0,
                    reinterpret_cast<const struct sockaddr*>(&punchAddr),
                    sizeof(punchAddr)
                );
            }
        };

        sendUdpPunch();
        if (m_udpBlockedHint) {
            // Periodic 65ms re-probe (every 10th item) when container NAT previously blocked UDP:
            // upgrades back to UDP immediately if `:5569/udp` responds with `DMDA`, otherwise
            // switches straight to zero-heap TCP streaming without a 1.2s delay or extra MQTT calls!
            bool gotPunchAck = false;
            if (hasPunchTarget) {
                fd_set pfds;
                FD_ZERO(&pfds);
                FD_SET(udpFd, &pfds);
                struct timeval ptv = {0, 65000};
                if (lwip_select(udpFd + 1, &pfds, nullptr, nullptr, &ptv) > 0 && FD_ISSET(udpFd, &pfds)) {
                    while (true) {
                        int pLen = lwip_recvfrom(udpFd, m_ioScratch, kUdpPktCap, MSG_DONTWAIT, nullptr, nullptr);
                        if (pLen <= 0) break;
                        if (pLen >= 4 && (memcmp(m_ioScratch, "DMDA", 4) == 0 || memcmp(m_ioScratch, "DMDF", 4) == 0)) {
                            gotPunchAck = true;
                        }
                    }
                }
            }
            if (gotPunchAck) {
                m_udpBlockedHint = false;
                m_udpBlockedSkipCounter = 0;
            } else {
                closeUdpSocket(udpFd);
                if (m_wantActive.load(std::memory_order_acquire) && outHttpFallbackItemId && fallbackItemCap > 0) {
                    copySafeStr(outHttpFallbackItemId, fallbackItemCap, itemIdBuf);
                }
                return true;
            }
        }

        // Flash MQTT `start_stream`: connects, publishes, and closes TCP BEFORE UDP frames flow!
        {
            char startStreamCmd[192];
            snprintf(
                startStreamCmd,
                sizeof(startStreamCmd),
                "{\"action\":\"start_stream\",\"item_id\":\"%s\",\"udp_port\":%u,\"client_ip\":\"%s\",\"udp_ip\":\"%s\"}",
                itemIdBuf,
                static_cast<unsigned>(localUdpPort),
                localIpBuf,
                localIpBuf
            );
            if (!publishFlashMqttEvent(
                    mqttHost,
                    mqttPort,
                    m_sessionId,
                    m_mqttConnSeq,
                    eventsTopic,
                    startStreamCmd,
                    m_ioScratch,
                    IO_SCRATCH_BYTES,
                    m_wantActive)) {
                closeUdpSocket(udpFd);
                if (m_wantActive.load(std::memory_order_acquire) && outHttpFallbackItemId && fallbackItemCap > 0) {
                    copySafeStr(outHttpFallbackItemId, fallbackItemCap, itemIdBuf);
                }
                return true;
            }
        }
        sendUdpPunch();

        m_presentedStreamFrames.store(0, std::memory_order_release);
        uint32_t streamT0 = millis();
        uint32_t firstUdpPktMs = 0;
        uint32_t lastUdpPktMs = 0;
        uint32_t frameChunkStartMs = 0;
        uint32_t udpFramesCompleted = 0;
        bool resentPunch = false;
        uint16_t curFrameSeq = 0xFFFF;
        uint64_t chunkMask = 0;
        size_t maxBufBytes = m_framePixels * sizeof(uint16_t);
        constexpr uint32_t kUdpInitialGraceMs = 1200U;
        uint8_t* udpBuf = reinterpret_cast<uint8_t*>(m_ioScratch);
        bool stoppedEarlyByClient = false;
        bool initialGraceTimeout = false;

        auto markFrameWriteBegin = [&]() {
            if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) == 0U) {
                m_frameSeq.fetch_add(1, std::memory_order_acq_rel);
                frameChunkStartMs = millis();
            }
        };
        auto markFrameWriteComplete = [&]() {
            if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
                m_frameSeq.fetch_add(1, std::memory_order_release);
            }
            ++udpFramesCompleted;
            if (!m_liveCard.hasVideoFrame) {
                m_seq.fetch_add(1, std::memory_order_acq_rel);
                m_liveCard.hasVideoFrame = true;
                m_seq.fetch_add(1, std::memory_order_release);
            }
            if (m_phase.load(std::memory_order_relaxed) != static_cast<uint8_t>(Phase::LiveStream)) {
                m_phase.store(static_cast<uint8_t>(Phase::LiveStream), std::memory_order_release);
            }
        };

        while (m_wantActive.load(std::memory_order_acquire) &&
               m_cfgGen.load(std::memory_order_acquire) == startGen &&
               (millis() - streamT0) < streamMaxMs) {
            if (firstUdpPktMs > 0U && (millis() - firstUdpPktMs) >= maxCfgStreamMs) {
                stoppedEarlyByClient = (maxCfgStreamMs + 500U < naturalStreamMs);
                break;
            }
            if (firstUdpPktMs == 0U && !resentPunch && (millis() - streamT0) >= 250U) {
                resentPunch = true;
                sendUdpPunch();
            }

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(udpFd, &rfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 4000;

            int sel = lwip_select(udpFd + 1, &rfds, nullptr, nullptr, &tv);
            if (sel > 0 && FD_ISSET(udpFd, &rfds)) {
                while (m_wantActive.load(std::memory_order_acquire)) {
                    int rLen = lwip_recvfrom(udpFd, udpBuf, kUdpPktCap, MSG_DONTWAIT, nullptr, nullptr);
                    if (rLen <= 0) break;
                    if (rLen >= 4 && memcmp(udpBuf, "DMDA", 4) == 0) {
                        m_udpBlockedHint = false;
                        m_udpBlockedSkipCounter = 0;
                        continue;
                    }
                    if (rLen > 10 && memcmp(udpBuf, "DMDF", 4) == 0) {
                        m_udpBlockedHint = false;
                        m_udpBlockedSkipCounter = 0;
                        uint16_t seq = static_cast<uint16_t>(udpBuf[4] | (udpBuf[5] << 8));
                        uint8_t chunkIdx = udpBuf[6];
                        uint8_t totalChunks = udpBuf[7] > 0 ? udpBuf[7] : 1;
                        uint16_t byteOff = static_cast<uint16_t>(udpBuf[8] | (udpBuf[9] << 8));
                        size_t payloadBytes = static_cast<size_t>(rLen - 10);

                        if (seq != curFrameSeq) {
                            if (chunkMask != 0) {
                                markFrameWriteComplete();
                            } else if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
                                m_frameSeq.fetch_add(1, std::memory_order_release);
                            }
                            curFrameSeq = seq;
                            chunkMask = 0;
                        }
                        if (chunkIdx < totalChunks && totalChunks <= 64 && (static_cast<size_t>(byteOff) + payloadBytes) <= maxBufBytes) {
                            if (chunkMask == 0) {
                                markFrameWriteBegin();
                            }
                            memcpy(reinterpret_cast<uint8_t*>(m_frame565) + byteOff, udpBuf + 10, payloadBytes);
                            chunkMask |= (1ULL << chunkIdx);
                            uint64_t expectedMask = (totalChunks == 64) ? ~0ULL : ((1ULL << totalChunks) - 1ULL);
                            if (chunkMask == expectedMask) {
                                chunkMask = 0;
                                markFrameWriteComplete();
                            }
                            uint32_t pktNow = millis();
                            if (firstUdpPktMs == 0U) firstUdpPktMs = pktNow;
                            lastUdpPktMs = pktNow;
                        }
                    }
                }
            }

            uint32_t nowMs = millis();
            if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U &&
                (nowMs - frameChunkStartMs) >= 6U) {
                // Never hold `m_frameSeq` ODD across Wi-Fi packet loss: commit partial frame cleanly
                // (WLED DDP style) so Core 1 never stalls waiting for a lost final UDP chunk!
                if (chunkMask != 0) {
                    chunkMask = 0;
                    markFrameWriteComplete();
                } else {
                    m_frameSeq.fetch_add(1, std::memory_order_release);
                }
            }

            if (firstUdpPktMs > 0U) {
                uint32_t elapsedStreamMs = nowMs - firstUdpPktMs;
                uint32_t silenceMs = nowMs - lastUdpPktMs;
                if (elapsedStreamMs + 600U >= naturalStreamMs && silenceMs >= 450U) {
                    break; // Natural UDP transmitter completion
                }
                if (silenceMs > 2800U) {
                    stoppedEarlyByClient = true;
                    break;
                }
            } else if ((nowMs - streamT0) > kUdpInitialGraceMs) {
                initialGraceTimeout = true;
                break;
            }
        }

        if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
            m_frameSeq.fetch_add(1, std::memory_order_release);
        }
        if (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) != startGen) {
            stoppedEarlyByClient = true;
        }

        // Always close UDP socket FIRST to free LwIP pbufs before any post-stream action!
        closeUdpSocket(udpFd);

        if (udpFramesCompleted > 0U && firstUdpPktMs > 0U) {
            uint32_t durMs = std::max<uint32_t>(1U, millis() - firstUdpPktMs);
            uint32_t pres = m_presentedStreamFrames.load(std::memory_order_acquire);
            LOGI(
                "Showcase",
                "[%s] UDP stream done: item=%s rxFrames=%u presented=%u durMs=%u effFps=%.1f",
                domain,
                itemIdBuf,
                static_cast<unsigned>(udpFramesCompleted),
                static_cast<unsigned>(pres),
                static_cast<unsigned>(durMs),
                (static_cast<float>(pres) * 1000.0f) / static_cast<float>(durMs)
            );
        }

        if (initialGraceTimeout) {
            m_udpBlockedHint = true;
            m_udpBlockedSkipCounter = 0;
            // Container-to-LAN UDP blocked by VM NAT: stop server UDP transmitter while explicitly
            // preserving `.rgb565.stream` (`"purge":false,"reason":"tcp_fallback"`) so the immediate
            // zero-heap TCP fallback fetches `200 OK` instead of `404 Not Found`!
            if (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen) {
                char stopCmd[168];
                snprintf(
                    stopCmd,
                    sizeof(stopCmd),
                    "{\"action\":\"stop_stream\",\"item_id\":\"%s\",\"purge\":false,\"reason\":\"tcp_fallback\"}",
                    itemIdBuf
                );
                publishFlashMqttEvent(
                    mqttHost,
                    mqttPort,
                    m_sessionId,
                    m_mqttConnSeq,
                    eventsTopic,
                    stopCmd,
                    m_ioScratch,
                    IO_SCRATCH_BYTES,
                    m_wantActive
                );
                if (outHttpFallbackItemId && fallbackItemCap > 0) {
                    copySafeStr(outHttpFallbackItemId, fallbackItemCap, itemIdBuf);
                }
            }
            return true;
        }

        // Only open a post-stream flash MQTT socket when `m_wantActive` is true (Invariant 15)
        // and the client intentionally cut a longer stream short (`maxCfgStreamMs` or live config edit).
        if (stoppedEarlyByClient && m_wantActive.load(std::memory_order_acquire) && itemIdBuf[0] != '\0') {
            char stopCmd[128];
            snprintf(stopCmd, sizeof(stopCmd), "{\"action\":\"stop_stream\",\"item_id\":\"%s\"}", itemIdBuf);
            publishFlashMqttEvent(
                mqttHost,
                mqttPort,
                m_sessionId,
                m_mqttConnSeq,
                eventsTopic,
                stopCmd,
                m_ioScratch,
                IO_SCRATCH_BYTES,
                m_wantActive
            );
        }
    }

    finishTurnItem();
    return true;
#else
    (void)cfg;
    (void)domain;
    (void)reqLang;
    (void)outHttpFallbackItemId;
    (void)fallbackItemCap;
    return false;
#endif
}

void MediaShowcaseBase::runHttpPollTurn(const ConfigSlot& cfg, const char* domain, const char* reqLang) {
    if (!m_ioScratch) return;

    auto handleTransientHttpFailure = [this]() {
        if (m_consecutiveNetFailures < 10U) {
            ++m_consecutiveNetFailures;
        }
        if (shouldReportNetworkOffline(m_consecutiveNetFailures)) {
            m_statusState.store(2, std::memory_order_release);
            clearLiveCardState();
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
        } else if (m_statusState.load(std::memory_order_acquire) != 0U) {
            m_statusState.store(1, std::memory_order_release);
        }
#if defined(ESP32)
        for (int w = 0; w < 10 && m_wantActive.load(std::memory_order_acquire); ++w) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
#endif
    };

    char baseServerUrl[112] = "";
    {
        char resolvedHost[64] = "";
        uint16_t resolvedPort = 8890;
        bool resolvedOnline = false;
        if (!net::ShowcaseDiscovery::resolveUrl(
                cfg.serverUrl,
                baseServerUrl,
                sizeof(baseServerUrl),
                resolvedHost,
                sizeof(resolvedHost),
                resolvedPort,
                resolvedOnline,
                nullptr,
                0,
                true,
                false) ||
            net::ShowcaseDiscovery::isAutoDiscoverableHost(resolvedHost)) {
            handleTransientHttpFailure();
            return;
        }
    }

    net::SecureHttpOptions httpOpts;
    httpOpts.ownerId = net::OWNER_SHOWCASE;
    httpOpts.requestTimeoutMs = 3500;
    httpOpts.handshakeTimeoutSec = 2;
    httpOpts.keepAlive = false;

    uint16_t pw = 32;
    uint16_t ph = 32;
    computeCardPosterDims(m_width, m_height, pw, ph);
    uint8_t effFps = resolveEffectiveVideoFps(cfg.videoFps, m_hasPsram);

    // 1. Register client config (`POST /api/v1/{domain}/config`) using `m_ioScratch`
    size_t pos = 0;
    m_ioScratch[0] = '\0';
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "{\"client_id\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_sessionId);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"width\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_width);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"height\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, m_height);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"poster_width\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, pw);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"poster_height\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ph);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"systems\":");
    appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.systems);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"genres\":");
    appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.genres);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"decades\":");
    appendCsvAsJsonArrayToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.decades);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_type\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoType);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"include_poster\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showPoster ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"include_synopsis\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showSynopsis ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"text_scroll_speed\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.textScrollSpeed);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"jacket_panorama\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.jacketPanorama ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"marquee_panorama\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.marqueePanorama ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"snapshot_panorama_count\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.snapshotPanoramaCount);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"show_video\":");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.showVideo ? "true" : "false");
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_fps\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, effFps);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_max_duration_s\":");
    appendUintToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoMaxDurationS);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, ",\"video_mode\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.videoMode);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"color_boost\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, cfg.colorBoost);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\",\"lang\":\"");
    appendJsonEscapedToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, reqLang);
    appendRawToBuf(m_ioScratch, IO_SCRATCH_BYTES, pos, "\"}");

    {
        char cfgUrl[160];
        snprintf(cfgUrl, sizeof(cfgUrl), "%s/api/v1/%s/config", baseServerUrl, domain);
        auto cfgResp = net::SecureHttpClient::post(cfgUrl, "application/json", m_ioScratch, httpOpts);
        if (!cfgResp.ok()) {
            handleTransientHttpFailure();
            return;
        }
    }

    if (!m_wantActive.load(std::memory_order_acquire)) return;

    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

    // 2. Poll single Consume-Once item (`GET /api/v1/{domain}/next?client_id=...`)
    // Scoped so `nextResp` closes its TCP socket and frees its stack frame before poster/stream steps.
    char unifiedStreamPath[144] = "";
    char posterPathBuf[144] = "";
    CardSnapshot nextCard;
    bool parsedReady = false;
    bool serverWarming = false;
    {
        char nextUrl[192];
        snprintf(nextUrl, sizeof(nextUrl), "%s/api/v1/%s/next?client_id=%s", baseServerUrl, domain, m_sessionId);
        auto nextResp = net::SecureHttpClient::get(nextUrl, httpOpts);
        if (!nextResp.ok()) {
            handleTransientHttpFailure();
            return;
        }
        m_consecutiveNetFailures = 0;

        // Zero-copy in-place parse when Content-Length fits in `m_psramMqttRx` (S3) or `m_frame565` (ESP32_STD)
        char* rxBuf = m_psramMqttRx ? m_psramMqttRx : reinterpret_cast<char*>(m_frame565);
        size_t rxCap = m_psramMqttRx ? PSRAM_MQTT_RX_BYTES : (m_frame565 ? (m_framePixels * sizeof(uint16_t)) : 0U);
        size_t respBytes = nextResp.contentLength();

        if (rxBuf && respBytes > 0 && (respBytes + 1U) <= rxCap &&
            readExactFromStream(nextResp.stream(), reinterpret_cast<uint8_t*>(rxBuf), respBytes, m_wantActive)) {
            rxBuf[respBytes] = '\0';
            StaticJsonDocument<JSON_OBJECT_SIZE(44)> nextDoc;
            if (deserializeJson(nextDoc, rxBuf) == DeserializationError::Ok) {
                const char* status = nextDoc["status"] | "warming";
                JsonObject item = nextDoc["item"].as<JsonObject>();
                if (strcmp(status, "ready") == 0 && !item.isNull()) {
                    copySafeStr(nextCard.title, sizeof(nextCard.title), item["title"] | "UNKNOWN");
                    copySafeStr(nextCard.year, sizeof(nextCard.year), item["year"] | "");
                    copySafeStr(nextCard.badge, sizeof(nextCard.badge), item["badge"] | domain);
                    copySafeStr(nextCard.rating, sizeof(nextCard.rating), item["rating"] | "");
                    copySafeStr(nextCard.synopsis, sizeof(nextCard.synopsis), item["synopsis"] | "");
                    nextCard.badgeColor565 = parseHexColor565(item["badge_color"] | nullptr, defaultAccent565());
                    nextCard.posterW = pw;
                    nextCard.posterH = ph;
                    nextCard.hasData = true;
                    JsonObject assetsObj = item["assets"].as<JsonObject>();
                    copySafeStr(unifiedStreamPath, sizeof(unifiedStreamPath), assetsObj["unified_stream_rgb565"] | "");
                    copySafeStr(posterPathBuf, sizeof(posterPathBuf), assetsObj["poster_rgb565"] | (item["poster_rgb565_url"] | ""));
                    const char* posterB64 = item["poster_rgb565_b64"] | "";
                    size_t expectedPosterBytes = static_cast<size_t>(pw) * static_cast<size_t>(ph) * 2U;
                    if (cfg.showPoster && m_poster565 && expectedPosterBytes <= m_posterCapBytes && posterB64[0] != '\0') {
                        if (decodeBase64Fixed(posterB64, reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes)) {
                            nextCard.hasPoster = true;
                        }
                    }
                    parsedReady = true;
                } else {
                    serverWarming = true;
                }
            }
            if (rxBuf == reinterpret_cast<char*>(m_frame565) && m_frame565) {
                memset(m_frame565, 0, m_framePixels * sizeof(uint16_t));
            }
        } else {
            DynamicJsonDocument nextDoc(m_hasPsram ? 24576 : 6144);
            if (deserializeJson(nextDoc, nextResp.stream()) == DeserializationError::Ok) {
                const char* status = nextDoc["status"] | "warming";
                JsonObject item = nextDoc["item"].as<JsonObject>();
                if (strcmp(status, "ready") == 0 && !item.isNull()) {
                    copySafeStr(nextCard.title, sizeof(nextCard.title), item["title"] | "UNKNOWN");
                    copySafeStr(nextCard.year, sizeof(nextCard.year), item["year"] | "");
                    copySafeStr(nextCard.badge, sizeof(nextCard.badge), item["badge"] | domain);
                    copySafeStr(nextCard.rating, sizeof(nextCard.rating), item["rating"] | "");
                    copySafeStr(nextCard.synopsis, sizeof(nextCard.synopsis), item["synopsis"] | "");
                    nextCard.badgeColor565 = parseHexColor565(item["badge_color"] | nullptr, defaultAccent565());
                    nextCard.posterW = pw;
                    nextCard.posterH = ph;
                    nextCard.hasData = true;
                    JsonObject assetsObj = item["assets"].as<JsonObject>();
                    copySafeStr(unifiedStreamPath, sizeof(unifiedStreamPath), assetsObj["unified_stream_rgb565"] | "");
                    copySafeStr(posterPathBuf, sizeof(posterPathBuf), assetsObj["poster_rgb565"] | (item["poster_rgb565_url"] | ""));
                    const char* posterB64 = item["poster_rgb565_b64"] | "";
                    size_t expectedPosterBytes = static_cast<size_t>(pw) * static_cast<size_t>(ph) * 2U;
                    if (cfg.showPoster && m_poster565 && expectedPosterBytes <= m_posterCapBytes && posterB64[0] != '\0') {
                        if (decodeBase64Fixed(posterB64, reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes)) {
                            nextCard.hasPoster = true;
                        }
                    }
                    parsedReady = true;
                } else {
                    serverWarming = true;
                }
            }
        }
    }

    if (!parsedReady) {
        if (serverWarming) {
            m_statusState.store(1, std::memory_order_release);
            if (m_rotationSlotCount.load(std::memory_order_relaxed) > 1) {
                m_skipTurnImmediately.store(true, std::memory_order_release);
            }
        } else {
            handleTransientHttpFailure();
        }
        return;
    }

    size_t expectedPosterBytes = static_cast<size_t>(pw) * static_cast<size_t>(ph) * 2U;
    if (cfg.showPoster && !nextCard.hasPoster && m_poster565 && expectedPosterBytes <= m_posterCapBytes && posterPathBuf[0] != '\0') {
        char posterUrl[224];
        if (strncmp(posterPathBuf, "http", 4) == 0) {
            copySafeStr(posterUrl, sizeof(posterUrl), posterPathBuf);
        } else {
            snprintf(posterUrl, sizeof(posterUrl), "%s%s", baseServerUrl, posterPathBuf);
        }
        auto pResp = net::SecureHttpClient::get(posterUrl, httpOpts);
        if (pResp.ok() && readExactFromStream(pResp.stream(), reinterpret_cast<uint8_t*>(m_poster565), expectedPosterBytes, m_wantActive)) {
            nextCard.hasPoster = true;
        }
    }

    m_seq.fetch_add(1, std::memory_order_acq_rel);
    m_liveCard = nextCard;
    m_seq.fetch_add(1, std::memory_order_release);
    m_statusState.store(0, std::memory_order_release);
    m_phaseStartedAtMs = millis();
    m_phase.store(static_cast<uint8_t>(Phase::Card), std::memory_order_release);

    // Phase 1: Organic Rich Card dwell
    uint32_t synGlyphs = static_cast<uint32_t>(countUtf8Glyphs(nextCard.synopsis));
    uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);
    uint32_t cardWaitMs = 4500U;
    if (cfg.showSynopsis && synGlyphs > 0) {
        uint32_t textW = synGlyphs * 6U;
        uint32_t availW = static_cast<uint32_t>(computeCardTextViewportWidth(m_width, m_height, cfg.showPoster));
        if (textW > availW) {
            uint32_t scrollDistPx = (textW - availW) + (availW / 2U);
            uint32_t synScrollMs = 900U + (scrollDistPx * 1000U) / speedPx + 700U;
            if (synScrollMs > cardWaitMs) cardWaitMs = synScrollMs;
        }
    }
    uint32_t startGen = m_cfgGen.load(std::memory_order_acquire);
    uint32_t t0 = millis();
    while (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen && (millis() - t0) < cardWaitMs) {
#if defined(ESP32)
        vTaskDelay(pdMS_TO_TICKS(40));
#else
        break;
#endif
    }

    // Phase 2 & 3: Stream unified `.rgb565.stream` (`DMDS`) into `m_frame565`
    if (m_wantActive.load(std::memory_order_acquire) && m_cfgGen.load(std::memory_order_acquire) == startGen && m_frame565 && unifiedStreamPath[0] != '\0') {
        streamUnifiedRgb565Http(baseServerUrl, unifiedStreamPath);
    }

    finishTurnItem();
}

bool MediaShowcaseBase::streamUnifiedRgb565Http(const char* serverUrl, const char* streamPath) {
    if (!serverUrl || !streamPath || streamPath[0] == '\0' || !m_frame565 || !m_ioScratch) {
        return false;
    }

#if defined(ESP32) && !defined(UNIT_TEST)
    // Zero-Heap ZeDMD-Grade Raw LwIP TCP Streamer:
    // Bypasses `SecureHttpClient` / `HTTPClient` / `WiFiClient` (~10 KB DRAM saved on ESP32_STD)
    // and pairs with Showcase `?paced=1` (`X-DMD-Paced: 1`) so `TCP_WND` stays 100% open with
    // zero client-side sleep and zero `TCP ZeroWindow` stalls!
    char resolvedHost[64] = "";
    uint16_t resolvedPort = 8890;
    const char* rawPath = streamPath;

    if (strncasecmp(streamPath, "http://", 7) == 0 || strncasecmp(streamPath, "https://", 8) == 0) {
        if (!parseHostAndPort(streamPath, 8890, resolvedHost, sizeof(resolvedHost), resolvedPort)) {
            return false;
        }
        if (!net::ShowcaseDiscovery::resolveHostInPlace(resolvedHost, sizeof(resolvedHost), resolvedPort, true)) {
            return false;
        }
        const char* afterScheme = strstr(streamPath, "://");
        const char* slash = afterScheme ? strchr(afterScheme + 3, '/') : nullptr;
        rawPath = slash ? slash : "/";
    } else {
        char baseServerUrl[112] = "";
        bool resolvedOnline = false;
        if (!net::ShowcaseDiscovery::resolveUrl(
                serverUrl,
                baseServerUrl,
                sizeof(baseServerUrl),
                resolvedHost,
                sizeof(resolvedHost),
                resolvedPort,
                resolvedOnline,
                nullptr,
                0,
                true,
                false) ||
            net::ShowcaseDiscovery::isAutoDiscoverableHost(resolvedHost)) {
            return false;
        }
    }

    char reqPath[192];
    bool hasQ = (strchr(rawPath, '?') != nullptr);
    bool hasPaced = (strstr(rawPath, "paced=") != nullptr);
    bool hasDropLate = (strstr(rawPath, "drop_late=") != nullptr);
    if (hasPaced && hasDropLate) {
        copySafeStr(reqPath, sizeof(reqPath), rawPath);
    } else if (hasPaced && !hasDropLate) {
        snprintf(reqPath, sizeof(reqPath), "%s&drop_late=1", rawPath);
    } else {
        snprintf(reqPath, sizeof(reqPath), "%s%spaced=1&drop_late=1", rawPath, hasQ ? "&" : "?");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(resolvedPort);
    if (inet_aton(resolvedHost, &addr.sin_addr) == 0) {
        IPAddress resolvedIp;
        if (ESP.getFreeHeap() < 10240U || !WiFi.hostByName(resolvedHost, resolvedIp)) {
            return false;
        }
        addr.sin_addr.s_addr = static_cast<uint32_t>(resolvedIp);
    }

    int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return false;
    }

    auto closeStreamSocket = [&](bool abortWithRst) {
        if (fd >= 0) {
            if (abortWithRst) {
                struct linger lin = {1, 0};
                lwip_setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
            }
            lwip_close(fd);
            fd = -1;
        }
    };

    int nodelay = 1;
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    int flags = lwip_fcntl(fd, F_GETFL, 0);
    lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = lwip_connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        closeStreamSocket(true);
        return false;
    }
    if (rc != 0) {
        uint32_t connT0 = millis();
        bool connected = false;
        while ((millis() - connT0) < 1500U && m_wantActive.load(std::memory_order_acquire)) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            struct timeval tv = {0, 150000};
            int sel = lwip_select(fd + 1, nullptr, &wfds, nullptr, &tv);
            if (sel > 0 && FD_ISSET(fd, &wfds)) {
                int soErr = -1;
                socklen_t errLen = sizeof(soErr);
                if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &errLen) == 0 && soErr == 0) {
                    connected = true;
                }
                break;
            }
        }
        if (!connected) {
            closeStreamSocket(true);
            return false;
        }
    }

    lwip_fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval rcvTv = {0, 250000}; // 250 ms slice keeps `deactivate()` responsive (< 250 ms)
    struct timeval sndTv = {1, 0};
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvTv, sizeof(rcvTv));
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sndTv, sizeof(sndTv));

    // Use HTTP/1.0 (`Connection: close`) so Uvicorn never wraps `drop_late=1` responses (where
    // `Content-Length` is intentionally omitted) in HTTP/1.1 `Transfer-Encoding: chunked`.
    char reqBuf[288];
    int reqLen = snprintf(
        reqBuf,
        sizeof(reqBuf),
        "GET %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
        reqPath,
        resolvedHost,
        static_cast<unsigned>(resolvedPort)
    );
    if (reqLen <= 0 || lwip_send(fd, reqBuf, static_cast<size_t>(reqLen), 0) != reqLen) {
        closeStreamSocket(true);
        return false;
    }

    // Read HTTP response header into `m_ioScratch` (`1408` bytes preallocated)
    size_t scratchUsed = 0;
    size_t bodyStart = 0;
    bool foundHeaderEnd = false;
    uint32_t hdrT0 = millis();
    while (!foundHeaderEnd && (millis() - hdrT0) < 3500U && m_wantActive.load(std::memory_order_acquire)) {
        if (scratchUsed >= IO_SCRATCH_BYTES - 1U) {
            break;
        }
        int r = lwip_recv(fd, m_ioScratch + scratchUsed, IO_SCRATCH_BYTES - 1U - scratchUsed, 0);
        if (r > 0) {
            scratchUsed += static_cast<size_t>(r);
            m_ioScratch[scratchUsed] = '\0';
            char* sep = strstr(m_ioScratch, "\r\n\r\n");
            if (sep != nullptr) {
                foundHeaderEnd = true;
                bodyStart = static_cast<size_t>((sep - m_ioScratch) + 4);
                *sep = '\0';
            }
        } else if (r == 0) {
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
    }

    if (!foundHeaderEnd ||
        (strncmp(m_ioScratch, "HTTP/1.1 200", 12) != 0 && strncmp(m_ioScratch, "HTTP/1.0 200", 12) != 0)) {
        closeStreamSocket(true);
        return false;
    }

    bool serverPaced = false;
    for (const char* p = m_ioScratch; *p != '\0'; ++p) {
        if ((*p == 'X' || *p == 'x') && strncasecmp(p, "x-dmd-paced:", 12) == 0) {
            const char* v = p + 12;
            while (*v == ' ' || *v == '\t') ++v;
            if (*v == '1') {
                serverPaced = true;
            }
            break;
        }
    }

    size_t preBufferedOff = bodyStart;
    size_t preBufferedEnd = scratchUsed;

    auto readExactRawTcp = [&](uint8_t* dst, size_t wantLen, uint32_t maxIdleMs) -> bool {
        size_t got = 0;
        if (preBufferedOff < preBufferedEnd) {
            size_t avail = preBufferedEnd - preBufferedOff;
            size_t take = (avail < wantLen) ? avail : wantLen;
            memcpy(dst, m_ioScratch + preBufferedOff, take);
            preBufferedOff += take;
            got += take;
        }
        uint32_t lastProg = millis();
        while (got < wantLen) {
            if (!m_wantActive.load(std::memory_order_acquire)) {
                return false;
            }
            int r = lwip_recv(fd, dst + got, wantLen - got, 0);
            if (r > 0) {
                got += static_cast<size_t>(r);
                lastProg = millis();
            } else if (r == 0) {
                return false;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    return false;
                }
                if ((millis() - lastProg) >= maxIdleMs) {
                    return false;
                }
            }
        }
        return true;
    };

    uint8_t hdr[16];
    if (!readExactRawTcp(hdr, sizeof(hdr), 3500U) || memcmp(hdr, "DMDS", 4) != 0) {
        closeStreamSocket(true);
        return false;
    }

    uint8_t fps = hdr[5] > 0 ? hdr[5] : 15;
    uint16_t w = static_cast<uint16_t>(hdr[6] | (hdr[7] << 8));
    uint16_t h = static_cast<uint16_t>(hdr[8] | (hdr[9] << 8));
    uint16_t totalFrames = static_cast<uint16_t>(hdr[10] | (hdr[11] << 8));
    uint16_t delayMs = static_cast<uint16_t>(hdr[12] | (hdr[13] << 8));
    uint32_t frameDelayMs = (delayMs > 0) ? delayMs : (1000U / fps);
    size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2U;
    size_t maxBufBytes = m_framePixels * sizeof(uint16_t);
    if (frameBytes == 0 || frameBytes > maxBufBytes || totalFrames == 0) {
        closeStreamSocket(true);
        return false;
    }

    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];
    uint32_t maxAllowedMs = configuredMaxStreamMs(cfg);
    uint32_t streamStartMs = millis();
    uint32_t startGen = m_cfgGen.load(std::memory_order_acquire);
    m_presentedStreamFrames.store(0, std::memory_order_release);

    uint16_t framesRead = 0;
    for (uint16_t f = 0; f < totalFrames; ++f) {
        if (!m_wantActive.load(std::memory_order_acquire) || m_cfgGen.load(std::memory_order_acquire) != startGen) break;
        if ((millis() - streamStartMs) >= maxAllowedMs) break;

        // 1. Wait for the next frame's bytes to arrive in LwIP's kernel buffer WHILE `m_frameSeq`
        //    remains EVEN! This keeps the previous frame 100% unlocked for Core 1 during the ~48 ms
        //    inter-frame network interval instead of holding `m_frameSeq` ODD and starving Core 1.
        if (preBufferedOff >= preBufferedEnd) {
            uint32_t waitT0 = millis();
            bool socketReadable = false;
            while (m_wantActive.load(std::memory_order_acquire) &&
                   m_cfgGen.load(std::memory_order_acquire) == startGen &&
                   (millis() - streamStartMs) < maxAllowedMs &&
                   (millis() - waitT0) < 3500U) {
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(fd, &rfds);
                struct timeval tv = {0, 25000};
                int sel = lwip_select(fd + 1, &rfds, nullptr, nullptr, &tv);
                if (sel > 0 && FD_ISSET(fd, &rfds)) {
                    socketReadable = true;
                    break;
                }
                if (sel < 0 && errno != EINTR) {
                    break;
                }
            }
            if (!socketReadable) {
                break;
            }
        }

        // 2. Ensure Core 1 has blitted Frame `f - 1` before overwriting `m_frame565` (bounded <= 18 ms)
        //    while incoming Frame `f` sits safely in LwIP's `TCP_WND`.
        if (framesRead > 0U) {
            uint32_t handoffT0 = millis();
            while (m_wantActive.load(std::memory_order_acquire) &&
                   m_consumedFrameSeq.load(std::memory_order_acquire) != m_frameSeq.load(std::memory_order_relaxed) &&
                   (millis() - handoffT0) < 18U) {
                vTaskDelay(1);
            }
        }

        // 3. Critical write window (~1.2 ms RAM copy): mark ODD -> drain frame -> mark EVEN immediately!
        if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) == 0U) {
            m_frameSeq.fetch_add(1, std::memory_order_acq_rel);
        }
        bool okFrame = readExactRawTcp(reinterpret_cast<uint8_t*>(m_frame565), frameBytes, 3500U);
        if ((m_frameSeq.load(std::memory_order_relaxed) & 1U) != 0U) {
            m_frameSeq.fetch_add(1, std::memory_order_release);
        }
        if (!okFrame) {
            break;
        }
        ++framesRead;
        if (!m_liveCard.hasVideoFrame) {
            m_seq.fetch_add(1, std::memory_order_acq_rel);
            m_liveCard.hasVideoFrame = true;
            m_seq.fetch_add(1, std::memory_order_release);
        }
        if (m_phase.load(std::memory_order_relaxed) != static_cast<uint8_t>(Phase::LiveStream)) {
            m_phase.store(static_cast<uint8_t>(Phase::LiveStream), std::memory_order_release);
        }

        if (serverPaced) {
            // Showcase paces frames monotonically on the server (`X-DMD-Paced: 1`), so draining
            // `lwip_recv` immediately keeps `TCP_WND` wide open with ZERO `TCP ZeroWindow` stalls!
            if ((f & 0x03U) == 0U) {
                taskYIELD();
            }
        } else {
            uint32_t targetElapsedMs = (fps > 0U)
                ? ((static_cast<uint32_t>(f + 1U) * 1000U) / static_cast<uint32_t>(fps))
                : (static_cast<uint32_t>(f + 1U) * frameDelayMs);
            uint32_t elapsedMs = millis() - streamStartMs;
            if (elapsedMs >= maxAllowedMs) break;
            if (elapsedMs < targetElapsedMs) {
                uint32_t waitMs = std::min<uint32_t>(targetElapsedMs - elapsedMs, maxAllowedMs - elapsedMs);
                if (waitMs > 0U) {
                    vTaskDelay(pdMS_TO_TICKS(waitMs));
                }
            } else if ((f & 0x07U) == 0U) {
                vTaskDelay(1);
            }
        }
    }

    // Drop EOF / early abort cleanly (`drop_late=1` may intentionally send fewer than `totalFrames` before EOF)
    closeStreamSocket(false);
    if (framesRead > 0U) {
        uint32_t durMs = std::max<uint32_t>(1U, millis() - streamStartMs);
        uint32_t pres = m_presentedStreamFrames.load(std::memory_order_acquire);
        LOGI(
            "Showcase",
            "TCP stream done: path=%s rxFrames=%u presented=%u durMs=%u effFps=%.1f",
            reqPath,
            static_cast<unsigned>(framesRead),
            static_cast<unsigned>(pres),
            static_cast<unsigned>(durMs),
            (static_cast<float>(pres) * 1000.0f) / static_cast<float>(durMs)
        );
    }
    return (framesRead > 0U);
#else
    net::SecureHttpResponse sResp;
    {
        char fullUrl[256];
        if (strncmp(streamPath, "http", 4) == 0) {
            copySafeStr(fullUrl, sizeof(fullUrl), streamPath);
        } else {
            snprintf(fullUrl, sizeof(fullUrl), "%s%s", serverUrl, streamPath);
        }
        net::SecureHttpOptions httpOpts;
        httpOpts.ownerId = net::OWNER_SHOWCASE;
        httpOpts.requestTimeoutMs = 4500;
        httpOpts.keepAlive = false;
        sResp = net::SecureHttpClient::get(fullUrl, httpOpts);
    }
    if (!sResp.ok()) {
        return false;
    }

    Stream& st = sResp.stream();
    uint8_t hdr[16];
    if (!readExactFromStream(st, hdr, sizeof(hdr), m_wantActive) || memcmp(hdr, "DMDS", 4) != 0) {
        return false;
    }
    uint16_t w = static_cast<uint16_t>(hdr[6] | (hdr[7] << 8));
    uint16_t h = static_cast<uint16_t>(hdr[8] | (hdr[9] << 8));
    uint16_t totalFrames = static_cast<uint16_t>(hdr[10] | (hdr[11] << 8));
    size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 2U;
    size_t maxBufBytes = m_framePixels * sizeof(uint16_t);
    if (frameBytes == 0 || frameBytes > maxBufBytes || totalFrames == 0) {
        return false;
    }

    uint16_t framesRead = 0;
    for (uint16_t f = 0; f < totalFrames; ++f) {
        if (!m_wantActive.load(std::memory_order_acquire)) break;
        if (!readExactFromStream(st, reinterpret_cast<uint8_t*>(m_frame565), frameBytes, m_wantActive)) {
            break;
        }
        ++framesRead;
        m_frameSeq.fetch_add(2, std::memory_order_release);
        m_seq.fetch_add(1, std::memory_order_acq_rel);
        m_liveCard.hasVideoFrame = true;
        m_seq.fetch_add(1, std::memory_order_release);
        if (m_phase.load(std::memory_order_relaxed) != static_cast<uint8_t>(Phase::LiveStream)) {
            m_phase.store(static_cast<uint8_t>(Phase::LiveStream), std::memory_order_release);
        }
    }
    return (framesRead > 0U);
#endif
}

void MediaShowcaseBase::update(EngineContext* context) {
    (void)context;
    // Zero-copy RAII snapshot read on Core 1 (Invariants 1 & 2)
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    const auto& snap = guard.get();
    m_rotationSlotCount.store(static_cast<uint8_t>(snap.rotation.size()), std::memory_order_relaxed);

    // Lock-free seqlock check: only copy CardSnapshot when `m_seq` is EVEN and actually changed
    uint32_t s1 = m_seq.load(std::memory_order_acquire);
    if ((s1 & 1U) == 0U && s1 != m_lastRenderedCardSeq) {
        CardSnapshot temp = m_liveCard;
        uint32_t s2 = m_seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            m_renderCard = temp;
            m_lastRenderedCardSeq = s1;
            m_dirty = true;
        }
    }

    uint8_t curPhase = m_phase.load(std::memory_order_acquire);
    if (curPhase != m_lastRenderedPhase) {
        m_lastRenderedPhase = curPhase;
        m_dirty = true;
    }

    uint32_t now = millis();
    if (curPhase == static_cast<uint8_t>(Phase::LiveStream)) {
        // Tear-free double-buffer handoff: mark dirty when Core 0 finished a complete new frame (`EVEN` seq).
        // Do NOT advance `m_lastRenderedFrameSeq` here — only `render()` advances it after `blit565` succeeds,
        // guaranteeing a frame is never lost if `m_frameSeq` briefly turns `ODD` before `render()` runs!
        uint32_t fSeq = m_frameSeq.load(std::memory_order_acquire);
        if ((fSeq & 1U) == 0U && fSeq != m_lastRenderedFrameSeq && m_renderCard.hasVideoFrame) {
            m_dirty = true;
        } else if (!m_renderCard.hasVideoFrame && (now - m_lastScrollStepMs) >= 90U) {
            m_lastScrollStepMs = now;
            m_dirty = true;
        }
    } else if ((now - m_lastScrollStepMs) >= 33U) {
        m_lastScrollStepMs = now;
        m_dirty = true;
    }
}

void MediaShowcaseBase::drawScrollingLine(
    IDrawingSurface* s,
    const char* text,
    int minX,
    int maxX,
    int y,
    uint16_t color,
    uint32_t elapsedMs
) {
    if (!s || !text || text[0] == '\0' || minX >= maxX) return;
    int availW = maxX - minX;
    int textW = static_cast<int>(countUtf8Glyphs(text)) * 6;

    if (textW <= availW) {
        drawClippedText5x7(s, text, minX, y, minX, maxX, color);
        return;
    }

    int totalScroll = textW - availW + 12;
    int cycleSteps = totalScroll + 28; // 14 steps (~630ms) start hold + scroll + 14 steps end hold
    int step = static_cast<int>((elapsedMs / 45U) % static_cast<uint32_t>(cycleSteps));
    int offset = std::min(totalScroll, std::max(0, step - 14));
    drawClippedText5x7(s, text, minX - offset, y, minX, maxX, color);
}

void MediaShowcaseBase::drawSinglePassSynopsis(
    IDrawingSurface* s,
    const char* text,
    int minX,
    int maxX,
    int y,
    uint16_t color,
    uint32_t elapsedMs,
    uint32_t speedPxPerSec
) {
    if (!s || !text || text[0] == '\0' || minX >= maxX) return;
    int availW = maxX - minX;
    int textW = static_cast<int>(countUtf8Glyphs(text)) * 6;

    if (textW <= availW) {
        drawClippedText5x7(s, text, minX, y, minX, maxX, color);
        return;
    }

    uint32_t activeScrollMs = (elapsedMs > 900U) ? (elapsedMs - 900U) : 0U;
    int maxScrollPx = (textW - availW) + (availW / 2);
    int scrollPx = std::min(maxScrollPx, static_cast<int>((activeScrollMs * speedPxPerSec) / 1000U));
    drawClippedText5x7(s, text, minX - scrollPx, y, minX, maxX, color);
}

void MediaShowcaseBase::drawWaitingPixelArt(IDrawingSurface* s, int mW, int mH, uint32_t elapsedMs) {
    uint32_t frameStep = elapsedMs / 180U;
    uint16_t accent = defaultAccent565();

    // Symmetrical 3-band layout:
    // - Top band:    Centered domain pill (y = 1..7)
    // - Middle band: Centered domain Pixel-Art animation (y = midTop..midBot)
    // - Bottom band: Centered steady 3x5 localized status + gentle 3-dot progress (y = textY..textY+4)
    int textY = (mH >= 64) ? (mH - 9) : (mH - 6);
    int midTop = (mH >= 64) ? 12 : 9;
    int midBot = std::max(midTop + 8, textY - 2);
    int midH = std::max(8, midBot - midTop);
    int midCy = midTop + (midH / 2);
    int cx = mW / 2;

    switch (m_domain) {
        case ShowcaseDomainId::Games: {
            // 1. Centered Arcade Attract-Mode: Parallax Starfield + Symmetric Space Invader Patrol + Pellets
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
            // 2. Centered 35mm Cinema Projector: Dual Spinning Reels + Flickering Beam + Silver Screen
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
            // 3. Centered Retro CRT Television + Rabbit-Ear Antennae + Symmetric RF Waves + SMPTE Bars
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
            // 4. Centered Anime Shuriken / Energy Core + Middle-Band Speed-Lines + Sakura Petals
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

    // Top centered header pill (3x5 font)
    const char* badge = domainBadgeLabel();
    int badgeGlyphs = static_cast<int>(countUtf8Glyphs(badge));
    int badgeTextW = std::max(0, badgeGlyphs * 4 - 1);
    int pillW = badgeTextW + 6;
    int pillX = std::max(1, (mW - pillW) / 2);
    s->fillRoundRect(pillX, 1, pillW, 7, 2, s->color565(18, 22, 34));
    s->drawRoundRect(pillX, 1, pillW, 7, 2, accent);
    drawClippedText3x5(s, badge, pillX + 3, 2, 0, mW, accent);

    // Bottom steady centered status label (zero blink, zero unnecessary scroll)
    ConfigSnapshotGuard guard = config.acquireSnapshot();
    Lang sysLang = I18n::parseLang(guard.get().system.lang.c_str());
    uint8_t st = m_statusState.load(std::memory_order_relaxed);
    const char* statusText = (st == 3)
        ? I18n::getShowcaseUnconfiguredLabel(sysLang)
        : (st == 2)
            ? I18n::getShowcaseOfflineLabel(sysLang)
            : I18n::getShowcaseWarmingLabel(sysLang);

    char stemBuf[48];
    copySafeStr(stemBuf, sizeof(stemBuf), statusText);
    size_t rawLen = strlen(stemBuf);
    bool hasEllipsis = false;
    while (rawLen > 0 && stemBuf[rawLen - 1] == '.') {
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
        // Fallback smooth non-blinking 3x5 scroll only on ultra-narrow (< 112 px) panels
        int availW = std::max(8, mW - 4);
        int fullW = static_cast<int>(countUtf8Glyphs(statusText)) * 4;
        int scrollRange = std::max(1, fullW - availW + 8);
        int step = static_cast<int>((elapsedMs / 55U) % static_cast<uint32_t>(scrollRange + 20));
        int off = std::min(scrollRange, std::max(0, step - 10));
        drawClippedText3x5(s, statusText, 2 - off, textY, 2, mW - 2, steadyLabelCol);
    }
}

void MediaShowcaseBase::drawCardPhase(IDrawingSurface* s, const CardSnapshot& card, uint32_t elapsedMs) {
    int mW = s->width();
    int mH = s->height();
    const ConfigSlot& cfg = m_cfg[m_cfgIdx.load(std::memory_order_acquire)];

    // If no live item is active, render the domain-themed Pixel-Art waiting animation
    if (!card.hasData) {
        drawWaitingPixelArt(s, mW, mH, elapsedMs);
        return;
    }

    uint32_t cardElapsedMs = (millis() >= m_phaseStartedAtMs) ? (millis() - m_phaseStartedAtMs) : elapsedMs;
    uint32_t speedPx = synopsisPxPerSec(cfg.textScrollSpeed);
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
        if (cfg.showPoster) {
            int pw = static_cast<int>(card.posterW);
            int ph = static_cast<int>(card.posterH);
            int px = std::max(0, (mW - pw) / 2);
            int py = 10;
            if (card.hasPoster && m_poster565) {
                s->blit565(m_poster565, px, py, pw, ph);
            } else {
                s->drawRect(px, py, pw, ph, card.badgeColor565);
            }
            posterBottom = py + ph + 3;
        }
        drawScrollingLine(s, card.title, 2, mW - 2, posterBottom, 0xFFFF, cardElapsedMs);
        int sepY = posterBottom + 10;
        if (sepY < mH - 10) {
            s->drawFastHLine(2, sepY, mW - 4, sepCol);
            if (cfg.showSynopsis && card.synopsis[0] != '\0') {
                drawSinglePassSynopsis(s, card.synopsis, 2, mW - 2, sepY + 3, synCol, cardElapsedMs, speedPx);
            }
        }
        return;
    }

    // 2. Square Panel (e.g. 64x64)
    if (mW == mH) {
        int rightX = cfg.showPoster ? 35 : 2;
        if (cfg.showPoster) {
            if (card.hasPoster && m_poster565) {
                s->blit565(m_poster565, 0, 0, 32, 32);
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
        drawScrollingLine(s, card.title, 2, mW - 2, 35, 0xFFFF, cardElapsedMs);
        s->drawFastHLine(2, 45, mW - 4, sepCol);
        if (cfg.showSynopsis && card.synopsis[0] != '\0') {
            drawSinglePassSynopsis(s, card.synopsis, 2, mW - 2, 49, synCol, cardElapsedMs, speedPx);
        }
        return;
    }

    // 3. Tall / XL Landscape (e.g. 128x64, 256x64 on ESP32-S3 PSRAM) -> 48x64 mini-jacket + spacious metadata
    if (mH >= 64) {
        int pw = static_cast<int>(card.posterW);
        int ph = static_cast<int>(card.posterH);
        int textX = cfg.showPoster ? (pw + 4) : 3;
        int maxX = mW - 2;

        if (cfg.showPoster) {
            if (card.hasPoster && m_poster565) {
                s->blit565(m_poster565, 0, 0, pw, ph);
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
        drawScrollingLine(s, card.title, textX, maxX, 14, 0xFFFF, cardElapsedMs);
        if (card.year[0] != '\0') {
            drawClippedText5x7(s, card.year, textX, 25, textX, maxX, yearCol);
        }
        s->drawFastHLine(textX, 36, std::max(1, maxX - textX), sepCol);
        if (cfg.showSynopsis && card.synopsis[0] != '\0') {
            drawSinglePassSynopsis(s, card.synopsis, textX, maxX, 43, synCol, cardElapsedMs, speedPx);
        }
        return;
    }

    // 4. Standard Landscape (128x32):
    // - Left 32x32 poster (`x = 0..31`), 1px gutter (`x = 32`), divider (`x = 33`), 1px gutter (`x = 34`)
    // - Strict per-column clipping inside `[textX, maxX)` (`[35, 126)`) guarantees text NEVER touches the poster!
    int textX = cfg.showPoster ? 35 : 3;
    int maxX = mW - 2;

    if (cfg.showPoster) {
        if (card.hasPoster && m_poster565) {
            s->blit565(m_poster565, 0, 0, 32, 32);
        } else {
            s->fillRoundRect(0, 0, 32, 32, 3, s->color565(16, 20, 32));
            s->drawRoundRect(0, 0, 32, 32, 3, card.badgeColor565);
            char shortBadge[7];
            copySafeStr(shortBadge, sizeof(shortBadge), card.badge);
            int sbW = std::max(0, static_cast<int>(countUtf8Glyphs(shortBadge)) * 4 - 1);
            int sbX = std::max(2, (32 - sbW) / 2);
            drawClippedText3x5(s, shortBadge, sbX, 13, 2, 30, card.badgeColor565);
        }
        s->drawFastVLine(33, 1, 30, sepCol);
    }

    // Top Header Bar (y = 1..7): Crisp 3x5 Badge Pill + Year + Right-Aligned Rating
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

    // Middle Row (y = 10..17): Full 5x7 UTF-8 Accented Title strictly clipped to [textX, maxX)
    drawScrollingLine(s, card.title, textX, maxX, 10, 0xFFFF, cardElapsedMs);

    // Subtle Horizontal Divider (y = 19)
    s->drawFastHLine(textX, 19, std::max(1, maxX - textX), sepCol);

    // Bottom Row (y = 22..29): Full 5x7 UTF-8 Accented Synopsis strictly clipped to [textX, maxX)
    if (cfg.showSynopsis && card.synopsis[0] != '\0') {
        drawSinglePassSynopsis(s, card.synopsis, textX, maxX, 22, synCol, cardElapsedMs, speedPx);
    }
}

void MediaShowcaseBase::render(EngineContext* context) {
    if (!m_dirty) {
        m_renderedNewFrame = false;
        return;
    }
    IDrawingSurface* s = (context && context->getSurface()) ? context->getSurface() : m_surface;
    if (!s) {
        m_renderedNewFrame = false;
        return;
    }

    uint32_t elapsedMs = millis() - m_activatedAtMs;
    Phase curPhase = static_cast<Phase>(m_phase.load(std::memory_order_acquire));

    if (curPhase == Phase::LiveStream) {
        if (m_renderCard.hasVideoFrame && m_frame565 != nullptr) {
            // Never clear or blit while Core 0 is mid-frame (`ODD` m_frameSeq): keep `m_dirty = true`
            // so the very next Core 1 tick blits the completed frame as soon as `m_frameSeq` turns `EVEN`.
            uint32_t fSeq1 = m_frameSeq.load(std::memory_order_acquire);
            if ((fSeq1 & 1U) != 0U) {
                m_renderedNewFrame = false;
                return;
            }
            if (s->width() != static_cast<int>(m_width) || s->height() != static_cast<int>(m_height)) {
                s->clear(0);
            }
            s->blit565(m_frame565, 0, 0, m_width, m_height);
            m_lastRenderedFrameSeq = fSeq1;
            m_consumedFrameSeq.store(fSeq1, std::memory_order_release);
            m_presentedStreamFrames.fetch_add(1, std::memory_order_relaxed);
        } else {
            s->clear(0);
            drawWaitingPixelArt(s, s->width(), s->height(), elapsedMs);
        }
    } else {
        s->clear(0);
        drawCardPhase(s, m_renderCard, elapsedMs);
    }

    m_dirty = false;
    m_renderedNewFrame = true;
}

EngineDescriptor RetroGamesEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"retro_games", "Retro Games Showcase", "arcade", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = false;
    desc.requirements.targetFps = 60;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 16000;
    desc.requirements.internalContiguousBytes = 8192;
    desc.requirements.psramBytes = 0;

    desc.schema.fields = {
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width cover/jacket from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Marquee / Banner Panorama", "Scroll the arcade marquee or title banner from top to bottom after the jacket", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional gameplay snapshots to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "20", false, "8", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Framing Mode", "Video framing strategy powered by dmd_gif_converter (Auto AI, Panorama, or Center Crop)", "auto_action", false, "", "", "", "auto_action,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::ENUM, "LED Color Boost", "Colorimetry profile for HUB75 LED panels (Auto AI or fixed domain preset)", "auto", false, "", "", "", "auto,pixel_art,anime,cinema", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("systems", ConfigType::LIST, "Arcade & Console Systems", "Select one or more systems dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept),
        ConfigField("genres", ConfigType::LIST, "Game Genres", "Select one or more genres dynamically filtered by your chosen systems (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically filtered by your chosen systems (leave empty for all)", "", false, "", "", "", "", "/api/v1/games/filters", true, "", ValidationPolicy::Accept)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new RetroGamesEngine()); };
    return desc;
}

EngineDescriptor MovieShowcaseEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"movie_showcase", "Movie Showcase", "media", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = false;
    desc.requirements.targetFps = 60;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 16000;
    desc.requirements.internalContiguousBytes = 8192;
    desc.requirements.psramBytes = 0;

    desc.schema.fields = {
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width movie poster from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the movie backdrop banner from top to bottom after the poster", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional movie stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "20", false, "8", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Framing Mode", "Video framing strategy powered by dmd_gif_converter (Auto AI, Panorama, or Center Crop)", "auto_action", false, "", "", "", "auto_action,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::ENUM, "LED Color Boost", "Colorimetry profile for HUB75 LED panels (Auto AI or fixed domain preset)", "auto", false, "", "", "", "auto,pixel_art,anime,cinema", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::LIST, "Movie Genres", "Select one or more movie genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/movies/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/movies/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (trailer,scene,any)", "any", false, "", "", "", "any,trailer,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MovieShowcaseEngine()); };
    return desc;
}

EngineDescriptor SeriesShowcaseEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"series_showcase", "Series Showcase", "media", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = false;
    desc.requirements.targetFps = 60;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 16000;
    desc.requirements.internalContiguousBytes = 8192;
    desc.requirements.psramBytes = 0;

    desc.schema.fields = {
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width series poster from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the series banner from top to bottom after the poster", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional episode stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "20", false, "8", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Framing Mode", "Video framing strategy powered by dmd_gif_converter (Auto AI, Panorama, or Center Crop)", "auto_action", false, "", "", "", "auto_action,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::ENUM, "LED Color Boost", "Colorimetry profile for HUB75 LED panels (Auto AI or fixed domain preset)", "auto", false, "", "", "", "auto,pixel_art,anime,cinema", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::LIST, "Series Genres", "Select one or more series genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/series/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/series/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,trailer,any)", "any", false, "", "", "", "any,opening,trailer", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new SeriesShowcaseEngine()); };
    return desc;
}

EngineDescriptor AnimeShowcaseEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = {"anime_showcase", "Anime Showcase", "media", FIRMWARE_VERSION};
    desc.capabilities.realtime = true;
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.allowsOverlay = true;
    desc.capabilities.allowRotation = true;
    desc.capabilities.selfPaced = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsNetwork = true;
    desc.requirements.needsTls = false;
    desc.requirements.targetFps = 60;
    desc.requirements.prefersDoubleBuffer = true;
    desc.requirements.supportsSingleBuffer = true;
    desc.requirements.internalPersistentBytes = 16000;
    desc.requirements.internalContiguousBytes = 8192;
    desc.requirements.psramBytes = 0;

    desc.schema.fields = {
        ConfigField("text_scroll_speed", ConfigType::ENUM, "Synopsis Scroll Speed (1-10)", "Reading speed for the organic full-synopsis scroll on the Info Card (1=Slow .. 10=Turbo)", "4", false, "", "", "", "1,2,3,4,5,6,7,8,9,10,slow,normal,fast", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("jacket_panorama", ConfigType::BOOLEAN, "Jacket Panorama Scroll", "Scroll the full-width anime key visual from top to bottom right after the info card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("marquee_panorama", ConfigType::BOOLEAN, "Banner / Backdrop Panorama", "Scroll the anime banner from top to bottom after the key visual", "false", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("snapshot_panorama_count", ConfigType::INTEGER, "Snapshot Panoramas", "Additional anime stills to scroll from top to bottom (0..5)", "0", false, "0", "5", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_max_duration_s", ConfigType::INTEGER, "Video Max Duration (s)", "Maximum seconds for the RGB565 action video clip", "8", false, "2", "120", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_fps", ConfigType::INTEGER, "Video FPS", "Target frame rate for streamed RGB565 video clips", "20", false, "8", "60", "1", "", "", false, "", ValidationPolicy::Clamp),
        ConfigField("video_mode", ConfigType::ENUM, "Video Framing Mode", "Video framing strategy powered by dmd_gif_converter (Auto AI, Panorama, or Center Crop)", "auto_action", false, "", "", "", "auto_action,panorama,center_crop", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_poster", ConfigType::BOOLEAN, "Show Poster", "Display the fitted mini-jacket artwork on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_synopsis", ConfigType::BOOLEAN, "Show Synopsis", "Scroll the complete localized plot summary on the card", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("show_video", ConfigType::BOOLEAN, "Show Video Clip", "Stream the RGB565 video clip if available (skipped if unavailable)", "true", false, "", "", "", "", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("color_boost", ConfigType::ENUM, "LED Color Boost", "Colorimetry profile for HUB75 LED panels (Auto AI or fixed domain preset)", "auto", false, "", "", "", "auto,pixel_art,anime,cinema", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("lang", ConfigType::ENUM, "Metadata Language", "Language for titles and synopses", "system", false, "", "", "", "system,en,fr,es,de,it", "", false, "", ValidationPolicy::FallbackDefault),
        ConfigField("genres", ConfigType::LIST, "Anime Genres", "Select one or more anime genres dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/anime/filters", true, "", ValidationPolicy::Accept),
        ConfigField("decades", ConfigType::LIST, "Decades", "Select one or more decades dynamically retrieved from the Showcase server referential (leave empty for all)", "", false, "", "", "", "", "/api/v1/anime/filters", true, "", ValidationPolicy::Accept),
        ConfigField("video_type", ConfigType::ENUM, "Clip Type", "Preferred clip type (opening,ending,scene,any)", "opening", false, "", "", "", "any,opening,ending,scene", "", false, "", ValidationPolicy::FallbackDefault)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new AnimeShowcaseEngine()); };
    return desc;
}
