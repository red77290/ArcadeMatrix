#include "PixelcadeArtResolver.h"
#include "BuiltinSystemMaps.h"
#include <cctype>
#include <cstring>

namespace engines {

namespace {

char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

bool strEqIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (asciiLower(*a) != asciiLower(*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool strEqIgnoreCaseLen(const char* a, size_t aLen, const char* b) {
    if (!a || !b) return false;
    size_t bLen = strlen(b);
    if (aLen != bLen) return false;
    for (size_t i = 0; i < aLen; ++i) {
        if (asciiLower(a[i]) != asciiLower(b[i])) return false;
    }
    return true;
}

const char* findSubstringIgnoreCase(const char* haystack, const char* needle) {
    if (!haystack || !needle || *needle == '\0') return nullptr;
    const size_t nLen = strlen(needle);
    for (const char* p = haystack; *p != '\0'; ++p) {
        size_t i = 0U;
        while (i < nLen && p[i] != '\0' && asciiLower(p[i]) == asciiLower(needle[i])) {
            ++i;
        }
        if (i == nLen) return p;
    }
    return nullptr;
}

void compactNormalizedSystemKey(const char* src, char* out, size_t cap) {
    if (!out || cap == 0U) return;
    size_t w = 0U;
    if (src) {
        for (size_t i = 0U; src[i] != '\0' && w + 1U < cap; ++i) {
            char c = asciiLower(src[i]);
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                out[w++] = c;
            }
        }
    }
    out[w] = '\0';
}

size_t stripKnownRomOrImageExtLen(const char* s, size_t len) {
    static const char* const kExts[] = {
        ".png", ".gif", ".zip", ".7z", ".sfc", ".smc", ".nes", ".fds",
        ".md", ".gen", ".smd", ".bin", ".cue", ".iso", ".chd", ".gba",
        ".gbc", ".gb", ".nds", ".3ds", ".cci", ".z64", ".n64", ".v64",
        ".pce", ".cdi", ".gdi", ".pbp", ".cso", ".wad", ".rvz", ".wbfs",
        ".xci", ".nsp", ".dsk", ".adf", ".ipf", ".a26", ".a52", ".a78",
        ".lnx", ".j64", ".col", ".vec", ".ws", ".wsc", ".ngp", ".ngc"
    };
    for (const char* ext : kExts) {
        size_t eLen = strlen(ext);
        if (len > eLen && strEqIgnoreCaseLen(s + (len - eLen), eLen, ext)) {
            return len - eLen;
        }
    }
    return len;
}

// Translates common UTF-8 Latin-1 Supplement (0xC3 xx) and Latin Extended characters
// to unaccented ASCII equivalents (e.g. "Astérix" -> "asterix", "Pokémon" -> "pokemon").
size_t foldNextUtf8ToAsciiLower(const char* s, size_t idx, size_t len, char* outChars) {
    unsigned char b0 = static_cast<unsigned char>(s[idx]);
    if (b0 < 0x80U) {
        outChars[0] = asciiLower(static_cast<char>(b0));
        outChars[1] = '\0';
        return 1U;
    }
    if (b0 == 0xC3U && (idx + 1U) < len) {
        unsigned char b1 = static_cast<unsigned char>(s[idx + 1U]);
        // Normalize uppercase 0x80..0x9E to lowercase 0xA0..0xBE
        if (b1 >= 0x80U && b1 <= 0x9EU) {
            b1 = static_cast<unsigned char>(b1 + 0x20U);
        }
        outChars[1] = '\0';
        if (b1 >= 0xA0U && b1 <= 0xA5U) { outChars[0] = 'a'; return 2U; }
        if (b1 == 0xA6U) { outChars[0] = 'a'; outChars[1] = 'e'; outChars[2] = '\0'; return 2U; }
        if (b1 == 0xA7U) { outChars[0] = 'c'; return 2U; }
        if (b1 >= 0xA8U && b1 <= 0xABU) { outChars[0] = 'e'; return 2U; }
        if (b1 >= 0xACU && b1 <= 0xAFU) { outChars[0] = 'i'; return 2U; }
        if (b1 == 0xB1U) { outChars[0] = 'n'; return 2U; }
        if (b1 >= 0xB2U && b1 <= 0xB6U || b1 == 0xB8U) { outChars[0] = 'o'; return 2U; }
        if (b1 >= 0xB9U && b1 <= 0xBCU) { outChars[0] = 'u'; return 2U; }
        if (b1 == 0xBDU || b1 == 0xBFU) { outChars[0] = 'y'; return 2U; }
        if (b1 == 0x9FU) { outChars[0] = 's'; outChars[1] = 's'; outChars[2] = '\0'; return 2U; }
        outChars[0] = '\0';
        return 2U;
    }
    if (b0 == 0xC5U && (idx + 1U) < len) {
        unsigned char b1 = static_cast<unsigned char>(s[idx + 1U]);
        if (b1 == 0x92U || b1 == 0x93U) { // OE / oe
            outChars[0] = 'o';
            outChars[1] = 'e';
            outChars[2] = '\0';
            return 2U;
        }
        outChars[0] = '\0';
        return 2U;
    }
    // Skip multi-byte UTF-8 sequence (e.g. smart quotes ’ — ™)
    if ((b0 & 0xE0U) == 0xC0U && (idx + 1U) < len) { outChars[0] = '\0'; return 2U; }
    if ((b0 & 0xF0U) == 0xE0U && (idx + 2U) < len) { outChars[0] = '\0'; return 3U; }
    if ((b0 & 0xF8U) == 0xF0U && (idx + 3U) < len) { outChars[0] = '\0'; return 4U; }
    outChars[0] = '\0';
    return 1U;
}

bool isLeadingArticle(const char* s, size_t wordLen) {
    static const char* const kArticles[] = {
        "the", "les", "le", "la", "die", "der", "das", "el", "los", "las"
    };
    for (const char* art : kArticles) {
        if (strEqIgnoreCaseLen(s, wordLen, art)) return true;
    }
    return false;
}

bool isInvertedArticle(const char* s, size_t wordLen) {
    if (strEqIgnoreCaseLen(s, wordLen, "a") || strEqIgnoreCaseLen(s, wordLen, "an")) {
        return true;
    }
    return isLeadingArticle(s, wordLen);
}

void extractMainTitleBeforeSubtitle(const char* stripped, char* out, size_t outCap) {
    if (!out || outCap == 0U) return;
    out[0] = '\0';
    if (!stripped || stripped[0] == '\0') return;

    const char* bestSep = nullptr;
    static const char* const kSeps[] = { " - ", ": ", " : " };
    for (const char* sep : kSeps) {
        const char* p = strstr(stripped, sep);
        if (p && p > stripped && (!bestSep || p < bestSep)) {
            bestSep = p;
        }
    }
    if (!bestSep) return;

    size_t len = static_cast<size_t>(bestSep - stripped);
    while (len > 0U && stripped[len - 1U] == ' ') --len;
    if (len == 0U) return;
    if (len >= outCap) len = outCap - 1U;
    memcpy(out, stripped, len);
    out[len] = '\0';
}

const char* skipFourDigitReleasePrefix(const char* s) {
    if (!s) return nullptr;
    if (s[0] >= '0' && s[0] <= '9' &&
        s[1] >= '0' && s[1] <= '9' &&
        s[2] >= '0' && s[2] <= '9' &&
        s[3] >= '0' && s[3] <= '9') {
        const char* p = s + 4;
        while (*p == ' ') ++p;
        if (*p == '-') {
            ++p;
            while (*p == ' ') ++p;
            if (*p != '\0') return p;
        }
    }
    return nullptr;
}

void pushUniqueString(std::vector<String>& out, const String& val) {
    for (const auto& existing : out) {
        if (existing == val) return;
    }
    out.push_back(val);
}

void appendPrefixedVariant(std::vector<String>& out, const char* prefix, const String& suffix, const char* tail = nullptr) {
    String s(prefix);
    s += suffix;
    if (tail) s += tail;
    pushUniqueString(out, s);
}

void appendTargetsFromMappingsOrBuiltin(
    const std::map<String, std::vector<String>>& mappings,
    const String& key,
    std::vector<String>& out) {
    if (key.length() == 0) return;

    auto it = mappings.find(key);
    if (it != mappings.end()) {
        for (const auto& target : it->second) {
            pushUniqueString(out, target);
        }
    }

    for (size_t i = 0; BUILTIN_SYSTEM_MAPS[i].key != nullptr; i++) {
        if (key.equalsIgnoreCase(BUILTIN_SYSTEM_MAPS[i].key)) {
            const char* cur = BUILTIN_SYSTEM_MAPS[i].targets;
            while (*cur) {
                const char* comma = strchr(cur, ',');
                String t = comma
                    ? String(cur).substring(0, static_cast<size_t>(comma - cur))
                    : String(cur);
                cur = comma ? (comma + 1) : (cur + strlen(cur));
                t.trim();
                if (t.length() > 0) {
                    pushUniqueString(out, t);
                }
            }
            break;
        }
    }
}

void replaceAllInString(String& s, char fromCh, char toCh) {
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] == fromCh) s[i] = toCh;
    }
}

