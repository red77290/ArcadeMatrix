#include "MusicEngine.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../core/Logger.h"
#include "../core/BuildInfo.h"

MusicEngine::MusicEngine()
    : _matrix(nullptr), _hasPsram(false),
      _showSource(true), _showTitle(true), _showArtist(true),
      _showProgress(true), _showVolume(true), _showAlbumArt(true),
      _showVisualizer(true), _marqueeSpeed(2),
      _cachedGeneration(0), _marqueeOffset(0),
      _lastMarqueeTick(0), _lastAnimTick(0), _animFrame(0) {}

void MusicEngine::applyConfig(const EngineConfig* config) {
    if (!config) return;
    _showSource = config->getBool("show_source", true);
    _showTitle = config->getBool("show_title", true);
    _showArtist = config->getBool("show_artist", true);
    _showProgress = config->getBool("show_progress", true);
    _showVolume = config->getBool("show_volume", true);
    _showAlbumArt = config->getBool("show_album_art", true);
    _showVisualizer = config->getBool("show_visualizer", true);
    _marqueeSpeed = config->getInt("marquee_speed", 2);
}

EngineError MusicEngine::initialize(EngineContext* context, const EngineConfig* config) {
    _matrix = context ? context->getMatrix() : nullptr;
    _hasPsram = context ? context->hasPsram() : false;
    applyConfig(config);

    LOGI("MusicEngine", "MusicEngine initialized. PSRAM: %s, Marquee Speed: %d",
         _hasPsram ? "ENABLED" : "DISABLED", _marqueeSpeed);
    return EngineError::OK;
}

void MusicEngine::activate() {
    _marqueeOffset = 0;
    _lastMarqueeTick = millis();
    _lastAnimTick = millis();
    _cachedState = audioHub.getPlaybackStateSnapshot();
    _cachedGeneration = _cachedState.generation;
}

void MusicEngine::deactivate() {}

void MusicEngine::onConfigChanged(const EngineConfig* config) {
    applyConfig(config);
}

void MusicEngine::update(EngineContext* context) {
    uint32_t now = millis();

    // Check for updated state snapshot from AudioHub
    AudioPlaybackState current = audioHub.getPlaybackStateSnapshot();
    if (current.generation != _cachedGeneration) {
        _cachedState = current;
        _cachedGeneration = current.generation;
    }

    // Marquee scroll step interval inversely proportional to speed (1..5)
    uint32_t scrollInterval = 60 - (_marqueeSpeed * 8);
    if (scrollInterval < 20) scrollInterval = 20;

    if (now - _lastMarqueeTick >= scrollInterval) {
        _marqueeOffset++;
        _lastMarqueeTick = now;
    }

    if (now - _lastAnimTick >= 80) {
        _animFrame = (_animFrame + 1) % 100;
        _lastAnimTick = now;
    }
}

uint16_t MusicEngine::getSourceColor(AudioSource source, MatrixPanel_I2S_DMA* display) {
    if (!display) return 0xFFFF;
    switch (source) {
        case AudioSource::BLUETOOTH: return display->color565(0, 122, 255);  // Bluetooth Blue
        case AudioSource::SPOTIFY:   return display->color565(30, 215, 96);   // Spotify Green
        case AudioSource::AIRPLAY:   return display->color565(220, 220, 230); // Apple AirPlay White
        case AudioSource::WEBRADIO:  return display->color565(255, 140, 0);   // Radio Orange
        default:                     return display->color565(100, 200, 255); // Cyan
    }
}

#include <glcdfont.c>

static void drawClippedText(MatrixPanel_I2S_DMA* display, const String& text, int x, int y, int clipMinX, int clipMaxX, uint16_t color) {
    if (!display || text.isEmpty()) return;
    int curX = x;
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if (curX >= clipMinX && curX + 6 <= clipMaxX) {
            display->drawChar(curX, y, c, color, 0, 1);
        } else if (curX + 6 > clipMinX && curX < clipMaxX) {
            for (int col = 0; col < 5; col++) {
                int px = curX + col;
                if (px >= clipMinX && px < clipMaxX) {
                    uint8_t line = pgm_read_byte(&font[c * 5 + col]);
                    for (int row = 0; row < 8; row++) {
                        if (line & 1) {
                            display->drawPixel(px, y + row, color);
                        }
                        line >>= 1;
                    }
                }
            }
        }
        curX += 6;
    }
}

