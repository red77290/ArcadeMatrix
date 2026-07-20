# Guía de configuración (`conf.ini`)

🇬🇧 [English](CONFIGURATION.md) | 🇫🇷 [Français](CONFIGURATION_FR.md) | 🇪🇸 Español

ArcadeMatrix está diseñado para estar completamente desacoplado de su código C++ de cara al usuario final. Todos los ajustes se gestionan ya sea mediante la interfaz web (en `http://arcadematrix.local`) o directamente a través del archivo `conf.ini` en la raíz de tu tarjeta SD.

## Referencia completa
La lista exhaustiva de parámetros de `conf.ini` se incluye en la carpeta `release/sdcard/`. Abre ese archivo para ver todos los valores disponibles y sus comentarios.

### Secciones clave:
- `[WIFI]`: SSID, contraseña y hostname mDNS para acceder a la interfaz web.
- `[MATRIX]`: mapeo de geometría (Width, Height, Panel Type, Chain count). **Ajusta estos valores si tu matriz aparece distorsionada.**
- `[MQTT]`: dirección IP de tu Batocera/Recalbox para la sincronización Live Marquee.
- `[TIME]`: zona horaria y opciones de diseño del reloj.
- `[IDLE]`: secuencia de módulos que se reproducen cuando no está ocurriendo nada (Clock, Date, Weather, GIFs, Sprites).
- `[DATE]`: fondos de sprites y formato del módulo de fecha.
- `[WEATHER]`: claves API de OpenWeatherMap.
- `[STANDBY]`: temporizadores de ahorro de energía del modo nocturno y nivel `night_brightness` (ponlo a 0 para apagar completamente el panel por la noche).
