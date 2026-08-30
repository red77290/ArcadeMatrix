#include "BluetoothAudioService.h"
#include "../core/Logger.h"
#include "AudioAnalysisService.h"

#if defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_system.h"
#include "esp_mac.h"

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
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macSuffix[10];
    snprintf(macSuffix, sizeof(macSuffix), "-%02X%02X", mac[4], mac[5]);

    if (deviceName.length() > 0 && deviceName != "ArcadeMatrix Audio" && deviceName != "ArcadeMatrix") {
        _deviceName = deviceName;
    } else {
        _deviceName = String("ArcadeMatrix Audio") + macSuffix;
    }

    const auto& caps = hardwareHAL.capabilities();
    if (!caps.audio.bluetoothClassic) {
        _supported = false;
        LOGD("BluetoothAudio", "Bluetooth Classic A2DP not supported on this SoC profile (BLE-only).");
        return false;
    }

#if defined(CONFIG_IDF_TARGET_ESP32) && !defined(CONFIG_IDF_TARGET_ESP32S3)
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_init(&bt_cfg);
        esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    }
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        esp_bluedroid_init();
        esp_bluedroid_enable();
    }

    esp_bt_dev_set_device_name(_deviceName.c_str());

    esp_err_t err = esp_a2d_sink_init();
    if (err == ESP_OK) {
        esp_a2d_register_callback(a2d_cb);
        esp_a2d_sink_register_data_callback(a2d_data_cb);
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        _supported = true;
        LOGI("BluetoothAudio", "Bluetooth A2DP Sink service initialized and discoverable as \"%s\".", _deviceName.c_str());
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