void MusicEngine::renderMarqueeText(MatrixPanel_I2S_DMA* display, const String& text, int y, int clipMinX, int clipMaxX, uint16_t color) {
    if (!display || text.isEmpty()) return;
    int availW = clipMaxX - clipMinX;
    int textW = (int)text.length() * 6;

    if (textW <= availW) {
        drawClippedText(display, text, clipMinX, y, clipMinX, clipMaxX, color);
    } else {
        int gap = 20; // 20px seamless spacing between loop repetitions (matches RPi)
        int totalW = textW + gap;
        int dx = (_marqueeOffset % totalW + totalW) % totalW;

        // Draw primary text instance
        int drawX1 = clipMinX - dx;
        drawClippedText(display, text, drawX1, y, clipMinX, clipMaxX, color);

        // Draw trailing secondary instance for circular looping
        int drawX2 = drawX1 + totalW;
        if (drawX2 < clipMaxX) {
            drawClippedText(display, text, drawX2, y, clipMinX, clipMaxX, color);
        }
    }
}

#include "../services/AudioAnalysisService.h"

void MusicEngine::renderVisualizerBars(MatrixPanel_I2S_DMA* display, int x, int y, int width, int height, uint16_t color) {
    if (!display || width <= 0 || height <= 0) return;

    AudioVisualizerState fftState = audioAnalysisService.getVisualizerStateSnapshot();
    bool hasRealAudio = (fftState.peak > 0.02f);

    int numBars = width / 3;
    if (numBars < 4) numBars = 4;

    for (int i = 0; i < numBars; i++) {
        float energy = 0.0f;
        if (hasRealAudio) {
            int bandIdx = (i * 16) / numBars;
            if (bandIdx > 15) bandIdx = 15;
            energy = fftState.bands[bandIdx];
        } else {
            // Procedural animated equalizer fallback
            energy = (sinf((_animFrame * 0.2f) + (i * 0.8f)) * 0.5f + 0.5f) * 0.6f;
        }

        int barH = (int)(energy * (height - 1)) + 1;
        if (barH > height) barH = height;
        int barX = x + (i * 3);
        int barY = y + height - barH;

        uint16_t barColor = (barH > height * 0.7f) ? display->color565(255, 60, 60) : color;
        display->drawFastVLine(barX, barY, barH, barColor);
        display->drawFastVLine(barX + 1, barY, barH, barColor);
    }
}

void MusicEngine::renderIdle(MatrixPanel_I2S_DMA* display, int w, int h) {
    display->fillScreen(0);
    display->setFont(nullptr);
    display->setTextSize(1);

    String title = "ArcadeMatrix Music";
    String sub = "Ready to stream";

    int titleW = title.length() * 6;
    int xTitle = (titleW <= w - 4) ? (w - titleW) / 2 : 2;
    int yTitle = (h >= 64) ? ((h / 2) - 10) : 4;
    int ySub = (h >= 64) ? ((h / 2) + 4) : 16;

    if (titleW <= w - 4) {
        display->setTextColor(display->color565(0, 180, 255));
        display->setCursor(xTitle, yTitle);
        display->print(title);
    } else {
        renderMarqueeText(display, title, yTitle, 2, w - 2, display->color565(0, 180, 255));
    }

    renderMarqueeText(display, sub, ySub, 2, w - 2, display->color565(180, 180, 190));
}

#include "../services/ArtworkService.h"
#include "../core/LayoutHelper.h"

struct MusicLayout {
    Rect artworkRect;
    Rect badgeRect;
    Rect titleRect;
    Rect artistRect;
    Rect progressRect;
    Rect visualizerRect;
    bool isVertical;
};