String removeCharFromString(const String& s, char dropCh) {
    String out;
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] != dropCh) out += s[i];
    }
    return out;
}

} // namespace

String PixelcadeArtResolver::cleanSystemName(const String& rawSystem) {
    String s = rawSystem;
    s.trim();

    String sNorm = s;
    sNorm.toLowerCase();
    replaceAllInString(sNorm, '_', ' ');
    replaceAllInString(sNorm, '-', ' ');

    static const char* const prefixes[] = {
        "arcade manufacturer ",
        "arcade system ",
        "arcade genre ",
        "arcade collection ",
        "manufacturer ",
        "system ",
        "genre ",
        "collection "
    };

    for (const char* p : prefixes) {
        if (sNorm.startsWith(p)) {
            size_t len = strlen(p);
            if (len <= s.length()) {
                String remainder = s.substring(len);
                if (remainder.startsWith("_") || remainder.startsWith("-")) {
                    remainder = remainder.substring(1);
                }
                remainder.trim();
                return remainder;
            }
        }
    }
    return s;
}

String PixelcadeArtResolver::formatDisplayTitle(const String& rawName) {
    String stripped = cleanSystemName(rawName);
    int idxParen = stripped.indexOf(" (");
    if (idxParen > 0) {
        stripped = stripped.substring(0, static_cast<size_t>(idxParen));
    }
    int idxBrack = stripped.indexOf(" [");
    if (idxBrack > 0) {
        stripped = stripped.substring(0, static_cast<size_t>(idxBrack));
    }
    replaceAllInString(stripped, '-', ' ');
    replaceAllInString(stripped, '_', ' ');
    stripped.trim();

    String out;
    bool prevSpace = true;
    for (size_t i = 0; i < stripped.length(); ++i) {
        char c = stripped[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!prevSpace) {
                out += ' ';
                prevSpace = true;
            }
        } else {
            if (prevSpace && c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - ('a' - 'A'));
            }
            out += c;
            prevSpace = false;
        }
    }
    out.trim();
    return out;
}

std::vector<PixelcadeSystemVariant> PixelcadeArtResolver::getSystemNameVariants(const String& rawSystem) {
    std::map<String, std::vector<String>> emptyMap;
    return getSystemNameVariantsMapped(emptyMap, rawSystem);
}

