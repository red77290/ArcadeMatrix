/**
 * @file GNewsTickerUtils.h
 * @brief Zero-allocation single-line CP437 text sanitization and harmonic sub-pixel scroll helpers for GNews.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace GNewsTickerUtils {

/**
 * @brief Collapses ASCII control characters (`\r`, `\n`, `\t`, `< 0x20`, `0x7F`) and consecutive
 *        spaces into single `' '` characters in-place, trimming leading and trailing spaces.
 */
inline void collapseWhitespaceInPlace(char* str) {
    if (!str) return;
    size_t r = 0;
    size_t w = 0;
    while (str[r] != '\0') {
        const uint8_t b = static_cast<uint8_t>(str[r++]);
        if (b <= 0x20U || b == 0x7FU) {
            if (w > 0 && str[w - 1] != ' ') {
                str[w++] = ' ';
            }
        } else {
            str[w++] = static_cast<char>(b);
        }
    }
    if (w > 0 && str[w - 1] == ' ') {
        --w;
    }
    str[w] = '\0';
}

/**
 * @brief Converts UTF-8 input to single-line CP437 for the 5x7 GLCD font with zero heap allocation.
 *        All newlines (`\r`, `\n`), tabs, control bytes, NBSP (`U+00A0`), and line/paragraph separators
 *        are normalized to single collapsed spaces so horizontal ticker rendering never wraps or stalls.
 */
inline void formatSingleLineCp437(const char* utf8Input, char* output, size_t maxLen) {
    if (!output || maxLen == 0) return;
    output[0] = '\0';
    if (!utf8Input) return;

    static constexpr struct { uint16_t cp; uint8_t ch; } kCp437Map[] = {
        {0x00E9, 0x82}, {0x00E8, 0x8A}, {0x00EA, 0x88}, {0x00EB, 0x89},
        {0x00E0, 0x85}, {0x00E1, 0xA0}, {0x00E2, 0x83}, {0x00E4, 0x84},
        {0x00E5, 0x86}, {0x00E7, 0x87}, {0x00EE, 0x8C}, {0x00EF, 0x8B},
        {0x00EC, 0x8D}, {0x00ED, 0xA1}, {0x00F4, 0x93}, {0x00F6, 0x94},
        {0x00F2, 0x95}, {0x00F3, 0xA2}, {0x00F9, 0x97}, {0x00FB, 0x96},
        {0x00FC, 0x81}, {0x00FA, 0xA3}, {0x00F1, 0xA4}, {0x00C9, 0x90},
        {0x00C8, 'E'},  {0x00CA, 'E'},  {0x00CB, 'E'},  {0x00C0, 'A'},
        {0x00C1, 'A'},  {0x00C2, 'A'},  {0x00C4, 0x8E}, {0x00C5, 0x8F},
        {0x00C7, 0x80}, {0x00CE, 'I'},  {0x00CF, 'I'},  {0x00D4, 'O'},
        {0x00D6, 0x99}, {0x00D9, 'U'},  {0x00DB, 'U'},  {0x00DC, 0x9A},
        {0x00D1, 0xA5}, {0x00E6, 0x91}, {0x00C6, 0x92}, {0x00DF, 0xE1},
        {0x00AB, 0xAE}, {0x00BB, 0xAF}, {0x00A0, ' '},  {0x00B0, 0xF8}
    };

    size_t inIdx = 0;
    size_t outIdx = 0;
    const size_t inLen = strlen(utf8Input);

    while (inIdx < inLen && outIdx < maxLen - 1) {
        const uint8_t b1 = static_cast<uint8_t>(utf8Input[inIdx]);
        char seq[3] = {' ', '\0', '\0'};
        uint8_t seqLen = 1U;

        if (b1 < 0x80U) {
            inIdx++;
            seq[0] = (b1 <= 0x20U || b1 == 0x7FU) ? ' ' : static_cast<char>(b1);
        } else if ((b1 & 0xE0U) == 0xC0U) {
            if (inIdx + 1 >= inLen) break;
            const uint8_t b2 = static_cast<uint8_t>(utf8Input[inIdx + 1]);
            const uint32_t cp = (static_cast<uint32_t>(b1 & 0x1FU) << 6) | static_cast<uint32_t>(b2 & 0x3FU);
            inIdx += 2;

            if (cp == 0x0153U || cp == 0x0152U) {
                if (outIdx + 1 < maxLen - 1) {
                    seq[0] = (cp == 0x0153U) ? 'o' : 'O';
                    seq[1] = (cp == 0x0153U) ? 'e' : 'E';
                    seqLen = 2U;
                } else {
                    seqLen = 0U;
                }
            } else {
                for (const auto& entry : kCp437Map) {
                    if (entry.cp == cp) {
                        seq[0] = static_cast<char>(entry.ch);
                        break;
                    }
                }
            }
        } else if ((b1 & 0xF0U) == 0xE0U) {
            if (inIdx + 2 >= inLen) break;
            const uint8_t b2 = static_cast<uint8_t>(utf8Input[inIdx + 1]);
            const uint8_t b3 = static_cast<uint8_t>(utf8Input[inIdx + 2]);
            const uint32_t cp = (static_cast<uint32_t>(b1 & 0x0FU) << 12) |
                                (static_cast<uint32_t>(b2 & 0x3FU) << 6) |
                                static_cast<uint32_t>(b3 & 0x3FU);
            inIdx += 3;

            if (cp == 0x200BU || cp == 0x200CU || cp == 0x200DU || cp == 0xFEFFU) {
                continue;
            } else if (cp == 0x2026U) {
                seq[0] = '.';
                if (outIdx + 2 < maxLen - 1) {
                    seq[1] = '.';
                    seq[2] = '.';
                    seqLen = 3U;
                }
            } else if (cp == 0x2018U || cp == 0x2019U || cp == 0x201AU || cp == 0x2032U) {
                seq[0] = '\'';
            } else if (cp == 0x201CU || cp == 0x201DU || cp == 0x201EU) {
                seq[0] = '"';
            } else if (cp == 0x2013U || cp == 0x2014U || cp == 0x2212U) {
                seq[0] = '-';
            } else if (cp == 0x20ACU) {
                seq[0] = 'E';
            }
        } else if ((b1 & 0xF8U) == 0xF0U) {
            if (inIdx + 3 >= inLen) break;
            inIdx += 4;
        } else {
            inIdx++;
            continue;
        }

        for (uint8_t k = 0U; k < seqLen && outIdx < maxLen - 1; ++k) {
            const char ch = seq[k];
            if (ch == ' ' && (outIdx == 0 || output[outIdx - 1] == ' ')) {
                continue;
            }
            output[outIdx++] = ch;
        }
    }

    if (outIdx > 0 && output[outIdx - 1] == ' ') {
        --outIdx;
    }
    output[outIdx] = '\0';
}

