#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <strings.h>

class IDrawingSurface;

namespace engines::showcase {

enum class ShowcaseDomainId : uint8_t {
    Games = 0,
    Movies = 1,
    Series = 2,
    Anime = 3
};

struct ShowcaseCardData {
    static constexpr size_t MAX_TITLE_LEN = 80;
    static constexpr size_t MAX_BADGE_LEN = 12;
    static constexpr size_t MAX_SYNOPSIS_LEN = 256;

    char title[MAX_TITLE_LEN] = "";
    char year[8] = "";
    char badge[MAX_BADGE_LEN] = "";
    char rating[8] = "";
    char synopsis[MAX_SYNOPSIS_LEN] = "";
    uint16_t badgeColor565 = 0xFD20;
    uint16_t posterW = 32;
    uint16_t posterH = 32;
    bool hasData = false;
    bool hasPoster = false;
    bool hasVideoFrame = false;
};

/**
 * @class ShowcaseCardRenderer
 * @brief Stateless, zero-allocation Core 1 `IDrawingSurface` renderer for DMD Media Showcase:
 *        - UTF-8 NFD-folding CP437 + accented custom `5x7` & `3x5` pixel-font renderers with
 *          per-column `[clipMinX, clipMaxX)` clipping.
 *        - 4 domain-specific procedural Pixel-Art waiting animations (`Games`, `Movies`, `Series`, `Anime`).
 *        - Adaptive multi-geometry Info Card layouts (`128x32`, `128x64`/`256x64`, `64x64`, vertical TATE).
 */
class ShowcaseCardRenderer {
public:
    static const char* domainSlug(ShowcaseDomainId domain);
    static const char* domainBadgeLabel(ShowcaseDomainId domain);
    static uint16_t defaultAccent565(ShowcaseDomainId domain);

    static size_t countUtf8Glyphs(const char* utf8);
    static inline uint32_t synopsisPxPerSec(const char* speedStr) {
        if (!speedStr || speedStr[0] == '\0') return 30U;
        if (strcmp(speedStr, "1") == 0) return 15U;
        if (strcmp(speedStr, "2") == 0 || strcasecmp(speedStr, "slow") == 0) return 20U;
        if (strcmp(speedStr, "3") == 0) return 24U;
        if (strcmp(speedStr, "4") == 0 || strcasecmp(speedStr, "normal") == 0) return 30U;
        if (strcmp(speedStr, "5") == 0) return 40U;
        if (strcmp(speedStr, "6") == 0 || strcasecmp(speedStr, "fast") == 0) return 60U;
        if (strcmp(speedStr, "7") == 0) return 75U;
        if (strcmp(speedStr, "8") == 0) return 90U;
        if (strcmp(speedStr, "9") == 0) return 120U;
        if (strcmp(speedStr, "10") == 0) return 150U;
        return 30U;
    }
    static inline uint32_t effectiveScrollPxPerSec(uint32_t basePxPerSec, uint16_t panelW) {
        if (basePxPerSec == 0U) return 0U;
        if (panelW > 128U) {
            uint32_t cappedW = (panelW > 512U) ? 512U : static_cast<uint32_t>(panelW);
            return (basePxPerSec * cappedW + 64U) / 128U;
        }
        return basePxPerSec;
    }
    static inline uint32_t advanceCardScrollSubpixels(
        uint32_t& ioStepPx,
        uint32_t& ioSubpixelRem,
        uint32_t dtUs,
        uint32_t effPxPerSec
    ) {
        if (dtUs == 0U || effPxPerSec == 0U) {
            return ioStepPx;
        }
        // Lock ~60 px/s (e.g. default speed "4" scaled onto a 256px-wide panel) strictly 1:1 to
        // steady ~60 Hz display ticks (11..23 ms), eliminating both 30 Hz sample-and-hold ghosting
        // and the 2.5 Hz beat hitch between `FrameScheduler`'s 16 ms (62.5 Hz) tick and 60.0 px/s.
        if (effPxPerSec >= 54U && effPxPerSec <= 68U && dtUs >= 11000U && dtUs <= 23000U) {
            ioStepPx += 1U;
            return ioStepPx;
        }
        const uint32_t boundedUs = (dtUs > 50000U) ? 50000U : dtUs;
        const uint32_t cappedSpeed = (effPxPerSec > 600U) ? 600U : effPxPerSec;
        ioSubpixelRem += boundedUs * cappedSpeed;
        ioStepPx += ioSubpixelRem / 1000000U;
        ioSubpixelRem %= 1000000U;
        return ioStepPx;
    }
    static void computeCardPosterDims(uint16_t panelW, uint16_t panelH, uint16_t& outW, uint16_t& outH);
    static int computeCardTextViewportWidth(uint16_t panelW, uint16_t panelH, bool showPoster);

    static void drawClippedText5x7(
        IDrawingSurface* s,
        const char* utf8,
        int x,
        int y,
        int clipMinX,
        int clipMaxX,
        uint16_t color
    );

    static void drawClippedText3x5(
        IDrawingSurface* s,
        const char* utf8,
        int x,
        int y,
        int clipMinX,
        int clipMaxX,
        uint16_t color
    );

    static void drawScrollingLine(
        IDrawingSurface* s,
        const char* text,
        int minX,
        int maxX,
        int y,
        uint16_t color,
        uint32_t stepPx
    );

    static void drawSinglePassSynopsis(
        IDrawingSurface* s,
        const char* text,
        int minX,
        int maxX,
        int y,
        uint16_t color,
        uint32_t scrollPx
    );

    static void drawWaitingPixelArt(
        IDrawingSurface* s,
        ShowcaseDomainId domain,
        uint8_t statusState,
        int mW,
        int mH,
        uint32_t elapsedMs
    );

    static void drawCardPhase(
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
    );
};

} // namespace engines::showcase