std::vector<PixelcadeSystemVariant> PixelcadeArtResolver::getSystemNameVariantsMapped(
    const std::map<String, std::vector<String>>& mappings,
    const String& rawSystem) {
    String clean = cleanSystemName(rawSystem);
    String rawLower = rawSystem;
    rawLower.trim();
    rawLower.toLowerCase();

    std::vector<String> nameVariants;

    String sysLower = clean;
    sysLower.toLowerCase();
    String sysNospace = removeCharFromString(sysLower, ' ');
    sysNospace = removeCharFromString(sysNospace, '_');
    sysNospace = removeCharFromString(sysNospace, '-');
    String sysUnderscore = sysLower;
    replaceAllInString(sysUnderscore, ' ', '_');
    replaceAllInString(sysUnderscore, '-', '_');
    String cleanKebab = sysLower;
    replaceAllInString(cleanKebab, ' ', '-');
    replaceAllInString(cleanKebab, '_', '-');

    // 1. Exact full-system lookups in SD mappings + built-in Flash table FIRST
    const String* lookupKeys[] = { &rawLower, &sysLower, &sysNospace, &sysUnderscore, &cleanKebab };
    for (const String* key : lookupKeys) {
        appendTargetsFromMappingsOrBuiltin(mappings, *key, nameVariants);
    }

    // 2. Direct full-system stems & standard Pixelcade prefixes BEFORE any partial token fallback
    const String* directForms[] = { &sysLower, &clean, &sysUnderscore, &sysNospace, &cleanKebab };
    for (const String* f : directForms) pushUniqueString(nameVariants, *f);
    for (const String* f : directForms) appendPrefixedVariant(nameVariants, "default-", *f);
    const String* underForms[] = { &clean, &sysLower, &sysUnderscore };
    for (const String* f : underForms) appendPrefixedVariant(nameVariants, "default-_", *f);

    appendPrefixedVariant(nameVariants, "default-z", sysLower);
    appendPrefixedVariant(nameVariants, "default-z", sysNospace);
    appendPrefixedVariant(nameVariants, "z", sysLower);
    appendPrefixedVariant(nameVariants, "z", sysNospace);

    appendPrefixedVariant(nameVariants, "default-arcade_", sysUnderscore, "_classics");
    appendPrefixedVariant(nameVariants, "default-arcade", sysNospace, "classics");
    appendPrefixedVariant(nameVariants, "arcade", sysNospace, "classics");
    appendPrefixedVariant(nameVariants, "arcade", sysNospace);
    appendPrefixedVariant(nameVariants, "default-manufacture_", sysUnderscore);
    appendPrefixedVariant(nameVariants, "default-manufacture_", sysLower);

    std::vector<String> uniqueBase;
    String sysUpper = clean;
    sysUpper.toUpperCase();
    String sysSpace = clean;
    replaceAllInString(sysSpace, '_', ' ');

    String sysTitle = clean;
    bool newWord = true;
    for (size_t i = 0; i < sysTitle.length(); i++) {
        if (sysTitle[i] == '_' || sysTitle[i] == ' ') {
            sysTitle[i] = ' ';
            newWord = true;
        } else if (newWord) {
            sysTitle[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(sysTitle[i])));
            newWord = false;
        } else {
            sysTitle[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(sysTitle[i])));
        }
    }

    const String* baseSeeds[] = { &clean, &sysLower, &sysNospace, &sysUnderscore, &sysUpper, &sysTitle, &sysSpace };
    for (const String* seed : baseSeeds) {
        pushUniqueString(uniqueBase, *seed);
    }

    struct SysAliasGroup {
        const char* keysCsv;
        const char* aliasesCsv;
    };
    static const SysAliasGroup kAliasGroups[] = {
        { "snes,supernintendo,sfc,superfamicom", "Super Nintendo|Super Nintendo Entertainment System|- Super Nintendo" },
        { "nes,famicom,fc", "Nintendo Entertainment System|3dnes" },
        { "megadrive,genesis", "genesis|Genesis|Mega Drive|SEGA Genesis|- Genesis" },
        { "mame,arcade,fbneo,fba", "arcade|Arcade|- Arcade|MAME|mame" },
        { "n64,nintendo64", "Nintendo 64" },
        { "gb,gameboy", "Game Boy" },
        { "gba,gameboyadvance", "Game Boy Advance" },
        { "gbc,gameboycolor", "Game Boy Color" },
        { "psx,ps1,playstation", "PlayStation|Sony PlayStation" },
        { "dreamcast", "Dreamcast|SEGA Dreamcast" },
        { "saturn,segasaturn", "sega_saturn|Saturn|SEGA Saturn" },
        { "neogeo,neogeocd", "Neo Geo|SNK Neo Geo" },
        { "atari,atari2600,atari7800,atari5200,atari800,atarilynx,atarijaguar,atarist", "atari|Atari|Atari_2600|Atari 2600|Atari_7800|Atari 7800" },
        { "mastersystem,sms", "Master System|SEGA Master System" },
        { "gamegear,gg", "Game Gear|SEGA Game Gear" },
        { "pcengine,tg16,pce", "NEC PC Engine|PC Engine" },
        { "amiga,amiga500,amiga1200", "Commodore Amiga|Amiga" },
        { "c64,commodore64", "COMMODORE_64|Commodore 64" },
    };
    for (const auto& grp : kAliasGroups) {
        bool keyMatched = false;
        const char* kc = grp.keysCsv;
        while (*kc) {
            const char* comma = strchr(kc, ',');
            size_t klen = comma ? static_cast<size_t>(comma - kc) : strlen(kc);
            if (sysLower.length() == klen && strncmp(sysLower.c_str(), kc, klen) == 0) {
                keyMatched = true;
                break;
            }
            kc = comma ? (comma + 1) : (kc + klen);
        }
        if (keyMatched) {
            const char* ac = grp.aliasesCsv;
            while (*ac) {
                const char* bar = strchr(ac, '|');
                String alias = bar
                    ? String(ac).substring(0, static_cast<size_t>(bar - ac))
                    : String(ac);
                pushUniqueString(uniqueBase, alias);
                ac = bar ? (bar + 1) : (ac + strlen(ac));
            }
            break;
        }
    }

    for (const auto& b : uniqueBase) {
        String bLower = b;
        bLower.toLowerCase();
        String bNospace = removeCharFromString(bLower, ' ');
        String bUnderscore = bLower;
        replaceAllInString(bUnderscore, ' ', '_');

        pushUniqueString(nameVariants, b);
        pushUniqueString(nameVariants, bLower);
        pushUniqueString(nameVariants, bUnderscore);
        appendPrefixedVariant(nameVariants, "default-", b);
        appendPrefixedVariant(nameVariants, "default-", bLower);
        appendPrefixedVariant(nameVariants, "default-_", b);
        appendPrefixedVariant(nameVariants, "default-z", bLower);
        appendPrefixedVariant(nameVariants, "default-z", bNospace);
        appendPrefixedVariant(nameVariants, "default-arcade_", bUnderscore, "_classics");
        appendPrefixedVariant(nameVariants, "default-arcade", bNospace, "classics");
        appendPrefixedVariant(nameVariants, "default-manufacture_", bUnderscore);
        appendPrefixedVariant(nameVariants, "default-manufacture_", bLower);
    }

    // 3. Embedded arcade hardware keywords ("Capcom CPS1" -> cps1, etc.)
    static const char* const embeddedKeywords[] = {
        "cps1", "cps2", "cps3", "atomiswave", "naomi2", "naomi", "neogeo", "model2", "model3", "hikaru", "triforce"
    };
    for (const char* kw : embeddedKeywords) {
        if (sysNospace.indexOf(kw) != -1) {
            String kwStr(kw);
            appendTargetsFromMappingsOrBuiltin(mappings, kwStr, nameVariants);
            appendPrefixedVariant(nameVariants, "default-z", kwStr);
            appendPrefixedVariant(nameVariants, "default-", kwStr);
        }
    }

    // 4. Hyphen / underscore / space sub-system manufacturer fallback
    // (e.g. Recalbox "taito-gnet" -> "taito", "namco-system11" -> "namco", "sega-system16" -> "sega", "konami-gx" -> "konami")
    auto isGenericSysToken = [](const char* tok) -> bool {
        static const char* const kGeneric[] = {
            "arcade", "manufacturer", "system", "genre", "collection", "auto", "custom",
            "super", "mega", "neo", "game", "boy", "color", "pocket", "advance",
            "entertainment", "computer", "video", "mini", "pro", "plus", "cd",
            "gnet", "gx", "m72", "m92", "zn", "stv"
        };
        if (!tok || strlen(tok) < 3U) return true;
        if (strncmp(tok, "system", 6) == 0) return true;
        for (const char* g : kGeneric) {
            if (strEqIgnoreCase(tok, g)) return true;
        }
        return false;
    };

    size_t tokStart = 0U;
    std::vector<String> tokens;
    for (size_t i = 0U; i <= sysLower.length(); ++i) {
        const bool isSep = (i == sysLower.length() || sysLower[i] == ' ' || sysLower[i] == '-' || sysLower[i] == '_');
        if (isSep) {
            if (i > tokStart) {
                String w = sysLower.substring(tokStart, i);
                w.trim();
                if (w.length() > 0 && w != sysLower && !isGenericSysToken(w.c_str())) {
                    tokens.push_back(w);
                }
            }
            tokStart = i + 1U;
        }
    }
    // Prefer more specific board token if listed at end ("capcom cps1"), then manufacturer prefix ("taito-gnet" -> "taito")
    for (size_t i = 0U; i < tokens.size(); ++i) {
        const String& w = tokens[i];
        appendTargetsFromMappingsOrBuiltin(mappings, w, nameVariants);
        pushUniqueString(nameVariants, w);
        appendPrefixedVariant(nameVariants, "default-", w);
        appendPrefixedVariant(nameVariants, "default-z", w);
        appendPrefixedVariant(nameVariants, "default-arcade_", w, "_classics");
        appendPrefixedVariant(nameVariants, "arcade", w, "classics");
    }

    std::vector<PixelcadeSystemVariant> list;
    list.reserve(nameVariants.size());
    for (const auto& n : nameVariants) {
        list.push_back({ "console", n });
    }
    return list;
}

