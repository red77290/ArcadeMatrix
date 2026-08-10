# ArcadeMatrix

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

¡Bienvenido al firmware open source ESP32 para controlar matrices LED HUB75! Este proyecto te permite mostrar relojes Arcade, GIF animados, el tiempo e incluso **sprites de juegos de lucha MUGEN** simulados directamente en una matriz LED real.

## 💾 Instalación

**[⬇️ Descargar el último firmware precompilado](https://github.com/red77290/ArcadeMatrix/releases/latest)**
(compilado y probado automáticamente por la CI en cada release etiquetada: elige `ArcadeMatrix-esp32dev.zip`
o `ArcadeMatrix-esp32s3.zip` según tu placa, y luego flashea `firmware-*.bin`,
`bootloader-*.bin`, `partitions-*.bin` y `boot_app0.bin` con `esptool.py`; consulta
[Primeros pasos](docs/GETTING_STARTED_ES.md#flashing-a-pre-built-release) para ver los offsets exactos y el
comando. El Web Installer en el navegador de arriba será la opción más sencilla cuando el repositorio sea público.
También descarga `ArcadeMatrix-sdcard.zip` de la misma release - un kit de inicio de tarjeta SD
listo para copiar con un `conf.ini` de ejemplo, las carpetas de GIFs/MUGEN, y los scripts de
indexación de playlists GIF.)


## Características
- **Amplia selección de relojes:** relojes animados que incluyen Arcade clásico, Binary, Cyberpunk, Flip, Word, **Pac-Man**, **Tetris**, **SlotMachine**, **MatrixRain** y **Versus (Mugen)**.
- **Tickers de Criptomonedas y Bolsa en tiempo real:** cotizaciones en vivo y distintivos % 24h de CoinGecko, Binance y Yahoo Finance con caché configurable.
- **Interfaz web Wi-Fi:** accede a `http://arcadematrix.local` para subir GIF y cambiar la configuración en vivo.
- **Motor de Pelea MUGEN:** ¡Simula nativamente juegos de pelea 2D en la matriz usando sprites extraídos con una alineación perfecta en el suelo virtual!
- **Motor GIF:** Reproducción fluida de GIFs almacenados en la tarjeta SD.
- **Clima (OpenWeatherMap):** Pronóstico de 3 días, con una caché de búsqueda de 15 minutos para ahorrar llamadas a la API.
- **Soporte MQTT:** Se integra perfectamente con Batocera y Recalbox para mostrar marquesinas de juegos.
- **Actualizaciones OTA:** Flashea actualizaciones de firmware de forma inalámbrica directamente a través de la Web UI.
 - **Soporte ESP32-S3 Waveshare:** Soporte completo para placas ESP32-S3 de gama alta y paneles 256x64 True Matrix mediante DMA.

## Estructura de la tarjeta SD
Formatea tu tarjeta SD en **FAT32** o **exFAT**. Tu tarjeta SD debería verse así:
```
SD:/
  ├─ conf.ini
  ├─ gifs/
  │  │   └─ mario.gif
  └─ fighters_32/
      ├─ backgrounds/
      │   └─ stage1.raw
      └─ ryu/
          ├─ idle.fgt
          └─ attack.fgt
  └─ fighters_64/
      ├─ (misma estructura para paneles de 64px de alto)
```
*Nota: la carpeta `www/` ya no es necesaria en la tarjeta SD, ya que la interfaz web ahora está integrada directamente en el firmware del ESP32.*

## Configuración (`conf.ini`)
El archivo `conf.ini` situado en la raíz de tu tarjeta SD es exhaustivo. Contiene parámetros para el tamaño de la matriz, la profundidad de color, los temas de reloj, el orden de rotación en reposo y los fondos de sprites MUGEN.
Abre el `conf.ini` incluido en la carpeta `release/sdCard/` para ver todos los valores posibles.

## Extracción de sprites MUGEN (script `mugen_extractor.py`)
Para mostrar luchadores en el módulo `SPRITES`, el ESP32 espera archivos brutos `.fgt`. Como el ESP32 no es lo bastante potente para decodificar de forma nativa formatos complejos de personajes MUGEN, proporcionamos un script Python personalizado para convertirlos y generar un manifiesto `index.txt` con cajas englobantes perfectas y valores de suelo virtual.

### Cómo usar el extractor:
1. Asegúrate de tener Python 3 instalado con la biblioteca `Pillow` (`pip install Pillow`), o simplemente ejecuta `tools/mugen_extractor/start_extractor.sh`/`.bat`, que lo instala automáticamente por ti.
2. Ve a la carpeta `tools/mugen_extractor/` dentro del repositorio.
3. Ejecuta el script apuntando `--src` a tu carpeta `chars/` de MUGEN:
   ```bash
   python mugen_extractor.py --src /Ruta/A/Tus/Personajes/Mugen/chars --dest ./fighters_32
   # O con un factor de escala personalizado (ej: --scale 0.5 para reducir al 50% ahorrando 75% de RAM):
   python mugen_extractor.py --src /Ruta/A/Tus/Personajes/Mugen/chars --dest ./fighters_64 --scale 0.5
   ```
4. El script genera los archivos `.fgt` junto con un manifiesto `index.txt`/`index.json` en la carpeta `--dest`. Ejecútalo dos veces (con `--dest ./fighters_32` y `--dest ./fighters_64`) si quieres assets para ambos tamaños de matriz.
5. Copia la carpeta resultante `fighters_32/` o `fighters_64/` a tu tarjeta SD.

Para ver todos los detalles, consulta la documentación en `tools/mugen_extractor/README_ES.md`.

### Fondos de sprites
¡Los luchadores necesitan una arena! Puedes definir el fondo en el que luchan colocando un archivo de imagen bruto (por ejemplo, `stage1.raw`) en `SD:/fighters_32/backgrounds/`.
Luego, vincula este fondo en tu `conf.ini` bajo la sección `[DATE]` (¡los fondos sirven para dar más vida al módulo de fecha!):
```ini
BACKGROUND_SPRITE=stage1.raw
```

## Indexación de playlists GIF (selección de carpetas en la Web UI)
La Web UI te permite marcar/desmarcar qué subcarpetas de `gifs/` se reproducen durante la rotación en reposo, pero necesita un manifiesto `playlists.json` para saber qué hay en la tarjeta SD. La reproducción de GIF funciona perfectamente sin él (el motor siempre lee los archivos directamente desde la tarjeta SD) - este paso solo es necesario si quieres usar ese selector de casillas.

1. Organiza tus GIF en subcarpetas dentro de `gifs/` en tu tarjeta SD, por ejemplo `gifs/mario/`, `gifs/sonic/` (cada subcarpeta se convierte en una playlist seleccionable; los archivos `.gif` sueltos directamente en `gifs/` siempre se reproducen y no necesitan este paso).
2. Ejecuta uno de los scripts nativos en `tools/gif_indexation/` - sin necesidad de Python:
   ```bash
   ./generate_index.sh /Volumes/SDCARD      # macOS/Linux - pasa la raíz de la SD o su carpeta gifs/
   ```
   ```powershell
   .\generate_index.ps1 -Path E:\           # Windows
   ```
3. Esto crea `gifs/playlists.json` en la tarjeta SD. Vuelve a ejecutarlo cada vez que agregues, elimines o renombres una carpeta dentro de `gifs/`.

Para ver todos los detalles, consulta `tools/gif_indexation/README_ES.md`.

## Fuentes personalizadas (conversión BDF → AMF)
El Reloj, la Fecha y el mensaje desplazante pueden usar fuentes bitmap personalizadas cargadas desde la tarjeta SD en lugar de las ~6 fuentes compiladas en el firmware, usando las mismas fuentes `.bdf` que `ArcadeMatrix_RPi` ya incluye. Sin embargo, el ESP32 no tiene un analizador BDF a bordo, por lo que primero deben convertirse al formato compacto `.amf`.

1. Copia tu(s) fuente(s) `.bdf` en la carpeta `fonts/` de tu tarjeta SD.
2. Ejecuta el convertidor por lotes:
   ```bash
   python3 tools/bdf_to_amfont/bdf_to_amfont.py /Volumes/SDCARD   # pasa la raíz de la SD o su carpeta fonts/
   ```
   (No requiere dependencias externas. Solo Python estándar.)
3. Esto convierte cada `.bdf` en un `.amf` del mismo nombre en el mismo lugar. Las fuentes resultantes aparecen de inmediato en la página de Configuración de la interfaz web (menús desplegables "Font" de Reloj/Fecha) - sin necesidad de reiniciar.

Para todos los detalles, revisa `tools/bdf_to_amfont/README_ES.md`.

## Compilación
Para compilar el firmware por tu cuenta, debes usar **PlatformIO**.
- Para 128x32: un ESP32 WROOM estándar es suficiente.
- Para 256x64: se recomienda encarecidamente un **ESP32-S3 con PSRAM** para evitar cuelgues por falta de memoria con doble búfer.

Ejecuta el siguiente comando para compilar:
```bash
pio run -e esp32dev
```

## 📚 Documentación adicional
- [Primeros pasos (instalación de PlatformIO, compilación, flasheo, logs)](docs/GETTING_STARTED_ES.md)
- [Web Installer (flasheo desde tu navegador, sin CLI)](webinstaller/README_ES.md) - *estará disponible cuando este repositorio sea público (GitHub Pages requiere un repositorio público en el plan gratuito); hasta entonces, usa el firmware precompilado de arriba.*
- [Guía de hardware](docs/HARDWARE_ES.md)
- [Guía de cableado](docs/WIRING_ES.md)
- [Guía de configuración](docs/CONFIGURATION_ES.md)
- [Guía para desarrolladores](docs/DEVELOPER_ES.md)
- [Arquitectura](docs/ARCHITECTURE_ES.md)

## 📜 Licencia
Este proyecto está licenciado bajo la **[PolyForm Noncommercial License 1.0.0](LICENSE)**.

**En resumen:** eres libre de usar, modificar y compartir este proyecto para cualquier propósito no comercial (uso personal, proyectos hobbyistas, investigación, educación, organizaciones públicas/sin fines de lucro) - consulta el archivo [LICENSE](LICENSE) completo para los términos exactos. **Cualquier uso comercial (venta de unidades ensambladas, kits, o productos/servicios derivados) requiere una licencia separada - contacta a [Red1L](https://github.com/red77290) para discutir los términos comerciales.**
