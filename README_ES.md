# ArcadeMatrix

🇬🇧 [English](README.md) | 🇫🇷 [Français](README_FR.md) | 🇪🇸 Español

¡Bienvenido al firmware open source ESP32 para controlar matrices LED HUB75! Este proyecto te permite mostrar relojes Arcade, GIF animados, el tiempo e incluso **sprites de juegos de lucha MUGEN** simulados directamente en una matriz LED real.

📚 **Enlaces de documentación:**
- [Primeros pasos (instalación de PlatformIO, compilación, flasheo, logs)](docs/GETTING_STARTED_ES.md)
- [Web Installer (flasheo desde tu navegador, sin CLI)](webinstaller/README_ES.md) - *estará disponible cuando este repositorio sea público (GitHub Pages requiere un repositorio público en el plan gratuito); hasta entonces, usa el firmware precompilado de abajo.*
- [Guía de hardware](docs/HARDWARE_ES.md)
- [Guía de cableado](docs/WIRING_ES.md)
- [Guía de configuración](docs/CONFIGURATION_ES.md)
- [Guía para desarrolladores](docs/DEVELOPER_ES.md)
- [Arquitectura](docs/ARCHITECTURE_ES.md)

## 💾 Instalación

**[⬇️ Descargar el último firmware precompilado](https://github.com/red77290/ArcadeMatrix/releases/latest)**
(compilado y probado automáticamente por la CI en cada release etiquetada: elige `ArcadeMatrix-esp32dev.zip`
o `ArcadeMatrix-esp32s3.zip` según tu placa, y luego flashea `firmware-*.bin`,
`bootloader-*.bin`, `partitions-*.bin` y `boot_app0.bin` con `esptool.py`; consulta
[Primeros pasos](docs/GETTING_STARTED_ES.md#flashing-a-pre-built-release) para ver los offsets exactos y el
comando. El Web Installer en el navegador de arriba será la opción más sencilla cuando el repositorio sea público.)


## Características
- **Amplia selección de relojes:** relojes animados que incluyen Arcade clásico, Binary, Cyberpunk, Flip, Word, **Pac-Man**, **Tetris**, **SlotMachine** y **Versus (Mugen)**.
- **Interfaz web Wi-Fi:** accede a `http://arcadematrix.local` para subir GIF y cambiar la configuración en vivo.
- **Motor de lucha MUGEN:** simula juegos de lucha 2D de forma nativa en la matriz usando sprites extraídos con una alineación perfecta sobre el suelo virtual.
- **Motor GIF:** reproducción fluida de GIF almacenados en la tarjeta SD.
- **Soporte MQTT:** se integra perfectamente con Batocera y Recalbox para mostrar los marquees de los juegos.

## Estructura de la tarjeta SD
Formatea tu tarjeta SD en **FAT32**. Tu tarjeta SD debería verse así:
```
SD:/
  ├─ conf.ini
  ├─ playlists.json
  ├─ gifs/
  │   └─ mario.gif
  └─ fighters_32/
      ├─ backgrounds/
      │   └─ stage1.raw
      └─ ryu/
          ├─ idle.fgt
          └─ attack.fgt
  └─ fighters_64/
      └─ (misma estructura para paneles de 64px de alto)
```
*Nota: la carpeta `www/` ya no es necesaria en la tarjeta SD, ya que la interfaz web ahora está integrada directamente en el firmware del ESP32.*

## Configuración (`conf.ini`)
El archivo `conf.ini` situado en la raíz de tu tarjeta SD es exhaustivo. Contiene parámetros para el tamaño de la matriz, la profundidad de color, los temas de reloj, el orden de rotación en reposo y los fondos de sprites MUGEN.
Abre el `conf.ini` incluido en la carpeta `release/sdcard/` para ver todos los valores posibles.

## Extracción de sprites MUGEN (script `mugen_extractor.py`)
Para mostrar luchadores en el módulo `SPRITES`, el ESP32 espera archivos brutos `.fgt`. Como el ESP32 no es lo bastante potente para decodificar de forma nativa formatos complejos de personajes MUGEN, proporcionamos un script Python personalizado para convertirlos y generar un manifiesto `index.txt` con cajas englobantes perfectas y valores de suelo virtual.

### Cómo usar el extractor:
1. Asegúrate de tener Python 3 instalado con la biblioteca `Pillow` (`pip install Pillow`), o simplemente ejecuta `tools/mugen_extractor/start_extractor.sh`/`.bat`, que lo instala automáticamente por ti.
2. Ve a la carpeta `tools/mugen_extractor/` dentro del repositorio.
3. Ejecuta el script apuntando `--src` a tu carpeta `chars/` de MUGEN:
   ```bash
   python mugen_extractor.py --src /Ruta/A/Tus/Personajes/Mugen/chars --dest ./fighters_32
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

## Compilación
Para compilar el firmware por tu cuenta, debes usar **PlatformIO**.
- Para 128x32: un ESP32 WROOM estándar es suficiente.
- Para 256x64: se recomienda encarecidamente un **ESP32-S3 con PSRAM** para evitar cuelgues por falta de memoria con doble búfer.

Ejecuta el siguiente comando para compilar:
```bash
pio run -e esp32dev
```