String PixelcadeArtResolver::mapSystemToPixelcadeFolder(const String& systemId) {
    String cleaned = cleanSystemName(systemId);
    char key[48];
    compactNormalizedSystemKey(cleaned.c_str(), key, sizeof(key));
    if (key[0] == '\0') return systemId;

    struct Mapping { const char* systemId; const char* folder; };
    static const Mapping kPrimaryTable[] = {
        // Arcade / MAME / FBNeo / Publishers / Boards
        {"mame", "mame"}, {"mame2003", "mame"}, {"mame2003plus", "mame"}, {"mame2010", "mame"},
        {"fbneo", "mame"}, {"fba", "mame"}, {"fbalibretro", "mame"}, {"arcade", "mame"},
        {"hbmame", "mame"}, {"cps", "mame"}, {"cps1", "mame"}, {"cps2", "mame"}, {"cps3", "mame"},
        {"capcom", "mame"}, {"konami", "mame"}, {"konamigx", "mame"},
        {"taito", "mame"}, {"taitognet", "mame"}, {"dataeast", "mame"}, {"deco", "mame"},
        {"midway", "mame"}, {"irem", "mame"}, {"iremm72", "mame"}, {"iremm92", "mame"},
        {"namco", "mame"}, {"namcosystem10", "mame"}, {"namcosystem11", "mame"},
        {"namcosystem12", "mame"}, {"namcosystem21", "mame"}, {"namcosystem22", "mame"},
        {"namcosystem246", "mame"}, {"namcosystem256", "mame"},
        {"toaplan", "mame"}, {"technos", "mame"}, {"tecmo", "mame"}, {"koeitecmo", "mame"},
        {"psikyo", "mame"}, {"kaneko", "mame"}, {"atlus", "mame"}, {"seta", "mame"},
        {"sunsoft", "mame"}, {"videosystem", "mame"}, {"nmk", "mame"},
        {"visco", "mame"}, {"raizing", "mame"}, {"eighting", "mame"}, {"banpresto", "mame"},
        {"jaleco", "mame"}, {"nichibutsu", "mame"}, {"seibu", "mame"}, {"snk", "mame"},
        {"sammy", "mame"}, {"gaelco", "mame"}, {"igs", "mame"}, {"pgm", "mame"},
        {"williams", "mame"}, {"bally", "mame"}, {"gottlieb", "mame"}, {"stern", "mame"},
        {"universal", "mame"}, {"exidy", "mame"}, {"cinematronics", "mame"}, {"atari", "mame"},
        {"sega", "mame"}, {"segasystem1", "mame"}, {"segasystem2", "mame"},
        {"segasystem16", "mame"}, {"segasystem18", "mame"}, {"segasystem24", "mame"},
        {"segasystem32", "mame"}, {"segachihiro", "mame"}, {"segahikaru", "mame"},
        {"nintendoarcade", "nintendo_arcade_systems"},
        {"gw", "mame"}, {"gameandwatch", "mame"},
        {"cave", "cave"}, {"cave3rd", "cave"},
        {"neogeo", "neogeo"}, {"neogeomvs", "snk_neo_geo_mvs"}, {"neogeoaes", "snk_neo_geo_aes"},
        {"neogeocd", "snk_neo_geo_cd"}, {"neocdz", "snk_neo_geo_cd"},
        {"naomi", "sega_naomi"}, {"naomi2", "sega_naomi"}, {"naomigd", "sega_naomi"}, {"seganaomi", "sega_naomi"},
        {"atomiswave", "sammy_atomiswave"}, {"sammyatomiswave", "sammy_atomiswave"},
        {"model2", "sega_model_2"}, {"segamodel2", "sega_model_2"},
        // Upstream `sega_model_3` contains accidental Master System copies while real Model 3 ROMs
        // (`scud`, `daytona2`, `srally2`, `lemans24`, `swtrilgy`, `vf3`) live in `mame`!
        {"model3", "mame"}, {"segamodel3", "mame"},
        {"stv", "sega_st-v"}, {"segastv", "sega_st-v"},
        {"hikaru", "mame"}, {"chihiro", "mame"},
        {"triforce", "sega_triforce"}, {"segatriforce", "sega_triforce"},
        {"lindbergh", "teknoparrot"}, {"segalindbergh", "teknoparrot"},
        {"ringedge", "sega_ring"}, {"ringwide", "sega_ring"}, {"segaringedge", "sega_ring"}, {"segaringwide", "sega_ring"},
        {"taitotypex", "taito_type_x"}, {"typex", "taito_type_x"},
        {"teknoparrot", "teknoparrot"},
        {"zinc", "zinc"}, {"aae", "aae"},
        {"daphne", "daphne"}, {"hypseus", "daphne"},
        {"singe", "daphne_singe"}, {"daphnesinge", "daphne_singe"},
        {"actionmax", "wow_action_max"}, {"americanlasergames", "american_laser_games"}, {"alg", "american_laser_games"},
        {"lightgun", "gun_games"}, {"gungames", "gun_games"},

        // Nintendo Consoles & Handhelds
        {"nes", "nes"}, {"nintendoentertainmentsystem", "nes"},
        {"famicom", "nintendo_famicom"}, {"fc", "nintendo_famicom"}, {"nintendofamicom", "nintendo_famicom"},
        {"fds", "nintendo_famicom_disk_system"}, {"famicomdisksystem", "nintendo_famicom_disk_system"},
        {"snes", "snes"}, {"supernintendo", "snes"}, {"snesmsu1", "snes"}, {"snesturbo", "snes"},
        {"sfc", "nintendo_super_famicom"}, {"superfamicom", "nintendo_super_famicom"},
        {"satellaview", "nintendo_satellaview"}, {"bsx", "nintendo_satellaview"},
        {"sufami", "nintendo_sufami_turbo"}, {"sufamiturbo", "nintendo_sufami_turbo"},
        {"n64", "n64"}, {"nintendo64", "n64"}, {"n64dd", "n64"},
        {"gc", "nintendo_gamecube"}, {"gamecube", "nintendo_gamecube"}, {"ngc", "nintendo_gamecube"}, {"nintendogamecube", "nintendo_gamecube"},
        {"wii", "nintendo_wii"}, {"nintendowii", "nintendo_wii"}, {"wiiware", "nintendo_wii"},
        {"wiiu", "nintendo_wii_u"}, {"nintendowiiu", "nintendo_wii_u"},
        {"switch", "nintendo_switch"}, {"nintendoswitch", "nintendo_switch"},
        {"gb", "gb"}, {"gameboy", "gb"}, {"sgb", "gb"}, {"supergameboy", "gb"},
        {"gbc", "gbc"}, {"gameboycolor", "gbc"},
        {"gba", "gba"}, {"gameboyadvance", "gba"},
        {"nds", "nintendo_ds"}, {"ds", "nintendo_ds"}, {"nintendods", "nintendo_ds"},
        {"3ds", "nintendo_3ds"}, {"n3ds", "nintendo_3ds"}, {"nintendo3ds", "nintendo_3ds"},
        {"virtualboy", "virtualboy"}, {"vb", "virtualboy"},

        // SEGA Consoles & Handhelds
        {"sg1000", "sega_sg-1000"}, {"segasg1000", "sega_sg-1000"},
        {"mastersystem", "mastersystem"}, {"sms", "mastersystem"}, {"segamastersystem", "mastersystem"},
        {"megadrive", "genesis"}, {"genesis", "genesis"}, {"segamegadrive", "genesis"}, {"segagenesis", "genesis"},
        {"megadrivejp", "megadrive"},
        {"segacd", "segacd"}, {"megacd", "segacd"}, {"mcd", "segacd"},
        {"sega32x", "sega32x"}, {"32x", "sega32x"}, {"mega32x", "sega32x"},
        {"saturn", "sega_saturn"}, {"segasaturn", "sega_saturn"},
        {"dreamcast", "dreamcast"}, {"dc", "dreamcast"}, {"segadreamcast", "dreamcast"},
        {"gamegear", "gamegear"}, {"gg", "gamegear"}, {"segagamegear", "gamegear"},

        // Sony PlayStation
        {"psx", "psx"}, {"ps1", "psx"}, {"playstation", "psx"}, {"sonyplaystation", "psx"},
        {"ps2", "ps2"}, {"playstation2", "ps2"}, {"sonyplaystation2", "ps2"},
        {"ps3", "sony_playstation_3"}, {"playstation3", "sony_playstation_3"}, {"sonyplaystation3", "sony_playstation_3"},
        {"psp", "psp"}, {"playstationportable", "psp"},
        {"pspminis", "sony_playstation_minis"}, {"psminis", "sony_playstation_minis"},

        // Microsoft
        {"xbox", "microsoft_xbox"}, {"microsoftxbox", "microsoft_xbox"},

        // NEC
        {"pcengine", "pcengine"}, {"pce", "pcengine"},
        {"tg16", "nec_turbografx-16"}, {"turbografx16", "nec_turbografx-16"}, {"turbografx", "nec_turbografx-16"},
        {"pcenginecd", "nec_pc_engine-cd"}, {"pcecd", "nec_pc_engine-cd"},
        {"tg16cd", "nec_turbografx_cd"}, {"turbografxcd", "nec_turbografx_cd"}, {"cdrom2", "nec_turbografx_cd"},
        {"supergrafx", "supergrafx"}, {"sgx", "supergrafx"}, {"necsupergrafx", "nec_supergrafx"},
        {"pcfx", "nec_pc_fx"}, {"necpcfx", "nec_pc_fx"},

        // SNK / Bandai / Other Handhelds
        {"ngp", "ngpc"}, {"ngpc", "ngpc"}, {"neogeopocket", "ngpc"}, {"neogeopocketcolor", "ngpc"},
        {"wswan", "wonderswan"}, {"wonderswan", "wonderswan"}, {"ws", "wonderswan"},
        {"wswanc", "wonderswancolor"}, {"wonderswancolor", "wonderswancolor"}, {"wsc", "wonderswancolor"},
        {"supervision", "watara_supervision"}, {"watarasupervision", "watara_supervision"},
        {"megaduck", "megaduck"},
        {"casio", "casio_pv-1000"}, {"pv1000", "casio_pv-1000"}, {"casiopv1000", "casio_pv-1000"},
        {"adventurevision", "entex_adventure_vision"},

        // Atari / Classic Consoles / Computers / Pinball
        {"atari2600", "atari2600"}, {"a2600", "atari2600"},
        {"atari5200", "atari5200"}, {"a5200", "atari5200"},
        {"atari7800", "atari7800"}, {"a7800", "atari7800"},
        {"atari800", "atari800"}, {"atarixe", "atari800"}, {"xegs", "atari800"},
        {"atarist", "atari_st"}, {"atariste", "atari_st"},
        {"atarilynx", "atarilynx"}, {"lynx", "atarilynx"},
        {"atarijaguar", "atarijaguar"}, {"jaguar", "atarijaguar"}, {"atarijaguarcd", "atarijaguar"},
        {"colecovision", "coleco"}, {"coleco", "coleco"},
        {"intellivision", "intellivision"}, {"intv", "intellivision"},
        {"vectrex", "vectrex"},
        {"odyssey2", "odyssey"}, {"videopac", "odyssey"}, {"odyssey", "odyssey"},
        {"astrocade", "bally_astrocade"}, {"ballyastrocade", "bally_astrocade"},
        {"arcadia", "emerson_arcadia_2001"}, {"arcadia2001", "emerson_arcadia_2001"},
        {"3do", "3do"}, {"panasonic3do", "3do"},
        {"amiga", "amiga"}, {"amiga500", "amiga"}, {"amiga600", "amiga"}, {"amiga1200", "amiga"}, {"amigacd32", "amiga"}, {"cd32", "amiga"},
        {"c64", "c64"}, {"commodore64", "c64"},
        {"amstradcpc", "amstradcpc"}, {"cpc", "amstradcpc"},
        {"gx4000", "amstrad_gx4000"}, {"amstradgx4000", "amstrad_gx4000"},
        {"dos", "dos"}, {"pc", "pc"}, {"msdos", "dos"},
        {"scummvm", "scummvm"},
        {"ports", "ports"}, {"windows", "windows"}, {"openbor", "openbor"}, {"mugen", "mugen"}, {"ikemen", "mugen"},
        {"vpinball", "visualpinball"}, {"visualpinball", "visualpinball"},
        {"fpinball", "futurepinball"}, {"futurepinball", "futurepinball"},
        {"pinballfx", "pinball_fx"}, {"pinballfx2", "pinball_fx2"}, {"pinballfx3", "pinball_fx3"},
        {"zaccaria", "zaccaria"}
    };

    for (const auto& m : kPrimaryTable) {
        if (strcmp(key, m.systemId) == 0) {
            return String(m.folder);
        }
    }
    return cleaned.length() > 0 ? cleaned : systemId;
}