class MusicLayoutCalculator {
public:
    static MusicLayout calculate(const DisplayGeometry& geometry, bool hasArt, bool showSource, bool showArtist, bool showProgress, bool showVisualizer) {
        MusicLayout l;
        uint16_t w = geometry.width;
        uint16_t h = geometry.height;
        l.isVertical = (geometry.layoutClass == LayoutClass::PORTRAIT || geometry.layoutClass == LayoutClass::TALL);

        if (l.isVertical) {
            // TATE / Portrait Layout
            int artSize = min((int)(w - 4), (int)(h * 0.35f));
            if (artSize < 16) artSize = 16;
            int artX = (w - artSize) / 2;
            l.artworkRect = Rect{ (int16_t)artX, 2, (uint16_t)artSize, (uint16_t)artSize };

            int curY = hasArt ? (2 + artSize + 2) : 2;
            l.badgeRect = Rect{ 2, (int16_t)curY, (uint16_t)(w - 4), 8 };
            if (showSource) curY += 10;

            l.titleRect = Rect{ 2, (int16_t)curY, (uint16_t)(w - 4), 8 };
            curY += 10;

            if (showArtist) {
                l.artistRect = Rect{ 2, (int16_t)curY, (uint16_t)(w - 4), 8 };
                curY += 10;
            } else {
                l.artistRect = Rect{ 0, 0, 0, 0 };
            }

            int bottomSpace = h - curY;
            if (bottomSpace >= 16 && showVisualizer) {
                if (showProgress) {
                    l.progressRect = Rect{ 2, (int16_t)curY, (uint16_t)(w - 4), 3 };
                    curY += 6;
                } else {
                    l.progressRect = Rect{ 0, 0, 0, 0 };
                }
                int visH = max(6, h - curY - 2);
                l.visualizerRect = Rect{ 2, (int16_t)curY, (uint16_t)(w - 4), (uint16_t)visH };
            } else if (showProgress) {
                l.progressRect = Rect{ 2, (int16_t)(h - 6), (uint16_t)(w - 4), 3 };
                l.visualizerRect = Rect{ 0, 0, 0, 0 };
            } else if (showVisualizer) {
                l.visualizerRect = Rect{ 2, (int16_t)(h - 10), (uint16_t)(w - 4), 8 };
                l.progressRect = Rect{ 0, 0, 0, 0 };
            }
        } else {
            // Landscape / Square Layout (Preserved 100% bit-for-bit with current layout)
            int artSize = (h >= 64) ? 30 : 22;
            l.artworkRect = Rect{ 2, 2, (uint16_t)artSize, (uint16_t)artSize };

            int leftMargin = hasArt ? (artSize + 6) : 2;
            int rightMargin = w - 2;
            int textW = max(0, rightMargin - leftMargin);

            l.badgeRect = Rect{ (int16_t)leftMargin, 2, (uint16_t)textW, 8 };
            l.titleRect = Rect{ (int16_t)leftMargin, 2, (uint16_t)textW, 8 };
            l.artistRect = Rect{ (int16_t)leftMargin, (int16_t)((h >= 64) ? 14 : 12), (uint16_t)textW, 8 };

            if (h >= 64) {
                l.progressRect = Rect{ 4, 28, (uint16_t)(w - 8), 4 };
                l.visualizerRect = Rect{ 4, 38, (uint16_t)(w - 8), 22 };
            } else {
                l.progressRect = Rect{ 2, 22, (uint16_t)(w - 4), 4 };
                l.visualizerRect = Rect{ 2, 22, (uint16_t)(w - 4), 8 };
            }
        }
        return l;
    }
};

