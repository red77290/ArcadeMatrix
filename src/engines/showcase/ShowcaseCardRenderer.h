#pragma once

#include <cstddef>
#include <cstdint>

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
    static uint32_t synopsisPxPerSec(const char* speedStr);
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

