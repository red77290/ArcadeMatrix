# Guía de Configuración (`config.json` y API)

🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 [Français](CONFIGURATION_FR.md) | 🇪🇸 Español

ArcadeMatrix es totalmente configurable sin recompilar. Todos los parámetros se pueden gestionar desde la Web UI (`http://arcadematrix.local`), la API REST o en el archivo `config.json` de la tarjeta SD.

---

## 📌 Notas Importantes

- **`ROTATION`**: Controla los módulos de visualización (`clock`, `date`, `weather`, `gifs`, `crypto`, `stocks`, `temp`, `decibel`).
- **Precisión de Audio y Visualizador**:
  - `VisualizerEngine` calcula un **pseudo-espectro** basado en bandas de energía de amplitud optimizado para matrices LED.
  - `DecibelEngine` calcula un **indicador relativo de nivel de sonido calibrable**.