void MusicEngine::renderPlaying(MatrixPanel_I2S_DMA* display, int w, int h, const AudioPlaybackState& state) {
    display->fillScreen(0);
    display->setFont(nullptr);
    display->setTextSize(1);

    uint16_t srcColor = getSourceColor(state.source, display);

    // 0. Check Album Artwork in PSRAM cache
    int artW = 0, artH = 0;
    const uint16_t* artBmp = (_showAlbumArt && !state.artworkId.isEmpty()) ? artworkService.getArtworkBitmap(state.artworkId, artW, artH) : nullptr;
    bool hasArt = (artBmp && artW > 0 && artH > 0);

    DisplayGeometry geom;
    geom.width = w;
    geom.height = h;
    geom.layoutClass = DisplayGeometry::classify(w, h);

    MusicLayout layout = MusicLayoutCalculator::calculate(geom, hasArt, _showSource, _showArtist, _showProgress, _showVisualizer);

    // 1. Draw Artwork
    if (hasArt) {
        int drawW = min(artW, (int)layout.artworkRect.width);
        int drawH = min(artH, (int)layout.artworkRect.height);
        display->drawRGBBitmap(layout.artworkRect.x, layout.artworkRect.y, artBmp, drawW, drawH);
    }

    // 2. Draw Source Badge & Title
    int titleMinX = layout.titleRect.x;
    int titleMaxX = layout.titleRect.x + layout.titleRect.width;

    if (!layout.isVertical && _showSource && state.source != AudioSource::NONE) {
        String srcBadge = "[" + String(AudioHub::getSourceName(state.source)) + "]";
        display->setTextColor(srcColor);
        display->setCursor(titleMinX, layout.titleRect.y);
        display->print(srcBadge);
        titleMinX += (srcBadge.length() * 6 + 4);
    } else if (layout.isVertical && _showSource && state.source != AudioSource::NONE && !layout.badgeRect.isEmpty()) {
        String srcBadge = "[" + String(AudioHub::getSourceName(state.source)) + "]";
        int badgeX = (w - (int)(srcBadge.length() * 6)) / 2;
        display->setTextColor(srcColor);
        display->setCursor(max(2, badgeX), layout.badgeRect.y);
        display->print(srcBadge);
    }

    String titleText = state.title.length() > 0 ? state.title : "Audio Stream";
    renderMarqueeText(display, titleText, layout.titleRect.y, titleMinX, titleMaxX, display->color565(255, 255, 255));

    // 3. Draw Artist
    if (_showArtist && state.artist.length() > 0 && !layout.artistRect.isEmpty()) {
        renderMarqueeText(display, state.artist, layout.artistRect.y, layout.artistRect.x, layout.artistRect.x + layout.artistRect.width, display->color565(190, 190, 200));
    }

    // 4. Draw Progress Bar
    if (_showProgress && state.durationMs > 0 && !layout.progressRect.isEmpty()) {
        float pct = (float)state.positionMs / (float)state.durationMs;
        if (pct > 1.0f) pct = 1.0f;
        display->drawRect(layout.progressRect.x, layout.progressRect.y, layout.progressRect.width, layout.progressRect.height, display->color565(60, 60, 70));
        int fillW = (int)(pct * (layout.progressRect.width - 2));
        if (fillW > 0) {
            display->fillRect(layout.progressRect.x + 1, layout.progressRect.y + 1, fillW, layout.progressRect.height - 2, srcColor);
        }
    }

    // 5. Draw Visualizer Bars
    if (_showVisualizer && (_cachedState.status == PlaybackStatus::STATUS_PLAYING) && !layout.visualizerRect.isEmpty()) {
        renderVisualizerBars(display, layout.visualizerRect.x, layout.visualizerRect.y, layout.visualizerRect.width, layout.visualizerRect.height, srcColor);
    }
}

void MusicEngine::render(EngineContext* context) {
    if (!_matrix) return;

    int w = _matrix->width();
    int h = _matrix->height();

    if (_cachedState.status == PlaybackStatus::STATUS_STOPPED || _cachedState.title.isEmpty()) {
        renderIdle(_matrix, w, h);
    } else {
        renderPlaying(_matrix, w, h, _cachedState);
    }
}

EngineDescriptor MusicEngineDescriptorHandler::getDescriptor() const {
    EngineDescriptor desc;
    desc.metadata = { "music_player", "Universal Music Player", "media", FIRMWARE_VERSION };
    desc.capabilities.supports_128x32 = true;
    desc.capabilities.supports_256x64 = true;
    desc.capabilities.realtime = true;
    desc.requirements.needsPsram = false;
    desc.requirements.needsAudio = false;
    desc.schema.fields = {
        ConfigField("show_source", ConfigType::BOOLEAN, "Show Source Badge", "Display source logo (Bluetooth, Spotify, AirPlay, Radio)", "true"),
        ConfigField("show_title", ConfigType::BOOLEAN, "Show Title", "Display scrolling track title", "true"),
        ConfigField("show_artist", ConfigType::BOOLEAN, "Show Artist", "Display artist or radio channel", "true"),
        ConfigField("show_progress", ConfigType::BOOLEAN, "Show Progress Bar", "Display playback time progress bar", "true"),
        ConfigField("show_visualizer", ConfigType::BOOLEAN, "Show Visualizer", "Display animated audio equalizer bars", "true"),
        ConfigField("marquee_speed", ConfigType::INTEGER, "Marquee Speed", "Scrolling text speed", "2", false, "1", "5", "1", "", "", false, "", ValidationPolicy::Clamp)
    };
    desc.factory = []() { return std::unique_ptr<IEngine>(new MusicEngine()); };
    return desc;
}
