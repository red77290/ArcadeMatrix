#include "BitmapFontLoader.h"

// "AMF1" read as a little-endian uint32.
static const uint32_t AMFONT_MAGIC = 0x31464D41;

BitmapFontLoader::BitmapFontLoader() : glyphs(nullptr), bitmap(nullptr), loaded(false) {
    font.bitmap = nullptr;
    font.glyph = nullptr;
    font.first = 0;
    font.last = 0;
    font.yAdvance = 0;
}

BitmapFontLoader::~BitmapFontLoader() {
    unload();
}

void BitmapFontLoader::unload() {
    if (glyphs) { free(glyphs); glyphs = nullptr; }
    if (bitmap) { free(bitmap); bitmap = nullptr; }
    loaded = false;
    font.bitmap = nullptr;
    font.glyph = nullptr;
}

bool BitmapFontLoader::loadFromSD(const char* path) {
    unload();

    FsFile f = sd.open(path, O_READ);
    if (!f) {
        Serial.printf("BitmapFontLoader: cannot open %s\n", path);
        return false;
    }

    // --- Header (12 bytes): magic(4) + first(2) + last(2) + yAdvance(1) + reserved(1) + glyphCount(2)
    uint8_t header[12];
    if (f.read(header, sizeof(header)) != (int)sizeof(header)) {
        Serial.println("BitmapFontLoader: truncated header");
        f.close();
        return false;
    }
    uint32_t magic = header[0] | (header[1] << 8) | ((uint32_t)header[2] << 16) | ((uint32_t)header[3] << 24);
    if (magic != AMFONT_MAGIC) {
        Serial.println("BitmapFontLoader: bad magic (not a valid .amf file)");
        f.close();
        return false;
    }
    uint16_t first = header[4] | (header[5] << 8);
    uint16_t last = header[6] | (header[7] << 8);
    uint8_t yAdvance = header[8];
    uint16_t glyphCount = header[10] | (header[11] << 8);

    if (glyphCount == 0 || (uint32_t)glyphCount != (uint32_t)(last - first + 1)) {
        Serial.println("BitmapFontLoader: inconsistent glyph range in header");
        f.close();
        return false;
    }

    glyphs = (GFXglyph*)malloc(sizeof(GFXglyph) * glyphCount);
    if (!glyphs) {
        Serial.println("BitmapFontLoader: out of memory for glyph table");
        f.close();
        return false;
    }

    // --- Glyph table: glyphCount entries of (bitmapOffset:u32, width:u8, height:u8,
    //     xAdvance:u8, xOffset:i8, yOffset:i8) = 9 bytes each, little-endian.
    for (uint16_t i = 0; i < glyphCount; i++) {
        uint8_t entry[9];
        if (f.read(entry, sizeof(entry)) != (int)sizeof(entry)) {
            Serial.println("BitmapFontLoader: truncated glyph table");
            free(glyphs); glyphs = nullptr;
            f.close();
            return false;
        }
        uint32_t bitmapOffset = entry[0] | (entry[1] << 8) | ((uint32_t)entry[2] << 16) | ((uint32_t)entry[3] << 24);

        // GFXglyph.bitmapOffset (Adafruit_GFX's own struct) is only a uint16_t - the same
        // 64KB-per-font ceiling applies to the project's compiled-in fonts too, so this isn't
        // a limitation we're introducing, just one we must guard against explicitly here.
        if (bitmapOffset > 0xFFFF) {
            Serial.println("BitmapFontLoader: font too large (bitmap offset overflows 16 bits)");
            free(glyphs); glyphs = nullptr;
            f.close();
            return false;
        }

        glyphs[i].bitmapOffset = (uint16_t)bitmapOffset;
        glyphs[i].width = entry[4];
        glyphs[i].height = entry[5];
        glyphs[i].xAdvance = entry[6];
        glyphs[i].xOffset = (int8_t)entry[7];
        glyphs[i].yOffset = (int8_t)entry[8];
    }

    // --- Bitmap blob: whatever remains in the file.
    size_t bitmapBytes = f.available();
    if (bitmapBytes == 0) {
        Serial.println("BitmapFontLoader: empty bitmap data");
        free(glyphs); glyphs = nullptr;
        f.close();
        return false;
    }
    bitmap = (uint8_t*)malloc(bitmapBytes);
    if (!bitmap) {
        Serial.println("BitmapFontLoader: out of memory for bitmap data");
        free(glyphs); glyphs = nullptr;
        f.close();
        return false;
    }
    if (f.read(bitmap, bitmapBytes) != (int)bitmapBytes) {
        Serial.println("BitmapFontLoader: truncated bitmap data");
        free(glyphs); glyphs = nullptr;
        free(bitmap); bitmap = nullptr;
        f.close();
        return false;
    }
    f.close();

    font.bitmap = bitmap;
    font.glyph = glyphs;
    font.first = first;
    font.last = last;
    font.yAdvance = yAdvance;
    loaded = true;

    Serial.printf("BitmapFontLoader: loaded %s (%u glyphs, %u bitmap bytes)\n",
                  path, (unsigned)glyphCount, (unsigned)bitmapBytes);
    return true;
}

GFXfont* BitmapFontLoader::getFont() {
    return loaded ? &font : nullptr;
}
