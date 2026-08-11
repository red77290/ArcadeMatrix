#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "../include/HardwareProfile.h"

/**
 * @struct EnvironmentData
 * @brief Structure contenant les données environnementales (température et humidité).
 */
struct EnvironmentData {
    bool available;      ///< Vrai si le capteur physique a répondu avec succès
    float temperatureC;  ///< Température en degrés Celsius
    float temperatureF;  ///< Température en degrés Fahrenheit
    float humidity;      ///< Humidité relative en pourcentage (0-100%)
};

/**
 * @class HardwareHAL
 * @brief Couche d'abstraction matérielle (HAL) pour les capteurs et l'audio d'ArcadeMatrix.
 */
class HardwareHAL {
public:
    HardwareHAL();
    ~HardwareHAL();

    /**
     * @brief Initialise les bus I2C et I2S, et effectue l'auto-détection du matériel.
     */
    void begin();

    // --- Capteur Environnemental (Température / Humidité) ---
    /**
     * @brief Indique si un capteur environnemental valide a été détecté sur le bus I2C.
     */
    bool isTempSensorAvailable() const { return tempSensorDetected; }

    /**
     * @brief Lit les données environnementales (Celsius, Fahrenheit, Humidité).
     * @param tempOffset Offset de calibration en °C
     * @return EnvironmentData contenant les valeurs et le statut.
     */
    EnvironmentData readEnvironment(float tempOffset = 0.0f);

    // --- Microphone & Entrée Audio (I2S DMA) ---
    /**
     * @brief Indique si le périphérique audio / microphone est disponible et fonctionnel.
     */
    bool isAudioAvailable() const { return audioDetected; }

    /**
     * @brief Active l'échantillonnage audio I2S DMA à la demande (Lazy Sampling).
     */
    void startAudioSampling();

    /**
     * @brief Désactive l'échantillonnage audio I2S DMA pour libérer le CPU/DMA.
     */
    void stopAudioSampling();

    /**
     * @brief Indique si l'échantillonnage audio I2S est en cours.
     */
    bool isAudioSamplingActive() const { return audioActive; }

    /**
     * @brief Calcule et retourne le niveau sonore actuel en décibels (dB SPL).
     * @param dbCalibration Offset de calibration en dB
     */
    float getDecibels(float dbCalibration = 0.0f);

    /**
     * @brief Remplit un tableau d'amplitudes de bandes de fréquence FFT (Visualiseur).
     * @param bands Tableau cible de taille numBands
     * @param numBands Nombre de bandes souhaité (ex: 16, 32, 64)
     * @return true si les bandes ont été remplies avec succès
     */
    bool getAudioSpectrum(float* bands, size_t numBands);

    /**
     * @brief Définit le gain du microphone.
     */
    void setMicGain(float gain) { micGain = (gain > 0.0f) ? gain : 1.0f; }

    /**
     * @brief Retourne le gain actuel du microphone.
     */
    float getMicGain() const { return micGain; }

private:
    bool tempSensorDetected;
    bool audioDetected;
    bool audioActive;
    float micGain;

    uint32_t lastTempReadTime;
    EnvironmentData cachedEnvData;

    bool probeSHTC3();
    bool probeES7210();
    bool readSHTC3Raw(float& tempC, float& hum);
    static uint8_t calcSensirionCRC8(const uint8_t* data, uint8_t len);
};

extern HardwareHAL hardwareHAL;
