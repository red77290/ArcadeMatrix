#pragma once
#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace engines {

struct PixelcadeSystemVariant {
    String folder;
    String name;
};

struct PixelcadeQueryPlan {
    const char* rawGame = nullptr;
    const char* rawTitle = nullptr;
    size_t gLen = 0;
    size_t tLen = 0;
    char gameStripped[96] = "";
    char titleStripped[96] = "";
    char normGame[96] = "";
    char normTitle[96] = "";
    const char* ngAlias = nullptr;
    const char* ntAlias = nullptr;
    char gameMainNorm[96] = "";
    char titleMainNorm[96] = "";
    uint32_t firstCharMask = 0; // bits 0..25: 'a'..'z', bit 26: '0'..'9'
};

/**
 * @class PixelcadeArtResolver
 * @brief Pure, allocation-frugal system mapper, virtual-collection resolver, and
 *        catalog-backed marquee matcher shared by FrontendSyncEngine and host unit tests.
 *
 * Designed for both ESP32 (`esp32dev` / `ESP32-S3`) and host native C++17 verification
 * with zero network or filesystem dependencies.
 */
class PixelcadeArtResolver {
public:
    static String cleanSystemName(const String& rawSystem);
    static String formatDisplayTitle(const String& rawName);

    static std::vector<PixelcadeSystemVariant> getSystemNameVariants(const String& systemId);
    static std::vector<PixelcadeSystemVariant> getSystemNameVariantsMapped(
        const std::map<String, std::vector<String>>& mappings,
        const String& systemId
    );

    static String mapSystemToPixelcadeFolder(const String& systemId);
    static String getSecondaryPixelcadeFolder(const String& systemId);
    static bool isKnownEmptyPixelcadeFolder(const char* folder);

    static bool isVirtualCollectionSystem(const char* sys);
    static String extractSystemFromRomPath(const char* currentSys, const char* romPath);

    static void sanitizeLocalStem(const char* raw, char* out, size_t outCap);
    static void stripRomOrArtExtensionAndTags(const char* raw, char* out, size_t outCap);
    static void normalizeMarqueeKey(const char* raw, char* out, size_t outCap);

    static void buildQueryPlan(
        const char* rawGame,
        const char* rawTitle,
        PixelcadeQueryPlan& outPlan
    );

    static int scoreCatalogEntryWithPlan(
        const char* candidateFile,
        const PixelcadeQueryPlan& plan
    );

    static int scoreCatalogEntry(
        const char* candidateFile,
        const char* rawGame,
        const char* rawTitle,
        const char* normGame = nullptr,
        const char* normTitle = nullptr
    );

    static int scoreSystemCatalogEntry(
        const char* candidateFile,
        const std::vector<PixelcadeSystemVariant>& sysVariants
    );

    static String urlEncodeSegment(const char* raw);
    static uint32_t hashArtKey(const char* folder, const char* name);
};

} // namespace engines
