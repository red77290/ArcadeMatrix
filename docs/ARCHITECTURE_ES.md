# Visión General de la Arquitectura (ESP32)

🇬🇧 [English](ARCHITECTURE.md) | 🇫🇷 [Français](ARCHITECTURE_FR.md) | 🇪🇸 Español

Este documento proporciona una visión general completa de la arquitectura de ArcadeMatrix para el microcontrolador ESP32.

---

## 1. Filosofía Principal: Restricciones de Hardware

La versión ESP32 está desarrollada en **C++** y diseñada con estrictas restricciones de hardware:
- **Límites de RAM (320KB interna):** Rendu directo en buffer DMA sin capas pesadas.
- **CPU (240MHz):** Rendu rápido para mantener 60 FPS.

---

## 2. Capa de Abstracción de Hardware y Coordinación Sensor/Audio

`HardwareHAL` actúa como un envoltorio centralizado de hardware que gestiona los buses periféricos y la disponibilidad de sensores:

```text
                    ┌───────────────────┐
                    │   HardwareHAL     │
                    └─────────┬─────────┘
                              │
             ┌────────────────┼────────────────┐
             │                │                │
             ▼                ▼                ▼
          HUB75             I2C             I2S
          Matriz            SHTC3           ES7210
             │                │                │
             ▼                ▼                ▼
          Renders         TempEngine     Muestreo de audio
                                               │
                                    ┌──────────┴─────────┐
                                    ▼                    ▼
                              DecibelEngine       VisualizerEngine

RotationManager
      │
      ├── TEMP ──► requiere sensor SHTC3 (se omite si falta)
      └── DECIBEL ─► requiere muestreo audio (se omite si falta)
```

### Coordinación de Ciclo de Vida
- **Temperatura y Humedad I2C (SHTC3):** `HardwareHAL` detecta el sensor. Si no está presente, `RotationManager` omite el módulo de temperatura.
- **Audio I2S (ES7210 / Micrófono):** El muestreo de audio es compartido entre `DecibelEngine` y `VisualizerEngine`.
- **Visualizador:** Modelo de pseudo-espectro basado en bandas de energía de amplitud optimizado para matrices LED.
- **Indicador de Decibelios:** Indicador relativo de nivel de sonido calibrable.
