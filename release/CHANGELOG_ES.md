# Changelog

🇬🇧 [English](CHANGELOG.md) | 🇫🇷 [Français](CHANGELOG_FR.md) | 🇪🇸 Español

## [1.1.0] - 2026-07-09

### Añadido
- **12 nuevas fuentes/temas:** integración completa de 12 temas icónicos de publishers (Nintendo, Capcom, Sega, SNK, Taito, etc.) aplicables tanto a los módulos Clock como Date.
- **Offsets de precisión (X/Y):** se añadieron nuevas configuraciones de offset X e Y a través de la interfaz web para los módulos Clock, Date y Weather, permitiendo un posicionamiento manual al píxel.
- **Ajustes de rotación en vivo:** los cambios en las duraciones de rotación (Idle Settings) y en la configuración de Weather (API/City) ahora se aplican al instante sin necesidad de reiniciar.
- **Actualizaciones de zona horaria en vivo:** al actualizar el ajuste Timezone ahora se sincroniza inmediatamente la RTC del ESP32.
- **Múltiples tamaños:** se añadió la selección Clock Size y Date Size (tamaño 1 a 3) en la interfaz web.

### Corregido
- **Reloj saltándose segundos:** se rediseñó el bucle de seguimiento del tiempo para leer directamente la RTC de hardware en cada frame (33ms) en lugar de depender de retrasos bloqueantes con `millis()`, logrando segundos perfectamente fluidos.
- **Ghosting LED (píxeles verdes):** se añadió `latch_blanking = 4` a la configuración HUB75 DMA, eliminando artefactos visuales y parpadeos verdes en las esquinas de la matriz en paneles más rápidos.
- **Bug de las casillas de playlist:** se corrigió un fallo por el que al marcar una playlist de sprites en la UI se desmarcaban otras, resolviendo discrepancias de estado en el DOM.
- **Botón Save ausente:** se añadió la lógica que faltaba para el botón "Save Clock & Date" en el panel unificado de ajustes Clock/Date.
- **Resiliencia de formularios:** ahora se evita que valores `NaN` bloqueen o contaminen la configuración JSON cuando se dejan en blanco campos de offset en la interfaz web.

## [1.0.0] - 2024-07-07

### Añadido
- **Web Dashboard (Vite/VanillaJS):** una interfaz web completa, estética y responsive para controlar la matriz por Wi-Fi sin volver a flashear.
- **API REST:** servidor web asíncrono en C++ (`WebServerAPI.cpp`) para gestionar todos los endpoints JSON de las peticiones.
- **Clock Engine:** arquitectura OOP para manejar relojes estándar (Word Clock, Cyberpunk) y relojes retro arcade interactivos (Mario, Ryu, Mega Man) activados con los cambios de minuto.
- **Integración MQTT con Batocera y Recalbox:** escucha `batocera/events` y `/Recalbox/EmulationStation/Event` para disparar instantáneamente GIF específicos de cada juego.
- **Mensajes marquee personalizados:** `MessageEngine` permite escribir texto desplazable personalizado desde la interfaz web con controles de color, tamaño, velocidad y dirección.
- **Scripts nativos de indexación JSON:** `generate_index.sh` (Mac/Linux) y `generate_index.ps1` (Windows) para indexar al instante el contenido de la tarjeta SD sin necesidad de Python.
- **Modo noche / standby:** programación configurable de apagado/encendido para ahorrar energía y LEDs.
- **Assets precompilados:** `firmware.bin` listo para usar y bundle `www` para la tarjeta SD pensado para usuarios finales sin entorno de desarrollo.

### Cambiado
- Reescritura completa desde el archivo monolítico `.ino` hacia archivos C++ modulares y orientados a objetos.
- La estructura de la tarjeta SD ahora espera los archivos de la interfaz web en `/www/` para ahorrar memoria flash del ESP32.

### Eliminado
- Requisito de usar un script Python para generar el índice (sustituido por scripts Shell / PowerShell nativos).