String PixelcadeArtResolver::getSecondaryPixelcadeFolder(const String& systemId) {
    String cleaned = cleanSystemName(systemId);
    char key[48];
    compactNormalizedSystemKey(cleaned.c_str(), key, sizeof(key));
    if (key[0] == '\0') return "";

    struct SecondaryMap { const char* systemId; const char* secondaryFolder; };
    static const SecondaryMap kSecondaryTable[] = {
        {"neogeo", "mame"},
        {"neogeomvs", "mame"},
        {"neogeoaes", "mame"},
        {"neogeocd", "neogeo"},
        {"neocdz", "neogeo"},
        {"cave", "mame"},
        {"cave3rd", "mame"},
        {"cps1", "capcom_play_system"},
        {"cps2", "capcom_play_system_ii"},
        {"cps3", "capcom_play_system_iii"},
        {"naomi", "mame"},
        {"naomi2", "mame"},
        {"naomigd", "mame"},
        {"seganaomi", "mame"},
        {"atomiswave", "mame"},
        {"sammyatomiswave", "mame"},
        {"model2", "mame"},
        {"segamodel2", "mame"},
        {"stv", "mame"},
        {"segastv", "mame"},
        {"triforce", "triforce"},
        {"segatriforce", "triforce"},
        {"zinc", "mame"},
        {"aae", "mame"},
        {"nintendoarcade", "mame"},
        {"singe", "daphne"},
        {"daphnesinge", "daphne"},
        {"sfc", "snes"},
        {"superfamicom", "snes"},
        {"snes", "nintendo_super_famicom"},
        {"supernintendo", "nintendo_super_famicom"},
        {"famicom", "nes"},
        {"fc", "nes"},
        {"nintendofamicom", "nes"},
        {"nes", "nintendo_famicom"},
        {"fds", "nes"},
        {"famicomdisksystem", "nes"},
        {"megadrive", "megadrive"},
        {"genesis", "megadrive"},
        {"megadrivejp", "genesis"},
        {"tg16", "pcengine"},
        {"turbografx16", "pcengine"},
        {"pcengine", "nec_turbografx-16"},
        {"pce", "nec_turbografx-16"},
        {"pcenginecd", "nec_turbografx_cd"},
        {"pcecd", "nec_turbografx_cd"},
        {"tg16cd", "nec_pc_engine-cd"},
        {"turbografxcd", "nec_pc_engine-cd"},
        {"supergrafx", "nec_supergrafx"},
        {"gx4000", "amstradcpc"},
        {"dos", "pc"},
        {"pc", "dos"},
        {"ports", "windows"},
        {"windows", "pc"}
    };

    for (const auto& m : kSecondaryTable) {
        if (strcmp(key, m.systemId) == 0) {
            return String(m.secondaryFolder);
        }
    }
    return "";
}

bool PixelcadeArtResolver::isKnownEmptyPixelcadeFolder(const char* folder) {
    if (!folder || folder[0] == '\0') return true;
    static const char* const kKnownEmpty[] = {
        "apple2", "commodore_cdtv", "commodore_vic_20", "doflinx", "driver",
        "fairchild_channel_f", "fba", "fds", "fonts", "fujitsu_fm_towns",
        "gamepark_gp32", "interton_electronic_vc_4000", "kodi", "macintosh",
        "magnavox_odyssey_2", "mattel_aquarius", "metadata", "mgt_sam_coupe",
        "microsoft_ms-dos", "microsoft_msx2", "misfit_mame", "msx", "msx1",
        "msx2", "msx2+", "msxturbor", "nds", "ngp", "nintendo_pokemon_mini",
        "pokemini", "philips_cd-i", "cdi", "pinball", "saturn", "search",
        "sega_pico", "pico", "sega_sc_3000", "sc-3000", "sc3000", "sega_vmu",
        "sg-1000", "sidekickwidgetart", "sinclair_zx81", "zx81",
        "snk_neo_geo_pocket", "sony_pocketstation", "steam", "system",
        "ti99", "ti994a", "toolkit", "user", "zmachine", "zxspectrum",
        "thomson", "to8", "mo5", "oric", "atmos", "samcoupe", "channelf",
        "vectrex2", "uzebox", "tic80", "pico8", "lowresnx", "wasm4", "lutro",
        "solarus", "easyrpg", "x68000", "pc88", "pc98", "fmtowns", "x1",
        "favorites", "allgames", "lastplayed", "imageviewer", "screenshots"
    };
    for (const char* emptyName : kKnownEmpty) {
        if (strEqIgnoreCase(folder, emptyName)) return true;
    }
    return false;
}

