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

static void drawClippedText(MatrixPanel_I2S_DMA* display, const String& text, int x, int y, int clipMinX, int clipMaxX, uint16_t color) {
    if (!display || text.isEmpty()) return;
    int curX = x;
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if (curX + 6 > clipMinX && curX < clipMaxX) {
            display->drawChar(curX, y, c, color, 0, 1);
        }
        curX += 6;
    }
}

void MusicEngine::renderMarqueeText(MatrixPanel_I2S_DMA* display, const String& text, int y, int clipMinX, int clipMaxX, uint16_t color) {
    int availW = clipMaxX - clipMinX;
    int textW = (int)text.length() * 6;
    int drawX = clipMinX;

    if (textW > availW) {
        int overflow = textW - availW + 12;
        int pauseStart = 30; // ~1s pause before start
        int pauseEnd = 20;   // ~0.7s pause at end
        int cycle = max(1, pauseStart + overflow + pauseEnd);
        int phase = (_marqueeOffset % cycle + cycle) % cycle;
        int dx = (phase < pauseStart) ? 0 : (phase < pauseStart + overflow ? (phase - pauseStart) : overflow);
        drawX = clipMinX - dx;
    }

    drawClippedText(display, text, drawX, y, clipMinX, clipMaxX, color);
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

void MusicEngine::renderPlaying(MatrixPanel_I2S_DMA* display, int w, int h, const AudioPlaybackState& state) {
    display->fillScreen(0);
    display->setFont(nullptr);
    display->setTextSize(1);

    uint16_t srcColor = getSourceColor(state.source, display);
    int clipMinX = 2;
    int clipMaxX = w - 2;

    // 0. Render Album Artwork if available in PSRAM cache
    int artW = 0, artH = 0;
    const uint16_t* artBmp = (_showAlbumArt && !state.artworkId.isEmpty()) ? artworkService.getArtworkBitmap(state.artworkId, artW, artH) : nullptr;
    if (artBmp && artW > 0 && artH > 0) {
        int drawH = min(artH, (h >= 64) ? 30 : 22);
        int drawW = min(artW, drawH);
        display->drawRGBBitmap(2, 2, artBmp, drawW, drawH);
        clipMinX += (drawW + 4);
    }

    // Header badge (e.g. "[SPOTIFY]" or "[RADIO]")
    int yTop = 2;
    if (_showSource && state.source != AudioSource::NONE) {
        String srcBadge = "[" + String(AudioHub::getSourceName(state.source)) + "]";
        display->setTextColor(srcColor);
        display->setCursor(clipMinX, yTop);
        display->print(srcBadge);
        clipMinX += (srcBadge.length() * 6 + 4);
    }

    // Title Marquee (Top line)
    String titleText = state.title.length() > 0 ? state.title : "Audio Stream";
    renderMarqueeText(display, titleText, yTop, clipMinX, clipMaxX, display->color565(255, 255, 255));

    // Artist / Channel (Second line)
    int yArtist = (h >= 64) ? 14 : 12;
    int artistMinX = (artBmp && artW > 0) ? (min(artW, (h >= 64) ? 30 : 22) + 6) : 2;
    if (_showArtist && state.artist.length() > 0) {
        renderMarqueeText(display, state.artist, yArtist, artistMinX, clipMaxX, display->color565(190, 190, 200));
    }

    // Progress Bar or Visualizer on bottom half
    if (h >= 64) {
        // 64px tall displays: full layout with progress & visualizer
        int yProgress = 28;
        if (_showProgress && state.durationMs > 0) {
            float pct = (float)state.positionMs / (float)state.durationMs;
            if (pct > 1.0f) pct = 1.0f;
            int barW = w - 8;
            display->drawRect(4, yProgress, barW, 4, display->color565(60, 60, 70));
            int fillW = (int)(pct * (barW - 2));
            if (fillW > 0) {
                display->fillRect(5, yProgress + 1, fillW, 2, srcColor);
            }
        }

        if (_showVisualizer && (_cachedState.status == PlaybackStatus::STATUS_PLAYING)) {
            renderVisualizerBars(display, 4, 38, w - 8, 22, srcColor);
        }
    } else {
        // 32px displays: compact visualizer / progress
        int yBottom = 22;
        if (_showVisualizer && (_cachedState.status == PlaybackStatus::STATUS_PLAYING)) {
            renderVisualizerBars(display, 2, yBottom, w - 4, 8, srcColor);
        } else if (_showProgress && state.durationMs > 0) {
            float pct = (float)state.positionMs / (float)state.durationMs;
            if (pct > 1.0f) pct = 1.0f;
            int barW = w - 4;
            display->drawRect(2, yBottom, barW, 4, display->color565(60, 60, 70));
            int fillW = (int)(pct * (barW - 2));
            if (fillW > 0) display->fillRect(3, yBottom + 1, fillW, 2, srcColor);
        }
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
