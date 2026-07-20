# Guía de configuración (`conf.ini`)

🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 [Français](CONFIGURATION_FR.md) | 🇪🇸 Español

ArcadeMatrix está diseñado para estar completamente desacoplado de su código C++ de cara al usuario final. Todos los ajustes se gestionan ya sea mediante la interfaz web (en `http://arcadematrix.local`) o directamente a través del archivo `conf.ini` en la raíz de tu tarjeta SD.

## Referencia completa
La lista exhaustiva de parámetros de `conf.ini` se incluye en la carpeta `release/sdCard/`. Abre ese archivo para ver todos los valores disponibles y sus comentarios.

### Secciones clave:
- `[WIFI]`: SSID, contraseña y hostname mDNS para acceder a la interfaz web.
- `[MATRIX]`: mapeo de geometría (Width, Height, Chain count), profundidad de color y buferización. **Ajusta estos valores si tu matriz aparece distorsionada.** `PANEL_TYPE` se acepta y se guarda, pero actualmente no tiene efecto en el driver (mapeo de pines único y codificado para todos los tipos de panel - ver `docs/HARDWARE_ES.md`).
- `[MQTT]`: dirección IP de tu Batocera/Recalbox para la sincronización Live Marquee.
- `[TIME]`: zona horaria, opciones de diseño del reloj, y colores de degradado `clock_color_1`/`clock_color_2`.
- `[IDLE]`: secuencia de módulos que se reproducen cuando no está ocurriendo nada (Clock, Date, Weather, GIFs, Sprites), incluyendo `fighter_interval_sec` (retraso entre combates MUGEN).
- `[DATE]`: fondos de sprites, formato, y colores de degradado `date_color_1`/`date_color_2` del módulo de fecha.
- `[WEATHER]`: claves API de OpenWeatherMap.
- `[STANDBY]`: temporizadores de ahorro de energía del modo nocturno y nivel `night_brightness` (ponlo a 0 para apagar completamente el panel por la noche).
- `[FONTS]`: `custom_font_path` opcional hacia una fuente bitmap `.amf` cargada desde la tarjeta SD (ver `tools/bdf_to_amfont/README_ES.md`).