bool PixelcadeArtResolver::isVirtualCollectionSystem(const char* sys) {
    if (!sys || sys[0] == '\0') return true;
    while (*sys == ' ') ++sys;
    if (*sys == '\0') return true;

    if (strncasecmp(sys, "auto-", 5) == 0 ||
        strncasecmp(sys, "auto_", 5) == 0 ||
        strncasecmp(sys, "custom-", 7) == 0 ||
        strncasecmp(sys, "custom_", 7) == 0 ||
        strncasecmp(sys, "collection", 10) == 0) {
        return true;
    }

    char key[48];
    compactNormalizedSystemKey(sys, key, sizeof(key));
    static const char* const kVirtualKeys[] = {
        "favorites", "favourite", "favourites", "fav", "favs",
        "all", "allgames", "lastplayed", "recent", "recentlyplayed",
        "neverplayed", "multiplayer", "2players", "3players", "4players",
        "retroachievements", "lightgun", "vertical", "tate", "shmups",
        "fighting", "beatemup", "platform", "puzzle", "racing", "rpg", "sports"
    };
    for (const char* vk : kVirtualKeys) {
        if (strcmp(key, vk) == 0) return true;
    }
    return false;
}

String PixelcadeArtResolver::extractSystemFromRomPath(const char* currentSys, const char* romPath) {
    if (!isVirtualCollectionSystem(currentSys)) {
        return currentSys ? String(currentSys) : String("");
    }
    if (!romPath || romPath[0] == '\0') {
        return currentSys ? String(currentSys) : String("");
    }

    // Normalize backslashes on stack buffer if needed and search `/roms/<real_system>/<rom>`
    char normPath[256];
    size_t n = 0U;
    for (size_t i = 0U; romPath[i] != '\0' && n + 1U < sizeof(normPath); ++i) {
        normPath[n++] = (romPath[i] == '\\') ? '/' : romPath[i];
    }
    normPath[n] = '\0';

    const char* marker = findSubstringIgnoreCase(normPath, "/roms/");
    if (!marker) {
        return currentSys ? String(currentSys) : String("");
    }

    const char* sysStart = marker + 6; // after "/roms/"
    while (*sysStart == '/') ++sysStart;
    const char* sysEnd = strchr(sysStart, '/');
    if (!sysEnd || sysEnd <= sysStart) {
        return currentSys ? String(currentSys) : String("");
    }

    size_t segLen = static_cast<size_t>(sysEnd - sysStart);
    if (segLen == 0U || segLen >= 48U) {
        return currentSys ? String(currentSys) : String("");
    }

    char extracted[48];
    memcpy(extracted, sysStart, segLen);
    extracted[segLen] = '\0';
    if (isVirtualCollectionSystem(extracted)) {
        return currentSys ? String(currentSys) : String("");
    }
    return String(extracted);
}

void PixelcadeArtResolver::sanitizeLocalStem(const char* raw, char* out, size_t outCap) {
    if (!out || outCap == 0U) return;
    out[0] = '\0';
    if (!raw || raw[0] == '\0') return;

    // Take basename if any path separator slipped in
    const char* base = raw;
    for (const char* p = raw; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    size_t len = stripKnownRomOrImageExtLen(base, strlen(base));

    size_t w = 0U;
    for (size_t i = 0U; i < len && w + 1U < outCap; ++i) {
        unsigned char ch = static_cast<unsigned char>(base[i]);
        if (ch < 0x20U || ch == '/' || ch == '\\' || ch == ':' ||
            ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            if (w > 0U && out[w - 1U] != '_') {
                out[w++] = '_';
            }
        } else {
            out[w++] = static_cast<char>(ch);
        }
    }
    while (w > 0U && (out[w - 1U] == ' ' || out[w - 1U] == '.')) {
        --w;
    }
    out[w] = '\0';
}

void PixelcadeArtResolver::stripRomOrArtExtensionAndTags(const char* raw, char* out, size_t outCap) {
    if (!out || outCap == 0U) return;
    out[0] = '\0';
    if (!raw || raw[0] == '\0') return;

    size_t len = stripKnownRomOrImageExtLen(raw, strlen(raw));
    size_t w = 0U;
    int parenDepth = 0;
    int brackDepth = 0;
    bool prevSpace = true;

    for (size_t i = 0U; i < len; ++i) {
        char c = raw[i];
        if (c == '(') { ++parenDepth; continue; }
        if (c == ')' && parenDepth > 0) { --parenDepth; continue; }
        if (c == '[') { ++brackDepth; continue; }
        if (c == ']' && brackDepth > 0) { --brackDepth; continue; }
        if (parenDepth > 0 || brackDepth > 0) continue;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!prevSpace && w + 1U < outCap) {
                out[w++] = ' ';
                prevSpace = true;
            }
        } else {
            if (w + 1U < outCap) {
                out[w++] = c;
                prevSpace = false;
            }
        }
    }
    while (w > 0U && out[w - 1U] == ' ') --w;
    out[w] = '\0';
}

void PixelcadeArtResolver::normalizeMarqueeKey(const char* raw, char* out, size_t outCap) {
    if (!out || outCap == 0U) return;
    out[0] = '\0';
    if (!raw || raw[0] == '\0') return;

    char bufA[128];
    char bufB[128];
    stripRomOrArtExtensionAndTags(raw, bufA, sizeof(bufA));
    if (bufA[0] == '\0') return;

    // 1. Fold UTF-8 Latin accents to lowercase ASCII into bufB while keeping spaces, commas, hyphens, and '&'
    size_t fw = 0U;
    const size_t sLen = strlen(bufA);
    for (size_t i = 0U; i < sLen && fw + 3U < sizeof(bufB); ) {
        char asciiSeq[4] = {0, 0, 0, 0};
        size_t consumed = foldNextUtf8ToAsciiLower(bufA, i, sLen, asciiSeq);
        i += (consumed > 0U) ? consumed : 1U;
        for (size_t k = 0U; asciiSeq[k] != '\0' && fw + 1U < sizeof(bufB); ++k) {
            bufB[fw++] = asciiSeq[k];
        }
    }
    bufB[fw] = '\0';

    // 2. Strip inverted articles (`, the`, `, a`, `, an`, `, les`, `, die`, `, el`) into bufA
    size_t cw = 0U;
    for (size_t i = 0U; i < fw && cw + 1U < sizeof(bufA); ) {
        if (bufB[i] == ',') {
            size_t j = i + 1U;
            while (j < fw && bufB[j] == ' ') ++j;
            size_t wordStart = j;
            while (j < fw && bufB[j] >= 'a' && bufB[j] <= 'z') ++j;
            size_t wordLen = j - wordStart;
            if (wordLen > 0U && isInvertedArticle(bufB + wordStart, wordLen)) {
                size_t after = j;
                while (after < fw && bufB[after] == ' ') ++after;
                if (after == fw || (after + 2U <= fw && bufB[after] == '-' && bufB[after + 1U] == ' ')) {
                    i = j;
                    continue;
                }
            }
        }
        bufA[cw++] = bufB[i++];
    }
    bufA[cw] = '\0';

    // 3. Strip leading article (`the `, `les `, `le `, `la `, `die `, `el `) when followed by at least 1 word
    const char* startPtr = bufA;
    while (*startPtr == ' ') ++startPtr;
    {
        size_t wLen = 0U;
        while (startPtr[wLen] >= 'a' && startPtr[wLen] <= 'z') ++wLen;
        if (wLen > 0U && startPtr[wLen] == ' ' && isLeadingArticle(startPtr, wLen)) {
            const char* rest = startPtr + wLen;
            while (*rest == ' ') ++rest;
            if (*rest != '\0') {
                startPtr = rest;
            }
        }
    }

    // 4. Compact to alphanumeric key in bufB, expanding '&' ("&" -> "and")
    size_t w = 0U;
    for (size_t i = 0U; startPtr[i] != '\0' && w + 1U < sizeof(bufB); ++i) {
        char c = startPtr[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            bufB[w++] = c;
        } else if (c == '&') {
            if (w + 3U < sizeof(bufB)) {
                bufB[w++] = 'a';
                bufB[w++] = 'n';
                bufB[w++] = 'd';
            }
        }
    }
    bufB[w] = '\0';

    // Normalize European TMNT censor rename ("heroturtles" -> "ninjaturtles")
    const char* heroPos = strstr(bufB, "heroturtles");
    if (heroPos) {
        size_t prefixLen = static_cast<size_t>(heroPos - bufB);
        const char* repl = "ninjaturtles";
        const size_t replLen = 12U;
        const char* tail = heroPos + 11U;
        size_t tailLen = strlen(tail);
        size_t outIdx = 0U;
        for (size_t i = 0U; i < prefixLen && outIdx + 1U < outCap; ++i) out[outIdx++] = bufB[i];
        for (size_t i = 0U; i < replLen && outIdx + 1U < outCap; ++i) out[outIdx++] = repl[i];
        for (size_t i = 0U; i < tailLen && outIdx + 1U < outCap; ++i) out[outIdx++] = tail[i];
        out[outIdx] = '\0';
        return;
    }

    size_t copyLen = (w < outCap - 1U) ? w : (outCap - 1U);
    memcpy(out, bufB, copyLen);
    out[copyLen] = '\0';
}

