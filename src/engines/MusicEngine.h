#pragma once
#include <Arduino.h>
#include "../core/AppEngineContext.h"
#include "../../include/core/EngineContract.h"
#include "../core/AudioHub.h"

/**
 * @class MusicEngine
 * @brief Universal Display Engine for Now Playing Music & Audio Streams.
 * Consumes AudioPlaybackState snapshots from AudioHub without knowing transport details.
 */
class MusicEngine : public IEngine {
public:
    MusicEngine();
    virtual ~MusicEngine() = default;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;

    bool isRealtime() const override { return true; }

private:
    MatrixPanel_I2S_DMA* _matrix;
    bool _hasPsram;

    // Config options
    bool _showSource;
    bool _showTitle;
    bool _showArtist;
    bool _showProgress;
    bool _showVolume;
    bool _showAlbumArt;
    bool _showVisualizer;
    int _marqueeSpeed;

    // Runtime state cache
    AudioPlaybackState _cachedState;
    uint32_t _cachedGeneration;
    int _marqueeOffset;
    uint32_t _lastMarqueeTick;
    uint32_t _lastAnimTick;
    uint8_t _animFrame;

    void applyConfig(const EngineConfig* config);
    void renderIdle(MatrixPanel_I2S_DMA* display, int w, int h);
    void renderPlaying(MatrixPanel_I2S_DMA* display, int w, int h, const AudioPlaybackState& state);
    void renderMarqueeText(MatrixPanel_I2S_DMA* display, const String& text, int y, int clipMinX, int clipMaxX, uint16_t color);
    void renderVisualizerBars(MatrixPanel_I2S_DMA* display, int x, int y, int width, int height, uint16_t color);
    uint16_t getSourceColor(AudioSource source, MatrixPanel_I2S_DMA* display);
};

/**
 * @class MusicEngineDescriptorHandler
 * @brief Descriptor and Schema Provider for MusicEngine.
 */
class MusicEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
