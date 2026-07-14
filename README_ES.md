# ArcadeMatrix

¡Bienvenido al firmware de código abierto ESP32 para controlar Matrices LED HUB75! Este proyecto te permite mostrar Relojes Arcade, GIFs animados, el Tiempo, ¡e incluso simular **sprites de juegos de lucha MUGEN** directamente en una matriz LED real!

📚 **Enlaces a la Documentación:**
- [Guía de Hardware](docs/HARDWARE.md)
- [Guía de Cableado](docs/WIRING.md)
- [Guía de Configuración](docs/CONFIGURATION.md)

## Características
- **Selección Masiva de Relojes:** Relojes animados incluyendo el clásico Arcade, Binario, Cyberpunk, Flip, Palabras, **Pac-Man**, **Tetris**, **SlotMachine** (Tragamonedas), y **Versus (Mugen)**.
- **Interfaz Web Wi-Fi:** ¡Accede a `http://arcadematrix.local` para subir GIFs y cambiar la configuración en vivo!
- **Motor MUGEN Fighter:** Simula juegos de lucha en 2D de forma nativa en la matriz utilizando sprites extraídos con una alineación perfecta de suelo virtual.
- **Motor de GIF:** Reproducción suave de GIFs almacenados en la tarjeta SD.
- **Soporte MQTT:** Se integra a la perfección con Batocera y Recalbox para mostrar las carpas (marques) de los juegos.

## Estructura de la Tarjeta SD
Formatea tu tarjeta SD a **FAT32**. Tu tarjeta SD debería verse así:
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
*Nota: ¡La carpeta `www/` ya no es necesaria en la tarjeta SD ya que la Interfaz Web ahora está integrada directamente en el firmware del ESP32!*

## Configuración (`conf.ini`)
El archivo `conf.ini` situado en la raíz de tu tarjeta SD es exhaustivo. Contiene parámetros para el tamaño de la Matriz, profundidad de color, temas de reloj, orden de rotación en reposo y fondos para los sprites MUGEN.
Abre el archivo `conf.ini` proporcionado en la carpeta `release/sdcard/` para ver todos los valores posibles.

## Extracción de Sprites MUGEN (El script `mugen_extractor.py`)
Para mostrar luchadores en el módulo `SPRITES`, el ESP32 requiere archivos en bruto `.fgt`. Dado que el ESP32 no es lo suficientemente potente para decodificar formatos complejos de personajes MUGEN de forma nativa, proporcionamos un script en Python personalizado para convertirlos y generar un manifiesto `index.txt` que contiene las cajas de colisión (bounding boxes) perfectas y los valores de suelo virtual.

### Cómo usar el extractor:
1. Asegúrate de tener Python 3 instalado con la librería `Pillow` (`pip install Pillow`).
2. Ve a la carpeta `tools/mugen_extractor/` en el repositorio.
3. Edita el archivo `mugen_extractor.py` para configurar `src_dir` apuntando a tu carpeta `chars/` de MUGEN.
4. Ejecuta el script:
   ```bash
   python mugen_extractor.py
   ```
5. El script generará automáticamente archivos `.fgt` junto con un manifiesto `index.txt` para todos los personajes, escalados perfectamente para matrices de 32px y 64px.
6. Copia la carpeta resultante `fighters_32/` o `fighters_64/` a tu tarjeta SD.

Para más detalles, por favor lee la documentación dentro de `tools/mugen_extractor/README_ES.md`.

### Fondos de Sprites
¡Los luchadores necesitan una arena! Puedes definir el fondo en el que luchan colocando un archivo de imagen en bruto (ej., `stage1.raw`) en `SD:/fighters_32/backgrounds/`.
Luego, vincula este fondo en tu `conf.ini` bajo la sección `[DATE]` (¡los fondos se usan para animar el módulo de la fecha!):
```ini
BACKGROUND_SPRITE=stage1.raw
```

## Compilación
Para compilar el firmware tú mismo, debes utilizar **PlatformIO**.
- Para 128x32: Un ESP32 WROOM estándar es suficiente.
- Para 256x64: Un **ESP32-S3 con PSRAM** es muy recomendable para evitar bloqueos por falta de memoria (Out-Of-Memory) con el doble búfer.

Ejecuta el siguiente comando para compilar:
```bash
pio run -e esp32dev
```