namespace {

const char* resolveRegionalAliasNorm(const char* normKey) {
    if (!normKey || normKey[0] == '\0') return nullptr;
    struct AliasPair { const char* regionalKey; const char* canonicalKey; };
    static const AliasPair kAliases[] = {
        {"starwing", "starfox"},
        {"lylatwars", "starfox64"},
        {"castlevaniavampireskiss", "castlevaniadraculax"},
        {"castlevaniathenewgeneration", "castlevaniabloodlines"},
        {"castlevanianewgeneration", "castlevaniabloodlines"},
        {"superprobotectoralienrebels", "contraiiithealienwars"},
        {"probotector2returnoftheevilforces", "superc"},
        {"mysticquestlegend", "finalfantasymysticquest"},
        {"kirbysfunpak", "kirbysuperstar"},
        {"kirbysghosttrap", "kirbysavalanche"},
        {"illusionoftime", "illusionofgaia"},
        {"anotherworld", "outofthisworld"},
        {"jetsetradio", "jetgrindradio"},
        {"bareknuckle", "streetsofrage"},
        {"bareknuckleii", "streetsofrage2"},
        {"bareknuckle2", "streetsofrage2"},
        {"bareknuckleiii", "streetsofrage3"},
        {"bareknuckle3", "streetsofrage3"},
        {"pokemonversionrouge", "pokemonredversion"},
        {"pokemonversionbleue", "pokemonblueversion"},
        {"pokemonversionjaune", "pokemonyellowversion"},
        {"pokemonversionor", "pokemongoldversion"},
        {"pokemonversionargent", "pokemonsilverversion"},
        {"pokemonversioncristal", "pokemoncrystalversion"},
        {"pokemonversionrubis", "pokemonrubyversion"},
        {"pokemonversionsaphir", "pokemonsapphireversion"},
        {"pokemonversionemeraude", "pokemonemeraldversion"},
        {"pokemonversionrougefeu", "pokemonfireredversion"},
        {"pokemonversionvertfeuille", "pokemonleafgreenversion"},
        {"pokemonversiondiamant", "pokemondiamondversion"},
        {"pokemonversionperle", "pokemonpearlversion"},
        {"pokemonversionplatine", "pokemonplatinumversion"},
        {"pokemonversionorheartgold", "pokemonheartgoldversion"},
        {"pokemonversionamesoulsilver", "pokemonsoulsilverversion"},
        {"pokemonversionargentsoulsilver", "pokemonsoulsilverversion"},
        {"pokemonversionnoire", "pokemonblackversion"},
        {"pokemonversionblanche", "pokemonwhiteversion"},
        {"pokemonversionnoire2", "pokemonblackversion2"},
        {"pokemonversionblanche2", "pokemonwhiteversion2"}
    };
    for (const auto& pair : kAliases) {
        if (strcmp(normKey, pair.regionalKey) == 0) return pair.canonicalKey;
        if (strcmp(normKey, pair.canonicalKey) == 0) return pair.regionalKey;
    }
    return nullptr;
}

} // namespace

