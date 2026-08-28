#include "BluetoothAudioService.h"
#include "../core/Logger.h"

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

    _supported = true;
    LOGI("BluetoothAudio", "Bluetooth A2DP Sink service initialized as \"%s\".", _deviceName.c_str());
    return true;
}

void BluetoothAudioService::stop() {
    if (_streaming) {
        _streaming = false;
        audioHub.updateStatus(AudioSource::BLUETOOTH, PlaybackStatus::STATUS_STOPPED);
        audioHub.releasePlayback(AudioSource::BLUETOOTH);
    }
    _connected = false;
}
