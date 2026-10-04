#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../include/core/EngineContract.h"
#include "mqttdata/DataSession.h"

/**
 * @class MqttDataEngine
 * @brief Pages published over MQTT by any publisher (docs/MQTT_DATA_CONTRACT.md): one page per
 *        subscribed topic, each payload naming its renderer ("value", "table", "graph", "weather")
 *        and its time on screen ("seconds"). The instance only lists the topics.
 *
 * Nothing runs in the background. activate() starts a short-lived task on Core 0 that allocates
 * this activation's buffers (sized for the board: see mqd::limitsFor), connects to the Data MQTT
 * broker, subscribes to the topics and parses each retained payload as it arrives, then keeps
 * updating the pages while the slot is on screen. deactivate() only asks it to stop (Core 1 must
 * not block); the task then disconnects, frees every buffer and deletes itself. Between two
 * activations the engine holds nothing but its settings.
 *
 * The render loop reads the pages through a seqlock (mqd::Session) and never locks or allocates.
 * Static screen: painted into both DMA buffers on a change, on activate() and on resume().
 *
 * The engine owns its time on screen (selfPaced + isFinished, like the GIF engine): one full cycle
 * of its pages, i.e. the sum of their seconds (a page without a payload yet counts 10 s). The
 * rotation slot's own duration is ignored.
 */
class MqttDataEngine : public IEngine {
public:
    MqttDataEngine() = default;
    ~MqttDataEngine() override;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override {}
    void deactivate() override;
    void resume() override { requestRedraw(); }   // back from a preemption or a slot transition
    bool shutdownForDestruction() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override { requestRedraw(); }
    bool isRealtime() const override { return false; }
    bool needsClear() const override { return false; }
    bool hasNewFrame() const override { return m_presented; }
    bool selfPaced() const override { return true; }
    bool isFinished() const override { return millis() - m_activatedAt >= m_cycleMs; }

private:
    enum class Link : uint8_t { Idle, Connecting, Connected, Failed };
    enum class Notice : uint8_t { None, Connecting, NoConnection, NoData, Unsupported };

    MatrixPanel_I2S_DMA* m_matrix = nullptr;
    bool m_hasPsram = false;

    // Settings. The topic list is double-buffered: written on a config change, read by the task.
    char m_topics[2][mqd::MAX_TOPICS][mqd::TOPIC_LEN] = {};
    uint8_t m_topicCount[2] = {0, 0};
    std::atomic<uint8_t> m_topicsIdx{0};
    std::atomic<uint32_t> m_configGen{0};

    // Task lifecycle.
    TaskHandle_t m_task = nullptr;
    std::atomic<bool> m_wantActive{false};
    std::atomic<bool> m_taskAlive{false};
    std::atomic<Link> m_link{Link::Idle};
    std::atomic<uint32_t> m_connectedAt{0};

    // Hand-off: the task publishes the session once it is built and withdraws it before freeing;
    // m_readers lets it wait out a render-loop read in progress.
    mqd::Session m_session;
    std::atomic<mqd::Session*> m_live{nullptr};
    std::atomic<uint8_t> m_readers{0};

    // Render state (Core 1).
    mqd::Cycle m_cycle;
    int m_viewTopic = -1;
    uint32_t m_viewSeq = 0;
    Notice m_notice = Notice::None;
    uint32_t m_activatedAt = 0;
    uint32_t m_cycleMs = mqd::DEFAULT_SECONDS * 1000u;   ///< time on screen this activation (see isFinished)
    uint8_t m_redrawFrames = 0;
    bool m_presented = false;

    void requestRedraw() { m_redrawFrames = 2; }
    void startTask();
    static void taskEntry(void* arg);
    void runTask();
    void runSession();
    void draw(mqd::Session& s);
    void drawNotice(Notice n);
};

class MqttDataEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