int PixelcadeArtResolver::scoreCatalogEntry(
    const char* candidateFile,
    const char* rawGame,
    const char* rawTitle,
    const char* normGame,
    const char* normTitle
) {
    if (!candidateFile || candidateFile[0] == '\0') return 0;
    const size_t candLen = strlen(candidateFile);
    if (candLen < 5U) return 0;

    bool isPng = false;
    if (strEqIgnoreCase(candidateFile + candLen - 4U, ".png")) {
        isPng = true;
    } else if (!strEqIgnoreCase(candidateFile + candLen - 4U, ".gif")) {
        return 0;
    }

    char candStem[128];
    size_t stemLen = candLen - 4U;
    if (stemLen >= sizeof(candStem)) stemLen = sizeof(candStem) - 1U;
    memcpy(candStem, candidateFile, stemLen);
    candStem[stemLen] = '\0';

    const size_t gLen = (rawGame && rawGame[0] != '\0') ? stripKnownRomOrImageExtLen(rawGame, strlen(rawGame)) : 0U;
    const size_t tLen = (rawTitle && rawTitle[0] != '\0') ? stripKnownRomOrImageExtLen(rawTitle, strlen(rawTitle)) : 0U;

    int baseScore = 0;
    if (gLen > 0U && strEqIgnoreCaseLen(rawGame, gLen, candStem)) {
        baseScore = 1000;
    } else if (tLen > 0U && strEqIgnoreCaseLen(rawTitle, tLen, candStem)) {
        baseScore = 980;
    }

    char candStripped[128];
    stripRomOrArtExtensionAndTags(candStem, candStripped, sizeof(candStripped));

    char scratchA[112] = "";
    char scratchB[112] = "";

    if (baseScore == 0 && gLen > 0U) {
        size_t copyG = (gLen < sizeof(scratchA) - 1U) ? gLen : (sizeof(scratchA) - 1U);
        memcpy(scratchA, rawGame, copyG);
        scratchA[copyG] = '\0';
        stripRomOrArtExtensionAndTags(scratchA, scratchB, sizeof(scratchB));
        if (scratchB[0] != '\0' && strEqIgnoreCase(candStripped, scratchB)) {
            baseScore = 880;
        }
    }
    if (baseScore == 0 && tLen > 0U) {
        size_t copyT = (tLen < sizeof(scratchA) - 1U) ? tLen : (sizeof(scratchA) - 1U);
        memcpy(scratchA, rawTitle, copyT);
        scratchA[copyT] = '\0';
        stripRomOrArtExtensionAndTags(scratchA, scratchB, sizeof(scratchB));
        if (scratchB[0] != '\0' && strEqIgnoreCase(candStripped, scratchB)) {
            baseScore = 860;
        }
    }

    if (baseScore == 0) {
        char localNormGame[96] = "";
        char localNormTitle[96] = "";
        const char* ng = normGame;
        if (!ng && rawGame && rawGame[0] != '\0') {
            normalizeMarqueeKey(rawGame, localNormGame, sizeof(localNormGame));
            ng = localNormGame;
        }
        const char* nt = normTitle;
        if (!nt && rawTitle && rawTitle[0] != '\0') {
            normalizeMarqueeKey(rawTitle, localNormTitle, sizeof(localNormTitle));
            nt = localNormTitle;
        }

        char candNorm[96];
        normalizeMarqueeKey(candStripped, candNorm, sizeof(candNorm));

        const char* afterNumPfx = skipFourDigitReleasePrefix(candStripped);
        char candNormNoNum[96] = "";
        if (afterNumPfx) {
            normalizeMarqueeKey(afterNumPfx, candNormNoNum, sizeof(candNormNoNum));
        }

        const char* ngAlias = resolveRegionalAliasNorm(ng);
        const char* ntAlias = resolveRegionalAliasNorm(nt);

        auto matchesCandNorm = [&](const char* target) -> bool {
            if (!target || target[0] == '\0') return false;
            return (strcmp(candNorm, target) == 0) ||
                   (candNormNoNum[0] != '\0' && strcmp(candNormNoNum, target) == 0);
        };

        if (matchesCandNorm(ng)) {
            baseScore = 760;
        } else if (matchesCandNorm(nt)) {
            baseScore = 750;
        } else if (matchesCandNorm(ngAlias)) {
            baseScore = 720;
        } else if (matchesCandNorm(ntAlias)) {
            baseScore = 710;
        } else {
            auto matchesMain = [](const char* mNorm, const char* target) -> bool {
                return mNorm && mNorm[0] != '\0' && strlen(mNorm) >= 4U && target && strcmp(mNorm, target) == 0;
            };

            // 1. Candidate has subtitle (`1943 - The Battle of Midway` vs `1943`)
            extractMainTitleBeforeSubtitle(candStripped, scratchA, sizeof(scratchA));
            if (scratchA[0] != '\0') {
                normalizeMarqueeKey(scratchA, scratchB, sizeof(scratchB));
                if (matchesMain(scratchB, ng) || matchesMain(scratchB, nt)) {
                    baseScore = 540;
                }
            }
            if (baseScore == 0 && afterNumPfx) {
                extractMainTitleBeforeSubtitle(afterNumPfx, scratchA, sizeof(scratchA));
                if (scratchA[0] != '\0') {
                    normalizeMarqueeKey(scratchA, scratchB, sizeof(scratchB));
                    if (matchesMain(scratchB, ng) || matchesMain(scratchB, nt)) {
                        baseScore = 540;
                    }
                }
            }
            // 2. Query/title has subtitle while candidate omits it
            if (baseScore == 0 && candNorm[0] != '\0' && strlen(candNorm) >= 5U) {
                if (rawGame && rawGame[0] != '\0') {
                    stripRomOrArtExtensionAndTags(rawGame, scratchA, sizeof(scratchA));
                    extractMainTitleBeforeSubtitle(scratchA, scratchB, sizeof(scratchB));
                    if (scratchB[0] != '\0') {
                        normalizeMarqueeKey(scratchB, scratchA, sizeof(scratchA));
                        if (strcmp(scratchA, candNorm) == 0) {
                            baseScore = 530;
                        }
                    }
                }
                if (baseScore == 0 && rawTitle && rawTitle[0] != '\0') {
                    stripRomOrArtExtensionAndTags(rawTitle, scratchA, sizeof(scratchA));
                    extractMainTitleBeforeSubtitle(scratchA, scratchB, sizeof(scratchB));
                    if (scratchB[0] != '\0') {
                        normalizeMarqueeKey(scratchB, scratchA, sizeof(scratchA));
                        if (strcmp(scratchA, candNorm) == 0) {
                            baseScore = 530;
                        }
                    }
                }
            }
        }
    }

    if (baseScore == 0) return 0;

    int bonus = isPng ? 4 : 0;
    if (strchr(candStem, '(') == nullptr) {
        bonus += 28;
    } else if (findSubstringIgnoreCase(candStem, "(World)")) {
        bonus += 30;
    } else if (findSubstringIgnoreCase(candStem, "(Europe)") ||
               findSubstringIgnoreCase(candStem, "(USA, Europe)") ||
               findSubstringIgnoreCase(candStem, "(France)")) {
        bonus += 26;
    } else if (findSubstringIgnoreCase(candStem, "(USA)") ||
               findSubstringIgnoreCase(candStem, "(US)")) {
        bonus += 24;
    } else if (findSubstringIgnoreCase(candStem, "(Japan)") ||
               findSubstringIgnoreCase(candStem, "(JP)")) {
        bonus += 10;
    }

    static const char* const kPenaltyTags[] = {
        "(Proto", "(Beta", "(Demo", "(Sample", "(Pirate", "(Unl)", "(Hack", "[b", "[h", "(Track"
    };
    for (const char* tag : kPenaltyTags) {
        if (findSubstringIgnoreCase(candStem, tag)) {
            bonus -= 25;
            break;
        }
    }
    if (findSubstringIgnoreCase(candStem, "(Disc 1)") || findSubstringIgnoreCase(candStem, "(Disk 1)")) {
        bonus += 2;
    } else if (findSubstringIgnoreCase(candStem, "(Disc ") || findSubstringIgnoreCase(candStem, "(Disk ")) {
        bonus -= 5;
    }

    int total = baseScore + bonus;
    return (total > 0) ? total : 1;
}

int PixelcadeArtResolver::scoreSystemCatalogEntry(
    const char* candidateFile,
    const std::vector<PixelcadeSystemVariant>& sysVariants
) {
    if (!candidateFile || candidateFile[0] == '\0') return 0;
    const size_t candLen = strlen(candidateFile);
    if (candLen < 5U) return 0;

    bool isPng = false;
    if (strEqIgnoreCase(candidateFile + candLen - 4U, ".png")) {
        isPng = true;
    } else if (!strEqIgnoreCase(candidateFile + candLen - 4U, ".gif")) {
        return 0;
    }

    const size_t stemLen = candLen - 4U;
    for (size_t idx = 0U; idx < sysVariants.size(); ++idx) {
        const char* target = sysVariants[idx].name.c_str();
        if (strEqIgnoreCaseLen(candidateFile, stemLen, target)) {
            // Earlier variants in `sysVariants` strictly outscore later fallbacks; `.png` beats `.gif` on same variant.
            int rankScore = 2000 - static_cast<int>(idx * 4U) + (isPng ? 2 : 0);
            return (rankScore > 10) ? rankScore : 10;
        }
    }
    return 0;
}

String PixelcadeArtResolver::urlEncodeSegment(const char* raw) {
    if (!raw || raw[0] == '\0') return String("");
    static const char kHex[] = "0123456789ABCDEF";
    String out;
    for (size_t i = 0U; raw[i] != '\0'; ++i) {
        unsigned char ch = static_cast<unsigned char>(raw[i]);
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out += static_cast<char>(ch);
        } else {
            out += '%';
            out += kHex[(ch >> 4) & 0x0FU];
            out += kHex[ch & 0x0FU];
        }
    }
    return out;
}

uint32_t PixelcadeArtResolver::hashArtKey(const char* folder, const char* name) {
    uint32_t h = 2166136261UL;
    auto feedStr = [&](const char* s) {
        if (!s) return;
        for (size_t i = 0U; s[i] != '\0'; ++i) {
            unsigned char ch = static_cast<unsigned char>(asciiLower(s[i]));
            h ^= ch;
            h *= 16777619UL;
        }
    };
    feedStr(folder);
    h ^= static_cast<unsigned char>('/');
    h *= 16777619UL;
    feedStr(name);
    return (h == 0U) ? 1U : h;
}

} // namespace engines
