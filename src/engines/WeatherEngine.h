#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#include <atomic>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <vector>
#include "../api/IWeatherProvider.h"

#include "../../include/core/EngineContract.h"
#include "../core/AppEngineContext.h"

class WeatherEngine : public IEngine {
public:
    static const int MAX_FORECAST_DAYS = 3;

    WeatherEngine();
    ~WeatherEngine();
    
    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override { requestRedraw(); }
    /// The screen is static between slides: it is painted once into each DMA buffer after a change
    /// and not presented again until the next one. Repainting every frame (clear, then draw) let the
    /// panel show the black gap for a few milliseconds after each flip, which read as a flicker in
    /// the light grey of the cloud icon. The runtime's per-frame clear is declined for the same reason.
    bool needsClear() const override { return false; }
    bool hasNewFrame() const override { return m_presented; }

    void addProvider(IWeatherProvider* provider);
    
    String config_api_key;
    String config_city;
    String config_lang;
    String config_units = "metric";
    int config_offset_x = 0;
    int config_offset_y = 0;
    
    void updateWeather(const String& apiKey, const String& city, const String& units = "metric");
    
    bool loop();
    void setCharacter(int characterId);
    void forceUpdate() { lastFetchTime.store(0, std::memory_order_relaxed); }

    bool hasValidData() const { return m_validData.load(std::memory_order_acquire); }
    /// State of the last fetch, for /api/stats: whether data is in hand, how many days, how long
    /// ago the last attempt was, and what stopped it.
    struct FetchState { bool valid; uint8_t days; uint32_t lastAttemptAgeS; char lastError[40]; };
    static FetchState fetchState();

private:
    MatrixPanel_I2S_DMA* matrix;
    std::vector<IWeatherProvider*> providers;
    // The forecast is fetched on a task of its own. Doing it from loop() meant the render path
    // stopped for the length of an HTTPS round trip, which showed as a black panel for a second or
    // more right after the rotation switched to weather.
    TaskHandle_t m_fetchTask = nullptr;
    std::atomic<bool> m_newData{false};
    // The task is asked to leave its loop and is given time to do so, rather than being deleted
    // where it stands: vTaskDelete on a task inside an HTTPS round trip drops the socket and the
    // buffers it holds, and could land while it is still reading the providers this object owns.
    std::atomic<bool> m_stopFetch{false};
    std::atomic<bool> m_fetchExited{false};
    void startFetchTask();
    static void fetchTaskEntry(void* arg);
    void fetchOnce();
    // Two forecast buffers with an atomic index instead of a mutex: Core 0 fills the buffer that is
    // not on screen and then publishes it, so the render loop on Core 1 reads a complete forecast
    // without ever taking a lock (the Core 1 hot path stays mutex-free).
    WeatherData m_forecastBuf[2][MAX_FORECAST_DAYS];
    uint8_t m_forecastCount[2] = {0, 0};
    std::atomic<uint8_t> m_activeBuf{0};
    std::atomic<bool> m_validData{false};
    std::atomic<uint32_t> lastFetchTime{0};
    
    uint16_t textColor;
    uint16_t shadowColor;

    // Cycles through `forecasts` every slideDurationMs, mirroring the RPi's slideshow (simplified:
    // an instant switch instead of the RPi's eased horizontal scroll animation, since redrawing
    // vector icons every frame during a scroll would be significantly more CPU-expensive on an
    // MCU with no GPU/compositing hardware).
    int activeSlide;
    unsigned long lastSlideChange;
    uint8_t m_redrawFrames = 0;   ///< frames left to paint (2 = one per DMA buffer)
    bool m_presented = false;     ///< whether the last update() painted a frame
    void requestRedraw() { m_redrawFrames = 2; }
    static const unsigned long slideDurationMs = 5000;

    void drawIcon(const String& icon, int x, int y, int scale = 1);   ///< 24x24 icon, drawn at `scale`x
    void drawForecast(const WeatherData& data);
};

class WeatherEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