/**
 * @brief Maps `scroll_speed` (`1..10`) to target horizontal ticker speed in pixels/second.
 *        Speeds 8 (`62 px/s`) and 10 (`125 px/s`) lock harmonically onto `1 px/frame` and
 *        `2 px/frame` at `FrameScheduler::CURRENT_REALTIME_INTERVAL` (`16 ms` = `62.5 Hz`),
 *        eliminating `2/3 px` frame-to-frame retinal smooth-pursuit judder.
 */
inline uint32_t scrollSpeedPxPerSec(int speed1To10) {
    static constexpr uint8_t kPxPerSec[10] = {10, 14, 18, 24, 31, 42, 56, 62, 94, 125};
    const int idx = (speed1To10 < 1) ? 0 : ((speed1To10 > 10) ? 9 : (speed1To10 - 1));
    return kPxPerSec[idx];
}

/**
 * @brief Advances integer pixel offset using an exact 32-bit microsecond sub-pixel remainder accumulator.
 */
inline bool advanceScrollSubpixels(int& pixelOffset, uint32_t& remUs, uint32_t deltaUs, uint32_t pxPerSec) {
    if (pxPerSec == 0U || deltaUs == 0U) {
        return false;
    }
    if (deltaUs > 50000U) {
        deltaUs = 50000U;
    }
    if (pxPerSec > 10000U) {
        pxPerSec = 10000U;
    }
    const uint32_t total = remUs + deltaUs * pxPerSec;
    const uint32_t stepPx = total / 1000000U;
    remUs = total - stepPx * 1000000U;
    if (stepPx == 0U) {
        return false;
    }
    pixelOffset += static_cast<int>(stepPx);
    return true;
}

} // namespace GNewsTickerUtils
