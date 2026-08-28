#pragma once
#include <Arduino.h>
#include "../core/AudioHub.h"
#include "../hal/HardwareHAL.h"

/**
 * @class BluetoothAudioService
 * @brief Autonomous background Bluetooth A2DP Audio Sink and AVRCP Metadata Service.
 * Allows phones/laptops to stream audio directly to ArcadeMatrix speaker.
 */
class BluetoothAudioService {
public:
    BluetoothAudioService();
    ~BluetoothAudioService();

    /**
     * @brief Starts Bluetooth A2DP Sink service if supported on this hardware.
     * @param deviceName Bluetooth advertised name (default: "ArcadeMatrix Audio")
     */
    bool begin(const String& deviceName = "ArcadeMatrix Audio");

    /**
     * @brief Stops Bluetooth service.
     */
    void stop();

    /**
     * @brief Returns whether a Bluetooth device is currently connected.
     */
    bool isConnected() const { return _connected; }

    /**
     * @brief Returns whether Bluetooth audio is actively streaming.
     */
    bool isStreaming() const { return _streaming; }

    /**
     * @brief Returns whether Bluetooth Classic A2DP is supported on this target.
     */
    bool isSupported() const { return _supported; }

private:
    bool _supported;
    bool _connected;
    bool _streaming;
    String _deviceName;
};

extern BluetoothAudioService bluetoothAudioService;
