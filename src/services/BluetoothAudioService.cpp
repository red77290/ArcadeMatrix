#include "BluetoothAudioService.h"
#include "../core/Logger.h"
#include "AudioAnalysisService.h"

#if defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

static void a2d_data_cb(const uint8_t *data, uint32_t len) {
    if (!data || len == 0) return;
    const int16_t* pcm = (const int16_t*)data;
    size_t numSamples = len / sizeof(int16_t);
    audioHub.writePCM(AudioSource::BLUETOOTH, pcm, numSamples);
    audioAnalysisService.processSamples(pcm, numSamples);
}

static void a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param) {
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                LOGI("BluetoothAudio", "A2DP device connected!");
                audioHub.requestPlayback(AudioSource::BLUETOOTH);
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                LOGI("BluetoothAudio", "A2DP device disconnected.");
                audioHub.releasePlayback(AudioSource::BLUETOOTH);
            }
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
                audioHub.updateStatus(AudioSource::BLUETOOTH, PlaybackStatus::STATUS_PLAYING);
            } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
                audioHub.updateStatus(AudioSource::BLUETOOTH, PlaybackStatus::STATUS_PAUSED);
            }
            break;
        default:
            break;
    }
}
#endif

BluetoothAudioService bluetoothAudioService;

BluetoothAudioService::BluetoothAudioService()
    : _supported(false), _connected(false), _streaming(false), _deviceName("ArcadeMatrix Audio") {}

BluetoothAudioService::~BluetoothAudioService() {
    stop();
}

bool BluetoothAudioService::begin(const String& deviceName) {
    _deviceName = deviceName.length() > 0 ? deviceName : "ArcadeMatrix Audio";
    const auto& caps = hardwareHAL.capabilities();

    if (!caps.audio.bluetoothClassic) {
        _supported = false;
        LOGD("BluetoothAudio", "Bluetooth Classic A2DP not supported on this SoC profile (BLE-only).");
        return false;
    }

#if defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    esp_err_t err = esp_a2d_sink_init();
    if (err == ESP_OK) {
        esp_a2d_register_callback(a2d_cb);
        esp_a2d_sink_register_data_callback(a2d_data_cb);
        _supported = true;
        LOGI("BluetoothAudio", "Bluetooth A2DP Sink service initialized as \"%s\".", _deviceName.c_str());
        return true;
    } else {
        LOGE("BluetoothAudio", "Failed to initialize A2DP sink: %d", err);
        return false;
    }
#else
    _supported = false;
    return false;
#endif
}

void BluetoothAudioService::stop() {
    if (_streaming) {
        _streaming = false;
        audioHub.updateStatus(AudioSource::BLUETOOTH, PlaybackStatus::STATUS_STOPPED);
        audioHub.releasePlayback(AudioSource::BLUETOOTH);
    }
    _connected = false;
#if defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    if (_supported) {
        esp_a2d_sink_deinit();
    }
#endif
}
