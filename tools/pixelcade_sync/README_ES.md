# pixelcade_sync

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

Herramienta one-shot del lado del PC que descarga el repositorio de artwork marquee de [Pixelcade](https://github.com/alinke/pixelcade)
y lo organiza listo para copiarlo a tu tarjeta SD de ArcadeMatrix, para que el ESP32 nunca necesite acceso a internet en vivo para mostrar box-art / marquees durante una partida.

## ¿Por qué no obtenerlo en vivo desde el ESP32?

`ArcadeMatrix_RPi` (el proyecto hermano para Raspberry Pi) descarga imágenes de Pixelcade bajo demanda
(`core/dmd_cache.py`), guardándolas en caché en disco tras la primera descarga. El ESP32 no puede razonablemente hacer lo mismo:
- No hay presupuesto sobrante de flash / RAM para un cliente HTTPS+TLS que obtenga imágenes en mitad de una partida, junto al driver DMA de la matriz.
- No hay caché de sistema de archivos lo bastante grande para crecer sin límite, ni lógica de expulsión de caché que merezca esa complejidad en un microcontrolador.

En su lugar, `RetroFrontendListener` en el ESP32 espera que el artwork ya esté presente en
`/pixelcade/<system>/<game>.png` dentro de la tarjeta SD. Este script rellena esa carpeta una vez (offline,
en un PC real), tú la copias a la tarjeta SD y el firmware simplemente hace un `SD.exists()` rápido +
`gif->playGif()` en tiempo de ejecución. Ningún viaje de red cuando arranca un juego.

## Uso

Sin Python, sin instalaciones de terceros, sin nada que descargar: solo el script y las herramientas que tu SO
ya trae. Elige la variante que encaje con tu plataforma:

### macOS / Linux

```bash
# Sync every system Pixelcade has artwork for (large - several hundred MB)
./pixelcade_sync.sh

# Only sync the systems you actually use (recommended - much faster/smaller)
./pixelcade_sync.sh mame,snes,nes,gba

# Custom output location
DEST=./sdcard/pixelcade ./pixelcade_sync.sh mame
```

Solo requiere `curl` (o `wget`) y `unzip`, que ya vienen instalados en prácticamente cualquier equipo
Mac/Linux. Si falta alguno, el script te dirá exactamente qué instalar y cómo
(por ejemplo `brew install unzip curl` / `sudo apt install unzip curl`).

### Windows

```powershell
# Sync every system Pixelcade has artwork for (large - several hundred MB)
.\pixelcade_sync.ps1

# Only sync the systems you actually use (recommended - much faster/smaller)
.\pixelcade_sync.ps1 -Systems mame,snes,nes,gba

# Custom output location
.\pixelcade_sync.ps1 -Dest D:\sdcard\pixelcade -Systems mame
```

Solo requiere lo que viene integrado en Windows 10/11 (PowerShell 5+, `Invoke-WebRequest`,
`Expand-Archive`): no hace falta instalar nada. Si tu PowerShell es demasiado antiguo o le falta algo, el
script te indicará exactamente qué falta y cómo solucionarlo. Si la execution policy lo bloquea,
ejecútalo así: `powershell -ExecutionPolicy Bypass -File .\pixelcade_sync.ps1`

## Aplicarlo a tu tarjeta SD

Copia el contenido de la carpeta de salida a la raíz de tu tarjeta SD ArcadeMatrix, para terminar
con rutas como:

```
/pixelcade/mame/pacman.png
/pixelcade/snes/super_mario_world.png
```

Los nombres de carpeta de sistema (`mame`, `snes`, `nes`, ...) coinciden con la estructura del propio repositorio Pixelcade, y
`RetroFrontendListener::mapSystemToPixelcadeFolder()` (lado firmware) mapea los valores `SystemId` de Recalbox/Batocera
(por ejemplo `fbneo`, `megadrive`) a esos mismos nombres de carpeta, mantenidos en sincronía con el `SYSTEM_MAP` de
`ArcadeMatrix_RPi/core/dmd_cache.py`. Si añades un sistema allí, refleja el cambio en ambos lugares.

## Volver a ejecutarlo / mantener el artwork al día

El repositorio de Pixelcade crece con el tiempo a medida que más juegos reciben artwork. Basta con volver a ejecutar este script periódicamente (descarga de nuevo el snapshot completo cada vez; no hay modo incremental / diff) y volver a copiar la salida a tu tarjeta SD para obtener los nuevos juegos.
